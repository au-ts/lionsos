/*
 * Copyright 2024, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

/* Common values between the guest and VMM */
/* All of these details must match up with the DTS overlay and Sys Desc File. */
/* See the vmfs example for more details. */

#define UIO_PATH_FS_VM_CONFIG "/dev/uio0"

/* This is the only hardcoded memory region size, everything else are derived from sdfgen. */
#define UIO_PATH_FS_VM_CONFIG_SZ 0x1000

#define UIO_PATH_FS_COMMAND_QUEUE_AND_IRQ "/dev/uio1"
#define UIO_PATH_FS_COMPLETION_QUEUE "/dev/uio2"
#define UIO_PATH_FS_DATA "/dev/uio3"
#define UIO_PATH_GUEST_TO_VMM_NOTIFY_FAULT "/dev/uio4"

typedef struct vmm_to_fs_vm_conf_data {
    uint64_t fs_cmd_queue_region_size;
    uint64_t fs_comp_queue_region_size;
    uint64_t fs_data_share_region_size;
    uint64_t fs_vm_to_vmm_fault_reg_size;
} vmm_to_guest_conf_data_t;
