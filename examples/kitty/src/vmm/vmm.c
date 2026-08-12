/*
 * Copyright 2023, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <stddef.h>
#include <stdint.h>
#include <microkit.h>
#include <libvmm/config.h>
#include <libvmm/guest.h>
#include <libvmm/virq.h>
#include <libvmm/tcb.h>
#include <libvmm/util/util.h>
#include <libvmm/arch/aarch64/linux.h>
#include <libvmm/arch/aarch64/fault.h>
#include <lions/fb/fb.h>

__attribute__((__section__(".vmm_config"))) vmm_config_t config;

#ifdef CONFIG_PLAT_ODROIDC4
#define GUEST_RAM_START_GPA 0x20000000
#elif CONFIG_PLAT_QEMU_ARM_VIRT
#define GUEST_RAM_START_GPA 0x40000000
#endif

/*
 * This is a virtual IRQ, meaning it does not correspond to any hardware.
 * The IRQ number is chosen because it does not overlap with any other
 * IRQs delivered by the VMM into the guest.
 */
#define UIO_GPU_IRQ 50
/* For when we get notified from MicroPython */
#define MICROPYTHON_CH 0

/* Data for the guest's kernel image. */
extern char _guest_kernel_image[];
extern char _guest_kernel_image_end[];
/* Data for the device tree to be passed to the kernel. */
extern char _guest_dtb_image[];
extern char _guest_dtb_image_end[];
/* Data for the initial RAM disk to be passed to the kernel. */
extern char _guest_initrd_image[];
extern char _guest_initrd_image_end[];

void uio_gpu_ack(irq_routing_info_t irq_routing_info, void *cookie)
{
    // Do nothing, there is no actual IRQ to ack since UIO IRQs are virtual!
}

bool uio_init_handler(size_t vcpu_id, uintptr_t addr, size_t fsr, seL4_UserContext *regs, void *data)
{
    microkit_notify(MICROPYTHON_CH);
    return true;
}

void init(void)
{
    /* Initialise the VMM, the VCPU(s), and start the guest */
    LOG_VMM("starting \"%s\"\n", microkit_name);

    arch_guest_init_t args = {
        .pci_init.mmio_aperature_size = 0, /* Disable the virtual PCI bus */
        .num_vcpus = 1,
        .num_guest_ram_regions = 1,
        .guest_ram_regions = {(struct guest_ram_region){
            .gpa_start = GUEST_RAM_START_GPA, .size = config.ram_size, .vmm_vaddr = (void *)config.ram}}};

    if (!guest_init(args))
    {
        LOG_VMM_ERR("Failed to initialise VMM\n");
        return;
    }

    /* Place all the binaries in the right locations before starting the guest */
    size_t kernel_size = _guest_kernel_image_end - _guest_kernel_image;
    size_t dtb_size = _guest_dtb_image_end - _guest_dtb_image;
    size_t initrd_size = _guest_initrd_image_end - _guest_initrd_image;

    if (!kernel_size)
    {
        LOG_VMM_ERR("Kernel image is empty\n");
        return;
    }
    if (!initrd_size)
    {
        LOG_VMM_ERR("Initial ramdisk image is empty\n");
        return;
    }
    if (!dtb_size)
    {
        LOG_VMM_ERR("DTB image is empty\n");
        return;
    }

    uintptr_t kernel_pc = linux_setup_images(GUEST_RAM_START_GPA,
                                             (uintptr_t)_guest_kernel_image,
                                             kernel_size,
                                             (uintptr_t)_guest_dtb_image,
                                             config.dtb,
                                             dtb_size,
                                             (uintptr_t)_guest_initrd_image,
                                             config.initrd,
                                             initrd_size);
    if (!kernel_pc)
    {
        LOG_VMM_ERR("Failed to initialise guest images\n");
        return;
    }

    for (int i = 0; i < config.num_irqs; i++)
    {
        bool success = virq_register_passthrough(ARM_GIC_IRQ_ROUTE(config.vcpus[0].id, config.irqs[i].irq), config.irqs[i].id);
        /* Should not be any reason for this to fail */
        assert(success);
    }

    /* Setting up the UIO region for the framebuffer */
    virq_register(ARM_GIC_IRQ_ROUTE(GUEST_BOOT_VCPU_ID, UIO_GPU_IRQ), &uio_gpu_ack, NULL);
    fault_register_vm_exception_handler(FB_UIO_INIT_ADDRESS, sizeof(size_t), &uio_init_handler, NULL);

    /* Finally start the guest */
    guest_start(kernel_pc, config.dtb, config.initrd);
}

void notified(microkit_channel ch)
{
    switch (ch)
    {
    case MICROPYTHON_CH:
    {
        bool success = virq_inject(ARM_GIC_IRQ_ROUTE(GUEST_BOOT_VCPU_ID, UIO_GPU_IRQ));
        if (!success)
        {
            LOG_VMM_ERR("IRQ %d dropped on vCPU %lu\n", UIO_GPU_IRQ, GUEST_BOOT_VCPU_ID);
        }
        break;
    }
    default:
    {
        bool success = virq_handle_passthrough(ch);
        if (!success)
        {
            LOG_VMM_ERR("IRQ %d dropped on vCPU %lu\n", vmm_config_irq_from_id(&config, ch), GUEST_BOOT_VCPU_ID);
        }
        break;
    }
    }
}

seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo)
{
    bool success = fault_handle(child, msginfo);
    if (success)
    {
        /* Now that we have handled the fault successfully, we reply to it so
         * that the guest can resume execution. */
        *reply_msginfo = microkit_msginfo_new(0, 0);
        return seL4_True;
    }

    return seL4_False;
}
