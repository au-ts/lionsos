/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * Derived from sDDF examples/gpu/include/gpu_config.h. One GPU client (the
 * compositor) with a 4 MiB data region: 4 KiB for display info followed by the
 * framebuffer, which is enough for 1024x768 at 4 bytes per pixel.
 */

#pragma once

#include <stddef.h>
#include <string.h>
#include <sddf/gpu/queue.h>
#include <sddf/gpu/events.h>
/* Keeps the pinned sDDF GPU virtualiser building, see the header for details */
#include <sddf_gpu_compat.h>

#define GPU_NUM_CLIENTS                     1

#define GPU_NAME_CLI0                       "compositor"

#define GPU_QUEUE_CAPACITY_CLI0             1024
#define GPU_QUEUE_CAPACITY_DRV              1024

#define GPU_DATA_REGION_SIZE_CLI0           0x400000
#define GPU_DATA_REGION_SIZE_DRV            0x1000
#define GPU_QUEUE_REGION_SIZE_CLI0          0x200000
#define GPU_QUEUE_REGION_SIZE_DRV           0x200000

/*
 * These are sizes of the regions that hold virtIO information, such as the virtq (metadata)
 * and the memory that descriptors would point to (data).
 */
#define GPU_VIRTIO_METADATA_REGION_SIZE     0x200000
#define GPU_VIRTIO_DATA_REGION_SIZE         0x200000

static inline gpu_events_t *gpu_virt_cli_events_region(gpu_events_t *events, unsigned int id)
{
    switch (id) {
    case 0:
        return events;
    default:
        return NULL;
    }
}

static inline gpu_req_queue_t *gpu_virt_cli_req_queue(gpu_req_queue_t *req, unsigned int id)
{
    switch (id) {
    case 0:
        return req;
    default:
        return NULL;
    }
}

static inline gpu_resp_queue_t *gpu_virt_cli_resp_queue(gpu_resp_queue_t *resp, unsigned int id)
{
    switch (id) {
    case 0:
        return resp;
    default:
        return NULL;
    }
}

static inline uintptr_t gpu_virt_cli_data_region(uintptr_t data, unsigned int id)
{
    switch (id) {
    case 0:
        return data;
    default:
        return 0;
    }
}

static inline uint64_t gpu_virt_cli_data_region_size(unsigned int id)
{
    switch (id) {
    case 0:
        return GPU_DATA_REGION_SIZE_CLI0;
    default:
        return 0;
    }
}

static inline uint32_t gpu_virt_cli_queue_capacity(unsigned int id)
{
    switch (id) {
    case 0:
        return GPU_QUEUE_CAPACITY_CLI0;
    default:
        return 0;
    }
}

static inline uint32_t gpu_cli_queue_capacity(char *pd_name)
{
    if (!strcmp(pd_name, GPU_NAME_CLI0)) {
        return GPU_QUEUE_CAPACITY_CLI0;
    } else {
        return 0;
    }
}
