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
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "py/runtime.h"
#include "py/mphal.h"
#include "extmod/virtpin.h"

#include <pin.h>

#include <extint.h>
#include "extint.h"
#include "inc/hw_gpio.h"
#include "inc/hw_ints.h"
#include "driverlib/gpio.h"
#include "driverlib/rom.h"
#include "driverlib/rom_map.h"

/// \moduleref pyb
/// \class Pin - control I/O pins
///
/// A pin is the basic object to control I/O pins.  It has methods to set
/// the mode of the pin (input, output, etc) and methods to get and set the
/// digital logic level.  For analog control of a pin, see the ADC class.
///
/// Usage Model:
///
/// All Board Pins are predefined as pyb.Pin.board.Name
///
///     x1_pin = pyb.Pin.board.X1
///
///     g = pyb.Pin(pyb.Pin.board.X1, pyb.Pin.IN)
///
/// CPU pins which correspond to the board pins are available
/// as `pyb.cpu.Name`. For the CPU pins, the names are the port letter
/// followed by the pin number. On the PYBv1.0, `pyb.Pin.board.X1` and
/// `pyb.Pin.cpu.B6` are the same pin.
///
/// You can also use strings:
///
///     g = pyb.Pin('X1', pyb.Pin.OUT_PP)
///
/// Users can add their own names:
///
///     MyMapperDict = { 'LeftMotorDir' : pyb.Pin.cpu.C12 }
///     pyb.Pin.dict(MyMapperDict)
///     g = pyb.Pin("LeftMotorDir", pyb.Pin.OUT_OD)
///
/// and can query mappings
///
///     pin = pyb.Pin("LeftMotorDir")
///
/// Users can also add their own mapping function:
///
///     def MyMapper(pin_name):
///        if pin_name == "LeftMotorDir":
///            return pyb.Pin.cpu.A0
///
///     pyb.Pin.mapper(MyMapper)
///
/// So, if you were to call: `pyb.Pin("LeftMotorDir", pyb.Pin.OUT_PP)`
/// then `"LeftMotorDir"` is passed directly to the mapper function.
///
/// To summarise, the following order determines how things get mapped into
/// an ordinal pin number:
///
/// 1. Directly specify a pin object
/// 2. User supplied mapping function
/// 3. User supplied mapping (object must be usable as a dictionary key)
/// 4. Supply a string which matches a board pin
/// 5. Supply a string which matches a CPU port/pin
///
/// You can set `pyb.Pin.debug(True)` to get some debug information about
/// how a particular object gets mapped to a pin.

// Pin class variables
STATIC bool pin_class_debug;

void pin_init0(void) {
    MP_STATE_PORT(pin_class_mapper) = mp_const_none;
    MP_STATE_PORT(pin_class_map_dict) = mp_const_none;
    pin_class_debug = false;
}



