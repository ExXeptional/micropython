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
#include <stddef.h>
#include <string.h>

#include "py/runtime.h"
#include "py/gc.h"
#include "py/mphal.h"
#include "pin.h"
#include "extint.h"
#include "irq.h"

#include "driverlib/gpio.h"
#include "inc/hw_memmap.h"
#include "inc/hw_ints.h"
#include "handlers.h"

/// \moduleref pyb
/// \class ExtInt - configure I/O pins to interrupt on external events
///
/// There are a total of 22 interrupt lines. 16 of these can come from GPIO pins
/// and the remaining 6 are from internal sources.
///
/// For lines 0 thru 15, a given line can map to the corresponding line from an
/// arbitrary port. So line 0 can map to Px0 where x is A, B, C, ... and
/// line 1 can map to Px1 where x is A, B, C, ...
///
///     def callback(line):
///         print("line =", line)
///
/// Note: ExtInt will automatically configure the gpio line as an input.
///
///     extint = pyb.ExtInt(pin, pyb.ExtInt.IRQ_FALLING, pyb.Pin.PULL_UP, callback)
///
/// Now every time a falling edge is seen on the X1 pin, the callback will be
/// called. Caution: mechanical pushbuttons have "bounce" and pushing or
/// releasing a switch will often generate multiple edges.
/// See: http://www.eng.utah.edu/~cs5780/debouncing.pdf for a detailed
/// explanation, along with various techniques for debouncing.
///
/// Trying to register 2 callbacks onto the same pin will throw an exception.
///
/// If pin is passed as an integer, then it is assumed to map to one of the
/// internal interrupt sources, and must be in the range 16 thru 22.
///
/// All other pin objects go through the pin mapper to come up with one of the
/// gpio pins.
///
///     extint = pyb.ExtInt(pin, mode, pull, callback)
///
/// Valid modes are pyb.ExtInt.IRQ_RISING, pyb.ExtInt.IRQ_FALLING,
/// pyb.ExtInt.IRQ_RISING_FALLING, pyb.ExtInt.EVT_RISING,
/// pyb.ExtInt.EVT_FALLING, and pyb.ExtInt.EVT_RISING_FALLING.
///
/// Only the IRQ_xxx modes have been tested. The EVT_xxx modes have
/// something to do with sleep mode and the WFE instruction.
///
/// Valid pull values are pyb.Pin.PULL_UP, pyb.Pin.PULL_DOWN, pyb.Pin.PULL_NONE.
///
/// There is also a C API, so that drivers which require EXTI interrupt lines
/// can also use this code. See extint.h for the available functions and
/// usrsw.h for an example of using this.

// TODO Add python method to change callback object.

#define EXTI_OFFSET (EXTI_BASE - PERIPH_BASE)

//STATIC uint8_t pyb_extint_mode[EXTI_NUM_VECTORS];
STATIC bool pyb_extint_hard_irq[EXTI_NUM_VECTORS];

 // The callback arg is a small-int or a ROM Pin object, so no need to scan by GC
 STATIC mp_obj_t pyb_extint_callback_arg[8];


//Struct for hadlers
STATIC const void *int_hndls[8] = {

    &GPIOA_Handler,
    &GPIOB_Handler,
    &GPIOC_Handler,
    &GPIOD_Handler,
    &GPIOE_Handler,
    &GPIOF_Handler,
    &GPIOG_Handler,
    &GPIOH_Handler,
};

//text for printing
STATIC const char text[8] = {
    'A',
    'B',
    'C',
    'D',
    'E',
    'F',
    'G',
    'H',
};

