#include "pdcontrol/config.h"
#include "sel4/shared_types_gen.h"
#include <pdcontrol/ipc_tracer.h>
#include <stddef.h>
#include <assert.h>
#include <string.h>
#include <microkit.h>

static inline pdc_ipc_obj_queue_t *_get_queue_of_current(pdc_ipc_tracer_handle_t *handle)
{
    return &handle->per_thread_queue[*handle->ipc_target_child_id];
}

pdc_ipc_tracer_handle_t pdc_ipc_tracer_init(const pdc_config_handle_t *all_cfg, const pdc_ipc_tracer_config_t *ipc_cfg)
{
    assert(all_cfg != NULL);
    assert(ipc_cfg != NULL);
    assert(strcmp(ipc_cfg->magic, PDC_IPC_CONFIG_MAGIC) == 0);
    // just do naively for now.
    // reserve the first 8 bytes for the currently scheduled child.
    seL4_Word *current_sched_child_id = (seL4_Word *)(ipc_cfg->queue_storage);
    *current_sched_child_id = PDC_NO_THREAD_SCHEDULED;
    pdc_ipc_obj_queue_t *pt_recv_queue = (pdc_ipc_obj_queue_t *)(((uint8_t *)ipc_cfg->queue_storage)
                                                                 + sizeof(seL4_Word));
    for (int i = 0; i < all_cfg->chan_config->num_channels; i++) {
        pdc_internal_ipc_obj_queue_init(ipc_cfg->queue_storage);
    }

    return (pdc_ipc_tracer_handle_t) {
        .all_cfg = all_cfg,
        .ipc_cfg = ipc_cfg,
        .sender_ch = ipc_cfg->sender_channel_id,
        .sender_id = ipc_cfg->sender_id,
        .per_thread_queue = pt_recv_queue,
        .ipc_target_child_id = current_sched_child_id,
    };
}

void pdc_ipc_tracer_set_target_child(pdc_ipc_tracer_handle_t *handle, seL4_Word child)
{
    assert(child < handle->all_cfg->pd_config->num_clients);
    *handle->ipc_target_child_id = child;
}

// store the curretly stored message in the ipc buffer into the recv queue of that
// child.
void pdc_ipc_tracer_push_ipc_from_buffer(pdc_ipc_tracer_handle_t *handle, seL4_MessageInfo_t msg, seL4_Word badge,
                                         seL4_Word source_ch)
{
    assert(*handle->ipc_target_child_id < handle->all_cfg->pd_config->num_clients);
    pdc_internal_ipc_obj_queue_push(&handle->per_thread_queue[*handle->ipc_target_child_id], msg, badge, source_ch);
}

// TODO: Implement if I want one that doesn't prefire through the ipc buffer.
// void pdc_ipc_tracer_push_ipc(pdc_ipc_tracer_handle_t *handle)
// {
// }

// NOTE: Consider using handles instead of child ids?
// not sure, spamming handles everywhere already.
seL4_Word pdc_ipc_tracer_get_target_child(pdc_ipc_tracer_handle_t *handle)
{
    return *handle->ipc_target_child_id;
}

bool pdc_ipc_tracer_can_peek_ipc(pdc_ipc_tracer_handle_t *handle)
{
    pdc_ipc_obj_queue_t *q = &handle->per_thread_queue[*handle->ipc_target_child_id];
    return pdc_internal_ipc_obj_queue_len(q);
}

pdc_ipc_obj_t pdc_ipc_tracer_peek_ipc(pdc_ipc_tracer_handle_t *handle)
{
    return pdc_internal_ipc_obj_queue_peek(_get_queue_of_current(handle));
}

// sends the ipc
// also blocks.
void pdc_ipc_do_ipc(pdc_ipc_tracer_handle_t *handle, pdc_ipc_obj_t *ipc)
{
    seL4_Word target_ch = pdc_internal_ipc_source_ch_to_target_ch(ipc->channel);
    pdc_ipc_obj_queue_t *queue = _get_queue_of_current(handle);
    // sets up the registers
    seL4_Word len = seL4_MessageInfo_get_length(ipc->msginfo);
    for (int i = 0; i < len; i++) {
        seL4_SetMR(i, pdc_internal_ipc_handler_get_mr(queue, ipc, i));
    }
    // check if it's a notification or a badge.
    switch (pdc_badge_to_ipctype(ipc->badge)) {
    case pdc_ipctype_SenderReply: {
    } break;
    case pdc_ipctype_Fault: {
    } break;
    case pdc_ipctype_Send: {
    } break;
    case pdc_ipctype_BlockChecker: {
    } break;
    case pdc_ipctype_Call: {
        seL4_Send(BASE_ENDPOINT_CAP + target_ch, ipc->msginfo);
    } break;
    case pdc_ipctype_Ntfn: {
        seL4_Send(BASE_OUTPUT_NOTIFICATION_CAP + target_ch, ipc->msginfo);
    } break;
    }
}

