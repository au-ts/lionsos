#include <microkit.h>
#include <sddf/blk/config.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "ext4_blockdev.h"
#include <test_lwext4.h>

#ifndef MICROKIT_LIBC_HEAP_SIZE
#define HEAP_SIZE (0x200000)
#endif

char __heap_start[HEAP_SIZE] __attribute__((__aligned__(16)));
char __heap_end[1];

void io_timings_clear() {}
uint32_t tim_get_ms(void) { return 0; }

const struct ext4_io_stats *io_timings_get(uint32_t time_sum_ms)
{
    return NULL;
}

/* Setup for getting printf functionality working */
static int
libc_microkit_putc(char c, FILE *file)
{
    (void) file; /* Not used by us */
    microkit_dbg_putc(c);
    return c;
}

/*
 * We don't have a getc function in default Microkti environments, so we only
 * pass a putc function. We don't need flush in this case, so the third argument
 * is also NULL.
 */
static FILE __stdio = FDEV_SETUP_STREAM(libc_microkit_putc,
                    NULL,
                    NULL,
                    _FDEV_SETUP_WRITE);
FILE *const stdin = &__stdio; __strong_reference(stdin, stdout); __strong_reference(stdin, stderr);

// @ivanv: I could not find a default implementation of `_exit` from picolibc
// (except for one specific arch). This is fairly simple to define and for
// getting printf working we already have to some setup. What we'll probably
// have is some default libc.c that does everything Microkit specific that also
// gets compiled into the final libc.a that gets linked with user-programs.
void _exit (int __status) {
    while (1) {}
}


__attribute__((__section__(".blk_client_config"))) blk_client_config_t blk_config;

blk_storage_info_t *storage_info;
blk_queue_handle_t blk_queue;

static struct ext4_blockdev *bd;
static struct ext4_bcache *bc;

void init(void) {
    microkit_dbg_puts("starting EXT4\n");

    blk_queue_init(&blk_queue, blk_config.virt.req_queue.vaddr, blk_config.virt.resp_queue.vaddr, blk_config.virt.num_buffers);

    storage_info = blk_config.virt.storage_info.vaddr;
    while (!blk_storage_is_ready(storage_info));

    bd = ext4_blockdev_get();

    if (!test_lwext4_mount(bd, bc)) {
        printf("EXT4 ERROR: mount failed\n");
    }

    test_lwext4_cleanup();

    test_lwext4_dir_ls("/mp/");

    if (!test_lwext4_dir_test(10)) {
        printf("EXT4 ERROR: dir test failed\n");
    }
}

void notified(microkit_channel ch) {}
