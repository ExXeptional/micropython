/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2019 Damien P. George
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <string.h>

#include "py/runtime.h"
#include "py/mphal.h"
#include "lib/oofatfs/ff.h"
#include "lib/oofatfs/diskio.h"
#include "extmod/vfs_fat.h"
#include "py/mperrno.h"

#include "inc/hw_memmap.h"
#include "driverlib/pin_map.h"
#include "driverlib/gpio.h"
#include "driverlib/rom.h"
#include "driverlib/rom_map.h"
#include "driverlib/ssi.h"
#include "driverlib/sysctl.h"

#include "mpconfigboard.h"
#include "sdcard.h"
#include "pin.h"
#include "bufhelper.h"
// #include "dma.h"
#include "irq.h"

#if MICROPY_HW_HAS_SDCARD

#define MICROPY_HW_SDCARD_DETECT_PRESENT (1)

/* Definitions for MMC/SDC command */
#define CMD0    (0x40+0)    /* GO_IDLE_STATE */
#define CMD1    (0x40+1)    /* SEND_OP_COND */
#define CMD8    (0x40+8)    /* SEND_IF_COND */
#define CMD9    (0x40+9)    /* SEND_CSD */
#define CMD10    (0x40+10)    /* SEND_CID */
#define CMD12    (0x40+12)    /* STOP_TRANSMISSION */
#define CMD16    (0x40+16)    /* SET_BLOCKLEN */
#define CMD17    (0x40+17)    /* READ_SINGLE_BLOCK */
#define CMD18    (0x40+18)    /* READ_MULTIPLE_BLOCK */
#define CMD23    (0x40+23)    /* SET_BLOCK_COUNT */
#define CMD24    (0x40+24)    /* WRITE_BLOCK */
#define CMD25    (0x40+25)    /* WRITE_MULTIPLE_BLOCK */
#define CMD41    (0x40+41)    /* SEND_OP_COND (ACMD) */
#define CMD55    (0x40+55)    /* APP_CMD */
#define CMD58    (0x40+58)    /* READ_OCR */

// SSI port
#define SDC_SSI_BASE            SSI2_BASE
#define SDC_SSI_SYSCTL_PERIPH   SYSCTL_PERIPH_SSI2

// GPIO for SSI pins
#define SDC_GPIO_PORT_BASE      GPIO_PORTB_AHB_BASE
#define SDC_GPIO_SYSCTL_PERIPH  SYSCTL_PERIPH_GPIOB
#define SDC_SSI_CLK             GPIO_PIN_4
#define SDC_SSI_TX              GPIO_PIN_7
#define SDC_SSI_RX              GPIO_PIN_6
#define SDC_SSI_FSS             GPIO_PIN_5
#define SDC_SSI_PINS            (SDC_SSI_TX | SDC_SSI_RX | SDC_SSI_CLK |      \
                                 SDC_SSI_FSS)


// asserts the CS pin to the card

void sd_assert_cs (void) {
    ROM_GPIOPinWrite(SDC_GPIO_PORT_BASE, SDC_SSI_FSS, 0);
}

// de-asserts the CS pin to the card

void sd_deassert_cs (void) {
    ROM_GPIOPinWrite(SDC_GPIO_PORT_BASE, SDC_SSI_FSS, SDC_SSI_FSS);
}

/*--------------------------------------------------------------------------

   Module Private Functions

---------------------------------------------------------------------------*/

volatile DSTATUS Stat = STA_NOINIT;    /* Disk status */
uint8_t CardType;            /* b0:MMC, b1:SDC, b2:Block addressing */
uint8_t PowerFlag = 0;     /* indicates if "power" is on */

/*-----------------------------------------------------------------------*/
/* Transmit a byte to MMC via SPI  (Platform dependent)                  */
/*-----------------------------------------------------------------------*/
void sd_spi_send_byte(uint8_t dat) {
    uint32_t ui32RcvDat;
    while(SSIBusy(SDC_SSI_BASE)){};
    ROM_SSIDataPut(SDC_SSI_BASE, dat); /* Write the data to the tx fifo */
    while(SSIBusy(SDC_SSI_BASE)){};
    ROM_SSIDataGet(SDC_SSI_BASE, &ui32RcvDat); /* flush data read during the write */
}


/*-----------------------------------------------------------------------*/
/* Receive a byte from MMC via SPI  (Platform dependent)                 */
/*-----------------------------------------------------------------------*/
uint8_t sd_spi_recieve_byte (void) {
    uint32_t ui32RcvDat;
    while(SSIBusy(SDC_SSI_BASE)){};
    ROM_SSIDataPut(SDC_SSI_BASE, 0xFF); /* write dummy data */
    while(SSIBusy(SDC_SSI_BASE)){};
    ROM_SSIDataGet(SDC_SSI_BASE, &ui32RcvDat); /* read data frm rx fifo */
    return (uint8_t)ui32RcvDat;
}

void sd_spi_recieve_byte_ptr (uint8_t *dst) {
    *dst = sd_spi_recieve_byte();
}

/*-----------------------------------------------------------------------*/
/* Wait for card ready                                                   */
/*-----------------------------------------------------------------------*/
uint8_t sd_wait_ready (void) {
    uint8_t res;
    mp_uint_t start = mp_hal_ticks_ms();    /* Wait for ready in timeout of 500ms */
    sd_spi_recieve_byte();
    do
        res = sd_spi_recieve_byte();
    while ((res != 0xFF) && (mp_hal_ticks_ms() - start) < 500);
    return res;
}

