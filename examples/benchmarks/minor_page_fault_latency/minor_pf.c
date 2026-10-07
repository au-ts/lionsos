#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <time.h>
#include "minor_pf.h"
#include <sys/mman.h>
#include "pager_instrumentation.h"
#define PAGE_SIZE       4096ULL

#define NUM_PAGES       16384ULL

#define TEST_VADDR_R      0x40000000ULL
#define TEST_VADDR_W      0x50000000ULL


#define WARMUP_SAMPLES  10

#ifdef PAGER_INSTRUMENTATION
uint64_t fault_idx = 0;
#endif


static void benchmark_read(uint64_t freq)
{
    volatile uint8_t *base =
        (volatile uint8_t *) mmap(
            (void *)TEST_VADDR_R,
            NUM_PAGES * PAGE_SIZE,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
            -1,
            0
        );

    printf("\nRunning READ benchmark...\n");
    uint64_t start = read_cntpct();
    for (size_t i = 0; i < NUM_PAGES; i++) {
        volatile uint8_t *page = base + i * PAGE_SIZE;

        #ifdef PAGER_INSTRUMENTATION
        before_faults[fault_idx] = read_cntpct();
        #endif
        uint8_t value = *page;
        #ifdef PAGER_INSTRUMENTATION
        after_faults[fault_idx] = read_cntpct();
        fault_idx++;
        #endif
    }
    uint64_t end = read_cntpct();

    long double average = ticks_to_ns(end - start, freq) / NUM_PAGES;
    printf("read average latency: %.3Lf ns\n", average);
}


static void benchmark_write(uint64_t freq)
{
    static uint64_t samples[NUM_PAGES];

    volatile uint8_t *base =
        (volatile uint8_t *)mmap(
            (void *)TEST_VADDR_W,
            NUM_PAGES * PAGE_SIZE,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
            -1,
            0
        );

    printf("\nRunning WRITE benchmark...\n");
    uint64_t start = read_cntpct();
    for (size_t i = 0; i < NUM_PAGES; i++) {

        volatile uint8_t *page =
            base + i * PAGE_SIZE;
        #ifdef PAGER_INSTRUMENTATION
        before_faults[fault_idx] = read_cntpct();
        #endif
        *page = 42;
        #ifdef PAGER_INSTRUMENTATION
        after_faults[fault_idx] = read_cntpct();
        fault_idx++;
        #endif
    }
    uint64_t end = read_cntpct();

    long double average = ticks_to_ns(end - start, freq) / NUM_PAGES;
    printf("write average latency: %.3Lf ns\n", average);
}


int minor_pf(void)
{
    uint64_t freq = read_cntfrq();
    printf("pf latency benchmark:\n");
    printf("CNTFRQ_EL0 : %lu Hz\n", freq);
    printf("page size  : %lu bytes\n", PAGE_SIZE);
    printf("pages      : %lu\n", NUM_PAGES);
    printf("size       : %lu MiB\n",
           (NUM_PAGES * PAGE_SIZE) / (1024 * 1024));

    /*
     * IMPORTANT:
     *
     * The region must start completely unmapped.
     *
     * Do not touch TEST_VADDR before benchmark_read().
     */

    benchmark_read(freq);

    benchmark_write(freq);

    return 0;
}