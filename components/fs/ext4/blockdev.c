/*
 * Copyright (c) 2015 Grzegorz Kostka (kostka.grzegorz@gmail.com)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * - The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <libmicrokitco.h>

#include <sddf/blk/queue.h>
#include <sddf/blk/storage_info.h>

#include <ext4_component_config.h>
#include <ext4_blockdev.h>
#include <ext4_errno.h>

#include "decl.h"

extern blk_queue_handle_t blk_queue;
extern blk_storage_info_t *blk_storage_info;
extern char *blk_data;
extern bool blk_request_pushed;
extern uint64_t max_cluster_size;

static uint64_t thread_blk_addr[EXT4_WORKER_THREAD_NUM];

static int blk_resp_to_errno(blk_resp_status_t status) {
    switch (status) {
    case BLK_RESP_OK:
        return EOK;
    case BLK_RESP_ERR_INVALID_PARAM:
        return EINVAL;
    case BLK_RESP_ERR_IO:
    case BLK_RESP_ERR_UNSPEC:
        return EIO;
    case BLK_RESP_ERR_NO_DEVICE:
        return ENODEV;
    default:
        return EIO;
    }
}

static inline uint32_t max_blocks_per_request(void) { return (uint32_t)(max_cluster_size / BLK_TRANSFER_SIZE); }

/**********************BLOCKDEV INTERFACE**************************************/
static int blockdev_open(struct ext4_blockdev *bdev);
static int blockdev_bread(struct ext4_blockdev *bdev, void *buf, uint64_t blk_id, uint32_t blk_cnt);
static int blockdev_bwrite(struct ext4_blockdev *bdev, const void *buf, uint64_t blk_id, uint32_t blk_cnt);
static int blockdev_close(struct ext4_blockdev *bdev);
static int blockdev_lock(struct ext4_blockdev *bdev);
static int blockdev_unlock(struct ext4_blockdev *bdev);

/******************************************************************************/
EXT4_BLOCKDEV_STATIC_INSTANCE(blockdev, BLK_TRANSFER_SIZE, 0, blockdev_open, blockdev_bread, blockdev_bwrite,
                              blockdev_close, blockdev_lock, blockdev_unlock);

/******************************************************************************/
static int blockdev_open(struct ext4_blockdev *bdev) {
    (void)bdev;

    if (!blk_storage_is_ready(blk_storage_info)) {
        return ENODEV;
    }

    assert(blk_storage_info->sector_size > 0);
    assert(blk_storage_info->sector_size <= BLK_TRANSFER_SIZE);
    assert(BLK_TRANSFER_SIZE % blk_storage_info->sector_size == 0);

    for (uint16_t i = 0; i < EXT4_WORKER_THREAD_NUM; i++) {
        thread_blk_addr[i] = i * max_cluster_size;
    }

    blockdev.part_offset = 0;
    blockdev.part_size = blk_storage_info->capacity * BLK_TRANSFER_SIZE;
    /*
     * Expose 4KiB physical blocks to lwext4 to match BLK_TRANSFER_SIZE.
     * mkvirtdisk must format ext4 with 4KiB blocks as well.
     */
    blockdev.bdif->ph_bsize = BLK_TRANSFER_SIZE;
    blockdev.bdif->ph_bcnt = blk_storage_info->capacity;

    LOG_EXT4FS("Block Storage Information:\n");
    LOG_EXT4FS("--------------------------\n");
    LOG_EXT4FS("Serial Number: %s\n", blk_storage_info->serial_number);
    LOG_EXT4FS("Read-Only: %s\n", blk_storage_info->read_only ? "Yes" : "No");
    LOG_EXT4FS("Ready: %s\n", blk_storage_info->ready ? "Yes" : "No");
    LOG_EXT4FS("Sector Size: %u bytes\n", blk_storage_info->sector_size);
    LOG_EXT4FS("Optimal Block Size: %u units (%u bytes)\n", blk_storage_info->block_size,
               blk_storage_info->block_size * BLK_TRANSFER_SIZE);
    LOG_EXT4FS("Queue Depth: %u\n", blk_storage_info->queue_depth);
    LOG_EXT4FS("Total Capacity: %llu units (%llu bytes)\n", (unsigned long long)blk_storage_info->capacity,
               (unsigned long long)blockdev.part_size);
    LOG_EXT4FS("--------------------------\n");

    return EOK;
}

