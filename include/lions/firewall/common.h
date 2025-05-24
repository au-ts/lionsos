#pragma once

#include <stdint.h>

#define BITS_LT(N)  ((N) - 1u)
#define BITS_LE(N)  (BITS_LT(N) | (N))

#define IPV4_ADDR(a, b, c, d) ((a) | ((b) << 8) | ((c) << 16) | ((uint32_t) (d) << 24))

static char ip_addr_buf0[IPV4_ADDR_BUFLEN];
static char ip_addr_buf1[IPV4_ADDR_BUFLEN];

static char *ipaddr_to_string(uint32_t s_addr, char *buf, int buflen)
{
    char inv[3], *rp;
    uint8_t *ap, rem, n, i;
    int len = 0;

    rp = buf;
    ap = (uint8_t *)&s_addr;
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
