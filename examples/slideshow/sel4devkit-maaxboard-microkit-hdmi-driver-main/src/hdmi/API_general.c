/******************************************************************************
 *
 * Copyright (C) 2016-2017 Cadence Design Systems, Inc.
 * All rights reserved worldwide.
 *
 * Copyright 2017-2018 NXP
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 ******************************************************************************
 *
 * API_General.c
 *
 ******************************************************************************
 */

#include "API_General.h"
#include "util.h"
#ifndef __UBOOT__
#include <string.h>
#endif
#include "address.h"
#include "apb_cfg.h"
#include "opcodes.h"
#include "general_handler.h"
#include "externs.h"
// #include <linux/delay.h>
#ifndef __UBOOT__
#include <stdio.h>
#endif

#include "uboot_conversions.h"

extern state_struct state;

void cdn_api_init(void)
{
	memset(&state, 0, sizeof(state_struct));
}

// CDN_API_STATUS hdp_rx_loadfirmware(unsigned char *imem, int imemsize,
// 				   unsigned char *dmem, int dmemsize)
// {
// 	int i;

// 	for (i = 0; i < imemsize; i += 4)
// 		if (hdp_rx_apb_write(ADDR_IMEM + i,
// 				     (unsigned int)imem[i] << 0 |
// 				     (unsigned int)imem[i + 1] << 8 |
// 				     (unsigned int)imem[i + 2] << 16 |
// 				     (unsigned int)imem[i + 3] << 24))
// 			return CDN_ERR;
// 	for (i = 0; i < dmemsize; i += 4)
// 		if (hdp_rx_apb_write(ADDR_DMEM + i,
// 				     (unsigned int)dmem[i] << 0 |
// 				     (unsigned int)dmem[i + 1] << 8 |
// 				     (unsigned int)dmem[i + 2] << 16 |
// 				     (unsigned int)dmem[i + 3] << 24))
// 			return CDN_ERR;
// 	return CDN_OK;
// }

CDN_API_STATUS cdn_api_loadfirmware(unsigned char *imem, int imemsize,
				    unsigned char *dmem, int dmemsize)
{
	int i;

	for (i = 0; i < imemsize; i += 4)
		if (cdn_apb_write(ADDR_IMEM + i,
				  (unsigned int)imem[i] << 0 |
				  (unsigned int)imem[i + 1] << 8 |
				  (unsigned int)imem[i + 2] << 16 |
				  (unsigned int)imem[i + 3] << 24))
			return CDN_ERR;
	for (i = 0; i < dmemsize; i += 4)
		if (cdn_apb_write(ADDR_DMEM + i,
				  (unsigned int)dmem[i] << 0 |
				  (unsigned int)dmem[i + 1] << 8 |
				  (unsigned int)dmem[i + 2] << 16 |
				  (unsigned int)dmem[i + 3] << 24))
			return CDN_ERR;
	return CDN_OK;
}

CDN_API_STATUS cdn_api_general_test_echo(unsigned int val,
					 CDN_BUS_TYPE bus_type)
{
	CDN_API_STATUS ret;

	if (!state.running) {
		if (!internal_apb_available())
			return CDN_BSY;
		state.bus_type = bus_type;
		state.rxenable = 1;
		internal_tx_mkfullmsg(MB_MODULE_ID_GENERAL, GENERAL_TEST_ECHO,
				      1, 4, val);
		return CDN_STARTED;
	}
	if (state.txenable && !internal_mbox_tx_process().txend)
		return CDN_BSY;
	if (state.rxenable && !internal_mbox_rx_process().rxend)
		return CDN_BSY;
	ret = internal_test_rx_head(MB_MODULE_ID_GENERAL, GENERAL_TEST_ECHO);
	if (ret != CDN_OK) {
		state.running = 0;
		return ret;
	}
	state.running = 0;
	if (val != internal_betoi(state.rxbuffer + INTERNAL_CMD_HEAD_SIZE, 4))
		return CDN_ERR;
	return CDN_OK;
}

CDN_API_STATUS cdn_api_general_test_echo_blocking(unsigned int val,
						  CDN_BUS_TYPE bus_type)
{
	internal_block_function(cdn_api_general_test_echo(val, bus_type));
}

