/* This work is Crown Copyright NCSC, 2024. */

#include "sub_sampler.h"
#include "write_register.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define COEFF 0x21612161

void write_sub_sampler_memory_registers(uintptr_t dcss_base, struct hdmi_data *hdmi_config) {

	write_register((uint32_t*)(dcss_base + SS_COEFF), COEFF);
	write_register((uint32_t*)(dcss_base + SS_CLIP_CB), 0x3ff << 16); // Set max value for cb clipping function
	write_register((uint32_t*)(dcss_base + SS_CLIP_CR), 0x3ff << 16); // Set max value for cr clipping function

	// 15.11.2.3 SUBSAM Programming Example
	int display_lrc_y = hdmi_config->v_total -1;
	int display_lrc_x = hdmi_config->h_total -1;
	int hsync_end = hdmi_config->hsync- 1;
	int vysnc_start = hdmi_config->v_front_porch - 1; 
	int vysnc_end = hdmi_config->v_front_porch +  hdmi_config->vsync - 1;
	int de_ulc_y = hdmi_config->v_back_porch + hdmi_config->v_front_porch +  hdmi_config->vsync; 
	int de_ulc_x = hdmi_config->hsync+ hdmi_config->h_back_porch - 1;
	int de_lrc_y = hdmi_config->v_total -1;
	int de_lrc_x = hdmi_config->hsync+ hdmi_config->h_back_porch + hdmi_config->h_active - 1;

	write_register((uint32_t*)(dcss_base + SS_DISPLAY),((display_lrc_y << 16) | display_lrc_x));
	write_register((uint32_t*)(dcss_base + SS_HSYNC), ((hsync_end << 16) | display_lrc_x));
	write_register((uint32_t*)(dcss_base + SS_VSYNC), ((vysnc_end << 16) | vysnc_start));
	write_register((uint32_t*)(dcss_base + SS_DE_ULC), ((1 << 31) | de_ulc_y << 16) | de_ulc_x);
	write_register((uint32_t*)(dcss_base + SS_DE_LRC),((de_lrc_y << 16) | de_lrc_x));

	write_register((uint32_t*)(dcss_base + SS_MODE), 0x0);
	write_register((uint32_t*)(dcss_base + SS_SYS_CTRL), 0x1); // Enable subsampler
}