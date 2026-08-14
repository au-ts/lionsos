#pragma once

#include <microkit.h>
#include <stddef.h>
#include <stdint.h>

#define NUM_DEBUGEES 2

uint32_t get_page(uint8_t child_id, uintptr_t addr, uint64_t *page_size);
uint32_t libvspace_read_word(uint16_t client, uintptr_t addr, seL4_Word *val);
uint32_t libvspace_write_word(uint16_t client, uintptr_t addr, seL4_Word val);
uint32_t libvspace_write_page(uint16_t client, uintptr_t addr, char *bytes, size_t nbytes);

void libvspace_set_small_mapping_region(uint64_t vaddr);
void libvspace_set_large_mapping_region(uint64_t vaddr);