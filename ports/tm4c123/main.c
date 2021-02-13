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

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include CMSIS_HEADER

#include "system_TM4C123.h"

#include "py/runtime.h"
#include "py/stackctrl.h"
#include "py/gc.h"
#include "py/mphal.h"
#include "py/compile.h"
#include "py/parse.h"
#include "py/mperrno.h"
#include "lib/mp-readline/readline.h"
#include "lib/utils/pyexec.h"
#include "lib/oofatfs/ff.h"
#include "extmod/vfs.h"
#include "extmod/vfs_fat.h"


#include "systick.h"
#include "pendsv.h"
#include "pybthread.h"
#include "gccollect.h"
#include "modmachine.h"
//#include "i2c.h"
#include "spi.h"
#include "uart.h"
#include "timer.h"
//#include "led.h"
#include "pin.h"
#include "extint.h"
//#include "usrsw.h"
#include "usb.h"
#include "rtc.h"
// #include "storage.h"
#include "sdcard.h"
#include "rng.h"
//#include "accel.h"
//#include "servo.h"
//#include "dac.h"
//#include "can.h"
//#include "modnetwork.h"
#include "dma.h"

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

#include "inc/hw_types.h"
#include "inc/hw_gpio.h"
#include "inc/hw_ints.h"
#include "inc/hw_uart.h"
#include "inc/hw_nvic.h"
#include "inc/hw_memmap.h"
#include "inc/hw_hibernate.h"
#include "driverlib/sysctl.h"
#include "driverlib/systick.h"
#include "driverlib/uart.h"
#include "driverlib/pin_map.h"
#include "driverlib/gpio.h"
#include "driverlib/rom.h"
#include "driverlib/rom_map.h"

pyb_thread_t pyb_thread_main;
fs_user_mount_t fs_user_mount_flash;
#define led_state(x,y) 
#define PYB_LED_RED     (0)
#define PYB_LED_GREEN   (0)
#define PYB_LED_BLUE    (0)
void flash_error(int n) {
    for (int i = 0; i < n; i++) {
        // TODO
        /* led_state(PYB_LED_RED, 1);
        led_state(PYB_LED_GREEN, 0); */
        mp_hal_delay_ms(250);
        /* led_state(PYB_LED_RED, 0);
        led_state(PYB_LED_GREEN, 1); */
        mp_hal_delay_ms(250);
    }
    led_state(PYB_LED_GREEN, 0);
}

void NORETURN __fatal_error(const char *msg) {
    for (volatile uint delay = 0; delay < 10000000; delay++) {
    }
    //TODO
    /* led_state(1, 1);
    led_state(2, 1);
    led_state(3, 1);
    led_state(4, 1); */
    mp_hal_stdout_tx_strn("\nFATAL ERROR:\n", 14);
    mp_hal_stdout_tx_strn(msg, strlen(msg));
    for (uint i = 0;;) {
        //led_toggle(((i++) & 3) + 1);
        for (volatile uint delay = 0; delay < 10000000; delay++) {
        }
        if (i >= 16) {
            // to conserve power
            __WFI();
        }
    }
}

STATIC mp_obj_t pyb_main(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_opt, MP_ARG_INT, {.u_int = 0} }
    };

    if (MP_OBJ_IS_STR(pos_args[0])) {
        MP_STATE_PORT(pyb_config_main) = pos_args[0];

        // parse args
        mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
        mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);
        MP_STATE_VM(mp_optimise_value) = args[0].u_int;
    }
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_KW(pyb_main_obj, 1, pyb_main);

void nlr_jump_fail(void *val) {
    printf("FATAL: uncaught exception %p\n", val);
    mp_obj_print_exception(&mp_plat_print, (mp_obj_t)val);
    __fatal_error("");
}

#ifndef NDEBUG
void MP_WEAK __assert_func(const char *file, int line, const char *func, const char *expr) {
    (void)func;
    printf("Assertion '%s' failed, at file %s:%d\n", expr, file, line);
    __fatal_error("");
}
#endif

