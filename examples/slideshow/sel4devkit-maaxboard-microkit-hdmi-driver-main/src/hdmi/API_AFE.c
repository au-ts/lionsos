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
 * API_AFE.c
 *
 ******************************************************************************
 */

#include "address.h"
#include "API_AFE.h"
#include "util.h"
#ifndef __UBOOT__
#include <stdio.h>
#endif

#include "uboot_conversions.h"

void afe_write(unsigned int offset, unsigned short val)
{
#ifdef EXTERNAL_AFE
	cdn_phapb_write(offset << 2, val);
#else
	CDN_API_STATUS sts;

	sts = cdn_api_general_write_register_blocking(
		ADDR_AFE + (offset << 2), val);

	if (sts != CDN_OK) {
		printf("CDN_API_General_Write_Register_blocking(0x%.8X, 0x%.8X) returned %d\n",
		       offset,
		       val,
		       (int)sts);
	}
#endif
}

unsigned short afe_read(unsigned int offset)
{
	GENERAL_READ_REGISTER_RESPONSE resp;

#ifdef EXTERNAL_AFE
	cdn_phapb_read(offset << 2, &resp.val);
#else
	CDN_API_STATUS sts;

	sts = cdn_api_general_read_register_blocking(
		ADDR_AFE + (offset << 2), &resp);

	if (sts != CDN_OK) {
		printf("CDN_API_General_Read_Register_blocking(0x%.8X) returned %d\n",
		       offset,
		       (int)sts);
	}
#endif
	return resp.val;
}

void set_field_value(reg_field_t *reg_field, u32 value)
{
	u8 length;
	u32 max_value;
	u32 trunc_val;
	length = (reg_field->msb - reg_field->lsb + 1);

	max_value = (1 << length) - 1;
	if (value > max_value) {
		trunc_val = value;
		trunc_val &= (1 << length) - 1;
		printf("set_field_value() Error! Specified value (0x%0X) exceeds field capacity - it will by truncated to 0x%0X (%0d-bit field - max value: %0d dec)\n",
		       value, trunc_val, length, max_value);
	} else {
		reg_field->value = value;
	}
}

int set_reg_value(reg_field_t reg_field)
{
	return reg_field.value << reg_field.lsb;
}
