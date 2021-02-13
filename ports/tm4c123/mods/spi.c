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

#include <stdio.h>
#include <string.h>

#include "py/runtime.h"
#include "py/mphal.h"
#include "extmod/machine_spi.h"
#include "mperrno.h"
#include "irq.h"
#include "pin.h"
#include "bufhelper.h"
#include "spi.h"
#include "dma.h"

#include "driverlib/ssi.h"
#include "inc/hw_ssi.h"
#include "inc/hw_memmap.h"
#include "driverlib/rom.h"
#include "driverlib/rom_map.h"
#include "driverlib/udma.h"
#include "inc/hw_udma.h"

/// \moduleref pyb
/// \class SPI - a master-driven serial protocol
///
/// SPI is a serial protocol that is driven by a master.  At the physical level
/// there are 3 lines: SCK, MOSI, MISO.
///
/// See usage model of I2C; SPI is very similar.  Main difference is
/// parameters to init the SPI bus:
///
///     from pyb import SPI
///     spi = SPI(1, SPI.MASTER, baudrate=600000, polarity=1, phase=0, crc=0x7)
///
/// Only required parameter is mode, SPI.MASTER or SPI.SLAVE.  Polarity can be
/// 0 or 1, and is the level the idle clock line sits at.  Phase can be 0 or 1
/// to sample data on the first or second clock edge respectively.  Crc can be
/// None for no CRC, or a polynomial specifier.
///
/// Additional method for SPI:
///
///     data = spi.send_recv(b'1234')        # send 4 bytes and receive 4 bytes
///     buf = bytearray(4)
///     spi.send_recv(b'1234', buf)          # send 4 bytes and receive 4 into buf
///     spi.send_recv(buf, buf)              # send/recv 4 bytes from/to buf

// Possible DMA configurations for SPI busses:
// SPI1_TX: DMA2_Stream3.CHANNEL_3 or DMA2_Stream5.CHANNEL_3
// SPI1_RX: DMA2_Stream0.CHANNEL_3 or DMA2_Stream2.CHANNEL_3
// SPI2_TX: DMA1_Stream4.CHANNEL_0
// SPI2_RX: DMA1_Stream3.CHANNEL_0
// SPI3_TX: DMA1_Stream5.CHANNEL_0 or DMA1_Stream7.CHANNEL_0
// SPI3_RX: DMA1_Stream0.CHANNEL_0 or DMA1_Stream2.CHANNEL_0
// SPI4_TX: DMA2_Stream4.CHANNEL_5 or DMA2_Stream1.CHANNEL_4
// SPI4_RX: DMA2_Stream3.CHANNEL_5 or DMA2_Stream0.CHANNEL_4
// SPI5_TX: DMA2_Stream4.CHANNEL_2 or DMA2_Stream6.CHANNEL_7
// SPI5_RX: DMA2_Stream3.CHANNEL_2 or DMA2_Stream5.CHANNEL_7
// SPI6_TX: DMA2_Stream5.CHANNEL_1
// SPI6_RX: DMA2_Stream6.CHANNEL_1

// #if defined(MICROPY_HW_SPI1_SCK)
// SPI_HandleTypeDef SPIHandle1 = {.Instance = NULL};
// #endif
// #if defined(MICROPY_HW_SPI2_SCK)
// SPI_HandleTypeDef SPIHandle2 = {.Instance = NULL};
// #endif
// #if defined(MICROPY_HW_SPI3_SCK)
// SPI_HandleTypeDef SPIHandle3 = {.Instance = NULL};
// #endif
// #if defined(MICROPY_HW_SPI4_SCK)
// SPI_HandleTypeDef SPIHandle4 = {.Instance = NULL};
// #endif
// #if defined(MICROPY_HW_SPI5_SCK)
// SPI_HandleTypeDef SPIHandle5 = {.Instance = NULL};
// #endif
// #if defined(MICROPY_HW_SPI6_SCK)
// SPI_HandleTypeDef SPIHandle6 = {.Instance = NULL};
// #endif

void spi_init0(void) {
    // Initialise the SPI handles.
    // The structs live on the BSS so all other fields will be zero after a reset.
    // #if defined(MICROPY_HW_SPI1_SCK)
    // SPIHandle1.Instance = SPI1;
    // #endif
    // #if defined(MICROPY_HW_SPI2_SCK)
    // SPIHandle2.Instance = SPI2;
    // #endif
    // #if defined(MICROPY_HW_SPI3_SCK)
    // SPIHandle3.Instance = SPI3;
    // #endif
    // #if defined(MICROPY_HW_SPI4_SCK)
    // SPIHandle4.Instance = SPI4;
    // #endif
    // #if defined(MICROPY_HW_SPI5_SCK)
    // SPIHandle5.Instance = SPI5;
    // #endif
    // #if defined(MICROPY_HW_SPI6_SCK)
    // SPIHandle6.Instance = SPI6;
    // #endif
}

