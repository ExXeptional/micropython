#include <stdint.h>
#include "cmsis_gcc.h"
#include "mpconfigboard.h"

// options to control how MicroPython is built

// You can disable the built-in MicroPython compiler by setting the following
// config option to 0.  If you do this then you won't get a REPL prompt, but you
// will still be able to execute pre-compiled scripts, compiled with mpy-cross.
#define MICROPY_ENABLE_COMPILER     (1)

#define MICROPY_QSTR_BYTES_IN_HASH  (1)
//#define MICROPY_QSTR_EXTRA_POOL     mp_qstr_frozen_const_pool
#define MICROPY_ALLOC_PATH_MAX      (256)
#define MICROPY_ALLOC_PARSE_CHUNK_INIT (16)
#define MICROPY_EMIT_X64            (0)
#define MICROPY_EMIT_THUMB          (0)
#define MICROPY_EMIT_INLINE_THUMB   (0)
#define MICROPY_COMP_MODULE_CONST   (0)
#define MICROPY_COMP_CONST          (0)
#define MICROPY_COMP_DOUBLE_TUPLE_ASSIGN (0)
#define MICROPY_COMP_TRIPLE_TUPLE_ASSIGN (0)
#define MICROPY_MEM_STATS           (0)
#define MICROPY_DEBUG_VERBOSE       (0)
#define MICROPY_DEBUG_PRINTERS      (MICROPY_DEBUG_VERBOSE)
#define MICROPY_ENABLE_GC           (1)
#define MICROPY_GC_ALLOC_THRESHOLD  (1)
#define MICROPY_ENABLE_FINALISER    (0)
#define MICROPY_REPL_EVENT_DRIVEN   (0)
#define MICROPY_HELPER_REPL         (1)
#define MICROPY_REPL_AUTO_INDENT    (1)
#define MICROPY_HELPER_LEXER_UNIX   (0)
#define MICROPY_ENABLE_SOURCE_LINE  (0)
#define MICROPY_ENABLE_DOC_STRING   (0)
#define MICROPY_STREAMS_NON_BLOCK   (1)
//#define MICROPY_ERROR_REPORTING     (MICROPY_ERROR_REPORTING_TERSE)
#define MICROPY_BUILTIN_METHOD_CHECK_SELF_ARG (0)
#define MICROPY_PY_ASYNC_AWAIT      (0)
#define MICROPY_PY_BUILTINS_BYTEARRAY (1)
#define MICROPY_PY_BUILTINS_MEMORYVIEW (0)
#define MICROPY_PY_BUILTINS_ENUMERATE (0)
#define MICROPY_PY_BUILTINS_FILTER  (0)
#define MICROPY_PY_BUILTINS_FROZENSET (0)
#define MICROPY_PY_BUILTINS_REVERSED (0)
#define MICROPY_PY_BUILTINS_SET     (0)
#define MICROPY_PY_BUILTINS_SLICE   (0)
#define MICROPY_PY_BUILTINS_PROPERTY (0)
#define MICROPY_PY_BUILTINS_MIN_MAX (0)
#define MICROPY_PY___FILE__         (0)
#define MICROPY_PY_GC               (0) // 0
#define MICROPY_PY_ARRAY            (1)
#define MICROPY_PY_ATTRTUPLE        (1)
#define MICROPY_PY_COLLECTIONS      (1)
#define MICROPY_PY_MATH             (1)
#define MICROPY_PY_CMATH            (1)
#define MICROPY_PY_IO               (1)
#define MICROPY_PY_STRUCT           (1)
#define MICROPY_PY_SYS              (1)
#define MICROPY_VFS                 (1)
#define MICROPY_VFS_FAT             (1)
#define MICROPY_READER_VFS          (1)
#define MICROPY_MODULE_FROZEN_MPY   (0) //was 1
#define MICROPY_CPYTHON_COMPAT      (0)
// #define MICROPY_LONGINT_IMPL        (MICROPY_LONGINT_IMPL_NONE)
// #define MICROPY_FLOAT_IMPL          (MICROPY_FLOAT_IMPL_NONE)
#define MICROPY_PY_THREAD           (0)
#define MICROPY_PY_OS_DUPTERM       (1)
#define MICROPY_PY_MACHINE          (0)
#define MICROPY_KBD_EXCEPTION       (1)

// extended modules
#define MICROPY_PY_UCTYPES          (1)
// #define MICROPY_PY_UZLIB            (1)
// #define MICROPY_PY_UJSON            (1)
// #define MICROPY_PY_URE              (1)
#define MICROPY_PY_UHEAPQ           (1)
#define MICROPY_PY_UHASHLIB         (1)
#define MICROPY_PY_UBINASCII        (1)
// #define MICROPY_PY_URANDOM          (1)
// #define MICROPY_PY_URANDOM_EXTRA_FUNCS (1)
// #define MICROPY_PY_USELECT          (1)
// #define MICROPY_PY_UTIMEQ           (1)
// #define MICROPY_PY_UTIME_MP_HAL     (1)

// optimisations
#define MICROPY_OPT_COMPUTED_GOTO   (1)
#define MICROPY_OPT_CACHE_MAP_LOOKUP_IN_BYTECODE (0)
#define MICROPY_OPT_MPZ_BITWISE     (1)

// sd card
#define MICROPY_HW_HAS_SDCARD       (1)

// fatfs configuration used in ffconf.h
#define MICROPY_FATFS_ENABLE_LFN       (1)
#define MICROPY_FATFS_LFN_CODE_PAGE    437 /* 1=SFN/ANSI 437=LFN/U.S.(OEM) */
#define MICROPY_FATFS_USE_LABEL        (1)
#define MICROPY_FATFS_RPATH            (2)
#define MICROPY_FATFS_MULTI_PARTITION  (1)

