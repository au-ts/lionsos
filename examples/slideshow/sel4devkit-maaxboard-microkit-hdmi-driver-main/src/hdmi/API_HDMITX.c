/******************************************************************************
 *
 * Copyright (C) 2016-2017 Cadence Design Systems, Inc.
 * All rights reserved worldwide.
 *
 * Copyright 2017-2018 NXP
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT  (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. THE SOFTWARE IS PROVIDED "AS IS",
 * WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
 * TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
 * FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 ******************************************************************************
 *
 * API_HDMITX.c
 *
 ******************************************************************************
 */

#include "API_HDMITX.h"
#include "util.h"
#include "opcodes.h"
#ifndef __UBOOT__
#include "string.h"
#include "stdio.h"
#endif
#include "mhl_hdtx_top.h"
#include "source_phy.h"
#include "address.h"
#include "source_car.h"
#include "source_vif.h"
#include "general_handler.h"

CDN_API_STATUS CDN_API_HDMITX_DDC_READ(HDMITX_TRANS_DATA *data_in,
				       HDMITX_TRANS_DATA *data_out)
{
	internal_macro_command_txrx(MB_MODULE_ID_HDMI_TX, HDMI_TX_READ,
				    CDN_BUS_TYPE_APB, 3,
				    1, data_in->slave,
				    1, data_in->offset,
				    2, data_in->len
				   );
	internal_readmsg(5,
			 1, &data_out->status,
			 1, &data_out->slave,
			 1, &data_out->offset,
			 2, &data_out->len,
			 0, &data_out->buff
			);
	return CDN_OK;
}

CDN_API_STATUS CDN_API_HDMITX_DDC_READ_blocking(HDMITX_TRANS_DATA *data_in,
						HDMITX_TRANS_DATA *data_out)
{
	internal_block_function(CDN_API_HDMITX_DDC_READ(data_in, data_out));
}

CDN_API_STATUS CDN_API_HDMITX_DDC_WRITE(HDMITX_TRANS_DATA *data_in,
					HDMITX_TRANS_DATA *data_out)
{
	printf("foo: %x\n", data_in->buff[0]);
	internal_macro_command_txrx(MB_MODULE_ID_HDMI_TX, HDMI_TX_WRITE,
				    CDN_BUS_TYPE_APB, 4,
				    1, data_in->slave,
				    1, data_in->offset,
				    2, data_in->len,
				    -data_in->len, data_in->buff
				   );
	internal_readmsg(4,
			 1, &data_out->status,
			 1, &data_out->slave,
			 1, &data_out->offset,
			 2, &data_out->len
			);
	return CDN_OK;
}

CDN_API_STATUS CDN_API_HDMITX_DDC_WRITE_blocking(HDMITX_TRANS_DATA *data_in,
						 HDMITX_TRANS_DATA *data_out)
{
	internal_block_function(CDN_API_HDMITX_DDC_WRITE(data_in, data_out));
}

CDN_API_STATUS CDN_API_HDMITX_DDC_UPDATE_READ(HDMITX_TRANS_DATA *data_out)
{
	internal_macro_command_txrx(MB_MODULE_ID_HDMI_TX, HDMI_TX_UPDATE_READ,
				    CDN_BUS_TYPE_APB, 0);
	internal_readmsg(2,
			 1, &data_out->status,
			 0, &data_out->buff
			);
	return CDN_OK;
}

CDN_API_STATUS CDN_API_HDMITX_DDC_UPDATE_READ_blocking(HDMITX_TRANS_DATA
						       *data_out)
{
	internal_block_function(CDN_API_HDMITX_DDC_UPDATE_READ(data_out));
}

CDN_API_STATUS CDN_API_HDMITX_READ_EDID(unsigned char block,
					unsigned char segment,
					HDMITX_TRANS_DATA *data_out)
{
	internal_macro_command_txrx(MB_MODULE_ID_HDMI_TX, HDMI_TX_EDID,
				    CDN_BUS_TYPE_APB, 2,
				    1, block,
				    1, segment
				   );
	internal_readmsg(5,
			 1, &data_out->status,
			 1, &data_out->slave,
			 1, &data_out->offset,
			 2, &data_out->len,
			 0, &data_out->buff
			);
	return CDN_OK;
}

CDN_API_STATUS CDN_API_HDMITX_READ_EDID_blocking(unsigned char block,
						 unsigned char segment,
						 HDMITX_TRANS_DATA *data_out)
{
	internal_block_function(CDN_API_HDMITX_READ_EDID(block, segment,
							 data_out));
}

