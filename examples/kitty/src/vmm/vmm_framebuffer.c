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

#include <libvmm/virtio/virtio.h>
#include <sddf/serial/queue.h>
#include <serial_config.h>

#if defined(CONFIG_PLAT_ODROIDC4)
#define GUEST_RAM_SIZE 0x10000000
#define GUEST_DTB_VADDR 0x2f000000
#define GUEST_INIT_RAM_DISK_VADDR 0x2c000000
#elif defined(CONFIG_PLAT_QEMU_ARM_VIRT)
#define GUEST_RAM_SIZE 0x10000000
#define GUEST_DTB_VADDR 0x4f000000
#define GUEST_INIT_RAM_DISK_VADDR 0x4d000000
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
#if defined(CONFIG_PLAT_ODROIDC4)
uint32_t irqs[] = { 232, 35, 192, 193, 194, 53, 246, 71, 227, 228, 63, 62, 48, 89, 5 };
#elif defined(CONFIG_PLAT_QEMU_ARM_VIRT)
uint32_t irqs[] = { 35, 36, 37, 38 };
#else
#error "Need to define platform specific pass-through IRQs"
#endif

/* Virtio Console */
#define SERIAL_VIRT_TX_CH 1
#define SERIAL_VIRT_RX_CH 2
#define VIRTIO_CONSOLE_IRQ (74)
#define VIRTIO_CONSOLE_BASE (0x130000)
#define VIRTIO_CONSOLE_SIZE (0x1000)

serial_queue_t *serial_rx_queue;
serial_queue_t *serial_tx_queue;

char *serial_rx_data;
char *serial_tx_data;

static struct virtio_console_device virtio_console;

/* Pinmux handling */
#define PINCTRL_DRIVER_CH 2

#include <sddf/pinctrl/board/meson/pinctrl_memranges.h>
#include <sddf/pinctrl/client.h>

// We trap any reads/writes to bus2 because the AO pinmux device
// is on this bus, and other device is in the same page as the pinmux device.
// If the memory operation is within the pinmux region, we talk to the pinmux driver.
// Otherwise, the VMM emulate the writes manually.
#define BUS2_MR_SIZE 0x1000
uintptr_t bus2_vaddr;

// Same case as above for the peripherals pinmux
#define GPIO_MR_SIZE 0x1000
uintptr_t gpio_vaddr;
// The two vaddr must be the same as phys addr

uintptr_t pinctrl_periphs_void;
uintptr_t pinctrl_ao_void;

#define PINCTRL_PERIPHS_PADDR_START 0xff634400
#define PINCTRL_PERIPHS_PADDR_END 0xff634800 // exclusive

#define PINCTRL_AO_PADDR_START 0xff800000
#define PINCTRL_AO_PADDR_END 0xff8000a8 // exclusive

// Emulate memory read and writes from the guest at the vaddr in VMM's address space
bool emulate_memory(uintptr_t vaddr, size_t fsr, seL4_UserContext *regs) {
    if (fault_is_read(fsr)) {
        uint64_t mask = fault_get_data_mask(vaddr, fsr);
        // LOG_VMM("emulate_memory(): emulated read at offset 0x%x, pc: 0x%lx, mask: 0x%lx, fsr 0x%lx\n", vaddr, regs->pc, mask, fsr);
        switch (mask) {
            case 0x000000ff: {
                uint8_t *target_vaddr = (uint8_t *) vaddr;
                asm volatile("" : : : "memory");
                uint64_t data = *target_vaddr;
                asm volatile("" : : : "memory");
                fault_emulate_write(regs, vaddr, fsr, data);
                break;
            }
            case 0x0000ffff: {
                uint16_t *target_vaddr = (uint16_t *) vaddr;
                asm volatile("" : : : "memory");
                uint64_t data = *target_vaddr;
                asm volatile("" : : : "memory");
                fault_emulate_write(regs, vaddr, fsr, data);
                break;
            }
            case 0xffffffff: {
                uint32_t *target_vaddr = (uint32_t *) vaddr;
                asm volatile("" : : : "memory");
                uint64_t data = *target_vaddr;
                asm volatile("" : : : "memory");
                fault_emulate_write(regs, vaddr, fsr, data);
                break;
            }
            case ~((uint64_t) 0): {
                uint64_t *target_vaddr = (uint64_t *) vaddr;
                asm volatile("" : : : "memory");
                uint64_t data = *target_vaddr;
                asm volatile("" : : : "memory");
                fault_emulate_write(regs, vaddr, fsr, data);
                break;
            }
            default:
                LOG_VMM_ERR("emulate_memory(): unaligned write at vaddr 0x%x\n", vaddr);
                return false;
        }
    } else {
        uint64_t mask = fault_get_data_mask(vaddr, fsr);
        // LOG_VMM("emulate_memory(): emulated write at offset 0x%x, pc: 0x%lx, mask: 0x%lx, fsr 0x%lx\n", vaddr, regs->pc, mask, fsr);

        uint64_t data = fault_get_data(regs, fsr);
        switch (mask) {
            case 0x000000ff: {
                uint8_t *target_vaddr = (uint8_t *) vaddr;
                asm volatile("" : : : "memory");
                *target_vaddr = (uint8_t) (data & mask);
                asm volatile("" : : : "memory");
                break;
            }
            case 0x0000ffff: {
                uint16_t *target_vaddr = (uint16_t *) vaddr;
                asm volatile("" : : : "memory");
                *target_vaddr = (uint16_t) (data & mask);
                asm volatile("" : : : "memory");
                break;
            }
            case 0xffffffff: {
                uint32_t *target_vaddr = (uint32_t *) vaddr;
                asm volatile("" : : : "memory");
                *target_vaddr = (uint32_t) (data & mask);
                asm volatile("" : : : "memory");
                break;
            }
            case ~((uint64_t) 0): {
                uint64_t *target_vaddr = (uint64_t *) vaddr;
                asm volatile("" : : : "memory");
                *target_vaddr = (uint64_t) (data & mask);
                asm volatile("" : : : "memory");
                break;
            }
            default:
                LOG_VMM_ERR("emulate_memory(): unaligned read at vaddr 0x%x\n", vaddr);
                return false;
        }
    }

    return true;
}

