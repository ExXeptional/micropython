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
#include <stdarg.h>

#include "py/runtime.h"
#include "py/stream.h"
#include "py/mperrno.h"
#include "py/mphal.h"
#include "lib/utils/interrupt_char.h"
#include "uart.h"
#include "irq.h"
#include "pendsv.h"
#include "driverlib/rom.h"
#include "driverlib/rom_map.h"
#include "inc/hw_ints.h"
#include "inc/hw_memmap.h"
#include "inc/hw_sysctl.h"
#include "inc/hw_types.h"
#include "inc/hw_uart.h"
#include "driverlib/uart.h"
#include "driverlib/sysctl.h"

/// \moduleref pyb
/// \class UART - duplex serial communication bus
///
/// UART implements the standard UART/USART duplex serial communications protocol.  At
/// the physical level it consists of 2 lines: RX and TX.  The unit of communication
/// is a character (not to be confused with a string character) which can be 8 or 9
/// bits wide.
///
/// UART objects can be created and initialised using:
///
///     from pyb import UART
///
///     uart = UART(1, 9600)                         # init with given baudrate
///     uart.init(9600, bits=8, parity=None, stop=1) # init with given parameters
///
/// Bits can be 8 or 9.  Parity can be None, 0 (even) or 1 (odd).  Stop can be 1 or 2.
///
/// A UART object acts like a stream object and reading and writing is done
/// using the standard stream methods:
///
///     uart.read(10)       # read 10 characters, returns a bytes object
///     uart.read()         # read all available characters
///     uart.readline()     # read a line
///     uart.readinto(buf)  # read and store into the given buffer
///     uart.write('abc')   # write the 3 characters
///
/// Individual characters can be read/written using:
///
///     uart.readchar()     # read 1 character and returns it as an integer
///     uart.writechar(42)  # write 1 character
///
/// To check if there is anything to be read, use:
///
///     uart.any()               # returns True if any characters waiting



enum {
    CHAR_WIDTH_5BIT = 5,
    CHAR_WIDTH_6BIT,
    CHAR_WIDTH_7BIT,
    CHAR_WIDTH_8BIT,
    CHAR_WIDTH_9BIT
};

// doubles the length if char width exeeds 1 byte
#define LARGER_THAN_BYTE(width_of_char)  (width_of_char / CHAR_WIDTH_9BIT)

struct _machine_uart_obj_t {
    mp_obj_base_t base;
    uint32_t uart;
    uint32_t periph;
    periph_uart_t* regs;
    uint32_t irqn;
    machine_uart_t uart_id : 8;
    bool is_enabled : 1;
    bool attached_to_repl;              // whether the UART is attached to REPL
    byte char_width;                    // 0 for 7,8 bit chars, 1 for 9 bit chars
    uint16_t char_mask;                 // 0x7f for 7 bit, 0xff for 8 bit, 0x1ff for 9 bit
    uint16_t timeout;                   // timeout waiting for first char
    uint16_t timeout_char;              // timeout waiting between chars
    uint16_t read_buf_len;              // len in chars; buf can hold len-1 chars
    volatile uint16_t read_buf_head;    // indexes first empty slot
    uint16_t read_buf_tail;             // indexes first full slot (not full if equals head)
    byte *read_buf;                     // byte or uint16_t, depending on char size
};

STATIC mp_obj_t machine_uart_deinit(mp_obj_t self_in);
extern void NORETURN __fatal_error(const char *msg);

void uart_init0(void) {
//    UARTClockSourceSet(UART0_BASE, UART_CLOCK_SYSTEM);
//    UARTClockSourceSet(UART1_BASE, UART_CLOCK_SYSTEM);
//    UARTClockSourceSet(UART2_BASE, UART_CLOCK_SYSTEM);
//    UARTClockSourceSet(UART3_BASE, UART_CLOCK_SYSTEM);
//    UARTClockSourceSet(UART4_BASE, UART_CLOCK_SYSTEM);
//    UARTClockSourceSet(UART5_BASE, UART_CLOCK_SYSTEM);
//    UARTClockSourceSet(UART6_BASE, UART_CLOCK_SYSTEM);
//    UARTClockSourceSet(UART7_BASE, UART_CLOCK_SYSTEM);
    #if defined(STM32H7)
    RCC_PeriphCLKInitTypeDef RCC_PeriphClkInit = {0};
    // Configure USART1/6 clock source
    RCC_PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USART16;
    RCC_PeriphClkInit.Usart16ClockSelection = RCC_USART16CLKSOURCE_D2PCLK2;
    if (HAL_RCCEx_PeriphCLKConfig(&RCC_PeriphClkInit) != HAL_OK) {
        __fatal_error("HAL_RCCEx_PeriphCLKConfig");
    }

    // Configure USART2/3/4/5/7/8 clock source
    RCC_PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USART234578;
    RCC_PeriphClkInit.Usart16ClockSelection = RCC_USART234578CLKSOURCE_D2PCLK1;
    if (HAL_RCCEx_PeriphCLKConfig(&RCC_PeriphClkInit) != HAL_OK) {
        __fatal_error("HAL_RCCEx_PeriphCLKConfig");
    }
    #endif

    for (int i = 0; i < MP_ARRAY_SIZE(MP_STATE_PORT(machine_uart_obj_all)); i++) {
        MP_STATE_PORT(machine_uart_obj_all)[i] = NULL;
    }
}

