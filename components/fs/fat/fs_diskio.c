#include "ff15/source/ff.h"
#include "ff15/source/diskio.h"
#include "fatfs_decl.h"
#include <stdbool.h>
#include <stdint.h>
#include <sddf/blk/queue.h>
#include <string.h>
#include "co_helper.h"
#include "fatfs_config.h"
#include <config/blk_config.h>

#ifdef FS_DEBUG_PRINT
#include <sddf/util/printf.h>
#endif

#define SD 0 /* Map SD card to physical drive 0 */

extern blk_queue_handle_t *blk_queue_handle;

extern bool blk_request_pushed;

// This is the offset of the data buffer shared between file system and blk device driver
extern uint64_t fs_metadata;

extern blk_storage_info_t *config;

extern uint64_t blk_data_region;

#ifdef MEMBUF_STRICT_ALIGN_TO_BLK_TRANSFER_SIZE

/*
 *  This def restrict the maximum cluster size that the fatfs can have if MEMBUF_STRICT_ALIGN_TO_BLK_TRANSFER_SIZE is enabled. 
 *  This restriction should not cause any problem as long as the BLK_REGION_SIZE between file system and blk virt is not
 *  too small. For example, 32GB - 256TB disks are recommended to have a sector size of 128KB, and if you have 4 worker coroutines, 
 *  BLK_REGION_SIZE should be bigger than 512KB. In fileio example, BLK_REGION_SIZE is set to 2 MB.
 */
#define MAX_CLUSTER_SIZE (BLK_REGION_SIZE / WORKER_COROUTINE_NUM)

uint64_t coroutine_blk_addr[WORKER_COROUTINE_NUM];
#endif

DSTATUS disk_initialize (
	BYTE pdrv				/* Physical drive number to identify the drive */
)
{
	DSTATUS stat;
	int result;

	#ifdef MEMBUF_STRICT_ALIGN_TO_BLK_TRANSFER_SIZE
	// coroutine_blk_addr[0] is not initialized as that is the slot for event coroutine
	for (uint16_t i = 0; i < WORKER_COROUTINE_NUM; i++) {
		coroutine_blk_addr[i] = i * MAX_CLUSTER_SIZE;
	}
	#endif

	switch (pdrv) {
	default:
		#ifdef FS_DEBUG_PRINT
		sddf_printf("Block Storage Information:\n");
		sddf_printf("--------------------------\n");
		sddf_printf("Serial Number: %s\n", config->serial_number);
		sddf_printf("Read-Only: %s\n", config->read_only ? "Yes" : "No");
		sddf_printf("Ready: %s\n", config->ready ? "Yes" : "No");
		sddf_printf("Sector Size: %u bytes\n", config->sector_size);
		sddf_printf("Optimal Block Size: %u units (%u bytes)\n", 
			config->block_size, config->block_size * BLK_TRANSFER_SIZE);
		sddf_printf("Queue Depth: %u\n", config->queue_depth);
		sddf_printf("Geometry:\n");
		sddf_printf("  Cylinders: %u\n", config->cylinders);
		sddf_printf("  Heads: %u\n", config->heads);
		sddf_printf("  Blocks: %u\n", config->blocks);
		sddf_printf("Total Capacity: %llu units (%llu bytes)\n", 
			(unsigned long long)config->capacity, 
			(unsigned long long)(config->capacity * BLK_TRANSFER_SIZE));
		sddf_printf("--------------------------\n");
		#endif
		return RES_OK;
	}
	return STA_NOINIT;
}

DSTATUS disk_status (
	BYTE pdrv		/* Physical drive nmuber to identify the drive */
)
{
	DSTATUS stat;
	int result;

	switch (pdrv) {
	default:
		return RES_OK;
	}
	return STA_NOINIT;
}

DRESULT disk_ioctl (BYTE pdrv, BYTE cmd, void* buff) {
	DRESULT res;
	switch (pdrv) {
	default:
		if (cmd == GET_SECTOR_SIZE) {
			WORD *size = buff;
			// The sector size should be a mutiple of 512, BLK_TRANSFER_SIZE % SECTOR_SIZE should be 0
			// BLK_TRANSFER_SIZE % SECTOR_SIZE should be a power of 2
			*size = config->sector_size;
			res = RES_OK;
		}
		if (cmd == CTRL_SYNC) {
			res = RES_OK;
			#ifdef FS_DEBUG_PRINT
			sddf_printf("blk_enqueue_syncreq\n");
			#endif
			blk_enqueue_req(blk_queue_handle, BLK_REQ_FLUSH, 0, 0, 0, co_get_handle());
			blk_request_pushed = true;
			co_block();
			res = (DRESULT)(uintptr_t)co_get_args();
		}
	}
	return res;
}

#ifdef MEMBUF_STRICT_ALIGN_TO_BLK_TRANSFER_SIZE

