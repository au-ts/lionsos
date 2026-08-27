/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <microkit.h>
#include <sddf/util/printf.h>
#define LOG(...) sddf_printf("PONGER | " __VA_ARGS__)
#define microkit_notify(ch) do {LOG("send\n"); microkit_notify(ch); } while (0)

void init()
{
}

uintptr_t pingch = 9999;

static size_t i = 0;
void notified(microkit_channel ch)
{
    LOG("Notified! %d\n", ch);
    if (ch == pingch && i++ < 10)
        microkit_notify(ch);
    else
        *(volatile int*)NULL; // force a crash to trigger replaying for now.
}

microkit_msginfo protected(microkit_channel ch, microkit_msginfo msginfo)
{
    LOG("Protected!\n");
    return msginfo;
}
