/*
 * Copyright 2024, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <blk_config.h>

/* Common values between the guest and */
/* All of these details must match up with the DTS overlay and Sys Desc File. */

#define UIO_FS_IRQ_NUM 71

#define UIO_LENGTH_FS_COMMAND_QUEUE 0x8000
#define UIO_PATH_FS_COMMAND_QUEUE_AND_IRQ "/dev/uio0"

#define UIO_LENGTH_FS_COMPLETION_QUEUE 0x8000
#define UIO_PATH_FS_COMPLETION_QUEUE "/dev/uio1"

#define UIO_LENGTH_FS_DATA BLK_REGION_SIZE
#define UIO_PATH_FS_DATA "/dev/uio2"

#define GUEST_TO_VMM_NOTIFY_FAULT_ADDR 0x10000000
#define UIO_LENGTH_GUEST_TO_VMM_NOTIFY_FAULT 0x1000
#define UIO_PATH_GUEST_TO_VMM_NOTIFY_FAULT "/dev/uio3"
