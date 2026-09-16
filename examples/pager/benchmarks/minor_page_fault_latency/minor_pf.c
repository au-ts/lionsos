#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "minor_pf.h"
#include <sys/mman.h>
#define PAGE_SIZE       4096ULL

#define NUM_PAGES       16384ULL

#define TEST_VADDR_R      0x40000000ULL
#define TEST_VADDR_W      0x50000000ULL


#define WARMUP_SAMPLES  10


/*
 * The cycle counter rather than cntpct: cntpct is 62.5MHz on qemu and 8MHz on
 * maaxboard, which is only a handful of ticks per fault. seL4 runs and exports
 * PMCCNTR_EL0 to EL0 in the benchmark config (CONFIG_EXPORT_PMU_USER).
 */
static inline uint64_t read_ccnt(void)
{
    uint64_t value;

    asm volatile(
        "isb\n"
        "mrs %0, pmccntr_el0"
        : "=r"(value)
        :
        : "memory"
    );

    return value;
}

static inline uint64_t read_cntpct(void)
{
    uint64_t value;

    asm volatile(
        "mrs %0, cntpct_el0"
        : "=r"(value)
    );

    return value;
}

static inline uint64_t read_cntfrq(void)
{
    uint64_t value;

    asm volatile(
        "mrs %0, cntfrq_el0"
        : "=r"(value)
    );

    return value;
}

static uint64_t ticks_to_ns(uint64_t ticks, uint64_t freq)
{
    return (ticks * 1000000000ULL) / freq;
}

/* How many cycles a second is, measured rather than assumed so the ns column
 * means something on both qemu and maaxboard. */
static uint64_t measure_ccnt_freq(uint64_t freq)
{
    uint64_t ticks = freq / 100;
    uint64_t t0 = read_cntpct();
    uint64_t c0 = read_ccnt();

    while (read_cntpct() - t0 < ticks) {
    }

    return (read_ccnt() - c0) * 100;
}

static void print_statistics(const char *name,
                             uint64_t *samples,
                             size_t count,
                             uint64_t ccnt_freq)
{
    uint64_t min = UINT64_MAX;
    uint64_t max = 0;
    uint64_t sum = 0;
    for (size_t i = 0; i < count; i++) {
        uint64_t x = samples[i];

        if (x < min)
            min = x;

        if (x > max)
            max = x;

        sum += x;
    }
    for (size_t i = 1; i < count; i++) {
        uint64_t key = samples[i];
        size_t j = i;

        while (j > 0 && samples[j - 1] > key) {
            samples[j] = samples[j - 1];
            j--;
        }

        samples[j] = key;
    }
    uint64_t median;
    if (count & 1) {
        median = samples[count / 2];
    } else {
        median =
            (samples[count / 2 - 1] +
             samples[count / 2]) / 2;
    }
    uint64_t mean = sum / count;

    printf("\n%s\n", name);
    printf("-----------------------------\n");
    printf("samples : %zu\n", count);
    printf("min     : %lu cycles (%lu ns)\n", min, ticks_to_ns(min, ccnt_freq));
    printf("median  : %lu cycles (%lu ns)\n", median, ticks_to_ns(median, ccnt_freq));
    printf("mean    : %lu cycles (%lu ns)\n", mean, ticks_to_ns(mean, ccnt_freq));
    printf("max     : %lu cycles (%lu ns)\n", max, ticks_to_ns(max, ccnt_freq));
}

static void benchmark_read(uint64_t ccnt_freq)
{
    static uint64_t samples[NUM_PAGES];

    /* samples lives in .bss and the client is not backed, so touch it before
     * timing or every 512th sample carries a fault of its own. */
    memset(samples, 0, sizeof(samples));

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

    for (size_t i = 0; i < NUM_PAGES; i++) {
        volatile uint8_t *page =
            base + i * PAGE_SIZE;

        uint64_t start = read_ccnt();
        /*
         * This is the operation that causes the page fault.
         */
        asm volatile("" ::: "memory");
        uint8_t value = *page;
        asm volatile("" ::: "memory");

        uint64_t end = read_ccnt();

        /*
         * Prevent the compiler from eliminating the load.
         */
        asm volatile("" :: "r"(value) : "memory");

        samples[i] = end - start;
    }

    print_statistics(
        "Anonymous READ page faults",
        samples + WARMUP_SAMPLES,
        NUM_PAGES - WARMUP_SAMPLES,
        ccnt_freq
    );
}


static void benchmark_write(uint64_t ccnt_freq)
{
    static uint64_t samples[NUM_PAGES];

    memset(samples, 0, sizeof(samples));

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

    for (size_t i = 0; i < NUM_PAGES; i++) {

        volatile uint8_t *page =
            base + i * PAGE_SIZE;

        uint64_t start = read_ccnt();
        /*
         * This is the operation that causes the page fault.
         */
        asm volatile("" ::: "memory");
        *page = 42;
        asm volatile("" ::: "memory");
        uint64_t end = read_ccnt();

        samples[i] = end - start;
    }

    print_statistics(
        "Anonymous WRITE page faults",
        samples + WARMUP_SAMPLES,
        NUM_PAGES - WARMUP_SAMPLES,
        ccnt_freq
    );
}


int minor_pf(void)
{
    uint64_t freq = read_cntfrq();
    uint64_t ccnt_freq = measure_ccnt_freq(freq);

    printf("Anonymous page-fault latency benchmark\n");
    printf("========================================\n");

    printf("CNTFRQ_EL0 : %lu Hz\n", freq);
    printf("PMCCNTR    : %lu Hz\n", ccnt_freq);
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

    benchmark_read(ccnt_freq);

    benchmark_write(ccnt_freq);

    return 0;
}