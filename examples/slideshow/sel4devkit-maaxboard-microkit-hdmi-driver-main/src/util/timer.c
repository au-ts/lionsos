/* This work is Crown Copyright NCSC, 2024. */
/* 
 * Copyright 2022, Capgemini Engineering
 * 
 * SPDX-License-Identifier: BSD-2-Clause
 *
 */

// Edited 01/06/23 - Capgemini Engineering CP dev

/* This timer uses the System Counter (SYS_CTR) provided by the iMX8MQ SoC
 * to implement the timing functions expected by the U-Boot codebase */

#include <stdint.h>
#include <stdio.h>

#include "timer.h"

#include <microkit.h>

#ifdef TIMER_DEBUG
#define timer_print(...) printf(__VA_ARGS__)
#else
#define timer_print(...) 0
#endif

#define hang() 0 //noop

struct cntl_reg {
	uint32_t cntcr;
	uint32_t cntsr;
	uint32_t cntcv0;
	uint32_t cntcv1;
    uint32_t rsvd0[4];
    uint32_t cntfid0;
    uint32_t cntfid1;
    uint32_t cntfid2;
};

uint64_t readl(unsigned long addr, uint32_t offset) {
	void *_GET_ADDR;
	_GET_ADDR = (uint32_t*)(addr + offset);
	return (*(volatile uint32_t *)(_GET_ADDR));
}

void writel(uint32_t val, unsigned long addr, uint32_t offset) {
	uint32_t *_GET_ADDR;
	_GET_ADDR = (uint32_t*)(addr + offset);
	(*(volatile uint32_t *)(_GET_ADDR) = (val));
}


#define CNTCR_EN    0x0b
#define CNTCR_FCR0  0x1000b

struct cntl_reg *ctrl_reg = (struct cntl_reg *)0x306c0000;

uint64_t tick_frequency = 0;
uintptr_t tmr_base;

int start_time_ticks;
int start_timer_in_use = 0;

uint64_t get_clock_freq(uintptr_t timer_base) {
    return readl(timer_base, 0x020);
}

void initialise_and_start_timer(uintptr_t timer_base)
{
    tmr_base = timer_base;
    tick_frequency = readl(tmr_base, 0x020);

    timer_print("Tick freq = %x\n", tick_frequency);
    if (tick_frequency < 1000000) {
        printf("Fatal: System counter tick frequency is too low for accurate timing\n");
        hang();
    }

    /* Set the enable bitand select the base frequency */
    writel(CNTCR_EN | CNTCR_FCR0, tmr_base, 0); //cntcr
}

void shutdown_timer(void)
{
    /* Nothing to do here. It would be unsafe to stop the timer as other
     * instances of the library may still be using it */
}

/* Provide implementations of the various timer functions used by U-Boot */

uint64_t get_ticks(void) {

    if (tick_frequency == 0) {
        printf("Fatal: Attempt to read from uninitialised timer\n");
        hang();
    }
    uint32_t initial_high = readl(tmr_base,0x00); //cntcv1
    uint32_t low = readl(tmr_base,0x08); //cntcv0
    uint32_t high = readl(tmr_base,0x00); //cntcv1
    if (high != initial_high) {
        /* get low again if high has ticked over. */
        low = readl(tmr_base,0x08); //cntcv0
        printf("Tick over low: %x\n", low);
    }

    return (((uint64_t)high << 32) | low);
}

unsigned long timer_get_us(void) {
    /* To improve accuracy we shift ticks left by 7 bits. Note that
    * the counter value is only a 57 bit value so this is safe.
    * When calculating the resulting time this shift is accounted for.
    */

    uint64_t ticks_per_us = ((uint64_t)tick_frequency << 7) / 1000000;

    return (get_ticks() << 7) / ticks_per_us;
}

void ms_delay(int delay) {
    timer_print("TIMER START\n");
    unsigned long timer_count_init = get_ticks();
	timer_print("Start count: %ld\n", timer_count_init);
    unsigned long delay_ticks = delay*(tick_frequency/1000);
    timer_print("Delay ticks: %ld\n", delay_ticks);
	while (get_ticks() < timer_count_init + delay_ticks) {
        seL4_Yield();
	}
	timer_print("Finish count: %ld\n", get_ticks());
    timer_print("Target end was: %ld\n", timer_count_init + delay_ticks);
    timer_print("TIMER END\n");
}

void start_timer(){
    start_time_ticks = get_ticks();
    start_timer_in_use = 1;
}

int stop_timer(){
    int time_in_ms = -1;
    if (start_timer_in_use == 1){
        int end_time_ticks = get_ticks();
        time_in_ms = ((end_time_ticks - start_time_ticks)/ (tick_frequency/1000));
        start_timer_in_use = 0;
    }
    return time_in_ms;
}

void u_delay(int delay) {
    timer_print("TIMER START\n");
    unsigned long timer_count_init = get_ticks();
	timer_print("Start count: %ld\n", timer_count_init);
    unsigned long delay_ticks = delay*(tick_frequency/1000000);
    timer_print("Delay ticks: %ld\n", delay_ticks);
	while (get_ticks() < timer_count_init + delay_ticks) {
        seL4_Yield();
	}
	timer_print("Finish count: %ld\n", get_ticks());
    timer_print("Target end was: %ld\n", timer_count_init + delay_ticks);
    timer_print("TIMER END\n");
}