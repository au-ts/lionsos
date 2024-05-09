/*
 * Copyright 2024, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <stddef.h>
#include <stdint.h>
#include <microkit.h>
#include "util/util.h"
#include "arch/aarch64/vgic/vgic.h"
#include "arch/aarch64/linux.h"
#include "arch/aarch64/fault.h"
#include "guest.h"
#include "virq.h"
#include "tcb.h"
#include "vcpu.h"
#include "virtio/virtio.h"
#include "virtio/console.h"
#include <sddf/serial/queue.h>


#include "vmm_ram.h"
#define GUEST_DTB_VADDR 0x8f000000

// @ivanv: need a more systematic way of choosing this IRQ number?
/*
 * This is a virtual IRQ, meaning it does not correspond to any hardware.
 * The IRQ number is chosen because it does not overlap with any other
 * IRQs delivered by the VMM into the guest.
 */
#define UIO_GPU_IRQ 50

/* Data for the guest's kernel image. */
extern char _guest_kernel_image[];
extern char _guest_kernel_image_end[];
/* Data for the device tree to be passed to the kernel. */
extern char _guest_dtb_image[];
extern char _guest_dtb_image_end[];
/* Data for the initial RAM disk to be passed to the kernel. */
extern char _guest_initrd_image[];
extern char _guest_initrd_image_end[];
/* Microkit will set this variable to the start of the guest RAM memory region. */
uintptr_t guest_ram_vaddr;

// @ivanv: should be part of libvmm
#define MAX_IRQ_CH 63
int passthrough_irq_map[MAX_IRQ_CH];

/*
 * For the virtual console
 */
uintptr_t serial_rx_free;
uintptr_t serial_rx_active;
uintptr_t serial_tx_free;
uintptr_t serial_tx_active;

uintptr_t serial_rx_data;
uintptr_t serial_tx_data;

serial_queue_handle_t serial_rx_serial_queue;
serial_queue_handle_t serial_tx_serial_queue;

#define SERIAL_TX_VIRTUALISER_CH 1
#define SERIAL_RX_VIRTUALISER_CH 2


static struct virtio_device virtio_console;


static void passthrough_device_ack(size_t vcpu_id, int irq, void *cookie) {
    microkit_channel irq_ch = (microkit_channel)(int64_t)cookie;
    microkit_irq_ack(irq_ch);
}

static void register_passthrough_irq(int irq, microkit_channel irq_ch) {
    LOG_VMM("Register passthrough IRQ %d (channel: 0x%lx)\n", irq, irq_ch);
    assert(irq_ch < MAX_IRQ_CH);
    passthrough_irq_map[irq_ch] = irq;

    int err = virq_register(GUEST_VCPU_ID, irq, &passthrough_device_ack, (void *)(int64_t)irq_ch);
    if (!err) {
        LOG_VMM_ERR("Failed to register IRQ %d\n", irq);
        return;
    }
}


