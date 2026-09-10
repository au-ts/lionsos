#pragma once

#include "sel4/functions.h"
#include "sel4/shared_types_gen.h"
#include "sel4/simple_types.h"
#include <assert.h>
#include <microkit.h>
#include <stdint.h>
#include <stdbool.h>
// Storage in number
#define IPC_WORD_STORAGE_SIZE (0x800)
#define IPC_WORD_STORAGE_WORDS (IPC_WORD_STORAGE_SIZE / sizeof(seL4_Word))
#define QUEUE_MAX_LEN 10
#define NO_THREAD_SCHEDULED ((seL4_Word)(-1))
#define UNSET_VALUE ((seL4_Word)(-1))

#define BADGE_FAULT_BIT 62
#define BADGE_ENDPOINT_BIT 63

// #define LOG(...) do {sddf_printf("%s [%s]| ", microkit_name, __func__); sddf_printf(__VA_ARGS__);} while (0)
#define ERR(...) do {sddf_printf("%s [%s] ERROR| ", microkit_name, __func__); sddf_printf(__VA_ARGS__);} while (0)
#define WARN(...) do {sddf_printf("%s [%s] WARN| ", microkit_name, __func__); sddf_printf(__VA_ARGS__);} while (0)
#define INFO(...) do {sddf_printf("%s [%s] INFO| ", microkit_name, __func__); sddf_printf(__VA_ARGS__);} while (0)
#define LOG(...)

// We store IPC as the following:
// - Each child get's it's own IPC buffer (array ring/queue), in which we store message registers.
// - Each child get's it's own IPC queue, in which we store the following:
//   - handler_index (a reference to where the message registers are stored)
//   - badge (the badge which the IPC was sent with)
//   - msginfo (the message info that was sent)
//   - channel (the source channel which can be converted into the target one)
// - The ipc buffer ring queue is guaraneed to empty in the order in which the IPC events occur,
//   so we won't have issues with sparsity.
typedef struct rrer_ipc {
    // index to the message registers inside the handler.
    // The handler holds the message registers in a "queue"
    seL4_Word handler_index;
    seL4_Word badge;
    seL4_MessageInfo_t msginfo;
    seL4_Word channel;
} rrer_ipc_t;

// holds all the message registers.
// is technically a ring allocator.
// Full when head == tail - 1
typedef struct rrer_ipc_handler {
    seL4_Word head;
    seL4_Word tail;
    seL4_Word data[IPC_WORD_STORAGE_WORDS];
} rrer_ipc_handler_t;

// queue holds an ipc.
typedef struct rrer_queue {
    rrer_ipc_handler_t handler;
    seL4_Word head;
    seL4_Word tail;
    rrer_ipc_t data[QUEUE_MAX_LEN];
} rrer_queue_t;

static rrer_queue_t *rrer_queue_init(uint8_t *memory)
{
    // zero out the data.
    for (int i = 0; i < sizeof(rrer_queue_t); i++) {
        memory[i] = 0;
    }
    return (rrer_queue_t *)(memory);
}

static seL4_Word rrer_ipc_handler_len(rrer_ipc_handler_t *handler)
{
    int64_t len = handler->head - handler->tail;
    // the head is wrapped
    if (len < 0) {
        len += IPC_WORD_STORAGE_WORDS;
    }
    return len;
}

static seL4_Word rrer_queue_len(rrer_queue_t *q)
{
    int64_t len = q->head - q->tail;
    // the head is wrapped
    if (len < 0) {
        len += IPC_WORD_STORAGE_WORDS;
    }
    return len;
}

static rrer_ipc_t rrer_ipc_handler_copy_msg(rrer_ipc_handler_t *handler, seL4_MessageInfo_t msg, seL4_Word badge,
                                            seL4_Word target_ch)
{
    seL4_Word len = seL4_MessageInfo_get_length(msg);
    // assert that we have some space left.
    assert(len + rrer_ipc_handler_len(handler) < IPC_WORD_STORAGE_WORDS - 1);

    seL4_Word begin = handler->head;
    for (int i = 0; i < len; i++) {
        handler->data[handler->head] = seL4_GetMR(i);
        handler->head += 1;
        handler->head %= IPC_WORD_STORAGE_WORDS;
    }
    return (rrer_ipc_t) { .handler_index = begin, .badge = badge, .msginfo = msg, .channel = target_ch };
}

