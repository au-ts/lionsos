/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */
#pragma once
#include <stdbool.h>
#include "py/ringbuf.h"

extern void mp_hal_set_interrupt_char (int c);
extern void mp_sched_keyboard_interrupt(void);

/**
 * Search the serial Rx queue for an interrupt character. If an interrupt
 * character is found, the resulting behaviour is dependent on the state of the
 * Micropython cothread. If the Micropython cothread is not waiting on serial
 * input, an interrupt is scheduled and all characters prior to and including
 * the interrupt character are discarded. If the Micropython cothread is waiting
 * on serial input, no interrupt is scheduled, and all characters prior to the
 * interrupt character are discaded. Micropython does not require an interrupt
 * to be scheduled if an interrupt character is inputted into the repl.
 *
 * @return true if interrupt character was found and Micropython cothread is not
 * currently waiting on serial input, false if either no interrupt character was
 * found, or the Micropython cothread is currently waiting on serial input.
 */
bool intercept_serial_rx_interrupt(void);
