/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

ENTRY(_start)

SECTIONS
{
    . = 0x80000000;
    .text : {
        *(.text.start)
        *(.text*)
        *(.rodata*)
    }
    /DISCARD/ : {
        *(.data*)
        *(.bss*)
        *(.comment)
    }
}