static seL4_Word rrer_ipc_handler_get_mr(rrer_ipc_handler_t *handler, rrer_ipc_t *ipc, seL4_Word i)
{
    seL4_Word ind = (ipc->handler_index + i) % IPC_WORD_STORAGE_WORDS;
    return handler->data[ind];
}

static void rrer_ipc_handler_free(rrer_ipc_handler_t *handler, rrer_ipc_t *ipc)
{
    seL4_Word len = seL4_MessageInfo_get_length(ipc->msginfo);
    // assert that we are dequeuing more than what we have.
    assert(len <= rrer_ipc_handler_len(handler));

    handler->tail += len;
    handler->tail %= IPC_WORD_STORAGE_WORDS;
}

static void rrer_queue_push(rrer_queue_t *q, seL4_MessageInfo_t msg, seL4_Word badge, seL4_Word target_ch)
{
    rrer_ipc_t val = rrer_ipc_handler_copy_msg(&q->handler, msg, badge, target_ch);

    assert(rrer_queue_len(q) < QUEUE_MAX_LEN - 1);
    q->data[q->head] = val;
    q->head += 1;
    q->head %= QUEUE_MAX_LEN;
}

// Copies the queue contents into the IPC buffer.
static rrer_ipc_t rrer_queue_peek(rrer_queue_t *q)
{
    assert(rrer_queue_len(q) > 0);

    rrer_ipc_t val = q->data[q->tail];
    return val;
}

static seL4_MessageInfo_t rrer_ipc_handler_read_msg(rrer_ipc_handler_t *handler, rrer_ipc_t ipc)
{
    seL4_Word len = seL4_MessageInfo_get_length(ipc.msginfo);
    seL4_Word label = seL4_MessageInfo_get_label(ipc.msginfo);

    seL4_MessageInfo_t msg = seL4_MessageInfo_new(0, 0, 0, len);
    msg = seL4_MessageInfo_set_label(msg, label);

    for (int i = 0; i < len; i++) {
        seL4_SetMR(i, rrer_ipc_handler_get_mr(handler, &ipc, i));
    }

    // they should be the same, if there are no caps being sent?
    assert(msg.words[0] == ipc.msginfo.words[0]);
    return msg;
}

static seL4_Word rrer_queue_peek_badge(rrer_queue_t *q)
{
    assert(rrer_queue_len(q) > 0);
    return q->data[q->tail].badge;
}

static seL4_Word rrer_queue_peek_channel(rrer_queue_t *q)
{
    assert(rrer_queue_len(q) > 0);
    return q->data[q->tail].channel;
}

static void rrer_queue_pop_ignore(rrer_queue_t *q)
{
    assert(rrer_queue_len(q) > 0);
    // free the message
    rrer_ipc_t val = q->data[q->tail];
    rrer_ipc_handler_free(&q->handler, &val);
    q->tail += 1;
    q->tail %= QUEUE_MAX_LEN;
};

static bool rrer_badge_is_ntfn(seL4_Word badge)
{
    seL4_Word is_endpoint = badge >> BADGE_ENDPOINT_BIT;
    seL4_Word is_fault = (badge >> BADGE_FAULT_BIT) & 1;
    if (is_endpoint || is_fault)
        return false;
    return true;
}

static inline seL4_Word rrer_source_ch_to_target_ch(seL4_Word ch)
{
    if (ch % 2 == 1)
        return ch - 1;
    return ch + 1;
}
#undef IPC_WORD_STORAGE_SIZE
#undef IPC_WORD_STORAGE_WORDS
#undef QUEUE_MAX_LEN
