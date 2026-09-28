/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * Per-instance configuration of an input driver. One driver image is shared
 * by several device instances, so the build patches this structure into each
 * instance's ELF (section .input_driver_config) with values produced by the
 * system's meta program.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#define INPUT_DRIVER_CONFIG_MAGIC 0x4c494e50 /* "LINP" */

typedef struct input_driver_config {
    uint32_t magic;
    /* Offset of the device's registers within the mapped `device_regs` region */
    uint32_t regs_offset;
    /* Capacity in events of the queue to the virtualiser */
    uint32_t queue_capacity;
} input_driver_config_t;

static inline bool input_driver_config_check_magic(input_driver_config_t *config)
{
    return config->magic == INPUT_DRIVER_CONFIG_MAGIC;
}
