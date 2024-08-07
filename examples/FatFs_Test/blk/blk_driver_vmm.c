/*
 * Copyright 2023, UNSW
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
#include "virtio/block.h"
#include <sddf/serial/shared_ringbuffer.h>

/*
 * As this is just an example, for simplicity we just make the size of the
 * guest's "RAM" the same for all platforms. For just booting Linux with a
 * simple user-space, 0x10000000 bytes (256MB) is plenty.
 */
#define GUEST_RAM_SIZE 0x8000000

#if defined(BOARD_qemu_arm_virt)
#define GUEST_DTB_VADDR 0x47f00000
#define GUEST_INIT_RAM_DISK_VADDR 0x47000000
#else
#error Need to define guest kernel image address and DTB address
#endif

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

#define MAX_IRQ_CH 63
int passthrough_irq_map[MAX_IRQ_CH];

static void dummy_ack(size_t vcpu_id, int irq, void *cookie) {
    return;
}

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

/* sDDF block */
typedef struct {
    int irq;
    int ch;
} uio_device_t;

#define MAX_UIO_DEVICE 32
// @ericc: @TODO: autogen these, irq from dts, ch from microkit system file
#define NUM_UIO_DEVICE 2
uio_device_t uio_devices[MAX_UIO_DEVICE] = {
    { .irq = 50, .ch = 3 },
    { .irq = 51, .ch = 4 },
};

// @ericc: @TODO: change from linear search to O(1) later
static int get_uio_ch(int irq) {
    for (int i = 0; i < MAX_UIO_DEVICE; i++) {
        if (uio_devices[i].irq == irq) {
            return uio_devices[i].ch;
        }
    }
    return -1;
}

static int get_uio_irq_from_ch(int ch) {
    for (int i = 0; i < MAX_UIO_DEVICE; i++) {
        if (uio_devices[i].ch == ch) {
            return uio_devices[i].irq;
        }
    }
    return -1;
}

void uio_ack(size_t vcpu_id, int irq, void *cookie) {
    // printf("Going to notify : %d\n", get_uio_ch(irq));
    microkit_notify(get_uio_ch(irq));
}

void init(void) {
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

    /* Register UIO irq */
    for (int i = 0; i < NUM_UIO_DEVICE; i++) {
        virq_register(GUEST_VCPU_ID, uio_devices[i].irq, &uio_ack, NULL);
    }
    
    register_passthrough_irq(33, 30);

    /* Finally start the guest */
    guest_start(GUEST_VCPU_ID, kernel_pc, GUEST_DTB_VADDR, GUEST_INIT_RAM_DISK_VADDR);
}

void notified(microkit_channel ch) {
    /* Handle notifications from clients of block device */
    // printf("IRQ received: %d\n", ch);
    int irq = get_uio_irq_from_ch(ch);
    if (irq != -1) {
        // printf("UIO_IRQ RECEIVED: %d\n", ch);
        virq_inject(GUEST_VCPU_ID, irq);
        return;
    }

    if (ch == 30) {
        virq_inject(GUEST_VCPU_ID, 33);
        return;
    }

    switch (ch) {
        default:
            LOG_VMM_ERR("Unexpected channel, ch: 0x%lx\n", ch);
    }
}

/*
 * The primary purpose of the VMM after initialisation is to act as a fault-handler,
 * whenever our guest causes an exception, it gets delivered to this entry point for
 * the VMM to handle.
 */
void fault(microkit_id id, microkit_msginfo msginfo) {
    bool success = fault_handle(id, msginfo);
    if (success) {
        /* Now that we have handled the fault successfully, we reply to it so
         * that the guest can resume execution. */
        microkit_fault_reply(microkit_msginfo_new(0, 0));
    }
}