/******************************************************************************/
static int blockdev_bread(struct ext4_blockdev *bdev, void *buf, uint64_t blk_id, uint32_t blk_cnt) {
    (void)bdev;

    if (blk_cnt == 0) {
        return EOK;
    }
    if (blk_cnt > UINT16_MAX) {
        return EINVAL;
    }

    microkit_cothread_ref_t handle = microkit_cothread_my_handle();
    if (handle == LIBMICROKITCO_NULL_HANDLE || handle > EXT4_WORKER_THREAD_NUM) {
        return EIO;
    }

    uint32_t max_blk_cnt = max_blocks_per_request();
    if (max_blk_cnt == 0) {
        return EINVAL;
    }

    uint64_t read_data_offset = thread_blk_addr[handle - 1];
    uint32_t blocks_done = 0;
    while (blocks_done < blk_cnt) {
        uint32_t remaining = blk_cnt - blocks_done;
        uint32_t chunk_blocks = remaining;
        if (chunk_blocks > max_blk_cnt) {
            chunk_blocks = max_blk_cnt;
        }
        if (chunk_blocks > UINT16_MAX) {
            chunk_blocks = UINT16_MAX;
        }

        LOG_EXT4FS("blk_enqueue_read: addr: 0x%lx blk_id: %llu, count: %u ID: %d\n", read_data_offset,
                   (unsigned long long)(blk_id + blocks_done), chunk_blocks, handle);

        int err = blk_enqueue_req(&blk_queue, BLK_REQ_READ, read_data_offset, blk_id + blocks_done,
                                  (uint16_t)chunk_blocks, handle);
        if (err) {
            return EIO;
        }

        blk_request_pushed = true;
        wait_for_blk_resp();

        blk_resp_status_t status = (blk_resp_status_t)(uintptr_t)microkit_cothread_my_arg();
        int ret = blk_resp_to_errno(status);
        if (ret != EOK) {
            return ret;
        }

        uint64_t chunk_bytes = (uint64_t)chunk_blocks * BLK_TRANSFER_SIZE;
        memcpy((uint8_t *)buf + (uint64_t)blocks_done * BLK_TRANSFER_SIZE, blk_data + read_data_offset, chunk_bytes);
        blocks_done += chunk_blocks;
    }

    return EOK;
}

/******************************************************************************/
static int blockdev_bwrite(struct ext4_blockdev *bdev, const void *buf, uint64_t blk_id, uint32_t blk_cnt) {
    (void)bdev;

    if (blk_cnt == 0) {
        return EOK;
    }
    if (blk_cnt > UINT16_MAX) {
        return EINVAL;
    }

    microkit_cothread_ref_t handle = microkit_cothread_my_handle();
    if (handle == LIBMICROKITCO_NULL_HANDLE || handle > EXT4_WORKER_THREAD_NUM) {
        return EIO;
    }

    uint32_t max_blk_cnt = max_blocks_per_request();
    if (max_blk_cnt == 0) {
        return EINVAL;
    }

    uint64_t write_data_offset = thread_blk_addr[handle - 1];
    uint32_t blocks_done = 0;
    while (blocks_done < blk_cnt) {
        uint32_t remaining = blk_cnt - blocks_done;
        uint32_t chunk_blocks = remaining;
        if (chunk_blocks > max_blk_cnt) {
            chunk_blocks = max_blk_cnt;
        }
        if (chunk_blocks > UINT16_MAX) {
            chunk_blocks = UINT16_MAX;
        }

        uint64_t chunk_bytes = (uint64_t)chunk_blocks * BLK_TRANSFER_SIZE;
        memcpy(blk_data + write_data_offset, (const uint8_t *)buf + (uint64_t)blocks_done * BLK_TRANSFER_SIZE,
               chunk_bytes);

        LOG_EXT4FS("blk_enqueue_write: addr: 0x%lx blk_id: %llu, count: %u ID: %d\n", write_data_offset,
                   (unsigned long long)(blk_id + blocks_done), chunk_blocks, handle);

        int err = blk_enqueue_req(&blk_queue, BLK_REQ_WRITE, write_data_offset, blk_id + blocks_done,
                                  (uint16_t)chunk_blocks, handle);
        if (err) {
            return EIO;
        }

        blk_request_pushed = true;
        wait_for_blk_resp();

        blk_resp_status_t status = (blk_resp_status_t)(uintptr_t)microkit_cothread_my_arg();
        int ret = blk_resp_to_errno(status);
        if (ret != EOK) {
            return ret;
        }

        blocks_done += chunk_blocks;
    }

    return EOK;
}

/******************************************************************************/
static int blockdev_close(struct ext4_blockdev *bdev) {
    (void)bdev;
    return EOK;
}

static int blockdev_lock(struct ext4_blockdev *bdev) {
    (void)bdev;
    return EOK;
}

static int blockdev_unlock(struct ext4_blockdev *bdev) {
    (void)bdev;
    return EOK;
}

/******************************************************************************/
struct ext4_blockdev *ext4_blockdev_get(void) { return &blockdev; }
/******************************************************************************/