CDN_API_STATUS cdn_api_general_test_echo_ext(u8 const *msg, u8 *resp,
					     u16 num_bytes,
					     CDN_BUS_TYPE bus_type)
{
	CDN_API_STATUS ret;

	if (!msg || !resp)
		return CDN_ERR;

	if ((num_bytes > GENERAL_TEST_ECHO_MAX_PAYLOAD) ||
	    (num_bytes < GENERAL_TEST_ECHO_MIN_PAYLOAD))
		return CDN_ERR;

	if (!state.running) {
		if (!internal_apb_available())
			return CDN_BSY;

		state.bus_type = bus_type;
		state.rxenable = 1;

		internal_tx_mkfullmsg(MB_MODULE_ID_GENERAL, GENERAL_TEST_ECHO,
				      1, -num_bytes, msg);

		return CDN_STARTED;
	}

	if (state.txenable && !internal_mbox_tx_process().txend)
		return CDN_BSY;

	if (state.rxenable && !internal_mbox_rx_process().rxend)
		return CDN_BSY;

	ret = internal_test_rx_head(MB_MODULE_ID_GENERAL, GENERAL_TEST_ECHO);

	if (ret != CDN_OK) {
		state.running = 0;
		return ret;
	}

	state.running = 0;

	memcpy(resp, state.rxbuffer + INTERNAL_CMD_HEAD_SIZE, num_bytes);

	if (memcmp(msg, resp, num_bytes) != 0)
		return CDN_ERR;

	return CDN_OK;
}

CDN_API_STATUS cdn_api_general_test_echo_ext_blocking(u8 const *msg,
						      u8 *resp,
						      u16 num_bytes,
						      CDN_BUS_TYPE bus_type)
{
	internal_block_function(cdn_api_general_test_echo_ext
				(msg, resp, num_bytes, bus_type));
}

CDN_API_STATUS cdn_api_general_getcurversion(unsigned short *ver,
					     unsigned short *verlib)
{
	unsigned int vh, vl, vlh, vll;

	if (cdn_apb_read(VER_L << 2, &vl))
		return CDN_ERR;
	if (cdn_apb_read(VER_H << 2, &vh))
		return CDN_ERR;
	if (cdn_apb_read(VER_LIB_L_ADDR << 2, &vll))
		return CDN_ERR;
	if (cdn_apb_read(VER_LIB_H_ADDR << 2, &vlh))
		return CDN_ERR;
	*ver = F_VER_MSB_RD(vh) << 8 | F_VER_LSB_RD(vl);
	*verlib = F_SW_LIB_VER_H_RD(vlh) << 8 | F_SW_LIB_VER_L_RD(vll);
	return CDN_OK;
}

CDN_API_STATUS cdn_api_get_event(uint32_t *events)
{
	u32 evt[4] = { 0 };

	if (!events) {
		printf("events pointer is NULL!\n");
		return CDN_ERR;
	}

	if (cdn_apb_read(SW_EVENTS0 << 2, &evt[0]) ||
	    cdn_apb_read(SW_EVENTS1 << 2, &evt[1]) ||
	    cdn_apb_read(SW_EVENTS2 << 2, &evt[2]) ||
	    cdn_apb_read(SW_EVENTS3 << 2, &evt[3])) {
		printf("Failed to read events registers.\n");
		return CDN_ERR;
	}

	*events = (evt[0] & 0xFF)
	    | ((evt[1] & 0xFF) << 8)
	    | ((evt[2] & 0xFF) << 16)
	    | ((evt[3] & 0xFF) << 24);

	return CDN_OK;
}

CDN_API_STATUS cdn_api_get_debug_reg_val(uint16_t *val)
{
	u32 dbg[2] = { 0 };

	if (!val) {
		printf("val pointer is NULL!\n");
		return CDN_ERR;
	}

	if (cdn_apb_read(SW_DEBUG_L << 2, &dbg[0]) ||
	    cdn_apb_read(SW_DEBUG_H << 2, &dbg[1])) {
		printf("Failed to read debug registers.\n");
		return CDN_ERR;
	}

	*val = (u16)((dbg[0] & 0xFF) | ((dbg[1] & 0xFF) << 8));

	return CDN_OK;
}

