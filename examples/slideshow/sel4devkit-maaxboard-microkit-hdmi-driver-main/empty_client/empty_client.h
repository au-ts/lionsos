/* This work is Crown Copyright NCSC, 2024. */

#ifndef __EMPTY_CLIENT_H__
#define __EMPTY_CLIENT_H__

#include "hdmi_data.h"

struct display_config init_static_example();
void write_static_frame_buffer(struct hdmi_data* hd); 

struct display_config init_moving_example();
void write_moving_frame_buffer(struct hdmi_data* hd); 

#endif