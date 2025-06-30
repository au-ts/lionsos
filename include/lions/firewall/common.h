#pragma once

#include <stdint.h>
#include <sddf/util/util.h>
#include <string.h>

static inline uint32_t htonl(uint32_t n) {
    return n >> 24 | (n & 0xff) << 24 | (n & 0xff00) << 8 | ((n >> 8) & 0xff00);
}
/* Subnet value of N means IPs must match on highest N bits. */
#define subnet_mask(n) htonl((uint32_t)(0xffffffffUL << (32 - (n))))

/* We store IP addresses big-endian */
#define IPV4_ADDR(a, b, c, d) ((a) | ((b) << 8) | ((c) << 16) | ((uint32_t) (d) << 24))

#define IPV4_ADDR_BUFLEN 16

#define FW_EXTERNAL_INTERFACE_ID 0
#define FW_INTERNAL_INTERFACE_ID 1

static const char *fw_frmt_str[] = {
    "EXT --> INT | ",
    "INT --> EXT | "
};

static char ip_addr_buf0[IPV4_ADDR_BUFLEN];
static char ip_addr_buf1[IPV4_ADDR_BUFLEN];

static char *ipaddr_to_string(uint32_t s_addr, char *buf)
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

static void generic_array_shift(void* array, uint32_t entry_size, uint32_t array_len, uint32_t start_point) {
    unsigned char* arr = (unsigned char*) array;
    uint32_t len = (array_len - start_point - 1) * entry_size;
    if (len > 0) {
        uint32_t byte_offset = start_point * entry_size;
        memmove(arr + byte_offset, arr + byte_offset + entry_size, len);
    }
}