STATIC int spi_find(mp_obj_t id) {
    if (MP_OBJ_IS_STR(id)) {
        // given a string id
        const char *port = mp_obj_str_get_str(id);
        if (0) {
        #ifdef MICROPY_HW_SPI0_NAME
        } else if (strcmp(port, MICROPY_HW_SPI0_NAME) == 0) {
            return SPI_0;
        #endif
        #ifdef MICROPY_HW_SPI1_NAME
        } else if (strcmp(port, MICROPY_HW_SPI1_NAME) == 0) {
            return SPI_1;
        #endif
        #ifdef MICROPY_HW_SPI2_NAME
        } else if (strcmp(port, MICROPY_HW_SPI2_NAME) == 0) {
            return SPI_2;
        #endif
        #ifdef MICROPY_HW_SPI3_NAME
        } else if (strcmp(port, MICROPY_HW_SPI3_NAME) == 0) {
            return SPI_3;
        #endif
        }
        nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("SPI(%s) doesn't exist"), port));
    } else {
        // given an integer id
        int spi_id = mp_obj_get_int(id);
        if (spi_id >= 0 && spi_id <= MP_ARRAY_SIZE(MP_STATE_PORT(machine_spi_obj_all))) {
            return spi_id;
        }
        nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("SPI(%d) doesn't exist"), spi_id));
    }
}

// sets the parameters in the SPI_InitTypeDef struct
// if an argument is -1 then the corresponding parameter is not changed
// STATIC void spi_set_params(mp_obj_t *spi_obj, uint8_t prescale, int32_t baudrate,
//     int32_t polarity, int32_t phase, int32_t bits, int32_t firstbit) {
//     machine_hard_spi_obj_t *self = spi_obj;

//     if (prescale != 0xff || baudrate != -1) {
//         if (prescale == 0xff) {
//             // prescaler not given, so select one that yields at most the requested baudrate
//             mp_uint_t spi_clock = MAP_SysCtlClockGet();
//             prescale = (spi_clock / baudrate);
//         }
//         prescale &= 0xFE;
//     }

//     if (polarity != -1) {
//         init->CLKPolarity = polarity == 0 ? SPI_POLARITY_LOW : SPI_POLARITY_HIGH;
//     }

//     if (phase != -1) {
//         init->CLKPhase = phase == 0 ? SPI_PHASE_1EDGE : SPI_PHASE_2EDGE;
//     }

//     if (bits != -1) {
//         init->DataSize = (bits == 16) ? SPI_DATASIZE_16BIT : SPI_DATASIZE_8BIT;
//     }

//     if (firstbit != -1) {
//         init->FirstBit = firstbit;
//     }
// }

