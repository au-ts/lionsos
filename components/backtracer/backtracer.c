/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include "monitor.h"
#include <microkit.h>
#include <sddf/util/printf.h>

void (**backtraceFunctions)() = NULL;

void init()
{
    LOG("Backtracer initialised!\n");
    LOG("Backtracer table starting address: %p\n", backtraceFunctions);
}

#if defined(__aarch64__)
static void callConvention_prologue(seL4_UserContext *ctxt, uintptr_t funcAddr)
{
  // Set the link register to old PC
    ctxt->x30 = ctxt->pc;
  // Set the PC to the next function
    ctxt->pc = funcAddr;
    LOG("Old PC: %p, New PC: %p\n", (void *)ctxt->x30, (void *)funcAddr);
}
#elif defined(__riscv__)
#error "Unimplemented backtracer for riscv"
#elif defined(__x86_64__)
#error "Unimplemented backtracer for x86_64"
#else
#error "Unsupported architecture for backtracing"
#endif

seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo)
{
    LOG("Child '%d' Faulted!\n", child);
    print_fault_error(child, msginfo);
    seL4_UserContext ctxt = { 0 };
    int readRegResult = seL4_TCB_ReadRegisters(BASE_TCB_CAP + child, seL4_True, 0,
                                               sizeof(seL4_UserContext) / sizeof(seL4_Word), &ctxt);
    if (readRegResult != 0) {
        LOG("Failed to read registers for setting up backtrace jump! Got %d, "
            "expected %d\n",
            readRegResult, 0);
        return seL4_False;
    }
    print_tcb_registers(&ctxt);
    callConvention_prologue(&ctxt, (uintptr_t)(backtraceFunctions[child]));
    int writeRegResult = seL4_TCB_WriteRegisters(BASE_TCB_CAP + child, seL4_True, 0,
                                                 sizeof(seL4_UserContext) / sizeof(seL4_Word), &ctxt);
    if (writeRegResult != 0) {
        LOG("Failed to write registers for setting up backtrace jump! Got error value %d, "
            "expected %d\n",
            writeRegResult, 0);
        return seL4_False;
    }
    return seL4_True;
}

void notified(microkit_channel ch)
{
}
microkit_msginfo protected(microkit_channel ch, microkit_msginfo msginfo)
{
    microkit_pd_stop(ch);
    return msginfo;
}
