/* This work is Crown Copyright NCSC, 2024. */

#include "frame_buffer.h"
#include <microkit.h>
#include "dma_offsets.h"

#include <stdio.h>
#include <stdlib.h>

uintptr_t dma_base; 

uint8_t* get_active_frame_buffer_uint8(){
	uintptr_t* frame_buffer_addr_offset = (uintptr_t*)(dma_base + ACTIVE_FRAME_BUFFER_ADDR_OFFSET);
	uint8_t* frame_buffer_addr = (uint8_t*)(dma_base + *frame_buffer_addr_offset);
	return frame_buffer_addr;
}

uint32_t* get_active_frame_buffer_uint32(){
	uintptr_t* frame_buffer_addr_offset = (uintptr_t*)(dma_base + ACTIVE_FRAME_BUFFER_ADDR_OFFSET);
	uint32_t* frame_buffer_addr = (uint32_t*)(dma_base + *frame_buffer_addr_offset);
	return frame_buffer_addr;
}

uint64_t* get_active_frame_buffer_uint64(){
	uintptr_t* frame_buffer_addr_offset = (uintptr_t*)(dma_base + ACTIVE_FRAME_BUFFER_ADDR_OFFSET);
	uint64_t* frame_buffer_addr = (uint64_t*)(dma_base + *frame_buffer_addr_offset);
	return frame_buffer_addr;
}

uint8_t* get_cache_frame_buffer_uint8(){
	uintptr_t* frame_buffer_addr_offset = (uintptr_t*)(dma_base + CACHE_FRAME_BUFFER_ADDR_OFFSET);
	uint8_t* frame_buffer_addr = (uint8_t*)(dma_base + *frame_buffer_addr_offset);
	return frame_buffer_addr;
}

uint32_t* get_cache_frame_buffer_uint32(){
	uintptr_t* frame_buffer_addr_offset = (uintptr_t*)(dma_base + CACHE_FRAME_BUFFER_ADDR_OFFSET);
	uint32_t* frame_buffer_addr = (uint32_t*)(dma_base + *frame_buffer_addr_offset);
	return frame_buffer_addr;
}

uint64_t* get_cache_frame_buffer_uint64(){
	uintptr_t* frame_buffer_addr_offset = (uintptr_t*)(dma_base + CACHE_FRAME_BUFFER_ADDR_OFFSET);
	uint64_t* frame_buffer_addr = (uint64_t*)(dma_base + *frame_buffer_addr_offset);
	return frame_buffer_addr;
}


void clear_current_frame_buffer(struct hdmi_data* hd) {
	
	uint64_t* frame_buffer_addr = get_active_frame_buffer_uint64();

	if (hd == NULL){
		printf("hdmi data not yet set, cannot write frame buffer.\n;");
		return;
	}
	
	int height = hd->v_active;
	int width = hd->h_active/2;

	for (int i = 0; i < height; i++) {
		for (int j = 0; j < width; j++) {
			*(frame_buffer_addr++) = 0x00;
		}
	}
}


void clear_current_cache_buffer(struct hdmi_data* hd) {
	
	uint64_t* frame_buffer_addr = get_cache_frame_buffer_uint64();

	if (hd == NULL){
		printf("hdmi data not yet set, cannot write frame buffer.\n;");
		return;
	}
	
	int height = hd->v_active;
	int width = hd->h_active/2;

	for (int i = 0; i < height; i++) {
		for (int j = 0; j < width; j++) {
			*(frame_buffer_addr++) = 0x00;
		}
	}
}


