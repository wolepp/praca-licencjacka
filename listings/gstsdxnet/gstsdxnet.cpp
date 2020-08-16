/*
 * Copyright (C) 2017 – 2018 Xilinx, Inc.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * XILINX BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of the Xilinx shall not be used
 * in advertising or otherwise to promote the sale, use or other dealings in
 * this Software without prior written authorization from Xilinx.
 */

/**
 * SECTION:sdxnet
 *
 * This element draw the computed optical flow using an SDx accelerator.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch videotestsrc ! sdxnet ! kmssink
 * ]|
 * </refsect2>
 */
typedef float			c_output_t;
#define N_OUT 10

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <net_sds.h>
#include <stdlib.h>

#include "gstsdxnet.h"

GST_DEBUG_CATEGORY_STATIC (gst_sdx_net_debug);
#define GST_CAT_DEFAULT gst_sdx_net_debug

#define SDX_NET_FRAME_INPUTS_PER_OUTPUT      1

#define SDX_NET_CAPS_INPUT \
    "video/x-raw, " \
    "format = (string) {YUY2, UYVY}, " \
    "width = (int) 28, " \
    "height = (int) 28, " \
    "framerate = " GST_VIDEO_FPS_RANGE

#define SDX_NET_CAPS_OUTPUT \
    "video/x-raw, " \
    "format = (string) {YUY2, UYVY}, " \
    "width = (int) 28, " \
    "height = (int) 28, " \
    "framerate = " GST_VIDEO_FPS_RANGE

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SDX_NET_CAPS_INPUT)
    );

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SDX_NET_CAPS_OUTPUT)
    );

#define gst_sdx_net_parent_class parent_class
G_DEFINE_TYPE (GstSdxNet, gst_sdx_net, GST_TYPE_SDX_BASE);

static uint32_t
video_format_to_fourcc (GstVideoFormat fmt)
{
  switch (fmt) {
    case GST_VIDEO_FORMAT_YUY2:
      return GST_MAKE_FOURCC ('Y', 'U', 'Y', 'V');
    case GST_VIDEO_FORMAT_UYVY:
      return GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y');
    default:
      return 0;
  }
}

static void
gst_sdx_net_free_data (GstSdxNet * filter)
{
  g_clear_pointer (&filter->sds_data, net_deinit_sds);
}

static gboolean
gst_sdx_net_set_caps (GstBaseTransform * trans, GstCaps * in_caps,
    GstCaps * out_caps)
{
  GstSdxBase *base = GST_SDX_BASE (trans);
  GstSdxNet *filter = GST_SDX_NET (trans);
  GstVideoInfo info;

  if (gst_sdx_base_get_filter_mode (base) != GST_SDX_BASE_FILTER_MODE_HW) {
    GST_ELEMENT_ERROR (base, STREAM, NOT_IMPLEMENTED,
        ("Neural net does not support SW mode"), (NULL));
    return FALSE;
  }

  /* Caps is used here only to initialize optical flow without modifying */
  GST_DEBUG_OBJECT (filter, "in caps : %" GST_PTR_FORMAT, in_caps);

  if (!gst_video_info_from_caps (&info, in_caps)) {
    GST_WARNING_OBJECT (filter, "failed to get video info from caps");
    return FALSE;
  }

  gst_sdx_net_free_data (filter);

  net_init_sds (GST_VIDEO_INFO_HEIGHT (&info),
      GST_VIDEO_INFO_WIDTH (&info), GST_VIDEO_INFO_HEIGHT (&info),
      GST_VIDEO_INFO_WIDTH (&info),
      video_format_to_fourcc (GST_VIDEO_INFO_FORMAT (&info)),
      video_format_to_fourcc (GST_VIDEO_INFO_FORMAT (&info)), &filter->sds_data);

  return TRUE;
}

