/* This work is Crown Copyright NCSC, 2024. */

#ifndef __SUB_SAMPLER_H__
#define __SUB_SAMPLER_H__

#include "hdmi_data.h"
#include <stdint.h>

// Color Sub-sampler
#define SS_COEFF 0x1b070 // 15.11.3.1.30 (SS_COEFF)
#define SS_CLIP_CB 0x1b080 //15.11.3.1.34 (SS_CLIP_CB)
#define SS_CLIP_CR 0x1b090 // 15.11.3.1.38 (SS_CLIP_CR)
#define SS_DISPLAY 0x1b010 // 15.11.3.1.6 (SS_DISPLAY)
#define SS_HSYNC 0x1b020 // 15.11.3.1.10 (SS_HSYNC)
#define SS_VSYNC 0x1b030 // 15.11.3.1.14 (SS_VSYNC)
#define SS_DE_ULC 0x1b040 // 15.11.3.1.18 (SS_DE_ULC)
#define SS_DE_LRC 0x1b050 // 15.11.3.1.22 (SS_DE_LRC)
#define SS_MODE 0x1b060 // 15.11.3.1.26 (SS_MODE)
#define SS_SYS_CTRL 0x1b000 // 15.11.3.1.2 (SS_SYS_CTRL)

void write_sub_sampler_memory_registers(uintptr_t dcss_base, struct hdmi_data *hdmi_config);


#endif