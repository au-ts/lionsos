/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * virtIO input driver (MMIO transport).
 *
 * Keeps the device's event queue stocked with buffers, translates the evdev
 * events the device writes into them into LionsOS input events, and pushes
 * those to the input virtualiser. Absolute axes are normalised to
 * [0, INPUT_ABS_MAX]. The status queue (LEDs) is set up but not used.
 *
 * DMA region layout (`virtio_dma`, physically contiguous):
 *   [0, VIRTQ_RINGS_SIZE)        split virtqueue rings, event queue then status queue
 *   [EVENT_BUFS_OFFSET, ...)     one struct virtio_input_event per descriptor
 */

#include <stdint.h>
#include <stddef.h>
#include <microkit.h>
#include <sddf/util/util.h>
#include <sddf/util/fence.h>
#include <sddf/util/printf.h>
#include <sddf/virtio/transport/common.h>
#include <sddf/virtio/transport/mmio.h>
#include <sddf/virtio/feature.h>
#include <sddf/virtio/queue.h>
#include <lions/input/input.h>
#include <lions/input/config.h>
#include "virtio_input.h"

#define LOG_DRIVER(...) do{ sddf_dprintf("INPUT DRIVER|INFO: "); sddf_dprintf(__VA_ARGS__); }while(0)
#define LOG_DRIVER_ERR(...) do{ sddf_dprintf("INPUT DRIVER|ERROR: "); sddf_dprintf(__VA_ARGS__); }while(0)

#define IRQ_CH 0
#define VIRT_CH 1

#define QUEUE_SIZE 64

/* Split virtqueue ring sizes, see "2.7 Split Virtqueues" */
#define DESC_SIZE (16 * QUEUE_SIZE)
#define AVAIL_SIZE (6 + 2 * QUEUE_SIZE)
#define USED_SIZE (6 + 8 * QUEUE_SIZE)
#define AVAIL_OFFSET DESC_SIZE
#define USED_OFFSET ALIGN(AVAIL_OFFSET + AVAIL_SIZE, 4)
#define VIRTQ_SIZE ALIGN(USED_OFFSET + USED_SIZE, 16)
#define VIRTQ_RINGS_SIZE (2 * VIRTQ_SIZE)
#define EVENT_BUFS_OFFSET 0x1000
#define VIRTIO_DMA_SIZE 0x2000


_Static_assert(VIRTQ_RINGS_SIZE <= EVENT_BUFS_OFFSET, "virtqueue rings overlap the event buffers");
_Static_assert(EVENT_BUFS_OFFSET + QUEUE_SIZE * sizeof(struct virtio_input_event) <= VIRTIO_DMA_SIZE,
               "event buffers do not fit in the DMA region");

__attribute__((__section__(".input_driver_config"))) input_driver_config_t config;

/* Patched in by the system description */
uintptr_t device_regs;
uintptr_t virtio_dma;
uintptr_t virtio_dma_paddr;
uintptr_t input_queue;

static volatile virtio_mmio_regs_t *regs;
static struct virtq event_vq;
static struct virtq status_vq;
static input_queue_handle_t queue_handle;

static bool has_abs;
static struct virtio_input_absinfo abs_info[2];

static struct virtio_input_event *event_buf(uint16_t idx)
{
    return (struct virtio_input_event *)(virtio_dma + EVENT_BUFS_OFFSET) + idx;
}

static uint64_t event_buf_paddr(uint16_t idx)
{
    return virtio_dma_paddr + EVENT_BUFS_OFFSET + idx * sizeof(struct virtio_input_event);
}

/* Select a configuration item and return its size, 0 if the device has none */
static uint8_t config_select(uint8_t select, uint8_t subsel)
{
    volatile struct virtio_input_config *cfg = (volatile struct virtio_input_config *)regs->Config;
    cfg->select = select;
    cfg->subsel = subsel;
    return cfg->size;
}