/*-----------------------------------------------------------------------*/
/* Send 80 or so clock transitions with CS and DI held high. This is     */
/* required after card power up to get it into SPI mode                  */
/*-----------------------------------------------------------------------*/
void sd_sel_spi_mode(void) {
    unsigned int i;
    uint32_t ui32Dat;

    /* Ensure CS is held high. */
    sd_deassert_cs();

    // /* Switch the SSI TX line to a GPIO and drive it high too. */
    // ROM_GPIOPinTypeGPIOOutput(SDC_GPIO_PORT_BASE, SDC_SSI_TX);
    // ROM_GPIOPinWrite(SDC_GPIO_PORT_BASE, SDC_SSI_TX, SDC_SSI_TX);
    while(SSIBusy(SDC_SSI_BASE)){};

    /* Send 10 bytes over the SSI. This causes the clock to wiggle the */
    /* required number of times. */
    for(i = 0 ; i < 10 ; i++)
    {
        /* Write DUMMY data. SSIDataPut() waits until there is room in the */
        /* FIFO. */
        ROM_SSIDataPut(SDC_SSI_BASE, 0xFF);

        /* Flush data read during data write. */
        ROM_SSIDataGet(SDC_SSI_BASE, &ui32Dat);
    }

    // /* Revert to hardware control of the SSI TX line. */
    // ROM_GPIOPinTypeSSI(SDC_GPIO_PORT_BASE, SDC_SSI_TX);
    
}

/*-----------------------------------------------------------------------*/
/* Power Control  (Platform dependent)                                   */
/*-----------------------------------------------------------------------*/
/* When the target system does not support socket power control, there   */
/* is nothing to do in these functions and sd_chk_power always returns 1.   */
void sd_power_on (void) {
    /*
     * This doesn't really turn the power on, but initializes the
     * SSI port and pins needed to talk to the card.
     */

    /* Enable the peripherals used to drive the SDC on SSI */
    // ROM_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);
    ROM_SysCtlPeripheralEnable(SDC_SSI_SYSCTL_PERIPH);
    ROM_SysCtlPeripheralEnable(SDC_GPIO_SYSCTL_PERIPH);

    /*
     * Configure the appropriate pins to be SSI instead of GPIO. The FSS (CS)
     * signal is directly driven to ensure that we can hold it low through a
     * complete transaction with the SD card.
     */

    GPIOPinConfigure(GPIO_PB4_SSI2CLK);
    // GPIOPinConfigure(GPIO_PB5_SSI2FSS);
    GPIOPinConfigure(GPIO_PB6_SSI2RX);
    GPIOPinConfigure(GPIO_PB7_SSI2TX);
    // ROM_GPIOPinTypeSSI(SDC_GPIO_PORT_BASE, SDC_SSI_TX | SDC_SSI_RX | SDC_SSI_CLK);
    // ROM_GPIOPinTypeGPIOOutput(SDC_GPIO_PORT_BASE, SDC_SSI_FSS);
    // ROM_GPIOPinTypeGPIOOutput(SDC_GPIO_PORT_BASE, MICROPY_HW_SDCARD_DETECT_PIN->pin_mask);
    ROM_GPIOPinTypeSSI(SDC_GPIO_PORT_BASE, SDC_SSI_TX | SDC_SSI_RX | SDC_SSI_CLK);
    ROM_GPIOPinTypeGPIOOutput(SDC_GPIO_PORT_BASE, SDC_SSI_FSS);
    GPIOPinTypeGPIOInput(SDC_GPIO_PORT_BASE, MICROPY_HW_SDCARD_DETECT_PIN->pin_mask);
    GPIOPadConfigSet(SDC_GPIO_PORT_BASE, MICROPY_HW_SDCARD_DETECT_PIN->pin_mask, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD_WPU);

    /*
     * Set the SSI output pins to 4MA drive strength and engage the
     * pull-up on the receive line.
     */
    MAP_GPIOPadConfigSet(SDC_GPIO_PORT_BASE, SDC_SSI_RX, GPIO_STRENGTH_2MA,
                         GPIO_PIN_TYPE_STD_WPU);
    MAP_GPIOPadConfigSet(SDC_GPIO_PORT_BASE, SDC_SSI_CLK | SDC_SSI_TX | SDC_SSI_FSS,
                         GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD);
    

    /* Configure the SSI0 port */
    ROM_SSIConfigSetExpClk(SDC_SSI_BASE, ROM_SysCtlClockGet(),
                           SSI_FRF_MOTO_MODE_0, SSI_MODE_MASTER, 200000, 8);
    ROM_SSIEnable(SDC_SSI_BASE);

    /* Set DI and CS high and apply more than 74 pulses to SCLK for the card */
    /* to be able to accept a native command. */
    sd_sel_spi_mode();
    // sd_deassert_cs();

    PowerFlag = 1;
}

// set the SSI speed to the max setting

void sd_spi_set_max_speed(void) {
    unsigned long i;

    /* Disable the SSI */
    ROM_SSIDisable(SDC_SSI_BASE);

    /* Set the maximum speed as half the system clock, with a max of 20 MHz. */
    i = ROM_SysCtlClockGet() / 2;
    if(i > 12000000)
    {
        i = 12000000;
    }

    /* Configure the SSI0 port to run at 12.5MHz */
    SSIConfigSetExpClk(SDC_SSI_BASE, ROM_SysCtlClockGet(),
                           SSI_FRF_MOTO_MODE_0, SSI_MODE_MASTER, i, 8);

    /* Enable the SSI */
    ROM_SSIEnable(SDC_SSI_BASE);
}


void sd_power_off (void) {
    MAP_SysCtlPeripheralDisable(SDC_SSI_SYSCTL_PERIPH);
    PowerFlag = 0;
}


int sd_chk_power(void) {       /* Socket power state: 0=off, 1=on */
    return PowerFlag;
}



/*-----------------------------------------------------------------------*/
/* Receive a data packet from MMC                                        */
/*-----------------------------------------------------------------------*/


