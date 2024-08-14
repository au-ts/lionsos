/*
 * Copyright 2023, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <microkit.h>
#include <libvmm/guest.h>
#include <libvmm/virq.h>
#include <libvmm/util/util.h>
#include <libvmm/arch/aarch64/linux.h>
#include <libvmm/arch/aarch64/fault.h>

#include "vmm_ram.h"
#define GUEST_DTB_VADDR 0x8f000000

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

    /* Ethernet */
    virq_register_passthrough(GUEST_VCPU_ID, 40, 21);
    /* Ethernet PHY */
    virq_register_passthrough(GUEST_VCPU_ID, 41, 22);
    /* panfrost-gpu */
    virq_register_passthrough(GUEST_VCPU_ID, 192, 7);
    /* panfrost-mmu */
    virq_register_passthrough(GUEST_VCPU_ID, 193, 8);
    /* panfrost-job */
    virq_register_passthrough(GUEST_VCPU_ID, 194, 9);
    /* I2C */
    virq_register_passthrough(GUEST_VCPU_ID, 53, 10);
    /* USB */
    virq_register_passthrough(GUEST_VCPU_ID, 63, 12);
    /* USB */
    virq_register_passthrough(GUEST_VCPU_ID, 62, 13);
    /* HDMI */
    virq_register_passthrough(GUEST_VCPU_ID, 89, 14);
    /* VPU */
    virq_register_passthrough(GUEST_VCPU_ID, 35, 15);
    /* USB */
    virq_register_passthrough(GUEST_VCPU_ID, 48, 16);
    virq_register_passthrough(GUEST_VCPU_ID, 5, 17);
    /* eMMCB */
    virq_register_passthrough(GUEST_VCPU_ID, 222, 18);
    /* eMMCC */
    virq_register_passthrough(GUEST_VCPU_ID, 223, 19);
    /* serial */
    virq_register_passthrough(GUEST_VCPU_ID, 225, 20);
    /* GPIO IRQs */
    for (i = 96, j = 23; i < 104; i++, j++) {
        virq_register_passthrough(GUEST_VCPU_ID, i, j);
    }

    /* Finally start the guest */
    guest_start(GUEST_VCPU_ID, kernel_pc, GUEST_DTB_VADDR, GUEST_INIT_RAM_DISK_VADDR);
}

void notified(microkit_channel ch) {
    switch (ch) {
        default: {
            bool success = virq_handle_passthrough(ch);
            if (!success) {
                LOG_VMM_ERR("IRQ corresponding to channel %d dropped on vCPU %d\n", ch, GUEST_VCPU_ID);
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

