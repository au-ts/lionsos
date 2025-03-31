/*
 * Copyright 2020, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdlib.h>

#define NO_SYS 1
#define LWIP_TIMERS 1
#define LWIP_NETCONN 0
#define LWIP_SOCKET 0
#define LWIP_IPV4 1
#define LWIP_ICMP 1
#define LWIP_RAND rand
#define LWIP_DHCP 0
#define LWIP_IGMP 1
#define LWIP_DNS 1
#define NETWORK_HW_HAS_CHECKSUM 1

#ifdef NETWORK_HW_HAS_CHECKSUM

/* Leave the checksum checking on tx to hw */
#define CHECKSUM_GEN_IP                 0
#define CHECKSUM_GEN_UDP                0
#define CHECKSUM_GEN_TCP                0
#define CHECKSUM_GEN_ICMP               0
#define CHECKSUM_GEN_ICMP6              0

#else

#define CHECKSUM_GEN_IP                 1
#define CHECKSUM_GEN_UDP                1
#define CHECKSUM_GEN_TCP                1
#define CHECKSUM_GEN_ICMP               1
#define CHECKSUM_GEN_ICMP6              1

#endif

#define MEM_ALIGNMENT 4
#define MEM_SIZE 0x30000

#define ETHARP_SUPPORT_STATIC_ENTRIES 1
#define SYS_LIGHTWEIGHT_PROT 0
#define LWIP_NETIF_STATUS_CALLBACK 1

/* Leave the checksum checking on RX to hardware */
#define CHECKSUM_CHECK_IP 0
#define CHECKSUM_CHECK_UDP 0
#define CHECKSUM_CHECK_TCP 0
#define CHECKSUM_CHECK_ICMP 0
#define CHECKSUM_CHECK_ICMP6 0

#define TCP_MSS 2000 // maximum segment size, max size of a single packet
#define TCP_WND 0x800000 // tcp window, max data we can receive at once
#define TCP_SND_BUF TCP_WND // send buffer space
#define TCP_SNDLOWAT TCP_MSS

#define TCP_QUEUE_OOSEQ 1 // hold out-of-sequence packets instead of immediately dropping them
#define LWIP_TCP_SACK_OUT 1 // support sending selective acknowledgements

#define LWIP_WND_SCALE 1 // support window sizes > 65536
#define TCP_RCV_SCALE 12

#define LWIP_TCP_TIMESTAMPS 1 // support tcp timestamp option

#define PBUF_POOL_SIZE 5000
#define MEMP_NUM_PBUF TCP_SND_QUEUELEN
#define MEMP_NUM_TCP_SEG TCP_SND_QUEUELEN
#define MEMP_NUM_SYS_TIMEOUT 512
#define MEMP_NUM_TCP_PCB 100
#define MEMP_NUM_TCP_PCB_LISTEN 100
#define MEMP_NUM_NETCONN 100

/* Set this to 0 for performance */
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