CDN_API_STATUS
CDN_API_HDMITX_Set_Mode_blocking(HDMI_TX_MAIL_HANDLER_PROTOCOL_TYPE protocol,
				 unsigned int character_rate)
{
	CDN_API_STATUS ret;
	GENERAL_READ_REGISTER_RESPONSE resp;
	HDMITX_TRANS_DATA data_in;
	HDMITX_TRANS_DATA data_out;
	unsigned char buff = 1;

	/*enable/disable  scrambler; */
	if (protocol == HDMI_TX_MODE_HDMI_2_0) {
		if (character_rate > 340000)
			buff = 3; /*enable scrambling + TMDS_Bit_Clock_Ratio */
		else
			buff = 1; /*enable scrambling */
	} else {
		buff = 0; /*disable scrambling */
	}

	data_in.buff = &buff;
	data_in.len = 1;
	data_in.slave = 0x54;
	data_in.offset = 0x20; /*TMDS config */
#if 1
	if (protocol == HDMI_TX_MODE_HDMI_2_0)
		ret =  CDN_API_HDMITX_DDC_WRITE_blocking(&data_in, &data_out);

#endif
	ret = cdn_api_general_read_register_blocking(ADDR_SOURCE_MHL_HD +
						     (HDTX_CONTROLLER << 2),
						     &resp);

	/*remove data enable */
	resp.val = resp.val & (~(F_DATA_EN(1)));
	ret = cdn_api_general_write_register_blocking(ADDR_SOURCE_MHL_HD +
						      (HDTX_CONTROLLER << 2),
						      resp.val);

	if (protocol == HDMI_TX_MODE_HDMI_2_0) {
		if (character_rate > 3400000) {
			/* Set TMDS clock ratio */
			ret = cdn_api_general_write_register_blocking
				(ADDR_SOURCE_MHL_HD +
				 (HDTX_CLOCK_REG_0 << 2),
				 F_DATA_REGISTER_VAL_0(0x00000));
			ret = cdn_api_general_write_register_blocking
				(ADDR_SOURCE_MHL_HD +
				 (HDTX_CLOCK_REG_1 << 2),
				 F_DATA_REGISTER_VAL_1(0xFFFFF));
		}
	}

	/*set hdmi mode and preemble mode */
	resp.val = resp.val & (~(F_HDMI_MODE(3)));
	resp.val = resp.val & (~(F_HDMI2_PREAMBLE_EN(1)));

	resp.val = (resp.val) | (F_HDMI_MODE(protocol)) |
		(F_HDMI2_PREAMBLE_EN(1));
	ret = cdn_api_general_write_register_blocking(ADDR_SOURCE_MHL_HD +
						      (HDTX_CONTROLLER << 2),
						      resp.val);

	/*data enable */
	resp.val |= F_DATA_EN(1);
	ret = cdn_api_general_write_register_blocking(ADDR_SOURCE_MHL_HD +
						      (HDTX_CONTROLLER << 2),
						      resp.val);

	return ret;
}