// TODO allow to take a list of pins to use
void spi_init(const mp_obj_t *self_in) {
    machine_hard_spi_obj_t *self = (machine_hard_spi_obj_t*) self_in;
    const pin_obj_t *pins[4] = { NULL, NULL, NULL, NULL };

    if (0) {
    #if defined(MICROPY_HW_SPI0_SCK)
    } else if (self->spi_id == SPI_0) {
        self->spi_base = SSI0_BASE;
        self->periph = SYSCTL_PERIPH_SSI0;
        self->regs = (periph_spi_t*)SSI0_BASE;
        self->irqn = INT_SSI0;
        pins[0] = MICROPY_HW_SPI0_SCK;
        self->dma_channel_rx = dma_id_10;
        self->dma_channel_tx = dma_id_11;
        #if defined(MICROPY_HW_SPI0_MISO)
        pins[1] = MICROPY_HW_SPI0_MISO;
        #endif
        #if defined(MICROPY_HW_SPI0_MOSI)
        pins[2] = MICROPY_HW_SPI0_MOSI;
        #endif
        #if defined(MICROPY_HW_SPI0_FSS)
        if(!self->soft_fss) {
            pins[3] = MICROPY_HW_SPI0_FSS;
        }
        #endif
    #endif
    #if defined(MICROPY_HW_SPI1_SCK)
    } else if (self->spi_id == SPI_1) {
        self->spi_base = SSI1_BASE;
        self->periph = SYSCTL_PERIPH_SSI1;
        self->regs = (periph_spi_t*)SSI1_BASE;
        self->irqn = INT_SSI1;
        self->dma_channel_rx = dma_id_10;
        self->dma_channel_tx = dma_id_11;
        pins[0] = MICROPY_HW_SPI1_SCK;
        #if defined(MICROPY_HW_SPI1_MISO)
        pins[1] = MICROPY_HW_SPI1_MISO;
        #endif
        #if defined(MICROPY_HW_SPI1_MOSI)
        pins[2] = MICROPY_HW_SPI1_MOSI;
        #endif
        #if defined(MICROPY_HW_SPI1_FSS)
        if(!self->soft_fss) {
            pins[3] = MICROPY_HW_SPI1_FSS;
        }
        #endif
    #endif
    #if defined(MICROPY_HW_SPI2_SCK)
    } else if (self->spi_id == SPI_2) {
        self->spi_base = SSI2_BASE;
        self->periph = SYSCTL_PERIPH_SSI2;
        self->regs = (periph_spi_t*)SSI2_BASE;
        self->irqn = INT_SSI2;
        self->dma_channel_rx = dma_id_12;
        self->dma_channel_tx = dma_id_13;
        pins[0] = MICROPY_HW_SPI2_SCK;
        #if defined(MICROPY_HW_SPI2_MISO)
        pins[1] = MICROPY_HW_SPI2_MISO;
        #endif
        #if defined(MICROPY_HW_SPI2_MOSI)
        pins[2] = MICROPY_HW_SPI2_MOSI;
        #endif
        #if defined(MICROPY_HW_SPI2_FSS)
        if(!self->soft_fss) {
            pins[3] = MICROPY_HW_SPI2_FSS;
        }
        #endif
    #endif
    #if defined(MICROPY_HW_SPI3_SCK)
    } else if (self->spi_id == SPI_3) {
        self->spi_base = SSI3_BASE;
        self->periph = SYSCTL_PERIPH_SSI3;
        self->regs = (periph_spi_t*)SSI3_BASE;
        self->irqn = INT_SSI3;
        self->dma_channel_rx = dma_id_14;
        self->dma_channel_tx = dma_id_15;
        pins[0] = MICROPY_HW_SPI3_SCK;
        #if defined(MICROPY_HW_SPI3_MISO)
        pins[1] = MICROPY_HW_SPI3_MISO;
        #endif
        #if defined(MICROPY_HW_SPI3_MOSI)
        pins[2] = MICROPY_HW_SPI3_MOSI;
        #endif
        #if defined(MICROPY_HW_SPI3_FSS)
        if(!self->soft_fss) {
            pins[3] = MICROPY_HW_SPI3_FSS;
        }
        #endif
    #endif
    } else {
        // SPI does not exist for this board (shouldn't get here, should be checked by caller)
        return;
    }

    // init the GPIO lines
    for (uint i = 0; i < (self->soft_fss ? 3 : 4); i++) {
        if (pins[i] == NULL) {
            continue;
        }
        mp_hal_pin_config_alt(pins[i], PIN_FN_SSI, self->spi_id);
        // idle_high_polarity
        if(i==0 && (self->protocol & 0x1) ) MAP_GPIOPadConfigSet(pins[i]->gpio, pins[i]->pin_mask, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD_WPU);
    }

    // init the SPI device

    SysCtlPeripheralEnable(self->periph);
    while(!SysCtlPeripheralReady(self->periph));
    IntDisable(self->irqn);
    SSIDisable(self->spi_base);
    
    SSIClockSourceSet(self->spi_base, SSI_CLOCK_SYSTEM);
    SSIConfigSetExpClk(self->spi_base, MAP_SysCtlClockGet(), self->protocol, self->mode, self->baudrate, self->bits);
    

    if(self->dma_enabled == SSI_DMA_RX)
    {
        dma_init(self->dma_channel_rx, DMA_CHANNEL_RX);
    }
    else if (self->dma_enabled == SSI_DMA_TX)
    {
        dma_init(self->dma_channel_tx, DMA_CHANNEL_TX);
    }
    else if (self->dma_enabled == (SSI_DMA_TX | SSI_DMA_RX))
    {
        /* code */
        dma_init(self->dma_channel_tx, DMA_CHANNEL_TX);
        dma_init(self->dma_channel_rx, DMA_CHANNEL_RX);
    }

    SSIDMADisable(self->spi_base, SSI_DMA_TX | SSI_DMA_RX);
    SSIDMAEnable(self->spi_base, self->dma_enabled);
    

    SSIEnable(self->spi_base);
}

void spi_deinit(const mp_obj_t *self_in) {
    machine_hard_spi_obj_t* self = (machine_hard_spi_obj_t*) self_in;
    SSIDisable(self->spi_base);
    SysCtlPeripheralDisable(self->periph);
}



/******************************************************************************/
// Implementation of hard SPI for machine module

// Waits at most timeout milliseconds for SSI flag to be set.
// Returns true if flag is/was set, false on timeout.
STATIC bool spi_tx_wait(machine_hard_spi_obj_t *self, uint32_t timeout) {
    uint32_t start = mp_hal_ticks_ms();
    for (;;) {
        if (self->regs->SR & SSI_SR_TNF) {
            return true;
        }
        if (timeout == 0 || mp_hal_ticks_ms() - start >= timeout) {
            return false; // timeout
        }
        MICROPY_EVENT_POLL_HOOK
    }
}

STATIC bool spi_rx_wait(machine_hard_spi_obj_t *self, uint32_t timeout) {
    uint32_t start = mp_hal_ticks_ms();
    for (;;) {
        if (self->regs->SR & SSI_SR_RNE) {
            return true;
        }
        if (timeout == 0 || mp_hal_ticks_ms() - start >= timeout) {
            return false; // timeout
        }
        MICROPY_EVENT_POLL_HOOK
    }
}

