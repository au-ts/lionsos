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
#include <libvmm/util/util.h>
#include <libvmm/virtio/virtio.h>
#include <libvmm/arch/aarch64/linux.h>
#include <libvmm/arch/aarch64/fault.h>

#include <sddf/serial/queue.h>
#include <sddf/blk/queue.h>

#include <libvmm/config.h>
#include <sddf/blk/config.h>
#include <sddf/serial/config.h>
#include <lions/fs/config.h>

#include <vmfs_shared.h>

__attribute__((__section__(".vmm_config"))) vmm_config_t vmm_config;
__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;
__attribute__((__section__(".blk_client_config"))) blk_client_config_t blk_config;
__attribute__((__section__(".fs_server_config"))) fs_server_config_t fs_server_config;

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

/* Virtio Console */
serial_queue_handle_t serial_rxq, serial_txq;

/* From guest DTS overlay: */
#define VIRTIO_CONSOLE_IRQ (74)
#define VIRTIO_CONSOLE_BASE (0x130000)
#define VIRTIO_CONSOLE_SIZE (0x1000)
static struct virtio_console_device virtio_console;

/* From sdfgen: */
serial_queue_t *serial_rx_queue;
serial_queue_t *serial_tx_queue;
char *serial_rx_data;
char *serial_tx_data;
uint8_t serial_tx_channel;
uint8_t serial_rx_channel;

/* Virtio Block */
#define VIRTIO_BLK_IRQ (75)
#define VIRTIO_BLK_BASE (0x150000)
#define VIRTIO_BLK_SIZE (0x1000)
static struct virtio_blk_device virtio_blk;

/* From sdfgen: */
uint8_t blk_channel;
uint64_t blk_data_share_size;
blk_req_queue_t *blk_req_queue;
blk_resp_queue_t *blk_resp_queue;
uintptr_t blk_data_share;
blk_storage_info_t *blk_storage_info;

/* Client ch, also from sdfgen */
uint8_t client_channel;

void uio_fs_to_vmm_ack(size_t vcpu_id, int irq, void *cookie)
{
    /* Nothing to do. */
}

bool uio_fs_from_vmm_signal(size_t vcpu_id, uintptr_t addr, size_t fsr, seL4_UserContext *regs, void *data)
{
    microkit_notify(client_channel);
    return true;
}

void init(void)
{
    /* Extract data we need from the sdfgen tool */
    assert(serial_config_check_magic(&serial_config));
    assert(vmm_config_check_magic(&vmm_config));
    assert(blk_config_check_magic(&blk_config));
    assert(fs_config_check_magic(&fs_server_config));

    serial_rx_queue = (serial_queue_t *) serial_config.rx.queue.vaddr;
    serial_tx_queue = (serial_queue_t *) serial_config.tx.queue.vaddr;
    serial_rx_data = (char *) serial_config.rx.data.vaddr;
    serial_tx_data = (char *) serial_config.tx.data.vaddr;
    serial_rx_channel = serial_config.rx.id;
    serial_tx_channel = serial_config.tx.id;

    /* Initialise our sDDF ring buffers for the serial device */
    serial_queue_init(&serial_rxq, serial_rx_queue, serial_config.rx.data.size, serial_rx_data);
    serial_queue_init(&serial_txq, serial_tx_queue, serial_config.tx.data.size, serial_tx_data);
    /* Initialise virtIO console device */
    success = virtio_mmio_console_init(&virtio_console,
                                       VIRTIO_CONSOLE_BASE,
                                       VIRTIO_CONSOLE_SIZE,
                                       VIRTIO_CONSOLE_IRQ,
                                       &serial_rxq, &serial_txq,
                                       serial_tx_channel);

    blk_channel = blk_config.virt.id;
    blk_data_share_size = blk_config.data.size;
    blk_req_queue = (blk_req_queue_t *) blk_config.virt.req_queue.vaddr;
    blk_resp_queue = (blk_resp_queue_t *) blk_config.virt.resp_queue.vaddr;
    blk_data_share = (uintptr_t) blk_config.data.vaddr;
    blk_storage_info = (blk_storage_info_t *) blk_config.virt.storage_info.vaddr;

    /* virtIO block */
    /* Initialise our sDDF queues for the block device */
    blk_queue_handle_t blk_queue_h;
    blk_queue_init(&blk_queue_h, blk_config.virt.req_queue.vaddr, blk_config.virt.resp_queue.vaddr, blk_config.virt.num_buffers);

    /* Make sure the blk device is ready */
    while (!blk_storage_is_ready(blk_storage_info));

    /* Initialise virtIO block device */
    success = virtio_mmio_blk_init(&virtio_blk,
                                   VIRTIO_BLK_BASE, VIRTIO_BLK_SIZE, VIRTIO_BLK_IRQ,
                                   blk_data_share,
                                   blk_data_share_size,
                                   blk_storage_info,
                                   &blk_queue_h,
                                   blk_channel);
    assert(success);

    client_channel = fs_server_config.client.id;

    /* Initialise the VMM, the VCPU(s), and start the guest */
    LOG_VMM("starting \"%s\"\n", microkit_name);
    /* Place all the binaries in the right locations before starting the guest */
    size_t kernel_size = _guest_kernel_image_end - _guest_kernel_image;
    size_t dtb_size = _guest_dtb_image_end - _guest_dtb_image;
    size_t initrd_size = _guest_initrd_image_end - _guest_initrd_image;
    uintptr_t kernel_pc = linux_setup_images(vmm_config.ram,
                                             (uintptr_t) _guest_kernel_image,
                                             kernel_size,
                                             (uintptr_t) _guest_dtb_image,
                                             vmm_config.dtb,
                                             dtb_size,
                                             (uintptr_t) _guest_initrd_image,
                                             vmm_config.initrd,
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

    /* Register the fault handler to trap guest's fault to signal FS client. */
    success = fault_register_vm_exception_handler(GUEST_TO_VMM_NOTIFY_FAULT_ADDR,
                                                  0x1000,
                                                  uio_fs_from_vmm_signal,
                                                  NULL);
    assert(success);

    /* Register the UIO virtual IRQ */
    success = virq_register(GUEST_VCPU_ID, UIO_FS_IRQ_NUM, uio_fs_to_vmm_ack, NULL);
    assert(success);

    /* Finally start the guest */
    guest_start(GUEST_VCPU_ID, kernel_pc, vmm_config.dtb, vmm_config.initrd);
}

void notified(microkit_channel ch)
{
    /* Ideally we should use switch statement here for better performance but the channel numbers
       are only known at link time. */
    if (ch == serial_rx_channel) {
        /* Console input */
        virtio_console_handle_rx(&virtio_console);
    } else if (ch == blk_channel) {
        /* Response from block driver */
        virtio_blk_handle_resp(&virtio_blk);
    } else if (ch == client_channel) {
        /* Command from client */
        virq_inject(GUEST_VCPU_ID, UIO_FS_IRQ_NUM);
    } else {
        LOG_VMM_ERR("Unexpected channel, ch: 0x%lx\n", ch);
    }
}

seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo)
{
    bool success = fault_handle(child, msginfo);
    if (success) {
        /* Now that we have handled the fault successfully, we reply to it so
         * that the guest can resume execution. */
        *reply_msginfo = microkit_msginfo_new(0, 0);
        return seL4_True;
    }

    return seL4_False;
}
