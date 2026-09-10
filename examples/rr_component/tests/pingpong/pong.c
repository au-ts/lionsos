/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include "generic.h"
#define microkit_notify(ch) do {LOG("send\n"); microkit_notify(ch); } while (0)

void init()
{
    LOG("Init!\n");
}

uintptr_t pingch = 9999;

static size_t i = 0;
void notified(microkit_channel ch)
{
    LOG("Notified! %d %lu\n", ch, i);
    if (ch == pingch && i++ < 10)
        microkit_notify(ch);
    else {
        LOG("CRASHING!\n");
        *(volatile int *)NULL; // force a crash to trigger replaying for now.
    }
}

microkit_msginfo protected(microkit_channel ch, microkit_msginfo msginfo)
{
    LOG("Protected!\n");
    return msginfo;
}