// C API used to convert a user-supplied pin name into an ordinal pin number.
const pin_obj_t *pin_find(mp_obj_t user_obj) {
    const pin_obj_t *pin_obj;

    // If a pin was provided, then use it
    if (MP_OBJ_IS_TYPE(user_obj, &pin_type)) {
        pin_obj = MP_OBJ_TO_PTR(user_obj);
        if (pin_class_debug) {
            printf("Pin map passed pin ");
            mp_obj_print(MP_OBJ_FROM_PTR(pin_obj), PRINT_STR);
            printf("\n");
        }
        return pin_obj;
    }


    if (MP_STATE_PORT(pin_class_mapper) != mp_const_none) {
        mp_obj_t o = mp_call_function_1(MP_STATE_PORT(pin_class_mapper), user_obj);
        if (o != mp_const_none) {
            if (!MP_OBJ_IS_TYPE(o, &pin_type)) {
                mp_raise_ValueError(MP_ERROR_TEXT("Pin.mapper didn't return a Pin object"));
            }
            if (pin_class_debug) {
                printf("Pin.mapper maps ");
                mp_obj_print(user_obj, PRINT_REPR);
                printf(" to ");
                mp_obj_print(o, PRINT_STR);
                printf("\n");
            }
            return MP_OBJ_TO_PTR(o);
        }
        // The pin mapping function returned mp_const_none, fall through to
        // other lookup methods.
    }

    if (MP_STATE_PORT(pin_class_map_dict) != mp_const_none) {
        mp_map_t *pin_map_map = mp_obj_dict_get_map(MP_STATE_PORT(pin_class_map_dict));
        mp_map_elem_t *elem = mp_map_lookup(pin_map_map, user_obj, MP_MAP_LOOKUP);
        if (elem != NULL && elem->value != MP_OBJ_NULL) {
            mp_obj_t o = elem->value;
            if (pin_class_debug) {
                printf("Pin.map_dict maps ");
                mp_obj_print(user_obj, PRINT_REPR);
                printf(" to ");
                mp_obj_print(o, PRINT_STR);
                printf("\n");
            }
            return MP_OBJ_TO_PTR(o);
        }
    }

    // See if the pin name matches a board pin
    pin_obj = pin_find_named_pin(&pin_board_pins_locals_dict, user_obj);
    if (pin_obj) {
        if (pin_class_debug) {
            printf("Pin.board maps ");
            mp_obj_print(user_obj, PRINT_REPR);
            printf(" to ");
            mp_obj_print(MP_OBJ_FROM_PTR(pin_obj), PRINT_STR);
            printf("\n");
        }
        return pin_obj;
    }

    // See if the pin name matches a cpu pin
    // TODO
//    pin_obj = pin_find_named_pin(&pin_cpu_pins_locals_dict, user_obj);
//    if (pin_obj) {
//        if (pin_class_debug) {
//            printf("Pin.cpu maps ");
//            mp_obj_print(user_obj, PRINT_REPR);
//            printf(" to ");
//            mp_obj_print(MP_OBJ_FROM_PTR(pin_obj), PRINT_STR);
//            printf("\n");
//        }
//        return pin_obj;
//    }

    nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("Pin(%s) doesn't exist"), mp_obj_str_get_str(user_obj)));
}

/// \method __str__()
/// Return a string describing the pin object.
STATIC void pin_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {

    pin_obj_t *self = MP_OBJ_TO_PTR(self_in);

    // pin name
    mp_printf(print, "Pin(Pin.cpu.%q, mode=Pin.", self->name);

    uint32_t dir = MAP_GPIODirModeGet(self->gpio, self->pin_mask);
    uint32_t type;
    uint32_t strength;
    MAP_GPIOPadConfigGet(self->gpio, self->pin_mask, &strength, &type);


    if (dir == GPIO_DIR_MODE_HW && type == GPIO_PIN_TYPE_ANALOG) {
        // analog
        mp_print_str(print, "ANALOG)");

    } else {
        // IO mode
        bool af = false;
        qstr mode_qst = MP_QSTR_NULL;
        if (dir == GPIO_DIR_MODE_IN) {
            mode_qst = MP_QSTR_IN;
        } else if (dir == GPIO_DIR_MODE_OUT) {
            if (type == GPIO_PIN_TYPE_OD){
                mode_qst = MP_QSTR_OPEN_DRAIN;
            } else {
                mode_qst = MP_QSTR_OUT;
            }
        } else {
            af = true;
            if (dir == GPIO_DIR_MODE_HW) {
                if (type == GPIO_PIN_TYPE_OD){
                   mode_qst = MP_QSTR_ALT_OPEN_DRAIN;
                } else {
                    mode_qst = MP_QSTR_ALT;
                }
            }
        }
        mp_print_str(print, qstr_str(mode_qst));

        // pull mode
        qstr pull_qst = MP_QSTR_NULL;
        if (type == GPIO_PIN_TYPE_STD_WPU) {
            pull_qst = MP_QSTR_PULL_UP;
        } else if (type == GPIO_PIN_TYPE_STD_WPD) {
            pull_qst = MP_QSTR_PULL_DOWN;
        }
        if (pull_qst != MP_QSTR_NULL) {
            mp_printf(print, ", pull=Pin.%q", pull_qst);
        }

        // AF mode
        if (af) {
            mp_uint_t af_idx = mp_hal_pin_get_af(self);
            const pin_af_obj_t *af_obj = pin_find_af_by_index(self, af_idx);
            if (af_obj == NULL) {
                mp_printf(print, ", af=%d)", af_idx);
            } else {
                mp_printf(print, ", af=Pin.%q)", af_obj->name);
            }
        } else {
            mp_print_str(print, ")");
        }
    }
}