static void read_device_config(void)
{
    volatile struct virtio_input_config *cfg = (volatile struct virtio_input_config *)regs->Config;

    char name[sizeof(cfg->u.string) + 1] = { 0 };
    uint8_t len = config_select(VIRTIO_INPUT_CFG_ID_NAME, 0);
    for (uint8_t i = 0; i < len && i < sizeof(cfg->u.string); i++) {
        name[i] = cfg->u.string[i];
    }
    LOG_DRIVER("found '%s'\n", name);

    has_abs = config_select(VIRTIO_INPUT_CFG_EV_BITS, INPUT_EV_ABS) > 0;
    if (!has_abs) {
        return;
    }

    for (int axis = INPUT_ABS_X; axis <= INPUT_ABS_Y; axis++) {
        if (config_select(VIRTIO_INPUT_CFG_ABS_INFO, axis) < sizeof(struct virtio_input_absinfo)) {
            LOG_DRIVER_ERR("device reports EV_ABS but no range for axis %d\n", axis);
            abs_info[axis] = (struct virtio_input_absinfo) { .min = 0, .max = INPUT_ABS_MAX };
            continue;
        }
        abs_info[axis].min = cfg->u.abs.min;
        abs_info[axis].max = cfg->u.abs.max;
        LOG_DRIVER("axis %d range [%u, %u]\n", axis, abs_info[axis].min, abs_info[axis].max);
    }
}

static void virtq_init(struct virtq *vq, uintptr_t base)
{
    vq->num = QUEUE_SIZE;
    vq->desc = (struct virtq_desc *)base;
    vq->avail = (struct virtq_avail *)(base + AVAIL_OFFSET);
    vq->used = (struct virtq_used *)(base + USED_OFFSET);
    vq->used_head = 0;
}

static bool setup_queue(uint32_t index, uint64_t base_paddr)
{
    regs->QueueSel = index;
    if (regs->QueueReady) {
        LOG_DRIVER_ERR("queue %u is already in use\n", index);
        return false;
    }
    if (regs->QueueNumMax < QUEUE_SIZE) {
        LOG_DRIVER_ERR("queue %u supports only %u entries\n", index, regs->QueueNumMax);
        return false;
    }

    regs->QueueNum = QUEUE_SIZE;
    regs->QueueDescLow = base_paddr & 0xffffffff;
    regs->QueueDescHigh = base_paddr >> 32;
    regs->QueueDriverLow = (base_paddr + AVAIL_OFFSET) & 0xffffffff;
    regs->QueueDriverHigh = (base_paddr + AVAIL_OFFSET) >> 32;
    regs->QueueDeviceLow = (base_paddr + USED_OFFSET) & 0xffffffff;
    regs->QueueDeviceHigh = (base_paddr + USED_OFFSET) >> 32;
    regs->QueueReady = 1;
    return true;
}

/* Hand an event buffer to the device */
static void provide_buffer(uint16_t idx)
{
    event_vq.desc[idx] = (struct virtq_desc) {
        .addr = event_buf_paddr(idx),
        .len = sizeof(struct virtio_input_event),
        .flags = VIRTQ_DESC_F_WRITE,
        .next = 0,
    };
    event_vq.avail->ring[event_vq.avail->idx % QUEUE_SIZE] = idx;
    THREAD_MEMORY_RELEASE();
    event_vq.avail->idx++;
}