// // Set override_callback_obj to true if you want to unconditionally set the
// // callback function.
uint extint_register(extint_obj_t *self_in, mp_obj_t pin_obj, uint32_t mode, uint32_t pull, mp_obj_t callback_obj, bool override_callback_obj) {
    const pin_obj_t *pin = NULL;
    uint line=0;

    if (MP_OBJ_IS_INT(pin_obj)) {
        // If an integer is passed in, then use it to identify lines 16 thru 22
        // We expect lines 0 thru 15 to be passed in as a pin, so that we can
        // get both the port number and line number.
        line = mp_obj_get_int(pin_obj);
        if (line < 16) {
            nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("ExtInt vector %d < 16, use a Pin object"), line));
        }
        if (line >= EXTI_NUM_VECTORS) {
            nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("ExtInt vector %d >= max of %d"), line, EXTI_NUM_VECTORS));
        }
    } else {
        pin = pin_find(pin_obj);
      // v_line = pin->gpio;
    }

    if (mode != GPIO_RISING_EDGE &&
        mode != GPIO_FALLING_EDGE &&
        mode != GPIO_BOTH_EDGES) 
        {
        nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("invalid ExtInt Mode: %d"), mode));
    }
    self_in->gpio = pin->gpio;
    self_in->pin_mask = pin->pin_mask;

    switch (pin->gpio)
    {
        case GPIO_PORTA_AHB_BASE: line = GPIOA_IntHndl; break;
        case GPIO_PORTB_AHB_BASE: line = GPIOB_IntHndl; break;
        case GPIO_PORTC_AHB_BASE: line = GPIOC_IntHndl; break; 
        case GPIO_PORTD_AHB_BASE: line = GPIOD_IntHndl; break;
        case GPIO_PORTE_AHB_BASE: line = GPIOE_IntHndl; break;
        case GPIO_PORTF_AHB_BASE: line = GPIOF_IntHndl; break;
        case GPIO_PORTG_AHB_BASE: line = GPIOG_IntHndl; break;
        case GPIO_PORTH_AHB_BASE: line = GPIOH_IntHndl; break;
        case GPIO_PORTJ_AHB_BASE: line = GPIOJ_IntHndl; break;
    }   

    mp_obj_t *cb = &MP_STATE_PORT(pyb_extint_callback)[line];
    if (*cb != mp_const_none && /*MP_OBJ_FROM_PTR(pin)*/ callback_obj != *cb/*pyb_extint_callback_arg[line]*/) {
        nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError,
            MP_ERROR_TEXT("There is already an interrupt handler for Port %c in use"), text[line]));
    }
    /*if (!override_callback_obj && *cb != mp_const_none && callback_obj != mp_const_none) {
        nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, "ExtInt vector %d is already in use", v_line));
    }*/

    // We need to update callback atomically, so we disable the line
    // before we update anything.

    extint_disable(pin->gpio, pin->pin_mask);

    *cb = callback_obj;

    if (*cb != mp_const_none) {
        //pyb_extint_hard_irq[line] = true;
        //pyb_extint_callback_arg[line] = MP_OBJ_NEW_SMALL_INT(line);

        //Enable DEN for the Pin
        MAP_GPIOPadConfigSet(pin->gpio, pin->pin_mask, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_OD);

        pyb_extint_hard_irq[line] = false;
        pyb_extint_callback_arg[line] = MP_OBJ_FROM_PTR(pin);

      /*  mp_hal_gpio_clock_enable(pin->gpio);
        GPIO_InitTypeDef exti;
        exti.Pin = pin->pin_mask;
        exti.Mode = mode;
        exti.Pull = pull;
        exti.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(pin->gpio, &exti);*/

    // Register the port-level interrupt handler. This handler is the first 
        // level interrupt handler for all the pin interrupts.

        GPIOIntRegister((uint32_t) pin->gpio, int_hndls[line]);

        GPIOIntTypeSet((uint32_t) pin->gpio, pin->pin_mask, mode);

        // Enable the interrupt
        extint_enable(pin->gpio, pin->pin_mask);
    }
    return line;
}

// This function is intended to be used by the Pin.irq() method
void extint_register_pin(const pin_obj_t *pin, uint32_t mode, bool hard_irq, mp_obj_t callback_obj) {
    uint32_t line = pin->pin_num;

    switch (pin->gpio)
    {
        case GPIO_PORTA_AHB_BASE: line = GPIOA_IntHndl; break;
        case GPIO_PORTB_AHB_BASE: line = GPIOB_IntHndl; break;
        case GPIO_PORTC_AHB_BASE: line = GPIOC_IntHndl; break;
        case GPIO_PORTD_AHB_BASE: line = GPIOD_IntHndl; break;
        case GPIO_PORTE_AHB_BASE: line = GPIOE_IntHndl; break;
        case GPIO_PORTF_AHB_BASE: line = GPIOF_IntHndl; break;
        case GPIO_PORTG_AHB_BASE: line = GPIOG_IntHndl; break;
        case GPIO_PORTH_AHB_BASE: line = GPIOH_IntHndl; break;
        case GPIO_PORTJ_AHB_BASE: line = GPIOJ_IntHndl; break;
    }   

    // Check if the ExtInt line is already in use by another Pin/ExtInt
    volatile mp_obj_t *cb = &MP_STATE_PORT(pyb_extint_callback)[line];
    if (*cb != mp_const_none && /*MP_OBJ_FROM_PTR(pin)*/ callback_obj != *cb/*pyb_extint_callback_arg[line]*/) {
        //printf("tesing");
       //if (MP_OBJ_IS_SMALL_INT(pyb_extint_callback_arg[line])) {
            nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError,
               MP_ERROR_TEXT("There is already an interrupt handler for Port %c in use"), text[line]));
       /* } else {
            const pin_obj_t *other_pin = (const pin_obj_t*)pyb_extint_callback_arg[line];
            nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError,
                "IRQ resource already taken by Pin('%ui')", other_pin->name));
        }*/
    }

     *cb = callback_obj;
    // pyb_extint_mode[line] = (mode & 0x00010000) ? // GPIO_MODE_IT == 0x00010000
    //     EXTI_Mode_Interrupt : EXTI_Mode_Event;

     if (*cb != mp_const_none) {
        // Configure and enable the callback
   
        pyb_extint_hard_irq[line] = hard_irq;
        pyb_extint_callback_arg[line] = MP_OBJ_FROM_PTR(pin);

        // // Register the port-level interrupt handler. This handler is the first 
        // level interrupt handler for all the pin interrupts.

        GPIOIntRegister((uint32_t) pin->gpio, int_hndls[line]);

        GPIOIntTypeSet((uint32_t) pin->gpio, pin->pin_mask, mode);

        // Enable the interrupt
        extint_enable(pin->gpio, pin->pin_mask);
        //GPIOIntEnable((uint32_t) pin->gpio, pin->pin_mask);
     }
}

