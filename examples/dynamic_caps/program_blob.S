/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/* The program the manager loads, as bytes in the manager's image */

    .section .rodata
    .balign 4096
    .global _program
_program:
    .incbin PROGRAM_BIN
    .global _program_end
_program_end:
