OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_start)

MEMORY {
    ewram : ORIGIN = 0x02000000, LENGTH = 256K
    iwram : ORIGIN = 0x03000000, LENGTH = 28K
}

SECTIONS {
    .crt0 : { KEEP(*(.crt0)) } > ewram
    .text : { *(.text .text.* .glue_7 .glue_7t) } > ewram
    .rodata : { *(.rodata .rodata.*) } > ewram
    .data : { *(.data .data.*) . = ALIGN(4); } > ewram
    .iwram : {
        __iwram_start = .;
        *(.iwram .iwram.*)
        . = ALIGN(4);
        __iwram_end = .;
    } > iwram AT > ewram
    __iwram_lma = LOADADDR(.iwram);
    __image_end = __iwram_lma + SIZEOF(.iwram);
    .bss __image_end (NOLOAD) : {
        __bss_start = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > ewram
    .ibss (NOLOAD) : {
        *(.ibss .ibss.*)
        . = ALIGN(4);
        __ibss_end = .;
    } > iwram
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.attributes) *(.comment) }
}
