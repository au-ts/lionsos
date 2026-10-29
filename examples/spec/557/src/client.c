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

#ifdef BENCH_557
#include <557.h>
#endif

__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;
__attribute__((__section__(".pager_client_config"))) pager_client_config_t pager_config;

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

    /* argv[1] is the .xz; control names the tar it decompresses to, whose
     * SHA-512 is argv[3]. argv[2] is the input buffer size in MiB. */
    // char *argv_557[] = {
    //     "xz",
    //     "cpu2006docs.tar.xz",
    //     "4",
    //     "055ce243071129412e9dd0b3b69a21654033a9b723d874b2015c774fac1553d9713be561ca86f74e4f16f22e664fc17a79f30caa5ad2c04fbc447549c2810fae",
    //     "1548636",
    //     "1555348",
    //     "0",
    // };
    // fivefiveseven(sizeof(argv_557) / sizeof(argv_557[0]), argv_557);

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
