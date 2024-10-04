/*
 * Copyright 2023, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <microkit.h>
#include <libvmm/guest.h>
#include <libvmm/virq.h>
#include <libvmm/util/util.h>
#include <libvmm/arch/aarch64/linux.h>
#include <libvmm/arch/aarch64/fault.h>

#include <libvmm/virtio/virtio.h>
#include <sddf/serial/queue.h>
#include <serial_config.h>

// @ivanv: ideally we would have none of these hardcoded values
// initrd, ram size come from the DTB
// We can probably add a node for the DTB addr and then use that.
// Part of the problem is that we might need multiple DTBs for the same example
// e.g one DTB for VMM one, one DTB for VMM two. we should be able to hide all
// of this in the build system to avoid doing any run-time DTB stuff.

/*
 * As this is just an example, for simplicity we just make the size of the
 * guest's "RAM" the same for all platforms. For just booting Linux with a
 * simple user-space, 0x10000000 bytes (256MB) is plenty.
 */
#define GUEST_RAM_SIZE 0x10000000

#if defined(BOARD_qemu_virt_aarch64)
#define GUEST_DTB_VADDR 0x4f000000
#define GUEST_INIT_RAM_DISK_VADDR 0x4d700000
#elif defined(BOARD_rpi4b_hyp)
#define GUEST_DTB_VADDR 0x2e000000
#define GUEST_INIT_RAM_DISK_VADDR 0x2d700000
#elif defined(BOARD_odroidc2_hyp)
#define GUEST_DTB_VADDR 0x2f000000
#define GUEST_INIT_RAM_DISK_VADDR 0x2d700000
#elif defined(BOARD_odroidc4)
#define GUEST_DTB_VADDR 0x4f000000
#define GUEST_INIT_RAM_DISK_VADDR 0x4d700000
#elif defined(BOARD_maaxboard)
#define GUEST_DTB_VADDR 0x4f000000
#define GUEST_INIT_RAM_DISK_VADDR 0x4c000000
#else
#error Need to define guest kernel image address and DTB address
#endif

/* For simplicity we just enforce the serial IRQ channel number to be the same
 * across platforms. */
#define SERIAL_IRQ_CH 1

#if defined(BOARD_qemu_virt_aarch64)
#define SERIAL_IRQ 33
#elif defined(BOARD_odroidc4)
#define SD_IRQ 222
#define SD_IRQ_CH 5
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

#elif defined(BOARD_rpi4b_hyp)
#define SERIAL_IRQ 57
#elif defined(BOARD_imx8mm_evk)
#define SERIAL_IRQ 59
#elif defined(BOARD_imx8mq_evk) || defined(BOARD_maaxboard)
#define SERIAL_IRQ 58
#else
#error Need to define serial interrupt
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

#define BOARD_odroidc4 // @billn remove this

#if defined(BOARD_odroidc4)

/* Where the memory regions are, but there are other devices sharing the same page */
#define PINCTRL_PERIPHS_MR_PADDR_START 0xff634000
#define PINCTRL_PERIPHS_MR_SIZE 0x1000
#define PINCTRL_AO_MR_PADDR_START 0xff800000
#define PINCTRL_AO_MR_SIZE 0x1000

/* Where the pinmux actually is in the region */
#define PINCTRL_PERIPHS_PADDR_START 0xff634400
#define PINCTRL_PERIPHS_PADDR_END 0xff634800 // exclusive
#define PINCTRL_AO_PADDR_START 0xff800000
#define PINCTRL_AO_PADDR_END 0xff8000a8 // exclusive

#define CLK_CNTL_PADDR_START 0xff63c000
#define CLK_CNTL_PADDR_END 0xff63d000
#define CLK_CNTL_MR_SIZE 0x1000

uintptr_t pinctrl_periphs_void;
uintptr_t pinctrl_ao_void;
uintptr_t clk_void;

#include "../../../vmm_mem_emu.h"

bool clk_vmfault_handler(size_t vcpu_id, uintptr_t addr, size_t fsr, seL4_UserContext *regs, void *data) {
    uintptr_t phys_addr = addr + CLK_CNTL_PADDR_START;
    if (fault_is_read(fsr)) {
        uint32_t *target_phys_vaddr = (uint32_t *)phys_addr;
        asm volatile("" : : : "memory");
        uint64_t phys_data = *target_phys_vaddr;
        asm volatile("" : : : "memory");

        uint32_t *target_void_vaddr = (uint32_t *)(clk_void + addr);
        asm volatile("" : : : "memory");
        uint64_t void_data = *target_void_vaddr;
        asm volatile("" : : : "memory");
        if (phys_addr == 0xff63c098 && void_data == 0x14090496) {
            // printf("1111\n");
            fault_emulate_write(regs, phys_addr, fsr, 0x84090496);
        } else {
            fault_emulate_write(regs, phys_addr, fsr, phys_data);

        }
        // printf("CLK|READ: vaddr(0x%llx) phys_data(0x%lx) void_data(0x%x)\n", phys_addr, phys_data, void_data);

    } else {
        uint64_t mask = fault_get_data_mask(addr, fsr);
        uint64_t data = fault_get_data(regs, fsr);

        uint32_t *target_void_vaddr = (uint32_t *)(clk_void + addr);
        asm volatile("" : : : "memory");
        *target_void_vaddr = (uint32_t)(data & mask);
        asm volatile("" : : : "memory");

        uint32_t *target_phys_vaddr = (uint32_t *)phys_addr;
        asm volatile("" : : : "memory");
        uint64_t phys_data = *target_phys_vaddr;
        asm volatile("" : : : "memory");

        if (phys_data != data) {
            // printf("CLK|NATIVE: vaddr(0x%llx) data(0x%lx) mask(0x%llx)\n", phys_addr, phys_data, mask);
            // printf("CLK|WRITE: vaddr(0x%llx) data(0x%lx) mask(0x%llx)\n", phys_addr, data, mask);
        } else {
            // printf("CLK|MATCHED WRITE: vaddr(0x%llx) data(0x%lx) mask(0x%llx)\n", phys_addr, data, mask);
        }
    }

    return true;
}

