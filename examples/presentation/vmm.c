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
#include <libvmm/virtio/virtio.h>
#include <libvmm/util/util.h>
#include <libvmm/arch/aarch64/linux.h>
#include <libvmm/arch/aarch64/fault.h>
#include <sddf/network/util.h>
#include "blk_config.h"
#include "serial_config.h"
#include "ethernet_config.h"

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
#define GUEST_DTB_VADDR 0x2f000000
#define GUEST_INIT_RAM_DISK_VADDR 0x2d700000
#elif defined(BOARD_maaxboard)
#define GUEST_DTB_VADDR 0x4f000000
#define GUEST_INIT_RAM_DISK_VADDR 0x4c000000
#else
#error Need to define guest kernel image address and DTB address
#endif

/* virtIO console */
serial_queue_t *serial_rx_queue;
serial_queue_t *serial_tx_queue;

char *serial_rx_data;
char *serial_tx_data;

struct virtio_console_device virtio_console;

#define SERIAL_TX_CH 0
#define SERIAL_RX_CH 1

#define VIRTIO_CONSOLE_IRQ (74)
#define VIRTIO_CONSOLE_BASE (0x130000)
#define VIRTIO_CONSOLE_SIZE (0x1000)

/* virtIO block */
blk_storage_info_t *blk_storage_info;
blk_req_queue_t *blk_req_queue;
blk_resp_queue_t *blk_resp_queue;
uintptr_t blk_data;

blk_queue_handle_t blk_queue;

struct virtio_blk_device virtio_blk;

#define BLK_CH 2

#define VIRTIO_BLK_IRQ (75)
#define VIRTIO_BLK_BASE (0x131000)
#define VIRTIO_BLK_SIZE (0x1000)

/* virtIO network */
net_queue_t *rx_free;
net_queue_t *rx_active;
net_queue_t *tx_free;
net_queue_t *tx_active;

net_queue_handle_t rx_queue;
net_queue_handle_t tx_queue;

// TODO: sort out these names
uintptr_t rx_buffer_data_region;
uintptr_t tx_buffer_data_region;

struct virtio_net_device virtio_net;

#define NET_VIRT_RX_CH 3
#define NET_VIRT_TX_CH 4

#define VIRTIO_NET_IRQ (76)
#define VIRTIO_NET_BASE (0x132000)
#define VIRTIO_NET_SIZE (0x1000)

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

    /* Initialise our sDDF ring buffers for the serial device */
    serial_queue_handle_t serial_rxq, serial_txq;
    serial_cli_queue_init_sys(microkit_name, &serial_rxq, serial_rx_queue, serial_rx_data, &serial_txq, serial_tx_queue, serial_tx_data);

    blk_queue_init(&blk_queue, blk_req_queue, blk_resp_queue, blk_cli_queue_size(microkit_name));
    while (!blk_storage_is_ready(blk_storage_info));

    /* Initialise virtIO console device */
    success = virtio_mmio_console_init(&virtio_console,
                                  VIRTIO_CONSOLE_BASE,
                                  VIRTIO_CONSOLE_SIZE,
                                  VIRTIO_CONSOLE_IRQ,
                                  &serial_rxq, &serial_txq,
                                  SERIAL_TX_CH);

    /* Initialise virtIO block device */
    success = virtio_mmio_blk_init(&virtio_blk,
                        VIRTIO_BLK_BASE, VIRTIO_BLK_SIZE, VIRTIO_BLK_IRQ,
                        blk_data,
                        // TODO: sort out
                        0x200000,
                        blk_storage_info,
                        &blk_queue,
                        BLK_CH);
    assert(success);

    uint8_t mac[6];
    uint64_t mac_addr = net_cli_mac_addr(microkit_name);
    net_set_mac_addr(mac, mac_addr);

    size_t rx_size, tx_size;
    net_cli_queue_capacity(microkit_name, &rx_size, &tx_size);
    net_queue_init(&rx_queue, rx_free, rx_active, rx_size);
    net_queue_init(&tx_queue, tx_free, tx_active, tx_size);
    net_buffers_init(&tx_queue, 0);

    /* Initialise virtIO net device */
    success = virtio_mmio_net_init(&virtio_net,
                               mac,
                               VIRTIO_NET_BASE,
                               VIRTIO_NET_SIZE,
                               VIRTIO_NET_IRQ,
                               &rx_queue, &tx_queue,
                               rx_buffer_data_region, tx_buffer_data_region,
                               NET_VIRT_RX_CH,
                               NET_VIRT_TX_CH);

    /* Finally start the guest */
    guest_start(GUEST_VCPU_ID, kernel_pc, GUEST_DTB_VADDR, GUEST_INIT_RAM_DISK_VADDR);
}

void notified(microkit_channel ch) {
    switch (ch) {
        case SERIAL_TX_CH:
            /* Nothing to do if we are notified for serial transmit. */
            break;
        case SERIAL_RX_CH: {
            /* We have received an event from the serial virtualiser, so we
             * call the virtIO console handling */
            virtio_console_handle_rx(&virtio_console);
            break;
        }
        case BLK_CH: {
            virtio_blk_handle_resp(&virtio_blk);
            break;
        }
        case NET_VIRT_RX_CH: {
            virtio_net_handle_rx(&virtio_net);
            break;
        }
        case NET_VIRT_TX_CH: {
            /* Nothing to do here */
            break;
        }
        default:
            LOG_VMM_ERR("Unexpected channel, ch: 0x%lx\n", ch);
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