STATIC mp_obj_t pin_obj_init_helper(const pin_obj_t *pin, size_t n_args, const mp_obj_t *args, mp_map_t *kw_args);

/// \classmethod \constructor(id, ...)
/// Create a new Pin object associated with the id.  If additional arguments are given,
/// they are used to initialise the pin.  See `init`.
mp_obj_t pin_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, MP_OBJ_FUN_ARGS_MAX, true);

    // Run an argument through the mapper and return the result.
    const pin_obj_t *pin = pin_find(args[0]);

    if (n_args > 1 || n_kw > 0) {
        // pin mode given, so configure this GPIO
        mp_map_t kw_args;
        mp_map_init_fixed_table(&kw_args, n_kw, args + n_args);
        pin_obj_init_helper(pin, n_args - 1, args + 1, &kw_args);
    }

    return MP_OBJ_FROM_PTR(pin);
}

// fast method for getting/setting pin value
STATIC mp_obj_t pin_call(mp_obj_t self_in, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 0, 1, false);
    pin_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (n_args == 0) {
        // get pin
        return MP_OBJ_NEW_SMALL_INT(mp_hal_pin_read(self));
    } else {
        // set pin
        mp_hal_pin_write(self, mp_obj_is_true(args[0]));
        return mp_const_none;
    }
}

/// \classmethod mapper([fun])
/// Get or set the pin mapper function.
STATIC mp_obj_t pin_mapper(size_t n_args, const mp_obj_t *args) {
    if (n_args > 1) {
        MP_STATE_PORT(pin_class_mapper) = args[1];
        return mp_const_none;
    }
    return MP_STATE_PORT(pin_class_mapper);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pin_mapper_fun_obj, 1, 2, pin_mapper);
STATIC MP_DEFINE_CONST_CLASSMETHOD_OBJ(pin_mapper_obj, MP_ROM_PTR(&pin_mapper_fun_obj));

/// \classmethod dict([dict])
/// Get or set the pin mapper dictionary.
STATIC mp_obj_t pin_map_dict(size_t n_args, const mp_obj_t *args) {
    if (n_args > 1) {
        MP_STATE_PORT(pin_class_map_dict) = args[1];
        return mp_const_none;
    }
    return MP_STATE_PORT(pin_class_map_dict);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pin_map_dict_fun_obj, 1, 2, pin_map_dict);
STATIC MP_DEFINE_CONST_CLASSMETHOD_OBJ(pin_map_dict_obj, MP_ROM_PTR(&pin_map_dict_fun_obj));

