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
fs_client_config_t fs_config;
fs_queue_t *fs_command_queue;
fs_queue_t *fs_completion_queue;
char *fs_share;

void init(void)
{
    assert(serial_config_check_magic(&serial_config));
    assert(pager_config_check_magic(&pager_config));

    serial_queue_init(&serial_tx_queue_handle, serial_config.tx.queue.vaddr, serial_config.tx.data.size,
                      serial_config.tx.data.vaddr);


    libc_init(NULL, (void *)pager_config.mmap_base, 0x20000000);

    minor_pf();
    #ifdef PAGER_INSTRUMENTATION
    // get the pager to send us the instrumentation data
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
        ++samples;
        total_mapping_latency += (uint64_t)microkit_mr_get(1);
        total_client_pager += (uint64_t)microkit_mr_get(2) - before_faults[i]; 
        total_pager_client += after_faults[i] - (uint64_t)microkit_mr_get(3);
    }

    uint64_t freq = read_cntfrq();
    printf("results:\n");
    /* Divide as long double: an integer mean would drop the fraction of a tick. */
    long double mean_mapping = ticks_to_ns((long double)total_mapping_latency / samples, freq);
    long double mean_pager_client = ticks_to_ns((long double)total_pager_client / samples, freq);
    long double mean_client_pager = ticks_to_ns((long double)total_client_pager / samples, freq);
    printf("mean mapping latency:       %.3Lf ns\n", mean_mapping);
    printf("mean pager->client latency: %.3Lf ns\n", mean_pager_client);
    printf("mean client->pager latency: %.3Lf ns\n", mean_client_pager);
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
