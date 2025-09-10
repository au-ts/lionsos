/*
 * Copyright 2022, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once


#include <stdlib.h>
#include <stdbool.h>
#include <sddf/network/constants.h>

/**
 * Use lwIP without OS-awareness (no thread, semaphores, mutexes or mboxes).
 */
#define NO_SYS 1

/**
 * Drop support for sys_timeout and lwip-internal cyclic timers.
 */
#define LWIP_TIMERS 1

/**
 * Enable Netconn API (require to use api_lib.c).
 */
#define LWIP_NETCONN 0

/**
 * Enable Socket API (require to use sockets.c).
 */
#define LWIP_SOCKET 0

/**
 * Enable ICMP module inside the IP stack.
 */
#define LWIP_ICMP 1

/**
 * Enable DHCP module.
 */
#define LWIP_DHCP 1

/**
 * Should be set to the alignment of the CPU.
 */
#define MEM_ALIGNMENT 4

/**
 * The size of the heap memory. If the application will send
 * a lot of data that needs to be copied, this should be set high.
 */
#define MEM_SIZE 0x20000

/**
 * Enable code to support static ARP table entries (using
 * etharp_add_static_entry/etharp_remove_static_entry).
 */
#define ETHARP_SUPPORT_STATIC_ENTRIES 1

/**
 * Enable inter-task protection (and task-vs-interrupt protection)
 * for certain critical regions during buffer allocation, deallocation
 * and memory allocation and deallocation.
 */
#define SYS_LIGHTWEIGHT_PROT 0

/**
 * Support a callback function whenever an interface changes its
 * up/down status (i.e., due to DHCP IP acquisition).
 */
#define LWIP_NETIF_STATUS_CALLBACK 1

/**
 * Set options to 1 to enable checking of checksums in software for incoming
 * packets. We leave the checksum checking on RX to hardware.
 */
#define CHECKSUM_CHECK_IP 0
#define CHECKSUM_CHECK_UDP 0
#define CHECKSUM_CHECK_TCP 0
#define CHECKSUM_CHECK_ICMP 0
#define CHECKSUM_CHECK_ICMP6 0

/**
 * Set options to 1 to generate checksums in software for outgoing packets.
 */
#ifdef NETWORK_HW_HAS_CHECKSUM

/* Leave the checksum checking on tx to hw */
#define CHECKSUM_GEN_IP 0
#define CHECKSUM_GEN_UDP 0
#define CHECKSUM_GEN_TCP 0
#define CHECKSUM_GEN_ICMP 0
#define CHECKSUM_GEN_ICMP6 0

#else

#define CHECKSUM_GEN_IP 1
#define CHECKSUM_GEN_UDP 1
#define CHECKSUM_GEN_TCP 1
#define CHECKSUM_GEN_ICMP 1
#define CHECKSUM_GEN_ICMP6 1

#endif

/**
 * TCP Maximum segment size. For the receive side, this MSS is advertised
 * to the remote side when opening a connection. For the transmit size, this
 * MSS sets an upper limit on the MSS advertised by the remote host.
 */
#define TCP_MSS 1460

/**
 * The size of a TCP window - Maximum data we can receive at once. This
 * must be at least (2 * TCP_MSS) for things to work well. TCP_WND is chosen to
 * be the smallest multiple of TCP_MSS which is less than < 65535, the largest
 * TCP window that can be used without enabling window scaling
 */
#define TCP_WND (44 * TCP_MSS)

/**
 * TCP sender buffer space (bytes). To achieve good performance, this
 * should be at least 2 * TCP_MSS.
 */
#define TCP_SND_BUF TCP_WND

/**
 * TCP will queue segments that arrive out of order. Define to 0 if your
 * device is low on memory.
 */
#define TCP_QUEUE_OOSEQ 1

/**
 * TCP will support sending selective acknowledgements (SACKs).
 */
#define LWIP_TCP_SACK_OUT 1

/**
 * Set LWIP_WND_SCALE to 1 to enable window scaling.
 */
#define LWIP_WND_SCALE 1

/**
 * Set TCP_RCV_SCALE to the desired scaling factor (shift count in the
 * range of [0..14]).
 * When LWIP_WND_SCALE is enabled but TCP_RCV_SCALE is 0, we can use a large
 * send window while having a small receive window only.
 */
#define TCP_RCV_SCALE 0

/**
 * Support the TCP timestamp option.
 */
#define LWIP_TCP_TIMESTAMPS 1

/**
 * The number of buffers in the pbuf pool.
 */
#define PBUF_POOL_SIZE 1000

/*
 * Streams can hang around in FIN_WAIT state for a
 * while after closing.  Increase the max number of concurrent streams to allow
 * for a few of these while the next benchmark runs.
 */
#define MEMP_NUM_TCP_PCB 100

/**
 * The number of memp struct pbufs (used for PBUF_ROM and PBUF_REF).
 * If the application sends a lot of data out of ROM (or other static memory),
 * this should be set high.
 */
#define MEMP_NUM_PBUF (MEMP_NUM_TCP_PCB * TCP_SND_QUEUELEN)

/**
 * The number of simultaneously queued TCP segments.
 */
#define MEMP_NUM_TCP_SEG (MEMP_NUM_TCP_PCB * TCP_SND_QUEUELEN)

/**
* The number of listening TCP connections.
* (requires the LWIP_TCP option)
*/
#define MEMP_NUM_TCP_PCB_LISTEN MEMP_NUM_TCP_PCB

/**
 * The number of struct netconns.
 */
#define MEMP_NUM_NETCONN MEMP_NUM_TCP_PCB

/**
 * Enable statistics collection in lwip_stats. Set this to 0 for performance.
 */
#define LWIP_STATS 0

/* Debugging options */
#define LWIP_DEBUG
/* Change this to LWIP_DBG_LEVEL_ALL to see a trace */
#define LWIP_DBG_MIN_LEVEL              LWIP_DBG_LEVEL_SERIOUS

#define DHCP_DEBUG                      LWIP_DBG_ON
#define UDP_DEBUG                       LWIP_DBG_ON
#define ETHARP_DEBUG                    LWIP_DBG_ON
#define PBUF_DEBUG                      LWIP_DBG_ON
#define IP_DEBUG                        LWIP_DBG_ON
#define TCPIP_DEBUG                     LWIP_DBG_ON
#define DHCP_DEBUG                      LWIP_DBG_ON
#define UDP_DEBUG                       LWIP_DBG_ON
