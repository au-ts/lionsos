/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/* The runner, as an ELF file in the host's image, for the host to load into sandboxes */

    .section .rodata
    .balign 16
    .global _runner_elf
_runner_elf:
    .incbin RUNNER_ELF
    .global _runner_elf_end
_runner_elf_end:
