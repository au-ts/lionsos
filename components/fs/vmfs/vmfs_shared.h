/*
 * Copyright 2024, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#define NUM_UIO_DEVICES 5

/* UIO devices name */
#define UIO_DEV_NAME_FS_VM_CONF "vmfs_config"
#define UIO_DEV_NAME_FS_CMD     "vmfs_command"
#define UIO_DEV_NAME_FS_COMP    "vmfs_completion"
#define UIO_DEV_NAME_FS_DATA    "vmfs_data"
#define UIO_DEV_NAME_FS_FAULT   "vmfs_fault"

/* Common values between the guest and VMM */
/* This is the only hardcoded memory region size, everything else are derived from sdfgen. */
#define UIO_PATH_FS_VM_CONFIG_SZ 0x1000

typedef struct vmm_to_fs_vm_conf_data {
    uint64_t fs_cmd_queue_region_size;
    uint64_t fs_comp_queue_region_size;
    uint64_t fs_data_share_region_size;
    uint64_t fs_vm_to_vmm_fault_reg_size;
} vmm_to_guest_conf_data_t;