STATIC bool spi_wait_flag_unset(machine_hard_spi_obj_t *self, uint32_t flag, uint32_t timeout) {
    // Note: we don't use WFI to idle in this loop because UART tx doesn't generate
    // an interrupt and the flag can be set quickly if the baudrate is large.
    uint32_t start = mp_hal_ticks_ms();
    for (;;) {
        if (!(self->regs->SR & flag)) {
            return true;
        }
        if (timeout == 0 || mp_hal_ticks_ms() - start >= timeout) {
            return false; // timeout
        }
        MICROPY_EVENT_POLL_HOOK
    }
}

mp_uint_t spi_tx_only(machine_hard_spi_obj_t *self, const uint16_t* data, size_t len, uint32_t word_timeout, int *errcode) {
    uint num_tx = 0;
    uint32_t dummy;
    volatile uint32_t dat;

    /* when dmaa is enabled for trnsmitt */
    if(self->dma_enabled == SSI_DMA_TX)
    {
        spi_dma_tx(self->dma_channel_tx, data,(uint8_t*) (self->spi_base + SSI_O_DR), self->bits,len);
        return len;
    }
    else {
        while(num_tx < len) {

            if(!spi_tx_wait(self, word_timeout)) {
                *errcode = MP_ETIMEDOUT;
                return num_tx;
            }
            // Reversing bit order of data
            if(self->lsb_first) {
                asm volatile("rbit %1,%0" : "=r" (dat) : "r" (data[num_tx]));
                dat >>= (32 - self->bits);
                //nop here?
                SSIDataPutNonBlocking(self->spi_base, dat);
            } else {
                SSIDataPutNonBlocking(self->spi_base, data[num_tx]);
            }   
            SSIDataGetNonBlocking(self->spi_base, &dummy);
            num_tx++;
        }

        if(!spi_wait_flag_unset(self, SSI_SR_BSY, word_timeout)) {
            *errcode = MP_ETIMEDOUT;
            return num_tx;
        }

        *errcode = 0;
        return num_tx;
    }
}

mp_uint_t spi_rx_only(machine_hard_spi_obj_t *self, uint16_t* data, size_t len, uint32_t word_timeout, int *errcode) {
    uint num_rx = 0;
    uint32_t dummy = 0;
    uint32_t dumm;

        /* when dmaa is enabled for receive */
    if(self->dma_enabled == SSI_DMA_RX)
    {
        spi_dma_rx(self->dma_channel_rx,(uint8_t*) (self->spi_base + SSI_O_DR), data, self->bits,len);
        return len;
    }
    else{
        while(num_rx < len) {

            if(!spi_rx_wait(self, word_timeout)) {
                *errcode = MP_ETIMEDOUT;
                return num_rx;
            }

            SSIDataPutNonBlocking(self->spi_base, dummy);
            num_rx += SSIDataGetNonBlocking(self->spi_base, &dumm);
            // Reversing bit order of data
            if(self->lsb_first) {
                asm volatile("rbit %1,%0" : "=r" (dumm) : "r" (dumm));
                dumm >>= (32 - self->bits);
                //nop here?
            }
            data[num_rx] = dumm;
        }

        if(!spi_wait_flag_unset(self, SSI_SR_BSY, word_timeout)) {
            *errcode = MP_ETIMEDOUT;
            return num_rx;
        }

        *errcode = 0;
        return num_rx;
    }
}


mp_uint_t spi_rx_tx(machine_hard_spi_obj_t *self, const uint16_t* data_tx, uint16_t* data_rx, size_t len, uint32_t word_timeout, int *errcode) {
    uint num_rtx = 0;
    uint32_t dumm;
    volatile uint32_t dat;

    /* when dmaa is enabled for receive and transmit */
    if(self->dma_enabled == (SSI_DMA_TX | SSI_DMA_RX))
    {
        spi_dma_rx(self->dma_channel_rx,(uint8_t*) (self->spi_base + SSI_O_DR), data_rx, self->bits,len);
        spi_dma_tx(self->dma_channel_tx, data_tx,(uint8_t*) (self->spi_base + SSI_O_DR), self->bits,len);
        return len;
    }
    else{
        while(num_rtx < len) {

            // if(!spi_rx_wait(self, word_timeout)) {
                //   *errcode = MP_ETIMEDOUT;
                // return num_rtx;
                //}

            /* if(!spi_tx_wait(self, word_timeout)) {
                    *errcode = MP_ETIMEDOUT;
                    return num_rtx;
                }*/

                // Reversing bit order of data
                if(self->lsb_first) {
                    asm volatile("rbit %1,%0" : "=r" (dat) : "r" (data_tx[num_rtx]));
                    dat >>= (32 - self->bits);
                    //nop here?
                    SSIDataPutNonBlocking(self->spi_base, dat);
                } else {
                    SSIDataPutNonBlocking(self->spi_base, data_tx[num_rtx]);
                }   
            int a;
                for(int i = 0; i<100000; i++)
                {
                    a++;
                }
                SSIDataGetNonBlocking(self->spi_base, &dumm);
                if(self->lsb_first) {
                    asm volatile("rbit %1,%0" : "=r" (dumm) : "r" (dumm));
                    dumm >>= (32 - self->bits);
                    //nop here?
                }
                data_rx[num_rtx] = dumm;
                num_rtx++;
        }

            if(!spi_wait_flag_unset(self, SSI_SR_BSY, word_timeout)) {
                *errcode = MP_ETIMEDOUT;
                return num_rtx;
            }

            *errcode = 0;
            return num_rtx;
            }
}