// TODO these should be generic, not bound to fatfs
#define mp_type_fileio fatfs_type_fileio
#define mp_type_textio fatfs_type_textio

// use vfs's functions for import stat and builtin open
#define mp_import_stat mp_vfs_import_stat
#define mp_builtin_open mp_vfs_open
#define mp_builtin_open_obj mp_vfs_open_obj


// type definitions for the specific machine

#define MICROPY_MAKE_POINTER_CALLABLE(p) ((void*)((mp_uint_t)(p) | 1))

// This port is intended to be 32-bit, but unfortunately, int32_t for
// different targets may be defined in different ways - either as int
// or as long. This requires different printf formatting specifiers
// to print such value. So, we avoid int32_t and use int directly.
#define UINT_FMT "%u"
#define INT_FMT "%d"
typedef int mp_int_t; // must be pointer size
typedef unsigned mp_uint_t; // must be pointer size

typedef long mp_off_t;

// We have inlined IRQ functions for efficiency (they are generally
// 1 machine instruction).
//
// Note on IRQ state: you should not need to know the specific
// value of the state variable, but rather just pass the return
// value from disable_irq back to enable_irq.  If you really need
// to know the machine-specific values, see irq.h.

static inline void enable_irq(mp_uint_t state) {
    __set_PRIMASK(state);
}

static inline mp_uint_t disable_irq(void) {
    mp_uint_t state = __get_PRIMASK();
    __disable_irq();
    return state;
}

#define MICROPY_BEGIN_ATOMIC_SECTION()     disable_irq()
#define MICROPY_END_ATOMIC_SECTION(state)  enable_irq(state)

#define MP_PLAT_PRINT_STRN(str, len) mp_hal_stdout_tx_strn_cooked(str, len)

#if MICROPY_PY_THREAD
#define MICROPY_EVENT_POLL_HOOK \
    do { \
        extern void mp_handle_pending(bool); \
        mp_handle_pending(true); \
        if (pyb_thread_enabled) { \
            MP_THREAD_GIL_EXIT(); \
            pyb_thread_yield(); \
            MP_THREAD_GIL_ENTER(); \
        } else { \
            __WFI(); \
        } \
    } while (0);

#define MICROPY_THREAD_YIELD() pyb_thread_yield()
#else
#define MICROPY_EVENT_POLL_HOOK \
    do { \
        extern void mp_handle_pending(bool); \
        mp_handle_pending(true); \
        __WFI(); \
    } while (0);

#define MICROPY_THREAD_YIELD()
#endif

extern const struct _mp_obj_module_t machine_module;
extern const struct _mp_obj_module_t mp_module_uos;
//extern const struct _mp_obj_module_t pin_module;

// extra built in names to add to the global namespace
#define MICROPY_PORT_BUILTINS
//      { MP_ROM_QSTR(MP_QSTR_umachine), MP_ROM_PTR(&machine_module) },
//      { MP_ROM_QSTR(MP_QSTR_Pin),                 MP_ROM_PTR(&pin_module) },
//    { MP_ROM_QSTR(MP_QSTR_umachine), MP_ROM_PTR(&machine_module) },
//    { MP_ROM_QSTR(MP_QSTR_open), MP_ROM_PTR(&mp_builtin_open_obj) },

// We need to provide a declaration/definition of alloca()
#include <alloca.h>

//#define MICROPY_HW_BOARD_NAME "Tiva Launch Pad"
//#define MICROPY_HW_MCU_NAME "TM4C123G6HPM"

#ifdef __linux__
#define MICROPY_MIN_USE_STDOUT (1)
#endif

#ifdef __thumb__
#define MICROPY_MIN_USE_CORTEX_CPU (1)
#define MICROPY_MIN_USE_TM4C123_MCU (1)
#endif

#define MICROPY_PORT_BUILTIN_MODULES \
    { MP_ROM_QSTR(MP_QSTR_umachine), MP_ROM_PTR(&machine_module) }, \
    { MP_ROM_QSTR(MP_QSTR_uos), MP_ROM_PTR(&mp_module_uos) }, 

#define MICROPY_PORT_CONSTANTS \
    { MP_ROM_QSTR(MP_QSTR_umachine), MP_ROM_PTR(&machine_module) }, \
    { MP_ROM_QSTR(MP_QSTR_machine), MP_ROM_PTR(&machine_module) },

#define MP_STATE_PORT MP_STATE_VM

#define MICROPY_PORT_ROOT_POINTERS \
    const char *readline_hist[8]; \
    mp_obj_t pyb_hid_report_desc; \
    \
    mp_obj_t pyb_config_main; \
    mp_obj_t machine_config_main; \
    \
    mp_obj_t pyb_switch_callback; \
    \
    /* Requred by exint module required by moduos*/ \
    mp_obj_t pyb_extint_callback[PYB_EXTI_NUM_VECTORS]; \
    \
    mp_obj_t pin_class_mapper; \
    mp_obj_t pin_class_map_dict; \
    \
    mp_obj_t test_callback_obj; \
    /* stdio is repeated on this UART object if it's not null */ \
    struct _machine_uart_obj_t *machine_stdio_uart; \
    \
    /* pointers to all UART objects (if they have been created) */ \
    struct _machine_uart_obj_t *machine_uart_obj_all[MICROPY_HW_MAX_UART]; \
    struct _machine_hard_spi_obj_t *machine_spi_obj_all[MICROPY_HW_MAX_SPI];\

//EOF
