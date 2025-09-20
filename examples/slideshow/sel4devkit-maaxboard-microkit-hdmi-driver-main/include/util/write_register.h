/* This work is Crown Copyright NCSC, 2024. */

#ifndef __WRITE_REGISTER_H__
#define __WRITE_REGISTER_H__

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include "uboot_conversions.h"

inline uint32_t set_bit(uint32_t number, uint32_t bit_position) {
    return number | ((uint32_t)1 << bit_position);
}

inline bool read_bit(uint32_t number, uint32_t bit_position) {
    return (number >> bit_position) & (uint32_t)1;
}

void write_register(uint32_t* addr, uint32_t value);
void write_uint_to_mem(unsigned int* addr, unsigned int value);

#endif