// A transfer of "len" bytes should take len*8*1000/baudrate milliseconds.
// To simplify the calculation we assume the baudrate is never less than 8kHz
// and use that value for the baudrate in the formula, plus a small constant.
#define SPI_TRANSFER_TIMEOUT(len) ((len) + 100)

STATIC void spi_transfer(const mp_obj_t *self_in, size_t len, const uint16_t *src, uint16_t *dest, uint32_t timeout) {
    mp_int_t status = 0;
    uint bytes_trans = 0;
    machine_hard_spi_obj_t* self = (machine_hard_spi_obj_t*) self_in;
    
    if (dest == NULL) {
        // send only
        if (len <= 4 || query_irq() == IRQ_STATE_DISABLED) {
            bytes_trans = spi_tx_only(self, src, len, timeout, &status);
        } else {
            // TODO
        }
    } else if (src == NULL) {
        // receive only
        if (len == 1 || query_irq() == IRQ_STATE_DISABLED) {
            bytes_trans = spi_rx_only(self, dest, len, timeout, &status);
        } else {
            // TODO
        }
    } else {
        // send and receive
        if (len <= 16 || query_irq() == IRQ_STATE_DISABLED) {
            bytes_trans = spi_rx_tx(self, src, dest, len, timeout, &status);
        } else {
            // TODO
        }
    }

    if (status != 0) {
        mp_hal_raise(status);
    }
    if (bytes_trans != len) {
        mp_hal_raise(MP_EFAULT);
    }
}

STATIC void machine_hard_spi_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    machine_hard_spi_obj_t *spi = (machine_hard_spi_obj_t*) self_in;;

    mp_printf(print, "SPI(%u", spi->spi_id);
    
    if(spi->mode == SSI_MODE_MASTER) {
        mp_printf(print, ", SPI.MASTER");
            
        mp_printf(print, ", baudrate=%u", spi->baudrate);
            
    } else if(spi->mode == SSI_MODE_SLAVE){
        mp_printf(print, ", SPI.SLAVE");
    } else if(spi->mode == SSI_MODE_SLAVE_OD) {
        mp_printf(print, ", SPI.SLAVE_OD");
    }
    
    mp_printf(print, ", protocol=");
    if(spi->protocol == SSI_FRF_MOTO_MODE_0) mp_printf(print, "SPI0");
    else if(spi->protocol == SSI_FRF_MOTO_MODE_1) mp_printf(print, "SPI1");
    else if(spi->protocol == SSI_FRF_MOTO_MODE_2) mp_printf(print, "SPI2");
    else if(spi->protocol == SSI_FRF_MOTO_MODE_3) mp_printf(print, "SPI3");
    else if(spi->protocol == SSI_FRF_TI) mp_printf(print, "TI");
    else if(spi->protocol == SSI_FRF_NMW) mp_printf(print, "MICROWIRE");
    mp_printf(print, ", bits=%u)", spi->bits);
}

