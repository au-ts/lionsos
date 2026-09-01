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

typedef struct {
    seL4_Word handler_index;
    seL4_Word badge;
    seL4_MessageInfo_t msginfo;
} ipc_t;

// holds all the message registers.
// is technically a ring allocator.
// Full when head == tail - 1
typedef struct {
    seL4_Word head;
    seL4_Word tail;
    seL4_Word data[IPC_WORD_STORAGE_WORDS];
} ipc_handler_t;

// queue holds an ipc.
typedef struct {
    ipc_handler_t handler;
    seL4_Word head;
    seL4_Word tail;
    ipc_t data[QUEUE_MAX_LEN];
} queue_t;

static queue_t *queue_init(uint8_t *memory)
{
    // zero out the data.
    for (int i = 0; i < sizeof(queue_t); i++) {
        memory[i] = 0;
    }
    return (queue_t *)(memory);
}

static seL4_Word ipc_handler_len(ipc_handler_t *handler)
{
    int64_t len = handler->head - handler->tail;
    // the head is wrapped
    if (len < 0) {
        len += IPC_WORD_STORAGE_WORDS;
    }
    return len;
}

static seL4_Word queue_len(queue_t *q)
{
    int64_t len = q->head - q->tail;
    // the head is wrapped
    if (len < 0) {
        len += IPC_WORD_STORAGE_WORDS;
    }
    return len;
}

static ipc_t ipc_handler_copy_msg(ipc_handler_t *handler, seL4_MessageInfo_t msg, seL4_Word badge)
{
    seL4_Word len = seL4_MessageInfo_get_length(msg);
    // assert that we have some space left.
    assert(len + ipc_handler_len(handler) < IPC_WORD_STORAGE_WORDS - 1);

    seL4_Word begin = handler->head;
    for (int i = 0; i < len; i++) {
        handler->data[handler->head] = seL4_GetMR(i);
        handler->head += 1;
        handler->head %= IPC_WORD_STORAGE_WORDS;
    }
    return (ipc_t) { .handler_index = begin, .badge = badge, .msginfo = msg };
}

static seL4_Word ipc_handler_get_mr(ipc_handler_t *handler, ipc_t *ipc, seL4_Word i)
{
    seL4_Word ind = (ipc->handler_index + i) % IPC_WORD_STORAGE_WORDS;
    return handler->data[ind];
}

static void ipc_handler_free(ipc_handler_t *handler, ipc_t *ipc)
{
    seL4_Word len = seL4_MessageInfo_get_length(ipc->msginfo);
    // assert that we are dequeuing more than what we have.
    assert(len <= ipc_handler_len(handler));

    handler->tail += len;
    handler->tail %= IPC_WORD_STORAGE_WORDS;
}

static void queue_push(queue_t *q, seL4_MessageInfo_t msg, seL4_Word badge)
{
    ipc_t val = ipc_handler_copy_msg(&q->handler, msg, badge);

    assert(queue_len(q) < QUEUE_MAX_LEN - 1);
    q->data[q->head] = val;
    q->head += 1;
    q->head %= QUEUE_MAX_LEN;
}

// Copies the queue contents into the IPC buffer.
static seL4_MessageInfo_t queue_peek(queue_t *q)
{
    assert(queue_len(q) > 0);

    ipc_t val = q->data[q->tail];
    seL4_Word len = seL4_MessageInfo_get_length(val.msginfo);
    seL4_Word label = seL4_MessageInfo_get_label(val.msginfo);

    seL4_MessageInfo_t msg = seL4_MessageInfo_new(0, 0, 0, len);
    msg = seL4_MessageInfo_set_label(msg, label);

    for (int i = 0; i < len; i++) {
        seL4_SetMR(i, ipc_handler_get_mr(&q->handler, &val, i));
    }

    // they should be the same, if there are no caps being sent?
    assert(msg.words[0] == val.msginfo.words[0]);
    return msg;
}

static void queue_pop_ignore(queue_t *q) {
    assert(queue_len(q) > 0);
    // free the message
    ipc_t val = q->data[q->tail];
    ipc_handler_free(&q->handler, &val);
    q->tail += 1;
    q->tail %= QUEUE_MAX_LEN;
};
#undef IPC_WORD_STORAGE_SIZE
#undef IPC_WORD_STORAGE_WORDS
#undef QUEUE_MAX_LEN
