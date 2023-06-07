/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#pragma once

// TODO: UNCONDENSE THIS FILE TO USE THE PROPER LIBRARY. 

#include <stdint.h>
#include <sel4/sel4.h>

/* A counter is an index to a performance counter on a platform.
 * The max counter index is sizeof(seL4_Word) */
typedef seL4_Word counter_t;
/* A counter_bitfield is used to select multiple counters.
 * Each bit corresponds to a counter id */
typedef seL4_Word counter_bitfield_t;

/* An event id is the hardware id of an event.
 * See the events.h for your architecture for specific events, caveats,
 * gotchas, and other trickery. */
typedef seL4_Word event_id_t;

//function attributes
//functions that need to be inlined for speed
#define FASTFN inline __attribute__((always_inline))
//functions that must not cache miss
#define CACHESENSFN __attribute__((noinline, aligned(64)))

#define BIT(n) (1ul<<(n))

#define DIV_ROUND_UP(n,d)   \
    ({ typeof (n) _n = (n); \
       typeof (d) _d = (d); \
       (_n/_d + (_n % _d == 0 ? 0 : 1)); \
   })

//counters and related constants
#define SEL4BENCH_ARMV8A_NUM_COUNTERS 4

#define SEL4BENCH_ARMV8A_COUNTER_CCNT 31


/* generic events */
#define SEL4BENCH_EVENT_CACHE_L1I_MISS              0x01
#define SEL4BENCH_EVENT_CACHE_L1D_MISS              0x03
#define SEL4BENCH_EVENT_TLB_L1I_MISS                0x02
#define SEL4BENCH_EVENT_TLB_L1D_MISS                0x05
#define SEL4BENCH_EVENT_EXECUTE_INSTRUCTION         0x08
#define SEL4BENCH_EVENT_BRANCH_MISPREDICT           0x10

#define SEL4BENCH_EVENT_MEMORY_ACCESS               0x13
/*
 * PMCR:
 *
 *  bits 31:24 = implementor
 *  bits 23:16 = idcode
 *  bits 15:11 = number of counters
 *  bits 10:6  = reserved, sbz
 *  bit  5 = disable CCNT when non-invasive debug is prohibited
 *  bit  4 = export events to ETM
 *  bit  3 = cycle counter divides by 64
 *  bit  2 = write 1 to reset cycle counter to zero
 *  bit  1 = write 1 to reset all counters to zero
 *  bit  0 = enable bit
 */
#define SEL4BENCH_ARMV8A_PMCR_N(x)       (((x) & 0xFFFF) >> 11u)
#define SEL4BENCH_ARMV8A_PMCR_ENABLE     BIT(0)
#define SEL4BENCH_ARMV8A_PMCR_RESET_ALL  BIT(1)
#define SEL4BENCH_ARMV8A_PMCR_RESET_CCNT BIT(2)
#define SEL4BENCH_ARMV8A_PMCR_DIV64      BIT(3) /* Should CCNT be divided by 64? */

#define PMUSERENR   "PMUSERENR_EL0"
#define PMINTENCLR  "PMINTENCLR_EL1"
#define PMINTENSET  "PMINTENSET_EL1"
#define PMCR        "PMCR_EL0"
#define PMCNTENCLR  "PMCNTENCLR_EL0"
#define PMCNTENSET  "PMCNTENSET_EL0"
#define PMXEVCNTR   "PMXEVCNTR_EL0"
#define PMSELR      "PMSELR_EL0"
#define PMXEVTYPER  "PMXEVTYPER_EL0"
#define PMCCNTR     "PMCCNTR_EL0"

#define PMOVSSERT   "PMOVSSET_EL0"
#define PMOVSCLR    "PMOVSCLR_EL0"

#define CCNT_FORMAT "%"PRIu64
typedef uint64_t ccnt_t;