CDN_API_STATUS CDN_API_HDMITX_Init_blocking(void)
{
	CDN_API_STATUS ret;

	/*init phy and CAR and HDMI TX */
/*	ret = cdn_api_general_write_register_blocking
		(ADDR_SOURCD_PHY + (LANES_CONFIG<<2),
		F_SOURCE_PHY_LANE0_SWAP(0) |
		F_SOURCE_PHY_LANE1_SWAP(1) |
		F_SOURCE_PHY_LANE2_SWAP(2) |
		F_SOURCE_PHY_LANE3_SWAP(3) |
		F_SOURCE_PHY_COMB_BYPASS(0) |
		F_SOURCE_PHY_20_10(1)); */

	ret = cdn_api_general_write_register_blocking(ADDR_SOURCD_PHY +
						      (PHY_DATA_SEL << 2),
						      F_SOURCE_PHY_MHDP_SEL(1));
	ret = cdn_api_general_write_register_blocking(ADDR_SOURCE_MHL_HD +
						      (HDTX_HPD << 2),
						      F_HPD_VALID_WIDTH(4) |
						      F_HPD_GLITCH_WIDTH(0));
	ret = cdn_api_general_write_register_blocking(ADDR_SOURCE_MHL_HD +
						      (HDTX_CONTROLLER << 2),
						      F_HDMI_MODE(1) |
						      F_AUTO_MODE(0) |
						      F_GCP_EN(1) |
						      F_DATA_EN(1) |
						      F_CLEAR_AVMUTE(1) |
						      F_HDMI2_PREAMBLE_EN(1) |
						      F_HDMI2_CTRL_IL_MODE(1) |
						      F_PIC_3D(0XF) |
						      F_BCH_EN(1));
	/* open CARS */
	ret = cdn_api_general_write_register_blocking(ADDR_SOURCE_CAR +
						      (SOURCE_PHY_CAR << 2),
						      0xF);
	ret = cdn_api_general_write_register_blocking(ADDR_SOURCE_CAR +
						      (SOURCE_HDTX_CAR << 2),
						      0xFF);
	ret = cdn_api_general_write_register_blocking(ADDR_SOURCE_CAR +
						      (SOURCE_PKT_CAR << 2),
						      0xF);
	ret = cdn_api_general_write_register_blocking(ADDR_SOURCE_CAR +
						      (SOURCE_AIF_CAR << 2),
						      0xF);
	ret = cdn_api_general_write_register_blocking(ADDR_SOURCE_CAR +
						      (SOURCE_CIPHER_CAR << 2),
						      0xF);
	ret = cdn_api_general_write_register_blocking(ADDR_SOURCE_CAR +
						      (SOURCE_CRYPTO_CAR << 2),
						      0xF);
	ret = cdn_api_general_write_register_blocking(ADDR_SOURCE_CAR +
						      (SOURCE_CEC_CAR << 2), 3);

	/*init vif */
	/*ret = cdn_api_general_write_register_blocking(ADDR_SOURCE_VIF
	  +(HSYNC2VSYNC_POL_CTRL<<2), F_HPOL(0) | F_VPOL(0)); */

	return ret;
}


CDN_API_STATUS CDN_API_HDMITX_SetVic_blocking(struct hdmi_data* hdmi_data, int bpp,
					      VIC_PXL_ENCODING_FORMAT format)
{
	CDN_API_STATUS ret;
	GENERAL_READ_REGISTER_RESPONSE resp;
	unsigned int vsync_lines = hdmi_data->vsync;
	unsigned int eof_lines = hdmi_data->v_front_porch;
	unsigned int sof_lines = hdmi_data->v_back_porch;
	unsigned int hblank = hdmi_data->h_blank;
	unsigned int hactive = hdmi_data->h_total - hblank;
	unsigned int vblank = vsync_lines + eof_lines + sof_lines;
	unsigned int vactive = hdmi_data->v_total - vblank;
	unsigned int hfront = hdmi_data->h_front_porch;
	unsigned int hback = hdmi_data->h_back_porch;
	unsigned int vfront = eof_lines;
	unsigned int hsync = hblank - hfront - hback;
	unsigned int vsync = vsync_lines;
	unsigned int vback = sof_lines;
	unsigned int v_h_polarity = ((hdmi_data->hsync_pol ==
				      ACTIVE_LOW) ? 0 : 1) +
		((hdmi_data->vsync_pol == ACTIVE_LOW) ? 0 : 2);

	ret = cdn_api_general_write_register_blocking(ADDR_SOURCE_MHL_HD +
						      (SCHEDULER_H_SIZE << 2),
						      (hactive << 16) + hblank);
	ret = cdn_api_general_write_register_blocking(ADDR_SOURCE_MHL_HD +
						      (SCHEDULER_V_SIZE << 2),
						      (vactive << 16) + vblank);
	ret = cdn_api_general_write_register_blocking(ADDR_SOURCE_MHL_HD +
						      (HDTX_SIGNAL_FRONT_WIDTH
						       << 2),
						      (vfront << 16) + hfront);
	ret = cdn_api_general_write_register_blocking(ADDR_SOURCE_MHL_HD +
						      (HDTX_SIGNAL_SYNC_WIDTH
						       << 2),
						      (vsync << 16) + hsync);
	ret = cdn_api_general_write_register_blocking(ADDR_SOURCE_MHL_HD +
						      (HDTX_SIGNAL_BACK_WIDTH
						       << 2),
						      (vback << 16) + hback);
	ret = cdn_api_general_write_register_blocking(ADDR_SOURCE_VIF +
						      (HSYNC2VSYNC_POL_CTRL
						       << 2),
						      v_h_polarity);

	/*  Data Enable is 1 */

	/*Reset Data Enable */
	cdn_api_general_read_register_blocking(ADDR_SOURCE_MHL_HD +
					       (HDTX_CONTROLLER << 2), &resp);

	/*reset data enable */
	resp.val = resp.val & (~(F_DATA_EN(1)));
	ret = cdn_api_general_write_register_blocking(ADDR_SOURCE_MHL_HD +
						      (HDTX_CONTROLLER << 2),
						      resp.val);

	/*set bpp */
	resp.val = resp.val &  (~(F_VIF_DATA_WIDTH(3)));
	switch (bpp) {
	case 8:
		resp.val = resp.val | (F_VIF_DATA_WIDTH(0));
		break;

	case 10:
		resp.val = resp.val | (F_VIF_DATA_WIDTH(1));
		break;

	case 12:
		resp.val = resp.val | (F_VIF_DATA_WIDTH(2));
		break;

	case 16:
		resp.val = resp.val | (F_VIF_DATA_WIDTH(3));
		break;
	}

	/*select color encoding */
	resp.val = resp.val &  (~(F_HDMI_ENCODING(3)));
	switch (format) {
	case PXL_RGB:

		resp.val = resp.val | (F_HDMI_ENCODING(0));
		break;

	case YCBCR_4_4_4:
		resp.val = resp.val | (F_HDMI_ENCODING(2));
		break;

	case YCBCR_4_2_2:
		resp.val = resp.val | (F_HDMI_ENCODING(1));
		break;

	case YCBCR_4_2_0:
		resp.val = resp.val | (F_HDMI_ENCODING(3));
		break;
	case Y_ONLY:
		/*not exist in hdmi */
		break;
	}

	ret = cdn_api_general_write_register_blocking(ADDR_SOURCE_MHL_HD +
						      (HDTX_CONTROLLER << 2),
						      resp.val);

	/*set data enable */
	resp.val = resp.val  | (F_DATA_EN(1));
	ret = cdn_api_general_write_register_blocking(ADDR_SOURCE_MHL_HD +
						      (HDTX_CONTROLLER << 2),
						      resp.val);

	return ret;
}

