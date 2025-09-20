#ifndef __HDMI_TX_H__
#define __HDMI_TX_H__


#include <vic_table.h>
#include <API_General.h>

#include "hdmi_data.h"

void init_hdmi(struct hdmi_data *hdmi_config);
CDN_API_STATUS init_api();
CDN_API_STATUS call_api(uint32_t phy_frequency, VIC_PXL_ENCODING_FORMAT pixel_encoding_format, uint8_t bits_per_pixel, struct hdmi_data *hdmi_config);
void handle_api_status(CDN_API_STATUS status, char* function_name);

#endif 