// unregister all interrupt sources
void uart_deinit(void) {
    for (int i = 0; i < MP_ARRAY_SIZE(MP_STATE_PORT(machine_uart_obj_all)); i++) {
        machine_uart_obj_t *uart_obj = MP_STATE_PORT(machine_uart_obj_all)[i];
        if (uart_obj != NULL) {
            machine_uart_deinit(uart_obj);
        }
    }
}

STATIC bool uart_exists(int uart_id) {
    if (uart_id > MP_ARRAY_SIZE(MP_STATE_PORT(machine_uart_obj_all))) {
        // safeguard against machine_uart_obj_all array being configured too small
        return false;
    }
    switch (uart_id) {
        #if defined(MICROPY_HW_UART0_TX) && defined(MICROPY_HW_UART0_RX)
        case UART_0: return true;
        #endif

        #if defined(MICROPY_HW_UART1_TX) && defined(MICROPY_HW_UART1_RX)
        case UART_1: return true;
        #endif

        #if defined(MICROPY_HW_UART2_TX) && defined(MICROPY_HW_UART2_RX)
        case UART_2: return true;
        #endif

        #if defined(MICROPY_HW_UART3_TX) && defined(MICROPY_HW_UART3_RX)
        case UART_3: return true;
        #endif

        #if defined(MICROPY_HW_UART4_TX) && defined(MICROPY_HW_UART4_RX)
        case UART_4: return true;
        #endif

        #if defined(MICROPY_HW_UART5_TX) && defined(MICROPY_HW_UART5_RX)
        case UART_5: return true;
        #endif

        #if defined(MICROPY_HW_UART6_TX) && defined(MICROPY_HW_UART6_RX)
        case UART_6: return true;
        #endif

        #if defined(MICROPY_HW_UART7_TX) && defined(MICROPY_HW_UART7_RX)
        case UART_7: return true;
        #endif

        default: return false;
    }
}

// assumes Init parameters have been set up correctly
STATIC bool uart_init2(machine_uart_obj_t *uart_obj) {
    uint32_t uart_base;
    uint32_t peripheral;
    uint32_t irqn;
    int uart_unit;

    const pin_obj_t *pins[4] = {0};

    switch (uart_obj->uart_id) {
        #if defined(MICROPY_HW_UART0_TX) && defined(MICROPY_HW_UART0_RX)
        case UART_0:
            uart_unit = 0;
            uart_base = UART0_BASE;
            irqn = INT_UART0_TM4C123;
            pins[0] = MICROPY_HW_UART0_TX;
            pins[1] = MICROPY_HW_UART0_RX;
            peripheral = SYSCTL_PERIPH_UART0;
            break;
        #endif

        #if defined(MICROPY_HW_UART1_TX) && defined(MICROPY_HW_UART1_RX)
        case UART_1:
            // TODO
            return false;
            // uart_unit = 1;
            // uart_base = UART1_BASE;
            // irqn = INT_UART1_TM4C123;
            // pins[0] = MICROPY_HW_UART1_TX;
            // pins[1] = MICROPY_HW_UART1_RX;
            // peripheral = SYSCTL_PERIPH_UART1;
            // #if defined(MICROPY_HW_UART1_RTS)
            // pins[2] = MICROPY_HW_UART1_RTS;
            // #endif
            // #if defined(MICROPY_HW_UART1_CTS)
            // pins[3] = MICROPY_HW_UART1_CTS;
            // #endif
            break;
        #endif

        #if defined(MICROPY_HW_UART2_TX) && defined(MICROPY_HW_UART2_RX)
        case UART_2:
            uart_unit = 2;
            uart_base = UART2_BASE;
            irqn = INT_UART2_TM4C123;
            pins[0] = MICROPY_HW_UART2_TX;
            pins[1] = MICROPY_HW_UART2_RX;
            peripheral = SYSCTL_PERIPH_UART2;
            break;
        #endif

        #if defined(MICROPY_HW_UART3_TX) && defined(MICROPY_HW_UART3_RX)
        case UART_3:
            uart_unit = 3;
            uart_base = UART3_BASE;
            irqn = INT_UART3_TM4C123;
            pins[0] = MICROPY_HW_UART3_TX;
            pins[1] = MICROPY_HW_UART3_RX;
            peripheral = SYSCTL_PERIPH_UART3;
            break;
        #endif

        #if defined(MICROPY_HW_UART4_TX) && defined(MICROPY_HW_UART4_RX)
        case UART_4:
            uart_unit = 4;
            uart_base = UART4_BASE;
            irqn = INT_UART4_TM4C123;
            pins[0] = MICROPY_HW_UART4_TX;
            pins[1] = MICROPY_HW_UART4_RX;
            peripheral = SYSCTL_PERIPH_UART4;
            break;
        #endif

        #if defined(MICROPY_HW_UART5_TX) && defined(MICROPY_HW_UART5_RX)
        case UART_5:
            uart_unit = 5;
            uart_base = UART5_BASE;
            irqn = INT_UART5_TM4C123;
            pins[0] = MICROPY_HW_UART5_TX;
            pins[1] = MICROPY_HW_UART5_RX;
            peripheral = SYSCTL_PERIPH_UART5;
            break;
        #endif

        #if defined(MICROPY_HW_UART6_TX) && defined(MICROPY_HW_UART6_RX)
        case UART_6:
            uart_unit = 6;
            uart_base = UART6_BASE;
            irqn = INT_UART6_TM4C123;
            pins[0] = MICROPY_HW_UART6_TX;
            pins[1] = MICROPY_HW_UART6_RX;
            peripheral = SYSCTL_PERIPH_UART6;
            break;
        #endif

        #if defined(MICROPY_HW_UART7_TX) && defined(MICROPY_HW_UART7_RX)
        case UART_7:
            uart_unit = 7;
            uart_base = UART7_BASE;
            irqn = INT_UART7_TM4C123;
            pins[0] = MICROPY_HW_UART7_TX;
            pins[1] = MICROPY_HW_UART7_RX;
            peripheral = SYSCTL_PERIPH_UART7;
            break;
        #endif

        default:
            // UART does not exist or is not configured for this board
            return false;
    }

    for (uint i = 0; i < 4; i++) {
        if (pins[i] != NULL) {
            bool ret = mp_hal_pin_config_alt(pins[i], PIN_FN_UART, uart_unit);
            if (!ret) {
                return false;
            }
        }
    }

    uart_obj->irqn = irqn;
    uart_obj->uart = uart_base;
    uart_obj->periph = peripheral;
    uart_obj->regs = ((periph_uart_t*) uart_base);

    // init uart_base
//    HAL_UART_Init(&uart_obj->uart);
    MAP_SysCtlPeripheralEnable(uart_obj->periph);
    while(!MAP_SysCtlPeripheralReady(uart_obj->periph)) {};


    uart_obj->is_enabled = true;
    uart_obj->attached_to_repl = false;

    return true;
}

