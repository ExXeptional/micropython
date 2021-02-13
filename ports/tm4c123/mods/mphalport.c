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
#include <mphalport.h>

#include "py/runtime.h"
#include "py/mperrno.h"
#include "py/mphal.h"
#include "extmod/misc.h"
#include "driverlib/pin_map.h"
#include "inc/hw_gpio.h"
#include "inc/hw_memmap.h"
//#include "usb.h"
#include "uart.h"

// prevent clash between driverlib and CMSIS
#ifdef NVIC_BASE
#undef NVIC_BASE
#endif

#ifdef DWT_BASE
#undef DWT_BASE
#endif

#ifdef ITM_BASE
#undef ITM_BASE
#endif

#include CMSIS_HEADER

// this table converts from HAL_StatusTypeDef to POSIX errno
//const byte mp_hal_status_to_errno_table[4] = {
//    [HAL_OK] = 0,
//    [HAL_ERROR] = MP_EIO,
//    [HAL_BUSY] = MP_EBUSY,
//    [HAL_TIMEOUT] = MP_ETIMEDOUT,
//};

NORETURN void mp_hal_raise(int status) {
   mp_raise_OSError(status);
}

void mp_hal_ticks_cpu_enable(void) {
   if (!(DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk)) {
       CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
       #if defined(__CORTEX_M) && __CORTEX_M == 7
       // on Cortex-M7 we must unlock the DWT before writing to its registers
       DWT->LAR = 0xc5acce55;
       #endif
       DWT->CYCCNT = 0;
       DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
   }
}

mp_uint_t mp_hal_ticks_cpu(void) {
    return (*((volatile uint32_t *)0xE000E018));
}

//mp_uint_t mp_hal_ticks_ms(void) {
//    return (*((volatile uint32_t *)0xE000E018)) / (MAP_SysCtlClockGet()*3000);
//}
//
//mp_uint_t mp_hal_ticks_us(void) {
//    return (*((volatile uint32_t *)0xE000E018)) / (MAP_SysCtlClockGet()*3000000);
//}

void mp_hal_gpio_clock_enable(const uint32_t periph) {
    if (MAP_SysCtlPeripheralReady(periph)) {
        //Already acive
        return;
    }
    MAP_SysCtlPeripheralEnable(periph);
    while(!MAP_SysCtlPeripheralReady(periph)){};
}

void mp_hal_unlock_special_pin(mp_hal_pin_obj_t pin) {
    pin->regs->LOCK = GPIO_LOCK_KEY;
    pin->regs->CR |= pin->pin_mask;
}

bool mp_hal_pin_needs_unlocking(mp_hal_pin_obj_t pin) { 
    return !(bool)(pin->regs->CR & pin->pin_mask);
}

bool mp_hal_pin_config(mp_hal_pin_obj_t pin_obj, uint32_t dir, uint32_t type, uint32_t drive) {
    mp_hal_gpio_clock_enable(pin_obj->periph);

    if(mp_hal_pin_needs_unlocking(pin_obj)) {
        nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("Pin \"%s\" needs to be unlocked first"), qstr_str(pin_obj->name)));
        return false;
    }

    MAP_GPIODirModeSet(pin_obj->gpio, pin_obj->pin_mask, dir);
    MAP_GPIOPadConfigSet(pin_obj->gpio, pin_obj->pin_mask, drive, type);
    return true;
}

void mp_hal_pin_set_af(mp_hal_pin_obj_t pin_obj, uint8_t af_id) {
    if (af_id == 0xFF) return;
    mp_hal_gpio_clock_enable(pin_obj->periph);
    GPIOPinConfigure(pin_find_af_by_index(pin_obj,af_id)->conf);
}

