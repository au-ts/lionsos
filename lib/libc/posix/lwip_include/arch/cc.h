/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2001-2003 Swedish Institute of Computer Science.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef  uint8_t  u8_t;
typedef uint16_t u16_t;
typedef uint32_t u32_t;
typedef uint64_t u64_t;

typedef  int8_t   s8_t;
typedef int16_t  s16_t;
typedef int32_t  s32_t;
typedef int64_t  s64_t;

typedef uintptr_t mem_ptr_t;


#define U16_F "hu"
#define S16_F "d"
#define X16_F "hx"
#define U32_F "u"
#define S32_F "d"
#define X32_F "x"
#define SZT_F "lu"


// BYTE_ORDER might be defined by the architecture
#ifndef BYTE_ORDER
#if defined(__BYTE_ORDER__)
#  define BYTE_ORDER __BYTE_ORDER__
#elif defined(__BIG_ENDIAN)
#  define BYTE_ORDER BIG_ENDIAN
#elif defined(__LITTLE_ENDIAN)
#  define BYTE_ORDER LITTLE_ENDIAN
#else
#  error Unable to detemine system endianess
#endif
#endif


#define LWIP_CHKSUM_ALGORITHM 3

#define PACK_STRUCT_STRUCT __attribute__((packed))
#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_END


#define LWIP_PLATFORM_BYTESWAP 1
#define LWIP_PLATFORM_HTONS(x) ( (((u16_t)(x))>>8) | (((x)&0xFF)<<8) )
#define LWIP_PLATFORM_HTONL(x) ( (((u32_t)(x))>>24) | (((x)&0xFF0000)>>8) \
                               | (((x)&0xFF00)<<8) | (((x)&0xFF)<<24) )

#define LWIP_RAND                       rand

/* Plaform specific diagnostic output */
#define LWIP_PLATFORM_DIAG(x)                                   \
        do {                                                    \
            printf x;                                           \
        } while(0)

#define LWIP_PLATFORM_ASSERT(x)                                 \
        do {                                                    \
            if (!x) {                                           \
                printf("assertion violated: %s : %s:%d:%s\n",     \
                       #x, __FILE__, __LINE__, __FUNCTION__);   \
                while(1);                                       \
            }                                                   \
        } while(0)
