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
#include <stdint.h>

#include "py/obj.h"
#include "dma.h"
#include "irq.h"

#include "driverlib/udma.h"
#include "driverlib/sysctl.h"
#include "inc/hw_udma.h"
#include "inc/hw_memmap.h"



uint8_t ui8ControlTable[1024] __attribute__ ((aligned(1024)));


void dma_hw_init(){

    /* Enable uDMA clock */
    SysCtlPeripheralEnable(SYSCTL_PERIPH_UDMA);

    /* Enable the uDMA cotroller */
    uDMAEnable();

    /* Set base address of controltable */
    uDMAControlBaseSet(ui8ControlTable);
}

void dma_init(uint32_t ui32ChannelStructIndex, DMA_Channel_Dir ui8ChnDir){

    if(ui8ChnDir == DMA_CHANNEL_TX)
    {
        // Put the attributes in a known state for the uDMA UART1RX channel.  These
        // should already be disabled by default.
        //
        uDMAChannelAttributeDisable(ui32ChannelStructIndex,
                                    UDMA_ATTR_ALTSELECT | UDMA_ATTR_USEBURST |
                                    UDMA_ATTR_HIGH_PRIORITY |
                                    UDMA_ATTR_REQMASK);

    } else if(ui8ChnDir == DMA_CHANNEL_RX)
    {
                // Put the attributes in a known state for the uDMA UART1RX channel.  These
        // should already be disabled by default.
        //
        uDMAChannelAttributeDisable(ui32ChannelStructIndex,
                                    UDMA_ATTR_ALTSELECT | UDMA_ATTR_USEBURST |
                                    UDMA_ATTR_HIGH_PRIORITY |
                                    UDMA_ATTR_REQMASK);

    }
}

// void dma_deinit(const dma_descr_t *dma_descr) {
//     if (dma_descr != NULL) {
//         HAL_NVIC_DisableIRQ(dma_irqn[dma_descr->id]);
//         dma_handle[dma_descr->id] = NULL;

//         dma_disable_clock(dma_descr->id);
//     }
// }


void spi_dma_tx(uint32_t ui32ChannelStructIndex, const uint16_t *pvSrcAddr, uint8_t *pvDstAddr, size_t bits, size_t len)
{
    uint32_t temp=0;
    if(bits == 0x8)
    {
        temp = UDMA_SIZE_8 | UDMA_SRC_INC_8;
    } else if(bits== 16)
    {
        temp = UDMA_SIZE_16 | UDMA_SRC_INC_16;
    } 

    uDMAChannelControlSet(ui32ChannelStructIndex | UDMA_PRI_SELECT,
                              temp| UDMA_DST_INC_NONE |
                              UDMA_ARB_8);

                                  //
        // Set up the transfer parameters for the uDMA UART TX channel.  This will
        // configure the transfer source and destination and the transfer size.
        // Basic mode is used because the peripheral is making the uDMA transfer
        // request.  The source is the TX buffer and the destination is the UART
        // data register.
        //
    uDMAChannelTransferSet(ui32ChannelStructIndex | UDMA_PRI_SELECT,
                               UDMA_MODE_BASIC, (void*) pvSrcAddr,
                               (void*) pvDstAddr, len);
                               //sizeof(*pvSrcAddr));

                                   //
        // Now both the uDMA UART TX and RX channels are primed to start a
        // transfer.  As soon as the channels are enabled, the peripheral will
        // issue a transfer request and the data transfers will begin.
        //
       uDMAChannelEnable(ui32ChannelStructIndex);
}

void spi_dma_rx(uint32_t ui32ChannelStructIndex, const uint8_t *pvSrcAddr, uint16_t *pvDstAddr, size_t bits, size_t len)
{
    uint32_t temp=0;
    if(bits == 0x8)
    {
        temp = UDMA_SIZE_8 | UDMA_DST_INC_8;
    } else     if(bits== 16)
    {
        temp = UDMA_SIZE_16 | UDMA_DST_INC_16;
    } 

    uDMAChannelControlSet(ui32ChannelStructIndex | UDMA_PRI_SELECT,
                              temp| UDMA_SRC_INC_NONE |
                              UDMA_ARB_8);

                                  //
        // Set up the transfer parameters for the uDMA UART TX channel.  This will
        // configure the transfer source and destination and the transfer size.
        // Basic mode is used because the peripheral is making the uDMA transfer
        // request.  The source is the TX buffer and the destination is the UART
        // data register.
        //
    uDMAChannelTransferSet(ui32ChannelStructIndex | UDMA_PRI_SELECT,
                               UDMA_MODE_BASIC, (void*) pvSrcAddr,
                               (void*) pvDstAddr, len);
                               //sizeof(*pvDstAddr));

                                   //
        // Now both the uDMA UART TX and RX channels are primed to start a
        // transfer.  As soon as the channels are enabled, the peripheral will
        // issue a transfer request and the data transfers will begin.
        //
       uDMAChannelEnable(ui32ChannelStructIndex);
}
