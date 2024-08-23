#include "ff15/source/ff.h"
#include "ff15/source/diskio.h"
#include "fatfs_decl.h"
#include <stdbool.h>
#include <stdint.h>
#include "../../../dep/sddf/include/sddf/blk/queue.h"
#include "co_helper.h"
#include "fatfs_config.h"

#ifdef FS_DEBUG_PRINT
#include "../../../dep/sddf/include/sddf/util/printf.h"
#endif

#define SD 0 /* Map SD card to physical drive 0 */

extern blk_queue_handle_t *blk_queue_handle;

extern bool blk_request_pushed;

// This is the offset of the data buffer shared between file system and blk device driver
extern uint64_t fs_metadata;

extern blk_storage_info_t *config;

DSTATUS disk_initialize (
	BYTE pdrv				/* Physical drive number to identify the drive */
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
			// Hardcode for now
            *size = 512;
			#ifdef FS_DEBUG_PRINT
			sddf_printf("file system sector size: %d\n", config->sector_size);
			#endif
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

/*
	The disk operation part have not changed to deal with sector size and BLK_TRANSFER_SIZE
	Currently for testing file system, just set the BLK_TRANSFER_SIZE in blk queue.h to 512
    As all SD cards use 512 byte sector size
*/
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
			sddf_printf("blk_enqueue_write: addr: 0x%lx sector: %u, count: %u ID: %d\n", write_data_offset, sector, count, co_get_handle());
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