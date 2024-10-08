/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdint.h>
#include <sddf/serial/queue.h>
#include <microkit.h>
#include <libmicrokitco.h>

#ifdef ENABLE_FRAMEBUFFER
#define FRAMEBUFFER_VMM_CH 0
#endif
#define TIMER_CH 1
#define ETH_RX_CH 2
#define ETH_TX_CH 3
#define FS_CH 7
#define SERIAL_RX_CH 8
#define SERIAL_TX_CH 9
#ifdef ENABLE_I2C
#define I2C_CH 10
#endif

extern serial_queue_handle_t serial_rx_queue_handle;
extern serial_queue_handle_t serial_tx_queue_handle;