bool bus_vmfault_handler(size_t vcpu_id, uintptr_t addr, size_t fsr, seL4_UserContext *regs, void *data) {
    // Data is the base guest address for the fault.
    // If the fault is in the pinmux region, redirect to the void region
    // Else continue to the real device
    
    uintptr_t guest_fault_vaddr = (uintptr_t) data + addr;
    if (guest_fault_vaddr >= PINCTRL_PERIPHS_PADDR_START && guest_fault_vaddr < PINCTRL_PERIPHS_PADDR_END) {
        // LOG_VMM("emulating peripherals pinmux at 0x%x\n", guest_fault_vaddr);
        uintptr_t page_offset = guest_fault_vaddr & 0xFFF;
        return emulate_memory(((uintptr_t) pinctrl_periphs_void) + page_offset, fsr, regs);
    } else if (guest_fault_vaddr >= PINCTRL_AO_PADDR_START && guest_fault_vaddr < PINCTRL_AO_PADDR_END) {
        // LOG_VMM("emulating AO pinmux at 0x%x\n", guest_fault_vaddr);
        uintptr_t page_offset = guest_fault_vaddr & 0xFFF;
        return emulate_memory(((uintptr_t) pinctrl_ao_void) + page_offset, fsr, regs);
    } else {
        // LOG_VMM("emulating memory at guest vaddr 0x%x\n", guest_fault_vaddr);
        return emulate_memory(guest_fault_vaddr, fsr, regs);
    }
}

void uio_gpu_ack(size_t vcpu_id, int irq, void *cookie) {
    // Do nothing, there is no actual IRQ to ack since UIO IRQs are virtual!
}

bool uio_init_handler(size_t vcpu_id, uintptr_t addr, size_t fsr, seL4_UserContext *regs, void *data) {
    microkit_notify(MICROPYTHON_CH);
    return true;
}

void init(void) {
    /* Initialise the VMM, the VCPU(s), and start the guest */
    LOG_VMM("hello pinmux, starting \"%s\"\n", microkit_name);
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

    bool bus2_fault_reg_ok = fault_register_vm_exception_handler(bus2_vaddr, BUS2_MR_SIZE, &bus_vmfault_handler, (void *) bus2_vaddr);
    if (!bus2_fault_reg_ok) {
        LOG_VMM_ERR("Failed to register the VM fault handler for bus2\n");
        return;
    }

    bool periphs_fault_reg_ok = fault_register_vm_exception_handler(gpio_vaddr, GPIO_MR_SIZE, &bus_vmfault_handler, (void *) gpio_vaddr);
    if (!periphs_fault_reg_ok) {
        LOG_VMM_ERR("Failed to register the VM fault handler for peripherals pinmux\n");
        return;
    }

    /* Initialise our sDDF ring buffers for the serial device */
    serial_queue_handle_t serial_rxq, serial_txq;
    serial_cli_queue_init_sys(microkit_name, &serial_rxq, serial_rx_queue, serial_rx_data, &serial_txq, serial_tx_queue, serial_tx_data);

    /* Initialise virtIO console device */
    success = virtio_mmio_console_init(&virtio_console,
                                  VIRTIO_CONSOLE_BASE,
                                  VIRTIO_CONSOLE_SIZE,
                                  VIRTIO_CONSOLE_IRQ,
                                  &serial_rxq, &serial_txq,
                                  SERIAL_VIRT_TX_CH);

    /* Finally start the guest */
    guest_start(GUEST_VCPU_ID, kernel_pc, GUEST_DTB_VADDR, GUEST_INIT_RAM_DISK_VADDR);
}

void notified(microkit_channel ch) {
    switch (ch) {
        case SERIAL_VIRT_RX_CH: {
            /* We have received an event from the serial virtualiser, so we
            * call the virtIO console handling */
            virtio_console_handle_rx(&virtio_console);
            break;
        }
        case MICROPYTHON_CH: {
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