void uart_attach_to_repl(machine_uart_obj_t *self, bool attached) {
    self->attached_to_repl = attached;
}

/* obsolete and unused
bool uart_init(machine_uart_obj_t *uart_obj, uint32_t baudrate) {
    UART_HandleTypeDef *uh = &uart_obj->uart;
    memset(uh, 0, sizeof(*uh));
    uh->Init.BaudRate = baudrate;
    uh->Init.WordLength = UART_WORDLENGTH_8B;
    uh->Init.StopBits = UART_STOPBITS_1;
    uh->Init.Parity = UART_PARITY_NONE;
    uh->Init.Mode = UART_MODE_TX_RX;
    uh->Init.HwFlowCtl = UART_HWCONTROL_NONE;
    uh->Init.OverSampling = UART_OVERSAMPLING_16;
    return uart_init2(uart_obj);
}
*/

mp_uint_t uart_rx_any(machine_uart_obj_t *self) {
    int buffer_bytes = self->read_buf_head - self->read_buf_tail;
    if (buffer_bytes < 0) {
        return buffer_bytes + self->read_buf_len;
    } else if (buffer_bytes > 0) {
        return buffer_bytes;
    } else {
        return !(self->regs->FR & UART_FR_RXFE);
    }
}

// Waits at most timeout milliseconds for at least 1 char to become ready for
// reading (from buf or for direct reading).
// Returns true if something available, false if not.
STATIC bool uart_rx_wait(machine_uart_obj_t *self, uint32_t timeout) {
    uint32_t start = mp_hal_ticks_ms();
    for (;;) {
        if (self->read_buf_tail != self->read_buf_head || (!(self->regs->FR & UART_FR_RXFE))) {
            return true; // have at least 1 char ready for reading
        }
        if (mp_hal_ticks_ms() - start >= timeout) {
            return false; // timeout
        }
        MICROPY_EVENT_POLL_HOOK
    }
}

// assumes there is a character available
int uart_rx_char(machine_uart_obj_t *self) {
    if (self->read_buf_tail != self->read_buf_head) {
        // buffering via IRQ
        int data;
        if (self->char_width == CHAR_WIDTH_9BIT) {
            data = ((uint16_t*)self->read_buf)[self->read_buf_tail];
        } else {
            data = self->read_buf[self->read_buf_tail];
        }
        self->read_buf_tail = (self->read_buf_tail + 1) % self->read_buf_len;
        if (!(self->regs->FR & UART_FR_RXFE)) {
            // UART was stalled by flow ctrl: re-enable IRQ now we have room in buffer
            MAP_UARTIntEnable(self->uart, UART_INT_RX);
        }
        return data;
    } else {
        // no buffering
        // TODO
        uint32_t data = self->regs->DR;
        // if(data & UART_DR_BE) {
            // BREAK ERROR
        // } else if(data & UART_DR_FE){
            // FRAMING ERROR
        // } else if(data & UART_DR_OE){
            // OVERRUN ERROR
        // } else if(data & UART_DR_PE){
            // PARITY ERROR
        // }
        return data & self->char_mask;;
    }
}

