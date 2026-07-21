/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <microkit.h>
#include <sddf/util/printf.h>
#define LOG(...) sddf_printf("RRER | " __VA_ARGS__)

void init()
{
    LOG("Initialised\n");
}
void notified(microkit_channel ch)
{
    LOG("Notified! %d\n", ch);
}
microkit_msginfo protected(microkit_channel ch, microkit_msginfo msginfo)
{
    LOG("Protected!\n");
    return msginfo;
}

