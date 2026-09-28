/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * Input class configuration: a keyboard and a tablet (absolute pointer)
 * driver, and the desktop as the only client. Must match meta.py.
 */

#pragma once

#include <lions/input/input.h>

#define INPUT_NUM_DRIVERS 2
#define INPUT_NUM_CLIENTS 1

#define INPUT_QUEUE_REGION_SIZE 0x1000
