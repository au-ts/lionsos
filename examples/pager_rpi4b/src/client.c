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

#ifdef BENCH_505
#include <505.h>
#endif
#ifdef BENCH_519
#include <519.h>
#endif
#ifdef BENCH_544
#include <544.h>
#endif
#ifdef BENCH_557
#include <557.h>
#endif

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

    /*
     * The SPEC CPU benchmarks. Each argv below is that benchmark's test
     * workload command, taken from its data/test/input/control, and its input
     * files are packed into the ramfs root by pager_rpi4b.mk. Uncomment one at
     * a time: they run for a long time, and the instrumentation below is
     * aligned to the faults minor_pf takes.
     */
    // char *argv_505[] = {
    //     "mcf",
    //     "inp.in",
    // };
    // fivezerofive(sizeof(argv_505) / sizeof(argv_505[0]), argv_505);

    // char *argv_519[] = {
    //     "lbm",
    //     "1",
    //     "reference.dat",
    //     "0",
    //     "1",
    //     "100_100_130_cf_a.of",
    // };
    // fiveonenine(sizeof(argv_519) / sizeof(argv_519[0]), argv_519);

    // char *argv_544[] = {
    //     "nab",
    //     "hkrdenq",
    //     "1930344093",
    //     "1000",
    // };
    // fivefourfour(sizeof(argv_544) / sizeof(argv_544[0]), argv_544);

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

    #ifdef PAGER_INSTRUMENTATION
    // get the pager to send us the instrumentation data
    uint64_t samples = 0;
    uint64_t read_samples = 0;
    uint64_t write_samples = 0;
    uint64_t total_read_mapping = 0;
    uint64_t total_write_mapping = 0;
    uint64_t discarded = 0;
    for (uint64_t i = 0;; i++) {
        microkit_mr_set(0, i);
        microkit_msginfo reply = microkit_ppcall(pager_config.id,
                                                 microkit_msginfo_new(PAGER_INSTRUMENTATION_TAG, 1));
        uint64_t total = microkit_mr_get(0);
        if (microkit_msginfo_get_count(reply) < 4 || i >= total) {
            printf("instrumentation: %lu samples\n", (uint64_t)total);
            if (total != fault_idx) {
                printf("instrumentation: WARNING pager recorded %lu faults but the "
                       "benchmark recorded %lu -- samples are not aligned\n",
                       (uint64_t)total, (uint64_t)fault_idx);
            }
            break;
        }
        /* A warmup fault: the slot only exists to keep the indices aligned. */
        if (before_faults[i] == 0) {
            ++discarded;
            continue;
        }
        ++samples;
        if (i < minor_pf_read_end) {
            total_read_mapping += (uint64_t)microkit_mr_get(1);
            ++read_samples;
        } else {
            total_write_mapping += (uint64_t)microkit_mr_get(1);
            ++write_samples;
        }

        total_mapping_latency += (uint64_t)microkit_mr_get(1);
        total_client_pager += (uint64_t)microkit_mr_get(2) - before_faults[i];
        total_pager_client += after_faults[i] - (uint64_t)microkit_mr_get(3);
    }

    uint64_t freq = read_cntfrq();

    long double timer_checks = 0;
    for (int i = 0; i < 100; ++i) {
        uint64_t start_timer_check = read_cntpct();
        uint64_t end_timer_check = read_cntpct();
        timer_checks += end_timer_check - start_timer_check;
    }
    timer_checks /= 100;

    printf("instrumentation: %lu measured, %lu warmup (discarded)\n", samples, discarded);
    printf("results:\n");
    long double read_mapping = ticks_to_ns((long double)total_read_mapping / read_samples - timer_checks, freq);
    long double write_mapping = ticks_to_ns((long double)total_write_mapping / write_samples - timer_checks, freq);
    long double mean_mapping = ticks_to_ns((long double)total_mapping_latency / samples - timer_checks, freq);
    long double mean_pager_client = ticks_to_ns((long double)total_pager_client / samples - timer_checks, freq);
    long double mean_client_pager = ticks_to_ns((long double)total_client_pager / samples - timer_checks, freq);
    printf("mean read mapping latency:  %.3Lf ns\n", read_mapping);
    printf("mean write mapping latency: %.3Lf ns\n", write_mapping);
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
