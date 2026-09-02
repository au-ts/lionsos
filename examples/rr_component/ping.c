/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <microkit.h>
#include <sddf/util/printf.h>
#define LOG(...) do {sddf_printf("PING [%s]| ", __func__); sddf_printf(__VA_ARGS__);} while (0)
#define microkit_notify(ch) do {LOG("send\n"); microkit_notify(ch); } while (0)
uintptr_t pongch = 9999;

void init()
{
    LOG("Init!\n");
    microkit_notify((microkit_channel)pongch);
}
static size_t i = 0;
void notified(microkit_channel ch)
{
    LOG("Notified! %d\n", ch);
    if (i++ < 10)
        microkit_notify(ch);
}
microkit_msginfo protected(microkit_channel ch, microkit_msginfo msginfo)
{
    LOG("Protected!\n");
    return msginfo;
}
