/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * The runner is loaded by the host (sandbox_host.c), which maps each segment
 * with its own permissions, so segments start on page boundaries and never
 * share a page. SANDBOX_RUNNER_VADDR in sandbox.h must match.
 */

ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x10000000;
    .text : {
        *(.text.start)
        *(.text .text.*)
    } :text

    . = ALIGN(0x1000);
    .rodata : {
        *(.rodata .rodata.*)
        __init_array_start = .;
        KEEP(*(.init_array .init_array.*))
        __init_array_end = .;
        __fini_array_start = .;
        KEEP(*(.fini_array .fini_array.*))
        __fini_array_end = .;
    } :rodata

    . = ALIGN(0x1000);
    .data : {
        *(.data .data.*)
    } :data
    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    } :data

    /DISCARD/ : {
        *(.comment)
        *(.note .note.*)
        *(.eh_frame .eh_frame_hdr)
        *(.interp)
    }
}
