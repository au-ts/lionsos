#include "pdcontrol/scheduler.h"
#include "interfaces/sel4_client.h"
#include "pdcontrol/helpers.h"
#include <sddf/util/printf.h>
#include <microkit.h>

pdc_sched_handle_t pdc_sched_init(pdc_config_handle_t *cfg)
{
    pdc_sched_handle_t handle = {
        .cfg = cfg,
        .currently_sched = NULL,
        .sched_next = false,
        .sched_queue = {},
        .sched_state_storage = {},
    };

    // We only suspend children who are correspondingly in a suspended state, or who
    // are schedulable but not running
    // Since everyone starts schedulable, everyone is suspended
    // This ensures that those who are being replied to will receive the reply non-blocking.
    for (int i = 0; i < cfg->pd_config->num_clients; i++) {
        handle.sched_state_storage[i] = (pdc_sched_pd_handle_t) { .cfg = &cfg->pd_config->configs[i],
                                                                  .state = pdc_sched_pd_state_Schedulable };
        handle.sched_queue[i] = &handle.sched_state_storage[i];
        NO_ERR(seL4_TCB_Suspend(cfg->pd_config->configs[i].id + BASE_TCB_CAP));
    }

    // Sort the schedule queue.
    // funny bubble sort
    for (int top = cfg->pd_config->num_clients; top >= 0; top--) {
        for (int i = 0; i < top - 1; i++) {
            if (handle.sched_queue[i]->cfg->def_prio < handle.sched_queue[i + 1]->cfg->def_prio) {
                pdc_sched_pd_handle_t *temp = handle.sched_queue[i];
                handle.sched_queue[i] = handle.sched_queue[i + 1];
                handle.sched_queue[i + 1] = temp;
            }
        }
    }
    for (int i = 0; i < cfg->pd_config->num_clients; i++) {
        sddf_printf("id: %lu, priority: %lu, sched_state: %lu\n", handle.sched_queue[i]->cfg->id,
                    handle.sched_queue[i]->cfg->def_prio, (seL4_Word)handle.sched_queue[i]->state);
    }
    return handle;
}

pdc_sched_pd_handle_t **pdc_sched_iterate(pdc_sched_handle_t *handle, pdc_sched_pd_handle_t **cur)
{
    if (cur == NULL) {
        return handle->sched_queue;
    } else if (cur + 1 >= handle->sched_queue + handle->cfg->pd_config->num_clients)
        return NULL;
    return cur + 1;
}

pdc_sched_pd_handle_t *pdc_sched_choose_child(pdc_sched_handle_t *handle, pdc_sched_pd_handle_t **cur)
{
    assert(cur != NULL);
    assert(cur >= handle->sched_queue);
    assert(cur < handle->sched_queue + handle->cfg->pd_config->num_clients);

    // swap the position of this value continuously until it encounters
    // a child with less priority or the end of the queue.
    while (cur + 1 < handle->sched_queue + handle->cfg->pd_config->num_clients
           && cur[0]->cfg->def_prio == cur[1]->cfg->def_prio) {
        pdc_sched_pd_handle_t *temp = cur[0];
        cur[0] = cur[1];
        cur[1] = temp;
        cur++;
    }
    return *cur;
}

pdc_sched_pd_handle_t *pdc_sched_schedule_child(pdc_sched_handle_t *handle, pdc_sched_pd_handle_t *child)
{
    // Assert that we have nothing scheduled.
    // Do this by pdc_sched_unschedule_current.
    assert(handle->currently_sched == NULL);

    // sets up the correct priority of the child.
    handle->currently_sched = child;

    // set the correct state.
    handle->currently_sched->state = pdc_sched_pd_state_Scheduled;
    seL4_TCB_SetPriority(TCB(child->cfg->id), SELF_TCB(), PDC_SCHED_PRIO);
    seL4_TCB_Resume(TCB(child->cfg->id));

    return handle->currently_sched;
}

pdc_sched_pd_handle_t *pdc_sched_unschedule_current(pdc_sched_handle_t *handle, pdc_sched_pd_state_e state)
{
    assert(handle->currently_sched != NULL && state != pdc_sched_pd_state_Scheduled);
    seL4_TCB_SetPriority(TCB(handle->currently_sched->cfg->id), SELF_TCB(), handle->currently_sched->cfg->def_prio);
    // if we are changing the state to suspended, then we will suspend the thread as well.
    // or schedulable (as suspending it won't prevent it from receiving stuff).
    if (state == pdc_sched_pd_state_Suspended)
        NO_ERR(seL4_TCB_Suspend(handle->currently_sched->cfg->id + BASE_TCB_CAP));
    if (state == pdc_sched_pd_state_Schedulable)
        NO_ERR(seL4_TCB_Suspend(handle->currently_sched->cfg->id + BASE_TCB_CAP));
    handle->currently_sched->state = state;
    pdc_sched_pd_handle_t *temp = handle->currently_sched;
    handle->currently_sched = NULL;
    handle->last_sched = temp;
    return temp;
}

bool pdc_sched_state_is_blocking(pdc_sched_pd_state_e state) {
    switch (state) {
    case pdc_sched_pd_state_Schedulable:
        return false;
    case pdc_sched_pd_state_Scheduled:
    case pdc_sched_pd_state_BlockedOnSend:
    case pdc_sched_pd_state_BlockedOnRecv:
    case pdc_sched_pd_state_Suspended:
    case pdc_sched_pd_state_BlockedOnCall:
    case pdc_sched_pd_state_BlockedOnReply:
        return true;
    case _pdc_sched_pd_state_: // force to be seL4_Word size
        assert(!"Invalid pdc_sched_state");
    }
}
