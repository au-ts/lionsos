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

#define GUEST_RAM_SIZE 0x10000000
#define GUEST_DTB_VADDR 0x3f000000
#define GUEST_INIT_RAM_DISK_VADDR 0x3d700000

#define PAGE_SIZE_4K 0x1000

/* Interrupts */
#define SERIAL_IRQ 225
#define SERIAL_IRQ_CH 1
#define ETH_IRQ 40
#define ETH_IRQ_CH 4
#define ETH_PHY_IRQ 96
#define ETH_PHY_IRQ_CH 5
#define WORK_IRQ 5
#define WORK_IRQ_CH 6

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

/* Network Virtualiser channels */
#define VIRT_NET_TX_CH  7
#define VIRT_NET_RX_CH  8

/* UIO Network Interrupts */
#define UIO_NET_TX_IRQ 71
#define UIO_NET_RX_IRQ 72

/* sDDF Networking queues  */
#include "../config/ethernet_config.h"
/* Control queues */
net_queue_t *rx_free;
net_queue_t *rx_active;
net_queue_t *tx_free;
net_queue_t *tx_active;

net_queue_handle_t rx_queue;
net_queue_handle_t tx_queue;
/* TX RX "DMA" Data regions */
uintptr_t eth_rx_buffer_data_region_paddr;
uintptr_t eth_tx_cli0_buffer_data_region_paddr;
uintptr_t eth_tx_cli1_buffer_data_region_paddr;

/* Data passing between VMM and Hypervisor */
#include <uio/net.h>
vmm_net_info_t *vmm_info_passing;

#define GUEST_TO_VMM_TX_FAULT_ADDR 0x60000000
#define GUEST_TO_VMM_RX_FAULT_ADDR 0x70000000

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

#include "../include/vmm_mem_emu.h"

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

void uio_net_to_vmm_ack(size_t vcpu_id, int irq, void *cookie) {}

bool uio_net_from_vmm_tx_signal(size_t vcpu_id, uintptr_t addr, size_t fsr, seL4_UserContext *regs, void *data)
{
    microkit_notify(VIRT_NET_TX_CH);
    return true;
}
bool uio_net_from_vmm_rx_signal(size_t vcpu_id, uintptr_t addr, size_t fsr, seL4_UserContext *regs, void *data)
{
    microkit_notify(VIRT_NET_RX_CH);
    return true;
} 

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
    if (!virq_register_passthrough(GUEST_VCPU_ID, ETH_IRQ, ETH_IRQ_CH)) {
        LOG_VMM_ERR("Failed to pass thru ETH irq\n");
        return;
    }
    if (!virq_register_passthrough(GUEST_VCPU_ID, ETH_PHY_IRQ, ETH_PHY_IRQ_CH)) {
        LOG_VMM_ERR("Failed to pass thru ETH PHY irq\n");
        return;
    }
    if (!virq_register_passthrough(GUEST_VCPU_ID, WORK_IRQ, WORK_IRQ_CH)) {
        LOG_VMM_ERR("Failed to pass thru ETH PHY irq\n");
        return;
    }

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
    
    /* Initialise UIO IRQ for TX and RX path */
    if (!virq_register(GUEST_VCPU_ID, UIO_NET_TX_IRQ, uio_net_to_vmm_ack, NULL)) {
        LOG_VMM_ERR("Failed to register TX interrupt\n");
        return;
    }
    if (!virq_register(GUEST_VCPU_ID, UIO_NET_RX_IRQ, uio_net_to_vmm_ack, NULL)) {
        LOG_VMM_ERR("Failed to register RX interrupt\n");
        return;
    }

    if (!success) {
        LOG_VMM_ERR("Failed to initialise virtio console\n");
        return;
    }

    /* Tell the VMM what the physaddr of the TX and RX data buffers are, so it can deduct it from the offset given by virtualiser */
    vmm_info_passing->rx_paddr = eth_rx_buffer_data_region_paddr;
    vmm_info_passing->tx_paddrs[0] = eth_tx_cli0_buffer_data_region_paddr;
    vmm_info_passing->tx_paddrs[1] = eth_tx_cli1_buffer_data_region_paddr;
    
    LOG_VMM("rx data physadd is 0x%p\n", vmm_info_passing->rx_paddr);
    LOG_VMM("tx cli0 data physadd is 0x%p\n",  vmm_info_passing->tx_paddrs[0]);
    LOG_VMM("tx cli1 data physadd is 0x%p\n",  vmm_info_passing->tx_paddrs[1]);

    /* Finally, register vmfault handlers for getting signals from the guest on tx and rx */
    bool tx_vmfault_reg_ok = fault_register_vm_exception_handler(GUEST_TO_VMM_TX_FAULT_ADDR, PAGE_SIZE_4K, &uio_net_from_vmm_tx_signal, NULL);
    if (!tx_vmfault_reg_ok) {
        LOG_VMM_ERR("Failed to register the VM fault handler for tx\n");
        return;
    }
    bool rx_vmfault_reg_ok = fault_register_vm_exception_handler(GUEST_TO_VMM_RX_FAULT_ADDR, PAGE_SIZE_4K, &uio_net_from_vmm_rx_signal, NULL);
    if (!rx_vmfault_reg_ok) {
        LOG_VMM_ERR("Failed to register the VM fault handler for rx\n");
        return;
    }

    // LOG_VMM("starting %s at \"%s\"\n", "ethernet driver vm", microkit_name);

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
        case VIRT_NET_TX_CH:
            if (!virq_inject(GUEST_VCPU_ID, UIO_NET_TX_IRQ)) {
                LOG_VMM_ERR("failed to inject TX UIO IRQ\n");
            }
            break;
        case VIRT_NET_RX_CH:
            if (!virq_inject(GUEST_VCPU_ID, UIO_NET_RX_IRQ)) {
                LOG_VMM_ERR("failed to inject RX UIO IRQ\n");
            }
            break;
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
