/* This work is Crown Copyright NCSC, 2024. */

#include "dpr.h"
#include "write_register.h"
#include "dma.h"
#include "dma_offsets.h"

#include <stdio.h>
#include <stdlib.h>

// 15.7.3.1.2 System Control 0 (SYSTEM_CTRL0)
#define RUN_EN 0
#define REPEAT_EN 2
#define SHADOW_LOAD_EN 3
#define SW_SHADOW_LOAD_SEL 4

void write_dpr_memory_registers(uintptr_t dcss_base, uintptr_t dma_base, struct hdmi_data *hdmi_config) {
	
	uintptr_t* fb_1 =  getPhys((void*) (dma_base));

    write_register((uint32_t*)(dcss_base + DPR_1_FRAME_1P_BASE_ADDR_CTRL0), (uintptr_t)fb_1); // The address of the frame buffer
    write_register((uint32_t*)(dcss_base + DPR_1_FRAME_1P_CTRL0), 0x2); // Set 256 bytes per prefetch request
    write_register((uint32_t*)(dcss_base + DPR_1_FRAME_1P_PIX_X_CTRL), hdmi_config->h_active);
	write_register((uint32_t*)(dcss_base + DPR_1_FRAME_1P_PIX_Y_CTRL), hdmi_config->v_active);
	write_register((uint32_t*)(dcss_base + DPR_1_FRAME_CTRL0), ((hdmi_config->h_active * 4) << 16));
	write_register((uint32_t*)(dcss_base + DPR_1_MODE_CTRL0), hdmi_config->rgb_format); // 32 bits per pixel (with rgba set to a certain value) This needs to be configured for differrent RGB ordering.

	uint32_t* dpr_sys_ctrl = (uint32_t*)(dcss_base + DPR_1_SYSTEM_CTRL0);
	*dpr_sys_ctrl = set_bit(*dpr_sys_ctrl, RUN_EN);
	*dpr_sys_ctrl = set_bit(*dpr_sys_ctrl, SHADOW_LOAD_EN);
	*dpr_sys_ctrl = set_bit(*dpr_sys_ctrl, SW_SHADOW_LOAD_SEL);
	*dpr_sys_ctrl = set_bit(*dpr_sys_ctrl, REPEAT_EN);
}
