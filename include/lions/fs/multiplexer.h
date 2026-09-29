/*
 * SPDX-FileCopyrightText: 2026 UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdint.h>
#include <lions/fs/protocol.h>

#define FS_MUX_CLIENT_ID_SHIFT  32
#define FS_MUX_REQUEST_ID_MASK  UINT32_MAX

/* The multiplexer uses the high 32 bits of the server-facing request ID to
 * route its completion. Clients only observe the low 32-bit request ID. */
static inline uint64_t fs_multiplexer_encode_id(uint64_t client_id,
                                                uint64_t request_id)
{
    return (client_id << FS_MUX_CLIENT_ID_SHIFT) |
           (request_id & FS_MUX_REQUEST_ID_MASK);
}

static inline uint64_t fs_multiplexer_client_id(uint64_t encoded_id)
{
    return encoded_id >> FS_MUX_CLIENT_ID_SHIFT;
}

static inline uint64_t fs_multiplexer_request_id(uint64_t encoded_id)
{
    return encoded_id & FS_MUX_REQUEST_ID_MASK;
}