/// \classmethod af_list()
/// Returns an array of alternate functions available for this pin.
STATIC mp_obj_t pin_af_list(mp_obj_t self_in) {
    pin_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_obj_t result = mp_obj_new_list(0, NULL);

    const pin_af_obj_t *af = self->af_list;
    for (mp_uint_t i = 0; i < self->num_afs; i++, af++) {
        mp_obj_list_append(result, MP_OBJ_FROM_PTR(af));
    }
    return result;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(pin_af_list_obj, pin_af_list);

/// \classmethod debug([state])
/// Get or set the debugging state (`True` or `False` for on or off).
STATIC mp_obj_t pin_debug(size_t n_args, const mp_obj_t *args) {
    if (n_args > 1) {
        pin_class_debug = mp_obj_is_true(args[1]);
        return mp_const_none;
    }
    return mp_obj_new_bool(pin_class_debug);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pin_debug_fun_obj, 1, 2, pin_debug);
STATIC MP_DEFINE_CONST_CLASSMETHOD_OBJ(pin_debug_obj, MP_ROM_PTR(&pin_debug_fun_obj));

// init(mode, pull=None, af=-1, *, value, alt)
STATIC mp_obj_t pin_obj_init_helper(const pin_obj_t *self, size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum {ARG_mode = 0, ARG_pull, ARG_drive, ARG_af, ARG_value, ARG_alt};
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_mode, MP_ARG_REQUIRED | MP_ARG_INT },
        { MP_QSTR_pull, MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE}},
        { MP_QSTR_drive, MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
        { MP_QSTR_af, MP_ARG_INT, {.u_int = -1}}, // legacy
        { MP_QSTR_value, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
        { MP_QSTR_alt, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1}},
    };

    // parse args
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    // get pull mode
    uint pull = GPIO_PIN_TYPE_STD;
    if (args[ARG_pull].u_obj != mp_const_none) {
        pull = mp_obj_get_int(args[ARG_pull].u_obj);
    }
    if (!IS_GPIO_TYPE(pull)) {
        nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("invalid pin pull: %d"), pull));
    }

    // get io mode; if OD, pull is overwritten! for api compat
    uint32_t mode = args[ARG_mode].u_int;
    if(mode == GPIO_PIN_TYPE_OD) {
        mode = GPIO_DIR_MODE_OUT;
        pull = GPIO_PIN_TYPE_OD;
    } else if (!IS_GPIO_DIR(mode)) {
        nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("invalid pin direction: %d"), mode));
    }

    uint drive = GPIO_STRENGTH_2MA;
    if (args[ARG_drive].u_obj != MP_OBJ_NULL) {
        drive = mp_obj_get_int(args[2].u_obj);
    }
    if (!IS_GPIO_STRENGTH(drive)) {
        nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("invalid pin drive: %d"), drive));
    }

    // get af (alternate function); alt-arg overrides af-arg
    mp_int_t af = args[ARG_alt].u_int;
    if (af == -1) {
        af = args[ARG_af].u_int;
    }
    if ((mode == GPIO_DIR_MODE_HW) && !IS_GPIO_AF(af)) {
        nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("invalid pin af: %d"), af));
    }

    // enable the peripheral clock for the port of this pin
//    mp_hal_gpio_clock_enable(self->port);

    // if given, set the pin value before initialising to prevent glitches
//    if (args[4].u_obj != MP_OBJ_NULL) {
//        mp_hal_pin_write(self, mp_obj_is_true(args[3].u_obj));
//    }
    mp_hal_pin_config(self, mode, pull, drive);
    mp_hal_pin_set_af(self, af);

    if (args[4].u_obj != MP_OBJ_NULL) {
        mp_hal_pin_write(self, mp_obj_is_true(args[ARG_value].u_obj));
    }

    return mp_const_none;
}

STATIC mp_obj_t pin_obj_init(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    return pin_obj_init_helper(MP_OBJ_TO_PTR(args[0]), n_args - 1, args + 1, kw_args);
}
MP_DEFINE_CONST_FUN_OBJ_KW(pin_init_obj, 1, pin_obj_init);

/// \method value([value])
/// Get or set the digital logic level of the pin:
///
///   - With no argument, return 0 or 1 depending on the logic level of the pin.
///   - With `value` given, set the logic level of the pin.  `value` can be
///   anything that converts to a boolean.  If it converts to `True`, the pin
///   is set high, otherwise it is set low.
STATIC mp_obj_t pin_value(size_t n_args, const mp_obj_t *args) {
    return pin_call(args[0], n_args - 1, 0, args + 1);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pin_value_obj, 1, 2, pin_value);

