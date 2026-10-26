#include "pdcontrol/config.h"
#include <pdcontrol/ipc_tracer.h>
#include <stddef.h>
#include <assert.h>
#include <string.h>
#include <microkit.h>

pdc_ipc_obj_queue_t *pdc_ipc_obj_queue_init(uint8_t *memory)
{
    // zero out the data.
    for (int i = 0; i < sizeof(pdc_ipc_obj_queue_t); i++) {
        memory[i] = 0;
    }
    return (pdc_ipc_obj_queue_t *)(memory);
}

seL4_Word pdc_ipc_obj_handler_len(pdc_ipc_obj_handler_t *handler)
{
    int64_t len = handler->head - handler->tail;
    // the head is wrapped
    if (len < 0) {
        len += PDC_IPC_WORD_STORAGE_WORDS;
    }
    return len;
}

seL4_Word pdc_ipc_obj_queue_len(pdc_ipc_obj_queue_t *q)
{
    int64_t len = q->head - q->tail;
    // the head is wrapped
    if (len < 0) {
        len += PDC_IPC_WORD_STORAGE_WORDS;
    }
    return len;
}

pdc_ipc_obj_t pdc_ipc_obj_handler_copy_msg(pdc_ipc_obj_handler_t *handler, seL4_MessageInfo_t msg, seL4_Word badge,
                                           seL4_Word target_ch)
{
    seL4_Word len = seL4_MessageInfo_get_length(msg);
    // assert that we have some space left.
    assert(len + pdc_ipc_obj_handler_len(handler) < PDC_IPC_WORD_STORAGE_WORDS - 1);

    seL4_Word begin = handler->head;
    for (int i = 0; i < len; i++) {
        handler->data[handler->head] = seL4_GetMR(i);
        handler->head += 1;
        handler->head %= PDC_IPC_WORD_STORAGE_WORDS;
    }
    return (pdc_ipc_obj_t) { .handler_index = begin, .badge = badge, .msginfo = msg, .channel = target_ch };
}

seL4_Word pdc_ipc_handler_get_mr(pdc_ipc_obj_handler_t *handler, pdc_ipc_obj_t *ipc, seL4_Word i)
{
    seL4_Word ind = (ipc->handler_index + i) % PDC_IPC_WORD_STORAGE_WORDS;
    return handler->data[ind];
}

void pdc_ipc_obj_handler_free(pdc_ipc_obj_handler_t *handler, pdc_ipc_obj_t *ipc)
{
    seL4_Word len = seL4_MessageInfo_get_length(ipc->msginfo);
    // assert that we are dequeuing less than what we have.
    assert(len <= pdc_ipc_obj_handler_len(handler));

    handler->tail += len;
    handler->tail %= PDC_IPC_WORD_STORAGE_WORDS;
}

void pdc_ipc_obj_queue_push(pdc_ipc_obj_queue_t *q, seL4_MessageInfo_t msg, seL4_Word badge, seL4_Word target_ch)
{
    pdc_ipc_obj_t val = pdc_ipc_obj_handler_copy_msg(&q->handler, msg, badge, target_ch);

    assert(pdc_ipc_obj_queue_len(q) < PDC_QUEUE_MAX_LEN - 1);
    q->data[q->head] = val;
    q->head += 1;
    q->head %= PDC_QUEUE_MAX_LEN;
}

pdc_ipc_obj_t pdc_ipc_obj_queue_peek(pdc_ipc_obj_queue_t *q)
{
    assert(pdc_ipc_obj_queue_len(q) > 0);

    pdc_ipc_obj_t val = q->data[q->tail];
    return val;
}

seL4_MessageInfo_t pdc_ipc_obj_handler_read_msg(pdc_ipc_obj_handler_t *handler, pdc_ipc_obj_t ipc)
{
    seL4_Word len = seL4_MessageInfo_get_length(ipc.msginfo);
    seL4_Word label = seL4_MessageInfo_get_label(ipc.msginfo);

    seL4_MessageInfo_t msg = seL4_MessageInfo_new(0, 0, 0, len);
    msg = seL4_MessageInfo_set_label(msg, label);

    for (int i = 0; i < len; i++) {
        seL4_SetMR(i, pdc_ipc_handler_get_mr(handler, &ipc, i));
    }

    // they should be the same, if there are no caps being sent?
    assert(msg.words[0] == ipc.msginfo.words[0]);
    return msg;
}

seL4_Word pdc_ipc_obj_queue_peek_badge(pdc_ipc_obj_queue_t *q)
{
    assert(pdc_ipc_obj_queue_len(q) > 0);
    return q->data[q->tail].badge;
}

