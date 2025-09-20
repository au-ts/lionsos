/* This work is Crown Copyright NCSC, 2024. */

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>

#include <microkit.h>

#include <vic_table.h>

#include "api.h"
#include "timer.h"
#include "hdmi_data.h"
#include "dma_offsets.h"
#include "frame_buffer.h"

struct hdmi_data *hd = NULL;
uintptr_t timer_base;
int frame_count = 0;
#define MAX_FRAME_COUNT 50000000 // The maximum number of frames displayed for a moving image

// Function pointer to current frame buffer function (used for double buffering)
void (*write_fb)(struct hdmi_data*);

void init_api() {
	
	// Allocate memory to hold the hdmi data
	hd = malloc(sizeof(struct hdmi_data));

	// Initialise timer
	initialise_and_start_timer(timer_base);
}

void reset_api() {
	clear_current_frame_buffer(hd);
	clear_current_cache_buffer(hd);
	free(hd);
}

void
notified(microkit_channel ch) {
	
	switch (ch) {
        // Notified by the context loader to draw the currently inactive frame buffer
		case 52:								
			frame_count++;		
			if (frame_count < MAX_FRAME_COUNT) {
				write_fb(hd);
				microkit_notify(52);
			}
			else {
				reset_api();
			}
			break;
		default:
			printf("Unexpected channel id: %d in api::notified()\n", ch);
	}
}

void static_image(struct display_config (*init_func)()) {

	// Get the display configurations 
	struct display_config dc = init_func();
	*hd = dc.hd;
	
	// Prewrite the buffer
	dc.write_fb(hd);

	// Send the hdmi data to the dcss PD to initialise the DCSS
	microkit_ppcall(0, seL4_MessageInfo_new((uint64_t)hd, 1, 0, 0));

	ms_delay(hd->ms_delay);
}

void reset_static_image() {

	// Clear the frame buffer
	clear_current_frame_buffer(hd);
	clear_current_cache_buffer(hd);

	// Reset the DCSS for next example
	microkit_notify(55); 
}

void moving_image(struct display_config (*init_func)()){

	// Get the display configurations 
	struct display_config dc = init_func();
	*hd = dc.hd;
	
	// Prewrite the buffer
	dc.write_fb(hd);

	// set frame buffer function 
	write_fb = dc.write_fb;

	// Send the hdmi data to the dcss PD to initialise the DCSS, as this example is double buffered it will call back to the examples implementation of write_fb()
	microkit_ppcall(0, seL4_MessageInfo_new((uint64_t)hd, 1, 0, 0));
}

