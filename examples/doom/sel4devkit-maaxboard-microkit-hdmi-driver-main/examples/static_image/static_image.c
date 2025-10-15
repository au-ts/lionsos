/* This work is Crown Copyright NCSC, 2024. */

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>

#include "static_image.h"
#include "frame_buffer.h"
#include "api.h"

#include "dma_offsets.h"
#include "vic_table.h"

#include "api.h"

void init(void) {
	
	init_api();
	static_image(init_example);
	reset_static_image();
	reset_api();
}

struct display_config init_example() {
	
	// Initialise the hdmi data with custom values
	struct hdmi_data hd;

	int v_mode = 1;

	hd.h_front_porch = vic_table[v_mode][FRONT_PORCH];
	hd.h_back_porch= vic_table[v_mode][BACK_PORCH];
	hd.hsync = vic_table[v_mode][HSYNC];
	hd.v_front_porch = vic_table[v_mode][TYPE_EOF];
	hd.v_back_porch = vic_table[v_mode][SOF];
	hd.vsync= vic_table[v_mode][VSYNC];
	hd.h_active = vic_table[v_mode][H_ACTIVE];
	hd.v_active = vic_table[v_mode][V_ACTIVE]; 
	hd.hsync_pol = vic_table[v_mode][HSYNC_POL];
	hd.vsync_pol = vic_table[v_mode][VSYNC_POL];
	hd.pixel_frequency_khz = vic_table[v_mode][PIXEL_FREQ_KHZ];
	hd.h_blank = vic_table[v_mode][H_BLANK];
	hd.h_total = vic_table[v_mode][H_TOTAL];
	hd.vic_r3 = vic_table[v_mode][VIC_R3_0];
	hd.vic_pr = vic_table[v_mode][VIC_PR];
	hd.v_total = vic_table[v_mode][V_TOTAL];
	hd.rgb_format = RBGA;
	hd.alpha_enable = ALPHA_ON;
	hd.mode = STATIC_IMAGE;
	hd.ms_delay = 30000;

	// Return struct containing the hdmi data and the function to write the frame buffer
	struct display_config dc = {hd, &write_frame_buffer};
	return dc;
}

void write_frame_buffer(struct hdmi_data* hd) {
	
	printf("Writing function api 1\n");
	
	if (hd == NULL){
		printf("hdmi data not yet set, cannot write frame buffer.\n;");
		return;
	}

	uint8_t* frame_buffer_addr = get_active_frame_buffer_uint8();
	
	int height = hd->v_active;
	int width = hd->h_active;
	int first_quarter = width * 0.25;
	int second_quarter = width * 0.5;
	int third_quarter = width * 0.75;
	int alpha = 0;

	/*
		Each of the 4 values written to the frame buffer reprsents a 32 bit RGBA channel.
		They are written in the order of the hdmi_data.rgb_format member. If the format is GBRA for example, 
		Then the order of the values written below will be green, blue, red, alpha. The alpha channel configures the
		opacity of the colour, at 0xff it will be completely visible and 0x00 it will not be visible.
		It is turned on or off using hdmi_data.alpha_enable. With this option turned on, this example will display each colour bar
		starting with a 0 alhpa increasing every 3 pixels. It is much quicker to write 32 or 64bit colours - see other api examples for this.
	*/ 
	for (int i = 0; i < height; i++) { 
		for (int j = 0; j < width; j++) {
			
			// reset alpha for each colour bar
			if (j % first_quarter == 0) {
				alpha = 0;
			}

			if (j < first_quarter)
			{
				*(frame_buffer_addr++) = 0xff; 
				*(frame_buffer_addr++) = 0x00;
				*(frame_buffer_addr++) = 0x00;
				*(frame_buffer_addr++) = alpha;
			}
			else if (j < second_quarter)
			{
				*(frame_buffer_addr++) = 0x00;
				*(frame_buffer_addr++) = 0xff;
				*(frame_buffer_addr++) = 0x00;
				*(frame_buffer_addr++) = alpha;
			}
			else if (j < third_quarter)
			{
				*(frame_buffer_addr++) = 0x00;
				*(frame_buffer_addr++) = 0x00;
				*(frame_buffer_addr++) = 0xff;
				*(frame_buffer_addr++) = alpha;
			}
			else {
				*(frame_buffer_addr++) = 0xff;
				*(frame_buffer_addr++) = 0xff;
				*(frame_buffer_addr++) = 0xff;
				*(frame_buffer_addr++) = alpha;
			}

			if (j %3 == 0) {
				alpha++;
			}
		}
	}
}