bool sd_spi_receive_block (
    uint8_t *buff,            /* Data buffer to store received data */
    UINT btr            /* Byte count (must be even number) */
) {
    uint8_t token;
    mp_uint_t start = mp_hal_ticks_ms();
    do {                            /* Wait for data packet in timeout of 100ms */
        token = sd_spi_recieve_byte();
    } while ((token == 0xFF) && (mp_hal_ticks_ms() - start) < 100);
    if(token != 0xFE) return false;    /* If not valid data token, retutn with error */

    do {                            /* Receive the data block into buffer */
        sd_spi_recieve_byte_ptr(buff++);
        sd_spi_recieve_byte_ptr(buff++);
    } while (btr -= 2);
    sd_spi_recieve_byte();                        /* Discard CRC */
    sd_spi_recieve_byte();

    return true;                    /* Return with success */
}



/*-----------------------------------------------------------------------*/
/* Send a data packet to MMC                                             */
/*-----------------------------------------------------------------------*/
#if _READONLY == 0

bool sd_spi_transmit_block (
    const uint8_t *buff,    /* 512 byte data block to be transmitted */
    uint8_t token            /* Data/Stop token */
) {
    uint8_t resp, wc;
    if (sd_wait_ready() != 0xFF) return false;

    sd_spi_send_byte(token);                    /* Xmit data token */
    if (token != 0xFD) {    /* Is data token */
        wc = 0;
        do {                            /* Xmit the 512 byte data block to MMC */
            sd_spi_send_byte(*buff++);
            sd_spi_send_byte(*buff++);
        } while (--wc);
        sd_spi_send_byte(0xFF);                    /* CRC (Dummy) */
        sd_spi_send_byte(0xFF);
        resp = sd_spi_recieve_byte();                /* Reveive data response */
        if ((resp & 0x1F) != 0x05)        /* If not accepted, return with error */
            return false;
    }

    return true;
}
#endif /* _READONLY */



/*-----------------------------------------------------------------------*/
/* Send a command packet to MMC                                          */
/*-----------------------------------------------------------------------*/
uint8_t sd_spi_send_cmd (
    uint8_t cmd,        /* Command byte */
    uint32_t arg        /* Argument */
) {
    uint8_t n, res;
    if (sd_wait_ready() != 0xFF) return 0xFF;

    /* Send command packet */
    sd_spi_send_byte(cmd);                        /* Command */
    sd_spi_send_byte((uint8_t)(arg >> 24));        /* Argument[31..24] */
    sd_spi_send_byte((uint8_t)(arg >> 16));        /* Argument[23..16] */
    sd_spi_send_byte((uint8_t)(arg >> 8));            /* Argument[15..8] */
    sd_spi_send_byte((uint8_t)arg);                /* Argument[7..0] */
    n = 0xff;
    if (cmd == CMD0) n = 0x95;            /* CRC for CMD0(0) */
    if (cmd == CMD8) n = 0x87;            /* CRC for CMD8(0x1AA) */
    if (cmd == CMD41) n = 0x95;
    sd_spi_send_byte(n);

    /* Receive command response */
    if (cmd == CMD12) sd_spi_recieve_byte();        /* Skip a stuff byte when stop reading */
    n = 10;                                /* Wait for a valid response in timeout of 10 attempts */
    do
        res = sd_spi_recieve_byte();
    while ((res & 0x80) && --n);

    return res;            /* Return with the response value */
}

/*-----------------------------------------------------------------------*
 * Send the special command used to terminate a multi-sector read.
 *
 * This is the only command which can be sent while the SDCard is sending
 * data. The SDCard spec indicates that the data transfer will stop 2 bytes
 * after the 6 byte CMD12 command is sent and that the card will then send
 * 0xFF for between 2 and 6 more bytes before the R1 response byte.  This
 * response will be followed by another 0xFF byte.  In testing, however, it
 * seems that some cards don't send the 2 to 6 0xFF bytes between the end of
 * data transmission and the response code.  This function, therefore, merely
 * reads 10 bytes and, if the last one read is 0xFF, returns the value of the
 * latest non-0xFF byte as the response code.
 *
 *-----------------------------------------------------------------------*/
uint8_t sd_spi_send_cmd12 (void) {
    uint8_t n, val;
    uint8_t res = 0;

    /* For CMD12, we don't wait for the card to be idle before we send
     * the new command.
     */

    /* Send command packet - the argument for CMD12 is ignored. */
    sd_spi_send_byte(CMD12);
    sd_spi_send_byte(0);
    sd_spi_send_byte(0);
    sd_spi_send_byte(0);
    sd_spi_send_byte(0);
    sd_spi_send_byte(0);

    /* Read up to 10 bytes from the card, remembering the value read if it's
       not 0xFF */
    for(n = 0; n < 10; n++)
    {
        val = sd_spi_recieve_byte();
        if(val != 0xFF)
        {
            res = val;
        }
    }

    return res;            /* Return with the response value */
}