//enale the coresponding interrupt
 void extint_enable(const uint32_t gpio, const uint32_t pin_mask) {
    // Enable the interrupt
    GPIOIntEnable((uint32_t) gpio, pin_mask);
 }

//disable the corresponding interrupt
 void extint_disable(const uint32_t gpio, const uint32_t pin_mask) {

    GPIOIntDisable((uint32_t) gpio, pin_mask);
 }

//trigger interrupt by oftware
void extint_swint(uint line) {
    if (line >= EXTI_NUM_VECTORS) {
        return;
    }

    /* Trigger Interrupt */
    switch (line)
    {
        case GPIOA_IntHndl: IntTrigger(INT_GPIOA); break;
        case GPIOB_IntHndl: IntTrigger(INT_GPIOB); break;
        case GPIOC_IntHndl: IntTrigger(INT_GPIOC); break;
        case GPIOD_IntHndl: IntTrigger(INT_GPIOD); break;
        case GPIOE_IntHndl: IntTrigger(INT_GPIOE); break;
        case GPIOF_IntHndl: IntTrigger(INT_GPIOF); break;
        case GPIOG_IntHndl: IntTrigger(INT_GPIOG); break;
        case GPIOH_IntHndl: IntTrigger(INT_GPIOH); break;
        case GPIOJ_IntHndl: IntTrigger(INT_GPIOJ); break; 
    }  
    
}

