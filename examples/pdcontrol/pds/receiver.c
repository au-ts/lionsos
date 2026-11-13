/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include "pdcontrol/config.h"
#include <pdcontrol/ipc_tracer.h>
#include <pdcontrol/scheduler.h>
#include <microkit.h>
#include <stddef.h>
#include <pdcontrol/pdcontrol.h>
#include <sddf/util/printf.h>

__attribute__((__section__(".pdc_pd_all_config"))) pdc_pd_all_config_t pdc_pd_all_config;
__attribute__((__section__(".pdc_channel_all_config"))) pdc_channel_all_config_t pdc_channel_all_config;
__attribute__((__section__(".pdc_ipc_tracer_config"))) pdc_ipc_tracer_config_t pdc_ipc_tracer_config;

pdc_config_handle_t cfg_all;
pdc_ipc_tracer_handle_t ipc_handle;
pdc_sched_handle_t sched_handle;
void init()
{
    cfg_all = pdc_config_init(&pdc_pd_all_config, &pdc_channel_all_config);
    ipc_handle = pdc_ipc_tracer_init(&cfg_all, &pdc_ipc_tracer_config);
    sched_handle = pdc_sched_init(&cfg_all);
    for (pdc_sched_pd_handle_t **handle = pdc_sched_iterate(&sched_handle, NULL); handle != NULL;
         handle = pdc_sched_iterate(&sched_handle, handle)) {
        pdc_sched_schedule_child(&sched_handle, pdc_sched_choose_child(&sched_handle, handle));
        break;
    }
    seL4_Word badge;
    while (true) {
        microkit_msginfo msg = seL4_Recv(PDC_INPUT_CAP, &badge, PDC_REPLY_CAP);
        microkit_dbg_puts(microkit_name);
        microkit_dbg_puts(" was notified by ");
        microkit_dbg_put32(badge);
        microkit_dbg_puts("\n");
        // figure out better scheduling?
        pdc_sched_unschedule_current(&sched_handle, pdc_sched_pd_state_BlockedOnRecv);
        // Just send some random bullshit.
        for (pdc_sched_pd_handle_t **handle = pdc_sched_iterate(&sched_handle, NULL); handle != NULL;
             handle = pdc_sched_iterate(&sched_handle, handle)) {
            if (pdc_sched_state_is_blocking(handle[0]->state))
                continue;
            pdc_sched_schedule_child(&sched_handle, pdc_sched_choose_child(&sched_handle, handle));
            break;
        }
        if (pdc_sched_get_current(&sched_handle) == NULL)
            assert(!"No schedulable children!");
        seL4_Word ch = pdc_badge_to_channel_id(badge);
        pdc_ipc_tracer_set_target_child(&ipc_handle, pdc_ipc_tracer_get_childid_from_channel(&ipc_handle, ch));
        pdc_ipc_tracer_push_ipc_from_buffer(&ipc_handle, msg, badge, ch);
        microkit_notify(ipc_handle.sender_id);
    }
}

void notified(microkit_channel ch)
{
}

microkit_msginfo protected(microkit_channel ch, microkit_msginfo msginfo)
{
    return microkit_msginfo_new(0, 0);
}
