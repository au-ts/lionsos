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
 * API_AVI.c
 *
 ******************************************************************************
 */

#include "API_AVI.h"
#include "API_Infoframe.h"


CDN_API_STATUS cdn_api_set_avi(struct hdmi_data* hdmi_data,	// MODIFIED - All uses of hdmi_data
	VIC_PXL_ENCODING_FORMAT color_mode,
	BT_TYPE itu_ver)
{
	unsigned int active_slot = hdmi_data->h_blank;
	unsigned int line_width = hdmi_data->h_total;
	unsigned int hactive = line_width - active_slot + 1;
	unsigned int vactive = hdmi_data->v_active + 1;

	unsigned int hactive_l = hactive - 256 * ((unsigned int)hactive / 256);
	unsigned int hactive_h = hactive / 256;
	unsigned int vactive_l = vactive - 256 * ((unsigned int)vactive / 256);
	unsigned int vactive_h = vactive / 256;

	/* unsigned int packet; */

	unsigned int packet_type = 0x82;
	unsigned int packet_version = 0x2;
	unsigned int packet_len = 0xd;
	unsigned int packet_y = 0;
	unsigned int packet_c = 0;
	unsigned int packet_r = 0;
	unsigned int packet_vic = 0;
	unsigned int packet_pr = 0;
	unsigned int packet_buf_size = 5; /* Total buf length is 18, aligned with 4 bytes, need 5 words */
	unsigned int packet_buf[packet_buf_size];
	unsigned char *packet = (unsigned char *)&packet_buf[0];
	unsigned int packet_hb0 = 0;
	unsigned int packet_hb1 = 0;
	unsigned int packet_hb2 = 0;
	unsigned int packet_pb0 = 0;
	unsigned int packet_pb1 = 0;
	unsigned int packet_pb2 = 0;
	unsigned int packet_pb3 = 0;
	unsigned int packet_pb4 = 0;
	unsigned int packet_pb5 = 0;
	unsigned int packet_pb6 = 0;
	unsigned int packet_pb7 = 0;
	unsigned int packet_pb8 = 0;
	unsigned int packet_pb9 = 0;
	unsigned int packet_pb10 = 0;
	unsigned int packet_pb11 = 0;
	unsigned int packet_pb12 = 0;
	unsigned int packet_pb13 = 0;
	unsigned int pb1_13_chksum = 0;
	unsigned int packet_chksum = 0;

	if (color_mode == PXL_RGB)
		packet_y = 0;
	else if (color_mode == YCBCR_4_4_4)
		packet_y = 2;
	else if (color_mode == YCBCR_4_2_2)
		packet_y = 1;
	else if (color_mode == YCBCR_4_2_0)
		packet_y = 3;

	/* Colorimetry:  Nodata=0 IT601=1 ITU709=2 */
	if (itu_ver == BT_601)
		packet_c = 1;
	else if (itu_ver == BT_709)
		packet_c = 2;
	else
		packet_c = 0;

	unsigned int packet_a0 = 1;
	unsigned int packet_b = 0;
	unsigned int packet_s = 0;
	unsigned int packet_sc = 0; /* Picture Scaling */

	/* Active Format Aspec Ratio: Same As Picture = 0x8 4:3(Center)=0x9
	   16:9=0xA 14:9=0xB */
	packet_r = hdmi_data->vic_r3;
	/* Aspect Ratio: Nodata=0 4:3=1 16:9=2 */
	unsigned int packet_m = 0;
	/* Quantization Range Default=0 Limited Range=0x1 FullRange=0x2
	   Reserved 0x3 */
	unsigned int packet_q = 0;
	/* Quantization Range 0=Limited Range  FullRange=0x1 Reserved 0x3/2 */
	unsigned int packet_yq = 0;
	/* Extended Colorimetry xvYCC601=0x0 xvYCC709=1 All other Reserved */
	unsigned int packet_ec = 0;
	/*IT content nodata=0 ITcontent=1 */
	unsigned int packet_it = 0;
	/* Video Code (CEA) */
	packet_vic = hdmi_data->vic;
	/* Pixel Repetition 0 ... 9 (1-10) */
	packet_pr = hdmi_data->vic_pr;
	/* Content Type */
	unsigned int packet_cn = 0;

	packet_hb0 = packet_type;
	packet_hb1 = packet_version;
	packet_hb2 = packet_len;

	packet_pb1 = 32 * packet_y + 16 * packet_a0 + 4 * packet_b + packet_s;
	packet_pb2 = 64 * packet_c + 16 * packet_m + packet_r;
	packet_pb3 =
		128 * packet_it + 16 * packet_ec + 4 * packet_q + packet_sc;
	packet_pb4 = packet_vic;
	packet_pb5 = 64 * packet_yq + 16 * packet_cn + packet_pr;
	packet_pb6 = 0;
	packet_pb7 = 0;
	packet_pb8 = vactive_l;
	packet_pb9 = vactive_h;
	packet_pb10 = 0;
	packet_pb11 = 0;
	packet_pb12 = hactive_l;
	packet_pb13 = hactive_h;

	pb1_13_chksum =
		(packet_hb0 + packet_hb1 + packet_hb2 + packet_pb1 +
		packet_pb2 + packet_pb3 + packet_pb4 + packet_pb5 +
		packet_pb6 + packet_pb7 + packet_pb8 + packet_pb9 +
		packet_pb10 + packet_pb11 + packet_pb12 + packet_pb13);
	packet_chksum =
		256 - (pb1_13_chksum -
		256 * ((unsigned int)pb1_13_chksum / 256));
	packet_pb0 = packet_chksum;

	packet[0] = 0;
	packet[1] = packet_hb0;
	packet[2] = packet_hb1;
	packet[3] = packet_hb2;
	packet[4] = packet_pb0;
	packet[5] = packet_pb1;
	packet[6] = packet_pb2;
	packet[7] = packet_pb3;
	packet[8] = packet_pb4;
	packet[9] = packet_pb5;
	packet[10] = packet_pb6;
	packet[11] = packet_pb7;
	packet[12] = packet_pb8;
	packet[13] = packet_pb9;
	packet[14] = packet_pb10;
	packet[15] = packet_pb11;
	packet[16] = packet_pb12;
	packet[17] = packet_pb13;

	cdn_api_infoframeset(0, packet_buf_size,
			     (unsigned int *)&packet[0], packet_type);

	return CDN_OK;
} /* End API */
