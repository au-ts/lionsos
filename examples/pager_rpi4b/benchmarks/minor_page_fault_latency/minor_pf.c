#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <time.h>
#include "minor_pf.h"
#include <sys/mman.h>
#define PAGE_SIZE       4096ULL

#define NUM_PAGES       16384ULL

#define TEST_VADDR_R      0x40000000ULL
#define TEST_VADDR_W      0x50000000ULL


#define WARMUP_SAMPLES  10


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

static void print_statistics(const char *name,
                             uint64_t *samples,
                             size_t count)
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
    printf("min     : %lu ns\n", min);
    printf("median  : %lu ns\n", median);
    printf("mean    : %lu ns\n", mean);
    printf("max     : %lu ns\n", max);
}

static uint64_t timespec_to_ns(struct timespec ts)
{
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void benchmark_read(uint64_t freq)
{
    static uint64_t samples[NUM_PAGES];

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

        struct timespec start_ts;
        struct timespec end_ts;
        struct timespec start_tsred;
        struct timespec end_tsred;
        
        uint64_t start = read_cntpct();
        /*
         * This is the operation that causes the page fault.
         */
        asm volatile("" ::: "memory");
        uint8_t value = *page;
        asm volatile("" ::: "memory");

        uint64_t end = read_cntpct();

        /*
         * Prevent the compiler from eliminating the load.
         */
        asm volatile("" :: "r"(value) : "memory");

        samples[i] = ticks_to_ns(end - start, freq);
    }

    print_statistics(
        "Anonymous READ page faults",
        samples + WARMUP_SAMPLES,
        NUM_PAGES - WARMUP_SAMPLES
    );
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

    for (size_t i = 0; i < NUM_PAGES; i++) {

        volatile uint8_t *page =
            base + i * PAGE_SIZE;

        struct timespec start_ts;
        struct timespec end_ts;

        uint64_t start = read_cntpct();
        /*
         * This is the operation that causes the page fault.
         */
        asm volatile("" ::: "memory");
        *page = 42;
        asm volatile("" ::: "memory");
        uint64_t end = read_cntpct();

        samples[i] = ticks_to_ns(end - start, freq);
    }

    print_statistics(
        "Anonymous WRITE page faults",
        samples + WARMUP_SAMPLES,
        NUM_PAGES - WARMUP_SAMPLES
    );
}


int minor_pf(void)
{
    uint64_t freq = read_cntfrq();

    printf("Anonymous page-fault latency benchmark\n");
    printf("========================================\n");

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