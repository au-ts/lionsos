/* This work is Crown Copyright NCSC, 2024. */

#include "scaler.h"
#include "write_register.h"

#include <stdio.h>
#include <stdlib.h>

// 15.8.3.1.2 Scale Control Register (SCALE_CTRL)
#define RUN_EN 0
#define ENABLE_REPEAT 4

#define INC_VAL 0x2000

void write_scaler_memory_registers(uintptr_t dcss_base, struct hdmi_data *hdmi_config) {

	write_register((uint32_t*)(dcss_base  + SCALE_SRC_DATA_CTRL), 0x0); // Must be initialised
	write_register((uint32_t*)(dcss_base  + SCALE_SRC_FORMAT), 0x2); // Sets to RGB
	write_register((uint32_t*)(dcss_base  + SCALE_DST_FORMAT), 0x2); // Sets to RGB
	write_register((uint32_t*)(dcss_base  + SCALE_SRC_LUMA_RES),
		    ((hdmi_config->v_active - 1) << 16 | (hdmi_config->h_active - 1)));
	write_register((uint32_t*)(dcss_base  + SCALE_SRC_CHROMA_RES),
		    ((hdmi_config->v_active - 1) << 16 | (hdmi_config->h_active - 1)));
	write_register((uint32_t*)(dcss_base  + SCALE_DST_CHROMA_RES),
		    ((hdmi_config->v_active - 1) << 16 | (hdmi_config->h_active - 1)));
	write_register((uint32_t*)(dcss_base  + SCALE_V_LUMA_INC), INC_VAL);
	write_register((uint32_t*)(dcss_base  + SCALE_H_LUMA_INC), INC_VAL);
	write_register((uint32_t*)(dcss_base  + SCALE_V_CHROMA_INC), INC_VAL);
	write_register((uint32_t*)(dcss_base  + SCALE_H_CHROMA_INC), INC_VAL);

	// Scaler coeffecients - 15.8.3.1.20 Scale Coefficient Memory Array (SCALE_COEF_ARRAY)
	write_register((uint32_t*)(dcss_base  + 0x1c0c0), 0x40000);
	write_register((uint32_t*)(dcss_base  + 0x1c140), 0x0); // Must be initialised
	write_register((uint32_t*)(dcss_base  + 0x1c180), 0x40000);
	write_register((uint32_t*)(dcss_base  + 0x1c1c0), 0x0); // Must be initialised
	write_register((uint32_t*)(dcss_base  + 0x1c020), ((hdmi_config->v_active - 1) << 16 | (hdmi_config->h_active - 1)));

	uint32_t* scale_ctrl = (uint32_t*)(dcss_base + SCALE_CTRL);
	*scale_ctrl = set_bit(*scale_ctrl, RUN_EN);
	*scale_ctrl = set_bit(*scale_ctrl, ENABLE_REPEAT);
}