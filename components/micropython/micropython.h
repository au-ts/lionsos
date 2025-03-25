/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdint.h>
#include <sddf/serial/queue.h>
#include <microkit.h>
#include <libmicrokitco.h>

/* Right now since the framebuffer is not hooked up via a proper sDDF protocol
 * we hard-code the channel we expect to the framebuffer VMM. */
#ifdef ENABLE_FRAMEBUFFER
#define FRAMEBUFFER_VMM_CH 0
#endif

extern serial_queue_handle_t serial_rx_queue_handle;
extern serial_queue_handle_t serial_tx_queue_handle;
