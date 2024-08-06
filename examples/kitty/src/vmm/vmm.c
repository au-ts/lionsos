/*
 * Copyright 2023, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <stddef.h>
#include <stdint.h>
#include <microkit.h>
#include <libvmm/guest.h>
#include <libvmm/virq.h>
#include <libvmm/tcb.h>
#include <libvmm/util/util.h>
#include <libvmm/arch/aarch64/linux.h>
#include <libvmm/arch/aarch64/fault.h>
/* Specific to the framebuffer example */
#include "uio.h"

#if defined(CONFIG_PLAT_ODROIDC4)
#define GUEST_RAM_SIZE 0x10000000
#define GUEST_DTB_VADDR 0x2f000000
#define GUEST_INIT_RAM_DISK_VADDR 0x2c000000
#else
#error "Need to define platform specific guest info"
#endif

// @ivanv: need a more systematic way of choosing this IRQ number?
/*
 * This is a virtual IRQ, meaning it does not correspond to any hardware.
 * The IRQ number is chosen because it does not overlap with any other
 * IRQs delivered by the VMM into the guest.
 */
#define UIO_GPU_IRQ 50
/* For when we get notified from MicroPython */
#define MICROPYTHON_CH 1

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

/* IRQs to pass-through to the guest */
/* These are expected to have a 1-1 mapping between index and Microkit channel,
 * starting from 10. For example, the second IRQ in this list should have the
 * channel number of 12. This will be cleaned up in the future.
 */
uint32_t irqs[] = { 232, 35, 192, 193, 194, 53, 246, 71, 227, 228, 63, 62, 48, 89, 5 };

void uio_gpu_ack(size_t vcpu_id, int irq, void *cookie) {
    // Do nothing, there is no actual IRQ to ack since UIO IRQs are virtual!
}

bool uio_init_handler(size_t vcpu_id, uintptr_t addr, size_t fsr, seL4_UserContext *regs, void *data) {
    LOG_VMM("sending notification to MicroPython!\n");
    microkit_notify(MICROPYTHON_CH);
    return true;
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

    for (int i = 0; i < sizeof(irqs) / sizeof(uint32_t); i++) {
        bool success = virq_register_passthrough(GUEST_VCPU_ID, irqs[i], i + 10);
        /* Should not be any reason for this to fail */
        assert(success);
    }

    /* Setting up the UIO region for the framebuffer */
    virq_register(GUEST_VCPU_ID, UIO_GPU_IRQ, &uio_gpu_ack, NULL);
    fault_register_vm_exception_handler(UIO_INIT_ADDRESS, sizeof(size_t), &uio_init_handler, NULL);

    /* Finally start the guest */
    guest_start(GUEST_VCPU_ID, kernel_pc, GUEST_DTB_VADDR, GUEST_INIT_RAM_DISK_VADDR);
}

void notified(microkit_channel ch) {
    switch (ch) {
        case MICROPYTHON_CH: {
            LOG_VMM("Got message from MicroPython, injecting IRQ\n");
            bool success = virq_inject(GUEST_VCPU_ID, UIO_GPU_IRQ);
            if (!success) {
                LOG_VMM_ERR("IRQ %d dropped on vCPU %d\n", UIO_GPU_IRQ, GUEST_VCPU_ID);
            }
            break;
        }
        default: {
            bool success = virq_handle_passthrough(ch);
            if (!success) {
                LOG_VMM_ERR("IRQ %d dropped on vCPU %d\n", irqs[ch - 10], GUEST_VCPU_ID);
            }
            break;
        }
    }
}

seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo) {
    bool success = fault_handle(child, msginfo);
    if (success) {
        /* Now that we have handled the fault successfully, we reply to it so
         * that the guest can resume execution. */
        *reply_msginfo = microkit_msginfo_new(0, 0);
        return seL4_True;
    }

    return seL4_False;
}