/*-----------------------------------------------------------------------*/
/* Initialize Disk Drive                                                 */
/*-----------------------------------------------------------------------*/
DSTATUS sd_disk_init (
    uint8_t drv        /* Physical drive nmuber (0) */
) {
    uint8_t n, ty, ocr[4];
    mp_uint_t start;

    if (drv) return STA_NOINIT;            /* Supports only single drive */

    if(PowerFlag==0) sd_power_on();                            /* Force socket power on */

    if (!sdcard_is_present()) return Stat;    /* No card in the socket */
 
    sd_sel_spi_mode();            /* Ensure the card is in SPI mode */

    sd_assert_cs();                /* CS = L */
    ty = 0;
    if (sd_spi_send_cmd(CMD0, 0) == 1) {            /* Enter Idle state */
    sd_deassert_cs();
    sd_wait_ready();
    sd_assert_cs();
        if (sd_spi_send_cmd(CMD8, 0x1AA) == 1) {    /* SDC Ver2+ */
            for (n = 0; n < 4; n++) ocr[n] = sd_spi_recieve_byte();
            if (ocr[2] == 0x01 && ocr[3] == 0xAA) {    /* The card can work at vdd range of 2.7-3.6V */
                start = mp_hal_ticks_ms();                      /* Initialization timeout of 1000 msec */
                do {
                    sd_wait_ready(); // Prevent command banging
                    if (sd_spi_send_cmd(CMD55, 0) <= 1 && sd_spi_send_cmd(CMD41, 0x40000000) == 0) {
                        break;
                    }     /* ACMD41 with HCS bit */
                } while ((mp_hal_ticks_ms() - start) < 1000);
                if ((mp_hal_ticks_ms() - start) < 1000 && sd_spi_send_cmd(CMD58, 0) == 0) {    /* Check CCS bit */
                    for (n = 0; n < 4; n++) ocr[n] = sd_spi_recieve_byte();
                    ty = (ocr[0] & 0x40) ? 6 : 2;
                }
            }
        } else {                            /* SDC Ver1 or MMC */
            ty = (sd_spi_send_cmd(CMD55, 0) <= 1 && sd_spi_send_cmd(CMD41, 0) <= 1) ? 2 : 1;    /* SDC : MMC */
            start = mp_hal_ticks_ms();                      /* Initialization timeout of 1000 msec */
            do {
                if (ty == 2) {
                    if (sd_spi_send_cmd(CMD55, 0) <= 1 && sd_spi_send_cmd(CMD41, 0) == 0) break;    /* ACMD41 */
                } else {
                    if (sd_spi_send_cmd(CMD1, 0) == 0) break;                                /* CMD1 */
                }
            } while ((mp_hal_ticks_ms() - start) < 1000);
            if (!((mp_hal_ticks_ms() - start) < 1000) || sd_spi_send_cmd(CMD16, 512) != 0)    /* sd_assert_cs R/W block length */
                ty = 0;
        }
    }
    CardType = ty;
    sd_deassert_cs();            /* CS = H */
    sd_spi_recieve_byte();            /* Idle (Release DO) */

    if (ty) {            /* Initialization succeded */
        Stat &= ~STA_NOINIT;        /* Clear STA_NOINIT */
        sd_spi_set_max_speed();
    } else {            /* Initialization failed */
        sd_power_off();
    }

    return Stat;
}



/*-----------------------------------------------------------------------*/
/* Get Disk Status                                                       */
/*-----------------------------------------------------------------------*/
DSTATUS sd_disk_staus (
    uint8_t drv        /* Physical drive nmuber (0) */
) {
    if (drv) return STA_NOINIT;        /* Supports only single drive */
    return Stat;
}



/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/
DRESULT sd_disk_read (
    uint8_t drv,            /* Physical drive nmuber (0) */
    uint8_t *buff,            /* Pointer to the data buffer to store read data */
    uint32_t sector,        /* Start sector number (LBA) */
    uint8_t count            /* Sector count (1..255) */
) {
    if (drv || !count) return RES_PARERR;
    if (Stat & STA_NOINIT) return RES_NOTRDY;

    if (!(CardType & 4)) sector *= 512;    /* Convert to byte address if needed */

    sd_assert_cs();            /* CS = L */

    if (count == 1) {    /* Single block read */
        if ((sd_spi_send_cmd(CMD17, sector) == 0)    /* READ_SINGLE_BLOCK */
            && sd_spi_receive_block(buff, 512))
            count = 0;
    }
    else {                /* Multiple block read */
        if (sd_spi_send_cmd(CMD18, sector) == 0) {    /* READ_MULTIPLE_BLOCK */
            do {
                if (!sd_spi_receive_block(buff, 512)) break;
                buff += 512;
            } while (--count);
            sd_spi_send_cmd12();                /* STOP_TRANSMISSION */
        }
    }

    sd_deassert_cs();            /* CS = H */
    sd_spi_recieve_byte();            /* Idle (Release DO) */

    return count ? RES_ERROR : RES_OK;
}

DRESULT sd_disk_read_dma (
    uint8_t drv,            /* Physical drive nmuber (0) */
    uint8_t *buff,            /* Pointer to the data buffer to store read data */
    uint32_t sector,        /* Start sector number (LBA) */
    uint8_t count            /* Sector count (1..255) */
) {
    if (drv || !count) return RES_PARERR;
    if (Stat & STA_NOINIT) return RES_NOTRDY;

    if (!(CardType & 4)) sector *= 512;    /* Convert to byte address if needed */

    sd_assert_cs();            /* CS = L */

    if (count == 1) {    /* Single block read */
        if ((sd_spi_send_cmd(CMD17, sector) == 0)    /* READ_SINGLE_BLOCK */
            && sd_spi_receive_block(buff, 512))
            count = 0;
    }
    else {                /* Multiple block read */
        if (sd_spi_send_cmd(CMD18, sector) == 0) {    /* READ_MULTIPLE_BLOCK */
            do {
                if (!sd_spi_receive_block(buff, 512)) break;
                buff += 512;
            } while (--count);
            sd_spi_send_cmd12();                /* STOP_TRANSMISSION */
        }
    }

    sd_deassert_cs();            /* CS = H */
    sd_spi_recieve_byte();            /* Idle (Release DO) */

    return count ? RES_ERROR : RES_OK;
}



