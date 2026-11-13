#pragma once
#include "config.h"
#include <stdint.h>
#include <stdbool.h>
#include <sel4/sel4.h>

typedef struct {
    char magic[sizeof(PDC_IPC_CONFIG_MAGIC)];
    uint8_t channel_id_to_source_child_id[PDC_MAX_CHANNELS];
    seL4_Word sender_channel_id;
    seL4_Word receiver_channel_id;
    seL4_Word sender_id;
    void *queue_storage;
} pdc_ipc_tracer_config_t;

// Storage in number of bytes
#define PDC_IPC_WORD_STORAGE_SIZE (0x800)
#define PDC_IPC_WORD_STORAGE_WORDS (PDC_IPC_WORD_STORAGE_SIZE / sizeof(seL4_Word))
#define PDC_QUEUE_MAX_LEN 10
#define PDC_NO_THREAD_SCHEDULED ((seL4_Word)(-1))

// We store IPC as the following:
// - Each child get's it's own IPC buffer (array ring/queue), in which we store message registers.
// - Each child get's it's own IPC queue, in which we store the following:
//   - handler_index (a reference to where the message registers are stored)
//   - badge (the badge which the IPC was sent with)
//   - msginfo (the message info that was sent)
//   - channel (the source channel which can be converted into the target one)
// - The ipc buffer ring queue is guaraneed to empty in the order in which the IPC events occur,
//   so we won't have issues with sparsity.
typedef struct pdc_ipc_obj {
    // index to the message registers inside the handler.
    // The handler holds the message registers in a "queue"
    seL4_Word handler_index;
    seL4_Word badge;
    seL4_MessageInfo_t msginfo;
    seL4_Word channel;
} pdc_ipc_obj_t;

// holds all the message registers.
// is technically a ring allocator.
// Full when head == tail - 1
typedef struct pdc_ipc_obj_handler {
    seL4_Word head;
    seL4_Word tail;
    seL4_Word data[PDC_IPC_WORD_STORAGE_WORDS];
} pdc_ipc_obj_handler_t;

// queue holds an ipc.
typedef struct pdc_ipc_obj_queue {
    pdc_ipc_obj_handler_t handler;
    seL4_Word head;
    seL4_Word tail;
    pdc_ipc_obj_t data[PDC_QUEUE_MAX_LEN];
} pdc_ipc_obj_queue_t;

typedef struct {
    const pdc_config_handle_t *all_cfg;
    const pdc_ipc_tracer_config_t *ipc_cfg;
    seL4_Word sender_ch;
    seL4_Word sender_id;
    seL4_Word *ipc_target_child_id;
    pdc_ipc_obj_queue_t *per_thread_queue;
} pdc_ipc_tracer_handle_t;

typedef enum {
    pdc_ipc_type_Call,
    pdc_ipc_type_Ntfn,
} pdc_ipc_type_e;

pdc_ipc_tracer_handle_t pdc_ipc_tracer_init(const pdc_config_handle_t *all_cfg, const pdc_ipc_tracer_config_t *ipc_cfg);

void pdc_ipc_tracer_set_target_child(pdc_ipc_tracer_handle_t *handle, seL4_Word child);

void pdc_ipc_tracer_push_ipc_from_buffer(pdc_ipc_tracer_handle_t *handle, seL4_MessageInfo_t msg, seL4_Word badge,
                                         seL4_Word source_ch);

void pdc_ipc_tracer_push_ipc(pdc_ipc_tracer_handle_t *handle);

seL4_Word pdc_ipc_tracer_get_target_child(pdc_ipc_tracer_handle_t *handle);

bool pdc_ipc_tracer_can_peek_ipc(pdc_ipc_tracer_handle_t *handle);
pdc_ipc_obj_t pdc_ipc_tracer_peek_ipc(pdc_ipc_tracer_handle_t *handle);

void pdc_ipc_do_ipc(pdc_ipc_tracer_handle_t *handle, pdc_ipc_obj_t *ipc);

// WARNING: This invalidates any pdc_ipc_obj_t given from peeking.
void pdc_ipc_tracer_consume_ipc(pdc_ipc_tracer_handle_t *handle);

seL4_Word pdc_ipc_tracer_get_childid_from_channel(pdc_ipc_tracer_handle_t *handle, seL4_Word channel);

/***************************/
/**** internal stuff ;) ****/
/***************************/

pdc_ipc_obj_queue_t *pdc_internal_ipc_obj_queue_init(uint8_t *memory);

seL4_Word pdc_internal_ipc_obj_handler_len(pdc_ipc_obj_handler_t *handler);

seL4_Word pdc_internal_ipc_obj_queue_len(pdc_ipc_obj_queue_t *q);

pdc_ipc_obj_t pdc_internal_ipc_obj_handler_copy_msg(pdc_ipc_obj_handler_t *handler, seL4_MessageInfo_t msg,
                                                    seL4_Word badge, seL4_Word target_ch);

seL4_Word pdc_internal_ipc_handler_get_mr(pdc_ipc_obj_queue_t *queue, pdc_ipc_obj_t *ipc, seL4_Word i);

void pdc_internal_ipc_obj_handler_free(pdc_ipc_obj_handler_t *handler, pdc_ipc_obj_t *ipc);

void pdc_internal_ipc_obj_queue_push(pdc_ipc_obj_queue_t *q, seL4_MessageInfo_t msg, seL4_Word badge,
                                     seL4_Word target_ch);

// Copies the queue contents into the IPC buffer.
pdc_ipc_obj_t pdc_internal_ipc_obj_queue_peek(pdc_ipc_obj_queue_t *q);

seL4_MessageInfo_t pdc_internal_ipc_obj_handler_read_msg(pdc_ipc_obj_handler_t *handler, pdc_ipc_obj_t ipc);

seL4_Word pdc_internal_ipc_obj_queue_peek_badge(pdc_ipc_obj_queue_t *q);

seL4_Word pdc_internal_ipc_obj_queue_peek_channel(pdc_ipc_obj_queue_t *q);

void pdc_internal_ipc_obj_queue_pop_ignore(pdc_ipc_obj_queue_t *q);

bool pdc_internal_ipc_obj_badge_is_ntfn(seL4_Word badge);

seL4_Word pdc_internal_ipc_source_ch_to_target_ch(seL4_Word ch);

// store the curretly stored message in the ipc buffer into the recv queue of that
// child.
void pdc_internal_ipc_tracer_store(pdc_ipc_tracer_handle_t *handle, seL4_Word target_child, seL4_MessageInfo_t msg,
                                   seL4_Word badge, seL4_Word source_ch);

// check the size of the child's ipc queue.
seL4_Word pdc_internal_ipc_child_queue_len(pdc_ipc_tracer_handle_t *handle, seL4_Word target_child);

// Get the badge of the earliest sender.
seL4_Word pdc_internal_ipc_child_queue_peek_badge(pdc_ipc_tracer_handle_t *handle, seL4_Word target_child);

// Get the badge of the channel sender.
seL4_Word pdc_internal_ipc_child_queue_peek_channel(pdc_ipc_tracer_handle_t *handle, seL4_Word target_child);
