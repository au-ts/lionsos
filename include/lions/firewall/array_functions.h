/*
 * Copyright 2025, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdint.h>
#include <sddf/util/util.h>
#include <string.h>

/**
 * Shift all array indices past index_to_remove one index lower.
 * Can be used for a general array with entries stored consecutively.
 *
 * @param array address of the array.
 * @param entry_size size of each entry.
 * @param array_len length of array including index to remove.
 * @param index_to_remove index of the array to be removed.
 */
static void generic_array_shift(void *array,
                                uint32_t entry_size,
                                uint32_t array_len,
                                uint32_t index_to_remove)
{
    unsigned char* arr = (unsigned char *) array;
    uint32_t shift_len = (array_len - index_to_remove - 1) * entry_size;
    if (shift_len > 0) {
        uint32_t byte_offset = index_to_remove * entry_size;
        memmove(arr + byte_offset, arr + byte_offset + entry_size, shift_len);
    }
}
