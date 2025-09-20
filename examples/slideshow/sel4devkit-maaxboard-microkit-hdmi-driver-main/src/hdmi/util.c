/******************************************************************************
 *
 * Copyright (C) 2016-2017 Cadence Design Systems, Inc.
 * All rights reserved worldwide.
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
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. THE SOFTWARE IS PROVIDED "AS IS",
 * WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
 * TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
 * FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Copyright 2017-2018 NXP
 *
 ******************************************************************************
 *
 * util.c
 *
 ******************************************************************************
 */

#include "util.h"
#include "API_General.h"
#include "externs.h"
#ifndef __UBOOT__
#include <string.h>
#endif
#include "apb_cfg.h"
#include "opcodes.h"
#ifndef __UBOOT__
#include <stdio.h>

#endif
state_struct state;

int cdn_bus_read(unsigned int addr, unsigned int *value)
{
	return state.bus_type ?
	    cdn_sapb_read(addr, value) : cdn_apb_read(addr, value);
}

int cdn_bus_write(unsigned int addr, unsigned int value)
{
	return state.bus_type ?
	    cdn_sapb_write(addr, value) : cdn_apb_write(addr, value);
}

void internal_itobe(int val, volatile unsigned char *dest, int bytes)
{
	int i;
	for (i = bytes - 1; i >= 0; --i) {
		dest[i] = (unsigned char)val;
		val >>= 8;
	}
}

uint32_t internal_betoi(volatile uint8_t const *src, uint8_t bytes)
{
	uint32_t ret = 0;
	int i;

	if (bytes > sizeof(ret)) {
		printf("Warning. Read request for payload larger then supported.\n");
		bytes = sizeof(ret);
	}

	for (i = 0; i < bytes; ++i) {
		ret <<= 8;
		ret |= (unsigned int)src[i];
	}

	return ret;
}

unsigned int internal_mkmsg(volatile unsigned char *dest, int valno, ...)
{
	va_list vl;
	unsigned int len = 0;
	va_start(vl, valno);
	len = internal_vmkmsg(dest, valno, vl);
	va_end(vl);
	return len;
}

unsigned int internal_vmkmsg(volatile unsigned char *dest, int valno,
			     va_list vl)
{
	unsigned int len = 0;
	int i;
	for (i = 0; i < valno; ++i) {
		int size = va_arg(vl, int);
		if (size > 0) {
			internal_itobe(va_arg(vl, int), dest, size);
			dest += size;
			len += size;
		} else {
			memcpy((void *)dest, va_arg(vl, void *), -size);
			dest -= size;
			len -= size;
		}
	}
	return len;
}

void internal_tx_mkfullmsg(unsigned char module, unsigned char opcode,
			   int valno, ...)
{
	va_list vl;
	va_start(vl, valno);
	internal_vtx_mkfullmsg(module, opcode, valno, vl);
	va_end(vl);
}

void internal_vtx_mkfullmsg(unsigned char module, unsigned char opcode,
			    int valno, va_list vl)
{
	unsigned int len =
	    internal_vmkmsg(state.txbuffer + INTERNAL_CMD_HEAD_SIZE, valno, vl);
	internal_mbox_tx_enable(module, opcode, len);
	state.txenable = 1;
	state.running = 1;
}

void internal_readmsg(int valno, ...)
{
	va_list vl;
	va_start(vl, valno);
	internal_vreadmsg(valno, vl);
	va_end(vl);
}

void internal_vreadmsg(int valno, va_list vl)
{
	uint8_t *src = state.rxbuffer + INTERNAL_CMD_HEAD_SIZE;
	size_t i;

	for (i = 0; i < (size_t) valno; ++i) {
		int size = va_arg(vl, int);
		void *ptr = va_arg(vl, void *);

		if (!ptr) {
			src += size;
		} else if (!size) {
			*((unsigned char **)ptr) = src;
		} else if (size > 0) {
			switch ((size_t) size) {
			case sizeof(uint8_t):
				*((uint8_t *)ptr) = internal_betoi(src, size);
				break;
			case sizeof(uint16_t):
				*((uint16_t *)ptr) = internal_betoi(src, size);
				break;
			case 3:	/* 3-byte value (e.g. DPCD address)
				can be safely converted from BE.*/
			case sizeof(uint32_t):
				*((uint32_t *)ptr) = internal_betoi(src, size);
				break;
			default:
				printf("Warning. Unsupported variable size.\n");
				memcpy(ptr, src, size);
			};

			src += size;
		} else {
			memcpy(ptr, src, -size);
			src -= size;
		}
	}
}

