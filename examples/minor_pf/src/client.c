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

#ifdef PAGER_INSTRUMENTATION
#define TIMER_OVERHEAD_SAMPLES 1000

static long double timer_read_overhead(void)
{
    uint64_t total = 0;
    for (int i = 0; i < TIMER_OVERHEAD_SAMPLES; ++i) {
        uint64_t start = read_cntpct();
        uint64_t end = read_cntpct();
        total += end - start;
    }
    return (long double)total / TIMER_OVERHEAD_SAMPLES;
}

static void report_pager_instrumentation(void)
{
    uint64_t samples = 0;
    for (uint64_t i = 0;; i++) {
        microkit_mr_set(0, i);
        microkit_msginfo reply = microkit_ppcall(pager_config.id,
                                                 microkit_msginfo_new(PAGER_INSTRUMENTATION_TAG, 1));
        uint64_t total = microkit_mr_get(0);
        if (microkit_msginfo_get_count(reply) < 4 || i >= total) {
            printf("instrumentation: %lu samples\n", (uint64_t)total);
            break;
        }
        if (before_faults[i] == 0) {
            continue;
        }
        ++samples;
        #ifndef PAGER_INSTRUMENTATION_MAPREPLYRECV
        total_mapping_latency += (uint64_t)microkit_mr_get(1);
        #endif
        total_client_pager += (uint64_t)microkit_mr_get(2) - before_faults[i];
        total_pager_client += after_faults[i] - (uint64_t)microkit_mr_get(3);
    }
    if (samples == 0) {
        return;
    }

    uint64_t freq = read_cntfrq();
    long double overhead = timer_read_overhead();
    printf("results:\n");
    #ifdef PAGER_INSTRUMENTATION_MAPREPLYRECV
    printf("mean mapping latency:       n/a (mapreplyrecv)\n");
    #else
    printf("mean mapping latency:       %.3Lf ns\n",
           ticks_to_ns((long double)total_mapping_latency / samples - overhead, freq));
    #endif
    printf("mean pager->client latency: %.3Lf ns\n",
           ticks_to_ns((long double)total_pager_client / samples - overhead, freq));
    printf("mean client->pager latency: %.3Lf ns\n",
           ticks_to_ns((long double)total_client_pager / samples - overhead, freq));
}
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

    // check CPU frequency.
    uint64_t pmcr; asm volatile("mrs %0, pmcr_el0" : "=r"(pmcr));
    asm volatile("msr pmcr_el0, %0" :: "r"((pmcr | 1) & ~(1UL << 3))); 
    asm volatile("msr pmcntenset_el0, %0" :: "r"(1UL << 31));         
    uint64_t c0, c1, t0 = read_cntpct();
    asm volatile("isb; mrs %0, pmccntr_el0" : "=r"(c0));
    for (volatile uint64_t i = 0; i < 100000000; i++);
    asm volatile("isb; mrs %0, pmccntr_el0" : "=r"(c1));
    uint64_t t1 = read_cntpct();
    printf("CPU ~%lu MHz\n", (c1 - c0) * read_cntfrq() / (t1 - t0) / 1000000);

    minor_pf();

    #ifdef PAGER_INSTRUMENTATION
    report_pager_instrumentation();
    #endif

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
