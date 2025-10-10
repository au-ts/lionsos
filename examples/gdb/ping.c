/*
 * Copyright 2024, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <microkit.h>
#include <stddef.h>

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
		microkit_notify(PINGPONG_CHANNEL);
		break;
	}
}