STATIC mp_obj_t machine_hard_spi_init_helper(mp_obj_t* self_in, size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_mode, ARG_id, ARG_baudrate,/* ARG_prescaler,*/ ARG_polarity, ARG_phase, ARG_bits, ARG_fss, ARG_protocol, ARG_dma, ARG_firstbit, ARG_sck, ARG_mosi, ARG_miso };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_mode,     MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = SSI_MODE_MASTER} }, // Default as master
        { MP_QSTR_id,       MP_ARG_OBJ, {.u_obj = MP_OBJ_NEW_SMALL_INT(-1)} },
        { MP_QSTR_baudrate, MP_ARG_INT, {.u_int = 500000} },
        //{ MP_QSTR_prescaler, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0xFF} },       // max prescaler
        { MP_QSTR_polarity, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} }, // for api compat         
        { MP_QSTR_phase,    MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} }, // for api compat
        //{ MP_QSTR_dir,      MP_ARG_KW_ONLY | MP_ARG_INT,  {.u_int = SSI_CR1_DIR} },
        { MP_QSTR_bits,     MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 8} }, // word length
        { MP_QSTR_fss,   MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false}}, // chip select mode (software or hardware)
        { MP_QSTR_protocol,      MP_ARG_KW_ONLY | MP_ARG_INT,  {.u_int = -1} }, // SSI frame format
        { MP_QSTR_dma,      MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0}}, // no dma
        { MP_QSTR_firstbit, MP_ARG_KW_ONLY | MP_ARG_INT,  {.u_int = 0} }, // for api compat, no hardware support
        //{ MP_QSTR_ti,       MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false} },
        //{ MP_QSTR_crc,      MP_ARG_KW_ONLY | MP_ARG_OBJ,  {.u_obj = mp_const_none} },
        { MP_QSTR_sck,      MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} }, // api compat, only fixed values, not implemented
        { MP_QSTR_mosi,     MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_miso,     MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    machine_hard_spi_obj_t *self = (machine_hard_spi_obj_t*)self_in;    

    // here we would check the sck/mosi/miso pins and configure them, but it's not implemented
    if (args[ARG_sck].u_obj != MP_OBJ_NULL
        || args[ARG_mosi].u_obj != MP_OBJ_NULL
        || args[ARG_miso].u_obj != MP_OBJ_NULL) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, MP_ERROR_TEXT("explicit choice of sck/mosi/miso is not implemented")));
    }

    // set the SPI configuration values
    if(args[ARG_mode].u_int > 2) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, MP_ERROR_TEXT("Mode accepts only MASTER or SLAVE")));
    }
    self->mode = args[ARG_mode].u_int;
  
    if(args[ARG_bits].u_int >= 16 || args[ARG_bits].u_int <= 4) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, MP_ERROR_TEXT("invalid word length, only values 4..16 are available")));
    }
    self->bits = args[ARG_bits].u_int;

    // if(args[ARG_prescaler].u_int & 0xFFFFFF00) {
    //     mp_raise_ValueError( "prescaler only 2-254!"));
    // }
    // uint8_t prescale = args[ARG_prescaler].u_int;

    self->lsb_first = (bool) (args[ARG_firstbit].u_int & 0x1);

    if(!self->mode) {
        if(args[ARG_baudrate].u_int >= 25000000) {
            nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, MP_ERROR_TEXT("baudrate too high, max 25 Mbaud as master")));
        }
    } else {
        if(args[ARG_baudrate].u_int >= 6666666) {
            nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, MP_ERROR_TEXT("baudrate too high, max 6.66 Mbaud as slave")));
        }
    }
    self->baudrate = args[ARG_baudrate].u_int;

    uint8_t phase = (uint8_t)args[ARG_phase].u_int & 0xFF;
    uint8_t polarity = (uint8_t)args[ARG_polarity].u_int & 0xFF;
    uint32_t protocol = args[ARG_protocol].u_int;

    // support both polarity/phase and protocol but only one at a time, protocol takes priority
    if(polarity < 0  || polarity > 1) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, MP_ERROR_TEXT("polarity not correct; IDLE_LOW or IDLE_HIGH")));
    }
    if(phase < 0 || phase > 1) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, MP_ERROR_TEXT("phase not correct; FIRST_EDGE or SECOND_EDGE")));
    }
    if(protocol != -1){
        if(!(protocol == SSI_FRF_MOTO_MODE_0 || protocol == SSI_FRF_MOTO_MODE_1  ||
            protocol == SSI_FRF_MOTO_MODE_2 || protocol == SSI_FRF_MOTO_MODE_3  ||
            protocol == SSI_FRF_TI || protocol == SSI_FRF_NMW)) {
            nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, MP_ERROR_TEXT("protocol not supported, please use SPI0..3, TI or MICROWIRE!")));
        }
        self->protocol = protocol;
    } else {
        self->protocol = ((phase & 0x1) << 1) | ((polarity & 0x1) << 1);
    }

//    if(args[ARG_fss].u_int != 0 || args[ARG_fss].u_int != 1) {
//        nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "fss only accepts CS_HARD(0) or CS_SOFT(1)"));
//    }
    // Automatic or Manually assert chipselect? Only applicable in SPI mode
    if(!(self->protocol & 0xF0)) {
        self->soft_fss = args[ARG_fss].u_bool;
    } else {
        self->soft_fss = false;
    }

    if(!(args[ARG_dma].u_int <= 3 && args[ARG_dma].u_int >= 0)) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, MP_ERROR_TEXT("dma accepts only DMA_NONE(0), DMA_RX(1), DMA_TX(2) or DMA_BOTH(3)")));
    }
    self->dma_enabled = args[ARG_dma].u_int;
    
    // init the SPI bus
    spi_init(self_in);

    return mp_const_none;
}