#define PMU_WRITE(reg, v)                      \
    do {                                       \
        seL4_Word _v = v;                         \
        asm volatile("msr  " reg ", %0" :: "r" (_v)); \
    }while(0)

#define PMU_READ(reg, v) asm volatile("mrs %0, " reg :  "=r"(v))

#define SEL4BENCH_READ_CCNT(var) PMU_READ(PMCCNTR, var);


static FASTFN void sel4bench_private_write_pmcr(uint32_t val)
{
    PMU_WRITE(PMCR, val);
}
static FASTFN uint32_t sel4bench_private_read_pmcr(void)
{
    uint32_t val;
    PMU_READ(PMCR, val);
    return val;
}

#define MODIFY_PMCR(op, val) sel4bench_private_write_pmcr(sel4bench_private_read_pmcr() op (val))

/*
 * CNTENS/CNTENC (Count Enable Set/Clear)
 *
 * Enables the Cycle Count Register, PMCCNTR_EL0, and any implemented event counters
 * PMEVCNTR<x>. Reading this register shows which counters are enabled.
 *
 */
static FASTFN void sel4bench_private_write_cntens(uint32_t mask)
{
    PMU_WRITE(PMCNTENSET, mask);
}

static FASTFN uint32_t sel4bench_private_read_cntens(void)
{
    uint32_t mask;
    PMU_READ(PMCNTENSET, mask);
    return mask;
}

/*
 * Disables the Cycle Count Register, PMCCNTR_EL0, and any implemented event counters
 * PMEVCNTR<x>. Reading this register shows which counters are enabled.
 */
static FASTFN void sel4bench_private_write_cntenc(uint32_t mask)
{
    PMU_WRITE(PMCNTENCLR, mask);
}

/*
 * Reads or writes the value of the selected event counter, PMEVCNTR<n>_EL0.
 * PMSELR_EL0.SEL determines which event counter is selected.
 */
static FASTFN uint32_t sel4bench_private_read_pmcnt(void)
{
    uint32_t val;
    PMU_READ(PMXEVCNTR, val);
    return val;
}

static FASTFN void sel4bench_private_write_pmcnt(uint32_t val)
{
    PMU_WRITE(PMXEVCNTR, val);
}

/*
 * Selects the current event counter PMEVCNTR<x> or the cycle counter, CCNT
 */
static FASTFN void sel4bench_private_write_pmnxsel(uint32_t val)
{
    PMU_WRITE(PMSELR, val);
}

/*
 * When PMSELR_EL0.SEL selects an event counter, this accesses a PMEVTYPER<n>_EL0
 * register. When PMSELR_EL0.SEL selects the cycle counter, this accesses PMCCFILTR_EL0.
 */
static FASTFN uint32_t sel4bench_private_read_evtsel(void)
{

    uint32_t val;
    PMU_READ(PMXEVTYPER, val);
    return val;
}

static FASTFN void sel4bench_private_write_evtsel(uint32_t val)
{
    PMU_WRITE(PMXEVTYPER, val);
}

static FASTFN uint32_t sel4bench_private_read_overflow(void)
{
    uint32_t val;
    PMU_READ(PMOVSSERT, val);
    PMU_WRITE(PMOVSCLR, val); // Clear the overflow bit so we can detect it again. 
    return val;
}

static FASTFN seL4_Word sel4bench_get_num_counters()
{
    return SEL4BENCH_ARMV8A_PMCR_N(sel4bench_private_read_pmcr());
}

static FASTFN void sel4bench_init()
{
    //ensure all counters are in the stopped state
    sel4bench_private_write_cntenc(-1);

    //Clear div 64 flag
    MODIFY_PMCR(&, ~SEL4BENCH_ARMV8A_PMCR_DIV64);

    //Reset all counters
    MODIFY_PMCR( |, SEL4BENCH_ARMV8A_PMCR_RESET_ALL | SEL4BENCH_ARMV8A_PMCR_RESET_CCNT);

    //Enable counters globally.
    MODIFY_PMCR( |, SEL4BENCH_ARMV8A_PMCR_ENABLE);

    //start CCNT
    sel4bench_private_write_cntens(BIT(SEL4BENCH_ARMV8A_COUNTER_CCNT));
}