bool mp_hal_pin_config_alt(mp_hal_pin_obj_t pin, uint8_t fn, uint8_t unit) {
    const pin_af_obj_t *af = pin_find_af(pin, fn, unit);
    // does af exist?
    if (af == NULL) {
        return false;
    }
    // default settings:
    uint32_t strength = GPIO_STRENGTH_2MA;
    uint32_t type = GPIO_PIN_TYPE_STD;
    uint32_t dir = GPIO_DIR_MODE_HW;
    
    switch(fn) {
        case PIN_FN_ADC:
            type = GPIO_PIN_TYPE_ANALOG;
            dir = GPIO_DIR_MODE_IN;
            break;
        case PIN_FN_COMP:
            if(af->type != AF_COMP_OUT) { // AF COMP NEG/POS
                type = GPIO_PIN_TYPE_ANALOG;
                dir = GPIO_DIR_MODE_IN;
            }
            break;
        case PIN_FN_I2C:
            if(af->type == AF_I2C_SDA) {
                type = GPIO_PIN_TYPE_OD;
            }
            break;
        case PIN_FN_CAN:
            strength = GPIO_STRENGTH_8MA;
            break;
        case PIN_FN_QEI:
            type = GPIO_PIN_TYPE_STD_WPU;
            break;
        case PIN_FN_USB:
            if(!(af->type == AF_USB_EPEN || af->type == AF_USB_PFLT)) { 
                type = GPIO_PIN_TYPE_ANALOG;
                dir = GPIO_DIR_MODE_IN;
            }
        break;
        case PIN_FN_UART:
        case PIN_FN_SSI:
        case PIN_FN_MTRL:
        case PIN_FN_TIM:
        case PIN_FN_WTIM:
        case PIN_FN_TR:
        default:
            break;
    }
    if(!mp_hal_pin_config(pin, dir, type, strength)) return false;
    // ADC does not need this config.
    if(fn != PIN_FN_ADC) mp_hal_pin_set_af(pin, af->idx);
    return true;
}   

uint32_t HAL_GetTick() {
    extern uint32_t uwTick;
    return uwTick;
}

MP_WEAK int mp_hal_stdin_rx_chr(void) {
   for (;;) {
#if 0
#ifdef USE_HOST_MODE
       pyb_usb_host_process();
       int c = pyb_usb_host_get_keyboard();
       if (c != 0) {
           return c;
       }
#endif
#endif

       #if MICROPY_HW_ENABLE_USB
       byte c;
       if (usb_vcp_recv_byte(&c) != 0) {
           return c;
       }
       #endif
       if (MP_STATE_PORT(machine_stdio_uart) != NULL && uart_rx_any(MP_STATE_PORT(machine_stdio_uart))) {
           return uart_rx_char(MP_STATE_PORT(machine_stdio_uart));
       }
    //    int dupterm_c = mp_uos_dupterm_rx_chr();
    //    if (dupterm_c >= 0) {
    //        return dupterm_c;
    //    }
       MICROPY_EVENT_POLL_HOOK
   }
}

void mp_hal_stdout_tx_str(const char *str) {
   mp_hal_stdout_tx_strn(str, strlen(str));
}

MP_WEAK void mp_hal_stdout_tx_strn(const char *str, size_t len) {
   if (MP_STATE_PORT(machine_stdio_uart) != NULL) {
       uart_tx_strn(MP_STATE_PORT(machine_stdio_uart), str, len);
   }
#if 0 && defined(USE_HOST_MODE) && MICROPY_HW_HAS_LCD
   lcd_print_strn(str, len);
#endif
   #if MICROPY_HW_ENABLE_USB
   if (usb_vcp_is_enabled()) {
       usb_vcp_send_strn(str, len);
   }
   #endif
   mp_uos_dupterm_tx_strn(str, len);
}

