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
#include <serial_config.h>
#include <blk_config.h>

#include <uio/fs.h>

#define GUEST_RAM_SIZE 0x6000000

#if defined(BOARD_qemu_virt_aarch64)
#define GUEST_DTB_VADDR 0x47f00000
#define GUEST_INIT_RAM_DISK_VADDR 0x47000000
#elif defined(BOARD_odroidc4)
#define GUEST_DTB_VADDR 0x25f10000
#define GUEST_INIT_RAM_DISK_VADDR 0x24000000
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

/* Virtio Block */
#define BLK_CH 3

#define BLK_DATA_SIZE 0x200000

#define VIRTIO_BLK_IRQ (75)
#define VIRTIO_BLK_BASE (0x150000)
#define VIRTIO_BLK_SIZE (0x1000)

blk_req_queue_t *blk_req_queue;
blk_resp_queue_t *blk_resp_queue;
uintptr_t blk_data;
blk_storage_info_t *blk_storage_info;

static struct virtio_blk_device virtio_blk;

/* FS output to Micropython */
#define MICROPYTHON_CH 4

void uio_fs_to_vmm_ack(size_t vcpu_id, int irq, void *cookie)
{
    /* Nothing to do. */
}

bool uio_fs_from_vmm_signal(size_t vcpu_id, uintptr_t addr, size_t fsr, seL4_UserContext *regs, void *data)
{
    microkit_notify(MICROPYTHON_CH);
    return true;
}

void init(void)
{
    /* Busy wait until blk device is ready */
    while (!blk_storage_is_ready(blk_storage_info));

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
    serial_cli_queue_init_sys(microkit_name, &serial_rxq, serial_rx_queue, serial_rx_data, &serial_txq, serial_tx_queue,
                              serial_tx_data);

    /* Initialise virtIO console device */
    success = virtio_mmio_console_init(&virtio_console,
                                       VIRTIO_CONSOLE_BASE,
                                       VIRTIO_CONSOLE_SIZE,
                                       VIRTIO_CONSOLE_IRQ,
                                       &serial_rxq, &serial_txq,
                                       SERIAL_VIRT_TX_CH);

    /* virtIO block */
    /* Initialise our sDDF queues for the block device */
    blk_queue_handle_t blk_queue_h;
    blk_queue_init(&blk_queue_h,
                   blk_req_queue,
                   blk_resp_queue,
                   blk_cli_queue_capacity(microkit_name));

    /* Initialise virtIO block device */
    success = virtio_mmio_blk_init(&virtio_blk,
                                   VIRTIO_BLK_BASE, VIRTIO_BLK_SIZE, VIRTIO_BLK_IRQ,
                                   blk_data,
                                   BLK_DATA_SIZE,
                                   blk_storage_info,
                                   &blk_queue_h,
                                   BLK_CH);
    assert(success);

    /* Register the fault handler to trap guest's fault to signal FS client. */
    success = fault_register_vm_exception_handler(GUEST_TO_VMM_NOTIFY_FAULT_ADDR,
                                                  0x1000,
                                                  uio_fs_from_vmm_signal,
                                                  NULL);
    assert(success);

    /* Register the UIO IRQ */
    success = virq_register(GUEST_VCPU_ID, UIO_FS_IRQ_NUM, uio_fs_to_vmm_ack, NULL);
    assert(success);

    /* Finally start the guest */
    guest_start(GUEST_VCPU_ID, kernel_pc, GUEST_DTB_VADDR, GUEST_INIT_RAM_DISK_VADDR);
}

void notified(microkit_channel ch)
{
    switch (ch) {
    case SERIAL_VIRT_RX_CH: {
        /* We have received an event from the serial virtualiser, so we
         * call the virtIO console handling */
        virtio_console_handle_rx(&virtio_console);
        break;
    }
    case BLK_CH: {
        virtio_blk_handle_resp(&virtio_blk);
        break;
    }
    case MICROPYTHON_CH: {
        virq_inject(GUEST_VCPU_ID, UIO_FS_IRQ_NUM);
        break;
    }
    default:
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