// Waits at most timeout milliseconds for TX register to become empty.
// Returns true if can write, false if can't.
STATIC bool uart_tx_wait(machine_uart_obj_t *self, uint32_t timeout) {
    uint32_t start = mp_hal_ticks_ms();
    for (;;) {

        if (!(self->regs->FR & UART_FR_TXFF)) {
            return true; // tx register is not full
        }
        if (mp_hal_ticks_ms() - start >= timeout) {
            return false; // timeout
        }
        MICROPY_EVENT_POLL_HOOK
    }
}

// // Waits at most timeout milliseconds for UART flag to be set.
// // Returns true if flag is/was set, false on timeout.
// STATIC bool uart_wait_flag_set(machine_uart_obj_t *self, uint32_t flag, uint32_t timeout) {
//     // Note: we don't use WFI to idle in this loop because UART tx doesn't generate
//     // an interrupt and the flag can be set quickly if the baudrate is large.
//     uint32_t start = mp_hal_ticks_cpu();
//     for (;;) {
//         if ((self->regs->FR & flag)) {
//             return true;
//         }
//         if (timeout == 0 || mp_hal_ticks_cpu() - start >= timeout) {
//             return false; // timeout
//         }
//     }
// }

// Waits at most timeout milliseconds for UART flag to be set.
// Returns true if flag is/was set, false on timeout.
STATIC bool uart_wait_flag_unset(machine_uart_obj_t *self, uint32_t flag, uint32_t timeout) {
    // Note: we don't use WFI to idle in this loop because UART tx doesn't generate
    // an interrupt and the flag can be set quickly if the baudrate is large.
    uint32_t start = mp_hal_ticks_ms();
    for (;;) {
        if (!(self->regs->FR & flag)) {
            return true;
        }
        if (timeout == 0 || mp_hal_ticks_ms() - start >= timeout) {
            return false; // timeout
        }
        MICROPY_EVENT_POLL_HOOK
    }
}

// src - a pointer to the data to send (16-bit aligned for 9-bit chars)
// num_chars - number of characters to send (9-bit chars count for 2 bytes from src)
// *errcode - returns 0 for success, MP_Exxx on error
// returns the number of characters sent (valid even if there was an error)
STATIC size_t uart_tx_data(machine_uart_obj_t *self, const void *src_in, size_t num_chars, int *errcode) {
    if (num_chars == 0) {
        *errcode = 0;
        return 0;
    }

    uint32_t timeout;
    if (!(self->regs->FR & UART_FR_CTS) && (self->regs->CTL & UART_CTL_CTSEN)) {
        // CTS can hold off transmission for an arbitrarily long time. Apply
        // the overall timeout rather than the character timeout.
        timeout = self->timeout;
    } else {
        // The timeout specified here is for waiting for the TX data register to
        // become empty (ie between chars), as well as for the final char to be
        // completely transferred.  The default value for timeout_char is long
        // enough for 1 char, but we need to double it to wait for the last char
        // to be transferred to the data register, and then to be transmitted.
        timeout = 2 * self->timeout_char;
    }

    const uint8_t *src = (const uint8_t*)src_in;
    size_t num_tx = 0;
    uint32_t data = 0;

    while (num_tx < num_chars) {
        if (!uart_wait_flag_unset(self, UART_FR_TXFF, timeout)) {
            *errcode = MP_ETIMEDOUT;
            return num_tx;
        }
        // if (self->char_width == CHAR_WIDTH_9BIT) {
        //     data = *((uint16_t*)src) & 0x1ff;
        //     src += 2;
        // } else {
            data = *src++;
        // }
        self->regs->DR = 0xFF & data;
        ++num_tx;
    }

    // wait for the UART frame to complete
    if (!uart_wait_flag_unset(self, UART_FR_BUSY, timeout)) {
        *errcode = MP_ETIMEDOUT;
        return num_tx;
    }

    *errcode = 0;
    return num_tx;
}

void uart_tx_strn(machine_uart_obj_t *uart_obj, const char *str, uint len) {
    int errcode;
    uart_tx_data(uart_obj, str, len, &errcode);
}

// this IRQ handler is set up to handle RXNE interrupts only
void uart_irq_handler(mp_uint_t uart_id) {
    // get the uart object
    machine_uart_obj_t *self = MP_STATE_PORT(machine_uart_obj_all)[uart_id - 1];

    if (self == NULL) {
        // UART object has not been set, so we can't do anything, not
        // even disable the IRQ.  This should never happen.
        return;
    }

    if (!(self->regs->FR & UART_FR_RXFE)) {
        if (self->read_buf_len != 0) {
            uint16_t next_head = (self->read_buf_head + 1) % self->read_buf_len;
            if (next_head != self->read_buf_tail) {
                // only read data if room in buf
                int data = self->regs->DR; // clears UART_FLAG_RXNE
                data &= self->char_mask;
                // Handle interrupt coming in on a UART REPL
                // if (self->attached_to_repl && data == mp_interrupt_char) {
                //     // TODO pendsv_kbd_intr();
                //     return;
                // }
                if (self->char_width == CHAR_WIDTH_9BIT) {
                    ((uint16_t*)self->read_buf)[self->read_buf_head] = data;
                } else {
                    self->read_buf[self->read_buf_head] = data;
                }
                self->read_buf_head = next_head;
            } else { // No room: leave char in buf, disable interrupt
                MAP_UARTIntDisable(self->uart, UART_INT_RX);
            }
        }
    }
}

