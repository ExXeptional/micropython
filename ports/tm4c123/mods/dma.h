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

#ifndef MICROPY_INCLUDED_STM32_DMA_H
#define MICROPY_INCLUDED_STM32_DMA_H

typedef struct _dma_descr_t dma_descr_t;

typedef enum {
    dma_id_not_defined=-1,
    dma_id_0,
    dma_id_1,
    dma_id_2,
    dma_id_3,
    dma_id_4,
    dma_id_5,
    dma_id_6,
    dma_id_7,
    dma_id_8,
    dma_id_9,
    dma_id_10,
    dma_id_11,
    dma_id_12,
    dma_id_13,
    dma_id_14,
    dma_id_15,
    dma_id_16,
    dma_id_17,
    dma_id_18,
    dma_id_19,
    dma_id_20,
    dma_id_21,
    dma_id_22,
    dma_id_23,
    dma_id_24,
    dma_id_25,
    dma_id_26,
    dma_id_27,
    dma_id_28,
    dma_id_29,
    dma_id_30,
    dma_id_31,
} dma_id_t;

typedef union {
    uint16_t    enabled;    // Used to test if both counters are == 0
    uint8_t     counter[2];
} dma_idle_count_t;
extern volatile dma_idle_count_t dma_idle;
#define DMA_IDLE_ENABLED()  (dma_idle.enabled != 0)

#define DMA_SYSTICK_MASK            0x0e
#define DMA_MSECS_PER_SYSTICK       (DMA_SYSTICK_MASK + 1)
#define DMA_IDLE_TICK_MAX           (8)     // 128 msec
#define DMA_IDLE_TICK(tick)         (((tick) & DMA_SYSTICK_MASK) == 0)

typedef enum{
    DMA_CHANNEL_RX = 0,
    DMA_CHANNEL_TX = 1
} DMA_Channel_Dir;


//void dma_init(DMA_HandleTypeDef *dma, const dma_descr_t *dma_descr, void *data);
void dma_init(uint32_t ui32ChannelStructIndex, DMA_Channel_Dir ui8ChnDir);
//void dma_init_handle(DMA_HandleTypeDef *dma, const dma_descr_t *dma_descr, void *data);
void dma_deinit(const dma_descr_t *dma_descr);
void dma_invalidate_channel(const dma_descr_t *dma_descr);
void dma_idle_handler(int controller);
void dma_hw_init();
void spi_dma_rx(uint32_t ui32ChannelStructIndex, const uint8_t *pvSrcAddr, uint16_t *pvDstAddr, size_t bits, size_t len);
void spi_dma_tx(uint32_t ui32ChannelStructIndex, const uint16_t *pvSrcAddr, uint8_t *pvDstAddr, size_t bits, size_t len);

#endif // MICROPY_INCLUDED_STM32_DMA_H
