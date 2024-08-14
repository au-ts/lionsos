/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Customise where Guest ram lives
 * in physical memory.
 * It will be mapped 1:1 into the VM.
 * This file is included into the system.xml file
 * and the virtual machine monitor file.
 */

#pragma once

/* Physical RAM address and size */
#define GUEST_RAM_ADDRESS 0x80000000
#define GUEST_RAM_SIZE 0x40000000

#define GUEST_INIT_RAM_DISK_VADDR 0x8d700000
/*
 * This value will be rewritten by
 * the Makefile based on the actual initrd size
 */
#define INITRD_END 0x8ee00000



#define stringise(x) #x
#define to_string(x) stringise(x)
#define GUESTRAMADDR to_string(GUEST_RAM_ADDRESS)
#define GUESTRAMSIZE to_string(GUEST_RAM_SIZE)
