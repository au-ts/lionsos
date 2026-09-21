#pragma once
// implements the specified headers in base.h
#include <sddf/blk/queue.h>
#include <sddf/blk/storage_info.h>
#include <sddf/blk/config.h>
#include <string.h>
#include "../base.h"
#include "os/sddf.h"
#include "sel4/shared_types_gen.h"
#include "sel4/syscalls_mcs.h"
#include "base.h"

#define BLK_NO_ERR(val) assert(val)
#define VIRTIO_BLK_SECTOR_SIZE 512

__attribute__((__section__(".blk_client_config"))) blk_client_config_t blk_config;
static blk_queue_handle_t _rr_block_queue_handle;

static bool _rr_block_initialised = false;
static blk_storage_info_t *_rr_block_storage_info = NULL;

// synchronous read and writes for now.
static inline bool _rr_block_queue_write(uint64_t byte_offset, const uint8_t *bytes, uint64_t num_bytes);
static inline bool _rr_block_queue_read(uint64_t byte_offset, uint8_t *result, uint64_t num_bytes);
static inline bool _rr_block_check_resp(uint64_t num_blocks, uint64_t expected_id);

static inline void _rr_block_init()
{
    REC("Initialising block storage\n");
    assert(blk_config_check_magic(&blk_config));
    blk_queue_init(&_rr_block_queue_handle, blk_config.virt.req_queue.vaddr, blk_config.virt.resp_queue.vaddr,
                   blk_config.virt.num_buffers);
    _rr_block_storage_info = blk_config.virt.storage_info.vaddr;
    while (!blk_storage_is_ready(_rr_block_storage_info));
    REC("device config ready\n");
    REC("device size: 0x%lx bytes\n", _rr_block_storage_info->capacity * BLK_TRANSFER_SIZE);
    _rr_block_initialised = true;

    // now perform a quick test read and write
    REC("Test write of magic %s\n", RR_STORAGE_MAGIC);
    BLK_NO_ERR(_rr_block_queue_write(99, (const uint8_t *)RR_STORAGE_MAGIC, sizeof(RR_STORAGE_MAGIC)));
    REC("Test read of magic\n");

    char magic_read[sizeof(RR_STORAGE_MAGIC)] = { 0 };

    BLK_NO_ERR(_rr_block_queue_read(99, (uint8_t *)magic_read, sizeof(RR_STORAGE_MAGIC)));

    REC("Magic read: %x %x %x %x %x %x %x\n", magic_read[0], magic_read[1], magic_read[2], magic_read[3],
        magic_read[4], magic_read[5], magic_read[6]);
    assert(memcmp(magic_read, RR_STORAGE_MAGIC, sizeof(RR_STORAGE_MAGIC)) == 0);
}

// Basic abstraction bc i'm stupid.
// Very synchronous and slow.
static inline bool _rr_block_queue_write(uint64_t byte_offset, const uint8_t *bytes, uint64_t num_bytes)
{
    assert(_rr_block_initialised);
    assert(num_bytes + byte_offset < _rr_block_storage_info->capacity * BLK_TRANSFER_SIZE);
    assert(num_bytes > 0);
    uint64_t blk_number = byte_offset / BLK_TRANSFER_SIZE;
    uint64_t blk_offset = byte_offset % BLK_TRANSFER_SIZE;

    // now run the block driver.
    // Copy our testing data into the block data region
    memcpy(blk_config.data.vaddr + blk_offset, bytes, num_bytes);

    // determine how many blocks will be overlapped
    uint64_t num_blks = 1 + ((num_bytes + blk_offset) / BLK_TRANSFER_SIZE);

	// not sure what the io_or_offset exactly is.
    int err = blk_enqueue_req(&_rr_block_queue_handle, BLK_REQ_WRITE, 0, blk_number, num_blks, 0);
    assert(!err);
    // i guess can't fail for now.
    assert(_rr_block_check_resp(num_blks, 0));
    return true;
}

// check if a request succeeded.
static inline bool _rr_block_check_resp(uint64_t num_blocks, uint64_t expected_id)
{
    sddf_notify(blk_config.virt.id);

    seL4_Yield();
    seL4_Word badge = 0;
    seL4_Recv(INPUT_CAP, &badge, REPLY_CAP);
    assert(rr_badge_to_channel_id(badge) == blk_config.virt.id);

    // assert that the write was successful.
    blk_resp_status_t status = -1;
    uint16_t count = -1;
    uint32_t id = -1;
    int err = blk_dequeue_resp(&_rr_block_queue_handle, &status, &count, &id);
    assert(!err);
    assert(status == BLK_RESP_OK);
    assert(count == num_blocks);
    assert(id == expected_id);
    return true;
}

// i cannot guarantee if
static inline bool _rr_block_queue_read(uint64_t byte_offset, uint8_t *result, uint64_t num_bytes)
{
    assert(_rr_block_initialised);
    assert(num_bytes + byte_offset < _rr_block_storage_info->capacity * BLK_TRANSFER_SIZE);

    uint64_t blk_number = byte_offset / BLK_TRANSFER_SIZE;
    uint64_t blk_offset = byte_offset % BLK_TRANSFER_SIZE;

    // determine how many blocks will be overlapped
    uint64_t num_blks = 1 + ((num_bytes + blk_offset) / BLK_TRANSFER_SIZE);

    int err = blk_enqueue_req(&_rr_block_queue_handle, BLK_REQ_READ, 0, blk_number, num_blks, 1);
    assert(!err);
    // i guess can't fail for now.
    assert(_rr_block_check_resp(num_blks, 1));
    // copy the data
    memcpy(result, ((uint8_t*)blk_config.data.vaddr) + blk_offset, num_bytes);
    return true;
}

static inline void rr_init_storage_backend()
{
    _rr_block_init();
}

static inline void rr_storage_store_ipc_backend(seL4_Word cycle_count, seL4_Word source_child, seL4_Word badge,
                                                seL4_MessageInfo_t msg)
{
}

static inline void rr_storage_store_scheduler_event_backend(seL4_Word cycle_count, seL4_Word child_id,
                                                            rr_ChildState_e new_state)
{
}

static inline void rr_storage_read_instruction()
{
}