/******************************************************************************/
/* MicroPython bindings                                                       */

STATIC void machine_uart_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    machine_uart_obj_t *self = self_in;
    if (!self->is_enabled) {
        mp_printf(print, "UART(%u)", self->uart_id);
    } else {
        mp_int_t bits;
        if (self->regs->_9BITADDR & UART_9BITADDR_9BITEN){
            bits = 9;
        } else {
            bits = 5 + ((self->regs->LCRH & UART_LCRH_WLEN_8) >> 5);
        }
        mp_printf(print, "UART(%u, baudrate=%u, bits=%u, parity=",
            self->uart_id, (MAP_SysCtlClockGet() * 4) / ((64 * self->regs->IBRD) + self->regs->IBRD), bits);
        if (!(self->regs->LCRH & UART_LCRH_PEN)) {
            mp_print_str(print, "None");
        } else {
            mp_printf(print, self->regs->LCRH & UART_LCRH_EPS ? "odd" : "even");
        }
        if (self->regs->CTL & (UART_CTL_CTSEN | UART_CTL_RTSEN)) {
            mp_printf(print, ", flow=");
            if (self->regs->CTL & UART_CTL_RTSEN) {
                mp_printf(print, "RTS%s", self->regs->CTL & UART_CTL_CTSEN ? "|" : "");
            }
            if (self->regs->CTL & UART_CTL_CTSEN ) {
                mp_printf(print, "CTS");
            }
        }
        mp_printf(print, ", stop=%u, break=%u, timeout=%u, timeout_char=%u, read_buf_len=%u)",
            self->regs->LCRH & UART_LCRH_STP2 ? 2 : 1,
            self->regs->LCRH & UART_LCRH_BRK,
            self->timeout, self->timeout_char,
            self->read_buf_len == 0 ? 0 : self->read_buf_len - 1); // -1 to adjust for usable length of buffer
    }
}

/// \method init(baudrate, bits=8, parity=None, stop=1, *, timeout=1000, timeout_char=0, flow=0, read_buf_len=64)
///
/// Initialise the UART bus with the given parameters:
///
///   - `baudrate` is the clock rate.
///   - `bits` is the number of bits per byte, 5, 6, 7 or 8.
///   - `parity` is the parity, `None`, 0 (even) or 1 (odd).
///   - `stop` is the number of stop bits, 1 or 2.
///   - `timeout` is the timeout in milliseconds to wait for the first character.
///   - `timeout_char` is the timeout in milliseconds to wait between characters.
///   - `flow` is RTS | CTS where RTS == UART_FLOWCONTROL_RX & CTS == UART_FLOWCONTROL_TX
///   - `read_buf_len` is the character length of the read buffer (0 to disable).
STATIC mp_obj_t machine_uart_init_helper(machine_uart_obj_t *self, size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_baudrate, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 9600} },
        { MP_QSTR_bits, MP_ARG_INT, {.u_int = 8} },
        { MP_QSTR_parity, MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_stop, MP_ARG_INT, {.u_int = 1} },
        { MP_QSTR_flow, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = UART_FLOWCONTROL_NONE} },
        { MP_QSTR_timeout, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 10} },
        { MP_QSTR_timeout_char, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_read_buf_len, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 64} },
    };

    // parse args
    struct {
        mp_arg_val_t baudrate, bits, parity, stop, flow, timeout, timeout_char, read_buf_len;
    } args;
    mp_arg_parse_all(n_args, pos_args, kw_args,
        MP_ARRAY_SIZE(allowed_args), allowed_args, (mp_arg_val_t*)&args);

    // set the UART configuration values
    memset(&self->uart, 0, sizeof(self->uart));


    uint32_t config = 0;

    // parity
    mp_int_t bits = args.bits.u_int;
    if (args.parity.u_obj == mp_const_none) {
        config |= UART_CONFIG_PAR_NONE;
    } else {
        mp_int_t parity = mp_obj_get_int(args.parity.u_obj);
        config |= (parity & 1) ? UART_CONFIG_PAR_ODD : UART_CONFIG_PAR_EVEN;
    }

    // number of bits
    if (bits == 5) {
        config |= UART_CONFIG_WLEN_5;
        self->char_width = CHAR_WIDTH_5BIT;
    } else if ( bits == 6) {
        config |= UART_CONFIG_WLEN_6;
        self->char_width = CHAR_WIDTH_6BIT;
    } else if ( bits == 7) {
        config |= UART_CONFIG_WLEN_7;
        self->char_width = CHAR_WIDTH_7BIT;
    } else if ( bits == 8) {
        config |= UART_CONFIG_WLEN_8;
        self->char_width = CHAR_WIDTH_8BIT;
        // 9Bit not supportet atm
//    } else if ( bits == 9) {
//        config |= UART_CONFIG_WLEN_8;
//        UART9BitEnable(self->uart);
    } else {
        mp_raise_ValueError(MP_ERROR_TEXT("unsupported combination of bits and parity"));
    }

    self->char_mask = ~(0xFF << self->char_width);

    // stop bits
    switch (args.stop.u_int) {
        case 1: config |= UART_CONFIG_STOP_ONE; break;
        default: config |= UART_CONFIG_STOP_TWO; break;
    }

    //baud rate
    mp_uint_t req_baudrate = args.baudrate.u_int;


    // extra config (not yet configurable)