#if MICROPY_ENABLE_COMPILER
void do_str(const char *src, mp_parse_input_kind_t input_kind) {
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_, src, strlen(src), 0);
        qstr source_name = lex->source_name;
        mp_parse_tree_t parse_tree = mp_parse(lex, input_kind);
        mp_obj_t module_fun = mp_compile(&parse_tree, source_name, true);
        mp_call_function_0(module_fun);
        nlr_pop();
    } else {
        // uncaught exception
        mp_obj_print_exception(&mp_plat_print, (mp_obj_t)nlr.ret_val);
    }
}
#endif

//extern uint32_t pui32Stack[128];
extern uint32_t _estack;
extern uint32_t _stack;
extern uint32_t _heap_start;
extern uint32_t _heap_end;
//static char *stack_top;
#if MICROPY_ENABLE_GC
//static char heap[2048];
#endif

// static const char fresh_boot_py[] =
// "# boot.py -- run on boot-up\r\n"
// "# can run arbitrary Python, but best to keep it minimal\r\n"
// "\r\n"
// "import machine\r\n"
// "import pyb\r\n"
// "#pyb.main('main.py') # main script to run after this one\r\n"
// #if MICROPY_HW_ENABLE_USB
// "#pyb.usb_mode('VCP+MSC') # act as a serial and a storage device\r\n"
// "#pyb.usb_mode('VCP+HID') # act as a serial device and a mouse\r\n"
// #endif
// ;

// static const char fresh_main_py[] =
// "# main.py -- put your code here!\r\n"
// ;

// // TODO
// static const char fresh_pybcdc_inf[] =
// #include "genhdr/pybcdc_inf.h"
// ;

// static const char fresh_readme_txt[] =
// "This is a MicroPython board\r\n"
// "\r\n"
// "You can get started right away by writing your Python code in 'main.py'.\r\n"
// "\r\n"
// "For a serial prompt:\r\n"
// " - Windows: you need to go to 'Device manager', right click on the unknown device,\r\n"
// "   then update the driver software, using the 'pybcdc.inf' file found on this drive.\r\n"
// "   Then use a terminal program like Hyperterminal or putty.\r\n"
// " - Mac OS X: use the command: screen /dev/tty.usbmodem*\r\n"
// " - Linux: use the command: screen /dev/ttyACM0\r\n"
// "\r\n"
// "Please visit http://micropython.org/help/ for further help.\r\n"
// ;

