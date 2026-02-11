/*
 * Copyright 2025, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <sddf/util/util.h>
#include <stdint.h>

/**
 * Convert a 16 bit unsigned from host byte order to network byte order.
 *
 * @param n Integer represented in host byte order.
 * @return Integer represented in network byte order.
 */
static inline uint16_t htons(uint16_t n)
{
#if BYTE_ORDER == BIG_ENDIAN
    return n;
#else
    return (n & 0xff) << 8 | (n & 0xff00) >> 8;
#endif
}

/**
 * Convert a 32 bit unsigned from host byte order to network byte order.
 *
 * @param n Integer represented in host byte order.
 * @return Integer represented in network byte order.
 */
static inline uint32_t htonl(uint32_t n)
{
#if BYTE_ORDER == BIG_ENDIAN
    return n;
#else
    return (n & 0xff) << 24 | (n & 0xff00) << 8 | ((n >> 8) & 0xff00) | n >> 24;
#endif
}

/* Subnet value of N means IPs must match on highest N bits. IP addresses are
stored big-endian, so mask byte order must be swapped for subnet match. */
#define subnet_mask(n) htonl((uint32_t)(0xffffffffUL << (32 - (n))))

#define IPV4_ADDR_BUFLEN 16

static char ip_addr_buf0[IPV4_ADDR_BUFLEN];
static char ip_addr_buf1[IPV4_ADDR_BUFLEN];

/**
 * Convert a big-endian ip address integer to a string.
 *
 * @param ip big-endian ip address.
 * @param buf buffer used to output ip address as a string.
 *
 * @return buffer or NULL upon failure.
 */
static inline char *ipaddr_to_string(uint32_t ip, char *buf)
{
    char inv[3], *rp;
    uint8_t *ap, rem, n, i;
    int len = 0;

    rp = buf;
    ap = (uint8_t *)&ip;
    for (n = 0; n < 4; n++) {
        i = 0;
        do {
            rem = *ap % (uint8_t)10;
            *ap /= (uint8_t)10;
            inv[i++] = (char)('0' + rem);
        } while (*ap);
        while (i--) {
            if (len++ >= IPV4_ADDR_BUFLEN) {
                return NULL;
            }
            *rp++ = inv[i];
        }
        if (len++ >= IPV4_ADDR_BUFLEN) {
            return NULL;
        }
        *rp++ = '.';
        ap++;
    }
    *--rp = 0;
    return buf;
}