void init(void) {
    int i;
    int j;

    /* Initialise the VMM, the VCPU(s), and start the guest */
    LOG_VMM("starting \"%s\"\n", microkit_name);
    /* Place all the binaries in the right locations before starting the guest */
    size_t kernel_size = _guest_kernel_image_end - _guest_kernel_image;
    size_t dtb_size = _guest_dtb_image_end - _guest_dtb_image;
    size_t initrd_size = _guest_initrd_image_end - _guest_initrd_image;
    uintptr_t kernel_pc = linux_setup_images(guest_ram_vaddr,
                                      (uintptr_t) _guest_kernel_image,
                                      kernel_size,
                                      (uintptr_t) _guest_dtb_image,
                                      GUEST_DTB_VADDR,
                                      dtb_size,
                                      (uintptr_t) _guest_initrd_image,
                                      GUEST_INIT_RAM_DISK_VADDR,
                                      initrd_size
                                      );
    if (!kernel_pc) {
        LOG_VMM_ERR("Failed to initialise guest images\n");
        return;
    }
    /* Initialise the virtual GIC driver */
    bool success = virq_controller_init(GUEST_VCPU_ID);
    if (!success) {
        LOG_VMM_ERR("Failed to initialise emulated interrupt controller\n");
        return;
    }

    // // @ivanv minimise
    /* Ethernet */
    register_passthrough_irq(40, 21);
    /* Ethernet PHY */
    register_passthrough_irq(41, 22);
    /* panfrost-gpu */
    register_passthrough_irq(192, 7);
    /* panfrost-mmu */
    register_passthrough_irq(193, 8);
    /* panfrost-job */
    register_passthrough_irq(194, 9);
    /* I2C */
    register_passthrough_irq(53, 10);
    /* USB */
    register_passthrough_irq(63, 12);
    /* USB */
    register_passthrough_irq(62, 13);
    /* HDMI */
    register_passthrough_irq(89, 14);
    /* VPU */
    register_passthrough_irq(35, 15);
    /* USB */
    register_passthrough_irq(48, 16);
    register_passthrough_irq(5, 17);
    /* eMMCB */
    register_passthrough_irq(222, 18);
    /* eMMCC */
    register_passthrough_irq(223, 19);
    /* serial */
//    register_passthrough_irq(225, 20);
    /* GPIO IRQs */
    for (i = 96, j = 23; i < 104; i++, j++)
        register_passthrough_irq(i, j);

    /*
     * Set up queues for virtual serial
     */
    /* Initialise our sDDF queue buffers for the serial device */
    serial_queue_init(&serial_rx_serial_queue, (serial_queue_t *)serial_rx_free,
                      (serial_queue_t *)serial_rx_active, true, NUM_ENTRIES, NUM_ENTRIES);
    for (int i = 0; i < NUM_ENTRIES - 1; i++) {
        int ret = serial_enqueue_free(&serial_rx_serial_queue, serial_rx_data + (i * BUFFER_SIZE), BUFFER_SIZE);
        if (ret != 0) {
            microkit_dbg_puts(microkit_name);
            microkit_dbg_puts(": server rx buffer population, unable to enqueue buffer\n");
        }
    }
    serial_queue_init(&serial_tx_serial_queue, (serial_queue_t *)serial_tx_free,
                      (serial_queue_t *)serial_tx_active, true, NUM_ENTRIES, NUM_ENTRIES);
    for (int i = 0; i < NUM_ENTRIES - 1; i++) {
        int ret = serial_enqueue_free(&serial_tx_serial_queue, serial_tx_data + (i * BUFFER_SIZE), BUFFER_SIZE);
        assert(ret == 0);
        if (ret != 0) {
            microkit_dbg_puts(microkit_name);
            microkit_dbg_puts(": server tx buffer population, unable to enqueue buffer\n");
        }
    }
    /*
     * Neither queue should be plugged and hence all buffers we send
     * should actually end up at the driver.
     */
    assert(!serial_queue_plugged(serial_tx_serial_queue.free));
    assert(!serial_queue_plugged(serial_tx_serial_queue.active));

    /* Initialise virtIO console device */
    success = virtio_mmio_device_init(&virtio_console, CONSOLE,
                                      VIRTIO_CONSOLE_BASE, VIRTIO_CONSOLE_SIZE,
                                      VIRTIO_CONSOLE_IRQ,
                                      &serial_rx_serial_queue, &serial_tx_serial_queue,
                                      SERIAL_TX_VIRTUALISER_CH
        );
    assert(success);


    /* Finally start the guest */
    guest_start(GUEST_VCPU_ID, kernel_pc, GUEST_DTB_VADDR, GUEST_INIT_RAM_DISK_VADDR);
}

void notified(microkit_channel ch) {
    switch (ch) {
    case SERIAL_RX_VIRTUALISER_CH: {
        virtio_console_handle_rx(&virtio_console);
        break;
    }
    default:
        if (passthrough_irq_map[ch]) {
            bool success = vgic_inject_irq(GUEST_VCPU_ID, passthrough_irq_map[ch]);
            if (!success) {
                LOG_VMM_ERR("IRQ %d dropped on vCPU %d\n", passthrough_irq_map[ch], GUEST_VCPU_ID);
            }
            break;
        }
        printf("Unexpected channel, ch: 0x%lx\n", ch);
    }
}

/*
 * The primary purpose of the VMM after initialisation is to act as a
 * fault-handler. Whenever our guest causes an exception, it gets
 * delivered to this entry point for the VMM to handle.
 */
void fault(microkit_id id, microkit_msginfo msginfo) {
    bool success = fault_handle(id, msginfo);
    if (success) {
        /*
         * Now that we have handled the fault successfully, we reply to it so
         * that the guest can resume execution.
         */
        microkit_fault_reply(microkit_msginfo_new(0, 0));
    }
}
