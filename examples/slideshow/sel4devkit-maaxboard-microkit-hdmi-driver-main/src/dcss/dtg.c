/* This work is Crown Copyright NCSC, 2024. */

#include "dtg.h"
#include "write_register.h"

#include <stdio.h>
#include <stdlib.h>

// 15.3.3.1.2 Timing Controller Control Register (TC_CONTROL_STATUS)
#define TC_OVERLAY_PATH_ENABLE 2
#define TC_BLENDER_VIDEO_ALPHA_SELECT 7
#define TC_GO 8
#define TC_CH1_PER_PEL_ALPHA_SEL 10
#define TC_CSS_PIX_COMP_SWAP_0 12
#define TC_CSS_PIX_COMP_SWAP_1 13
#define TC_CSS_PIX_COMP_SWAP_2 14
#define TC_DEFAULT_OVERLAY_ALPHA_0 24
#define TC_DEFAULT_OVERLAY_ALPHA_1 25
#define TC_DEFAULT_OVERLAY_ALPHA_2 26
#define TC_DEFAULT_OVERLAY_ALPHA_3 27
#define TC_DEFAULT_OVERLAY_ALPHA_4 28
#define TC_DEFAULT_OVERLAY_ALPHA_5 29
#define TC_DEFAULT_OVERLAY_ALPHA_6 30
#define TC_DEFAULT_OVERLAY_ALPHA_7 31

void write_dtg_memory_registers(uintptr_t dcss_base, struct hdmi_data *hdmi_config) {

	// 15.3.2.4 DTG Programming Example
	write_register((uint32_t*)(dcss_base + TC_DTG_REG1),(((hdmi_config->v_total - 1) << 16) | (hdmi_config->h_total - 1)));
	write_register((uint32_t*)(dcss_base + TC_DISPLAY_REG2),
		    ((( hdmi_config->vsync + hdmi_config->v_front_porch + hdmi_config->v_back_porch -
		       1) << 16) | (hdmi_config->hsync+ hdmi_config->h_back_porch - 1)));
	write_register((uint32_t*)(dcss_base + TC_DISPLAY_REG3),
		    ((( hdmi_config->v_total -
		       1) << 16) | (hdmi_config->hsync+ hdmi_config->h_back_porch + hdmi_config->h_active - 1)));
	write_register((uint32_t*)(dcss_base + TC_CH1_REG4),
		    ((( hdmi_config->vsync + hdmi_config->v_front_porch + hdmi_config->v_back_porch -
		       1) << 16) | (hdmi_config->hsync+ hdmi_config->h_back_porch - 1)));
	write_register((uint32_t*)(dcss_base + TC_CH1_REG5),
		    ((( hdmi_config->v_total -
		       1) << 16) | (hdmi_config->hsync+ hdmi_config->h_back_porch + hdmi_config->h_active - 1)));

	write_register((uint32_t*)(dcss_base + TC_CTX_LD_REG10),((0xb << 16) | (0xa))); // Context loader x y coordinates

	uint32_t* ctrl_status = (uint32_t*)(dcss_base + TC_CONTROL_STATUS);

	// Set the default alpha
	*ctrl_status = set_bit(*ctrl_status, TC_DEFAULT_OVERLAY_ALPHA_0);
	*ctrl_status = set_bit(*ctrl_status, TC_DEFAULT_OVERLAY_ALPHA_1);
	*ctrl_status = set_bit(*ctrl_status, TC_DEFAULT_OVERLAY_ALPHA_2);
	*ctrl_status = set_bit(*ctrl_status, TC_DEFAULT_OVERLAY_ALPHA_3);
	*ctrl_status = set_bit(*ctrl_status, TC_DEFAULT_OVERLAY_ALPHA_4);
	*ctrl_status = set_bit(*ctrl_status, TC_DEFAULT_OVERLAY_ALPHA_5);
	*ctrl_status = set_bit(*ctrl_status, TC_DEFAULT_OVERLAY_ALPHA_6);
	*ctrl_status = set_bit(*ctrl_status, TC_DEFAULT_OVERLAY_ALPHA_7);

	// Pixel component ordering for the subsampler
	*ctrl_status = set_bit(*ctrl_status, TC_CSS_PIX_COMP_SWAP_0);
	*ctrl_status = set_bit(*ctrl_status, TC_CSS_PIX_COMP_SWAP_2);

	*ctrl_status = set_bit(*ctrl_status, TC_GO);
	*ctrl_status = set_bit(*ctrl_status, TC_OVERLAY_PATH_ENABLE);
	*ctrl_status = set_bit(*ctrl_status, TC_BLENDER_VIDEO_ALPHA_SELECT);
	
	if (hdmi_config->alpha_enable == ALPHA_ON) {
		*ctrl_status = set_bit(*ctrl_status, TC_CH1_PER_PEL_ALPHA_SEL);
	}
}