// avoid inlining to avoid stack usage within main()
MP_NOINLINE STATIC bool init_flash_fs(uint reset_mode) {
    return false; // no flash fs possible
    #if 0
    // init the vfs object
    fs_user_mount_t *vfs_fat = &fs_user_mount_flash;
    vfs_fat->flags = 0;
    pyb_flash_init_vfs(vfs_fat);

    // try to mount the flash
    FRESULT res = f_mount(&vfs_fat->fatfs);

    if (reset_mode == 3 || res == FR_NO_FILESYSTEM) {
        // no filesystem, or asked to reset it, so create a fresh one

        // LED on to indicate creation of LFS
        led_state(PYB_LED_GREEN, 1);
        uint32_t start_tick = HAL_GetTick();

        uint8_t working_buf[_MAX_SS];
        res = f_mkfs(&vfs_fat->fatfs, FM_FAT, 0, working_buf, sizeof(working_buf));
        if (res == FR_OK) {
            // success creating fresh LFS
        } else {
            printf("PYB: can't create flash filesystem\n");
            return false;
        }

        // set label
        f_setlabel(&vfs_fat->fatfs, MICROPY_HW_FLASH_FS_LABEL);

        // create empty main.py
        FIL fp;
        f_open(&vfs_fat->fatfs, &fp, "/main.py", FA_WRITE | FA_CREATE_ALWAYS);
        UINT n;
        f_write(&fp, fresh_main_py, sizeof(fresh_main_py) - 1 /* don't count null terminator */, &n);
        // TODO check we could write n bytes
        f_close(&fp);

        // create .inf driver file
        f_open(&vfs_fat->fatfs, &fp, "/pybcdc.inf", FA_WRITE | FA_CREATE_ALWAYS);
        f_write(&fp, fresh_pybcdc_inf, sizeof(fresh_pybcdc_inf) - 1 /* don't count null terminator */, &n);
        f_close(&fp);

        // create readme file
        f_open(&vfs_fat->fatfs, &fp, "/README.txt", FA_WRITE | FA_CREATE_ALWAYS);
        f_write(&fp, fresh_readme_txt, sizeof(fresh_readme_txt) - 1 /* don't count null terminator */, &n);
        f_close(&fp);

        // keep LED on for at least 200ms
        sys_tick_wait_at_least(start_tick, 200);
        led_state(PYB_LED_GREEN, 0);
    } else if (res == FR_OK) {
        // mount sucessful
    } else {
    fail:
        printf("PYB: can't mount flash\n");
        return false;
    }

    // mount the flash device (there should be no other devices mounted at this point)
    // we allocate this structure on the heap because vfs->next is a root pointer
    mp_vfs_mount_t *vfs = m_new_obj_maybe(mp_vfs_mount_t);
    if (vfs == NULL) {
        goto fail;
    }
    vfs->str = "/flash";
    vfs->len = 6;
    vfs->obj = MP_OBJ_FROM_PTR(vfs_fat);
    vfs->next = NULL;
    MP_STATE_VM(vfs_mount_table) = vfs;

    // The current directory is used as the boot up directory.
    // It is set to the internal flash filesystem by default.
    MP_STATE_PORT(vfs_cur) = vfs;

    // Make sure we have a /flash/boot.py.  Create it if needed.
    FILINFO fno;
    res = f_stat(&vfs_fat->fatfs, "/boot.py", &fno);
    if (res != FR_OK) {
        // doesn't exist, create fresh file

        // LED on to indicate creation of boot.py
        led_state(PYB_LED_GREEN, 1);
        uint32_t start_tick = HAL_GetTick();

        FIL fp;
        f_open(&vfs_fat->fatfs, &fp, "/boot.py", FA_WRITE | FA_CREATE_ALWAYS);
        UINT n;
        f_write(&fp, fresh_boot_py, sizeof(fresh_boot_py) - 1 /* don't count null terminator */, &n);
        // TODO check we could write n bytes
        f_close(&fp);

        // keep LED on for at least 200ms
        sys_tick_wait_at_least(start_tick, 200);
        led_state(PYB_LED_GREEN, 0);
    }

    return true;
    #endif
}

#if MICROPY_HW_HAS_SDCARD
STATIC bool init_sdcard_fs(void) {
    bool first_part = true;
    for (int part_num = 1; part_num <= 4; ++part_num) {
        // create vfs object
        fs_user_mount_t *vfs_fat = m_new_obj_maybe(fs_user_mount_t);
        mp_vfs_mount_t *vfs = m_new_obj_maybe(mp_vfs_mount_t);
        if (vfs == NULL || vfs_fat == NULL) {
            break;
        }
        vfs_fat->blockdev.flags = MP_BLOCKDEV_FLAG_FREE_OBJ;
        sdcard_init_vfs(vfs_fat, part_num);

        // try to mount the partition
        FRESULT res = f_mount(&vfs_fat->fatfs);

        if (res != FR_OK) {
            // couldn't mount
            m_del_obj(fs_user_mount_t, vfs_fat);
            m_del_obj(mp_vfs_mount_t, vfs);
        } else {
            // mounted via FatFs, now mount the SD partition in the VFS
            if (first_part) {
                // the first available partition is traditionally called "sd" for simplicity
                vfs->str = "/sd";
                vfs->len = 3;
            } else {
                // subsequent partitions are numbered by their index in the partition table
                if (part_num == 2) {
                    vfs->str = "/sd2";
                } else if (part_num == 2) {
                    vfs->str = "/sd3";
                } else {
                    vfs->str = "/sd4";
                }
                vfs->len = 4;
            }
            vfs->obj = MP_OBJ_FROM_PTR(vfs_fat);
            vfs->next = NULL;
            for (mp_vfs_mount_t **m = &MP_STATE_VM(vfs_mount_table);; m = &(*m)->next) {
                if (*m == NULL) {
                    *m = vfs;
                    break;
                }
            }

            #if MICROPY_HW_ENABLE_USB
            if (pyb_usb_storage_medium == PYB_USB_STORAGE_MEDIUM_NONE) {
                // if no USB MSC medium is selected then use the SD card
                pyb_usb_storage_medium = PYB_USB_STORAGE_MEDIUM_SDCARD;
            }
            #endif

            #if MICROPY_HW_ENABLE_USB
            // only use SD card as current directory if that's what the USB medium is
            if (pyb_usb_storage_medium == PYB_USB_STORAGE_MEDIUM_SDCARD)
            #endif
            {
                if (first_part) {
                    // use SD card as current directory
                    MP_STATE_PORT(vfs_cur) = vfs;
                }
            }
            first_part = false;
        }
    }

    if (first_part) {
        printf("PYB: can't mount SD card\n");
        return false;
    } else {
        return true;
    }
}
#endif

