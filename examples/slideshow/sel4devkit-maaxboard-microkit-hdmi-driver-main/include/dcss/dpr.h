/* This work is Crown Copyright NCSC, 2024. */

#ifndef __DPR_H__
#define __DPR_H__

#include "hdmi_data.h"
#include <stdint.h>

// DPR Channel 1
#define DPR_1_FRAME_1P_BASE_ADDR_CTRL0 0x180c0  // 15.7.3.1.38 Frame 1-Plane Base Address Control 0 (FRAME_1P_BASE_ADDR_CTRL0)
#define DPR_1_FRAME_1P_CTRL0 0x18090 // 15.7.3.1.26 Frame 1-Plane Control 0 (FRAME_1P_CTRL0)
#define DPR_1_FRAME_1P_PIX_X_CTRL 0x180a0 // 15.7.3.1.30 Frame 1-Plane Pix X Control (FRAME_1P_PIX_X_CTRL)
#define DPR_1_FRAME_1P_PIX_Y_CTRL 0x180b0 // 15.7.3.1.34 Frame 1-Plane Pix Y Control (FRAME_1P_PIX_Y_CTRL)
#define DPR_1_FRAME_2P_BASE_ADDR_CTRL0 0x18110 // 15.7.3.1.54 Frame 2-Plane Base Address Control 0 (FRAME_2P_BASE_ADDR_CTRL0)
#define DPR_1_FRAME_2P_PIX_X_CTRL 0x180f0 // 15.7.3.1.46 Frame 2-Plane Pix X Control (FRAME_2P_PIX_X_CTRL)
#define DPR_1_FRAME_2P_PIX_Y_CTRL 0x18100 // 15.7.3.1.50 Frame 2-Plane Pix Y Control (FRAME_2P_PIX_Y_CTRL)
#define DPR_1_FRAME_CTRL0 0x18070 // 15.7.3.1.22 Frame Control 0 (FRAME_CTRL0)
#define DPR_1_MODE_CTRL0 0x18050 // 15.7.3.1.18 Mode Control 0 (MODE_CTRL0)
#define DPR_1_RTRAM_CTRL0 0x18200 // 15.7.3.1.58 RTRAM Control 0 (RTRAM_CTRL0)
#define DPR_1_SYSTEM_CTRL0 0x18000 // 15.7.3.1.2 System Control 0 (SYSTEM_CTRL0)

void write_dpr_memory_registers(uintptr_t dcss_base, uintptr_t dma_base, struct hdmi_data *hdmi_config);

#endif