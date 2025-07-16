#pragma once

#include <stdint.h>
#include <sddf/util/util.h>
#include <string.h>

static void generic_array_shift(void* array, uint32_t entry_size, uint32_t array_len, uint32_t index_to_delete) {
    unsigned char* arr = (unsigned char*) array;
    uint32_t len = (array_len - index_to_delete - 1) * entry_size;
    if (len > 0) {
        uint32_t byte_offset = index_to_delete * entry_size;
        memmove(arr + byte_offset, arr + byte_offset + entry_size, len);
    }
}