/// \method line()
/// Return the line number that the pin is mapped to.
STATIC mp_obj_t extint_obj_line(mp_obj_t self_in) {
    extint_obj_t *self = self_in;
    return MP_OBJ_NEW_SMALL_INT(self->line);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(extint_obj_line_obj, extint_obj_line);

/// \method enable()
/// Enable a disabled interrupt.
STATIC mp_obj_t extint_obj_enable(mp_obj_t self_in) {
    extint_obj_t *self = self_in;
    extint_enable(self->gpio, self->pin_mask);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(extint_obj_enable_obj, extint_obj_enable);

/// \method disable()
/// Disable the interrupt associated with the ExtInt object.
/// This could be useful for debouncing.
STATIC mp_obj_t extint_obj_disable(mp_obj_t self_in) {
    extint_obj_t *self = self_in;
    extint_disable(self->gpio, self->pin_mask);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(extint_obj_disable_obj, extint_obj_disable);

/// \method swint()
/// Trigger the callback from software.
STATIC mp_obj_t extint_obj_swint(mp_obj_t self_in) {
    extint_obj_t *self = self_in;
    extint_swint(self->line);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(extint_obj_swint_obj,  extint_obj_swint);

/// \classmethod \constructor(pin, mode, pull, callback)
/// Create an ExtInt object:
///
///   - `pin` is the pin on which to enable the interrupt (can be a pin object or any valid pin name).
///   - `mode` can be one of:
///     - `ExtInt.IRQ_RISING` - trigger on a rising edge;
///     - `ExtInt.IRQ_FALLING` - trigger on a falling edge;
///     - `ExtInt.IRQ_RISING_FALLING` - trigger on a rising or falling edge.
///   - `pull` can be one of:
///     - `pyb.Pin.PULL_NONE` - no pull up or down resistors;
///     - `pyb.Pin.PULL_UP` - enable the pull-up resistor;
///     - `pyb.Pin.PULL_DOWN` - enable the pull-down resistor.
///   - `callback` is the function to call when the interrupt triggers.  The
///   callback function must accept exactly 1 argument, which is the line that
///   triggered the interrupt.
STATIC const mp_arg_t pyb_extint_make_new_args[] = {
    { MP_QSTR_pin,      MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    { MP_QSTR_mode,     MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
   // { MP_QSTR_pull,     MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
    { MP_QSTR_callback, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
};
#define PYB_EXTINT_MAKE_NEW_NUM_ARGS MP_ARRAY_SIZE(pyb_extint_make_new_args)

STATIC mp_obj_t extint_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    // type_in == extint_obj_type

    // parse args
    mp_arg_val_t vals[PYB_EXTINT_MAKE_NEW_NUM_ARGS];
    mp_arg_parse_all_kw_array(n_args, n_kw, args, PYB_EXTINT_MAKE_NEW_NUM_ARGS, pyb_extint_make_new_args, vals);

    extint_obj_t *self = m_new_obj(extint_obj_t);
    self->base.type = type;
    self->line = extint_register(self, vals[0].u_obj, vals[1].u_int, vals[2].u_int, vals[2].u_obj, false);

    return self;
}

STATIC void extint_obj_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    extint_obj_t *self = self_in;
    mp_printf(print, "<ExtInt line=%u>", self->line);
}

STATIC const mp_rom_map_elem_t extint_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_line),    MP_ROM_PTR(&extint_obj_line_obj) },
    { MP_ROM_QSTR(MP_QSTR_enable),  MP_ROM_PTR(&extint_obj_enable_obj) },
    { MP_ROM_QSTR(MP_QSTR_disable), MP_ROM_PTR(&extint_obj_disable_obj) },
    { MP_ROM_QSTR(MP_QSTR_swint),   MP_ROM_PTR(&extint_obj_swint_obj) },
   // { MP_ROM_QSTR(MP_QSTR_regs),    MP_ROM_PTR(&extint_regs_obj) },

    // class constants
    /// \constant IRQ_RISING - interrupt on a rising edge
    /// \constant IRQ_FALLING - interrupt on a falling edge
    /// \constant IRQ_RISING_FALLING - interrupt on a rising or falling edge
    { MP_ROM_QSTR(MP_QSTR_IRQ_RISING),         MP_ROM_INT(GPIO_RISING_EDGE) },
    { MP_ROM_QSTR(MP_QSTR_IRQ_FALLING),        MP_ROM_INT(GPIO_FALLING_EDGE) },
    { MP_ROM_QSTR(MP_QSTR_IRQ_RISING_FALLING), MP_ROM_INT(GPIO_BOTH_EDGES) },};

STATIC MP_DEFINE_CONST_DICT(extint_locals_dict, extint_locals_dict_table);

const mp_obj_type_t extint_type = {
    { &mp_type_type },
    .name = MP_QSTR_ExtInt,
    .print = extint_obj_print,
    .make_new = extint_make_new,
    .locals_dict = (mp_obj_dict_t*)&extint_locals_dict,
};

void extint_init0(void) {
    for (int i = 0; i < PYB_EXTI_NUM_VECTORS; i++) {
        MP_STATE_PORT(pyb_extint_callback)[i] = mp_const_none;
        //pyb_extint_mode[i] = EXTI_Mode_Interrupt;
   }
}

//Interrupt handler
void Handle_EXTI_Irq(uint32_t line) {
        if (line < EXTI_NUM_VECTORS) {
            mp_obj_t *cb = &MP_STATE_PORT(pyb_extint_callback)[line];
            if (*cb != mp_const_none) {
                // When executing code within a handler we must lock the GC to prevent
                // any memory allocations.  We must also catch any exceptions.
                gc_lock();
                nlr_buf_t nlr;
                if (nlr_push(&nlr) == 0) {
                    mp_call_function_1(*cb, pyb_extint_callback_arg[line]);
                    nlr_pop();
                } else {
                    // Uncaught exception; disable the callback so it doesn't run again.
                    *cb = mp_const_none;
                    //extint_disable(line);
                    printf("Uncaught exception in ExtInt interrupt handler line %u\n", (unsigned int)line);
                    mp_obj_print_exception(&mp_plat_print, (mp_obj_t)nlr.ret_val);
                }
                gc_unlock();
            }
        }
}
