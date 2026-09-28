/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * The program the manager loads into the sandbox at run time. It is linked
 * at PROGRAM_VADDR and uses only memory and a capability that the manager
 * created or granted: it counts in a shared page, writes the count to the
 * surface, and signals the notification the manager put in slot 7 of its
 * CSpace.
 */

#include <stdint.h>
#include <sel4/sel4.h>
#include "layout.h"

void program_main(void);

__attribute__((naked, section(".text.start"))) void _start(void)
{
    __asm__ volatile("ldr x0, =%0\n"
                     "mov sp, x0\n"
                     "b program_main\n" : : "i"(PROGRAM_STACK_TOP));
}

void program_main(void)
{
    volatile uint64_t *counter = (volatile uint64_t *)PROGRAM_SHARED_VADDR;
    volatile uint64_t *surface = (volatile uint64_t *)PROGRAM_SURFACE_VADDR;
    for (;;) {
        for (volatile uint32_t i = 0; i < 2000000; i++) {
        }
        (*counter)++;
        *surface = *counter;
        seL4_Signal(SANDBOX_NTFN_CPTR);
    }
}