// WARNING: This invalidates any pdc_ipc_obj_t given from peeking.
void pdc_ipc_tracer_consume_ipc(pdc_ipc_tracer_handle_t *handle)
{
    return pdc_internal_ipc_obj_queue_pop_ignore(_get_queue_of_current(handle));
}

seL4_Word pdc_ipc_tracer_get_childid_from_channel(pdc_ipc_tracer_handle_t *handle, seL4_Word channel)
{
    return handle->ipc_cfg->channel_id_to_source_child_id[channel];
}

/***************************/
/**** internal stuff ;) ****/
/***************************/

pdc_ipc_obj_queue_t *pdc_internal_ipc_obj_queue_init(uint8_t *memory)
{
    // zero out the data.
    for (int i = 0; i < sizeof(pdc_ipc_obj_queue_t); i++) {
        memory[i] = 0;
    }
    return (pdc_ipc_obj_queue_t *)(memory);
}

seL4_Word pdc_internal_ipc_obj_handler_len(pdc_ipc_obj_handler_t *handler)
{
    int64_t len = handler->head - handler->tail;
    // the head is wrapped
    if (len < 0) {
        len += PDC_IPC_WORD_STORAGE_WORDS;
    }
    return len;
}

seL4_Word pdc_internal_ipc_obj_queue_len(pdc_ipc_obj_queue_t *q)
{
    int64_t len = q->head - q->tail;
    // the head is wrapped
    if (len < 0) {
        len += PDC_IPC_WORD_STORAGE_WORDS;
    }
    return len;
}

pdc_ipc_obj_t pdc_internal_ipc_obj_handler_copy_msg(pdc_ipc_obj_handler_t *handler, seL4_MessageInfo_t msg,
                                                    seL4_Word badge, seL4_Word target_ch)
{
    seL4_Word len = seL4_MessageInfo_get_length(msg);
    // assert that we have some space left.
    assert(len + pdc_internal_ipc_obj_handler_len(handler) < PDC_IPC_WORD_STORAGE_WORDS - 1);

    seL4_Word begin = handler->head;
    for (int i = 0; i < len; i++) {
        handler->data[handler->head] = seL4_GetMR(i);
        handler->head += 1;
        handler->head %= PDC_IPC_WORD_STORAGE_WORDS;
    }
    return (pdc_ipc_obj_t) { .handler_index = begin, .badge = badge, .msginfo = msg, .channel = target_ch };
}

seL4_Word pdc_internal_ipc_handler_get_mr(pdc_ipc_obj_queue_t *queue, pdc_ipc_obj_t *ipc, seL4_Word i)
{
    pdc_ipc_obj_handler_t *handler = &queue->handler;
    seL4_Word ind = (ipc->handler_index + i) % PDC_IPC_WORD_STORAGE_WORDS;
    return handler->data[ind];
}

void pdc_internal_ipc_obj_handler_free(pdc_ipc_obj_handler_t *handler, pdc_ipc_obj_t *ipc)
{
    seL4_Word len = seL4_MessageInfo_get_length(ipc->msginfo);
    // assert that we are dequeuing less than what we have.
    assert(len <= pdc_internal_ipc_obj_handler_len(handler));

    handler->tail += len;
    handler->tail %= PDC_IPC_WORD_STORAGE_WORDS;
}

void pdc_internal_ipc_obj_queue_push(pdc_ipc_obj_queue_t *q, seL4_MessageInfo_t msg, seL4_Word badge,
                                     seL4_Word target_ch)
{
    pdc_ipc_obj_t val = pdc_internal_ipc_obj_handler_copy_msg(&q->handler, msg, badge, target_ch);

    assert(pdc_internal_ipc_obj_queue_len(q) < PDC_QUEUE_MAX_LEN - 1);
    q->data[q->head] = val;
    q->head += 1;
    q->head %= PDC_QUEUE_MAX_LEN;
}

pdc_ipc_obj_t pdc_internal_ipc_obj_queue_peek(pdc_ipc_obj_queue_t *q)
{
    assert(pdc_internal_ipc_obj_queue_len(q) > 0);

    pdc_ipc_obj_t val = q->data[q->tail];
    return val;
}

