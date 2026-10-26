/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include "pdcontrol/config.h"
#include "pdcontrol/ipc_tracer.h"
#include <microkit.h>
#include <stddef.h>
#include <pdcontrol/pdcontrol.h>
#include <sddf/util/printf.h>

__attribute__((__section__(".pdc_pd_all_config"))) pdc_pd_all_config_t pdc_pd_all_config;
__attribute__((__section__(".pdc_channel_all_config"))) pdc_channel_all_config_t pdc_channel_all_config;
__attribute__((__section__(".pdc_ipc_tracer_config"))) pdc_ipc_tracer_config_t pdc_ipc_tracer_config;

pdc_config_handle_t cfg_all;
pdc_ipc_tracer_handle_t ipc_handle;
void init()
{
    cfg_all = pdc_config_init(&pdc_pd_all_config, &pdc_channel_all_config);
    ipc_handle = pdc_ipc_tracer_config_init_receiver(&cfg_all, &pdc_ipc_tracer_config) ;
}

void notified(microkit_channel ch)
{
}

microkit_msginfo protected(microkit_channel ch, microkit_msginfo msginfo)
{
    return microkit_msginfo_new(0,0);
}
