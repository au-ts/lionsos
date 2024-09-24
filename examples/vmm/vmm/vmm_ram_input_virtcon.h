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
/*
 * Convert a #define'd thing into a string
 * Need two to get at the value rather than the name of a thing
 */
#define stringise(x) #x
#define to_string(x) stringise(x)


/* Physical RAM address and size */
#define GUEST_RAM_ADDRESS 0x80000000
#define GUEST_RAM_SIZE 0x40000000
#define GUESTRAMADDR to_string(GUEST_RAM_ADDRESS)
#define GUESTRAMSIZE to_string(GUEST_RAM_SIZE)

#define GUEST_INIT_RAM_DISK_VADDR 0x8d700000
/*
 * This value will be rewritten by
 * the Makefile based on the actual initrd size
 */
#define INITRD_END 0x8ee00000
#define VIRTIO_SERIAL_ADDR 0x130000

/*
 * Shared data regions need to be at the same address in every PD
 * where they're mapped.
 * Use #defines here to say where they go.
 * Queue regions are 1 page; data regions 2 pages
 */
#define SERIAL_TX_QUEUE_VMM "0x6_000_000"
#define SERIAL_TX_DATA_VMM "0x6_002_000"
#define SERIAL_RX_QUEUE_VMM "0x6_001_000"
#define SERIAL_RX_DATA_VMM "0x6_004_000"
#define SERIAL_TX_QUEUE_DRIVER "0x4_000_000"
#define SERIAL_TX_DATA_DRIVER "0x4_002_000"
#define SERIAL_RX_QUEUE_DRIVER "0x4_001_000"
#define SERIAL_RX_DATA_DRIVER "0x4_004_000"


#define MMIO_CONSOLE_IRQ 42
#define VIRTIO_CONSOLE_IRQ (32 + MMIO_CONSOLE_IRQ)
#define VIRTIO_CONSOLE_BASE (VIRTIO_SERIAL_ADDR)
#define VIRTIO_CONSOLE_SIZE 0x1000