CDN_API_STATUS cdn_api_checkalive(void)
{
	unsigned int alive, newalive;
	u8 retries_left = 10;

	if (cdn_apb_read(KEEP_ALIVE << 2, &alive))
		return CDN_ERR;

	while (retries_left--) {
		udelay(1);

		if (cdn_apb_read(KEEP_ALIVE << 2, &newalive))
			return CDN_ERR;

		if (alive == newalive)
			continue;

		return CDN_OK;
	}

	printf("%s: keep-alive counter did not increment for 10us...\n", __func__);

	return CDN_BSY;
}

CDN_API_STATUS cdn_api_checkalive_blocking(void)
{
	internal_block_function(cdn_api_checkalive());
}

CDN_API_STATUS cdn_api_maincontrol(unsigned char mode, unsigned char *resp)
{
	if (!state.running) {
		if (!internal_apb_available())
			return CDN_BSY;
		state.bus_type = CDN_BUS_TYPE_APB;
		state.rxenable = 1;
		internal_tx_mkfullmsg(MB_MODULE_ID_GENERAL,
				      GENERAL_MAIN_CONTROL, 1, 1, mode);
		return CDN_STARTED;
	}
	INTERNAL_PROCESS_MESSAGES;
	internal_opcode_ok_or_return(MB_MODULE_ID_GENERAL,
				     GENERAL_MAIN_CONTROL_RESP);
	internal_readmsg(1, 1, resp);
	return CDN_OK;
}

CDN_API_STATUS cdn_api_maincontrol_blocking(unsigned char mode,
					    unsigned char *resp)
{
	internal_block_function(cdn_api_maincontrol(mode, resp));
}

CDN_API_STATUS cdn_api_apbconf(u8 dpcd_bus_sel, u8 dpcd_bus_lock,
			       u8 hdcp_bus_sel, u8 hdcp_bus_lock,
			       u8 capb_bus_sel, u8 capb_bus_lock,
			       u8 *dpcd_resp, u8 *hdcp_resp,
			       u8 *capb_resp)
{
	u8 resp;
	u8 set = 0;

	if (!state.running) {
		if (!internal_apb_available())
			return CDN_BSY;

		state.bus_type = CDN_BUS_TYPE_APB;
		state.rxenable = 1;

		set |= (dpcd_bus_sel)
		    ? (1 << GENERAL_BUS_SETTINGS_DPCD_BUS_BIT)
		    : 0;
		set |= (dpcd_bus_lock)
		    ? (1 << GENERAL_BUS_SETTINGS_DPCD_BUS_LOCK_BIT)
		    : 0;
		set |= (hdcp_bus_sel)
		    ? (1 << GENERAL_BUS_SETTINGS_HDCP_BUS_BIT)
		    : 0;
		set |= (hdcp_bus_lock)
		    ? (1 << GENERAL_BUS_SETTINGS_HDCP_BUS_LOCK_BIT)
		    : 0;
		set |= (capb_bus_sel)
		    ? (1 << GENERAL_BUS_SETTINGS_CAPB_OWNER_BIT)
		    : 0;
		set |= (capb_bus_lock)
		    ? (1 << GENERAL_BUS_SETTINGS_CAPB_OWNER_LOCK_BIT)
		    : 0;

		internal_tx_mkfullmsg(MB_MODULE_ID_GENERAL,
				      GENERAL_BUS_SETTINGS, 1, 1, set);

		return CDN_STARTED;
	}

	INTERNAL_PROCESS_MESSAGES;
	internal_opcode_ok_or_return(MB_MODULE_ID_GENERAL,
				     GENERAL_BUS_SETTINGS_RESP);

	/* Read one one-byte response */
	internal_readmsg(1, 1, &resp);

	*dpcd_resp =
	    (resp & (1 << GENERAL_BUS_SETTINGS_RESP_DPCD_BUS_BIT)) ? 1 : 0;
	*hdcp_resp =
	    (resp & (1 << GENERAL_BUS_SETTINGS_RESP_HDCP_BUS_BIT)) ? 1 : 0;
	*capb_resp =
	    (resp & (1 << GENERAL_BUS_SETTINGS_RESP_CAPB_OWNER_BIT)) ? 1 : 0;

	return CDN_OK;
}

