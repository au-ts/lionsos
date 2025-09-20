/* This work is Crown Copyright NCSC, 2024. */

#ifndef __API_H__
#define __API_H__

#include "hdmi_data.h"

void init_api();
void reset_api();
void static_image(struct display_config (*init_func)());
void moving_image(struct display_config (*init_func)());
void reset_static_image();
void run_examples();

#endif