static bool device_init(void)
{
    regs = (volatile virtio_mmio_regs_t *)(device_regs + config.regs_offset);

    if (regs->MagicValue != VIRTIO_MMIO_MAGIC_VALUE || regs->Version != VIRTIO_VERSION) {
        LOG_DRIVER_ERR("no virtIO 1.0 MMIO device at offset 0x%x\n", config.regs_offset);
        return false;
    }
    if (regs->DeviceID != VIRTIO_INPUT_DEVICE_ID) {
        LOG_DRIVER_ERR("device at offset 0x%x is not an input device (id %u)\n", config.regs_offset,
                       regs->DeviceID);
        return false;
    }

    regs->Status = 0;
    regs->Status = VIRTIO_DEVICE_STATUS_ACKNOWLEDGE;
    regs->Status |= VIRTIO_DEVICE_STATUS_DRIVER;

    regs->DeviceFeaturesSel = 1;
    if (!(regs->DeviceFeatures & BIT(VIRTIO_F_VERSION_1 - 32))) {
        LOG_DRIVER_ERR("device does not support virtIO 1.0\n");
        return false;
    }
    regs->DriverFeaturesSel = 0;
    regs->DriverFeatures = 0;
    regs->DriverFeaturesSel = 1;
    regs->DriverFeatures = BIT(VIRTIO_F_VERSION_1 - 32);

    regs->Status |= VIRTIO_DEVICE_STATUS_FEATURES_OK;
    if (!(regs->Status & VIRTIO_DEVICE_STATUS_FEATURES_OK)) {
        LOG_DRIVER_ERR("device did not accept our features\n");
        return false;
    }

    read_device_config();

    virtq_init(&event_vq, virtio_dma);
    virtq_init(&status_vq, virtio_dma + VIRTQ_SIZE);
    if (!setup_queue(VIRTIO_INPUT_EVENT_QUEUE, virtio_dma_paddr)
        || !setup_queue(VIRTIO_INPUT_STATUS_QUEUE, virtio_dma_paddr + VIRTQ_SIZE)) {
        return false;
    }

    for (uint16_t i = 0; i < QUEUE_SIZE; i++) {
        provide_buffer(i);
    }

    regs->Status |= VIRTIO_DEVICE_STATUS_DRIVER_OK;
    THREAD_MEMORY_FENCE();
    regs->QueueNotify = VIRTIO_INPUT_EVENT_QUEUE;
    return true;
}

static int32_t normalise_abs(uint16_t axis, uint32_t value)
{
    if (axis > INPUT_ABS_Y) {
        return (int32_t)value;
    }

    struct virtio_input_absinfo *info = &abs_info[axis];
    if (info->max <= info->min) {
        return 0;
    }
    if (value <= info->min) {
        return 0;
    }
    if (value >= info->max) {
        return INPUT_ABS_MAX;
    }
    return (int32_t)(((uint64_t)(value - info->min) * INPUT_ABS_MAX) / (info->max - info->min));
}

/* Returns true if any events were passed on to the virtualiser */
static bool handle_events(void)
{
    bool forwarded = false;

    while (event_vq.used_head != event_vq.used->idx) {
        THREAD_MEMORY_ACQUIRE();
        struct virtq_used_elem used = event_vq.used->ring[event_vq.used_head % QUEUE_SIZE];
        event_vq.used_head++;

        if (used.id >= QUEUE_SIZE) {
            LOG_DRIVER_ERR("device returned invalid descriptor %u\n", used.id);
            continue;
        }

        struct virtio_input_event *ev = event_buf(used.id);
        input_event_t event = {
            .type = ev->type,
            .code = ev->code,
            .value = (int32_t)ev->value,
        };
        if (event.type == INPUT_EV_ABS && has_abs) {
            event.value = normalise_abs(event.code, ev->value);
        }

        if (input_enqueue(&queue_handle, event) == 0) {
            forwarded = true;
        }

        provide_buffer(used.id);
    }

    THREAD_MEMORY_FENCE();
    regs->QueueNotify = VIRTIO_INPUT_EVENT_QUEUE;
    return forwarded;
}

void init(void)
{
    assert(input_driver_config_check_magic(&config));
    input_queue_init(&queue_handle, (input_queue_t *)input_queue, config.queue_capacity);

    if (!device_init()) {
        regs->Status |= VIRTIO_DEVICE_STATUS_FAILED;
        return;
    }
    microkit_irq_ack(IRQ_CH);
}

void notified(microkit_channel ch)
{
    if (ch != IRQ_CH) {
        LOG_DRIVER_ERR("notification on unexpected channel %u\n", ch);
        return;
    }

    uint32_t status = regs->InterruptStatus;
    regs->InterruptACK = status;

    if ((status & VIRTIO_IRQ_VQUEUE) && handle_events()) {
        microkit_notify(VIRT_CH);
    }
    microkit_irq_ack(IRQ_CH);
}
