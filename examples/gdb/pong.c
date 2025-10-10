/*
 * Copyright 2024, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <microkit.h>

#define PINGPONG_CHANNEL 0

void notified(microkit_channel ch) {
    switch (ch) {
    case PINGPONG_CHANNEL: {
        microkit_dbg_puts("Pong!\n");
        microkit_notify(PINGPONG_CHANNEL);
        break;
    }
    }
}

void init() {
	microkit_dbg_puts("Hi! I'm PONG!\n");
}