CDN_API_STATUS cdn_api_apbconf_blocking(u8 dpcd_bus_sel,
					u8 dpcd_bus_lock,
					u8 hdcp_bus_sel,
					u8 hdcp_bus_lock,
					u8 capb_bus_sel,
					u8 capb_bus_lock,
					u8 *dpcd_resp,
					u8 *hdcp_resp,
					u8 *capb_resp)
{
	internal_block_function(cdn_api_apbconf(dpcd_bus_sel, dpcd_bus_lock,
						hdcp_bus_sel, hdcp_bus_lock,
						capb_bus_sel, capb_bus_lock,
						dpcd_resp, hdcp_resp,
						capb_resp));
}

CDN_API_STATUS cdn_api_setclock(unsigned char mhz)
{
	return cdn_apb_write(SW_CLK_H << 2, mhz);
}

CDN_API_STATUS cdn_api_general_read_register(unsigned int addr,
					     GENERAL_READ_REGISTER_RESPONSE
					     *resp)
{
	CDN_API_STATUS ret;

	if (!state.running) {
		if (!internal_apb_available())
			return CDN_BSY;
		internal_tx_mkfullmsg(MB_MODULE_ID_GENERAL,
				      GENERAL_READ_REGISTER, 1, 4, addr);
		state.bus_type = CDN_BUS_TYPE_APB;
		state.rxenable = 1;
		return CDN_STARTED;
	}
	INTERNAL_PROCESS_MESSAGES;
	ret = internal_test_rx_head(MB_MODULE_ID_GENERAL,
				    GENERAL_READ_REGISTER_RESP);
	if (ret != CDN_OK)
		return ret;
	internal_readmsg(2, 4, &resp->addr, 4, &resp->val);
	return CDN_OK;
}

CDN_API_STATUS
cdn_api_general_read_register_blocking(unsigned int addr,
				       GENERAL_READ_REGISTER_RESPONSE *resp)
{
	internal_block_function(cdn_api_general_read_register(addr, resp));
}

CDN_API_STATUS cdn_api_general_write_register(unsigned int addr,
					      unsigned int val)
{
	if (!state.running) {
		if (!internal_apb_available())
			return CDN_BSY;
		internal_tx_mkfullmsg(MB_MODULE_ID_GENERAL,
				      GENERAL_WRITE_REGISTER, 2, 4, addr, 4,
				      val);
		state.bus_type = CDN_BUS_TYPE_APB;
		return CDN_STARTED;
	}
	INTERNAL_PROCESS_MESSAGES;
	return CDN_OK;
}

CDN_API_STATUS cdn_api_general_write_register_blocking(unsigned int addr,
						       unsigned int val)
{
	internal_block_function(cdn_api_general_write_register(addr, val));
}

CDN_API_STATUS cdn_api_general_write_field(unsigned int addr,
					   unsigned char startbit,
					   unsigned char bitsno,
					   unsigned int val)
{
	if (!state.running) {
		if (!internal_apb_available())
			return CDN_BSY;
		internal_tx_mkfullmsg(MB_MODULE_ID_GENERAL, GENERAL_WRITE_FIELD,
				      4, 4, addr, 1, startbit, 1, bitsno, 4,
				      val);
		state.bus_type = CDN_BUS_TYPE_APB;
		return CDN_STARTED;
	}
	INTERNAL_PROCESS_MESSAGES;
	return CDN_OK;
}

CDN_API_STATUS cdn_api_general_write_field_blocking(unsigned int addr,
						    unsigned char startbit,
						    unsigned char bitsno,
						    unsigned int val)
{
	internal_block_function(cdn_api_general_write_field
				(addr, startbit, bitsno, val));
}

CDN_API_STATUS cdn_api_general_phy_test_access(uint8_t *resp)
{
	CDN_API_STATUS ret;

	*resp = 0;

	if (!state.running) {
		if (!internal_apb_available())
			return CDN_BSY;

		internal_tx_mkfullmsg(MB_MODULE_ID_GENERAL, GENERAL_TEST_ACCESS,
				      0);
		state.bus_type = CDN_BUS_TYPE_APB;
		state.rxenable = 1;

		return CDN_STARTED;
	}

	INTERNAL_PROCESS_MESSAGES;

	ret = internal_test_rx_head(MB_MODULE_ID_GENERAL, GENERAL_TEST_ACCESS);

	if (ret != CDN_OK)
		return ret;

	internal_readmsg(1, 1, resp);

	return CDN_OK;
}

CDN_API_STATUS cdn_api_general_phy_test_access_blocking(uint8_t *resp)
{
	internal_block_function(cdn_api_general_phy_test_access(resp));
}