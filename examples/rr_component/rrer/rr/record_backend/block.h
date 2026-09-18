#pragma once
// implements the specified headers in record.h
#include <sddf/blk/queue.h>
#include <sddf/blk/storage_info.h>
#include <sddf/blk/config.h>
#include <string.h>
#include "../base.h"
#include "os/sddf.h"
#include "sel4/shared_types_gen.h"
#include "sel4/syscalls_mcs.h"

__attribute__((__section__(".blk_client_config"))) blk_client_config_t blk_config;
static blk_queue_handle_t blk_queue;
#define REQUEST_BLK_NUMBER 0
#define REQUEST_NUM_BLOCKS 1

static inline void rr_init_record_backend()
{
    static const char funny_str[] = "balls";
    // test if we can write to the backend.
    assert(blk_config_check_magic(&blk_config));
    REC("config check\n");
    blk_queue_init(&blk_queue, blk_config.virt.req_queue.vaddr, blk_config.virt.resp_queue.vaddr,
                   blk_config.virt.num_buffers);
    REC("queue init\n");

    /* Want to print out the storage info, so spin until the it is ready. */
    blk_storage_info_t *storage_info = blk_config.virt.storage_info.vaddr;
    while (!blk_storage_is_ready(storage_info));
    REC("device config ready\n");
    REC("device size: 0x%lx bytes\n", storage_info->capacity * BLK_TRANSFER_SIZE);

    // Do a write
    REC("Doing test write of '%s'\n", funny_str);
    memcpy((char *)blk_config.data.vaddr, funny_str, sizeof(funny_str));
    assert(!blk_enqueue_req(&blk_queue, BLK_REQ_WRITE, 0, REQUEST_BLK_NUMBER, REQUEST_NUM_BLOCKS, 0));
    {
        // perform a notify
        // Guaranteed preemption
        seL4_Signal(BASE_OUTPUT_NOTIFICATION_CAP + blk_config.virt.id);

        // We expect to get a response.
        seL4_Word badge = 0;
        // discard the message
        seL4_Recv(INPUT_CAP, &badge, REPLY_CAP);
        assert(rr_badge_to_channel_id(badge) == blk_config.virt.id);
    }

    REC("Checking if read was successful\n");
    blk_resp_status_t status = -1;
    uint16_t count = -1;
    uint32_t id = -1;
    int err = blk_dequeue_resp(&blk_queue, &status, &count, &id);
    assert(!err);
    assert(status == BLK_RESP_OK);
    assert(count == REQUEST_NUM_BLOCKS);
    assert(id == 0);

    REC("Doing test read\n");
    err = blk_enqueue_req(&blk_queue, BLK_REQ_READ, 0, REQUEST_BLK_NUMBER, REQUEST_NUM_BLOCKS, 0);
    assert(!err);

    {
        // perform a notify
        // Guaranteed preemption
        seL4_Signal(BASE_OUTPUT_NOTIFICATION_CAP + blk_config.virt.id);

        // We expect to get a response.
        seL4_Word badge = 0;
        // discard the message
        seL4_Recv(INPUT_CAP, &badge, REPLY_CAP);
        assert(rr_badge_to_channel_id(badge) == blk_config.virt.id);
    }

    status = -1;
    count = -1;
    id = -1;
    err = blk_dequeue_resp(&blk_queue, &status, &count, &id);
    assert(!err);
    assert(status == BLK_RESP_OK);
    assert(count == REQUEST_NUM_BLOCKS);
    assert(id == 0);

    // Check that the read went okay
    char *read_data = (char *)(blk_config.data.vaddr);
    bool failed = false;
    for (int i = 0; i < sizeof(funny_str); i++) {
        REC("position %d, expected %x got %x\n", i, funny_str[i], read_data[i]);
        if (funny_str[i] != read_data[i])
            failed = true;
    }
    assert(!failed);
}

static inline void rr_record_store_ipc_msg_backend(seL4_Word cycle_count, seL4_Word source_child, seL4_Word badge,
                                                   seL4_MessageInfo_t msg)
{
}

static inline void rr_record_store_scheduler_event_backend(seL4_Word cycle_count, seL4_Word child_id,
                                                           rr_ChildState_e new_state)
{
}
