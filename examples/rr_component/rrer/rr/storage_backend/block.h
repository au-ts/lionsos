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

static inline bool _rr_block_check_resp(uint64_t num_blocks, uint64_t expected_id);

// initialise the block storage and check that reading and writing succeeds.
static inline void rr_init_storage_backend()
{
    // TODO!!!!! Make it write back the original values so that this can be run both on record and replay.
    STORAGE_LOG("Initialising block storage\n");
    assert(blk_config_check_magic(&blk_config));
    blk_queue_init(&_rr_block_queue_handle, blk_config.virt.req_queue.vaddr, blk_config.virt.resp_queue.vaddr,
                   blk_config.virt.num_buffers);
    _rr_block_storage_info = blk_config.virt.storage_info.vaddr;
    while (!blk_storage_is_ready(_rr_block_storage_info));
    STORAGE_LOG("device config ready\n");
    STORAGE_LOG("device size: 0x%lx bytes\n", _rr_block_storage_info->capacity * BLK_TRANSFER_SIZE);
    _rr_block_initialised = true;

    uint8_t original[sizeof(RR_STORAGE_MAGIC)] = { 0 };
    BLK_NO_ERR(rr_storage_read(99, (uint8_t *)original, sizeof(RR_STORAGE_MAGIC)));

    // now perform a quick test read and write
    STORAGE_LOG("Test write of magic %s\n", RR_STORAGE_MAGIC);
    BLK_NO_ERR(rr_storage_write(99, (const uint8_t *)RR_STORAGE_MAGIC, sizeof(RR_STORAGE_MAGIC)));
    STORAGE_LOG("Test read of magic\n");

    char magic_read[sizeof(RR_STORAGE_MAGIC)] = { 0 };

    BLK_NO_ERR(rr_storage_read(99, (uint8_t *)magic_read, sizeof(RR_STORAGE_MAGIC)));

    STORAGE_LOG("Magic read: %x %x %x %x %x %x %x\n", magic_read[0], magic_read[1], magic_read[2], magic_read[3],
        magic_read[4], magic_read[5], magic_read[6]);
    // make sure we read things correctly.
    assert(memcmp(magic_read, RR_STORAGE_MAGIC, sizeof(RR_STORAGE_MAGIC)) == 0);
    BLK_NO_ERR(rr_storage_write(99, original, sizeof(RR_STORAGE_MAGIC)));
}

// Basic abstraction bc i'm stupid.
// Very synchronous and slow and unoptimized.
static inline bool rr_storage_write(uint64_t byte_offset, const uint8_t *bytes, uint64_t num_bytes)
{
    assert(_rr_block_initialised);
    assert(num_bytes + byte_offset < _rr_block_storage_info->capacity * BLK_TRANSFER_SIZE);
    assert(num_bytes > 0);

    // Read the sectors that we are going to write to.
    // So we don't clobber stuff on write (Please optimize me!)
    uint64_t blk_number = byte_offset / BLK_TRANSFER_SIZE;
    uint64_t blk_offset = byte_offset % BLK_TRANSFER_SIZE;
    uint64_t num_blks = 1 + (blk_offset + num_bytes) / BLK_TRANSFER_SIZE;
    int err = 0;
    err = blk_enqueue_req(&_rr_block_queue_handle, BLK_REQ_READ, 0, blk_number, num_blks, 9);
    assert(!err);
    assert(_rr_block_check_resp(num_blks, 9));

    // now write what we wanted.
    memcpy(blk_config.data.vaddr + blk_offset, bytes, num_bytes);
    // And then write back the blocks
    err = blk_enqueue_req(&_rr_block_queue_handle, BLK_REQ_WRITE, 0, blk_number, num_blks, 8);
    assert(!err);
    assert(_rr_block_check_resp(num_blks, 8));
    return true;
}

// Get the size in bytes
static inline seL4_Word rr_storage_get_size() {
    assert(_rr_block_initialised);
    // capacity is specified in BLK_TRANSFER_SIZE units.
    return _rr_block_storage_info->capacity * BLK_TRANSFER_SIZE;
}

// check if a request succeeded.
static inline bool _rr_block_check_resp(uint64_t num_blocks, uint64_t expected_id)
{
    sddf_notify(blk_config.virt.id);

    seL4_Yield();
    seL4_Word badge = 0;
    do {
        seL4_Recv(INPUT_CAP, &badge, REPLY_CAP);
    } while (rr_badge_to_channel_id(badge) == blocker_ch || rr_badge_to_channel_id(badge) == sender_ch);
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

static inline bool rr_storage_read(uint64_t byte_offset, uint8_t *result, uint64_t num_bytes)
{
    assert(_rr_block_initialised);
    assert(num_bytes + byte_offset < _rr_block_storage_info->capacity * BLK_TRANSFER_SIZE);

    // Read the sectors that we want
    // So we don't clobber stuff on write (Please optimize me!)
    uint64_t blk_number = byte_offset / BLK_TRANSFER_SIZE;
    uint64_t blk_offset = byte_offset % BLK_TRANSFER_SIZE;
    uint64_t num_blks = 1 + (blk_offset + num_bytes) / BLK_TRANSFER_SIZE;
    int err = 0;

    err = blk_enqueue_req(&_rr_block_queue_handle, BLK_REQ_READ, 0, blk_number, num_blks, 1);
    assert(!err);
    // i guess can't fail for now.
    assert(_rr_block_check_resp(num_blks, 1));
    // copy the data at the offset we want.
    memcpy(result, ((uint8_t*)blk_config.data.vaddr) + blk_offset, num_bytes);
    return true;
}
