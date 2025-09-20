/* This work is Crown Copyright NCSC, 2024. */

#include "hdmi_tx.h"

#include <stdio.h>
#include <stdlib.h>

// UBOOT
#include <inttypes.h>
#include <address.h>
#include <externs.h>


#include <API_HDMITX.h>
#include <source_phy.h> 
#include <API_AVI.h>
#include <API_AFE_t28hpc_hdmitx.h>

void init_hdmi(struct hdmi_data *hdmi_config) {
	
	uint8_t bits_per_pixel = 8;
	VIC_PXL_ENCODING_FORMAT pixel_encoding_format = PXL_RGB;

	CDN_API_STATUS api_status = init_api();
	handle_api_status(api_status, "init_api()");		

	uint32_t phy_frequency = phy_cfg_t28hpc(4, hdmi_config->pixel_frequency_khz, bits_per_pixel, pixel_encoding_format, 1);
	hdmi_tx_t28hpc_power_config_seq(4);
	api_status = call_api(phy_frequency, pixel_encoding_format, bits_per_pixel, hdmi_config);
	handle_api_status(api_status, "call_api()");	

}

CDN_API_STATUS init_api() {

	CDN_API_STATUS api_status = CDN_OK;
	
	cdn_api_init();
	
	api_status = cdn_api_checkalive();
	handle_api_status(api_status, "cdn_api_checkalive()");		

	
	uint8_t test_message[] = "test message";
	uint8_t test_response[sizeof(test_message) + 1];
	
	api_status = cdn_api_general_test_echo_ext_blocking(test_message,
														test_response,
														sizeof(test_message),
														CDN_BUS_TYPE_APB);
	handle_api_status(api_status, "cdn_api_general_test_echo_ext_blocking()");						
	
	return api_status;
}

CDN_API_STATUS call_api(uint32_t phy_frequency, VIC_PXL_ENCODING_FORMAT pixel_encoding_format, uint8_t bits_per_pixel, struct hdmi_data *hdmi_config) {
	
	CDN_API_STATUS api_status = CDN_OK;   
	BT_TYPE bt_type = 0;
	HDMI_TX_MAIL_HANDLER_PROTOCOL_TYPE protocol_type = 1;
	api_status = CDN_API_HDMITX_Init_blocking();
	handle_api_status(api_status, "CDN_API_HDMITX_Init_blocking()");		

	api_status = CDN_API_HDMITX_Set_Mode_blocking(protocol_type, phy_frequency);
	handle_api_status(api_status, "CDN_API_HDMITX_Set_Mode_blocking()");		
	
	api_status = CDN_API_HDMITX_SetVic_blocking(hdmi_config, bits_per_pixel, pixel_encoding_format);
	handle_api_status(api_status, "CDN_API_HDMITX_SetVic_blocking()");		

	return api_status;
}


void handle_api_status(CDN_API_STATUS status, char* function_name) {
	
	if (status != CDN_OK){
		printf("%s returned non 0 status %d\n", function_name, status);
		abort();
	}
	else {
		printf("%s returned successfully\n", function_name);
	}
}