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
#include <stdio.h>
#include <string.h>
#include <microkit.h>
#include <sddf/blk/config.h>
#include <sddf/blk/queue.h>

extern blk_storage_info_t *storage_info;
extern blk_queue_handle_t blk_queue;
extern blk_client_config_t blk_config;

#include <ext4_config.h>
#include <ext4_blockdev.h>
#include <ext4_errno.h>


/**********************BLOCKDEV INTERFACE**************************************/
static int blockdev_open(struct ext4_blockdev *bdev);
static int blockdev_bread(struct ext4_blockdev *bdev, void *buf, uint64_t blk_id,
             uint32_t blk_cnt);
static int blockdev_bwrite(struct ext4_blockdev *bdev, const void *buf,
              uint64_t blk_id, uint32_t blk_cnt);
static int blockdev_close(struct ext4_blockdev *bdev);
static int blockdev_lock(struct ext4_blockdev *bdev);
static int blockdev_unlock(struct ext4_blockdev *bdev);

/******************************************************************************/
EXT4_BLOCKDEV_STATIC_INSTANCE(blockdev, BLK_TRANSFER_SIZE, 0, blockdev_open,
                  blockdev_bread, blockdev_bwrite, blockdev_close,
                  blockdev_lock, blockdev_unlock);

/******************************************************************************/
static int blockdev_open(struct ext4_blockdev *bdev)
{
    /*blockdev_open: skeleton*/
    // microkit_dbg_puts("LIONSOS: open\n");

    blockdev.part_offset = 0;
    blockdev.part_size = storage_info->capacity * BLK_TRANSFER_SIZE;
    blockdev.bdif->ph_bcnt = blockdev.part_size / blockdev.bdif->ph_bsize;

    assert(bdev->bdif->ph_bsize == BLK_TRANSFER_SIZE);

    return EOK;
}

/******************************************************************************/

static int blockdev_bread(struct ext4_blockdev *bdev, void *buf, uint64_t blk_id,
             uint32_t blk_cnt)
{
    // microkit_dbg_puts("LIONSOS: bread\n");
    // printf("blk_id %lu, blk_cnt: %u\n", blk_id, blk_cnt);

    size_t buf_size = blk_cnt * BLK_TRANSFER_SIZE;
    assert(buf_size <= blk_config.data.size);

    int err = blk_enqueue_req(&blk_queue, BLK_REQ_READ, 0, blk_id, blk_cnt, 0);
    assert(!err);
    microkit_notify(blk_config.virt.id);

    blk_resp_status_t status;
    uint16_t success_count;
    uint32_t id;
    while (blk_dequeue_resp(&blk_queue, &status, &success_count, &id)) {}

    assert(status == BLK_RESP_OK);
    assert(success_count == blk_cnt);
    assert(id == 0);

    memcpy(buf, blk_config.data.vaddr, buf_size);

    /*blockdev_bread: skeleton*/
    return EOK;
}


/******************************************************************************/
static int blockdev_bwrite(struct ext4_blockdev *bdev, const void *buf,
              uint64_t blk_id, uint32_t blk_cnt)
{
    // microkit_dbg_puts("LIONSOS: bwrite\n");
    // printf("blk_id %d, blk_cnt: %d\n", blk_id, blk_cnt);

    size_t buf_size = blk_cnt * BLK_TRANSFER_SIZE;
    assert(buf_size <= blk_config.data.size);

    memcpy(blk_config.data.vaddr, buf, buf_size);

    int err = blk_enqueue_req(&blk_queue, BLK_REQ_WRITE, 0, blk_id, blk_cnt, 0);
    assert(!err);
    microkit_notify(blk_config.virt.id);

    blk_resp_status_t status;
    uint16_t success_count;
    uint32_t id;
    while (blk_dequeue_resp(&blk_queue, &status, &success_count, &id)) {}

    assert(status == BLK_RESP_OK);
    assert(success_count == blk_cnt);
    assert(id == 0);

    /*blockdev_bwrite: skeleton*/
    return EOK;
}
/******************************************************************************/
static int blockdev_close(struct ext4_blockdev *bdev)
{
    // todo: maybe call flush/sync? otherwise nothing for us to do
    // microkit_dbg_puts("LIONSOS: close\n");
    return EOK;
}

static int blockdev_lock(struct ext4_blockdev *bdev)
{
    // microkit_dbg_puts("LIONSOS: lock\n");
    /*blockdev_lock: skeleton*/
    return EOK;
}

static int blockdev_unlock(struct ext4_blockdev *bdev)
{
    // microkit_dbg_puts("LIONSOS: unlock\n");
    /*blockdev_unlock: skeleton*/
    return EOK;
}

/******************************************************************************/
struct ext4_blockdev *ext4_blockdev_get(void)
{
    return &blockdev;
}
/******************************************************************************/