INTERNAL_MBOX_STATUS mailbox_write(unsigned char val)
{
	INTERNAL_MBOX_STATUS ret;
	unsigned int full;
	if (cdn_bus_read(MAILBOX_FULL_ADDR << 2, &full)) {
		ret.tx_status = CDN_TX_APB_ERROR;
		return ret;
	}
	if (full) {
		ret.tx_status = CDN_TX_FULL;
		return ret;
	}
	if (cdn_bus_write(MAILBOX0_WR_DATA << 2, val)) {
		ret.tx_status = CDN_TX_APB_ERROR;
		return ret;
	}
	ret.tx_status = CDN_TX_WRITE;
	return ret;
}

INTERNAL_MBOX_STATUS mailbox_read(volatile unsigned char *val)
{
	INTERNAL_MBOX_STATUS ret;
	unsigned int empty;
	unsigned int rd;
	if (cdn_bus_read(MAILBOX_EMPTY_ADDR << 2, &empty)) {
		ret.rx_status = CDN_RX_APB_ERROR;
		return ret;
	}
	if (empty) {
		ret.rx_status = CDN_RX_EMPTY;
		return ret;
	}
	if (cdn_bus_read(MAILBOX0_RD_DATA << 2, &rd)) {
		ret.rx_status = CDN_RX_APB_ERROR;
		return ret;
	}
	*val = (unsigned char)rd;
	ret.rx_status = CDN_RX_READ;
	return ret;
}

INTERNAL_MBOX_STATUS internal_mbox_tx_process(void)
{
	unsigned int txcount = 0;
	unsigned int length =
	    (unsigned int)state.txbuffer[2] << 8 | (unsigned int)state.
	    txbuffer[3];
	INTERNAL_MBOX_STATUS ret = {.txend = 0 };
	ret.tx_status = CDN_TX_NOTHING;
	INTERNAL_MBOX_STATUS tx_ret;
	if (!state.txenable)
		return ret;
	while ((tx_ret.tx_status =
		mailbox_write(state.txbuffer[state.txi]).tx_status) ==
	       CDN_TX_WRITE) {
		txcount++;
		if (++state.txi >= length + 4) {
			state.txenable = 0;
			state.txi = 0;
			ret.txend = 1;
			break;
		}
	}
	if (txcount && tx_ret.tx_status == CDN_TX_FULL)
		ret.tx_status = CDN_TX_WRITE;
	else
		ret.tx_status = tx_ret.tx_status;
	return ret;
}

INTERNAL_MBOX_STATUS internal_mbox_rx_process(void)
{
	unsigned int rxcount = 0;
	INTERNAL_MBOX_STATUS ret = { 0, 0, 0, 0 };
	INTERNAL_MBOX_STATUS rx_ret;
	while ((rx_ret.rx_status =
		mailbox_read(state.rxbuffer + state.rxi).rx_status) ==
	       CDN_RX_READ) {
		rxcount++;
		if (++state.rxi >= 4 +
		    ((unsigned int)state.rxbuffer[2] << 8 |
		     (unsigned int)state.rxbuffer[3])) { /* end of message */
			state.rxi = 0;
			ret.rxend = 1;
			state.rxenable = 0;
			break;
		}
	}
	ret.rx_status = rxcount ? CDN_RX_READ : CDN_RX_EMPTY;
	return ret;
}

unsigned int internal_apb_available(void)
{
	return !(state.rxenable || state.txenable);
}

void internal_mbox_tx_enable(unsigned char module, unsigned char opcode,
			     unsigned short length)
{
	state.txbuffer[0] = opcode;
	state.txbuffer[1] = module;
	state.txbuffer[2] = (unsigned char)(length >> 8);
	state.txbuffer[3] = (unsigned char)length;
	state.txenable = 1;
}

CDN_API_STATUS internal_test_rx_head(unsigned char module, unsigned char opcode)
{
	if (opcode != state.rxbuffer[0])
		return CDN_BAD_OPCODE;
	if (module != state.rxbuffer[1])
		return CDN_BAD_MODULE;
	return CDN_OK;
}

CDN_API_STATUS internal_test_rx_head_match(void)
{
	return internal_test_rx_head(state.txbuffer[1], state.txbuffer[0]);
}

void print_fw_ver(void)
{
	unsigned short ver, verlib;
	cdn_api_general_getcurversion(&ver, &verlib);
	printf("FIRMWARE VERSION: %d, LIB VERSION: %d\n", ver, verlib);
}

unsigned short internal_get_msg_len(void)
{
	return ((unsigned short)state.rxbuffer[2] << 8) | (unsigned short)state.
	    rxbuffer[3];
}
