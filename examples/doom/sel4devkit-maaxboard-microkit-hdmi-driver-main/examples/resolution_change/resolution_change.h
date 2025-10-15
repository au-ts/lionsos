/* This work is Crown Copyright NCSC, 2024. */

#ifndef __RESOLUTION_CHANGE_H__
#define __RESOLUTION_CHANGE_H__

#include "hdmi_data.h"

struct display_config init_example();
void write_frame_buffer(struct hdmi_data* hd);

#endif