int tm4c_main(int reset_mode) {

    // Set the priority grouping
    // TODO NVIC_SetPriorityGrouping(NVIC_APINT_PRIGROUP_4_4);

    #if defined(MICROPY_BOARD_EARLY_INIT)
    MICROPY_BOARD_EARLY_INIT();
    #endif

    // basic sub-system init
    #if MICROPY_PY_THREAD
    pyb_thread_init(&pyb_thread_main);
    #endif
    // pendsv_init();
    //TODO led_init();
    #if MICROPY_HW_HAS_SWITCH
    switch_init0();
    #endif
    machine_init();
    #if MICROPY_HW_ENABLE_RTC
    rtc_init_start(false);
    #endif
    spi_init0();
    // disable_irq();
    // TODO 
//     #if MICROPY_HW_ENABLE_HW_I2C
//     i2c_init0();
//     #endif
    #if MICROPY_HW_HAS_SDCARD
    sdcard_init();
    #endif
//     storage_init();

soft_reset:

    // Python threading init
    #if MICROPY_PY_THREAD
    mp_thread_init();
    #endif
//    int stack_dummy;
    mp_stack_ctrl_init();

    mp_stack_set_top(&_estack);
    mp_stack_set_limit((char*)&_estack - (char*)&_stack - 1024);

    #if MICROPY_ENABLE_GC
    gc_init(&_heap_start, &_heap_end);
    #endif

    #if MICROPY_ENABLE_PYSTACK
    static mp_obj_t pystack[384];
    mp_pystack_init(pystack, &pystack[384]);
    #endif

    // MicroPython init
    mp_init();
    mp_obj_list_init(mp_sys_path, 0);
    mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(MP_QSTR_)); // current dir (or base dir of the script)
    mp_obj_list_init(mp_sys_argv, 0);

    readline_init0();
    pin_init0();
    extint_init0();
    //TODO timer_init0();
    uart_init0();

    dma_hw_init();

    //TODO Missing Repl Config

//     #if MICROPY_HW_ENABLE_USB
//     pyb_usb_init0();
//     #endif

    // Initialise the local flash filesystem.
    // Create it if needed, mount in on /flash, and set it as current dir.
    bool mounted_flash = init_flash_fs(reset_mode);

    bool mounted_sdcard = false;
#if MICROPY_HW_HAS_SDCARD
    // if an SD card is present then mount it on /sd/
    if (sdcard_is_present()) {
        // if there is a file in the flash called "SKIPSD", then we don't mount the SD card
        if (!mounted_flash || f_stat(&fs_user_mount_flash.fatfs, "/SKIPSD", NULL) != FR_OK) {
            mounted_sdcard = init_sdcard_fs();
        }
    }
#endif

