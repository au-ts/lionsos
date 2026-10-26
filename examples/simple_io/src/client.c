/*
 * Copyright 2026, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */


#include <microkit.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>
#include <sddf/timer/config.h>
#include <lions/fs/config.h>
#include <lions/fs/protocol.h>
#include <lions/posix/posix.h>
#include <fat_config.h>
#ifdef RAM_FATFS
#include <lions/fs/ramfs.h>
#endif

#include <simple_io.h>

__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;

serial_queue_handle_t serial_tx_queue_handle;


static char libc_heap[0x100000];


timer_client_config_t timer_config;
#ifndef RAM_FATFS
// file.c's file system client helpers refer to these.
fs_client_config_t fs_config;
fs_queue_t *fs_command_queue;
fs_queue_t *fs_completion_queue;
char *fs_share;
#endif

void init(void)
{
    assert(serial_config_check_magic(&serial_config));

    serial_queue_init(&serial_tx_queue_handle, serial_config.tx.queue.vaddr, serial_config.tx.data.size,
                      serial_config.tx.data.vaddr);

    libc_init(NULL, libc_heap, sizeof(libc_heap));

    #ifdef RAM_FATFS
    int err = handle_initialise();
    if (err != FS_STATUS_SUCCESS) {
        printf("CLIENT|ERROR: failed to mount ramfs (%d)\n", err);
        return;
    }
    #endif

    if (simple_io()) {
        printf("benchmark done, could not open the benchmark file\n");
    }
}

void notified(microkit_channel ch)
{
}

seL4_MessageInfo_t protected(microkit_channel ch, microkit_msginfo msginfo)
{
    return microkit_msginfo_new(0, 0);
}

seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo)
{
    return seL4_False;
}