//    init->Mode = UART_MODE_TX_RX;
//    init->OverSampling = UART_OVERSAMPLING_16;


    // init UART (if it fails, it's because the port doesn't exist)
    // enables Periph clock
    if (!uart_init2(self)) {
        nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("UART(%d) init failed!"), self->uart_id));
    }

    MAP_UARTDisable(self->uart);

    // now we can config the peripheral
    MAP_UART9BitDisable(self->uart);

    MAP_UARTConfigSetExpClk(self->uart, SysCtlClockGet(), req_baudrate, config);

    // flow control 
    // TODO only with UART1
    // MAP_UARTFlowControlSet(self->uart, args.flow.u_int);

    // set timeout
    self->timeout = args.timeout.u_int;

    // set timeout_char
    // make sure it is at least as long as a whole character (13 bits to be safe)
    // minimum value is 2ms because sys-tick has a resolution of only 1ms
    self->timeout_char = args.timeout_char.u_int;
    uint32_t min_timeout_char = 13000 / args.baudrate.u_int + 2;
    if (self->timeout_char < min_timeout_char) {
        self->timeout_char = min_timeout_char;
    }

    if(self->timeout < self->timeout_char) {
        self->timeout = self->timeout_char;
    }

    // setup the read buffer
    m_del(byte, self->read_buf, self->read_buf_len << LARGER_THAN_BYTE(self->char_width));

    self->read_buf_head = 0;
    self->read_buf_tail = 0;
    if (args.read_buf_len.u_int <= 0) {
        // no read buffer
        self->read_buf_len = 0;
        self->read_buf = NULL;
        MAP_SysCtlIntDisable(self->irqn);
        MAP_UARTIntDisable(self->uart, UART_INT_RX);
    } else {
        // read buffer using interrupts
        self->read_buf_len = args.read_buf_len.u_int + 1; // +1 to adjust for usable length of buffer
        self->read_buf = m_new(byte,  self->read_buf_len << LARGER_THAN_BYTE(self->char_width));

        MAP_UARTIntEnable(self->uart, UART_INT_RX);
        MAP_IntPrioritySet(self->irqn, 0xE0); //lowest
        MAP_SysCtlIntEnable(self->irqn);
    }

    // compute actual baudrate that was configured
    // (this formula assumes UART_OVERSAMPLING_16)
    uint32_t actual_baudrate = 0;
    uint32_t co, clk;
    clk = MAP_UARTClockSourceGet(self->uart);
    if(clk == UART_CLOCK_SYSTEM) {
        MAP_UARTConfigGetExpClk(self->uart, SysCtlClockGet(), &actual_baudrate, &co);
    } else {
        //PIOSC frequency is always 16MHz +-3%
        MAP_UARTConfigGetExpClk(self->uart, 16000000U, &actual_baudrate, &co);
    }

    // check we could set the baudrate within 5%
    uint32_t baudrate_diff;
    if (actual_baudrate > req_baudrate) {
        baudrate_diff = actual_baudrate - req_baudrate;
    } else {
        baudrate_diff = req_baudrate - actual_baudrate;
    }
    if (20 * baudrate_diff > req_baudrate) {
        nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("set baudrate %d is not within 5%% of desired value"), actual_baudrate));
    }

    MAP_UARTEnable(self->uart);

    return mp_const_none;
}

