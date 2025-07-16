#pragma once

#include <stdint.h>
#include <sddf/util/util.h>
#include <string.h>

static void generic_array_shift(void* array, uint32_t entry_size, uint32_t array_len, uint32_t start_point) {
    unsigned char* arr = (unsigned char*) array;
    uint32_t len = (array_len - start_point - 1) * entry_size;
    if (len > 0) {
        uint32_t byte_offset = start_point * entry_size;
        memmove(arr + byte_offset, arr + byte_offset + entry_size, len);
    }
}