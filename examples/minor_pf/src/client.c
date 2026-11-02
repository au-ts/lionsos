#include <microkit.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>
#include <sddf/timer/config.h>
#include <lions/fs/config.h>
#include <lions/fs/protocol.h>
#include <lions/pager/config.h>
#include <lions/posix/posix.h>
#include <fat_config.h>
#ifdef RAM_FATFS
#include <lions/fs/ramfs.h>
#endif

#include <minor_pf.h>
#include <pager_instrumentation.h>

__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;
__attribute__((__section__(".pager_client_config"))) pager_client_config_t pager_config;

#ifdef PAGER_INSTRUMENTATION
uint64_t total_mapping_latency = 0;
uint64_t total_pager_client = 0;
uint64_t total_client_pager = 0;
uint64_t before_faults[PAGER_MAX_SAMPLES];
uint64_t after_faults[PAGER_MAX_SAMPLES];
#endif

serial_queue_handle_t serial_tx_queue_handle;


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
    assert(pager_config_check_magic(&pager_config));

    serial_queue_init(&serial_tx_queue_handle, serial_config.tx.queue.vaddr, serial_config.tx.data.size,
                      serial_config.tx.data.vaddr);


    libc_init(NULL, (void *)pager_config.mmap_base, 0x20000000);

    #ifdef RAM_FATFS
    int err = handle_initialise();
    if (err != FS_STATUS_SUCCESS) {
        printf("CLIENT|ERROR: failed to mount ramfs (%d)\n", err);
        return;
    }
    #endif

    minor_pf();

    printf("benchmark done\n");
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
