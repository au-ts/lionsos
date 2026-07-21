/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <microkit.h>
#include <sddf/util/printf.h>
#define LOG(...) sddf_printf("PINGER | " __VA_ARGS__)
uintptr_t pongch = 9999;

void init()
{
    LOG("Initialised");
    microkit_notify((microkit_channel)pongch);
}
void notified(microkit_channel ch)
{
    LOG("Notified! %d\n", ch);
    microkit_notify(ch);
}
microkit_msginfo protected(microkit_channel ch, microkit_msginfo msginfo)
{
    LOG("Protected!\n");
}
