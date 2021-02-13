/******************************************************************************

 * Source: https://e2e.ti.com/support/microcontrollers/other/f/908/p/479812/2164975

 *****************************************************************************/

MEMORY
{
    FLASH (RX) : ORIGIN = 0x00000000, LENGTH = 256K 
	/* FLASH (RX) : ORIGIN = 0x00000000, LENGTH = 232K
	FLASH_FS (WX) : ORIGIN = 0x0003A000, LENGTH = 24K */
	SRAM  (WX) : ORIGIN = 0x20000000, LENGTH = 32K 
    /*SRAM  (WX) : ORIGIN = 0x20000000, LENGTH = 24K
	CACHE (WX) : ORIGIN = 0x20006000, LENGTH = 8K */
}

/* SRAM extents for the garbage collector */
_ram_start = ORIGIN(SRAM);
_ram_end = ORIGIN(SRAM) + LENGTH(SRAM);

/*_flash_fs_start = ORIGIN(FLASH_FS);
_flash_fs_end = ORIGIN(FLASH_FS) + LENGTH(FLASH_FS);

_cache_start = ORIGIN(CACHE);
_cache_end = ORIGIN(CACHE) + LENGTH(CACHE);*/

_Min_Heap_Size = 0x6000;
_Min_Stack_Size = 0x0800;


ENTRY(Reset_Handler)

SECTIONS {

    PROVIDE (_intvecs_base_address = 0x0);

    .intvecs (_intvecs_base_address) : AT (_intvecs_base_address) {
        KEEP (*(.vectors))
    } > FLASH

    PROVIDE (_vtable_base_address = 0x20000000);

    .vtable (_vtable_base_address) : AT (_vtable_base_address) {
        KEEP (*(.vtable))
    } > SRAM

    .text : {
        CREATE_OBJECT_SYMBOLS
        *(.text)
        *(.text.*)
        . = ALIGN(0x4);
        KEEP (*(.ctors))
        . = ALIGN(0x4);
        KEEP (*(.dtors))
        . = ALIGN(0x4);
        __init_array_start = .;
        KEEP (*(.init_array*))
        __init_array_end = .;
        *(.init)
        *(.fini*)
    } > FLASH

    PROVIDE (__etext = .);
    PROVIDE (_etext = .);
    PROVIDE (etext = .);
	PROVIDE (_sidata = .);

    .rodata : {
        *(.rodata)
        *(.rodata*)
    } > FLASH

    .data : ALIGN (0x4) {
        __data_load__ = LOADADDR (.data);
        __data_start__ = .;
		_sdata = __data_start__;
        *(.data)
        *(.data*)
        . = ALIGN (0x4);
        __data_end__ = .;
		_edata = __data_end__;
    } > SRAM AT> FLASH

    .ARM.exidx : {
        __exidx_start = .;
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        __exidx_end = .;
    } > FLASH

    .ARM.extab : {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } > FLASH

    .bss : {
        __bss_start__ = .;
		_sbss = __bss_start__;
        *(.shbss)
        *(.bss)
        *(.bss.*)
        *(COMMON)
        . = ALIGN (4);
        __bss_end__ = .;
		_ebss = __bss_end__;
    } > SRAM

    .heap : {
        PROVIDE (__heap_start__ = .);
        _heap_start = __heap_start__;
        . = . + _Min_Heap_Size;
        KEEP(*(.heap))
        PROVIDE (__heap_end__ = .);
        __HeapLimit = __heap_end__;
        _heap_end = __heap_end__;
    } > SRAM

    _Heapsize = __heap_end__ - __heap_start__;
    _Stacksize = _ram_end - __heap_end__ - 4;

    .stack : ALIGN(0x8) {
        PROVIDE (_stack = .);
        __stack = .;
        . = . + _Stacksize;
        KEEP(*(.stack))
        PROVIDE (_estack = .);
        __estack = .;
    } > SRAM

    /DISCARD/ :
    {
        libc.a ( * )
        libm.a ( * )
        libgcc.a ( * )
    }

	ASSERT(_Min_Heap_Size <= _Heapsize, "Could not allocate enough space for heap")
	ASSERT(_Min_Stack_Size <= _Stacksize, "Could not allocate enough space for stack")
}
