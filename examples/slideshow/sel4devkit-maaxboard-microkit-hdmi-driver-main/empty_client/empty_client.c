
/* This work is Crown Copyright NCSC, 2024. */

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>

#include "empty_client.h"
#include "frame_buffer.h"
#include "api.h"

#include "dma_offsets.h"
#include "vic_table.h"


void init(void) {
	
	init_api();

	/* Write a static image by writing the display configurations in init_static_example() and using
	   write_static_frame_buffer() to write the frame buffer. */

	// static_image(init_static_example);

	/* Write a moving image by writing the display configurations in init_moving_example() and using
	   write_moving_frame_buffer() to write the frame buffer.*/
	
	// moving_image(init_moving_example);

}

struct display_config init_static_example() {

	/* Init hd here with custom values or use pre-configured values from vic_table.h */
    struct hdmi_data hd;


	/* Return struct containing the hdmi data and the function to write the frame buffer */
	struct display_config dc = {hd, &write_static_frame_buffer};
	return dc;
}

void write_static_frame_buffer(struct hdmi_data* hd){

    if (hd == NULL){
		printf("hdmi data not yet set, cannot write frame buffer.\n;");
		return;
	}

    /* Get the frame buffer as a 8, 32 or 64 bit pointer */
	uint32_t* frame_buffer_addr = get_active_frame_buffer_uint32();

    /* The width and height of the active display region */
    int height = hd->v_active;
	int width = hd->h_active;

    /* Write to the frame buffer to display a static image */
	for (int i = 0; i < height; i++) { 
		for (int j = 0; j < width; j++) {
     
        }
    }
}

struct display_config init_moving_example(){

    /* Init hd here with custom values or use pre-configured values from vic_table.h */
    struct hdmi_data hd;


	/* Return struct containing the hdmi data and the function to write the frame buffer */
	struct display_config dc = {hd, &write_moving_frame_buffer};
	return dc;

}
void write_moving_frame_buffer(struct hdmi_data* hd){

    if (hd == NULL){
		printf("hdmi data not yet set, cannot write frame buffer.\n;");
		return;
	}

    /* Get the frame buffer as a 8, 32 or 64 bit pointer */
	uint32_t* frame_buffer_addr = get_active_frame_buffer_uint32();


    /* The width and height of the active display region */
    int height = hd->v_active;
	int width = hd->h_active;

    /* Write to the frame buffer here. This function will be called continously to update the buffer.
       Use global variables to alter the contents of this buffer so that the values persist. See Example rotating_bars.*/
	for (int i = 0; i < height; i++) { 
		for (int j = 0; j < width; j++) {
     
        }
    }
}