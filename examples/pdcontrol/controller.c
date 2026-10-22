/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <microkit.h>
#include <stddef.h>
#include <pdcontrol/pdcontrol.h>
#include <sddf/util/printf.h>

__attribute__((__section__(".pdc_pd_all_config"))) pdc_pd_all_config_t config;


void init()
{
    pdc_config_init(&config, NULL);
}
void notified(microkit_channel ch)
{
}
microkit_msginfo protected(microkit_channel ch, microkit_msginfo msginfo)
{
    return microkit_msginfo_new(0,0);
}