STATIC mp_obj_t machine_hard_spi_init(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {  
    return machine_hard_spi_init_helper(args[0], n_args - 1, args + 1, kw_args);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(machine_spi_init_obj, 1, machine_hard_spi_init);


mp_obj_t machine_hard_spi_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {

    // check arguments
    mp_arg_check_num(n_args, n_kw, 1, MP_OBJ_FUN_ARGS_MAX, true);

    // find SPI port
    spi_id_t spi_id = spi_find(all_args[0]);

    // create dynamic peripheral object
    machine_hard_spi_obj_t *self;

    // get SPI object
    if (MP_STATE_PORT(machine_spi_obj_all)[spi_id - 1] == NULL) {
        // create new SSI object
        self = m_new0(machine_hard_spi_obj_t, 1);
        self->base.type = &machine_hard_spi_type;
        self->spi_id = spi_id;
        MP_STATE_PORT(machine_spi_obj_all)[spi_id - 1] = self;
    } else {
        // reference existing SSI object
        self = MP_STATE_PORT(machine_spi_obj_all)[spi_id - 1];
    }

    if (n_args > 1 || n_kw > 0) {
        // start the peripheral
        mp_map_t kw_args;
        mp_map_init_fixed_table(&kw_args, n_kw, all_args + n_args);
        mp_obj_t* self_out = (mp_obj_t*) self;
        machine_hard_spi_init_helper(self_out, n_args - 1, all_args + 1, &kw_args);
    }

    return MP_OBJ_FROM_PTR(self);
}

STATIC mp_obj_t machine_hard_spi_deinit(mp_obj_t self_in) {
    // machine_hard_spi_obj_t *self = (machine_hard_spi_obj_t*)self_in;
    spi_deinit(&self_in);
    return mp_const_none;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_spi_deinit_obj, machine_hard_spi_deinit);


// STATIC mp_uint_t machine_hard_spi_transmit(const machine_hard_spi_obj_t *self, size_t len, const uint8_t *src, uint32_t timeout) {
    
// }

STATIC void machine_hard_spi_transfer(mp_obj_base_t *self_in, size_t len, const uint16_t *src, uint16_t *dest) {
    // machine_hard_spi_obj_t *self = (machine_hard_spi_obj_t*)self_in;
    spi_transfer((mp_obj_t*)self_in, len, src, dest, SPI_TRANSFER_TIMEOUT(len));
}

STATIC mp_obj_t mp_machine_hard_spi_read(size_t n_args, const mp_obj_t *args) {
    vstr_t vstr;
    vstr_init_len(&vstr, mp_obj_get_int(args[1]));
    memset(vstr.buf, n_args == 3 ? mp_obj_get_int(args[2]) : 0, vstr.len);
    machine_hard_spi_transfer(args[0], vstr.len, (uint16_t*)vstr.buf, (uint16_t*)vstr.buf);
    return mp_obj_new_str_from_vstr(&mp_type_bytes, &vstr);
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mp_machine_hard_spi_read_obj, 2, 3, mp_machine_hard_spi_read);

STATIC mp_obj_t mp_machine_hard_spi_readinto(size_t n_args, const mp_obj_t *args) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[1], &bufinfo, MP_BUFFER_WRITE);
    memset(bufinfo.buf, n_args == 3 ? mp_obj_get_int(args[2]) : 0, bufinfo.len);
    machine_hard_spi_transfer(args[0], bufinfo.len, bufinfo.buf, bufinfo.buf);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mp_machine_hard_spi_readinto_obj, 2, 3, mp_machine_hard_spi_readinto);