/// \classmethod \constructor(bus, ...)
STATIC mp_obj_t machine_uart_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    // check arguments
    mp_arg_check_num(n_args, n_kw, 1, MP_OBJ_FUN_ARGS_MAX, true);

    // work out port
    int uart_id = 0;
    if (MP_OBJ_IS_STR(args[0])) {
        const char *port = mp_obj_str_get_str(args[0]);
        if (0) {
        #ifdef MICROPY_HW_UART1_NAME
        } else if (strcmp(port, MICROPY_HW_UART1_NAME) == 0) {
            uart_id = UART_1;
        #endif
        #ifdef MICROPY_HW_UART2_NAME
        } else if (strcmp(port, MICROPY_HW_UART2_NAME) == 0) {
            uart_id = UART_2;
        #endif
        #ifdef MICROPY_HW_UART3_NAME
        } else if (strcmp(port, MICROPY_HW_UART3_NAME) == 0) {
            uart_id = UART_3;
        #endif
        #ifdef MICROPY_HW_UART4_NAME
        } else if (strcmp(port, MICROPY_HW_UART4_NAME) == 0) {
            uart_id = UART_4;
        #endif
        #ifdef MICROPY_HW_UART5_NAME
        } else if (strcmp(port, MICROPY_HW_UART5_NAME) == 0) {
            uart_id = UART_5;
        #endif
        #ifdef MICROPY_HW_UART6_NAME
        } else if (strcmp(port, MICROPY_HW_UART6_NAME) == 0) {
            uart_id = UART_6;
        #endif
        } else {
            nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("UART(%s) doesn't exist"), port));
        }
    } else {
        uart_id = mp_obj_get_int(args[0]);
        if (!uart_exists(uart_id)) {
            nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("UART(%d) doesn't exist"), uart_id));
        }
    }

    machine_uart_obj_t *self;
    if (MP_STATE_PORT(machine_uart_obj_all)[uart_id - 1] == NULL) {
        // create new UART object
        self = m_new0(machine_uart_obj_t, 1);
        self->base.type = &machine_uart_type;
        self->uart_id = uart_id;
        MP_STATE_PORT(machine_uart_obj_all)[uart_id - 1] = self;
    } else {
        // reference existing UART object
        self = MP_STATE_PORT(machine_uart_obj_all)[uart_id - 1];
    }

    if (n_args > 1 || n_kw > 0) {
        // start the peripheral
        mp_map_t kw_args;
        mp_map_init_fixed_table(&kw_args, n_kw, args + n_args);
        machine_uart_init_helper(self, n_args - 1, args + 1, &kw_args);
    }

    return self;
}

STATIC mp_obj_t machine_uart_init(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    return machine_uart_init_helper(args[0], n_args - 1, args + 1, kw_args);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(machine_uart_init_obj, 1, machine_uart_init);

/// \method deinit()
/// Turn off the UART bus.
STATIC mp_obj_t machine_uart_deinit(mp_obj_t self_in) {
    machine_uart_obj_t *self = self_in;
    self->is_enabled = false;
    UARTIntUnregister(self->uart);
    MAP_UARTDisable(self->uart);
    MAP_SysCtlIntDisable(self->irqn);
    MAP_SysCtlPeripheralDisable(self->periph);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_uart_deinit_obj, machine_uart_deinit);

/// \method any()
/// Return `True` if any characters waiting, else `False`.
STATIC mp_obj_t machine_uart_any(mp_obj_t self_in) {
    machine_uart_obj_t *self = self_in;
    return MP_OBJ_NEW_SMALL_INT(uart_rx_any(self));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_uart_any_obj, machine_uart_any);

/// \method writechar(char)
/// Write a single character on the bus.  `char` is an integer to write.
/// Return value: `None`.
STATIC mp_obj_t machine_uart_writechar(mp_obj_t self_in, mp_obj_t char_in) {
    machine_uart_obj_t *self = self_in;

    // get the character to write (might be 9 bits)
    uint16_t data = mp_obj_get_int(char_in);

    // write the character
    int errcode;
    if (uart_tx_wait(self, self->timeout)) {
        uart_tx_data(self, &data, 1, &errcode);
    } else {
        errcode = MP_ETIMEDOUT;
    }

    if (errcode != 0) {
        mp_raise_OSError(errcode);
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(machine_uart_writechar_obj, machine_uart_writechar);

/// \method readchar()
/// Receive a single character on the bus.
/// Return value: The character read, as an integer.  Returns -1 on timeout.
STATIC mp_obj_t machine_uart_readchar(mp_obj_t self_in) {
    machine_uart_obj_t *self = self_in;
    if (uart_rx_wait(self, self->timeout)) {
        return MP_OBJ_NEW_SMALL_INT(uart_rx_char(self));
    } else {
        // return -1 on timeout
        return MP_OBJ_NEW_SMALL_INT(-1);
    }
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_uart_readchar_obj, machine_uart_readchar);

// start of the break condition
STATIC mp_obj_t machine_uart_startbreak(mp_obj_t self_in) {
    machine_uart_obj_t *self = self_in;
    self->regs->LCRH |= UART_LCRH_BRK;
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_uart_startbreak_obj, machine_uart_startbreak);

// stop of the break condition, because a break generator is not available
STATIC mp_obj_t machine_uart_stopbreak(mp_obj_t self_in) {
    machine_uart_obj_t *self = self_in;
    self->regs->LCRH &= ~UART_LCRH_BRK;
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_uart_stopbreak_obj, machine_uart_stopbreak);

STATIC const mp_rom_map_elem_t machine_uart_locals_dict_table[] = {
    // instance methods

    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&machine_uart_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&machine_uart_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_any), MP_ROM_PTR(&machine_uart_any_obj) },

    /// \method read([nbytes])
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&mp_stream_read_obj) },
    /// \method readline()
    { MP_ROM_QSTR(MP_QSTR_readline), MP_ROM_PTR(&mp_stream_unbuffered_readline_obj)},
    /// \method readinto(buf[, nbytes])
    { MP_ROM_QSTR(MP_QSTR_readinto), MP_ROM_PTR(&mp_stream_readinto_obj) },
    /// \method write(buf)
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&mp_stream_write_obj) },

    { MP_ROM_QSTR(MP_QSTR_writechar), MP_ROM_PTR(&machine_uart_writechar_obj) },
    { MP_ROM_QSTR(MP_QSTR_readchar), MP_ROM_PTR(&machine_uart_readchar_obj) },
    { MP_ROM_QSTR(MP_QSTR_startbreak), MP_ROM_PTR(&machine_uart_startbreak_obj) },
    { MP_ROM_QSTR(MP_QSTR_stopbreak), MP_ROM_PTR(&machine_uart_stopbreak_obj) },

    // class constants
    { MP_ROM_QSTR(MP_QSTR_RTS), MP_ROM_INT(UART_FLOWCONTROL_RX) },
    { MP_ROM_QSTR(MP_QSTR_CTS), MP_ROM_INT(UART_FLOWCONTROL_TX) },
    { MP_ROM_QSTR(MP_QSTR_ODD), MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_EVEN), MP_ROM_INT(0) },
};