static GstFlowReturn
gst_sdx_net_process_frames (GstSdxBase * base,
    GstSdxFrame ** in_frames, GstSdxFrame * out_frame)
{
  GstSdxNet *filter = GST_SDX_NET (base);
  GstVideoFrame *in_vframe, *out_vframe;
  GstSdxFrame *in_frame;
  GstVideoInfo *out_info;
  gushort *data_in, *data_out;
  c_output_t *classes;
  static int counter = 0;

  in_frame = in_frames[0];
  if (in_frame == NULL) {
    GST_WARNING_OBJECT (base, "input frame is invalid");
    return GST_FLOW_ERROR;
  }

  in_vframe = &in_frame->vframe;
  out_vframe = &out_frame->vframe;
  out_info = &out_frame->info;
  data_in = (gushort *) GST_VIDEO_FRAME_PLANE_DATA (in_vframe, 0);
  data_out = (gushort *) GST_VIDEO_FRAME_PLANE_DATA (out_vframe, 0);
  classes = (c_output_t *) malloc(N_OUT * sizeof(c_output_t));

  net_sds_read (data_in, GST_VIDEO_INFO_HEIGHT (out_info),
    GST_VIDEO_INFO_WIDTH (out_info), filter->sds_data);

  net_sds_predict(classes, filter->sds_data);
  if (counter % 25 == 0) {
	  float max = 0.0;
	  int prediction = 0;
	  for (int i=0; i < 10; i++) {
		  if (classes[i] > max) {
			  max = classes[i];
			  prediction = i;
		  }
	  }
	  GST_LOG("\n %d %.4f \n" GST_PTR_FORMAT, prediction, max);
  }


  net_sds_write (data_out, GST_VIDEO_INFO_HEIGHT (out_info),
    GST_VIDEO_INFO_WIDTH (out_info), filter->sds_data);

  counter++;

  return GST_FLOW_OK;
}

static gboolean
gst_sdx_net_stop (GstBaseTransform * trans)
{
  GstSdxNet *filter = GST_SDX_NET (trans);

  gst_sdx_net_free_data (filter);

  return GST_BASE_TRANSFORM_CLASS (parent_class)->stop (trans);
}

static void
gst_sdx_net_init (GstSdxNet * filter)
{
  GstSdxBase *base = GST_SDX_BASE (filter);
  gst_sdx_base_set_filter_mode (base, GST_SDX_BASE_FILTER_MODE_HW);
  gst_sdx_base_set_inputs_per_output (base,
      SDX_NET_FRAME_INPUTS_PER_OUTPUT);
}

static void
gst_sdx_net_class_init (GstSdxNetClass * klass)
{
  GstElementClass *element_class;
  GstBaseTransformClass *transform_class;
  GstSdxBaseClass *sdxbase_class;

  element_class = (GstElementClass *) klass;
  transform_class = GST_BASE_TRANSFORM_CLASS (klass);
  sdxbase_class = GST_SDX_BASE_CLASS (klass);

  transform_class->stop = GST_DEBUG_FUNCPTR (gst_sdx_net_stop);
  transform_class->set_caps = GST_DEBUG_FUNCPTR (gst_sdx_net_set_caps);
  sdxbase_class->process_frames =
      GST_DEBUG_FUNCPTR (gst_sdx_net_process_frames);

  /* avoid concurrent access to process_frames function */
  gst_sdx_base_class_enable_time_division (sdxbase_class);

  gst_element_class_set_static_metadata (element_class,
      "SDx Neural Network",
      "Filter/Effect/Video",
      "SDx Neural Network", "Wojciech Lepich <wojciech.lepich@student.uj.edu.pl>");

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_sdx_net_debug, "sdxnet", 0,
      "SDx Neural Network");

  return gst_element_register (plugin, "sdxnet",
      GST_RANK_NONE, GST_TYPE_SDX_NET);
}

/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "sdxnet"
#endif

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    sdxnet,
    "SDx Neural Network",
    plugin_init, "0.3", "LGPL", "GStreamer", "http://fais.uj.edu.pl/")
