/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <libmicrokitco.h>
#include <sddf/util/util.h>
#include <sddf/blk/queue.h>
#include <sddf/blk/storage_info.h>
#include "ff.h"
#include "diskio.h"
#include "decl.h"

extern blk_queue_handle_t blk_queue;
extern blk_storage_info_t *blk_storage_info;
extern char *blk_data;

extern bool blk_request_pushed;

/* TODO fix comment
 *  This def restrict the maximum cluster size that the fatfs can have
 *  This restriction should not cause any problem as long as the BLK_REGION_SIZE between file system and blk virt is not
 *  too small. For example, 32GB - 256TB disks are recommended to have a sector size of 128KB, and if you have 4 worker threads,
 *  BLK_REGION_SIZE should be bigger than 512KB. In fileio example, BLK_REGION_SIZE is set to 2 MB.
 */
extern uint64_t max_cluster_size;

uint64_t thread_blk_addr[FAT_WORKER_THREAD_NUM];

#define IS_POWER_OF_2(x) ((x) && !((x) & ((x) - 1)))

void wait_for_blk_resp(void) {
    extern microkit_cothread_sem_t sem[FAT_WORKER_THREAD_NUM + 1];
    microkit_cothread_ref_t handle = microkit_cothread_my_handle();
    microkit_cothread_semaphore_wait(&sem[handle]);
}

DSTATUS disk_initialize (
    BYTE pdrv                /* Physical drive number to identify the drive */
)
{
    // thread_blk_addr[0] is not initialized as that is the slot for event thread
    for (uint16_t i = 0; i < FAT_WORKER_THREAD_NUM; i++) {
        thread_blk_addr[i] = i * max_cluster_size;
    }

    // Check whether the block device is ready or not
    if (!blk_storage_is_ready(blk_storage_info)) {
        return RES_NOTRDY;
    }

    // The sector size should be a mutiple of 512, BLK_TRANSFER_SIZE % SECTOR_SIZE should be 0
    // BLK_TRANSFER_SIZE % SECTOR_SIZE should be a power of 2
    assert(blk_storage_info->sector_size % 512 == 0 && "Sector size must be a multiple of 512");
    assert(blk_storage_info->sector_size <= BLK_TRANSFER_SIZE && "BLK_TRANSFER_SIZE must be the same or larger than sector size");
    assert(IS_POWER_OF_2(BLK_TRANSFER_SIZE / blk_storage_info->sector_size) && "BLK_TRANSFER_SIZE / SECTOR_SIZE must be a power of 2");

    LOG_FATFS("Block Storage Information:\n");
    LOG_FATFS("--------------------------\n");
    LOG_FATFS("Serial Number: %s\n", blk_storage_info->serial_number);
    LOG_FATFS("Read-Only: %s\n", blk_storage_info->read_only ? "Yes" : "No");
    LOG_FATFS("Ready: %s\n", blk_storage_info->ready ? "Yes" : "No");
    LOG_FATFS("Sector Size: %u bytes\n", blk_storage_info->sector_size);
    LOG_FATFS("Optimal Block Size: %u units (%u bytes)\n",
              blk_storage_info->block_size, blk_storage_info->block_size * BLK_TRANSFER_SIZE);
    LOG_FATFS("Queue Depth: %u\n", blk_storage_info->queue_depth);
    LOG_FATFS("Geometry:\n");
    LOG_FATFS("  Cylinders: %u\n", blk_storage_info->cylinders);
    LOG_FATFS("  Heads: %u\n", blk_storage_info->heads);
    LOG_FATFS("  Blocks: %u\n", blk_storage_info->blocks);
    LOG_FATFS("Total Capacity: %llu units (%llu bytes)\n",
              (unsigned long long)blk_storage_info->capacity,
              (unsigned long long)(blk_storage_info->capacity * BLK_TRANSFER_SIZE));
    LOG_FATFS("--------------------------\n");
    return RES_OK;
}

DSTATUS disk_status (
    BYTE pdrv        /* Physical drive nmuber to identify the drive */
)
{
    return RES_OK;
}

DRESULT disk_ioctl (BYTE pdrv, BYTE cmd, void* buff) {
    DRESULT res;
    if (cmd == GET_SECTOR_SIZE) {
        WORD *size = buff;
        *size = blk_storage_info->sector_size;
        res = RES_OK;
    }
    if (cmd == CTRL_SYNC) {
        res = RES_OK;
        LOG_FATFS("blk_enqueue_syncreq\n");
        int err = blk_enqueue_req(&blk_queue, BLK_REQ_FLUSH, 0, 0, 0, microkit_cothread_my_handle());
        assert(!err);
        blk_request_pushed = true;
        wait_for_blk_resp();
        res = (DRESULT)(uintptr_t)microkit_cothread_my_arg();
    }
    return res;
}