seL4_Word pdc_internal_ipc_obj_queue_peek_badge(pdc_ipc_obj_queue_t *q)
{
    assert(pdc_internal_ipc_obj_queue_len(q) > 0);
    return q->data[q->tail].badge;
}

seL4_Word pdc_internal_ipc_obj_queue_peek_channel(pdc_ipc_obj_queue_t *q)
{
    assert(pdc_internal_ipc_obj_queue_len(q) > 0);
    return q->data[q->tail].channel;
}

void pdc_internal_ipc_obj_queue_pop_ignore(pdc_ipc_obj_queue_t *q)
{
    assert(pdc_internal_ipc_obj_queue_len(q) > 0);
    // free the message
    pdc_ipc_obj_t val = q->data[q->tail];
    pdc_internal_ipc_obj_handler_free(&q->handler, &val);
    q->tail += 1;
    q->tail %= PDC_QUEUE_MAX_LEN;
}

seL4_Word pdc_internal_ipc_source_ch_to_target_ch(seL4_Word ch)
{
    // just hardcoded.
    if (ch % 2 == 1)
        return ch - 1;
    return ch + 1;
}

// setup the ipc sender. The child starts from 0, using microkit_child-like semantics.
// There might be a race case... not sure tho.
void pdc_internal_ipc_obj_sender_setup(pdc_ipc_tracer_handle_t *cfg, seL4_Word child)
{
    *cfg->ipc_target_child_id = child;

    // notify the sender.
    seL4_Signal(BASE_OUTPUT_NOTIFICATION_CAP + cfg->sender_ch);
}

void pdc_internal_ipc_obj_store_ipc_msg(pdc_ipc_tracer_handle_t *cfg, seL4_Word target_child, seL4_MessageInfo_t msg,
                                        seL4_Word badge, seL4_Word source_ch)
{
    assert(target_child < cfg->all_cfg->pd_config->num_clients);
    pdc_internal_ipc_obj_queue_push(&cfg->per_thread_queue[target_child], msg, badge, source_ch);
}

// check the size of the child's ipc queue.
seL4_Word pdc_internal_ipc_obj_child_queue_len(pdc_ipc_tracer_handle_t *cfg, seL4_Word target_child)
{
    assert(target_child < cfg->all_cfg->pd_config->num_clients);
    return pdc_internal_ipc_obj_queue_len(&cfg->per_thread_queue[target_child]);
}

// Get the badge of the earliest sender.
seL4_Word pdc_internal_ipc_obj_child_queue_peek_badge(pdc_ipc_tracer_handle_t *cfg, seL4_Word target_child)
{
    assert(target_child < cfg->all_cfg->pd_config->num_clients);
    return pdc_internal_ipc_obj_queue_peek_badge(&cfg->per_thread_queue[target_child]);
}

// Get the badge of the channel sender.
seL4_Word pdc_internal_ipc_obj_child_queue_peek_channel(pdc_ipc_tracer_handle_t *cfg, seL4_Word target_child)
{
    assert(target_child < cfg->all_cfg->pd_config->num_clients);
    return pdc_internal_ipc_obj_queue_peek_channel(&cfg->per_thread_queue[target_child]);
}

void pdc_internal_ipc_tracer_store(pdc_ipc_tracer_handle_t *handle, seL4_Word target_child, seL4_MessageInfo_t msg,
                                   seL4_Word badge, seL4_Word source_ch)
{
    assert(target_child < handle->all_cfg->pd_config->num_clients);
    pdc_internal_ipc_obj_queue_push(&handle->per_thread_queue[target_child], msg, badge, source_ch);
}

seL4_Word pdc_internal_ipc_child_queue_len(pdc_ipc_tracer_handle_t *handle, seL4_Word target_child)
{
    assert(target_child < handle->all_cfg->pd_config->num_clients);
    return pdc_internal_ipc_obj_queue_len(&handle->per_thread_queue[target_child]);
}

seL4_Word pdc_internal_ipc_child_queue_peek_badge(pdc_ipc_tracer_handle_t *handle, seL4_Word target_child)
{
    assert(target_child < handle->all_cfg->pd_config->num_clients);
    return pdc_internal_ipc_obj_queue_peek_badge(&handle->per_thread_queue[target_child]);
}

seL4_Word pdc_internal_ipc_child_queue_peek_channel(pdc_ipc_tracer_handle_t *handle, seL4_Word target_child)
{
    assert(target_child < handle->all_cfg->pd_config->num_clients);
    return pdc_internal_ipc_obj_queue_peek_channel(&handle->per_thread_queue[target_child]);
}
