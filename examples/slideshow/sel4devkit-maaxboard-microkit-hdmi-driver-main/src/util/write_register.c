/* This work is Crown Copyright NCSC, 2024. */

#include "write_register.h"
#include <string.h>
#include <stdio.h>

void write_register(uint32_t* addr, uint32_t value) {
	*addr = value;
}

void write_uint_to_mem(unsigned int* addr, unsigned int value) {

	*addr = value;
}