seL4_Word pdc_ipc_obj_queue_peek_channel(pdc_ipc_obj_queue_t *q)
{
    assert(pdc_ipc_obj_queue_len(q) > 0);
    return q->data[q->tail].channel;
}

void pdc_ipc_obj_queue_pop_ignore(pdc_ipc_obj_queue_t *q)
{
    assert(pdc_ipc_obj_queue_len(q) > 0);
    // free the message
    pdc_ipc_obj_t val = q->data[q->tail];
    pdc_ipc_obj_handler_free(&q->handler, &val);
    q->tail += 1;
    q->tail %= PDC_QUEUE_MAX_LEN;
}

seL4_Word pdc_ipc_source_ch_to_target_ch(seL4_Word ch)
{
    // just hardcoded.
    if (ch % 2 == 1)
        return ch - 1;
    return ch + 1;
}

// ONLY CALL on the RECEIVER.
pdc_ipc_tracer_handle_t pdc_ipc_tracer_config_init_receiver(const pdc_config_handle_t *all_cfg,
                                                            const pdc_ipc_tracer_config_t *ipc_cfg)
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
        pdc_ipc_obj_queue_init(ipc_cfg->queue_storage);
    }

    return (pdc_ipc_tracer_handle_t) {
        .all_cfg = all_cfg,
        .ipc_cfg = ipc_cfg,
        .sender_ch = ipc_cfg->sender_channel_id,
        .sender_id = ipc_cfg->sender_id,
        .per_thread_queue = pt_recv_queue,
    };
}

pdc_ipctype_e pdc_ipc_obj_get_type(const pdc_ipc_tracer_handle_t *cfg, seL4_MessageInfo_t msg, seL4_Word badge)
{
    seL4_Word is_endpoint = badge >> PDC_BADGE_ENDPOINT_BIT;
    seL4_Word is_fault = (badge >> PDC_BADGE_FAULT_BIT) & 1;

    if (is_endpoint) {
        if ((badge & PDC_PD_MASK) == cfg->sender_ch) {
            // We received a reply
            return pdc_ipctype_SenderReply;
        } else { // or not.
            return pdc_ipctype_Call;
        }
    };
    if (is_fault)
        return pdc_ipctype_Fault;

    seL4_Word idx = pdc_badge_to_channel_id(badge);
#ifdef PDC_HAS_BLOCKCHECKER
    if (idx == blocker_ch) {
        return pdc_ipctype_BlockChecker;
    }
#endif
    return pdc_ipctype_Ntfn;
}

seL4_Word pdc_ipc_obj_get_child(seL4_Word badge)
{
    // This might only be valid for messages?
    return badge & PDC_PD_MASK;
}

// setup the ipc sender. The child starts from 0, using microkit_child-like semantics.
// There might be a race case... not sure tho.
void pdc_ipc_obj_sender_setup(pdc_ipc_tracer_handle_t* cfg, seL4_Word child)
{
    *cfg->ipc_target_child_id = child;

    // notify the sender.
    seL4_Signal(BASE_OUTPUT_NOTIFICATION_CAP + cfg->sender_ch);
}

// store the curretly stored message in the ipc buffer into the recv queue of that
// child.
void pdc_ipc_obj_store_ipc_msg(pdc_ipc_tracer_handle_t* cfg, seL4_Word target_child, seL4_MessageInfo_t msg, seL4_Word badge, seL4_Word source_ch)
{
    assert(target_child < cfg->all_cfg->pd_config->num_clients);
    pdc_ipc_obj_queue_push(&cfg->per_thread_queue[target_child], msg, badge, source_ch);
}

// check the size of the child's ipc queue.
seL4_Word pdc_ipc_obj_child_queue_len(pdc_ipc_tracer_handle_t* cfg, seL4_Word target_child)
{
    assert(target_child < cfg->all_cfg->pd_config->num_clients);
    return pdc_ipc_obj_queue_len(&cfg->per_thread_queue[target_child]);
}

// Get the badge of the earliest sender.
seL4_Word pdc_ipc_obj_child_queue_peek_badge(pdc_ipc_tracer_handle_t* cfg, seL4_Word target_child)
{
    assert(target_child < cfg->all_cfg->pd_config->num_clients);
    return pdc_ipc_obj_queue_peek_badge(&cfg->per_thread_queue[target_child]);
}

// Get the badge of the channel sender.
seL4_Word pdc_ipc_obj_child_queue_peek_channel(pdc_ipc_tracer_handle_t* cfg, seL4_Word target_child)
{
    assert(target_child < cfg->all_cfg->pd_config->num_clients);
    return pdc_ipc_obj_queue_peek_channel(&cfg->per_thread_queue[target_child]);
}

