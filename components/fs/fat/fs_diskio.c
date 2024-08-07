#include "ff15/source/ff.h"
#include "ff15/source/diskio.h"
#include "fatfs_decl.h"
#include <stdbool.h>
#include <stdint.h>
#include "../../../dep/sddf/include/sddf/blk/queue.h"
#include "FiberPool/FiberPool.h"

#ifdef FS_DEBUG_PRINT
#include "../../../dep/sddf/include/sddf/util/printf.h"
#endif

#define SD 0 /* Map SD card to physical drive 0 */

extern blk_queue_handle_t *blk_queue_handle;

extern bool blk_request_pushed;

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
            *size = 512;
            res = RES_OK;
        }
        if (cmd == CTRL_SYNC) {
			res = RES_OK;
			#ifdef FS_DEBUG_PRINT
			sddf_printf("blk_enqueue_syncreq\n");
			#endif
			blk_enqueue_req(blk_queue_handle, FLUSH, 0, 0, 0,Get_Cohandle());
			blk_request_pushed = true;
			Fiber_block();
			res = (DRESULT)(uintptr_t)Fiber_GetArgs();
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
			#ifdef FS_DEBUG_PRINT
			sddf_printf("blk_enqueue_read: addr: 0x%lx sector: %u, count: %u ID: %d\n", (uintptr_t)buff, sector, count, Get_Cohandle());
			#endif
			blk_enqueue_req(blk_queue_handle, READ_BLOCKS, (uintptr_t)buff, sector, count,Get_Cohandle());
			blk_request_pushed = true;
			Fiber_block();
			res = (DRESULT)(uintptr_t)Fiber_GetArgs();
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
			#ifdef FS_DEBUG_PRINT
			sddf_printf("blk_enqueue_write: addr: 0x%lx sector: %u, count: %u ID: %d\n", (uintptr_t)buff, sector, count, Get_Cohandle());
			#endif
			blk_enqueue_req(blk_queue_handle, WRITE_BLOCKS, (uintptr_t)buff, sector, count,Get_Cohandle());
			blk_request_pushed = true;
			Fiber_block();
			res = (DRESULT)(uintptr_t)Fiber_GetArgs();
			break;
		}
	}
    return res;
}