/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/
#if _READONLY == 0
DRESULT sd_disk_write (
    uint8_t drv,            /* Physical drive nmuber (0) */
    const uint8_t *buff,    /* Pointer to the data to be written */
    uint32_t sector,        /* Start sector number (LBA) */
    uint8_t count            /* Sector count (1..255) */
) {
    if (drv || !count) return RES_PARERR;
    if (Stat & STA_NOINIT) return RES_NOTRDY;
    if (Stat & STA_PROTECT) return RES_WRPRT;

    if (!(CardType & 4)) sector *= 512;    /* Convert to byte address if needed */

    sd_assert_cs();            /* CS = L */

    if (count == 1) {    /* Single block write */
        if ((sd_spi_send_cmd(CMD24, sector) == 0)    /* WRITE_BLOCK */
            && sd_spi_transmit_block(buff, 0xFE))
            count = 0;
    }
    else {                /* Multiple block write */
        if (CardType & 2) {
            sd_spi_send_cmd(CMD55, 0); sd_spi_send_cmd(CMD23, count);    /* ACMD23 */
        }
        if (sd_spi_send_cmd(CMD25, sector) == 0) {    /* WRITE_MULTIPLE_BLOCK */
            do {
                if (!sd_spi_transmit_block(buff, 0xFC)) break;
                buff += 512;
            } while (--count);
            if (!sd_spi_transmit_block(0, 0xFD))    /* STOP_TRAN token */
                count = 1;
        }
    }

    sd_deassert_cs();            /* CS = H */
    sd_spi_recieve_byte();            /* Idle (Release DO) */

    return count ? RES_ERROR : RES_OK;
}

DRESULT sd_disk_write_dma (
    uint8_t drv,            /* Physical drive nmuber (0) */
    const uint8_t *buff,    /* Pointer to the data to be written */
    uint32_t sector,        /* Start sector number (LBA) */
    uint8_t count            /* Sector count (1..255) */
) {
    if (drv || !count) return RES_PARERR;
    if (Stat & STA_NOINIT) return RES_NOTRDY;
    if (Stat & STA_PROTECT) return RES_WRPRT;

    if (!(CardType & 4)) sector *= 512;    /* Convert to byte address if needed */

    sd_assert_cs();            /* CS = L */

    if (count == 1) {    /* Single block write */
        if ((sd_spi_send_cmd(CMD24, sector) == 0)    /* WRITE_BLOCK */
            && sd_spi_transmit_block(buff, 0xFE))
            count = 0;
    }
    else {                /* Multiple block write */
        if (CardType & 2) {
            sd_spi_send_cmd(CMD55, 0); sd_spi_send_cmd(CMD23, count);    /* ACMD23 */
        }
        if (sd_spi_send_cmd(CMD25, sector) == 0) {    /* WRITE_MULTIPLE_BLOCK */
            do {
                if (!sd_spi_transmit_block(buff, 0xFC)) break;
                buff += 512;
            } while (--count);
            if (!sd_spi_transmit_block(0, 0xFD))    /* STOP_TRAN token */
                count = 1;
        }
    }

    sd_deassert_cs();            /* CS = H */
    sd_spi_recieve_byte();            /* Idle (Release DO) */

    return count ? RES_ERROR : RES_OK;
}
#endif /* _READONLY */



/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

DRESULT sd_disk_ioctl (
    uint8_t drv,        /* Physical drive nmuber (0) */
    uint8_t ctrl,        /* Control code */
    void *buff        /* Buffer to send/receive control data */
) {
    DRESULT res;
    uint8_t n, csd[16], *ptr = buff;
    uint16_t csize;


    if (drv) return RES_PARERR;

    res = RES_ERROR;

    if (ctrl == CTRL_POWER) {
        switch (*ptr) {
        case 0:        /* Sub control code == 0 (sd_power_off) */
            if (sd_chk_power())
                sd_power_off();        /* Power off */
            res = RES_OK;
            break;
        case 1:        /* Sub control code == 1 (sd_power_on) */
            sd_power_on();                /* Power on */
            res = RES_OK;
            break;
        case 2:        /* Sub control code == 2 (POWER_GET) */
            *(ptr+1) = (uint8_t)sd_chk_power();
            res = RES_OK;
            break;
        default :
            res = RES_PARERR;
        }
    }
    else {
        if (Stat & STA_NOINIT) return RES_NOTRDY;

        sd_assert_cs();        /* CS = L */

        switch (ctrl) {
        case GET_SECTOR_COUNT :    /* Get number of sectors on the disk (uint32_t) */
            if ((sd_spi_send_cmd(CMD9, 0) == 0) && sd_spi_receive_block(csd, 16)) {
                if ((csd[0] >> 6) == 1) {    /* SDC ver 2.00 */
                    csize = csd[9] + ((uint16_t)csd[8] << 8) + 1;
                    *(uint32_t*)buff = (uint32_t)csize << 10;
                } else {                    /* MMC or SDC ver 1.XX */
                    n = (csd[5] & 15) + ((csd[10] & 128) >> 7) + ((csd[9] & 3) << 1) + 2;
                    csize = (csd[8] >> 6) + ((uint16_t)csd[7] << 2) + ((uint16_t)(csd[6] & 3) << 10) + 1;
                    *(uint32_t*)buff = (uint32_t)csize << (n - 9);
                }
                res = RES_OK;
            }
            break;

        case GET_SECTOR_SIZE :    /* Get sectors on the disk (uint16_t) */
            *(uint16_t*)buff = 512;
            res = RES_OK;
            break;

        case CTRL_SYNC :    /* Make sure that data has been written */
            if (sd_wait_ready() == 0xFF)
                res = RES_OK;
            break;

        case MMC_GET_CSD :    /* Receive CSD as a data block (16 bytes) */
            if (sd_spi_send_cmd(CMD9, 0) == 0        /* READ_CSD */
                && sd_spi_receive_block(ptr, 16))
                res = RES_OK;
            break;

        case MMC_GET_CID :    /* Receive CID as a data block (16 bytes) */
            if (sd_spi_send_cmd(CMD10, 0) == 0        /* READ_CID */
                && sd_spi_receive_block(ptr, 16))
                res = RES_OK;
            break;

        case MMC_GET_OCR :    /* Receive OCR as an R3 resp (4 bytes) */
            if (sd_spi_send_cmd(CMD58, 0) == 0) {    /* READ_OCR */
                for (n = 0; n < 4; n++)
                    *ptr++ = sd_spi_recieve_byte();
                res = RES_OK;
            }

//        case MMC_GET_TYPE :    /* Get card type flags (1 byte) */
//            *ptr = CardType;
//            res = RES_OK;
//            break;

        default:
            res = RES_PARERR;
        }

        sd_deassert_cs();            /* CS = H */
        sd_spi_recieve_byte();            /* Idle (Release DO) */
    }

    return res;
}

