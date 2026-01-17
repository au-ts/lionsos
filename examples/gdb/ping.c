/*
 * Copyright 2025, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <microkit.h>
#include <stddef.h>
#include <stdint.h>

#define PINGPONG_CHANNEL 0

void init() {
	int a = 1;
	int *b = NULL;
	// *b = 10;

	microkit_dbg_puts("Hi! I'm PING!\n");
	a = a + 1;

	microkit_dbg_puts("Ping!\n");
	microkit_notify(PINGPONG_CHANNEL);
}

void notified(microkit_channel ch) {
	switch (ch) {
	case PINGPONG_CHANNEL:
		microkit_dbg_puts("Ping!\n");
        volatile uintptr_t *null_ptr = 0;
        volatile int denull = (volatile int) *null_ptr;
        // For some reason our dereference is getting re-ordered
        asm volatile("isb");
		microkit_notify(PINGPONG_CHANNEL);
		break;
	}
}
