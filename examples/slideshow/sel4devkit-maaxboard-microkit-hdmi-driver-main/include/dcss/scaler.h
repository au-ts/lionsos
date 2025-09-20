/* This work is Crown Copyright NCSC, 2024. */

#ifndef __SCALER_H__
#define __SCALER_H__


#include "hdmi_data.h"
#include <stdint.h>

// Scaler
#define SCALE_CTRL 0x1c000 // 15.8.3.1.2 Scale Control Register (SCALE_CTRL)
#define SCALE_OFIFO_CTRL 0x1c004 // 15.8.3.1.3 Scale Output FIFO Control Register (SCALE_OFIFO_CTRL)
#define SCALE_SRC_DATA_CTRL 0x1c008 // 15.8.3.1.4 Scale Source Data Control Register (SCALE_SRC_DATA_CTRL)
#define SCALE_BIT_DEPTH 0x1c00c //SCALE_BIT_DEPTH
#define SCALE_SRC_FORMAT 0x1c010 // 15.8.3.1.6 Scale Source Format Control Register (SCALE_SRC_FORMAT)
#define SCALE_DST_FORMAT 0x1c014 // 15.8.3.1.7 Scale Destination Format Control Register (SCALE_DST_FORMAT)
#define SCALE_SRC_LUMA_RES 0x1c018 // 15.8.3.1.8 Scale Source Luma Resolution Register (SCALE_SRC_LUMA_RES)
#define SCALE_SRC_CHROMA_RES 0x1c01c // 15.8.3.1.9 Scale Source Chroma Resolution Register (SCALE_SRC_CHROMA_RES)
#define SCALE_DST_LUMA_RES 0x1c020 // 15.8.3.1.10 Scale Destination Luma Resolution Register (SCALE_DST_LUMA_RES)
#define SCALE_DST_CHROMA_RES 0x1c024 // 15.8.3.1.11 Scale Destination Chroma Resolution Register (SCALE_DST_CHROMA_RES)
#define SCALE_V_LUMA_INC 0x1c04c // 15.8.3.1.13 Scale Vertical Luma Increment Register (SCALE_V_LUMA_INC)
#define SCALE_H_LUMA_INC 0x1c054 // 15.8.3.1.15 Scale Horizontal Luma Increment Register (SCALE_H_LUMA_INC)
#define SCALE_V_CHROMA_INC 0x1c05c // 15.8.3.1.17 Scale Vertical Chroma Increment Register (SCALE_V_CHROMA_INC)
#define SCALE_H_CHROMA_INC 0x1c064 // 15.8.3.1.19 Scale Horizontal Chroma Increment Register (SCALE_H_CHROMA_INC)

void write_scaler_memory_registers(uintptr_t dcss_base, struct hdmi_data *hdmi_config);

#endif