STATIC MP_DEFINE_CONST_DICT(machine_uart_locals_dict, machine_uart_locals_dict_table);

STATIC mp_uint_t machine_uart_read(mp_obj_t self_in, void *buf_in, mp_uint_t size, int *errcode) {
    machine_uart_obj_t *self = self_in;
    byte *buf = buf_in;

    // check that size is a multiple of character width
    if (size & LARGER_THAN_BYTE(self->char_width)) {
        *errcode = MP_EIO;
        return MP_STREAM_ERROR;
    }

    // convert byte size to char size
    size >>= LARGER_THAN_BYTE(self->char_width);

    // make sure we want at least 1 char
    if (size == 0) {
        return 0;
    }

    // wait for first char to become available
    if (!uart_rx_wait(self, self->timeout)) {
        // return EAGAIN error to indicate non-blocking (then read() method returns None)
        *errcode = MP_EAGAIN;
        return MP_STREAM_ERROR;
    }

    // read the data
    byte *orig_buf = buf;
    for (;;) {
        int data = uart_rx_char(self);
        if (self->char_width == CHAR_WIDTH_9BIT) {
            *(uint16_t*)buf = data;
            buf += 2;
        } else {
            *buf++ = data;
        }
        if (--size == 0 || !uart_rx_wait(self, self->timeout_char)) {
            // return number of bytes read
            return buf - orig_buf;
        }
    }
}

STATIC mp_uint_t machine_uart_write(mp_obj_t self_in, const void *buf_in, mp_uint_t size, int *errcode) {
    machine_uart_obj_t *self = self_in;
    const byte *buf = buf_in;

    // check that size is a multiple of character width
    if (size & LARGER_THAN_BYTE(self->char_width)) {
        *errcode = MP_EIO;
        return MP_STREAM_ERROR;
    }

    // wait to be able to write the first character. EAGAIN causes write to return None
    if (!uart_tx_wait(self, self->timeout)) {
        *errcode = MP_EAGAIN;
        return MP_STREAM_ERROR;
    }

    // write the data
    size_t num_tx = uart_tx_data(self, buf, size >> LARGER_THAN_BYTE(self->char_width), errcode);

    if (*errcode == 0 || *errcode == MP_ETIMEDOUT) {
        // return number of bytes written, even if there was a timeout
        return num_tx << LARGER_THAN_BYTE(self->char_width);
    } else {
        return MP_STREAM_ERROR;
    }
}

STATIC mp_uint_t machine_uart_ioctl(mp_obj_t self_in, mp_uint_t request, mp_uint_t arg, int *errcode) {
    machine_uart_obj_t *self = self_in;
    mp_uint_t ret;
    if (request == MP_STREAM_POLL) {
        mp_uint_t flags = arg;
        ret = 0;
        if ((flags & MP_STREAM_POLL_RD) && uart_rx_any(self)) {
            ret |= MP_STREAM_POLL_RD;
        }
        if ((flags & MP_STREAM_POLL_WR) && (self->regs->FR & UART_FR_TXFE)) {
            ret |= MP_STREAM_POLL_WR;
        }
    } else {
        *errcode = MP_EINVAL;
        ret = MP_STREAM_ERROR;
    }
    return ret;
}

STATIC const mp_stream_p_t uart_stream_p = {
    .read = machine_uart_read,
    .write = machine_uart_write,
    .ioctl = machine_uart_ioctl,
    .is_text = false,
};

const mp_obj_type_t machine_uart_type = {
    { &mp_type_type },
    .name = MP_QSTR_UART,
    .print = machine_uart_print,
    .make_new = machine_uart_make_new,
    .getiter = mp_identity_getiter,
    .iternext = mp_stream_unbuffered_iter,
    .protocol = &uart_stream_p,
    .locals_dict = (mp_obj_dict_t*)&machine_uart_locals_dict,
};