//     #if MICROPY_HW_ENABLE_USB
//     // if the SD card isn't used as the USB MSC medium then use the internal flash
//     if (pyb_usb_storage_medium == PYB_USB_STORAGE_MEDIUM_NONE) {
//         pyb_usb_storage_medium = PYB_USB_STORAGE_MEDIUM_FLASH;
//     }
//     #endif

    // set sys.path based on mounted filesystems (/sd is first so it can override /flash)
    if (mounted_sdcard) {
        mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(MP_QSTR__slash_sd));
        mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(MP_QSTR__slash_sd_slash_lib));
    }
    if (mounted_flash) {
        mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(MP_QSTR__slash_flash));
        mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(MP_QSTR__slash_flash_slash_lib));
    }

    // reset config variables; they should be set by boot.py
    MP_STATE_PORT(pyb_config_main) = MP_OBJ_NULL;

    // run boot.py, if it exists
    // TODO perhaps have pyb.reboot([bootpy]) function to soft-reboot and execute custom boot.py
    if (reset_mode == 1 || reset_mode == 3) {
        const char *boot_py = "boot.py";
        mp_import_stat_t stat = mp_import_stat(boot_py);
        if (stat == MP_IMPORT_STAT_FILE) {
            int ret = pyexec_file(boot_py);
            if (ret & PYEXEC_FORCED_EXIT) {
                goto soft_reset_exit;
            }
            if (!ret) {
                flash_error(4);
            }
        }
    }

//     // turn boot-up LEDs off
// #if !defined(MICROPY_HW_LED2)
//     // If there is only one LED on the board then it's used to signal boot-up
//     // and so we turn it off here.  Otherwise LED(1) is used to indicate dirty
//     // flash cache and so we shouldn't change its state.
//     led_state(1, 0);
// #endif
//     led_state(2, 0);
//     led_state(3, 0);
//     led_state(4, 0);

//     // Now we initialise sub-systems that need configuration from boot.py,
//     // or whose initialisation can be safely deferred until after running
//     // boot.py.

//     #if MICROPY_HW_ENABLE_USB
//     // init USB device to default setting if it was not already configured
//     if (!(pyb_usb_flags & PYB_USB_FLAG_USB_MODE_CALLED)) {
//         pyb_usb_dev_init(USBD_VID, USBD_PID_CDC_MSC, USBD_MODE_CDC_MSC, NULL);
//     }
//     #endif
// // At this point everything is fully configured and initialised.

    // Run the main script from the current directory.
    if ((reset_mode == 1 || reset_mode == 3) && pyexec_mode_kind == PYEXEC_MODE_FRIENDLY_REPL) {
        const char *main_py;
        if (MP_STATE_PORT(pyb_config_main) == MP_OBJ_NULL) {
            main_py = "main.py";
        } else {
            main_py = mp_obj_str_get_str(MP_STATE_PORT(pyb_config_main));
        }
        mp_import_stat_t stat = mp_import_stat(main_py);
        if (stat == MP_IMPORT_STAT_FILE) {
            int ret = pyexec_file(main_py);
            if (ret & PYEXEC_FORCED_EXIT) {
                goto soft_reset_exit;
            }
            if (!ret) {
                flash_error(3);
            }
        }
    }

    // Main script is finished, so now go into REPL mode.
    // The REPL mode can change, or it can request a soft reset.
    pyexec_mode_kind = PYEXEC_MODE_FRIENDLY_REPL;
    for (;;) {
        if (pyexec_mode_kind == PYEXEC_MODE_RAW_REPL) {
            if (pyexec_raw_repl() != 0) {
                break;
            }
        } else {
            if (pyexec_friendly_repl() != 0) {
                break;
            }
        }
    }
    return 0;

soft_reset_exit:

    // soft reset

    printf("PYB: sync filesystems\n");
    // storage_flush();

    printf("PYB: soft reboot\n");
    // TODO timer_deinit();
    uart_deinit();
#if MICROPY_HW_ENABLE_CAN
    can_deinit();
#endif
    machine_deinit();

    #if MICROPY_PY_THREAD
    pyb_thread_deinit();
    #endif

    goto soft_reset;
}