void sdcard_init(void) {
    sd_power_on();
    // sd_disk_init(0);
}

// void HAL_SD_MspDeInit(SD_HandleTypeDef *hsd) {
//     HAL_NVIC_DisableIRQ(SDMMC_IRQn);
//     SDMMC_CLK_DISABLE();
// }

bool sdcard_is_present(void) {
    if(mp_hal_pin_read(MICROPY_HW_SDCARD_DETECT_PIN)) {
        Stat &= ~STA_NODISK;
        return true;
    } else {
        Stat |= STA_NODISK;
        return false; 
    }
}

bool sdcard_power_on(void) {
    if (!sdcard_is_present()) {
        return false;
    }
    sd_power_on();
    // TODO
    // if (sdc) {
    //     sdcard_power_off();
    //     goto error;
    // }

    return true;

// error:

//     return false;
}

void sdcard_power_off(void) {
    sd_power_off();
}

void sdcard_read_csd(uint8_t *data) {
    sd_spi_send_cmd(CMD9, 0);
    sd_spi_receive_block(data, 16);
}

uint64_t sdcard_get_capacity_in_bytes(void) {
    uint32_t size, count;
    sd_disk_ioctl(0, GET_SECTOR_SIZE, &size);
    sd_disk_ioctl(0, GET_SECTOR_COUNT, &count);
    return (uint64_t)size * (uint64_t)count;
}


void SD_IRQHandler(void) {
    IRQ_ENTER(INT_SSI2);
    // HAL_SD_IRQHandler(&sd_handle);
    IRQ_EXIT(INT_SSI2);
}


STATIC int sdcard_wait_finished(uint32_t timeout) {
    // Wait for HAL driver to be ready (eg for DMA to finish)
    uint32_t start = HAL_GetTick();
    for (;;) {
        // Do an atomic check of the state; WFI will exit even if IRQs are disabled
        uint32_t irq_state = disable_irq();
        if (!MAP_SSIBusy(SDC_GPIO_PORT_BASE)) {
            enable_irq(irq_state);
            break;
        }
        __WFI();
        enable_irq(irq_state);
        if (HAL_GetTick() - start >= timeout) {
            return MP_ETIMEDOUT;
        }
    }

    // Wait for SD card to complete the operation
    // for (;;) {
    //     HAL_SD_CardStateTypedef state = HAL_SD_GetCardState(sd);
    //     if (state == HAL_SD_CARD_TRANSFER) {
    //         return HAL_OK;
    //     }
    //     if (!(state == HAL_SD_CARD_SENDING || state == HAL_SD_CARD_RECEIVING || state == HAL_SD_CARD_PROGRAMMING)) {
    //         return HAL_ERROR;
    //     }
    //     if (HAL_GetTick() - start >= timeout) {
    //         return HAL_TIMEOUT;
    //     }
    //     __WFI();
    // }
    return RES_OK;
}

mp_uint_t sdcard_read_blocks(uint8_t *dest, uint32_t block_num, uint32_t num_blocks) {
    // check that SD card is initialised
    // if (sd_handle.Instance == NULL) {
    //     return HAL_ERROR;
    // }

    DRESULT err = RES_OK;

    // check that dest pointer is aligned on a 4-byte boundary
    uint8_t *orig_dest = NULL;
    uint32_t saved_word;
    if (((uint32_t)dest & 3) != 0) {
        // Pointer is not aligned so it needs fixing.
        // We could allocate a temporary block of RAM (as sdcard_write_blocks
        // does) but instead we are going to use the dest buffer inplace.  We
        // are going to align the pointer, save the initial word at the aligned
        // location, read into the aligned memory, move the memory back to the
        // unaligned location, then restore the initial bytes at the aligned
        // location.  We should have no trouble doing this as those initial
        // bytes at the aligned location should be able to be changed for the
        // duration of this function call.
        orig_dest = dest;
        dest = (uint8_t*)((uint32_t)dest & ~3);
        saved_word = *(uint32_t*)dest;
    }
#if MICROPY_HW_DMA
    if (query_irq() == IRQ_STATE_ENABLED) {
        // we must disable USB irqs to prevent MSC contention with SD card
        uint32_t basepri = raise_irq_pri(IRQ_PRI_OTG_FS);

        #if SDIO_USE_GPDMA
        dma_init(&sd_rx_dma, &SDMMC_RX_DMA, &sd_handle);
        sd_handle.hdmarx = &sd_rx_dma;
        #endif

        // make sure cache is flushed and invalidated so when DMA updates the RAM
        // from reading the peripheral the CPU then reads the new data
        // TODO
        // MP_HAL_CLEANINVALIDATE_DCACHE(dest, num_blocks * SDCARD_BLOCK_SIZE);

        err = sd_disk_read_dma(0, dest, block_num, num_blocks);
        if (err == RES_OK) {
            err = sdcard_wait_finished(60000);
        }

        #if SDIO_USE_GPDMA
        dma_deinit(&SDMMC_RX_DMA);
        sd_handle.hdmarx = NULL;
        #endif

        restore_irq_pri(basepri);
    } else {
#endif
        err = sd_disk_read(0, dest, block_num, num_blocks);
        if (err == RES_OK) {
            err = sdcard_wait_finished(60000);
        }
#if MICROPY_HW_DMA
    }
#endif

    if (orig_dest != NULL) {
        // move the read data to the non-aligned position, and restore the initial bytes
        memmove(orig_dest, dest, num_blocks * SDCARD_BLOCK_SIZE);
        memcpy(dest, &saved_word, orig_dest - dest);
    }

    return err;
}

