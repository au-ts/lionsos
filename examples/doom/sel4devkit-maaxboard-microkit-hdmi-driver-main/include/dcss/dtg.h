/* This work is Crown Copyright NCSC, 2024. */

#ifndef __DTG_H__
#define __DTG_H__

#include "hdmi_data.h"
#include <stdint.h>

// Display Timing Generator
#define TC_CONTROL_STATUS 0x20000 // 15.3.3.1.2 Timing Controller Control Register (TC_CONTROL_STATUS)
#define TC_DTG_REG1 0x20004 // 15.3.3.1.3 DTG lower right corner locations (TC_DTG_REG1)
#define TC_DISPLAY_REG2 0x20008 // 15.3.3.1.4 Display Register: TOP Window Coordinates for Active display area (TC_DISPLAY_REG2)
#define TC_DISPLAY_REG3 0x2000c // 15.3.3.1.5 Display Register: BOTTOM Window Coordinates for Activedisplay area (TC_DISPLAY_REG3)
#define TC_CH1_REG4 0x20010 // 15.3.3.1.6 Channel 1 window Register: TOP Window Coordinates forchannel1 (TC_CH1_REG4)
#define TC_CH1_REG5 0x20014 // 15.3.3.1.7 Channel_1 window Register: BOTTOM Window Coordinates forchannel_1 window (TC_CH1_REG5)
#define TC_CTX_LD_REG10 0x20028 // 15.3.3.1.12 Context Loader Register: Coordinates in the raster table wherethe context loader is started. (TC_CTX_LD_REG10)

#define TC_LINE1_INT_REG13 0x20050 // 15.3.3.1.21 LINE1 interrupt control: Coordinate where line1 interrupt is assered (TC_LINE1_INT_REG13)
#define TC_LINE2_INT_REG14 0x20054 // 15.3.3.1.22 LINE2 interrupt control: Coordinate where line2 interrupt is assered (TC_LINE2_INT_REG14)

void write_dtg_memory_registers(uintptr_t dcss_base, struct hdmi_data *hdmi_config);

#endif