//void gc_collect(void) {
//    // WARNING: This gc_collect implementation doesn't try to get root
//    // pointers from CPU registers, and thus may function incorrectly.
//    void *dummy;
//    gc_collect_start();
//    gc_collect_root(&dummy, ((mp_uint_t)stack_top - (mp_uint_t)&dummy) / sizeof(mp_uint_t));
//    gc_collect_end();
//    gc_dump_info();
//}

//void gc_collect(void) {
//
//    gc_collect_start();
//    // get the registers and the sp
//    uintptr_t regs[10];
//    uintptr_t sp = gc_helper_get_regs_and_sp(regs);
//
//    // trace the stack, including the registers (since they live on the stack in this function)
//    #if MICROPY_PY_THREAD
//    gc_collect_root((void**)sp, ((uint32_t)MP_STATE_THREAD(stack_top) - sp) / sizeof(uint32_t));
//    #else
//    gc_collect_root((void**)sp, ((uint32_t)&_ram_end - sp) / sizeof(uint32_t));
//    #endif
//
//    // trace root pointers from any threads
//    #if MICROPY_PY_THREAD
//    mp_thread_gc_others();
//    #endif
//
//    // end the GC
//    gc_collect_end();
//    gc_dump_info();
//}

// mp_lexer_t *mp_lexer_new_from_file(const char *filename) {
//    mp_raise_OSError(MP_ENOENT);
// }

// mp_import_stat_t mp_import_stat(const char *path) { // @suppress("Type cannot be resolved")
//    return MP_IMPORT_STAT_NO_EXIST;
// }

// mp_obj_t mp_builtin_open(size_t n_args, const mp_obj_t *args, mp_map_t *kwargs) {
//    return mp_const_none;
// }
// MP_DEFINE_CONST_FUN_OBJ_KW(mp_builtin_open_obj, 1, mp_builtin_open);

#if MICROPY_MIN_USE_CORTEX_CPU
void _start(void) {
    // when we get here: stack is initialised, bss is clear, data is copied

    // SCB->CCR: enable 8-byte stack alignment for IRQ handlers, in accord with EABI
    *((volatile uint32_t*)0xe000ed14) |= 1 << 9;

    // initialise the cpu and peripherals
    #if MICROPY_MIN_USE_TM4C123_MCU
    void tm4c123_init(void);
    tm4c123_init();
    #endif

    // now that we have a basic system up and running we can call tm4c_main
    tm4c_main(1);

    // we must not return
    for (;;) {
    }
}

#endif

#if MICROPY_MIN_USE_TM4C123_MCU