#define MOD_POWER_OF_2(a, b) ((a) & ((b) - 1))
#define DIV_POWER_OF_2(a, b) ((a) >> (__builtin_ctz(b)))

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
	DRESULT res;
	switch (pdrv) {
		default: {
			int handle = co_get_handle();
			// Accroding the protocol, all the read/write addr passed to the blk_virt should be page aligned
			// Substract the handle with one as the work coroutine ID starts at 1, not 0
			uint64_t read_data_offset = coroutine_blk_addr[handle - 1];
			uint16_t sector_size = config->sector_size;
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

			#ifdef FS_DEBUG_PRINT
			sddf_printf("blk_enqueue_read pre adjust: addr: 0x%lx sector: %u, count: %u ID: %d\n", read_data_offset, sector, count, handle);
			sddf_printf("blk_enqueue_read after adjust: addr: 0x%lx sector: %u, count: %d ID: %d\n", read_data_offset, sddf_sector, sddf_count, handle);
			#endif
			blk_enqueue_req(blk_queue_handle, BLK_REQ_READ, read_data_offset, sddf_sector, sddf_count, handle);

			blk_request_pushed = true;
			co_block();

			res = (DRESULT)(uintptr_t)co_get_args();
			memcpy(buff, (void*)(read_data_offset + blk_data_region + sector_size * MOD_POWER_OF_2(sector, sector_per_transfer)), sector_size * count);
			#ifdef FS_DEBUG_PRINT
			// print_sector_data(buff, 512);
			#endif
			break;
		}
	}
    return res;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    DRESULT res;
	switch (pdrv) {
		default: {
			int handle = co_get_handle();
			// Substract the handle with one as the work coroutine ID starts at 1, not 0
			uint64_t write_data_offset = coroutine_blk_addr[handle - 1];
			uint16_t sector_size = config->sector_size;
			if (sector_size == BLK_TRANSFER_SIZE) {
				memcpy((void*)(write_data_offset + blk_data_region), buff, sector_size * count);
				blk_enqueue_req(blk_queue_handle, BLK_REQ_WRITE, write_data_offset, sector, count,handle);
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

				#ifdef FS_DEBUG_PRINT
				sddf_printf("blk_enqueue_write pre adjust: addr: 0x%lx sector: %u, count: %u ID: %d buffer_addr_in_fs: 0x%p\n", write_data_offset, sector, count, handle, buff);
				sddf_printf("blk_enqueue_write after adjust: addr: 0x%lx sector: %u, count: %d ID: %d\n", write_data_offset, sddf_sector, sddf_count, handle);
				#endif
				// When there is no unaligned sector, we do not need to send a read request
				if (unaligned_head_sector == 0 && unaligned_tail_sector == 0) {
					memcpy((void*)(write_data_offset + blk_data_region), buff, sector_size * count);
					blk_enqueue_req(blk_queue_handle, BLK_REQ_WRITE, write_data_offset, sddf_sector, sddf_count,handle);
				}
				else {
					blk_enqueue_req(blk_queue_handle, BLK_REQ_READ, write_data_offset, sddf_sector, sddf_count,handle);
					blk_request_pushed = true;
					co_block();
					res = (DRESULT)(uintptr_t)co_get_args();
					// If the disk operation is not successful, stop here.
					if (res != RES_OK) {
						break;
					}
					memcpy((void*)(write_data_offset + blk_data_region + sector_size * MOD_POWER_OF_2(sector, sector_per_transfer)), buff, sector_size * count);
					blk_enqueue_req(blk_queue_handle, BLK_REQ_WRITE, write_data_offset, sddf_sector, sddf_count,handle);
				}
			}
			blk_request_pushed = true;
			co_block();
			res = (DRESULT)(uintptr_t)co_get_args();
			break;
		}
	}
    return res;
}

#else
DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    DRESULT res;
	switch (pdrv) {
		default: {
			uint64_t read_data_offset = (uint64_t)buff - fs_metadata;
			#ifdef FS_DEBUG_PRINT
			sddf_printf("blk_enqueue_read: addr: 0x%lx sector: %u, count: %u ID: %d\n", read_data_offset, sector, count, co_get_handle());
			#endif
			blk_enqueue_req(blk_queue_handle, BLK_REQ_READ, read_data_offset, sector, count,co_get_handle());
			blk_request_pushed = true;
			co_block();
			res = (DRESULT)(uintptr_t)co_get_args();
			#ifdef FS_DEBUG_PRINT
			// print_sector_data(buff, 512);
			#endif
			break;
		}
	}
    return res;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    DRESULT res;
	switch (pdrv) {
		default: {
			uint64_t write_data_offset = (uint64_t)buff - fs_metadata;
			#ifdef FS_DEBUG_PRINT
			sddf_printf("blk_enqueue_write: addr: 0x%lx sector: %u, count: %u ID: %d buffer_addr_in_fs: 0x%p\n", write_data_offset, sector, count, co_get_handle(), buff);
			#endif
			blk_enqueue_req(blk_queue_handle, BLK_REQ_WRITE, write_data_offset, sector, count,co_get_handle());
			blk_request_pushed = true;
			co_block();
			res = (DRESULT)(uintptr_t)co_get_args();
			break;
		}
	}
    return res;
}
#endif