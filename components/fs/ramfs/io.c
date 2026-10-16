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
#include "ff.h"
#include "diskio.h"
#include "decl.h"

#define IS_POWER_OF_2(x) ((x) && !((x) & ((x) - 1)))


DSTATUS disk_initialize (
    BYTE pdrv                /* Physical drive number to identify the drive */
)
{
    // do nothing unless I support multiple ramfses.
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
        // do nothing
    }
    return res;
}

#define MOD_POWER_OF_2(a, b) ((a) & ((b) - 1))
#define DIV_POWER_OF_2(a, b) ((a) >> (__builtin_ctz(b)))
#define MUL_POWER_OF_2(a, b) ((a) << (__builtin_ctz(b)))

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
}
