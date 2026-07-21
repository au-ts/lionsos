/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <microkit.h>
#include <sddf/util/printf.h>
#include <stddef.h>
#include <stdint.h>
#define LOG(...) sddf_printf("PONGER | " __VA_ARGS__)

void init()
{
}

microkit_channel pingch = 9999;

void notified(microkit_channel ch)
{
    LOG("Notified! %d\n", ch);
    microkit_notify(ch);
}

microkit_msginfo protected(microkit_channel ch, microkit_msginfo msginfo)
{
    LOG("Protected!\n");
    return msginfo;
}
