/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdint.h>
#include <sddf/serial/queue.h>
#include <microkit.h>
#include <libmicrokitco.h>

// TODO
#ifdef ENABLE_FRAMEBUFFER
#define FRAMEBUFFER_VMM_CH 0
#endif

// TODO
#define FS_CH 0

extern serial_queue_handle_t serial_rx_queue_handle;
extern serial_queue_handle_t serial_tx_queue_handle;