//typedef struct {
//    volatile uint32_t DID0;
//    volatile uint32_t DID1;
//    uint32_t _1[10];
//    volatile uint32_t PBORCTL;
//    uint32_t _2[7];
//    volatile uint32_t RIS;
//    volatile uint32_t IMC;
//    volatile uint32_t MISC;
//    volatile uint32_t RESC;
//    volatile uint32_t RCC;
//    uint32_t _3[2];
//    volatile uint32_t GPIOHBCTL;
//    volatile uint32_t RCC2;
//    uint32_t _4[2];
//    volatile uint32_t MOSCCTL;
//    uint32_t _5[49];
//    volatile uint32_t DSLPCLKCFG;
//    uint32_t _6;
//    volatile uint32_t SYSPROP;
//    volatile uint32_t PIOSCCAL;
//    volatile uint32_t PIOSCSTAT;
//    uint32_t _7[2];
//    volatile uint32_t PLLFREQ0;
//    volatile uint32_t PLLFREQ1;
//    volatile uint32_t PLLSTAT;
//    uint32_t _8[7];                 // 0x16C - 0x187
//    uint32_t LPCR[18];              // LPWR Config Registers 0x188 - 0x1CF
//    uint32_t _9[76];                // 0x1D0 - 0x2FF
//    uint32_t PPR[24];               // Peripheral Present Registers 0x300 - 0x35F
//    uint32_t _10[104];              // 0x360 - 0x4FF
//    uint32_t PRR[24];               // Peripheral Reset Registers 0x500 - 0x55F
//    uint32_t _11[40];               // 0x560 - 0x5FF
//    volatile uint32_t RCGCWD;       // Peripheral Run Mode Clock Gating Control begin
//    volatile uint32_t RCGCTIMER;
//    volatile uint32_t RCGCGPIO;
//    volatile uint32_t RCGCDMA;
//    uint32_t _12;
//    volatile uint32_t RCGCHIB;
//    volatile uint32_t RCGCUART;
//    volatile uint32_t RCGCSSI;
//    volatile uint32_t RCGCI2C;
//    uint32_t _13;
//    volatile uint32_t RCGCUSB;
//    uint32_t _14[2];
//    volatile uint32_t RCGCCAN;
//    volatile uint32_t RCGCADC;
//    volatile uint32_t RCGCACMP;
//    volatile uint32_t RCGCPWM;
//    volatile uint32_t RCGCQEI;
//    uint32_t _15[4];
//    volatile uint32_t RCGCEEPROM;
//    volatile uint32_t RCGCWTIMER;
//    uint32_t _16[40];               // 0x660 - 0x6FF
//    uint32_t SCGC[24];              // Peripheral Sleep Mode Clock Gating Control
//    uint32_t _17[40];               // 0x760 - 0x7FF
//    uint32_t DCGC[24];              // Peripheral Deep-Sleep Mode Clock Gating Control
//    uint32_t _18[104];              // 0x860 - 0x9FF
//    volatile uint32_t PRWD;         // Peripheral Ready begin
//    volatile uint32_t PRTIMER;
//    volatile uint32_t PRGPIO;
//    volatile uint32_t PRDMA;
//    uint32_t _19;
//    volatile uint32_t PRHIB;
//    volatile uint32_t PRUART;
//    volatile uint32_t PRSSI;
//    volatile uint32_t PRI2C;
//    uint32_t _20;
//    volatile uint32_t PRUSB;
//    uint32_t _21[2];
//    volatile uint32_t PRCAN;
//    volatile uint32_t PRADC;
//    volatile uint32_t PRACMP;
//    volatile uint32_t PRPWM;
//    volatile uint32_t PRQEI;
//    uint32_t _22[4];
//    volatile uint32_t PREEPROM;
//    volatile uint32_t PRWTIMER;
//} periph_sysctl_t;





#define UART0  ((periph_uart_t*) 0x4000C000)

//#define SYSCTL ((periph_sysctl_t*)  0x400FE000)

