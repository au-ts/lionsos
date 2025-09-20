/* This work is Crown Copyright NCSC, 2024. */

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>

#include "resolution_change.h"
#include "frame_buffer.h"
#include "api.h"

#include "dma_offsets.h"
#include "vic_table.h"

#define API_EXAMPLE_2_SIDE_LENGTH 300
#define RBGA_GREEN 0x00ff0000

int vic_mode = 1; // Must only be values 0-2

void init(void) {
	
	init_api();

	for (int i = 0; i < 3; i++) {
		vic_mode = i;
		static_image(init_example);
		reset_static_image();
	}

	reset_api();
}


struct display_config init_example() {

	struct hdmi_data hd;
	hd.h_front_porch = vic_table[vic_mode][FRONT_PORCH];
	hd.h_back_porch= vic_table[vic_mode][BACK_PORCH];
	hd.hsync = vic_table[vic_mode][HSYNC];
	hd.v_front_porch = vic_table[vic_mode][TYPE_EOF];
	hd.v_back_porch = vic_table[vic_mode][SOF];
	hd.vsync= vic_table[vic_mode][VSYNC];
	hd.h_active = vic_table[vic_mode][H_ACTIVE];
	hd.v_active = vic_table[vic_mode][V_ACTIVE]; 
	hd.hsync_pol = vic_table[vic_mode][HSYNC_POL];
	hd.vsync_pol = vic_table[vic_mode][VSYNC_POL];
	hd.pixel_frequency_khz = vic_table[vic_mode][PIXEL_FREQ_KHZ];
	hd.h_blank = vic_table[vic_mode][H_BLANK];
	hd.h_total = vic_table[vic_mode][H_TOTAL];
	hd.vic_r3 = vic_table[vic_mode][VIC_R3_0];
	hd.vic_pr = vic_table[vic_mode][VIC_PR];
	hd.v_total = vic_table[vic_mode][V_TOTAL];
	hd.rgb_format = RBGA;
	hd.alpha_enable = ALPHA_OFF;
	hd.mode = STATIC_IMAGE;
	hd.ms_delay = 10000;

	// Return struct containing the hdmi data and the function to write the frame buffer
	struct display_config dc = {hd, &write_frame_buffer};
	return dc;
}

void write_frame_buffer(struct hdmi_data* hd) {
	
	printf("Writing function api 2\n");
	
	if (hd == NULL){
		printf("hdmi data not yet set, cannot write frame buffer.\n;");
		return;
	}

	uint32_t* frame_buffer_addr = get_active_frame_buffer_uint32();
	
	int width = hd->h_active;

	for (int i = 0; i < API_EXAMPLE_2_SIDE_LENGTH; i++) {
		for (int j = 0; j < API_EXAMPLE_2_SIDE_LENGTH; j++) {
			*(frame_buffer_addr++) = RBGA_GREEN;
		}
		frame_buffer_addr += (width-API_EXAMPLE_2_SIDE_LENGTH);
	}
}
