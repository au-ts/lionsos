/*
 * Copyright 2024, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#define LIBCO_C
#include "libco.h"
#include "settings.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

void co_panic() {
    char *panic_addr = (char *)0;
    *panic_addr = (char)0;
}

// Cothread context memory layout:
// Registers... | c_entry | pc | canary | ... | <- stack
// If stack grows into canary then we crash!

// control region indexes at the beginning of cothread local storage
enum
{
    ra,
    sp,
    fp, // AKA s0
    s1,
    s2,
    s3,
    s4,
    s5,
    s6,
    s7,
    s8,
    s9,
    s10,
    s11,
    f8, // AKA fs0
    f9, // and so on...
    f18,
    f19,
    f20,
    f21,
    f22,
    f23,
    f24,
    f25,
    f26,
    f27,
    client_entry,
    pc,
    canary
};

#define STACK_CANARY (uintptr_t) 0x341294AA8642FE71

static thread_local uintptr_t co_active_buffer[32] = {
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    STACK_CANARY
};
static thread_local cothread_t co_active_handle = &co_active_buffer;

// co_swap(char *to, char *from)
static void (*co_swap)(cothread_t, cothread_t) = 0;

// Quick reference: https://inst.eecs.berkeley.edu/~cs61c/fa17/img/riscvcard.pdf
section(text)
    const uint32_t co_swap_function[1024] = {
        // Instructions encoded with this tool: https://luplab.gitlab.io/rvcodecjs/

        // Begin saving callee saved registers of current context

        // RV64I InsSet
        0x0015b023, // sd ra, 0(a1)
        0x0025b423, // sd sp, 8(a1)
        0x0085b823, // sd s0, 16(a1)
        0x0095bc23, // sd s1, 24(a1)
        0x0325b023, // sd s2, 32(a1)
        0x0335b423, // sd s3, 40(a1)
        0x0345b823, // sd s4, 48(a1)
        0x0355bc23, // sd s5, 56(a1)
        0x0565b023, // sd s6, 64(a1)
        0x0575b423, // sd s7, 72(a1)
        0x0585b823, // sd s8, 80(a1)
        0x0595bc23, // sd s9, 88(a1)
        0x07a5b023, // sd s10, 96(a1)
        0x07b5b423, // sd s11, 104(a1)

        // Save floating point saved register
        // Unused because the Microkit SDK itself is soft-float
        // We can't link soft float and hard float objects together

        // 0x0685b827, // fsd f8, 112(a1)  EQUIV fsd fs0, 112(a1)
        // 0x0695bc27, // fsd f9, 120(a1)
        // 0x0925b027, // fsd f18, 128(a1)
        // 0x0935b427, // fsd f19, 136(a1)
        // 0x0945b827, // fsd f20, 144(a1)
        // 0x0955bc27, // fsd f21, 152(a1)
        // 0x0b65b027, // fsd f22, 160(a1)
        // 0x0b75b427, // fsd f23, 168(a1)
        // 0x0b85b827, // fsd f24, 176(a1)
        // 0x0b95bc27, // fsd f25, 184(a1)
        // 0x0da5b027, // fsd f26, 192(a1)
        // 0x0db5b427, // fsd f27, 200(a1)

        // When co_swap is called, `ra` have the PC we need to resume the `from` cothread!
        0x0c15bc23, // sd ra, 216(a1)

        // Begin loading callee saved registers of the context we are switching to
        0x00053083, // ld ra, 0(a0)
        0x00853103, // ld sp, 8(a0)
        0x01053403, // ld s0, 16(a0)
        0x01853483, // ld s1, 24(a0)
        0x02053903, // ld s2, 32(a0)
        0x02853983, // ld s3, 40(a0)
        0x03053a03, // ld s4, 48(a0)
        0x03853a83, // ld s5, 56(a0)
        0x04053b03, // ld s6, 64(a0)
        0x04853b83, // ld s7, 72(a0)
        0x05053c03, // ld s8, 80(a0)
        0x05853c83, // ld s9, 88(a0)
        0x06053d03, // ld s10, 96(a0)
        0x06853d83, // ld s11, 104(a0)

        // 0x07053407, // fld f8, 112(a0)
        // 0x07853487, // fld f9, 120(a0)
        // 0x08053907, // fld f18, 128(a0)
        // 0x08853987, // fld f19, 136(a0)
        // 0x09053a07, // fld f20, 144(a0)
        // 0x09853a87, // fld f21, 152(a0)
        // 0x0a053b07, // fld f22, 160(a0)
        // 0x0a853b87, // fld f23, 168(a0)
        // 0x0b053c07, // fld f24, 176(a0)
        // 0x0b853c87, // fld f25, 184(a0)
        // 0x0c053d07, // fld f26, 192(a0)
        // 0x0c853d87, // fld f27, 200(a0)

        // load the PC of the destination context then jump to it
        // discard link result
        0x0d853603, // ld a2, 216(a0)
        0x00060067, // jalr a2, 0(a2)
        // Note to reader, `jalr a2, a2` is not equivalent!
};

static void co_entrypoint(void) {
    uintptr_t *buffer = (uintptr_t *)co_active_handle;
    void (*entrypoint)(void) = (void (*)(void))buffer[client_entry];
    entrypoint();
    co_panic(); /* Panic if cothread_t entrypoint returns */
}

cothread_t co_active() {
    return co_active_handle;
}

cothread_t co_derive(void *memory, unsigned int size, void (*entrypoint)(void)) {
    uintptr_t *handle;

    if (!co_swap)
        co_swap = (void (*)(cothread_t, cothread_t))co_swap_function;

    handle = (uintptr_t *)memory;
    // 16-bit align "down" the stack ptr, RISC-V requires this
    unsigned int offset = (size & ~15);
    uintptr_t *p = (uintptr_t *)((unsigned char *)handle + offset);

    handle[ra] = 0; /* crash if cothread return! */
    handle[sp] = (uintptr_t)p;
    handle[fp] = (uintptr_t)p;

    handle[client_entry] = (uintptr_t)entrypoint;
    handle[pc] = (uintptr_t)co_entrypoint;
    handle[canary] = STACK_CANARY;

    return handle;
}

void co_switch(cothread_t handle) {
    uintptr_t *memory = (uintptr_t *)handle;
    if (co_active_buffer[canary] != STACK_CANARY || memory[canary] != STACK_CANARY)
    {
        co_panic();
    }

    cothread_t co_previous_handle = co_active_handle;
    co_swap(co_active_handle = handle, co_previous_handle);
}

#ifdef __cplusplus
}
#endif