void tm4c123_init(void) {
    // basic MCU config

    // // set system clock to 80MHz
    SYSCTL->RCC |= (uint32_t)0x00000800;   // set BYPASS bit
    SYSCTL->RCC2 |= (uint32_t)0xC0000800;  // set BYPASS2 bit, DIV400 and USERCC2
    SYSCTL->RCC &= (uint32_t)0xFFBFFFFF;   // clear USESYSDIV bit
    SYSCTL->RCC = (SYSCTL->RCC & (uint32_t)0xFFFFF83F) | (uint32_t)0x00000B70;    // XTAL to 16 MHz
    SYSCTL->MISC &= 0xFFFFFFBF;            // clear PLLLRIS bit
    SYSCTL->RCC2 &= (uint32_t)0xFFFFDFFF;  // clear PWRDN2 Bit to enable PLL
    SYSCTL->RCC2 = (SYSCTL->RCC2 & (uint32_t)0xE03FFFFF) | (uint32_t)0x01000000;  // Set SYSDIV2 to 0x2 for 80MHz
    SYSCTL->RCC |= (uint32_t)0x00400000;   // set USESYSDIV bit
    while(!(SYSCTL->RIS & 0x00000040)){};  // wait for Pll to lock, PLLLRIS bit
    SYSCTL->RCC2 &= 0xFFFFF7FF;            // clear BYPASS2 bit, clears BYPASS as well
    // write final configuration
    SYSCTL->RCC = (uint32_t)(0x07C00550);  // 0b0000 0 1111 1 0 0 000 000 0 0 0 10101 01 000 0
    SYSCTL->RCC2 = (uint32_t)(0xC1000000); // 0b 1100 0001 0000 0000 0000 0000 0000 0000

    //SystemInit();
    // enable high performance GPIO BUS Ctl
    SYSCTL->GPIOHBCTL = 0x0000003F;

    // enable GPIO Port F
        SYSCTL->RCGCGPIO |= 0x00000020;
    //*((volatile uint32_t*)0x400FE608) = 0x00000021;
    while( !(SYSCTL->PRGPIO & 0x00000020)){};
    //while( *((volatile uint32_t*)0x400FEA08) != 0x00000021){};

//    // turn on an LED! (on pyboard it's the red one)
//    gpio_init(GPIOF, 1, GPIO_MODE_OUT, GPIO_PULL_NONE, 0);
//    gpio_init(GPIOF, 2, GPIO_MODE_OUT, GPIO_PULL_NONE, 0);
//    gpio_init(GPIOF, 3, GPIO_MODE_OUT, GPIO_PULL_NONE, 0);
//
//    gpio_high(GPIOF, 3);
//    //*((volatile uint32_t*)0x4005D3FC) = 0x00000007;

    // enable UART0 at 9600 baud (TX=A1, RX=A0)
    // enable UART0
    SYSCTL->RCGCUART = 0x00000001;
    //*((volatile uint32_t*)0x400FE618) = 0x00000001;
    while(SYSCTL->PRUART != 0x00000001){};
    //while( *((volatile uint32_t*)0x400FEA18) != 0x00000001){};

    // enable GPIO Port A
    SYSCTL->RCGCGPIO |= 0x00000001;
    while( !(SYSCTL->PRGPIO & 0x00000001)){};

//    // GPIOA already configured for UART0 after reset
//    gpio_init(GPIOA, 0, GPIO_MODE_ALT, GPIO_PULL_NONE, 1);
//    gpio_init(GPIOA, 1, GPIO_MODE_ALT, GPIO_PULL_NONE, 1);

    MAP_GPIOPinTypeUART(GPIO_PORTA_AHB_BASE, GPIO_PIN_0 | GPIO_PIN_1);

    // BRD = IBRD + FRAC = UARTSysClk / (ClkDiv * BaudRate)
    // 520.8333 = 104  + 0.166 = 80MHz      / (16     * 9600    )
    // FBRD = (0.833333 * 64 + 0.5) ~ 53
    // UART0->CTL &= ~0x00000001;
    // UART0->IBRD = 0x00000208;
    // UART0->FBRD = 0x00000035;
    // UART0->LCRH = 0x00000070; // no stick parity, word length 8bit, FIFO enable, one STOP bit, odd parity, no parity check, no break
    // UART0->CC = 0x00000000;   // use SysClock
    // UART0->CTL = 0x00000300; // disable cts & rts, RXE, TXE, no loopback, 16x oversampling, TXRIS on IFLS match, no smart card, no low power, no SIR, UART enabled
    // UART0->CTL |= 0x00000001;
    // to change settings in active mode: page 918 of reference

    UARTDisable(UART0_BASE);
    UARTClockSourceSet(UART0_BASE, UART_CLOCK_SYSTEM);
    UARTFlowControlSet(UART0_BASE, UART_FLOWCONTROL_NONE);
    UARTFIFOEnable(UART0_BASE);
    UARTConfigSetExpClk(UART0_BASE, SysCtlClockGet(), 115200, UART_CONFIG_WLEN_8 | UART_CONFIG_PAR_NONE | UART_CONFIG_STOP_ONE);
    UARTEnable(UART0_BASE);


    //Setup of Systick to 1ms
    //Already registered in int vector
//    SysTickIntDisable();
//    SysTickIntRegister(SysTick_Handler);

    ROM_FPUEnable();
    ROM_FPULazyStackingEnable();
    
    SysTickIntEnable();
    SysTickPeriodSet(SysCtlClockGet()/SYS_TICK_DIVIDER);
    SysTickEnable();
}

#endif
