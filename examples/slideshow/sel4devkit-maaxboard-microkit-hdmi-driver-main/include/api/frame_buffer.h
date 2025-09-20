/* This work is Crown Copyright NCSC, 2024. */

#ifndef __FRAME_BUFFER_H__
#define __FRAME_BUFFER_H__

#include <stdint.h>
#include "hdmi_data.h"

uint8_t* get_active_frame_buffer_uint8();
uint32_t* get_active_frame_buffer_uint32();
uint64_t* get_active_frame_buffer_uint64();

uint8_t* get_cache_frame_buffer_uint8();
uint32_t* get_cache_frame_buffer_uint32();
uint64_t* get_cache_frame_buffer_uint64();

void clear_current_frame_buffer(struct hdmi_data* hd);
void clear_current_cache_buffer(struct hdmi_data* hd);

#endif