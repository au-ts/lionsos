/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sddf/util/util.h>
#include "ff.h"
#include "diskio.h"

#define SECTOR_SIZE 512
#define CPIO_HDR_SIZE 110
#define CPIO_MODE 14
#define CPIO_FILESIZE 54
#define CPIO_NAMESIZE 94

static DSTATUS status = STA_NOINIT;
static bool started;

/**
 * TODO:
 * add more fields if necessary.
 */
typedef struct {
    char *name;
    uint32_t mode;
    uint32_t size;
    char *data;
    uint64_t next;
} cpio_entry_t;

static uint32_t cpio_field(const char *p)
{
    uint32_t val = 0;
    for (int i = 0; i < 8; i++) {
        val = val * 16 + (p[i] <= '9' ? p[i] - '0' : (p[i] | 0x20) - 'a' + 10);
    }
    return val;
}

static bool cpio_entry(char *cpio, uint64_t off, cpio_entry_t *e)
{
    char *hdr = cpio + off;
    if (memcmp(hdr, "070701", 6) != 0) {
        return false;
    }
    uint64_t data = ALIGN(off + CPIO_HDR_SIZE + cpio_field(hdr + CPIO_NAMESIZE), 4);
    e->name = hdr + CPIO_HDR_SIZE;
    e->mode = cpio_field(hdr + CPIO_MODE);
    e->size = cpio_field(hdr + CPIO_FILESIZE);
    e->data = cpio + data;
    e->next = ALIGN(data + e->size, 4);
    return true;
}

static FRESULT mkdir_parents(char *path)
{
    FRESULT res = FR_OK;
    for (char *p = strchr(path, '/'); p && (res == FR_OK || res == FR_EXIST); p = strchr(p + 1, '/')) {
        *p = '\0';
        res = f_mkdir(path);
        *p = '/';
    }
    return (res == FR_EXIST) ? FR_OK : res;
}

/**
 * Move prefilled CPIO to back, then initialises fatfs on memory region,
 * WARNING: memory region should be sufficiently big
 * enter in start of prefilled memory region and size of memory region
 */
static bool ramfs_build(char *disk, uint64_t disk_size)
{
    // move cpio to the back of memory region.
    cpio_entry_t e;
    uint64_t len = 0;
    do {
        if (!cpio_entry(disk, len, &e)) {
            return false;
        }
        len = e.next;
    } while (strcmp(e.name, "TRAILER!!!") != 0);
    uint64_t cpio_off = disk_size - len;
    char *cpio = disk + cpio_off;
    memmove(cpio, disk, len);

    // make the fatfs on disk
    static BYTE work[FF_MAX_SS];
    MKFS_PARM opt = { .fmt = FM_FAT | FM_FAT32 | FM_SFD, .n_fat = 1, .align = 1, .au_size = 4096 };
    DWORD nfree;
    FATFS *fs;
    if (f_mkfs("", &opt, work, sizeof(work)) != FR_OK || f_getfree("", &nfree, &fs) != FR_OK) {
        return false;
    }
    static FIL file;
    for (uint64_t off = 0;; off = e.next) {
        if (!cpio_entry(cpio, off, &e)) {
            return false;
        }
        if (strcmp(e.name, "TRAILER!!!") == 0) {
            return true;
        }
        FRESULT res = mkdir_parents(e.name);
        if (res == FR_OK && S_ISDIR(e.mode)) {
            res = f_mkdir(e.name);
        } else if (res == FR_OK && S_ISREG(e.mode)) {
            UINT written;
            res = f_open(&file, e.name, FA_WRITE | FA_CREATE_ALWAYS);
            if (res == FR_OK) {
                res = f_write(&file, e.data, e.size, &written);
                f_close(&file);
            }
        }
        if (res != FR_OK) {
            return false;
        }
    }
}

DSTATUS disk_initialize(BYTE pdrv)
{
    if (!started) {
        started = true;
        status = 0;
        if (!ramfs_build()) {
            status = STA_NOINIT;
        }
    }
    return status;
}

DSTATUS disk_status(BYTE pdrv)
{
    return status;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (cmd == GET_SECTOR_COUNT) {
        *(LBA_t *)buff = disk_size / SECTOR_SIZE;
    } else if (cmd == GET_SECTOR_SIZE) {
        *(WORD *)buff = SECTOR_SIZE;
    }
    return RES_OK;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    memcpy(buff, (char *)disk_start_addr + (uint64_t)sector * SECTOR_SIZE, (size_t)count * SECTOR_SIZE);
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    memmove((char *)disk_start_addr + (uint64_t)sector * SECTOR_SIZE, buff, (size_t)count * SECTOR_SIZE);
    return RES_OK;
}