static FASTFN ccnt_t sel4bench_get_cycle_count()
{
    ccnt_t val;
    uint32_t enable_word = sel4bench_private_read_cntens(); //store running state

    sel4bench_private_write_cntenc(BIT(SEL4BENCH_ARMV8A_COUNTER_CCNT)); //stop CCNT
    SEL4BENCH_READ_CCNT(val); //read its value
    sel4bench_private_write_cntens(enable_word); //start it again if it was running

    return val;
}

/* being declared FASTFN allows this function (once inlined) to cache miss; I
 * think it's worthwhile in the general case, for performance reasons.
 * moreover, it's small enough that it'll be suitably aligned most of the time
 */
static FASTFN ccnt_t sel4bench_get_counter(counter_t counter)
{
    sel4bench_private_write_pmnxsel(counter); //select the counter on the PMU

    counter = BIT(counter); //from here on in, we operate on a bitfield

    uint32_t enable_word = sel4bench_private_read_cntens();

    sel4bench_private_write_cntenc(counter); //stop the counter
    uint32_t val = sel4bench_private_read_pmcnt(); //read its value
    sel4bench_private_write_cntens(enable_word); //start it again if it was running

    return val;
}

/* this reader function is too complex to be inlined, so we force it to be
 * cacheline-aligned in order to avoid icache misses with the counters off.
 * (relevant note: GCC compiles this function to be exactly one ARMV7 cache
 * line in size) however, the pointer dereference is overwhelmingly likely to
 * produce a dcache miss, which will occur with the counters off
 */
static CACHESENSFN ccnt_t sel4bench_get_counters(counter_bitfield_t mask, ccnt_t *values)
{
    //we don't really have time for a NULL or bounds check here

    uint32_t enable_word = sel4bench_private_read_cntens(); //store current running state

    sel4bench_private_write_cntenc(enable_word); //stop running counters (we do this instead of stopping the ones we're interested in because it saves an instruction)

    unsigned int counter = 0;
    for (; mask != 0; mask >>= 1, counter++) { //for each counter...
        if (mask & 1) { //... if we care about it...
            sel4bench_private_write_pmnxsel(counter); //select it,
            values[counter] = sel4bench_private_read_pmcnt(); //and read its value
        }
    }

    ccnt_t ccnt;
    SEL4BENCH_READ_CCNT(ccnt); //finally, read CCNT

    sel4bench_private_write_cntens(enable_word); //start the counters again

    return ccnt;
}

static FASTFN void sel4bench_set_count_event(counter_t counter, event_id_t event)
{
    sel4bench_private_write_pmnxsel(counter); //select counter
    sel4bench_private_write_pmcnt(0); //reset it
    return sel4bench_private_write_evtsel(event); //change the event
}

static FASTFN void sel4bench_start_counters(counter_bitfield_t mask)
{
    /* conveniently, ARM performance counters work exactly like this,
     * so we just write the value directly to COUNTER_ENABLE_SET
     */
    return sel4bench_private_write_cntens(mask);
}

static FASTFN void sel4bench_stop_counters(counter_bitfield_t mask)
{
    /* conveniently, ARM performance counters work exactly like this,
     * so we just write the value directly to COUNTER_ENABLE_SET
     * (protecting the CCNT)
     */
    return sel4bench_private_write_cntenc(mask & ~BIT(SEL4BENCH_ARMV8A_COUNTER_CCNT));
}

static FASTFN void sel4bench_reset_counters(void)
{
    //Reset all counters except the CCNT
    MODIFY_PMCR( |, SEL4BENCH_ARMV8A_PMCR_RESET_ALL);
}