#define MOD_POWER_OF_2(a, b) ((a) & ((b) - 1))
#define DIV_POWER_OF_2(a, b) ((a) >> (__builtin_ctz(b)))
#define MUL_POWER_OF_2(a, b) ((a) << (__builtin_ctz(b)))

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    DRESULT res;
    int handle = microkit_cothread_my_handle();
    // Accroding the protocol, all the read/write addr passed to the blk_virt should be page aligned
    // Substract the handle with one as the worker thread ID starts at 1, not 0
    uint64_t read_data_offset = thread_blk_addr[handle - 1];
    uint16_t sector_size = blk_storage_info->sector_size;
    // This is the same as BLK_TRANSFER_SIZE / sector_size
    uint16_t sector_per_transfer = DIV_POWER_OF_2(BLK_TRANSFER_SIZE, sector_size);
    uint32_t sddf_sector = DIV_POWER_OF_2(sector, sector_per_transfer);
    // The final sddf_count is always positive, however in the process of calculating it may be added with a negative value, so use signed integer here
    int32_t sddf_count = 0;
    uint32_t unaligned_head_sector = MOD_POWER_OF_2(sector_per_transfer - MOD_POWER_OF_2(sector, sector_per_transfer), sector_per_transfer);
    uint32_t unaligned_tail_sector = MOD_POWER_OF_2(sector + count, sector_per_transfer);
    if (unaligned_head_sector) {
        sddf_count += 1;
    }
    if (unaligned_tail_sector) {
        sddf_count += 1;
    }
    // DIV_POWER_OF_2(aligned_sector, sector_per_transfer) maybe -1 if the sector to read is in one transfer block and not aligned to both side, but should still works
    int32_t aligned_sector = count - unaligned_head_sector - unaligned_tail_sector;
    sddf_count += DIV_POWER_OF_2(aligned_sector, sector_per_transfer);

    assert(MUL_POWER_OF_2(sddf_count, BLK_TRANSFER_SIZE) <= max_cluster_size);

    LOG_FATFS("blk_enqueue_read pre adjust: addr: 0x%lx sector: %u, count: %u ID: %d\n", read_data_offset, sector, count, handle);
    LOG_FATFS("blk_enqueue_read after adjust: addr: 0x%lx sector: %u, count: %d ID: %d\n", read_data_offset, sddf_sector, sddf_count, handle);

    int err = blk_enqueue_req(&blk_queue, BLK_REQ_READ, read_data_offset, sddf_sector, sddf_count, handle);
    assert(!err);

    blk_request_pushed = true;
    wait_for_blk_resp();

    res = (DRESULT)(uintptr_t)microkit_cothread_my_arg();
    memcpy(buff, blk_data + read_data_offset + sector_size * MOD_POWER_OF_2(sector, sector_per_transfer), sector_size * count);
    return res;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    DRESULT res;
    int handle = microkit_cothread_my_handle();
    // Substract the handle with one as the worker thread ID starts at 1, not 0
    uint64_t write_data_offset = thread_blk_addr[handle - 1];
    uint16_t sector_size = blk_storage_info->sector_size;
    if (sector_size == BLK_TRANSFER_SIZE) {
        assert(MUL_POWER_OF_2(count, BLK_TRANSFER_SIZE) <= max_cluster_size);

        memcpy(blk_data + write_data_offset, buff, sector_size * count);
        int err = blk_enqueue_req(&blk_queue, BLK_REQ_WRITE, write_data_offset, sector, count,handle);
        assert(!err);
    }
    else {
        uint16_t sector_per_transfer = DIV_POWER_OF_2(BLK_TRANSFER_SIZE, sector_size);
        uint32_t sddf_sector = DIV_POWER_OF_2(sector, sector_per_transfer);
        int32_t sddf_count = 0;
        uint32_t unaligned_head_sector = MOD_POWER_OF_2(sector_per_transfer - MOD_POWER_OF_2(sector, sector_per_transfer), sector_per_transfer);
        uint32_t unaligned_tail_sector = MOD_POWER_OF_2(sector + count, sector_per_transfer);
        if (unaligned_head_sector) {
            sddf_count += 1;
        }
        if (unaligned_tail_sector) {
            sddf_count += 1;
        }

        int32_t aligned_sector = count - unaligned_head_sector - unaligned_tail_sector;
        sddf_count += DIV_POWER_OF_2(aligned_sector, sector_per_transfer);

        assert(MUL_POWER_OF_2(sddf_count, BLK_TRANSFER_SIZE) <= max_cluster_size);

        LOG_FATFS("blk_enqueue_write pre adjust: addr: 0x%lx sector: %u, count: %u ID: %d buffer_addr_in_fs: 0x%p\n", write_data_offset, sector, count, handle, buff);
        LOG_FATFS("blk_enqueue_write after adjust: addr: 0x%lx sector: %u, count: %d ID: %d\n", write_data_offset, sddf_sector, sddf_count, handle);

        // When there is no unaligned sector, we do not need to send a read request
        if (unaligned_head_sector == 0 && unaligned_tail_sector == 0) {
            memcpy(blk_data + write_data_offset, buff, sector_size * count);
            int err = blk_enqueue_req(&blk_queue, BLK_REQ_WRITE, write_data_offset, sddf_sector, sddf_count,handle);
            assert(!err);
        }
        else {
            int err = blk_enqueue_req(&blk_queue, BLK_REQ_READ, write_data_offset, sddf_sector, sddf_count,handle);
            assert(!err);
            blk_request_pushed = true;
            wait_for_blk_resp();
            res = (DRESULT)(uintptr_t)microkit_cothread_my_arg();
            // If the disk operation is not successful, stop here.
            if (res != RES_OK) {
                return res;
            }
            memcpy(blk_data + write_data_offset + sector_size * MOD_POWER_OF_2(sector, sector_per_transfer), buff, sector_size * count);
            err = blk_enqueue_req(&blk_queue, BLK_REQ_WRITE, write_data_offset, sddf_sector, sddf_count,handle);
            assert(!err);
        }
    }
    blk_request_pushed = true;
    wait_for_blk_resp();
    res = (DRESULT)(uintptr_t)microkit_cothread_my_arg();
    return res;
}
