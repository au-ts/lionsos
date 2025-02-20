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
#include <libvmm/virtio/mmio.h>
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

/* What each UIO regions means */
enum UIO_IDX {
    UIO_IDX_SHARED_CONFIG = 0,
    UIO_IDX_COMMAND,
    UIO_IDX_COMPLETION,
    UIO_IDX_DATA,
    UIO_IDX_FAULT,
    NUM_UIO_REGIONS
};

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
static struct virtio_console_device virtio_console;

/* From sdfgen: */
serial_queue_t *serial_rx_queue;
serial_queue_t *serial_tx_queue;
char *serial_rx_data;
char *serial_tx_data;
uint8_t serial_tx_channel;
uint8_t serial_rx_channel;

/* Virtio Block */
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

    blk_channel = blk_config.virt.id;
    blk_data_share_size = blk_config.data.size;
    blk_req_queue = (blk_req_queue_t *) blk_config.virt.req_queue.vaddr;
    blk_resp_queue = (blk_resp_queue_t *) blk_config.virt.resp_queue.vaddr;
    blk_data_share = (uintptr_t) blk_config.data.vaddr;
    blk_storage_info = (blk_storage_info_t *) blk_config.virt.storage_info.vaddr;

    client_channel = fs_server_config.client.id;

    /* Make sure the UIO regions are sound */
    assert(vmm_config.num_uio_regions == NUM_UIO_REGIONS);
    assert(vmm_config.uios[UIO_IDX_COMMAND].irq);

    /* Then fill in the shared config region between guest and VMM */
    vmm_to_guest_conf_data_t *shared_conf = (vmm_to_guest_conf_data_t *) vmm_config.uios[UIO_IDX_SHARED_CONFIG].vmm_vaddr;
    assert(shared_conf);
    shared_conf->fs_cmd_queue_region_size = fs_server_config.client.command_queue.size;
    shared_conf->fs_comp_queue_region_size = fs_server_config.client.completion_queue.size;
    shared_conf->fs_data_share_region_size = fs_server_config.client.share.size;
    shared_conf->fs_vm_to_vmm_fault_reg_size = vmm_config.uios[UIO_IDX_FAULT].size;

    /* Initialise the VMM, guest RAM, vCPU and vGIC. */
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
    success = fault_register_vm_exception_handler(vmm_config.uios[UIO_IDX_FAULT].guest_paddr,
                                                  vmm_config.uios[UIO_IDX_FAULT].size,
                                                  uio_fs_from_vmm_signal,
                                                  NULL);
    assert(success);

    /* Register the UIO virtual IRQ */
    success = virq_register(GUEST_VCPU_ID, vmm_config.uios[UIO_IDX_COMMAND].irq, uio_fs_to_vmm_ack, NULL);
    assert(success);

    /* Find the details of the VirtIO block and console device from sdfgen data */
    int console_vdev_idx = -1;
    int blk_vdev_idx = -1;
    assert(vmm_config.num_virtio_mmio_devices == 2);
    for (int i = 0; i < vmm_config.num_virtio_mmio_devices; i += 1) {
        switch (vmm_config.virtio_mmio_devices[i].type) {
            case VIRTIO_DEVICE_ID_CONSOLE:
                console_vdev_idx = i;
                break;
            case VIRTIO_DEVICE_ID_BLOCK:
                blk_vdev_idx = i;
                break;
        }
    }
    assert(console_vdev_idx != -1);
    assert(blk_vdev_idx != -1);

    /* Initialise our sDDF ring buffers for the serial device */
    serial_queue_init(&serial_rxq, serial_rx_queue, serial_config.rx.data.size, serial_rx_data);
    serial_queue_init(&serial_txq, serial_tx_queue, serial_config.tx.data.size, serial_tx_data);
    /* Initialise virtIO console device */
    success = virtio_mmio_console_init(&virtio_console,
                                       vmm_config.virtio_mmio_devices[console_vdev_idx].base,
                                       vmm_config.virtio_mmio_devices[console_vdev_idx].size,
                                       vmm_config.virtio_mmio_devices[console_vdev_idx].irq,
                                       &serial_rxq, &serial_txq,
                                       serial_tx_channel);
    assert(success);

    /* virtIO block */
    /* Initialise our sDDF queues for the block device */
    blk_queue_handle_t blk_queue_h;
    blk_queue_init(&blk_queue_h, blk_config.virt.req_queue.vaddr, blk_config.virt.resp_queue.vaddr, blk_config.virt.num_buffers);

    /* Make sure the blk device is ready */
    while (!blk_storage_is_ready(blk_storage_info));

    /* Initialise virtIO block device */
    success = virtio_mmio_blk_init(&virtio_blk,
                                   vmm_config.virtio_mmio_devices[blk_vdev_idx].base,
                                   vmm_config.virtio_mmio_devices[blk_vdev_idx].size,
                                   vmm_config.virtio_mmio_devices[blk_vdev_idx].irq,
                                   blk_data_share,
                                   blk_data_share_size,
                                   blk_storage_info,
                                   &blk_queue_h,
                                   blk_channel);
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
        virq_inject(GUEST_VCPU_ID, vmm_config.uios[UIO_IDX_COMMAND].irq);
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
