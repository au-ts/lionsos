/* This work is Crown Copyright NCSC, 2024. */

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>

#include "rotating_bars.h"
#include "frame_buffer.h"
#include "api.h"

#include "dma_offsets.h"

#include "vic_table.h"

int width_offset = 0;
int ctx_ld_enable = 0;


#define RGBA_RED_64 0xff0000ffff0000ff
#define RGBA_GREEN_64 0xff00ff00ff00ff00
#define RGBA_BLUE_64 0xffff0000ffff0000
#define RGBA_WHITE_64 0xffffffffffffffff

void init(void) {
	init_api();
	moving_image(init_example);
}

struct display_config init_example() {

	// Initialise the vic mode with custom values
	struct hdmi_data hd = {1650, 1280, 370, 40, 110, 220, 750, 720, 5, 5, 20, 74250, 1, 1, 8, 0, 23, RGBA, ALPHA_OFF, MOVING_IMAGE, NO_DELAY}; // Vic mode 1

    // Return struct containing the hdmi data and the function to write the frame buffer
	struct display_config dc = {hd, &write_frame_buffer};
	return dc;
}

void write_frame_buffer(struct hdmi_data* hd) {
	
	if (hd == NULL){
		printf("hdmi data not yet set, cannot write frame buffer.\n;");
		return;
	}
	
	uint64_t* frame_buffer_addr = get_active_frame_buffer_uint64();

	int height = hd->v_active;
	int width = hd->h_active/2; // for 64 bit writing the width is divided by 2
	int first_quarter = width * 0.25;
	int second_quarter = width * 0.5;
	int third_quarter = width * 0.75;
	int j_pos = width_offset;

	for (int i = 0; i < height; i++) {
		for (int j = 0; j < width; j++) {
			
			if (j_pos < first_quarter)
			{
				*(frame_buffer_addr++) = RGBA_RED_64; 
			}
			else if (j_pos < second_quarter)
			{
				*(frame_buffer_addr++) = RGBA_GREEN_64; 
			}
			else if (j_pos < third_quarter)
			{
				*(frame_buffer_addr++) = RGBA_BLUE_64; 
			}
			else {
				*(frame_buffer_addr++) = RGBA_WHITE_64; 
			}
			
			j_pos++;
			if (j_pos == width)	{
				j_pos = 0;
			}
		}
	}

	width_offset += 1;
	if (width_offset == width) {
		width_offset = 0;
	}
}