STATIC mp_obj_t mp_machine_hard_spi_write(mp_obj_t self, mp_obj_t wr_buf) {
    mp_buffer_info_t src;
    mp_get_buffer_raise(wr_buf, &src, MP_BUFFER_READ);
    if(src.typecode == 0x48)
    {
        src.len = src.len/2;
    }
    machine_hard_spi_transfer(self, src.len, (const uint16_t*)src.buf, NULL);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(mp_machine_hard_spi_write_obj, mp_machine_hard_spi_write);

STATIC mp_obj_t mp_machine_hard_spi_write_readinto(mp_obj_t self, mp_obj_t wr_buf, mp_obj_t rd_buf) {
    mp_buffer_info_t src;
    mp_get_buffer_raise(wr_buf, &src, MP_BUFFER_READ);
    mp_buffer_info_t dest;
    mp_get_buffer_raise(rd_buf, &dest, MP_BUFFER_WRITE);
    if (src.len != dest.len) {
        mp_raise_ValueError(MP_ERROR_TEXT("buffers must be the same length"));
    }
    if(src.typecode == 0x48)
    {
        src.len = src.len/2;
    }

    machine_hard_spi_transfer(self, src.len, src.buf, dest.buf);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_3(mp_machine_hard_spi_write_readinto_obj, mp_machine_hard_spi_write_readinto);

STATIC const mp_rom_map_elem_t machine_hard_spi_locals_dict_table[] = {
    // instance methods
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&machine_spi_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&machine_spi_deinit_obj) },

    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&mp_machine_hard_spi_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_readinto), MP_ROM_PTR(&mp_machine_hard_spi_readinto_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&mp_machine_hard_spi_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_write_readinto), MP_ROM_PTR(&mp_machine_hard_spi_write_readinto_obj) },

    // class constants
    /// \constant MASTER - for initialising the bus to master mode
    /// \constant SLAVE - for initialising the bus to slave mode
    /// \constant MSB - set the first bit to MSB
    /// \constant LSB - set the first bit to LSB
    { MP_ROM_QSTR(MP_QSTR_MASTER), MP_ROM_INT(SSI_MODE_MASTER) },
    { MP_ROM_QSTR(MP_QSTR_SLAVE),  MP_ROM_INT(SSI_MODE_SLAVE) },
    { MP_ROM_QSTR(MP_QSTR_SLAVE_OD),  MP_ROM_INT(SSI_MODE_SLAVE_OD) },
    { MP_ROM_QSTR(MP_QSTR_MSB),    MP_ROM_INT(0) },
    { MP_ROM_QSTR(MP_QSTR_LSB),    MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_SPI0),    MP_ROM_INT(SSI_FRF_MOTO_MODE_0)},
    { MP_ROM_QSTR(MP_QSTR_SPI1),    MP_ROM_INT(SSI_FRF_MOTO_MODE_1)},
    { MP_ROM_QSTR(MP_QSTR_SPI2),    MP_ROM_INT(SSI_FRF_MOTO_MODE_2)},
    { MP_ROM_QSTR(MP_QSTR_SPI3),    MP_ROM_INT(SSI_FRF_MOTO_MODE_3)},
    { MP_ROM_QSTR(MP_QSTR_TI),      MP_ROM_INT(SSI_FRF_TI)},
    { MP_ROM_QSTR(MP_QSTR_MICROWIRE),    MP_ROM_INT(SSI_FRF_NMW)},
    { MP_ROM_QSTR(MP_QSTR_FIRST_EDGE),    MP_ROM_INT(0)},
    { MP_ROM_QSTR(MP_QSTR_SECOND_EDGE),    MP_ROM_INT(1)},
    { MP_ROM_QSTR(MP_QSTR_IDLE_LOW),    MP_ROM_INT(0)},
    { MP_ROM_QSTR(MP_QSTR_IDLE_HIGH),    MP_ROM_INT(1)},
    { MP_ROM_QSTR(MP_QSTR_FSS_SOFT),    MP_ROM_INT(true)},
    { MP_ROM_QSTR(MP_QSTR_FSS_HARD),    MP_ROM_INT(false)},
    { MP_ROM_QSTR(MP_QSTR_DMA_NONE),    MP_ROM_INT(0)},
    { MP_ROM_QSTR(MP_QSTR_DMA_RX),    MP_ROM_INT(SSI_DMA_RX)},
    { MP_ROM_QSTR(MP_QSTR_DMA_TX),    MP_ROM_INT(SSI_DMA_TX)},
    { MP_ROM_QSTR(MP_QSTR_DMA_BOTH),    MP_ROM_INT(SSI_DMA_TX | SSI_DMA_RX)},

    /* TODO
    { MP_ROM_QSTR(MP_QSTR_DIRECTION_2LINES             ((uint32_t)0x00000000)
    { MP_ROM_QSTR(MP_QSTR_DIRECTION_2LINES_RXONLY      SPI_CR1_RXONLY
    { MP_ROM_QSTR(MP_QSTR_DIRECTION_1LINE              SPI_CR1_BIDIMODE
    { MP_ROM_QSTR(MP_QSTR_NSS_SOFT                    SPI_CR1_SSM
    { MP_ROM_QSTR(MP_QSTR_NSS_HARD_INPUT              ((uint32_t)0x00000000)
    { MP_ROM_QSTR(MP_QSTR_NSS_HARD_OUTPUT             ((uint32_t)0x00040000)
    */
};

STATIC MP_DEFINE_CONST_DICT(machine_hard_spi_locals_dict, machine_hard_spi_locals_dict_table);

// STATIC const mp_machine_spi_p_t machine_hard_spi_p = {
//     .init = machine_hard_spi_init,
//     .deinit = machine_hard_spi_deinit,
//     .transfer = machine_hard_spi_transfer,
// };

const mp_obj_type_t machine_hard_spi_type = {
    { &mp_type_type },
    .name = MP_QSTR_SPI,
    .print = machine_hard_spi_print,
    .make_new = machine_hard_spi_make_new,
    // .protocol = &machine_hard_spi_p,
    .locals_dict = (mp_obj_t)&machine_hard_spi_locals_dict,
};

// const spi_t *spi_from_mp_obj(mp_obj_t o) {
//     if (MP_OBJ_IS_TYPE(o, &machine_hard_spi_type)) {
//         machine_hard_spi_obj_t *self = o;;
//         return self->spi;
//     } else {
//         mp_raise_TypeError("expecting an SPI object");
//     }
// }