STATIC mp_obj_t pin_off(mp_obj_t self_in) {
    pin_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_hal_pin_low(self);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(pin_off_obj, pin_off);

STATIC mp_obj_t pin_on(mp_obj_t self_in) {
    pin_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_hal_pin_high(self);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(pin_on_obj, pin_on);

// pin.irq(handler=None, trigger=IRQ_RISING, hard=False)
// TODO
STATIC mp_obj_t pin_irq(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
   enum { ARG_handler, ARG_trigger, ARG_hard };
   static const mp_arg_t allowed_args[] = {
       { MP_QSTR_handler, MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
       { MP_QSTR_trigger, MP_ARG_INT, {.u_int = GPIO_RISING_EDGE | GPIO_FALLING_EDGE} },
       { MP_QSTR_hard, MP_ARG_BOOL, {.u_bool = false} },
   };
   pin_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
   mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
   mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

   if (n_args > 1 || kw_args->used != 0) {
       // configure irq
       extint_register_pin(self, args[ARG_trigger].u_int,
           args[ARG_hard].u_bool, args[ARG_handler].u_obj);
   }

//    // TODO should return an IRQ object
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(pin_irq_obj, 1, pin_irq);

/// \method name()
/// Get the pin name.
STATIC mp_obj_t pin_name(mp_obj_t self_in) {
    pin_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return MP_OBJ_NEW_QSTR(self->name);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(pin_name_obj, pin_name);

/// \method names()
/// Returns the cpu and board names for this pin.
STATIC mp_obj_t pin_names(mp_obj_t self_in) {
    pin_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_obj_t result = mp_obj_new_list(0, NULL);
    mp_obj_list_append(result, MP_OBJ_NEW_QSTR(self->name));

    const mp_map_t *map = &pin_board_pins_locals_dict.map;
    mp_map_elem_t *elem = map->table;

    for (mp_uint_t i = 0; i < map->used; i++, elem++) {
        if (elem->value == self_in) {
            mp_obj_list_append(result, elem->key);
        }
    }
    return result;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(pin_names_obj, pin_names);

/// \method port()
/// Get the pin port.
STATIC mp_obj_t pin_port(mp_obj_t self_in) {
    pin_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return MP_OBJ_NEW_SMALL_INT(self->gpio);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(pin_port_obj, pin_port);

/// \method pin()
/// Get the pin number.
STATIC mp_obj_t pin_pin(mp_obj_t self_in) {
    pin_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return MP_OBJ_NEW_SMALL_INT(self->pin_num);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(pin_pin_obj, pin_pin);

/// \method gpio()
/// Returns the base address of the GPIO block associated with this pin.
STATIC mp_obj_t pin_gpio(mp_obj_t self_in) {
    pin_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return MP_OBJ_NEW_SMALL_INT((intptr_t)self->gpio);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(pin_gpio_obj, pin_gpio);

/// \method mode/dir()
/// Returns the currently configured mode of the pin. The integer returned
/// will match one of the allowed constants for the mode argument to the init
/// function.
STATIC mp_obj_t pin_mode(mp_obj_t self_in) {
    return MP_OBJ_NEW_SMALL_INT(mp_hal_pin_get_dir((pin_obj_t*)MP_OBJ_TO_PTR(self_in)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(pin_mode_obj, pin_mode);

/// \method pull/type()
/// Returns the currently configured pull of the pin. The integer returned
/// will match one of the allowed constants for the pull argument to the init
/// function.
STATIC mp_obj_t pin_pull(mp_obj_t self_in) {
    uint32_t type;
    pin_obj_t* self = MP_OBJ_TO_PTR(self_in);
    MAP_GPIOPadConfigGet(self->gpio, self->pin_mask, NULL, &type);
    return MP_OBJ_NEW_SMALL_INT(type);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(pin_pull_obj, pin_pull);

/// \method af()
/// Returns the currently configured alternate-function of the pin. The
/// integer returned will match one of the allowed constants for the af
/// argument to the init function.
STATIC mp_obj_t pin_af(mp_obj_t self_in) {
    return MP_OBJ_NEW_SMALL_INT(mp_hal_pin_get_af((pin_obj_t*)MP_OBJ_TO_PTR(self_in)));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(pin_af_obj, pin_af);

/// \classmethod unlock(str pin1, str pin2,..)
/// Sets the Commit Flag on the corresponding GPIO to enable
/// re-assingment of the supplied pin
STATIC mp_obj_t pin_unlock(size_t n_args, const mp_obj_t *args) {
    if (n_args > 1) {
        for (int i = 1 ; i < n_args ; i++) {
            const pin_obj_t *p = pin_find(args[i]);
            mp_hal_unlock_special_pin(p);
        }
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pin_unlock_fun_obj, 2, 2, pin_unlock);
STATIC MP_DEFINE_CONST_CLASSMETHOD_OBJ(pin_unlock_obj, MP_ROM_PTR(&pin_unlock_fun_obj));


STATIC const mp_rom_map_elem_t pin_locals_dict_table[] = {
//    { MP_ROM_QSTR(MP_QSTR___name__), MP_OBJ_NEW_QSTR(MP_QSTR_Pin) },
    // instance methods
    { MP_ROM_QSTR(MP_QSTR_init),    MP_ROM_PTR(&pin_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_value),   MP_ROM_PTR(&pin_value_obj) },
    { MP_ROM_QSTR(MP_QSTR_off),     MP_ROM_PTR(&pin_off_obj) },
    { MP_ROM_QSTR(MP_QSTR_on),      MP_ROM_PTR(&pin_on_obj) },
    { MP_ROM_QSTR(MP_QSTR_irq),     MP_ROM_PTR(&pin_irq_obj) },

    // Legacy names as used by pyb.Pin
    { MP_ROM_QSTR(MP_QSTR_low),     MP_ROM_PTR(&pin_off_obj) },
    { MP_ROM_QSTR(MP_QSTR_high),    MP_ROM_PTR(&pin_on_obj) },
    { MP_ROM_QSTR(MP_QSTR_name),    MP_ROM_PTR(&pin_name_obj) },
    { MP_ROM_QSTR(MP_QSTR_names),   MP_ROM_PTR(&pin_names_obj) },
    { MP_ROM_QSTR(MP_QSTR_af_list), MP_ROM_PTR(&pin_af_list_obj) },
    { MP_ROM_QSTR(MP_QSTR_port),    MP_ROM_PTR(&pin_port_obj) },
    { MP_ROM_QSTR(MP_QSTR_pin),     MP_ROM_PTR(&pin_pin_obj) },
    { MP_ROM_QSTR(MP_QSTR_gpio),    MP_ROM_PTR(&pin_gpio_obj) },
    { MP_ROM_QSTR(MP_QSTR_mode),    MP_ROM_PTR(&pin_mode_obj) },
    { MP_ROM_QSTR(MP_QSTR_pull),    MP_ROM_PTR(&pin_pull_obj) },
    { MP_ROM_QSTR(MP_QSTR_af),      MP_ROM_PTR(&pin_af_obj) },
    // additional names for tiva compliance
    { MP_ROM_QSTR(MP_QSTR_dir),     MP_ROM_PTR(&pin_mode_obj) },
    { MP_ROM_QSTR(MP_QSTR_type),    MP_ROM_PTR(&pin_pull_obj) },

    // class methods
    { MP_ROM_QSTR(MP_QSTR_mapper),  MP_ROM_PTR(&pin_mapper_obj) },
    { MP_ROM_QSTR(MP_QSTR_dict),    MP_ROM_PTR(&pin_map_dict_obj) },
    { MP_ROM_QSTR(MP_QSTR_debug),   MP_ROM_PTR(&pin_debug_obj) },
    { MP_ROM_QSTR(MP_QSTR_unlock),  MP_ROM_PTR(&pin_unlock_obj) },

    // class attributes
    { MP_ROM_QSTR(MP_QSTR_board),   MP_ROM_PTR(&pin_board_pins_obj_type) },
//    { MP_ROM_QSTR(MP_QSTR_cpu),     MP_ROM_PTR(&pin_cpu_pins_obj_type) },

    // class constants
    { MP_ROM_QSTR(MP_QSTR_IN),        MP_ROM_INT(GPIO_DIR_MODE_IN) },
    { MP_ROM_QSTR(MP_QSTR_OUT),       MP_ROM_INT(GPIO_DIR_MODE_OUT) },
    { MP_ROM_QSTR(MP_QSTR_OPEN_DRAIN), MP_ROM_INT(GPIO_PIN_TYPE_OD) },
    { MP_ROM_QSTR(MP_QSTR_ALT),       MP_ROM_INT(GPIO_DIR_MODE_HW) },
    { MP_ROM_QSTR(MP_QSTR_ANALOG),    MP_ROM_INT(GPIO_PIN_TYPE_ANALOG) },
    { MP_ROM_QSTR(MP_QSTR_PULL_UP),   MP_ROM_INT(GPIO_PIN_TYPE_STD_WPU) },
    { MP_ROM_QSTR(MP_QSTR_PULL_DOWN), MP_ROM_INT(GPIO_PIN_TYPE_STD_WPD) },
    { MP_ROM_QSTR(MP_QSTR_PULL_NONE), MP_ROM_INT(GPIO_PIN_TYPE_STD) },
    { MP_ROM_QSTR(MP_QSTR_IRQ_RISING), MP_ROM_INT(GPIO_RISING_EDGE) },
    { MP_ROM_QSTR(MP_QSTR_IRQ_FALLING), MP_ROM_INT(GPIO_FALLING_EDGE) },
    { MP_ROM_QSTR(MP_QSTR_IRQ_BOTH),  MP_ROM_INT(GPIO_BOTH_EDGES) },
    { MP_ROM_QSTR(MP_QSTR_IRQ_HIGH),  MP_ROM_INT(GPIO_HIGH_LEVEL) },
    { MP_ROM_QSTR(MP_QSTR_IRQ_LOW),     MP_ROM_INT(GPIO_LOW_LEVEL) },
    { MP_ROM_QSTR(MP_QSTR_LOW_POWER),     MP_ROM_INT(GPIO_STRENGTH_2MA) },
    { MP_ROM_QSTR(MP_QSTR_MED_POWER),     MP_ROM_INT(GPIO_STRENGTH_4MA) },
    { MP_ROM_QSTR(MP_QSTR_HI_POWER),     MP_ROM_INT(GPIO_STRENGTH_8MA) },
};

STATIC MP_DEFINE_CONST_DICT(pin_locals_dict, pin_locals_dict_table);

const mp_obj_type_t pin_type = {
    { &mp_type_type },
    .name = MP_QSTR_Pin,
    .print = pin_print,
    .make_new = pin_make_new,
    .call = pin_call,
    .locals_dict = (mp_obj_t)&pin_locals_dict,
};
//
//const mp_obj_module_t pin_module = {
//    .base = { &pin_type },
//    .globals = (mp_obj_dict_t*)&pin_locals_dict,
//};

// TODO
//STATIC const mp_irq_methods_t pin_irq_methods = {
//    .init = pin_irq,
//    .enable = pin_irq_enable,
//    .disable = pin_irq_disable,
//    .flags = pin_irq_flags,
//};