mp_uint_t sdcard_write_blocks(const uint8_t *src, uint32_t block_num, uint32_t num_blocks) {
    // check that SD card is initialised
    // if (sd_handle.Instance == NULL) {
    //     return HAL_ERROR;
    // }

    DRESULT err = RES_OK;

    // check that src pointer is aligned on a 4-byte boundary
    if (((uint32_t)src & 3) != 0) {
        // pointer is not aligned, so allocate a temporary block to do the write
        uint8_t *src_aligned = m_new_maybe(uint8_t, SDCARD_BLOCK_SIZE);
        if (src_aligned == NULL) {
            return MP_EFAULT;
        }
        for (size_t i = 0; i < num_blocks; ++i) {
            memcpy(src_aligned, src + i * SDCARD_BLOCK_SIZE, SDCARD_BLOCK_SIZE);
            err = sdcard_write_blocks(src_aligned, block_num + i, 1);
            if (err != RES_OK) {
                break;
            }
        }
        m_del(uint8_t, src_aligned, SDCARD_BLOCK_SIZE);
        return err;
    }
#if MICROPY_HW_DMA
    if (query_irq() == IRQ_STATE_ENABLED) {
        // we must disable USB irqs to prevent MSC contention with SD card
        uint32_t basepri = raise_irq_pri(IRQ_PRI_OTG_FS);

        #if SDIO_USE_GPDMA
        dma_init(&sd_tx_dma, &SDMMC_TX_DMA, &sd_handle);
        sd_handle.hdmatx = &sd_tx_dma;
        #endif

        // make sure cache is flushed to RAM so the DMA can read the correct data
        // MP_HAL_CLEAN_DCACHE(src, num_blocks * SDCARD_BLOCK_SIZE);

        err = sd_disk_write_dma(0,(uint8_t*)src, block_num, num_blocks);
        if (err == RES_OK) {
            err = sdcard_wait_finished(60000);
        }

        #if SDIO_USE_GPDMA
        dma_deinit(&SDMMC_TX_DMA);
        sd_handle.hdmatx = NULL;
        #endif

        restore_irq_pri(basepri);
    } else {
#endif
        err = sd_disk_write(0,(uint8_t*)src, block_num, num_blocks);
        if (err == RES_OK) {
            err = sdcard_wait_finished(60000);
        }
#if MICROPY_HW_DMA
    }
#endif
    return err;
}

/******************************************************************************/
// MicroPython bindings
//
// Expose the SD card as an object with the block protocol.

// there is a singleton SDCard object
const mp_obj_base_t pyb_sdcard_obj = {&pyb_sdcard_type};

STATIC mp_obj_t pyb_sdcard_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    // check arguments
    mp_arg_check_num(n_args, n_kw, 0, 0, false);

    // return singleton object
    return (mp_obj_t)&pyb_sdcard_obj;
}

STATIC mp_obj_t sd_present(mp_obj_t self) {
    return mp_obj_new_bool(sdcard_is_present());
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(sd_present_obj, sd_present);

STATIC mp_obj_t sd_power(mp_obj_t self, mp_obj_t state) {
    bool result;
    if (mp_obj_is_true(state)) {
        result = sdcard_power_on();
    } else {
        sdcard_power_off();
        result = true;
    }
    return mp_obj_new_bool(result);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(sd_power_obj, sd_power);

STATIC mp_obj_t sd_info(mp_obj_t self) {
    uint8_t csd[16];
    sdcard_read_csd(csd);
    uint64_t size = 0;
    uint bl_size = 0;
    int card_type = (csd[15] & 0b11000000) >> 6; // 0 == v1 SD, 1 == v2 SDHC/XC
    if(card_type) {
        // SDHC XC
        uint32_t c_size = (uint32_t)(csd[6]) + ((uint32_t)(csd[7]) << 8) + (((uint32_t)(csd[8]) & 0b00111111) << 16);
        size = (uint64_t)((c_size + 1) * 512000);
    } else {
        // SD
        uint16_t c_size = ((csd[7] & 0b11000000) >> 6) +  ((uint16_t)(csd[8] & 0b11111111) << 2) + ((uint16_t)(csd[9] & 0b00000011) << 10);
        uint16_t c_size_multi = (csd[5] & 0b00000001) + ((csd[6] & 0b00000011) << 1);
        uint16_t bl_len = (csd[10] & 0b00001111);
        bl_len = (1 << bl_len);
        bl_size = bl_len;
        c_size_multi = (1 << (c_size_multi+2));
        size = (uint64_t)((c_size + 1) * c_size_multi * bl_len);
    }
    mp_obj_t tuple[3] = {
        mp_obj_new_int_from_ull(size),
        mp_obj_new_int_from_uint(bl_size),
        mp_obj_new_int(card_type),
    };
    return mp_obj_new_tuple(3, tuple);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(sd_info_obj, sd_info);

// now obsolete, kept for backwards compatibility
STATIC mp_obj_t sd_read(mp_obj_t self, mp_obj_t block_num) {
    uint8_t *dest = m_new(uint8_t, SDCARD_BLOCK_SIZE);
    mp_uint_t ret = sdcard_read_blocks(dest, mp_obj_get_int(block_num), 1);

    if (ret != 0) {
        m_del(uint8_t, dest, SDCARD_BLOCK_SIZE);
        nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_Exception, MP_ERROR_TEXT("sdcard_read_blocks failed [%u]"), ret));
    }

    return mp_obj_new_bytearray_by_ref(SDCARD_BLOCK_SIZE, dest);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(sd_read_obj, sd_read);

// now obsolete, kept for backwards compatibility
STATIC mp_obj_t sd_write(mp_obj_t self, mp_obj_t block_num, mp_obj_t data) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(data, &bufinfo, MP_BUFFER_READ);
    if (bufinfo.len % SDCARD_BLOCK_SIZE != 0) {
        nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("writes must be a multiple of %d bytes"), SDCARD_BLOCK_SIZE));
    }

    mp_uint_t ret = sdcard_write_blocks(bufinfo.buf, mp_obj_get_int(block_num), bufinfo.len / SDCARD_BLOCK_SIZE);

    if (ret != 0) {
        nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_Exception, MP_ERROR_TEXT("sdcard_write_blocks failed [%u]"), ret));
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(sd_write_obj, sd_write);

