/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * Compatibility shim for the sDDF virtIO GPU driver.
 *
 * sDDF commit ff465ea ("virtio: make transport layer agnostic") replaced
 * <sddf/virtio/virtio.h> with the transport/, queue.h and feature.h headers,
 * but drivers/gpu/virtio/gpu.c was not ported and no longer compiles.
 * This header provides the handful of MMIO helpers it still expects on top
 * of the new headers. Remove it once the GPU driver is ported upstream.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <sddf/virtio/transport/common.h>
#include <sddf/virtio/transport/mmio.h>
#include <sddf/virtio/feature.h>

#define VIRTIO_MMIO_IRQ_VQUEUE VIRTIO_IRQ_VQUEUE
#define VIRTIO_MMIO_IRQ_CONFIG VIRTIO_IRQ_CONFIG

static inline bool virtio_mmio_check_magic(virtio_mmio_regs_t *regs)
{
    return regs->MagicValue == VIRTIO_MMIO_MAGIC_VALUE;
}

static inline bool virtio_mmio_check_device_id(virtio_mmio_regs_t *regs, virtio_device_id_t id)
{
    return regs->DeviceID == id;
}

static inline uint32_t virtio_mmio_version(virtio_mmio_regs_t *regs)
{
    return regs->Version;
}
