/* This work is Crown Copyright NCSC, 2024. */

#ifndef __STATIC_IMAGE_H__
#define __STATIC_IMAGE_H__

#include "hdmi_data.h"

struct display_config init_example();
void write_frame_buffer(struct hdmi_data* hd); 

#endif