STATIC mp_obj_t pyb_sdcard_readblocks(mp_obj_t self, mp_obj_t block_num, mp_obj_t buf) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf, &bufinfo, MP_BUFFER_WRITE);
    mp_uint_t ret = sdcard_read_blocks(bufinfo.buf, mp_obj_get_int(block_num), bufinfo.len / SDCARD_BLOCK_SIZE);
    return mp_obj_new_bool(ret == 0);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(pyb_sdcard_readblocks_obj, pyb_sdcard_readblocks);

STATIC mp_obj_t pyb_sdcard_writeblocks(mp_obj_t self, mp_obj_t block_num, mp_obj_t buf) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf, &bufinfo, MP_BUFFER_READ);
    mp_uint_t ret = sdcard_write_blocks(bufinfo.buf, mp_obj_get_int(block_num), bufinfo.len / SDCARD_BLOCK_SIZE);
    return mp_obj_new_bool(ret == 0);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(pyb_sdcard_writeblocks_obj, pyb_sdcard_writeblocks);

STATIC mp_obj_t pyb_sdcard_ioctl(mp_obj_t self, mp_obj_t cmd_in, mp_obj_t arg_in) {
    mp_int_t cmd = mp_obj_get_int(cmd_in);
    switch (cmd) {
        case MP_BLOCKDEV_IOCTL_INIT:
            if (!sd_disk_init(0)) {
                return MP_OBJ_NEW_SMALL_INT(-1); // error
            }
            return MP_OBJ_NEW_SMALL_INT(0); // success

        case MP_BLOCKDEV_IOCTL_DEINIT:
            sdcard_power_off();
            return MP_OBJ_NEW_SMALL_INT(0); // success

        case MP_BLOCKDEV_IOCTL_SYNC:
            // nothing to do
            return MP_OBJ_NEW_SMALL_INT(0); // success

        case MP_BLOCKDEV_IOCTL_BLOCK_COUNT:
            return MP_OBJ_NEW_SMALL_INT(sdcard_get_capacity_in_bytes() / SDCARD_BLOCK_SIZE);

        case MP_BLOCKDEV_IOCTL_BLOCK_SIZE:
            return MP_OBJ_NEW_SMALL_INT(SDCARD_BLOCK_SIZE);

        default: // unknown command
            return MP_OBJ_NEW_SMALL_INT(-1); // error
    }
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(pyb_sdcard_ioctl_obj, pyb_sdcard_ioctl);

STATIC const mp_rom_map_elem_t pyb_sdcard_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_present), MP_ROM_PTR(&sd_present_obj) },
    { MP_ROM_QSTR(MP_QSTR_power), MP_ROM_PTR(&sd_power_obj) },
    { MP_ROM_QSTR(MP_QSTR_info), MP_ROM_PTR(&sd_info_obj) },
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&sd_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&sd_write_obj) },
    // block device protocol
    { MP_ROM_QSTR(MP_QSTR_readblocks), MP_ROM_PTR(&pyb_sdcard_readblocks_obj) },
    { MP_ROM_QSTR(MP_QSTR_writeblocks), MP_ROM_PTR(&pyb_sdcard_writeblocks_obj) },
    { MP_ROM_QSTR(MP_QSTR_ioctl), MP_ROM_PTR(&pyb_sdcard_ioctl_obj) },
};

STATIC MP_DEFINE_CONST_DICT(pyb_sdcard_locals_dict, pyb_sdcard_locals_dict_table);

const mp_obj_type_t pyb_sdcard_type = {
    { &mp_type_type },
    .name = MP_QSTR_SDCard,
    .make_new = pyb_sdcard_make_new,
    .locals_dict = (mp_obj_dict_t*)&pyb_sdcard_locals_dict,
};

void sdcard_init_vfs(fs_user_mount_t *vfs, int part) {
    vfs->base.type = &mp_fat_vfs_type;
    vfs->blockdev.flags |= MP_BLOCKDEV_FLAG_NATIVE | MP_BLOCKDEV_FLAG_HAVE_IOCTL;
    vfs->fatfs.drv = vfs;
    vfs->fatfs.part = part;
    vfs->blockdev.readblocks[0] = MP_OBJ_FROM_PTR(&pyb_sdcard_readblocks_obj);
    vfs->blockdev.readblocks[1] = MP_OBJ_FROM_PTR(&pyb_sdcard_obj);
    vfs->blockdev.readblocks[2] = MP_OBJ_FROM_PTR(sdcard_read_blocks); // native version
    vfs->blockdev.writeblocks[0] = MP_OBJ_FROM_PTR(&pyb_sdcard_writeblocks_obj);
    vfs->blockdev.writeblocks[1] = MP_OBJ_FROM_PTR(&pyb_sdcard_obj);
    vfs->blockdev.writeblocks[2] = MP_OBJ_FROM_PTR(sdcard_write_blocks); // native version
    vfs->blockdev.u.ioctl[0] = MP_OBJ_FROM_PTR(&pyb_sdcard_ioctl_obj);
    vfs->blockdev.u.ioctl[1] = MP_OBJ_FROM_PTR(&pyb_sdcard_obj);
}

#endif // MICROPY_HW_HAS_SDCARD