CDN_API_STATUS CDN_API_HDMITX_ForceColorDepth_blocking(unsigned char force,
						       unsigned char val)
{
	unsigned int valToWrite = F_COLOR_DEPTH_VAL(val) |
		F_COLOR_DEPTH_FORCE(force);

	return cdn_api_general_write_register_blocking
		(ADDR_SOURCE_MHL_HD +
		 (GCP_FORCE_COLOR_DEPTH_CODING << 2),
		 valToWrite);
}

CDN_API_STATUS CDN_API_HDMITX_ReadEvents(uint32_t *events)
{
	CDN_API_STATUS ret;

	if (!state.running) {
		if (!internal_apb_available())
			return CDN_BSY;

		internal_tx_mkfullmsg(MB_MODULE_ID_HDMI_TX, HDMI_TX_EVENTS, 0);
		state.rxenable = 1;
		state.bus_type = CDN_BUS_TYPE_APB;

		return CDN_STARTED;
	}

	INTERNAL_PROCESS_MESSAGES;

	ret = internal_test_rx_head(MB_MODULE_ID_HDMI_TX, HDMI_TX_EVENTS);

	if (ret != CDN_OK)
		return ret;

	internal_readmsg(1, 4, events);

	return CDN_OK;
}

CDN_API_STATUS CDN_API_HDMITX_ReadEvents_blocking(uint32_t *events)
{
	internal_block_function(CDN_API_HDMITX_ReadEvents(events));
}

CDN_API_STATUS CDN_API_HDMITX_GetHpdStatus(uint8_t *hpd_sts)
{
	CDN_API_STATUS ret;

	if (!state.running) {
		if (!internal_apb_available())
			return CDN_BSY;

		/*
		 * General Module is used here for obtaining HPD State because
		 * HDMI TX Module is inactive in stand-by mode, thus cannot
		 * return it.
		 */
		internal_tx_mkfullmsg(MB_MODULE_ID_GENERAL,
				      GENERAL_GET_HPD_STATE, 0);
		state.rxenable = 1;
		state.bus_type = CDN_BUS_TYPE_APB;

		return CDN_STARTED;
	}

	INTERNAL_PROCESS_MESSAGES;

	ret = internal_test_rx_head(MB_MODULE_ID_GENERAL,
				    GENERAL_GET_HPD_STATE);

	if (ret != CDN_OK)
		return ret;

	internal_readmsg(1, 1, hpd_sts);

	return CDN_OK;
}

CDN_API_STATUS CDN_API_HDMITX_GetHpdStatus_blocking(uint8_t *hpd_sts)
{
	internal_block_function(CDN_API_HDMITX_GetHpdStatus(hpd_sts));
}