// Efficiently convert "\n" to "\r\n"
void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len) {
   const char *last = str;
   while (len--) {
       if (*str == '\n') {
           if (str > last) {
               mp_hal_stdout_tx_strn(last, str - last);
           }
           mp_hal_stdout_tx_strn("\r\n", 2);
           ++str;
           last = str;
       } else {
           ++str;
       }
   }
   if (str > last) {
       mp_hal_stdout_tx_strn(last, str - last);
   }
}
//
//#if __CORTEX_M >= 0x03
//void mp_hal_ticks_cpu_enable(void) {
//    if (!(DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk)) {
//        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
//        #if defined(__CORTEX_M) && __CORTEX_M == 7
//        // on Cortex-M7 we must unlock the DWT before writing to its registers
//        DWT->LAR = 0xc5acce55;
//        #endif
//        DWT->CYCCNT = 0;
//        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
//    }
//}
//#endif
//
//void mp_hal_gpio_clock_enable(GPIO_TypeDef *gpio) {
//    #if defined(STM32L476xx) || defined(STM32L496xx)
//    if (gpio == GPIOG) {
//        // Port G pins 2 thru 15 are powered using VddIO2 on these MCUs.
//        HAL_PWREx_EnableVddIO2();
//    }
//    #endif
//
//    // This logic assumes that all the GPIOx_EN bits are adjacent and ordered in one register
//
//    #if defined(STM32F0)
//    #define AHBxENR AHBENR
//    #define AHBxENR_GPIOAEN_Pos RCC_AHBENR_GPIOAEN_Pos
//    #elif defined(STM32F4) || defined(STM32F7)
//    #define AHBxENR AHB1ENR
//    #define AHBxENR_GPIOAEN_Pos RCC_AHB1ENR_GPIOAEN_Pos
//    #elif defined(STM32H7)
//    #define AHBxENR AHB4ENR
//    #define AHBxENR_GPIOAEN_Pos RCC_AHB4ENR_GPIOAEN_Pos
//    #elif defined(STM32L4)
//    #define AHBxENR AHB2ENR
//    #define AHBxENR_GPIOAEN_Pos RCC_AHB2ENR_GPIOAEN_Pos
//    #endif
//
//    uint32_t gpio_idx = ((uint32_t)gpio - GPIOA_BASE) / (GPIOB_BASE - GPIOA_BASE);
//    RCC->AHBxENR |= 1 << (AHBxENR_GPIOAEN_Pos + gpio_idx);
//    volatile uint32_t tmp = RCC->AHBxENR; // Delay after enabling clock
//    (void)tmp;
//}
//
//void mp_hal_pin_config(mp_hal_pin_obj_t pin_obj, uint32_t mode, uint32_t pull, uint32_t alt) {
//    GPIO_TypeDef *gpio = pin_obj->gpio;
//    uint32_t pin = pin_obj->pin;
//    mp_hal_gpio_clock_enable(gpio);
//    gpio->MODER = (gpio->MODER & ~(3 << (2 * pin))) | ((mode & 3) << (2 * pin));
//    #if defined(GPIO_ASCR_ASC0)
//    // The L4 has a special analog switch to connect the GPIO to the ADC
//    gpio->OTYPER = (gpio->OTYPER & ~(1 << pin)) | (((mode >> 2) & 1) << pin);
//    gpio->ASCR = (gpio->ASCR & ~(1 << pin)) | ((mode >> 3) & 1) << pin;
//    #else
//    gpio->OTYPER = (gpio->OTYPER & ~(1 << pin)) | ((mode >> 2) << pin);
//    #endif
//    gpio->OSPEEDR = (gpio->OSPEEDR & ~(3 << (2 * pin))) | (2 << (2 * pin)); // full speed
//    gpio->PUPDR = (gpio->PUPDR & ~(3 << (2 * pin))) | (pull << (2 * pin));
//    gpio->AFR[pin >> 3] = (gpio->AFR[pin >> 3] & ~(15 << (4 * (pin & 7)))) | (alt << (4 * (pin & 7)));
//}
//
//bool mp_hal_pin_config_alt(mp_hal_pin_obj_t pin, uint32_t mode, uint32_t pull, uint8_t fn, uint8_t unit) {
//    const pin_af_obj_t *af = pin_find_af(pin, fn, unit);
//    if (af == NULL) {
//        return false;
//    }
//    mp_hal_pin_config(pin, mode, pull, af->idx);
//    return true;
//}
//
//void mp_hal_pin_config_speed(mp_hal_pin_obj_t pin_obj, uint32_t speed) {
//    GPIO_TypeDef *gpio = pin_obj->gpio;
//    uint32_t pin = pin_obj->pin;
//    gpio->OSPEEDR = (gpio->OSPEEDR & ~(3 << (2 * pin))) | (speed << (2 * pin));
//}
