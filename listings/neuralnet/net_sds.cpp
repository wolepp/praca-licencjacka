/***************************************************************************
 Copyright (c) 2016, Xilinx, Inc.
 All rights reserved.

 Redistribution and use in source and binary forms, with or without modification,
 are permitted provided that the following conditions are met:

 1. Redistributions of source code must retain the above copyright notice,
 this list of conditions and the following disclaimer.

 2. Redistributions in binary form must reproduce the above copyright notice,
 this list of conditions and the following disclaimer in the documentation
 and/or other materials provided with the distribution.

 3. Neither the name of the copyright holder nor the names of its contributors
 may be used to endorse or promote products derived from this software
 without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 ***************************************************************************/
#include <cmath>
#include <common/xf_common.h>
//#include <imgproc/xf_crop.hpp>
//#include <imgproc/xf_resize.hpp>
#include <linux/videodev2.h>
#include <stdlib.h>
#include <iostream>

// używane w funkcjach wygenerowanych przez hls4ml
#include <ap_fixed.h>
#include <ap_int.h>

#include "net_sds.h"

#include "nn.hpp"

#include "sds_lib.h"


#define NET_WINDOW_SIZE		25
#ifndef NET_PIX_PER_CLOCK
#define NET_PIX_PER_CLOCK	2
#endif

#define NET_DM_COPY			0
#define NET_DM_ZERO_COPY	1
#ifndef NET_DM
#define NET_DM 			NET_DM_COPY
#endif

#ifndef NET_URAM
#define NET_URAM		0
#endif


struct net_data {
	xf::Mat<XF_8UC1, NET_HEIGHT, NET_WIDTH, XF_NPPC1> *inoutLuma;
	xf::Mat<XF_8UC1, NET_HEIGHT, NET_WIDTH, XF_NPPC1> *inoutUV;
	c_output_t *predictions;
	uint32_t in_fourcc;
	uint32_t out_fourcc;
};

#pragma SDS data copy("inoutUV.data"[0:"inoutUV.size"])
#pragma SDS data access_pattern("inoutUV.data":SEQUENTIAL)
#pragma SDS data mem_attribute("inoutUV.data":NON_CACHEABLE|PHYSICAL_CONTIGUOUS)
#pragma SDS data mem_attribute(frm_data_in:NON_CACHEABLE|PHYSICAL_CONTIGUOUS)
#if (NET_DM==NET_DM_COPY)
  #pragma SDS data copy(frm_data_in[0:pcnt])
#elif (NET_DM==NET_DM_ZERO_COPY)
  #pragma SDS data zero_copy(frm_data_in[0:pcnt])
#endif
#pragma SDS data access_pattern(frm_data_in:SEQUENTIAL)
#pragma SDS data mem_attribute("inoutLuma.data":NON_CACHEABLE|PHYSICAL_CONTIGUOUS)
#pragma SDS data copy("inoutLuma.data"[0:"inoutLuma.size"])
#pragma SDS data access_pattern("inoutLuma.data":SEQUENTIAL)
void read_net_input(unsigned short *frm_data_in,
		    xf::Mat<XF_8UC1, NET_HEIGHT, NET_WIDTH, XF_NPPC1> &inoutLuma,
		    xf::Mat<XF_8UC1, NET_HEIGHT, NET_WIDTH, XF_NPPC1> &inoutUV,
		    uint32_t in_fourcc, int pcnt)
{
	unsigned short lumamask    = (V4L2_PIX_FMT_YUYV==in_fourcc)? 0x00FF : 0xFF00;
	unsigned short lumashift   = (V4L2_PIX_FMT_YUYV==in_fourcc)? 0      : 8;
	unsigned short chromamask  = (V4L2_PIX_FMT_YUYV==in_fourcc)? 0xFF00 : 0x00FF;
	unsigned short chromashift = (V4L2_PIX_FMT_YUYV==in_fourcc)? 8      : 0;

	for(int i=0; i<pcnt; i++){
		#pragma HLS pipeline II=1
		unsigned short yuvpix = frm_data_in[i];
		ap_uint<8> ypix =  (ap_uint<8>)((yuvpix & lumamask)>>lumashift);
		ap_uint<8> uvpix = (ap_uint<8>)((yuvpix & chromamask)>>chromashift);
		inoutLuma.data[i] = ypix;
		inoutUV.data[i] = 128; //uvpix;   // 128 gives grayscale, uvpix gives colour image
	}
}