bool pinmux_vmfault_handler(size_t vcpu_id, uintptr_t addr, size_t fsr, seL4_UserContext *regs, void *data) {
    // Data is the base guest address for the fault.
    // If the fault is in the pinmux region, redirect to the void region
    // Else continue to the real device
    
    uintptr_t guest_fault_vaddr = (uintptr_t) data + addr;
    if (guest_fault_vaddr >= PINCTRL_PERIPHS_PADDR_START && guest_fault_vaddr < PINCTRL_PERIPHS_PADDR_END) {
        // LOG_VMM("sending pinmux periphs access at 0x%lx to the void at 0x%lx\n", addr + 0xff634000, addr + pinctrl_periphs_void);
        uintptr_t page_offset = guest_fault_vaddr & 0xFFF;
        return emulate_memory(((uintptr_t) pinctrl_periphs_void) + page_offset, fsr, regs);
    } else if (guest_fault_vaddr >= PINCTRL_AO_PADDR_START && guest_fault_vaddr < PINCTRL_AO_PADDR_END) {
        // LOG_VMM("sending pinmux AO access at 0x%lx to the void at 0x%lx\n", addr + 0xff800000, addr + pinctrl_periphs_void);
        uintptr_t page_offset = guest_fault_vaddr & 0xFFF;
        return emulate_memory(((uintptr_t) pinctrl_ao_void) + page_offset, fsr, regs);
    } else {
        // LOG_VMM("emulating memory at guest vaddr 0x%x\n", guest_fault_vaddr);
        return emulate_memory(guest_fault_vaddr, fsr, regs);
    }
}

#endif

void init(void) {
    /* Initialise the VMM, the VCPU(s), and start the guest */
    
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
    if (!virq_register_passthrough(GUEST_VCPU_ID, SD_IRQ, SD_IRQ_CH)) {
        LOG_VMM_ERR("Failed to pass thru SD card irq\n");
        return;
    }

#if defined(BOARD_odroidc4)
    /* Trap all access to pinmux into the hypervisor for emulation */
    bool bus2_fault_reg_ok = fault_register_vm_exception_handler(PINCTRL_AO_MR_PADDR_START, PINCTRL_AO_MR_SIZE, &pinmux_vmfault_handler, (void *) PINCTRL_AO_MR_PADDR_START);
    if (!bus2_fault_reg_ok) {
        LOG_VMM_ERR("Failed to register the VM fault handler for bus2\n");
        return;
    }

    bool periphs_fault_reg_ok = fault_register_vm_exception_handler(PINCTRL_PERIPHS_MR_PADDR_START, PINCTRL_PERIPHS_MR_SIZE, &pinmux_vmfault_handler, (void *) PINCTRL_PERIPHS_MR_PADDR_START);
    if (!periphs_fault_reg_ok) {
        LOG_VMM_ERR("Failed to register the VM fault handler for peripherals pinmux\n");
        return;
    }

    /* Trap all access to clk into the hypervisor for emulation */
    bool clk_fault_reg_ok = fault_register_vm_exception_handler(CLK_CNTL_PADDR_START, CLK_CNTL_MR_SIZE, &clk_vmfault_handler, NULL);
    if (!clk_fault_reg_ok) {
        LOG_VMM_ERR("Failed to register the VM fault handler for clk\n");
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
    
    if (!success) {
        LOG_VMM_ERR("Failed to initialise virtio console\n");
        return;
    }
#endif

    LOG_VMM("starting %s at \"%s\"\n", VMM_MACHINE_NAME, microkit_name);

    /* Finally start the guest */
    guest_start(GUEST_VCPU_ID, kernel_pc, GUEST_DTB_VADDR, GUEST_INIT_RAM_DISK_VADDR);
}

void notified(microkit_channel ch) {
    bool handled = virq_handle_passthrough(ch);
    switch (ch) {
        case SERIAL_VIRT_RX_CH: {
            /* We have received an event from the serial virtualiser, so we
            * call the virtIO console handling */
            virtio_console_handle_rx(&virtio_console);
            break;
        }
        default:
            if (handled) {
                return;
            }
            printf("Unexpected channel, ch: 0x%lx\n", ch);
    }
}

/*
 * The primary purpose of the VMM after initialisation is to act as a fault-handler.
 * Whenever our guest causes an exception, it gets delivered to this entry point for
 * the VMM to handle.
 */
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