#pragma SDS data buffer_depth("inoutUV.data":16384)
#pragma SDS data mem_attribute("inoutUV.data":NON_CACHEABLE|PHYSICAL_CONTIGUOUS)
#pragma SDS data copy("inoutUV.data"[0:"inoutUV.size"])
#pragma SDS data access_pattern("inoutUV.data":SEQUENTIAL)
#pragma SDS data mem_attribute("inoutLuma.data":NON_CACHEABLE|PHYSICAL_CONTIGUOUS)
#pragma SDS data copy("inoutLuma.data"[0:"inoutLuma.size"])
#pragma SDS data access_pattern("inoutLuma.data":SEQUENTIAL)
#pragma SDS data mem_attribute(frm_data_out:NON_CACHEABLE|PHYSICAL_CONTIGUOUS)
#if (NET_DM==NET_DM_COPY)
  #pragma SDS data copy(frm_data_out[0:pcnt])
#elif (NET_DM==NET_DM_ZERO_COPY)
  #pragma SDS data zero_copy(frm_data_out[0:pcnt])
#endif
#pragma SDS data access_pattern(frm_data_out:SEQUENTIAL)
void write_net_output(xf::Mat<XF_8UC1, NET_HEIGHT, NET_WIDTH, XF_NPPC1> &inoutLuma,
		      xf::Mat<XF_8UC1, NET_HEIGHT, NET_WIDTH, XF_NPPC1> &inoutUV,
		      unsigned short *frm_data_out, uint32_t out_fourcc, int pcnt)
{
	unsigned short lumashift = (V4L2_PIX_FMT_YUYV==out_fourcc)? 0 : 8;
	unsigned short chromashift = (V4L2_PIX_FMT_YUYV==out_fourcc)? 8 : 0;

	for(int i=0; i<pcnt; i++){
		#pragma HLS pipeline II=1
		ap_uint<8> ypix = inoutLuma.data[i];
		ap_uint<8> uvpix = inoutUV.data[i];
		unsigned short yuvpix = ((unsigned short) uvpix << chromashift) | ((unsigned short) ypix << lumashift);
		frm_data_out[i] = yuvpix;
	}
}


int net_init_sds(size_t in_height, size_t in_width, size_t out_height,
			  size_t out_width, uint32_t in_fourcc,
			  uint32_t out_fourcc, void **priv)
{
	struct net_data *netd = (struct net_data *) malloc(sizeof(struct net_data));
	if (netd == NULL) {
		return -1;
	}

	netd->inoutLuma = new xf::Mat<XF_8UC1, NET_HEIGHT, NET_WIDTH, XF_NPPC1>(in_height, in_width);
	netd->inoutUV = new xf::Mat<XF_8UC1, NET_HEIGHT, NET_WIDTH, XF_NPPC1>(in_height, in_width);
	netd->predictions = (c_output_t *) sds_alloc(N_OUT * sizeof(c_output_t));
	netd->in_fourcc = in_fourcc;
	netd->out_fourcc = out_fourcc;

	*priv = netd;
	return 0;
}

void net_deinit_sds(void *priv)
{
	struct net_data *netd = (struct net_data *) priv;

	delete netd->inoutLuma;
	delete netd->inoutUV;
	sds_free(netd->predictions);

	free(netd);
}


void net_sds_read(unsigned short *frm_data_in, int height, int width, void *priv)
{
	struct net_data *netd = (struct net_data *) priv;
	int pcnt = height*width;

	// split the 16b YUYV... input data into separate 8b YYYY... and 8b UVUV...
	read_net_input(frm_data_in, *netd->inoutLuma, *netd->inoutUV, netd->in_fourcc, pcnt);

	return;
}

void net_sds_write(unsigned short *frm_data_out, int height, int width, void *priv)
{
	struct net_data *netd = (struct net_data *) priv;
	int pcnt = height*width;

	// combine separate 8b YYYY... and 8b UVUV... data into 16b YUYV... output data
	write_net_output(*netd->inoutLuma, *netd->inoutUV, frm_data_out, netd->out_fourcc, pcnt);

	return;
}

void net_sds_predict(c_output_t *predictions, void *priv)
{
	struct net_data *netd = (struct net_data *) priv;

	nn((c_input_t *) netd->inoutLuma->data, netd->predictions);

	for(int i=0; i<N_OUT; i++) {
		predictions[i] = netd->predictions[i];
	}

	return;
}












