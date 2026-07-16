/*
 * GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2022 PengXue Duan <<user@hostname.org>>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-ambahwvdec
 *
 * prepare on EVK
 * // initialize
 * init.sh --imx274_mipi
 * test_aaa_service -a &
 * // set vout
 * test_vout --hdmi 1080p
 * // enter idle mode
 * test_encode --idle --nopreview
 *
 *
 * ## Example pipelines, playback h264
 * |[
 * gst-launch-1.0 filesrc location=/root/h264.mp4 ! qtdemux ! queue ! h264parse ! amba_hwvdec ! amba_vsink --gst-debug-level 3
 *
 * gst-launch-1.0 filesrc location=/tmp/1.mp4 ! qtdemux ! queue ! h264parse ! amba_hwvdec ! amba_vsink
 *
 * gst-launch-1.0 filesrc location=/tmp/1.h264 ! h264parse ! queue ! amba_hwvdec ! amba_vsink
 * ]|
 *  playback h264 (mp4 file).
 *
 * ## Example pipelines, playback h264 + opus
 * |[
 * gst-launch-1.0 filesrc location=/root/h264_opus.mp4 ! qtdemux name=demuxer demuxer. ! queue ! opusdec ! audioconvert ! autoaudiosink demuxer. ! queue ! h264parse ! amba_hwvdec ! amba_vsink --gst-debug-level 3
 *
 * gst-launch-1.0 filesrc location=/root/h264_opus.mp4 ! qtdemux name=demuxer demuxer.audio_0 ! queue ! opusdec ! audioconvert ! autoaudiosink demuxer.video_0 ! queue ! h264parse ! amba_hwvdec ! amba_vsink --gst-debug-level 3
 * ]|
 *  playback h264 (mp4 file).
 *
 */

#include "stdio.h"
#include <gst/gst.h>

#include "common_err_code_c.h"

#include "internal.h"
#include "iav_al.h"
#include "iav_ctx.h"
#include "debug_log.h"

#include "utils.h"
#include "codec_parser.h"

#include "gstambahwvdec.h"

#include "decoder_common.h"

//#define D_PRINT_DEBUG_DATA

GST_DEBUG_CATEGORY_STATIC (gst_amba_hwvdec_debug);
#define GST_CAT_DEFAULT gst_amba_hwvdec_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_VOUT_NUM,
  PROP_DECODER_NUM,
  PROP_DECODER_ID,
  PROP_DECODER_CONFIG_ID,
  PROP_VOUT_ID,
  PROP_VOUT_CONFIG_ID,
  PROP_ENABLE_VOUT,
  PROP_ENABLE_PB_PYRAMID,
  PROP_HEVC_PER_TILE,
  PROP_HEVC_TILE_NUM,
  PROP_VOUT_MODE,
  PROP_VOUT_SINK_TYPE,
  PROP_VOUT_DEVICE,
  PROP_VOUT_DIGITAL,
  PROP_VOUT_HDMI,
  PROP_VOUT_CVBS,
  PROP_CAP_MAX_CODED_WIDTH,
  PROP_CAP_MAX_CODED_HEIGHT,
  PROP_MAX_GOPSIZE,
  PROP_CUR_GOPSIZE,
  PROP_CODEC_FORMAT,
  PROP_SUPPORT_FAST_FW_FAST_BW_BACKWARD_PLAYBACK,
  PROP_SUPPORT_All_FRAME_BACKWARD_PLAYBACK,
  PROP_FRAME_RATE_NUM,
  PROP_FRAME_RATE_DEN,
  PROP_ADD_GOP_HEADER,
  PROP_STREAM_FORMAT
};

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS ("video/x-h264, "
    "stream-format=(string) { byte-stream, avc, avc3 }, "
    "alignment=(string) { nal };"
    "video/x-h265, "
    "stream-format=(string) { byte-stream, hvc1, hev1 }, "
    "alignment=(string) { nal }"));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("NV12"))
  );


#define gst_amba_hwvdec_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstAmbaHwvdec, gst_amba_hwvdec, GST_TYPE_VIDEO_DECODER,
  GST_DEBUG_CATEGORY_INIT(gst_amba_hwvdec_debug, "amba hw decoder", 0,
  "amba HW Video Decoder"));

static void gst_amba_hwvdec_set_property (GObject * object,
  guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_amba_hwvdec_get_property (GObject * object,
  guint prop_id, GValue * value, GParamSpec * pspec);

static void gst_amba_hwvdec_finalize (GObject * object);

static gboolean gst_amba_hwvdec_start (GstVideoDecoder * decoder);
static gboolean gst_amba_hwvdec_stop (GstVideoDecoder * decoder);
static gboolean gst_amba_hwvdec_set_format (GstVideoDecoder * decoder,
  GstVideoCodecState * state);
static GstFlowReturn gst_amba_hwvdec_finish (GstVideoDecoder * decoder);
static gboolean gst_amba_hwvdec_flush (GstVideoDecoder * decoder);
static GstFlowReturn gst_amba_hwvdec_drain (GstVideoDecoder * decoder);
static GstFlowReturn gst_amba_hwvdec_handle_frame (GstVideoDecoder * decoder,
  GstVideoCodecFrame * frame);

#ifndef D_SIMPLE_VERSION
/* GObject vmethod implementations */

GType
gst_amba_hwvdec_compliances_get_type (void)
{
  static gsize amba_hwvdec_type = 0;
  static const GEnumValue compliances[] = {
    {GST_AMBAHWVDEC_COMPLIANCE_AUTO, "GST_AMBAHWVDEC_COMPLIANCE_AUTO",
        "auto"},
    {GST_AMBAHWVDEC_COMPLIANCE_STRICT, "GST_AMBAHWVDEC_COMPLIANCE_STRICT",
        "strict"},
    {GST_AMBAHWVDEC_COMPLIANCE_NORMAL, "GST_AMBAHWVDEC_COMPLIANCE_NORMAL",
        "normal"},
    {GST_AMBAHWVDEC_COMPLIANCE_FLEXIBLE,
        "GST_AMBAHWVDEC_COMPLIANCE_FLEXIBLE", "flexible"},
    {0, NULL, NULL},
  };


  if (g_once_init_enter (&amba_hwvdec_type)) {
    GType _type;

    _type = g_enum_register_static ("GstAmbaHwvdecCompliance", compliances);
    g_once_init_leave (&amba_hwvdec_type, _type);
  }

  return (GType) amba_hwvdec_type;
}
#endif

/* initialize the ambahwvdec's class */
static void
gst_amba_hwvdec_class_init (GstAmbaHwvdecClass * klass)
{
  GstVideoDecoderClass *decoder_class = GST_VIDEO_DECODER_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_amba_hwvdec_set_property;
  gobject_class->get_property = gst_amba_hwvdec_get_property;
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_amba_hwvdec_finalize);

  decoder_class->start = GST_DEBUG_FUNCPTR (gst_amba_hwvdec_start);
  decoder_class->stop = GST_DEBUG_FUNCPTR (gst_amba_hwvdec_stop);
  decoder_class->set_format = GST_DEBUG_FUNCPTR (gst_amba_hwvdec_set_format);
  decoder_class->finish = GST_DEBUG_FUNCPTR (gst_amba_hwvdec_finish);
  decoder_class->flush = GST_DEBUG_FUNCPTR (gst_amba_hwvdec_flush);
  decoder_class->drain = GST_DEBUG_FUNCPTR (gst_amba_hwvdec_drain);
  decoder_class->handle_frame =
    GST_DEBUG_FUNCPTR (gst_amba_hwvdec_handle_frame);

  g_object_class_install_property (gobject_class, PROP_VOUT_NUM,
      g_param_spec_uchar ("vout-number", "VoutNum", "setup vout number ?",
          0, DAMBADSP_MAX_VOUT_NUMBER, 1, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_DECODER_NUM,
      g_param_spec_uchar ("decoder-number", "DecNum", "setup decoder number ?",
          0, DAMBADSP_MAX_DECODER_NUMBER, 1, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_DECODER_ID,
      g_param_spec_uchar ("decoder-id", "DecId", "setup decoder id ?",
          0, DAMBADSP_MAX_DECODER_NUMBER, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_DECODER_CONFIG_ID,
      g_param_spec_uchar ("decoder-config-id", "DecConfId", "provide decoder id to be configured?",
          0, DAMBADSP_MAX_DECODER_NUMBER, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_VOUT_ID,
      g_param_spec_uchar ("vout-id", "VoutId", "setup vout id?",
          0, DAMBADSP_MAX_VOUT_NUMBER, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_VOUT_CONFIG_ID,
      g_param_spec_uchar ("vout-config-id", "VoutConfId", "provide vout id to be configured?",
          0, DAMBADSP_MAX_VOUT_NUMBER, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_ENABLE_VOUT,
      g_param_spec_uchar ("enable-vout", "EnVout", "enable vout for configured decoder?",
          0, 1, 1, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_ENABLE_PB_PYRAMID,
      g_param_spec_uchar ("enable-pb-pyramid", "EnPyramid", "enable pb pyramid for configured decoder?",
          0, 1, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_HEVC_PER_TILE,
      g_param_spec_uchar ("enable-hevc-per-tile", "HEVCPerTile", "enable hevc per tile?",
          0, 1, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_HEVC_TILE_NUM,
      g_param_spec_uchar ("hevc-tile-num", "HEVCPerTile", "hevc tile number?",
          0, 32, 3, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_VOUT_MODE,
      g_param_spec_string ("vout-mode", "VoutMode", "vout mode for configured decoder?",
          NULL, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_VOUT_SINK_TYPE,
      g_param_spec_string ("vout-sink-type", "VoutSinkType", "vout sink type for configured decoder?",
          NULL, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_VOUT_DEVICE,
      g_param_spec_string ("vout-device", "VoutDevice", "vout device for configured decoder?",
          NULL, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_VOUT_DIGITAL,
      g_param_spec_uchar ("enable-vout-digital", "VoutDigital", "enable vout digital for configured decoder?",
          0, 1, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_VOUT_HDMI,
      g_param_spec_uchar ("enable-vout-hdmi", "VoutHdmi", "enable vout hdmi for configured decoder?",
          0, 1, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_VOUT_DIGITAL,
      g_param_spec_uchar ("enable-vout-cvbs", "VoutCvbs", "enable vout cvbs for configured decoder?",
          0, 1, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_CAP_MAX_CODED_HEIGHT,
      g_param_spec_uint ("cap-max-codec-height", "CapMaxCodecHeight", "cap max codec height for configured decoder?",
          0, 1080, 480, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_CAP_MAX_CODED_WIDTH,
      g_param_spec_uint ("cap-max-codec-width", "CapMaxCodecWidth", "cap max codec width for configured decoder?",
          0, 1920, 720, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_MAX_GOPSIZE,
      g_param_spec_uchar ("max-gop-size", "MaxGopSize", "max gop size?",
          0, 32, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_CUR_GOPSIZE,
      g_param_spec_uchar ("cur-gop-size", "CurGopSize", "current gop size?",
          0, 32, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_CODEC_FORMAT,
      g_param_spec_uint ("codec-format", "CodecFormat", "codec format for configured decoder?",
          0, 256, (guint) StreamFormat_H264, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_SUPPORT_FAST_FW_FAST_BW_BACKWARD_PLAYBACK,
      g_param_spec_uchar ("support-fast-fw-fast-bw-backward-playback", "FFFBBW", "support fast fw fast bw backward playback?",
          0, 1, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_SUPPORT_All_FRAME_BACKWARD_PLAYBACK,
      g_param_spec_uchar ("support-all-frame-backward-playback", "ALLFrameBW", "support all frame backward playback?",
          0, 1, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_FRAME_RATE_NUM,
      g_param_spec_uint ("frame-rate-num", "FrameRateNum", "frame rate number?",
          0, UINT_MAX, DDefaultVideoFramerateNum, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_FRAME_RATE_DEN,
      g_param_spec_uint ("frame-rate-den", "FrameRateDen", "frame rate den?",
          0, UINT_MAX, DDefaultVideoFramerateDen, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_ADD_GOP_HEADER,
      g_param_spec_uchar ("add-gop-header", "AddGopHead", "add gop header?",
          0, 1, 1, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_STREAM_FORMAT,
      g_param_spec_string ("stream-format", "StreamFormat", "stream format?",
          "h264", G_PARAM_READWRITE));

  gst_element_class_add_static_pad_template (gstelement_class,
      &sink_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &src_template);

  gst_element_class_set_static_metadata (gstelement_class,
      "amba hw video decoder", "Decoder/Video", "amba video decoder",
      "PengXue Duan <<pxduan@ambarella.com>>");

}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad callback functions
 * initialize instance structure
 */
static void
gst_amba_hwvdec_init (GstAmbaHwvdec * filter)
{
  gst_video_decoder_set_packetized (GST_VIDEO_DECODER (filter), TRUE);
  gst_video_decoder_set_needs_format (GST_VIDEO_DECODER (filter), TRUE);

  filter->mbAddAmbaGopHeader = 1;
  filter->mDecId = 0;
  filter->decoder_set_id = 0;
  filter->decoder_number = 1;
  filter->mbBWplayback = 0;
  filter->vout_number = 1;
  filter->mbDiscardCurrentGOP = 0;
  filter->mbGopBasedFeed = 0;
  filter->mbAutoMapBSB = 0;
  filter->mbSendDecodeReadyMsg = 0;
  filter->mbHEVCPerTile = 0;
  filter->mTileIndex = 0;
  filter->mTileNum = 3;
  filter->mMaxGopSize = 0;
  filter->mCurGopSize = 0;
  filter->mSpecifiedTimeScale = 0;
  filter->mSpecifiedFrameTick = 0;
  filter->mFrameRateNum = 0;
  filter->mFrameRateDen = 0;
  filter->mFrameRate = 30;
  filter->mDumpIndex = 0;
  filter->mpDumper = NULL;
  filter->mbSupportAllframeBackwardPlayback = 0;
  filter->mbExitDecodeMode = 0;
  filter->mbDumpBitstream = 0;
  filter->mbDumpOnly = 0;
  filter->b_setup_ctx_done = 0;
  filter->b_support_ff_fb_bw = 0;
  filter->mFeedingRule = DecoderFeedingRule_AllFrames;

  filter->max_vout0_width = 1920;
  filter->max_vout0_height = 1080;
  filter->max_vout1_width = 1920;
  filter->max_vout1_height = 1080;

  filter->codec_data = NULL;
  filter->codecdata_size = 0;
  filter->nal_length_size = 4;

  // Initialize decoder contexts
  for (int i = 0; i < DAMBADSP_MAX_DECODER_NUMBER; i++) {
    GstAmbaHwvdecDecoderCtx *ctx = &filter->decoder_ctx[i];
    ctx->mpBitSreamBufferStart = NULL;
    ctx->mpBitSreamBufferEnd = NULL;
    ctx->mpBitStreamBufferCurPtr = NULL;
    ctx->mH265FrameStartPtr = NULL;
    ctx->mH265FramePTS = 0;
    ctx->b_1st_frame = 1;
    ctx->b_dec_1_frame_done = 0;
    ctx->is_first_frame = 1;
    ctx->mbStopCmdSent = 0;
    ctx->mbGetBasePTS = 0;
    ctx->mBasePTS = 0;
    ctx->mFrameCount = 0;
    ctx->provided_pts = GST_CLOCK_TIME_NONE;
    ctx->width = 0;
    ctx->height = 0;
    ctx->p_cur_extradata = ctx->extradata_buf;
    ctx->extradata_size = 0;
    ctx->vps_size = 0;
    ctx->sps_size = 0;
    ctx->pps_size = 0;
    ctx->output_state = NULL;
    memset(&ctx->mDecCmdCtx, 0, sizeof(amba_dsp_decode_t));
    memset(ctx->mpAmbaGopHeader, 0, sizeof(ctx->mpAmbaGopHeader));

    // Initialize decoder-specific arrays
    filter->mCodecFormat[i] = StreamFormat_H264_BYTE;
    filter->mCapMaxCodedWidth[i] = 3840;
    filter->mCapMaxCodedHeight[i] = 2160;
    filter->vout_id[i] = 0;
    filter->pyramid_id[i] = 0;
    filter->enable_pb_pyramid[i] = 0;
    filter->enable_vout[i] = 0;
    filter->is_decoder_created[i] = 0;
  }

  // default vout config for dsp_v5
  filter->enable_vout[0] = 1;

  // default vout
  memcpy(filter->vout_configs[0].sinktype_string, "hdmi", strlen("hdmi") + 1);
  memcpy(filter->vout_configs[0].mode_string, "1080p", strlen("1080p") + 1);
  filter->vout_configs[0].b_hdmi_vout = 1;
  filter->vout_configs[0].b_digital_vout = 0;
  filter->vout_configs[0].b_cvbs_vout = 0;

  // iav context
  filter->iav_ctx = acquire_iav_ctx (1);
  if (!filter->iav_ctx) {
    DPRINT_ERROR("acquire_iav_ctx failed\n");
    return;
  }
  filter->hwtimer_outfreq = gst_amba_hwtimer_get_outfreq();
}

static void
gst_amba_hwvdec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAmbaHwvdec *filter = GST_AMBAHWVDEC (object);

  switch (prop_id) {
    case PROP_DECODER_NUM:
      filter->decoder_number = g_value_get_uchar (value);
      break;
    case PROP_VOUT_NUM:
      filter->vout_number = g_value_get_uchar (value);
      break;
    case PROP_DECODER_ID:
      filter->mDecId = g_value_get_uchar (value);
      break;
    case PROP_VOUT_CONFIG_ID:
      filter->vout_set_id = g_value_get_uchar (value);
      break;
    case PROP_VOUT_MODE:
      strncpy(filter->vout_configs[filter->vout_set_id].mode_string,
        g_value_get_string(value), DMAX_UT_VOUT_STRING_LENGTH - 1);
      break;
    case PROP_VOUT_SINK_TYPE: {
      strncpy(filter->vout_configs[filter->vout_set_id].sinktype_string,
        g_value_get_string(value), DMAX_UT_VOUT_STRING_LENGTH - 1);
      if (!strcmp("digital", g_value_get_string(value))) {
          filter->vout_configs[filter->vout_set_id].b_digital_vout = 1;
      } else if (!strcmp("hdmi", g_value_get_string(value))) {
          filter->vout_configs[filter->vout_set_id].b_hdmi_vout = 1;
      } else if (!strcmp("cvbs", g_value_get_string(value))) {
          filter->vout_configs[filter->vout_set_id].b_cvbs_vout = 1;
      } else {
          DPRINT_ERROR("not known vout %d sink type %s\n", filter->vout_set_id,
            g_value_get_string(value));
      }
      break;
    }
    case PROP_VOUT_DEVICE:
      strncpy(filter->vout_configs[filter->vout_set_id].device_string,
        g_value_get_string(value), DMAX_UT_VOUT_STRING_LENGTH - 1);
      break;
    case PROP_VOUT_DIGITAL: {
      filter->vout_configs[filter->vout_set_id].b_digital_vout = g_value_get_uchar (value);
      memcpy(filter->vout_configs[filter->vout_set_id].sinktype_string,
        "digital", sizeof("digital"));
      break;
    }
    case PROP_VOUT_HDMI: {
      filter->vout_configs[filter->vout_set_id].b_hdmi_vout = g_value_get_uchar (value);
      memcpy(filter->vout_configs[filter->vout_set_id].sinktype_string,
        "hdmi", sizeof("hdmi"));
      break;
    }
    case PROP_VOUT_CVBS: {
      filter->vout_configs[filter->vout_set_id].b_cvbs_vout = g_value_get_uchar (value);
      memcpy(filter->vout_configs[filter->vout_set_id].sinktype_string,
        "cvbs", sizeof("cvbs"));
      break;
    }

    case PROP_DECODER_CONFIG_ID:
      filter->decoder_set_id = g_value_get_uchar (value);
      break;
    case PROP_VOUT_ID:
      filter->vout_id[filter->decoder_set_id] = g_value_get_uchar (value);
      break;
    case PROP_ENABLE_VOUT:
      filter->enable_vout[filter->decoder_set_id] = g_value_get_uchar (value);
      break;
    case PROP_ENABLE_PB_PYRAMID: {
      filter->enable_pb_pyramid[filter->decoder_set_id] = g_value_get_uchar (value);
      SDSPPyramidConfig config_pyramid;
      memset(&config_pyramid, 0x0, sizeof(config_pyramid));

      config_pyramid.layers_map = 0x3f;
      config_pyramid.ext_buf_addr = 0;
      config_pyramid.ext_buf_size = 0;
      config_pyramid.scale_type = EDSP_PYRAMID_SCALE_SQRT2;

      for (int j = 0; j < DDSP_MAX_PYRAMID_LAYERS; j ++) {
          config_pyramid.crop_win[j].x = 0;
          config_pyramid.crop_win[j].y = 0;
          config_pyramid.crop_win[j].w = 0;
          config_pyramid.crop_win[j].h = 0;
      }

      memcpy(&filter->pb_pyramid_configs[filter->decoder_set_id],
          &config_pyramid, sizeof(config_pyramid));
      filter->pyramid_number++;
      break;
    }
    case PROP_CAP_MAX_CODED_WIDTH:
      filter->mCapMaxCodedWidth[filter->decoder_set_id] = g_value_get_uint (value);
      break;
    case PROP_CAP_MAX_CODED_HEIGHT:
      filter->mCapMaxCodedHeight[filter->decoder_set_id] = g_value_get_uint (value);
      break;
    case PROP_CODEC_FORMAT:
      filter->mCodecFormat[filter->decoder_set_id] = (StreamFormat) g_value_get_uint (value);
      break;
    case PROP_MAX_GOPSIZE:
      filter->mMaxGopSize = g_value_get_uchar (value);
      break;
    case PROP_CUR_GOPSIZE:
      filter->mCurGopSize = g_value_get_uchar (value);
      break;
    case PROP_HEVC_PER_TILE:
      filter->mbHEVCPerTile = g_value_get_uchar (value);
      break;
    case PROP_HEVC_TILE_NUM:
      filter->mTileNum = g_value_get_uchar (value);
      break;
    case PROP_SUPPORT_FAST_FW_FAST_BW_BACKWARD_PLAYBACK:
      filter->b_support_ff_fb_bw = g_value_get_uchar (value);
      break;
    case PROP_SUPPORT_All_FRAME_BACKWARD_PLAYBACK:
      filter->mbSupportAllframeBackwardPlayback = g_value_get_uchar (value);
      break;
    case PROP_FRAME_RATE_NUM:
      filter->mFrameRateNum = g_value_get_uint (value);
      break;
    case PROP_FRAME_RATE_DEN:
      filter->mFrameRateDen = g_value_get_uint (value);
      break;
    case PROP_ADD_GOP_HEADER:
      filter->mbAddAmbaGopHeader = g_value_get_uchar(value);
      break;
    case PROP_STREAM_FORMAT: {
      const char *name = g_value_get_string(value);
      if (!strncmp ("h264", name, strlen("h264"))) {
        filter->mCodecFormat[filter->decoder_set_id] = StreamFormat_H264;
      } else if (!strncmp ("h265", name, strlen("h265"))) {
        filter->mCodecFormat[filter->decoder_set_id] = StreamFormat_H265;
      } else {
        DPRINT_ERROR("not supported stream format: %s\n", name);
      }
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

}

static void
gst_amba_hwvdec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAmbaHwvdec *filter = GST_AMBAHWVDEC (object);

  switch (prop_id) {
    case PROP_DECODER_NUM:
      g_value_set_uchar (value, filter->decoder_number);
      break;
    case PROP_VOUT_NUM:
      g_value_set_uchar (value, filter->vout_number);
      break;
    case PROP_DECODER_ID:
      g_value_set_uchar (value, filter->mDecId);
      break;
    case PROP_DECODER_CONFIG_ID:
      g_value_set_uchar (value, filter->decoder_set_id);
      break;
    case PROP_VOUT_ID:
      g_value_set_uchar (value, filter->vout_id[filter->mDecId]);
      break;
    case PROP_VOUT_CONFIG_ID:
      g_value_set_uchar (value, filter->vout_set_id);
      break;
    case PROP_VOUT_MODE:
      g_value_set_string(value, filter->vout_configs[filter->vout_id[filter->mDecId]].mode_string);
      break;
    case PROP_VOUT_SINK_TYPE:
      g_value_set_string(value, filter->vout_configs[filter->vout_id[filter->mDecId]].sinktype_string);
      break;
    case PROP_VOUT_DEVICE:
      g_value_set_string(value, filter->vout_configs[filter->vout_id[filter->mDecId]].device_string);
      break;
    case PROP_VOUT_DIGITAL:
      g_value_set_uchar (value, filter->vout_configs[filter->vout_id[filter->mDecId]].b_digital_vout);
      break;
    case PROP_VOUT_HDMI:
      g_value_set_uchar (value, filter->vout_configs[filter->vout_id[filter->mDecId]].b_hdmi_vout);
      break;
    case PROP_VOUT_CVBS:
      g_value_set_uchar (value, filter->vout_configs[filter->vout_id[filter->mDecId]].b_cvbs_vout);
      break;
    case PROP_ENABLE_VOUT:
      g_value_set_uchar (value, filter->enable_vout[filter->mDecId]);
      break;
    case PROP_ENABLE_PB_PYRAMID:
      g_value_set_uchar (value, filter->enable_pb_pyramid[filter->mDecId]);
      break;
    case PROP_CAP_MAX_CODED_WIDTH:
      g_value_set_uint (value, filter->mCapMaxCodedWidth[filter->mDecId]);
      break;
    case PROP_CAP_MAX_CODED_HEIGHT:
      g_value_set_uint (value, filter->mCapMaxCodedHeight[filter->mDecId]);
      break;
    case PROP_CODEC_FORMAT:
      g_value_set_uint (value, (guint) filter->mCodecFormat[filter->mDecId]);
      break;
    case PROP_MAX_GOPSIZE:
      g_value_set_uchar (value, filter->mMaxGopSize);
      break;
    case PROP_CUR_GOPSIZE:
      g_value_set_uchar (value, filter->mCurGopSize);
      break;
    case PROP_HEVC_PER_TILE:
       g_value_set_uchar (value, filter->mbHEVCPerTile);
      break;
    case PROP_HEVC_TILE_NUM:
      g_value_set_uchar (value, filter->mTileNum);
      break;
    case PROP_SUPPORT_FAST_FW_FAST_BW_BACKWARD_PLAYBACK:
      g_value_set_uchar (value, filter->b_support_ff_fb_bw);
      break;
    case PROP_SUPPORT_All_FRAME_BACKWARD_PLAYBACK:
      g_value_set_uchar (value, filter->mbSupportAllframeBackwardPlayback);
      break;
    case PROP_FRAME_RATE_NUM:
      g_value_set_uint (value, filter->mFrameRateNum);
      break;
    case PROP_FRAME_RATE_DEN:
      g_value_set_uint (value, filter->mFrameRateDen);
      break;
    case PROP_ADD_GOP_HEADER:
      g_value_set_uchar(value, filter->mbAddAmbaGopHeader);
      break;
    case PROP_STREAM_FORMAT: {
      if (filter->mCodecFormat[filter->mDecId] == StreamFormat_H264 ||
        filter->mCodecFormat[filter->mDecId] == StreamFormat_H264_BYTE) {
        g_value_set_string (value, "h264");
      } else if (filter->mCodecFormat[filter->mDecId] == StreamFormat_H265 ||
        filter->mCodecFormat[filter->mDecId] == StreamFormat_H265_BYTE) {
        g_value_set_string (value, "h265");
      } else {
        DPRINT_ERROR("not supported stream foramt now\n");
      }
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

#ifdef DEV
static unsigned char * nalu_find_first_avc_slice_header_type (
  unsigned char * p, unsigned int len, unsigned char * out_nal_type)
{
  unsigned char nal_type = 0;

  if (!p) {
    return NULL;
  }

  while (len > 5) {
    if (*p == 0x00) {
      if (* (p + 1) == 0x00) {
        if (* (p + 2) == 0x00) {
          if (* (p + 3) == 0x01) {
            nal_type = (* (p + 4) ) & 0x1F;
            *out_nal_type = nal_type;
            if (nal_type <= ENalType_IDR) {
              return p;
            }
          }

        } else if (* (p + 2) == 0x01) {
          nal_type = (* (p + 3) ) & 0x1F;
          *out_nal_type = nal_type;
          if (nal_type <= ENalType_IDR) {
            return p;
          }
        }
      }
    }

    ++ p;
    len --;
  }

  return NULL;
}
#endif

static unsigned char * nalu_find_first_hevc_slice_header_type (
  unsigned char * p, unsigned int len, unsigned char * out_nal_type, unsigned char * is_first_slice)
{
  unsigned char nal_type = 0;

  if (!p) {
    return NULL;
  }

  *is_first_slice = 0;

  while (len > 5) {
    if (*p == 0x00) {
      if (* (p + 1) == 0x00) {
        if (* (p + 2) == 0x00) {
          if (* (p + 3) == 0x01) {
            nal_type = ( ( (* (p + 4) ) >> 1) & 0x3F);
            *out_nal_type = nal_type;

            if (nal_type < EHEVCNalType_VPS) {
              if (p[6] & 0x80) {
                *is_first_slice = 1;
              } else {
                *is_first_slice = 0;
              }
            }

            return p;
          }

        } else if (* (p + 2) == 0x01) {
          nal_type = ( ( (* (p + 3) ) >> 1) & 0x3F);
          *out_nal_type = nal_type;

          if (nal_type < EHEVCNalType_VPS) {
            if (p[5] & 0x80) {
              *is_first_slice = 1;
            } else {
              *is_first_slice = 0;
            }

          }

          return p;
        }
      }
    }

    ++ p;
    len --;
  }

  return NULL;
}

static unsigned char * nalu_find_first_avc_nal_type(
  unsigned char * p, unsigned int len, unsigned char *out_nal_type)
{
  if (NULL == p) {
    return NULL;
  }

  while (len > 5) {
    if (*p == 0x00) {
      if (*(p + 1) == 0x00) {
        if (*(p + 2) == 0x00) {
          if (*(p + 3) == 0x01) {
            *out_nal_type = ((*(p + 4)) & 0x1F);
            return p;
          }
        } else if (*(p + 2) == 0x01) {
          *out_nal_type = ((*(p + 3)) & 0x1F);
          return p;
        }
      }
    }
    ++ p;
    len --;
  }

  return NULL;
}

#ifdef DEV
static unsigned char * nalu_find_first_hevc_nal_type(
  unsigned char * p, unsigned int len, unsigned char *out_nal_type)
{
  if (NULL == p) {
    return NULL;
  }

  while (len > 5) {
    if (*p == 0x00) {
      if (*(p + 1) == 0x00) {
        if (*(p + 2) == 0x00) {
          if (*(p + 3) == 0x01) {
            *out_nal_type = (((*(p + 4)) >> 1) & 0x3F);
            return p;
          }
        } else if (*(p + 2) == 0x01) {
          *out_nal_type = (((*(p + 3)) >> 1) & 0x3F);
          return p;
        }
      }
    }
    ++ p;
    len --;
  }

  return NULL;
}
#endif

static gint create_decoder(GstAmbaHwvdec * self,
  guchar decoder_id, guchar decoder_type, guint width, guint height)
{
  gint ret = 0;
  GstAmbaHwvdecDecoderCtx *ctx = &self->decoder_ctx[decoder_id];

  self->mDecoderInfo[decoder_id].decoder_id = decoder_id;
  self->mDecoderInfo[decoder_id].decoder_type = decoder_type;
  self->mDecoderInfo[decoder_id].width = width;
  self->mDecoderInfo[decoder_id].height = height;

  ctx->width = width;
  ctx->height = height;

  if (self->enable_vout[decoder_id]) {
    memcpy(&self->mDecoderInfo[decoder_id].vout_configs[0],
      &self->iav_ctx->dec_mode.vout_configs[decoder_id],
      sizeof(amba_dsp_dec_vout_config_t));
    self->mDecoderInfo[decoder_id].num_vout = 1;
  }

  if (!self->mbAutoMapBSB) {
      if (!self->mDecoderInfo[decoder_id].b_use_addr) {
          // b_use_addr=0 means bsb_start_offset is a byte offset from base, not absolute address
          guchar *base_addr = (guchar *)self->iav_ctx->map_dec_bsb.base;
          ctx->mpBitStreamBufferCurPtr = ctx->mpBitSreamBufferStart = base_addr + self->mDecoderInfo[decoder_id].bsb_start_offset;
          ctx->mpBitSreamBufferEnd = ctx->mpBitSreamBufferStart + self->mDecoderInfo[decoder_id].bsb_size;
      } else {
          DPRINT_ERROR("mbAutoMapBSB=false but b_use_addr=true, should not here\n");
          // Fallback: treat as absolute address (though this shouldn't happen)
          ctx->mpBitStreamBufferCurPtr = ctx->mpBitSreamBufferStart = (guchar *)(unsigned long)self->mDecoderInfo[decoder_id].bsb_start_offset;
          ctx->mpBitSreamBufferEnd = ctx->mpBitSreamBufferStart + self->mDecoderInfo[decoder_id].bsb_size;
      }
  } else {
      if (!self->mDecoderInfo[decoder_id].b_use_addr) {
          // b_use_addr=1 means bsb_start_offset is an absolute address
          ctx->mpBitStreamBufferCurPtr = ctx->mpBitSreamBufferStart = (guchar *) (unsigned long) self->mDecoderInfo[decoder_id].bsb_start_offset;
          ctx->mpBitSreamBufferEnd = ctx->mpBitSreamBufferStart + self->mDecoderInfo[decoder_id].bsb_size;
      } else {
          DPRINT_ERROR("mbAutoMapBSB=true but b_use_addr=false, should not here\n");
          // Fallback: treat as offset from base
          guchar *base_addr = (guchar *)self->iav_ctx->map_dec_bsb.base;
          ctx->mpBitStreamBufferCurPtr = ctx->mpBitSreamBufferStart = base_addr + self->mDecoderInfo[decoder_id].bsb_start_offset;
          ctx->mpBitSreamBufferEnd = ctx->mpBitSreamBufferStart + self->mDecoderInfo[decoder_id].bsb_size;
      }
  }

  // Ensure buffers are initialized
  if (!ctx->mpBitSreamBufferStart || !ctx->mpBitSreamBufferEnd) {
      DPRINT_ERROR("Failed to initialize bitstream buffers for decoder %d\n", decoder_id);
      return -1;
  }

  if (!self->mbDumpOnly) {
        ret = self->iav_ctx->iav_al.f_create_decoder(self->iav_ctx->iav_fd, &self->mDecoderInfo[decoder_id]);
        if (0 > ret) {
            DPRINT_ERROR("create decoder fail, ret %d\n", ret);
            return ret;
        }
    }

  return ret;
}

static gint destroy_decoder(GstAmbaHwvdec * self, guchar decoder_id)
{
  gint ret = 0;
  if (!self->mbDumpOnly) {
      DPRINT_NOTICE("destroy decoder %d...\n", decoder_id);
      ret = self->iav_ctx->iav_al.f_destroy_decoder(self->iav_ctx->iav_fd, decoder_id);
      if (0 > ret) {
          DPRINT_ERROR("destroy decoder %d fail, ret %d\n", decoder_id, ret);
          return ret;
      }
      DPRINT_NOTICE("destroy decoder %d done\n", decoder_id);
  }
  return ret;
}

static gint create_decoder_with_sps_dimensions(GstAmbaHwvdec * self, guchar decoder_id, guint width, guint height)
{
  gint err = 0;

  if (self->is_decoder_created[decoder_id]) {
    DPRINT_WARNING("Decoder %d already created, skipping\n", decoder_id);
    return 0;
  }

  DPRINT_INFO("Creating decoder %d with SPS dimensions: %ux%u\n", decoder_id, width, height);

  if (StreamFormat_H264 == self->mCodecFormat[decoder_id]
    || StreamFormat_H264_BYTE == self->mCodecFormat[decoder_id]) {
      err = create_decoder(self, decoder_id, EAMDSP_VIDEO_CODEC_TYPE_H264, width, height);
  } else if (StreamFormat_H265 == self->mCodecFormat[decoder_id]
    || StreamFormat_H265_BYTE == self->mCodecFormat[decoder_id]) {
      err = create_decoder(self, decoder_id, EAMDSP_VIDEO_CODEC_TYPE_H265, width, height);
  } else {
      DPRINT_ERROR("bad format 0x%08x for decoder %d\n", (guint) self->mCodecFormat[decoder_id], decoder_id);
      err = -1;
  }

  if (err) {
      DPRINT_ERROR("create decoder %d fail\n", decoder_id);
      return err;
  }

  if (!self->mbDumpOnly) {
      self->iav_ctx->iav_al.f_start(self->iav_ctx->iav_fd, decoder_id);
      self->iav_ctx->iav_al.f_speed(self->iav_ctx->iav_fd, decoder_id,
          0x100, EAMDSP_PB_SCAN_MODE_ALL_FRAMES, EAMDSP_PB_DIRECTION_FW);
  }

  self->mbSendDecodeReadyMsg = 0;
  self->is_decoder_created[decoder_id] = 1;

  return 0;
}


static gint setup_context(GstAmbaHwvdec * self)
{
  gint err = 0;
  gint decoder_number = 0;
  gssize bsb_size = 0, bsb_per_decoder = 0;

  amba_dsp_query_decode_config_t dec_config;
  memset(&dec_config, 0x0, sizeof(dec_config));
  self->iav_ctx->iav_al.f_query_decode_config(self->iav_ctx->iav_fd, &dec_config);
  self->mbAutoMapBSB = dec_config.auto_map_bsb;

  for (int i = 0; i < self->decoder_number; i++) {
    if (!self->mCapMaxCodedWidth[i] || !self->mCapMaxCodedHeight[i]) {
      self->mCapMaxCodedWidth[i] = 1920;
      self->mCapMaxCodedHeight[i] = 1080;
      DPRINT_WARNING("decoder %d max coded size not specified, use default %u x %u\n",
          i, self->mCapMaxCodedWidth[i], self->mCapMaxCodedHeight[i]);
    }

    if (self->mbSupportAllframeBackwardPlayback &&
        ((self->mCapMaxCodedWidth[i] * self->mCapMaxCodedHeight[i] * self->mMaxGopSize) > (1920 * 1088 * 30))) {
        DPRINT_WARNING("disable all frame backward mode, with larger than 1920x1088x30 clips\n");
        self->mbSupportAllframeBackwardPlayback = 0;
    }
  }

  if (self->mFrameRateNum && self->mFrameRateDen) {
    self->mSpecifiedTimeScale = self->mFrameRateNum;
    self->mSpecifiedFrameTick = self->mFrameRateDen;
  }

  err = enter_decode_mode(self);
  if (err) {
      DPRINT_ERROR("enter decode mode fail\n");
      return err;
  }

  decoder_number = self->iav_ctx->dec_mode.mModeConfig.num_decoder;
  self->decoder_number = decoder_number;
  bsb_size = self->iav_ctx->map_dec_bsb.size;
  bsb_per_decoder = ROUND_DOWN((u64)bsb_size / decoder_number, DECODER_BSB_ALIGN_SIZE);
  if (!self->mbAutoMapBSB) {
        guchar *base_addr = (guchar *)self->iav_ctx->map_dec_bsb.base;
        for (int i = 0; i < decoder_number; i++) {
            guchar *decoder_addr = base_addr + i * bsb_per_decoder;
            // When b_use_addr is 0, store byte offset from base (not absolute address)
            // This avoids truncation of 64-bit addresses into 32-bit field
            self->mDecoderInfo[i].bsb_start_offset = (unsigned int)(decoder_addr - base_addr);
            self->mDecoderInfo[i].bsb_size = bsb_per_decoder;
            self->mDecoderInfo[i].b_use_addr = 0;
            //DPRINT_NOTICE("decoder %d bsb_start_offset = %u (offset from base), actual addr = %p, bsb_size = %u\n",
            //    i, self->mDecoderInfo[i].bsb_start_offset, decoder_addr, self->mDecoderInfo[i].bsb_size);
        }
    }

  // Initialize bitstream buffer pointers for all decoders to avoid NULL pointer errors
  // before decoder is created (which happens when SPS is detected)
  if (!self->mbAutoMapBSB) {
      guchar *base_addr = (guchar *)self->iav_ctx->map_dec_bsb.base;
      for (int i = 0; i < decoder_number; i++) {
          if (self->mDecoderInfo[i].bsb_start_offset != 0 || i == 0) {
              GstAmbaHwvdecDecoderCtx *ctx = &self->decoder_ctx[i];
              // Reconstruct pointer from base + offset
              ctx->mpBitStreamBufferCurPtr = ctx->mpBitSreamBufferStart = base_addr + self->mDecoderInfo[i].bsb_start_offset;
              ctx->mpBitSreamBufferEnd = ctx->mpBitSreamBufferStart + self->mDecoderInfo[i].bsb_size;
              DPRINT_NOTICE("Initialized BSB buffers for decoder %d: start=%p, end=%p, size=%u\n",
                  i, ctx->mpBitSreamBufferStart, ctx->mpBitSreamBufferEnd, self->mDecoderInfo[i].bsb_size);
          }
      }
  }

  // Initialize GOP headers for all decoders, but don't create decoder yet - wait for SPS
  for (int i = 0; i < decoder_number; i++) {
    GstAmbaHwvdecDecoderCtx *ctx = &self->decoder_ctx[i];
    if (StreamFormat_H264 == self->mCodecFormat[i]
      || StreamFormat_H264_BYTE == self->mCodecFormat[i]) {
        if (i == self->mDecId) {
          DPRINT_INFO("amba h264 dec %d, framerate %d/%d, waiting for SPS to create decoder\n",
              i, self->mSpecifiedTimeScale, self->mSpecifiedFrameTick);
        }
        gstFillAmbaH264GopHeader(ctx->mpAmbaGopHeader, self->mSpecifiedFrameTick, self->mSpecifiedTimeScale, 0, 0, 1);
    } else if (StreamFormat_H265 == self->mCodecFormat[i]
      || StreamFormat_H265_BYTE == self->mCodecFormat[i]) {
        if (i == self->mDecId) {
          DPRINT_INFO("amba h265 dec %d, framerate %d/%d, waiting for SPS to create decoder\n",
              i, self->mSpecifiedTimeScale, self->mSpecifiedFrameTick);
        }
        gstFillAmbaH265GopHeader(ctx->mpAmbaGopHeader, self->mSpecifiedFrameTick, self->mSpecifiedTimeScale, 0, 0, 1);
    }
  }

  // Check if current decoder has valid format
  if (StreamFormat_H264 != self->mCodecFormat[self->mDecId]
      && StreamFormat_H264_BYTE != self->mCodecFormat[self->mDecId]
      && StreamFormat_H265 != self->mCodecFormat[self->mDecId]
      && StreamFormat_H265_BYTE != self->mCodecFormat[self->mDecId]) {
      DPRINT_ERROR("bad format 0x%08x for decoder %d\n", (guint) self->mCodecFormat[self->mDecId], self->mDecId);
      err = -1;
      return err;
  }

  if (self->mbDumpBitstream) {
      if (self->mpDumper) {
          fclose((FILE *) self->mpDumper);
          self->mpDumper = NULL;
      }
      char filename[128] = {0};
      snprintf(filename, 127, "/tmp/amba_dec_%04d.h264", self->mDumpIndex);
      self->mDumpIndex ++;
      FILE *p = fopen((const char *)filename, "wb+");
      if (!p) {
          DPRINT_ERROR("open file (%s) fail\n", filename);
          return -1;
      }
      self->mpDumper = p;
  }

  self->mbBWplayback = 0;
  self->mbSendDecodeReadyMsg = 0;
  self->b_setup_ctx_done = 1;

  return COM_ECODE_OK;
}

static gint destroy_context(GstAmbaHwvdec * self)
{
  gint err = 0;

  if (self->mbDumpOnly) {
      return 0;
  }

  // Stop all active decoders that haven't been stopped yet
  for (int i = 0; i < DAMBADSP_MAX_DECODER_NUMBER; i++) {
      if (self->is_decoder_created[i] && !self->decoder_ctx[i].mbStopCmdSent) {
          self->iav_ctx->iav_al.f_stop(self->iav_ctx->iav_fd, i, 1);
          self->decoder_ctx[i].mbStopCmdSent = 1;
          DPRINT_NOTICE("Stopped decoder %d in destroy_context\n", i);
      }
  }
  self->mbSendDecodeReadyMsg = 0;

  // Destroy all created decoders
  for (int i = 0; i < DAMBADSP_MAX_DECODER_NUMBER; i++) {
      if (self->is_decoder_created[i]) {
          err = destroy_decoder(self, i);
          if (COM_ECODE_OK != err) {
              DPRINT_ERROR("destroy decoder %d fail\n", i);
          }
          self->is_decoder_created[i] = 0;
      }
  }

  // Reset all decoder contexts
  for (int i = 0; i < DAMBADSP_MAX_DECODER_NUMBER; i++) {
      self->decoder_ctx[i].b_1st_frame = 1;
      self->decoder_ctx[i].is_first_frame = 1;
      self->decoder_ctx[i].b_dec_1_frame_done = 0;
      self->decoder_ctx[i].mbStopCmdSent = 0;
  }

  err = leave_decode_mode(self->iav_ctx);//to do
  if (err) {
      DPRINT_ERROR("leave decode mode fail\n");
  }

  return err;
}

//to do
static gint set_codec_data(GstAmbaHwvdec * self, guchar *p, gulong size)
{
  if (G_UNLIKELY((!p) || (!size))) {
      DPRINT_ERROR("NULL extradata %p, or zero size %ld\n", p, size);
      return -1;
  }

  if (self->codec_data && (self->codecdata_size == size)) {
      if (!memcmp(self->codec_data, p, size)) {
          return 0;
      }
  }

  if (self->codec_data) {
      free(self->codec_data);
      self->codec_data = NULL;
      self->codecdata_size = 0;
  }

  self->codecdata_size = size;
  self->codec_data = (guchar *) malloc(self->codecdata_size);
  if (self->codec_data) {
      memcpy(self->codec_data, p, size);
  } else {
      DPRINT_ERROR("NO memory\n");
      return -1;
  }

  return 0;
}

static guchar *copy_data_to_bsb(GstAmbaHwvdec * self, guchar decoder_id,
  guchar *ptr, guchar *buffer, guint size)
{
    GstAmbaHwvdecDecoderCtx *ctx = &self->decoder_ctx[decoder_id];

    // Safety checks to prevent segmentation fault
    if (!ptr || !buffer || !ctx->mpBitSreamBufferStart || !ctx->mpBitSreamBufferEnd) {
        DPRINT_ERROR("copy_data_to_bsb: NULL pointer detected (decoder_id=%d, ptr=%p, buffer=%p, start=%p, end=%p)\n",
                     decoder_id, ptr, buffer, ctx->mpBitSreamBufferStart, ctx->mpBitSreamBufferEnd);
        return ptr;
    }

    if (size == 0) {
        DPRINT_WARNING("copy_data_to_bsb: zero size requested\n");
        return ptr;
    }

    // Check if pointers are within valid range
    if (ptr < ctx->mpBitSreamBufferStart || ptr > ctx->mpBitSreamBufferEnd) {
        DPRINT_ERROR("copy_data_to_bsb: ptr out of range (decoder_id=%d, ptr=%p, start=%p, end=%p)\n",
                     decoder_id, ptr, ctx->mpBitSreamBufferStart, ctx->mpBitSreamBufferEnd);
        return ptr;
    }

    if (self->mbDumpBitstream) {
        if (self->mpDumper) {
            fwrite(buffer, 1, size, (FILE *) self->mpDumper);
            fflush((FILE *) self->mpDumper);
        }
    }

    if (ptr + size <= ctx->mpBitSreamBufferEnd) {
        memcpy((void *)ptr, (const void *)buffer, size);
        return ptr + size;
    } else {
        int room = ctx->mpBitSreamBufferEnd - ptr;
        guchar *ptr2;
        memcpy((void *)ptr, (const void *)buffer, room);
        ptr2 = buffer + room;
        size -= room;
        memcpy((void *)ctx->mpBitSreamBufferStart, (const void *)ptr2, size);
        return ctx->mpBitSreamBufferStart + size;
    }
}

/**
 * gst_h264_parser_identify_nalu_avc:
 * @nalparser: a #GstH264NalParser
 * @data: The data to parse, containing an AVC coded NAL unit
 * @offset: the offset in @data from which to parse the NAL unit
 * @size: the size of @data
 * @nal_length_size: the size in bytes of the AVC nal length prefix.
 * @nalu: The #GstH264NalUnit to store the identified NAL unit in
 *
 * Parses the headers of an AVC coded NAL unit from @data and puts the result
 * into @nalu.
 *
 * Returns: a #GstH264ParserResult
 */
static gint
__identify_nalu_avc (
    const guchar * data, guint size, guint nal_length_size,
    H264NalUnit * nalu)
{
  guint nbits = nal_length_size * 8;
  guint bytes = 0, bits = 0;


  /* Would overflow guint below otherwise: the callers needs to ensure that
   * this never happens */
  if (nal_length_size > G_MAXUINT32) {
    GST_WARNING ("nal_length_size overflow");
    nalu->size = 0;
    return -1;
  }

  if (size < nal_length_size) {
    GST_DEBUG ("Can't parse, buffer has too small size %d\n", size);
    return -1;
  }

  nalu->size = 0;

  while (nbits > 0) {
    guint toread = MIN (nbits, 8 - bits);
    nalu->size <<= toread;
    nalu->size |= (data[bytes] & (0xff >> bits)) >> (8 - toread - bits);
    bits += toread;
    if (bits >= 8) {
      bytes++;
      bits = 0;
    }
    nbits -= toread;
  }

  if (nalu->size < 1) {
    GST_WARNING ("NALU size < 1");
    return -1;
  }
  nalu->sc_offset = 0;
  nalu->offset = nal_length_size;

  if ((nalu->size + nal_length_size > G_MAXUINT32) ||
    (nalu->size + nal_length_size > size)) {
    GST_WARNING ("NALU size + nal_length_size overflow");
    nalu->size = 0;
    return -1;
  }

  nalu->data = (guint8 *) data;

  nalu->sc_length = 4;
  nalu->start_code[3] = 0x01;
  nalu->valid = TRUE;

  nalu->type = (data[nalu->offset] & 0x1f);

  return 0;
}

static gint
__identify_nalu_hevc (
    const guchar * data, guint size, guint nal_length_size,
    H265NalUnit * nalu)
{
  guint nbits = nal_length_size * 8;
  guint bytes = 0, bits = 0;


  /* Would overflow guint below otherwise: the callers needs to ensure that
   * this never happens */
  if (nal_length_size > G_MAXUINT32) {
    GST_WARNING ("nal_length_size overflow");
    nalu->size = 0;
    return -1;
  }

  if (size < nal_length_size) {
    GST_DEBUG ("Can't parse, buffer has too small size %d", size);
    return -1;
  }

  nalu->size = 0;

  while (nbits > 0) {
    guint toread = MIN (nbits, 8 - bits);
    nalu->size <<= toread;
    nalu->size |= (data[bytes] & (0xff >> bits)) >> (8 - toread - bits);
    bits += toread;
    if (bits >= 8) {
      bytes++;
      bits = 0;
    }
    nbits -= toread;
  }

  if (nalu->size < 2) {
    GST_WARNING ("NALU size < 2");
    return -1;
  }
  nalu->sc_offset = 0;
  nalu->offset = nal_length_size;

  if ((nalu->size + nal_length_size > G_MAXUINT32)
    || (nalu->size + nal_length_size > size)) {
    GST_WARNING ("NALU size + nal_length_size overflow");
    nalu->size = 0;
    return -1;
  }

  nalu->data = (guint8 *) data;

  nalu->type = (data[nalu->offset] >> 1) & 0x3F;
  if (nalu->type < EHEVCNalType_VPS) {
    if (data[nalu->offset + 2] & 0x80) {
      nalu->is_first_slice = 1;
    } else {
      nalu->is_first_slice = 0;
    }
  }

  nalu->sc_length = 4;
  nalu->start_code[3] = 0x01;

  nalu->valid = TRUE;

  return 0;
}

static GstFlowReturn decodeH264(GstAmbaHwvdec * self, guchar decoder_id, GstBuffer *in_buf)
{
  guchar *p_data;
  guint size;
  int ret = 0;
  GstAmbaHwvdecDecoderCtx *ctx = &self->decoder_ctx[decoder_id];

  GstMapInfo map;

  unsigned char *p_check = NULL;
  unsigned char first_nal_type = 0;
  unsigned int pts_90k;

  H264NalUnit nalu;

  GstFlowReturn flow_ret = GST_FLOW_OK;

  int append_start_code = 0;

  if (G_UNLIKELY(!gst_buffer_map(in_buf, &map, GST_MAP_READ))) {
    DPRINT_ERROR("Failed to map the buffer!");
    gst_buffer_unref(in_buf);
    return GST_FLOW_ERROR;
  }

  size = map.size;
  p_data = map.data;

  memset (&nalu, 0, sizeof (H264NalUnit));

  if (self->mCodecFormat[decoder_id] == StreamFormat_H264) {
    __identify_nalu_avc(p_data, size, self->nal_length_size, &nalu);
    p_data = nalu.data + nalu.offset;
    size = nalu.size;
    first_nal_type = nalu.type;
    append_start_code = 1;
  } else {

    p_check = nalu_find_first_avc_nal_type(p_data, size, &first_nal_type);
    if (!p_check) {
      DPRINT_WARNING("no valid nalu, skip, size %d, first bytes:\n", size);
      if (16 > size) {
        print_memory_u8(p_data, size);
      } else {
        print_memory_u8(p_data, 16);
      }
      flow_ret = GST_FLOW_OK;
      goto tag_decode_h264_exit;
    }
    append_start_code = 0;
  }

  if ((ENalType_SEI == first_nal_type)
    || (ENalType_AUD == first_nal_type)) {
    // skip them
    flow_ret = GST_FLOW_OK;
    goto tag_decode_h264_exit;
  } else if (ENalType_SPS == first_nal_type) {
    // reset addr and size
    ctx->p_cur_extradata = ctx->extradata_buf;
    ctx->extradata_size = 0;

    // record sps
    if (append_start_code) {
      ctx->sps_size = size + nalu.sc_length;
      memcpy(ctx->p_cur_extradata, nalu.start_code, nalu.sc_length);
      memcpy(ctx->p_cur_extradata + ctx->sps_size - size, p_data, size);
    } else {
      ctx->sps_size = size;
      memcpy(ctx->p_cur_extradata, p_data, size);
    }

#ifdef D_PRINT_DEBUG_DATA
    // debug, print sps
    DPRINT_NOTICE("h264 sps:\n");
    print_memory_u8(ctx->p_cur_extradata, ctx->sps_size);
#endif

    // Parse SPS to get actual video dimensions and create decoder
    if (ctx->b_1st_frame) {
      guint sps_width = 0, sps_height = 0;
            // Check if we have enough data for SPS
      if (size < 4) {
        DPRINT_WARNING("H264 SPS too small: %u bytes\n", size);
        goto use_max_dimensions;
      }

      DPRINT_NOTICE("H264 SPS: original size=%u\n", size);
      DPRINT_INFO("H264 SPS first bytes: 0x%02x 0x%02x 0x%02x 0x%02x\n",
                  p_data[0], p_data[1], p_data[2], p_data[3]);

      // Find the actual NAL unit start by skipping start codes
      guchar *sps_data = p_data;
      guint sps_data_size = size;
      guint skip_bytes = 0;

      // Check for 4-byte start code: 0x00 0x00 0x00 0x01
      if (size >= 4 && p_data[0] == 0x00 && p_data[1] == 0x00 &&
          p_data[2] == 0x00 && p_data[3] == 0x01) {
        skip_bytes = 4;
        DPRINT_INFO("H264 SPS: Found 4-byte start code, skipping\n");
      }
      // Check for 3-byte start code: 0x00 0x00 0x01
      else if (size >= 3 && p_data[0] == 0x00 && p_data[1] == 0x00 && p_data[2] == 0x01) {
        skip_bytes = 3;
        DPRINT_INFO("H264 SPS: Found 3-byte start code, skipping\n");
      }
      // No start code, assume data starts with NAL unit
      else {
        skip_bytes = 0;
        DPRINT_INFO("H264 SPS: No start code found, assuming NAL unit starts immediately\n");
      }

      if (skip_bytes >= size) {
        DPRINT_WARNING("H264 SPS: Start code consumes entire buffer\n");
        goto use_max_dimensions;
      }

      sps_data = p_data + skip_bytes;
      sps_data_size = size - skip_bytes;

      DPRINT_INFO("H264 SPS: after skipping %u bytes, size=%u\n", skip_bytes, sps_data_size);
      DPRINT_INFO("H264 SPS NAL bytes: 0x%02x 0x%02x 0x%02x 0x%02x\n",
                  sps_data[0], sps_data[1], sps_data[2], sps_data[3]);

      // Validate NAL unit type (should be 0x67 for SPS)
      if ((sps_data[0] & 0x1F) != 7) {
        DPRINT_WARNING("H264 SPS: Invalid NAL unit type: 0x%02x (expected 0x67)\n", sps_data[0]);
        goto use_max_dimensions;
      }

      // Skip the NAL header byte to get to the actual SPS payload
      sps_data = sps_data + 1;
      sps_data_size = sps_data_size - 1;

      DPRINT_INFO("H264 SPS: final parsing data size=%u\n", sps_data_size);
      DPRINT_INFO("H264 SPS payload bytes: 0x%02x 0x%02x 0x%02x 0x%02x\n",
                  sps_data[0], sps_data[1], sps_data[2], sps_data[3]);

      // Check if SPS data needs RBSP conversion (remove emulation prevention bytes)
      // This is needed for byte-stream format but not for AVC format
      guchar *final_sps_data = sps_data;
      guint final_sps_size = sps_data_size;
      guchar *rbsp_buffer = NULL;

      if (self->mCodecFormat[decoder_id] == StreamFormat_H264_BYTE) {
        // For byte-stream format, we might need to remove emulation prevention bytes
        // Simple check: if we find 0x00 0x00 0x03 pattern, we need RBSP conversion
        gboolean needs_rbsp = FALSE;
        for (guint i = 0; i < sps_data_size - 2; i++) {
          if (sps_data[i] == 0x00 && sps_data[i+1] == 0x00 && sps_data[i+2] == 0x03) {
            needs_rbsp = TRUE;
            break;
          }
        }

        if (needs_rbsp) {
          DPRINT_INFO("H264 SPS: Converting RBSP (removing emulation prevention bytes)\n");
          // Use the existing RBSP conversion function from codec_parser.c
          // For now, let's try without RBSP conversion first
        }
      }

      gint parse_ret = get_h264_reso_from_sps(final_sps_data, final_sps_size, &sps_width, &sps_height);

      if (rbsp_buffer) {
        free(rbsp_buffer);
      }

      if (parse_ret == 0 && sps_width > 0 && sps_height > 0) {
        DPRINT_INFO("Parsed H264 SPS: %ux%u\n", sps_width, sps_height);

        gint create_ret = create_decoder_with_sps_dimensions(self, decoder_id, sps_width, sps_height);
        if (create_ret != 0) {
          DPRINT_ERROR("Failed to create decoder %d with SPS dimensions\n", decoder_id);
          flow_ret = GST_FLOW_ERROR;
          goto tag_decode_h264_exit;
        }

        ctx->width = sps_width;
        ctx->height = sps_height;
      } else {
        DPRINT_WARNING("Failed to parse H264 SPS dimensions (ret=%d), using max dimensions\n", parse_ret);

use_max_dimensions:
        // Fallback to max dimensions if parsing fails
        gint create_ret = create_decoder_with_sps_dimensions(self, decoder_id,
            self->mCapMaxCodedWidth[decoder_id], self->mCapMaxCodedHeight[decoder_id]);
        if (create_ret != 0) {
          DPRINT_ERROR("Failed to create decoder with max dimensions\n");
          flow_ret = GST_FLOW_ERROR;
          goto tag_decode_h264_exit;
        }

        ctx->width = self->mCapMaxCodedWidth[decoder_id];
        ctx->height = self->mCapMaxCodedHeight[decoder_id];
      }
    }

    ctx->p_cur_extradata += ctx->sps_size;

    flow_ret = GST_FLOW_OK;
    goto tag_decode_h264_exit;
  } else if (ENalType_PPS == first_nal_type) {
    // record pps
    if (append_start_code) {
      ctx->pps_size = size + nalu.sc_length;
      memcpy(ctx->p_cur_extradata, nalu.start_code, nalu.sc_length);
      memcpy(ctx->p_cur_extradata + nalu.sc_length, p_data, size);
    } else {
      ctx->pps_size = size;
      memcpy(ctx->p_cur_extradata, p_data, size);
    }

#ifdef D_PRINT_DEBUG_DATA
    // debug, print pps
    DPRINT_NOTICE("h264 pps:\n");
    print_memory_u8(ctx->p_cur_extradata, ctx->pps_size);
#endif

    // cal total extra data size
    ctx->extradata_size = ctx->sps_size + ctx->pps_size;

#ifdef D_PRINT_DEBUG_DATA
    // debug, print extra data
    DPRINT_NOTICE("h264 extradata:\n");
    print_memory_u8(ctx->extradata_buf, ctx->extradata_size);
#endif

    flow_ret = GST_FLOW_OK;
    goto tag_decode_h264_exit;
  } else {
#ifdef D_PRINT_DEBUG_DATA
    DPRINT_NOTICE("h264 data, nal type %d, size %d:\n", first_nal_type, size);
    print_memory_u8(p_data, 8);
#endif
  }

  if (ctx->mpBitStreamBufferCurPtr == ctx->mpBitSreamBufferEnd) {
    ctx->mpBitStreamBufferCurPtr = ctx->mpBitSreamBufferStart;
  }

  if (!self->mbDumpOnly) {
    ctx->mDecCmdCtx.decoder_id = decoder_id;
    ctx->mDecCmdCtx.num_frames = 1;
    if (!self->mbAutoMapBSB) {
      ctx->mDecCmdCtx.start_ptr_offset = (guint) (gulong) (ctx->mpBitStreamBufferCurPtr - ctx->mpBitSreamBufferStart);
    } else {
      ctx->mDecCmdCtx.start_ptr_offset = (guint) (gulong) (ctx->mpBitStreamBufferCurPtr);
    }
    ctx->mDecCmdCtx.first_frame_display = 0;

    ret = self->iav_ctx->iav_al.f_request_bsb(self->iav_ctx->iav_fd, decoder_id, size + 1024, ctx->mpBitStreamBufferCurPtr);
    if (DDECODER_STOPPED == ret) {
      flow_ret = GST_FLOW_OK;
      goto tag_decode_h264_exit;
    } else if (0 > ret) {
      DPRINT_ERROR("request bsb failed, return %d\n", ret);
      flow_ret = GST_FLOW_ERROR;
      goto tag_decode_h264_exit;
    }
  }

  // Use provided_pts which is set in handle_frame with priority: frame->pts > in_buf PTS > frame->dts
  if (ctx->provided_pts != GST_CLOCK_TIME_NONE) {
    pts_90k = gst_util_uint64_scale (ctx->provided_pts, self->hwtimer_outfreq, GST_SECOND);
  } else {
    // When PTS is invalid (e.g., video-only pipeline without audio clock),
    // generate PTS based on frame rate and frame count
    if (self->mFrameRateNum > 0 && self->mFrameRateDen > 0) {
      GstClockTime calculated_pts = gst_util_uint64_scale (ctx->mFrameCount,
          GST_SECOND * self->mFrameRateDen, self->mFrameRateNum);
      pts_90k = gst_util_uint64_scale (calculated_pts, self->hwtimer_outfreq, GST_SECOND);
      GST_DEBUG_OBJECT (self, "Generated PTS from frame count: decoder=%d, frame=%u, pts=%" GST_TIME_FORMAT,
          decoder_id, ctx->mFrameCount, GST_TIME_ARGS (calculated_pts));
    } else {
      DPRINT_ERROR("pts is invalid\n");
      flow_ret = GST_FLOW_ERROR;
      goto tag_decode_h264_exit;
    }
  }


  // Ensure decoder is created and buffers are initialized before copying data
  if (!self->is_decoder_created[decoder_id] || !ctx->mpBitSreamBufferStart || !ctx->mpBitSreamBufferEnd) {
    DPRINT_ERROR("Decoder %d not created or buffers not initialized, cannot copy data to BSB\n", decoder_id);
    flow_ret = GST_FLOW_ERROR;
    goto tag_decode_h264_exit;
  }

  if (ENalType_IDR == first_nal_type) {
    if (self->mbAddAmbaGopHeader) {
      gstUpdateAmbaH264GopHeader(
        (unsigned char *) ctx->mpAmbaGopHeader,
        pts_90k, self->mCurGopSize);
      ctx->mpBitStreamBufferCurPtr = copy_data_to_bsb(self, decoder_id, ctx->mpBitStreamBufferCurPtr, ctx->mpAmbaGopHeader, DAMBA_H264_GOP_HEADER_LENGTH);
    }
    // need always append extra data
    ctx->mpBitStreamBufferCurPtr = copy_data_to_bsb(self, decoder_id, ctx->mpBitStreamBufferCurPtr, ctx->extradata_buf, ctx->extradata_size);
  }

  if (append_start_code) {
    ctx->mpBitStreamBufferCurPtr = copy_data_to_bsb(self, decoder_id, ctx->mpBitStreamBufferCurPtr, nalu.start_code, nalu.sc_length);
  }
  ctx->mpBitStreamBufferCurPtr = copy_data_to_bsb(self, decoder_id, ctx->mpBitStreamBufferCurPtr, p_data, size);

  if (!self->mbDumpOnly) {
    if (!self->mbAutoMapBSB) {
      ctx->mDecCmdCtx.end_ptr_offset = (guint) (gulong) (ctx->mpBitStreamBufferCurPtr - ctx->mpBitSreamBufferStart);
    } else {
      ctx->mDecCmdCtx.end_ptr_offset = (guint) (gulong) (ctx->mpBitStreamBufferCurPtr);
    }
    ctx->mDecCmdCtx.first_frame_display = pts_90k;

    ret = self->iav_ctx->iav_al.f_decode(self->iav_ctx->iav_fd, &ctx->mDecCmdCtx);
    if (DDECODER_STOPPED == ret) {
      GST_INFO_OBJECT (self, "H264 decoder %d stopped, frame PTS=%" GST_TIME_FORMAT,
          decoder_id, GST_TIME_ARGS (ctx->provided_pts));
      flow_ret = GST_FLOW_OK;
      goto tag_decode_h264_exit;
    } else if (0 > ret) {
      GST_ERROR_OBJECT (self, "H264 f_decode failed: decoder=%d, ret=%d, PTS=%" GST_TIME_FORMAT,
          decoder_id, ret, GST_TIME_ARGS (ctx->provided_pts));
      flow_ret = GST_FLOW_ERROR;
      goto tag_decode_h264_exit;
    }
    ctx->b_dec_1_frame_done = 1;
    // Set flag to 0 after first frame is sent to hardware decoder
    ctx->b_1st_frame = 0;

    flow_ret = GST_FLOW_OK;
  }

tag_decode_h264_exit:

  gst_buffer_unmap (in_buf, &map);

  return flow_ret;
}

static GstFlowReturn decodeH265(GstAmbaHwvdec * self, guchar decoder_id, GstBuffer *in_buf)
{
  guchar *p_data;
  guint size;
  //guint b_append_extradata = 0;
  gint ret = 0;
  GstAmbaHwvdecDecoderCtx *ctx = &self->decoder_ctx[decoder_id];

  GstFlowReturn flow_ret = GST_FLOW_OK;
  H265NalUnit nalu;
  guchar first_nal_type = 0;
  guchar is_first_slice = 0;

  GstMapInfo map;

  guchar *p_check = NULL;

  if (G_UNLIKELY(!gst_buffer_map(in_buf, &map, GST_MAP_READ))) {
    DPRINT_ERROR("Failed to map the buffer!");
    gst_buffer_unref(in_buf);
    return GST_FLOW_ERROR;
  }

  size = map.size;
  p_data = map.data;
  //DPRINT_NOTICE("====================h265 decode: size: %d, p_data: %d\n", size, p_data);

  memset (&nalu, 0, sizeof (H265NalUnit));

  if (self->mCodecFormat[decoder_id] == StreamFormat_H265) {
    __identify_nalu_hevc(p_data, size, self->nal_length_size, &nalu);
    p_data = nalu.data + nalu.offset;
    size = nalu.size;
    first_nal_type = nalu.type;
    is_first_slice = nalu.is_first_slice;
  } else {
    p_check = nalu_find_first_hevc_slice_header_type(p_data, size, &first_nal_type, &is_first_slice);
    if (!p_check) {
      DPRINT_WARNING("not valid frame: not find frame data, discard current GOP. data size %d, first bytes:\n", size);
      if (16 > size) {
          print_memory_u8(p_data, size);
      } else {
          print_memory_u8(p_data, 16);
      }
      flow_ret = GST_FLOW_OK;
      goto tag_decode_h265_exit;
    }
  }

  //DPRINT_NOTICE("h265 nalu.type: %d, nalu.is_first_slice: %d, is_first_slice: %d\n", first_nal_type, nalu.is_first_slice, is_first_slice);


  // Handle Access Unit Delimiter (AUD) - indicates start of new frame
  if (EHEVCNalType_AUD == first_nal_type) {
    // Send previous frame if we have accumulated data and decoder is created
    if (ctx->mH265FrameStartPtr && ctx->mpBitStreamBufferCurPtr != ctx->mH265FrameStartPtr) {
      if (!self->mbDumpOnly) {
        if (!self->mbAutoMapBSB) {
          ctx->mDecCmdCtx.end_ptr_offset = (guint) (gulong) (ctx->mpBitStreamBufferCurPtr - ctx->mpBitSreamBufferStart);
        } else {
          ctx->mDecCmdCtx.end_ptr_offset = (guint) (gulong) (ctx->mpBitStreamBufferCurPtr);
        }
        ctx->mDecCmdCtx.first_frame_display = ctx->mH265FramePTS;

        if (ctx->b_1st_frame) {
          ret = self->iav_ctx->iav_al.f_request_bsb(self->iav_ctx->iav_fd, decoder_id, (ctx->mpBitStreamBufferCurPtr - ctx->mpBitSreamBufferStart) + 1024, ctx->mH265FrameStartPtr);
        }

        if (DDECODER_STOPPED == ret) {
          flow_ret = GST_FLOW_OK;
          goto tag_decode_h265_exit;
        } else if (0 > ret) {
          DPRINT_ERROR("request bsb fail, return %d\n", ret);
          flow_ret = GST_FLOW_ERROR;
          goto tag_decode_h265_exit;
        }

        ret = self->iav_ctx->iav_al.f_decode(self->iav_ctx->iav_fd, &ctx->mDecCmdCtx);
        if (DDECODER_STOPPED == ret) {
          GST_INFO_OBJECT (self, "H265 decoder %d stopped when sending previous frame", decoder_id);
          flow_ret = GST_FLOW_OK;
          goto tag_decode_h265_exit;
        } else if (0 > ret) {
          GST_ERROR_OBJECT (self, "H265 f_decode previous frame failed: decoder=%d, ret=%d", decoder_id, ret);
          flow_ret = GST_FLOW_ERROR;
          goto tag_decode_h265_exit;
        }
        ctx->b_dec_1_frame_done = 1;
        // Set flag to 0 after first frame is sent to hardware decoder
        ctx->b_1st_frame = 0;
      }
    }

    // Start new frame - set up decode command context for new frame
    if (ctx->mpBitStreamBufferCurPtr == ctx->mpBitSreamBufferEnd) {
      ctx->mpBitStreamBufferCurPtr = ctx->mpBitSreamBufferStart;
    }

    if (!self->mbDumpOnly) {
      ctx->mDecCmdCtx.decoder_id = decoder_id;
      ctx->mDecCmdCtx.num_frames = 1;
      if (!self->mbAutoMapBSB) {
        ctx->mDecCmdCtx.start_ptr_offset = (guint) (gulong) (ctx->mpBitStreamBufferCurPtr - ctx->mpBitSreamBufferStart);
      } else {
        ctx->mDecCmdCtx.start_ptr_offset = (guint) (gulong) (ctx->mpBitStreamBufferCurPtr);
      }
      ctx->mDecCmdCtx.first_frame_display = 0;
    }

    // Use provided_pts which is set in handle_frame with priority: frame->pts > in_buf PTS > frame->dts
    if (ctx->provided_pts != GST_CLOCK_TIME_NONE) {
      ctx->mH265FramePTS = gst_util_uint64_scale (ctx->provided_pts, self->hwtimer_outfreq, GST_SECOND);
    } else {
      // generate PTS based on frame rate and frame count
      if (self->mFrameRateNum > 0 && self->mFrameRateDen > 0) {
        GstClockTime calculated_pts = gst_util_uint64_scale (ctx->mFrameCount,
            GST_SECOND * self->mFrameRateDen, self->mFrameRateNum);
        ctx->mH265FramePTS = gst_util_uint64_scale (calculated_pts, self->hwtimer_outfreq, GST_SECOND);
        GST_DEBUG_OBJECT (self, "Generated PTS from frame count: decoder=%d, frame=%u, pts=%" GST_TIME_FORMAT,
            decoder_id, ctx->mFrameCount, GST_TIME_ARGS (calculated_pts));
      } else {
        DPRINT_ERROR("pts is invalid\n");
        flow_ret = GST_FLOW_ERROR;
        goto tag_decode_h265_exit;
      }
    }

    // Mark start of new frame
    ctx->mH265FrameStartPtr = ctx->mpBitStreamBufferCurPtr;

    // Copy AUD data to buffer
    if (!ctx->b_1st_frame) {
        ret = self->iav_ctx->iav_al.f_request_bsb(self->iav_ctx->iav_fd, decoder_id, size, ctx->mpBitStreamBufferCurPtr);
    }
    ctx->mpBitStreamBufferCurPtr = copy_data_to_bsb(self, decoder_id, ctx->mpBitStreamBufferCurPtr, p_data, size);
    //DPRINT_NOTICE("copy_data_to_bsb AUD: size: %d\n", size);

    flow_ret = GST_FLOW_OK;
    goto tag_decode_h265_exit;
  } else if (EHEVCNalType_PREFIX_SEI == first_nal_type) {
    if (!ctx->b_1st_frame) {
        ret = self->iav_ctx->iav_al.f_request_bsb(self->iav_ctx->iav_fd, decoder_id, size, ctx->mpBitStreamBufferCurPtr);
    }
    ctx->mpBitStreamBufferCurPtr = copy_data_to_bsb(self, decoder_id, ctx->mpBitStreamBufferCurPtr, p_data, size);
    //DPRINT_NOTICE("copy_data_to_bsb PREFIX_SEI: size: %d\n", size);

    flow_ret = GST_FLOW_OK;
    goto tag_decode_h265_exit;
  } else if (EHEVCNalType_AUD < first_nal_type) {
    // skip other high-numbered NAL types
    flow_ret = GST_FLOW_OK;
    goto tag_decode_h265_exit;
  } else if (EHEVCNalType_VPS == first_nal_type) {
    // reset addr and size
    ctx->p_cur_extradata = ctx->extradata_buf;
    ctx->extradata_size = 0;

    // record vps
    ctx->vps_size = size + nalu.sc_length;
    memcpy(ctx->p_cur_extradata, nalu.start_code, nalu.sc_length);
    memcpy(ctx->p_cur_extradata + nalu.sc_length, p_data, size);
    ctx->p_cur_extradata += ctx->vps_size;
    ctx->extradata_size += ctx->vps_size;

    if (!ctx->b_1st_frame) {
        ret = self->iav_ctx->iav_al.f_request_bsb(self->iav_ctx->iav_fd, decoder_id, size, ctx->mpBitStreamBufferCurPtr);
    }
    ctx->mpBitStreamBufferCurPtr = copy_data_to_bsb(self, decoder_id, ctx->mpBitStreamBufferCurPtr, p_data, size);
    //DPRINT_NOTICE("copy_data_to_bsb VPS: size: %d\n", size);

    flow_ret = GST_FLOW_OK;
    goto tag_decode_h265_exit;
  } else if (EHEVCNalType_SPS == first_nal_type) {
    // record sps
    ctx->sps_size = size + nalu.sc_length;
    memcpy(ctx->p_cur_extradata, nalu.start_code, nalu.sc_length);
    memcpy(ctx->p_cur_extradata + nalu.sc_length, p_data, size);
    ctx->p_cur_extradata += ctx->sps_size;
    ctx->extradata_size += ctx->sps_size;

    // Parse SPS to get actual video dimensions and create decoder
    if (ctx->b_1st_frame) {
      guint sps_width = 0, sps_height = 0;
      DPRINT_INFO("H265 SPS: original size=%u\n", size);
      DPRINT_INFO("H265 SPS first bytes: 0x%02x 0x%02x 0x%02x 0x%02x\n",
                  p_data[0], p_data[1], p_data[2], p_data[3]);

      // Find the actual NAL unit start by skipping start codes
      guchar *sps_data = p_data;
      guint sps_data_size = size;
      guint skip_bytes = 0;

      // Check for 4-byte start code: 0x00 0x00 0x00 0x01
      if (size >= 4 && p_data[0] == 0x00 && p_data[1] == 0x00 &&
          p_data[2] == 0x00 && p_data[3] == 0x01) {
        skip_bytes = 4;
        DPRINT_INFO("H265 SPS: Found 4-byte start code, skipping\n");
      }
      // Check for 3-byte start code: 0x00 0x00 0x01
      else if (size >= 3 && p_data[0] == 0x00 && p_data[1] == 0x00 && p_data[2] == 0x01) {
        skip_bytes = 3;
        DPRINT_INFO("H265 SPS: Found 3-byte start code, skipping\n");
      }
      // No start code, assume data starts with NAL unit
      else {
        skip_bytes = 0;
        DPRINT_INFO("H265 SPS: No start code found, assuming NAL unit starts immediately\n");
      }

      if (skip_bytes >= size) {
        DPRINT_WARNING("H265 SPS: Start code consumes entire buffer\n");
        return GST_FLOW_ERROR;
      }

      sps_data = p_data + skip_bytes;
      sps_data_size = size - skip_bytes;

      DPRINT_INFO("H265 SPS: after skipping %u bytes, size=%u\n", skip_bytes, sps_data_size);
      DPRINT_INFO("H265 SPS NAL bytes: 0x%02x 0x%02x 0x%02x 0x%02x\n",
                  sps_data[0], sps_data[1], sps_data[2], sps_data[3]);

      // Validate NAL unit type (should be 0x42 for H265 SPS, which is (0x42 >> 1) & 0x3F = 33)
      guchar nal_type = (sps_data[0] >> 1) & 0x3F;
      if (nal_type != 33) {  // H265 SPS NAL unit type
        DPRINT_WARNING("H265 SPS: Invalid NAL unit type: %u (expected 33)\n", nal_type);
        return GST_FLOW_ERROR;
      }

      // Skip the NAL header bytes (2 bytes for H265) to get to the actual SPS payload
      sps_data = sps_data + 2;
      sps_data_size = sps_data_size - 2;

      DPRINT_INFO("H265 SPS: final parsing data size=%u\n", sps_data_size);
      DPRINT_INFO("H265 SPS payload bytes: 0x%02x 0x%02x 0x%02x 0x%02x\n",
                  sps_data[0], sps_data[1], sps_data[2], sps_data[3]);

      gint parse_ret = get_h265_reso_from_sps(sps_data, sps_data_size, &sps_width, &sps_height);

      DPRINT_INFO("H265 SPS parsing: ret=%d, width=%u, height=%u\n", parse_ret, sps_width, sps_height);

      if (parse_ret == 0 && sps_width > 0 && sps_height > 0) {
        DPRINT_INFO("Parsed H265 SPS: %ux%u\n", sps_width, sps_height);

        gint create_ret = create_decoder_with_sps_dimensions(self, decoder_id, sps_width, sps_height);
        if (create_ret != 0) {
          DPRINT_ERROR("Failed to create decoder %d with SPS dimensions\n", decoder_id);
          flow_ret = GST_FLOW_ERROR;
          goto tag_decode_h265_exit;
        }
        ctx->width = sps_width;
        ctx->height = sps_height;
      } else {
        DPRINT_WARNING("Failed to parse H265 SPS dimensions (ret=%d, width=%u, height=%u), using max dimensions\n",
                      parse_ret, sps_width, sps_height);
        // Fallback to max dimensions if parsing fails
        gint create_ret = create_decoder_with_sps_dimensions(self, decoder_id,
            self->mCapMaxCodedWidth[decoder_id], self->mCapMaxCodedHeight[decoder_id]);
        if (create_ret != 0) {
          DPRINT_ERROR("Failed to create decoder with max dimensions\n");
          flow_ret = GST_FLOW_ERROR;
          goto tag_decode_h265_exit;
        }

        ctx->width = self->mCapMaxCodedWidth[decoder_id];
        ctx->height = self->mCapMaxCodedHeight[decoder_id];
      }
    } else {
        ret = self->iav_ctx->iav_al.f_request_bsb(self->iav_ctx->iav_fd, decoder_id, size, ctx->mpBitStreamBufferCurPtr);
    }

    ctx->mpBitStreamBufferCurPtr = copy_data_to_bsb(self, decoder_id, ctx->mpBitStreamBufferCurPtr, p_data, size);
    //DPRINT_NOTICE("copy_data_to_bsb SPS: size: %d\n", size);

    flow_ret = GST_FLOW_OK;
    goto tag_decode_h265_exit;
  } else if (EHEVCNalType_PPS == first_nal_type) {
    // record pps
    ctx->pps_size = size + nalu.sc_length;
    memcpy(ctx->p_cur_extradata, nalu.start_code, nalu.sc_length);
    memcpy(ctx->p_cur_extradata + nalu.sc_length, p_data, size);

    if (!ctx->b_1st_frame) {
        ret = self->iav_ctx->iav_al.f_request_bsb(self->iav_ctx->iav_fd, decoder_id, size, ctx->mpBitStreamBufferCurPtr);
    }
    ctx->mpBitStreamBufferCurPtr = copy_data_to_bsb(self, decoder_id, ctx->mpBitStreamBufferCurPtr, p_data, size);
    //DPRINT_NOTICE("copy_data_to_bsb PPS: size: %d\n", size);

    // cal total extra data size
    ctx->extradata_size += ctx->pps_size;
    flow_ret = GST_FLOW_OK;
    goto tag_decode_h265_exit;
  }

  /*if (((EHEVCNalType_IDR_W_RADL == first_nal_type) || (EHEVCNalType_IDR_N_LP == first_nal_type)) && is_first_slice) {
      b_append_extradata = 1;
  }*/

  if (is_first_slice && self->mbHEVCPerTile) {
      self->mTileIndex = 0;
  }

  if (ctx->mpBitStreamBufferCurPtr == ctx->mpBitSreamBufferEnd) {
      ctx->mpBitStreamBufferCurPtr = ctx->mpBitSreamBufferStart;
  }

  // If this is the first NAL unit of a frame, initialize frame tracking
  if (!ctx->mH265FrameStartPtr) {
    // Additional safety check
    if (!ctx->mpBitStreamBufferCurPtr || !ctx->mpBitSreamBufferStart || !ctx->mpBitSreamBufferEnd) {
      DPRINT_ERROR("Bitstream buffer not initialized for decoder %d, cannot process frame data\n", decoder_id);
      flow_ret = GST_FLOW_ERROR;
      goto tag_decode_h265_exit;
    }

    if (!self->mbDumpOnly) {
      ctx->mDecCmdCtx.decoder_id = decoder_id;
      ctx->mDecCmdCtx.num_frames = 1;
      if (!self->mbAutoMapBSB) {
        ctx->mDecCmdCtx.start_ptr_offset = (guint) (gulong) (ctx->mpBitStreamBufferCurPtr - ctx->mpBitSreamBufferStart);
      } else {
        ctx->mDecCmdCtx.start_ptr_offset = (guint) (gulong) (ctx->mpBitStreamBufferCurPtr);
      }
      ctx->mDecCmdCtx.first_frame_display = 0;

      ret = self->iav_ctx->iav_al.f_request_bsb(self->iav_ctx->iav_fd, decoder_id, size + 1024, ctx->mpBitStreamBufferCurPtr);
      if (DDECODER_STOPPED == ret) {
          flow_ret = GST_FLOW_OK;
          goto tag_decode_h265_exit;
      } else if (0 > ret) {
          DPRINT_ERROR("request bsb fail, return %d\n", ret);
          flow_ret = GST_FLOW_ERROR;
          goto tag_decode_h265_exit;
      }
    }

    // Use provided_pts which is set in handle_frame with priority: frame->pts > in_buf PTS > frame->dts
    if (ctx->provided_pts != GST_CLOCK_TIME_NONE) {
      ctx->mH265FramePTS = gst_util_uint64_scale (ctx->provided_pts, self->hwtimer_outfreq, GST_SECOND);
    } else {
      // generate PTS based on frame rate and frame count
      if (self->mFrameRateNum > 0 && self->mFrameRateDen > 0) {
        GstClockTime calculated_pts = gst_util_uint64_scale (ctx->mFrameCount,
            GST_SECOND * self->mFrameRateDen, self->mFrameRateNum);
        ctx->mH265FramePTS = gst_util_uint64_scale (calculated_pts, self->hwtimer_outfreq, GST_SECOND);
        GST_DEBUG_OBJECT (self, "Generated PTS from frame count: decoder=%d, frame=%u, pts=%" GST_TIME_FORMAT,
            decoder_id, ctx->mFrameCount, GST_TIME_ARGS (calculated_pts));
      } else {
        DPRINT_ERROR("pts is invalid\n");
        flow_ret = GST_FLOW_ERROR;
        goto tag_decode_h265_exit;
      }
    }

    // Mark start of frame
    ctx->mH265FrameStartPtr = ctx->mpBitStreamBufferCurPtr;
  }

  if (!ctx->mbGetBasePTS) {
      ctx->mbGetBasePTS = 1;
      ctx->mBasePTS = GST_BUFFER_PTS(in_buf);
  }

//  if (is_first_slice && self->mbAddAmbaGopHeader && ((EHEVCNalType_IDR_W_RADL == first_nal_type) || (EHEVCNalType_IDR_N_LP == first_nal_type))) {
//      gstUpdateAmbaH265GopHeader(ctx->mpAmbaGopHeader,
//          ctx->mH265FramePTS,
//          (guchar) self->mCurGopSize);
//      ctx->mpBitStreamBufferCurPtr = copy_data_to_bsb(self, decoder_id, ctx->mpBitStreamBufferCurPtr, ctx->mpAmbaGopHeader, DAMBA_H265_GOP_HEADER_LENGTH);
//      DPRINT_NOTICE("copy_data_to_bsb: DAMBA_H265_GOP_HEADER_LENGTH, mpAmbaGopHeader, : %d, %d\n", DAMBA_H265_GOP_HEADER_LENGTH, ctx->mpAmbaGopHeader);
//  }

  ctx->mpBitStreamBufferCurPtr = copy_data_to_bsb(self, decoder_id, ctx->mpBitStreamBufferCurPtr, p_data, size);
  //DPRINT_NOTICE("copy_data_to_bsb: nalu.type %d, size: %d\n", nalu.type, size);

tag_decode_h265_exit:

  gst_buffer_unmap (in_buf, &map);

  return flow_ret;
}

static GstFlowReturn decode_direct(GstAmbaHwvdec * self, guchar decoder_id, GstBuffer *in_buf)
{
  GstFlowReturn err = GST_FLOW_OK;

  switch (self->mCodecFormat[decoder_id]) {
    case StreamFormat_BYTE:
    case StreamFormat_H264:
    case StreamFormat_H264_BYTE:
      err = decodeH264(self, decoder_id, in_buf);
      break;

    case StreamFormat_H265:
    case StreamFormat_H265_BYTE:
      err = decodeH265(self, decoder_id, in_buf);
      break;

    default:
      DPRINT_ERROR("need add codec support %d for decoder %u\n", self->mCodecFormat[decoder_id], decoder_id);
      err = GST_FLOW_ERROR;
      break;
  }

  return err;
}

static void
gst_amba_hwvdec_finalize (GObject * object)
{
  GstAmbaHwvdec *self = GST_AMBAHWVDEC (object);

  if (self->codec_data) {
      free(self->codec_data);
      self->codec_data = NULL;
  }

  // Clean up decoder contexts
  for (int i = 0; i < DAMBADSP_MAX_DECODER_NUMBER; i++) {
    g_clear_pointer (&self->decoder_ctx[i].output_state, gst_video_codec_state_unref);
  }

  destroy_context(self);

  release_iav_ctx(1);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}


static void
gst_amba_hwvdec_reset (GstAmbaHwvdec * self)
{
  g_clear_pointer (&self->input_state, gst_video_codec_state_unref);
  g_clear_pointer (&self->output_state, gst_video_codec_state_unref);

  // Reset all decoder contexts
  for (int i = 0; i < DAMBADSP_MAX_DECODER_NUMBER; i++) {
    GstAmbaHwvdecDecoderCtx *ctx = &self->decoder_ctx[i];
    g_clear_pointer (&ctx->output_state, gst_video_codec_state_unref);
    ctx->mbStopCmdSent = 0;
    ctx->b_1st_frame = 1;
    ctx->is_first_frame = 1;
    ctx->b_dec_1_frame_done = 0;
    ctx->provided_pts = GST_CLOCK_TIME_NONE;
    ctx->mH265FrameStartPtr = NULL;
    ctx->mH265FramePTS = 0;
    ctx->mFrameCount = 0;
    ctx->mbGetBasePTS = 0;
    ctx->mBasePTS = 0;
    ctx->p_cur_extradata = ctx->extradata_buf;
    ctx->extradata_size = 0;
    ctx->vps_size = 0;
    ctx->sps_size = 0;
    ctx->pps_size = 0;
  }
}

static gboolean
gst_amba_hwvdec_start (GstVideoDecoder * decoder)
{
  GstAmbaHwvdec *self = GST_AMBAHWVDEC (decoder);

  gst_amba_hwvdec_reset (self);

  return TRUE;
}

static gboolean
gst_amba_hwvdec_stop (GstVideoDecoder * decoder)
{
  GstAmbaHwvdec *self = GST_AMBAHWVDEC (decoder);

  gst_amba_hwvdec_reset (self);

  // Stop all active decoders
  for (int i = 0; i < DAMBADSP_MAX_DECODER_NUMBER; i++) {
    if (self->is_decoder_created[i] && !self->decoder_ctx[i].mbStopCmdSent) {
      self->iav_ctx->iav_al.f_stop(self->iav_ctx->iav_fd, i, 1);
      self->decoder_ctx[i].mbStopCmdSent = 1;
      DPRINT_NOTICE("stop decoder %d done\n", i);
    }
  }
  self->mbSendDecodeReadyMsg = 0;

  return TRUE;
}

static gboolean
gst_amba_hwvdec_flush (GstVideoDecoder * decoder)
{
  GstAmbaHwvdec *self = GST_AMBAHWVDEC (decoder);

  if (self->iav_ctx) {
    // Flush all active decoders
    for (int i = 0; i < DAMBADSP_MAX_DECODER_NUMBER; i++) {
      if (self->is_decoder_created[i]) {
        if (G_UNLIKELY(self->iav_ctx->iav_al.f_stop(self->iav_ctx->iav_fd, i, 1) < 0)) {
          DPRINT_ERROR("fail to stop decoder %d\n", i);
          return FALSE;
        }
        DPRINT_NOTICE("stop decoder %d done\n", i);
        GstAmbaHwvdecDecoderCtx *ctx = &self->decoder_ctx[i];
        ctx->mpBitStreamBufferCurPtr = ctx->mpBitSreamBufferStart;
        ctx->b_1st_frame = 1;  // Reset decoder flag after stop

        if (G_UNLIKELY(self->iav_ctx->iav_al.f_start(self->iav_ctx->iav_fd, i) < 0)) {
          DPRINT_ERROR("fail to start decoder %d\n", i);
          return FALSE;
        }
        ctx->mbStopCmdSent = 0;
        DPRINT_NOTICE("start decoder %d done\n", i);
      }
    }
  }

  return TRUE;
}

static GstFlowReturn
gst_amba_hwvdec_drain (GstVideoDecoder * decoder)
{
  GstFlowReturn err = GST_FLOW_OK;
  DUNUSED(decoder);
#if 0
  GstAmbaHwvdec *self = GST_AMBAHWVDEC (decoder);

  // Send any accumulated H265 frame before drain
  if (self->mH265FrameStartPtr && self->mpBitStreamBufferCurPtr != self->mH265FrameStartPtr) {
    if (!self->mbDumpOnly) {
      gint ret;
      if (!self->mbAutoMapBSB) {
        self->mDecCmdCtx.end_ptr_offset = (guint) (gulong) (self->mpBitStreamBufferCurPtr - self->mpBitSreamBufferStart);
      } else {
        self->mDecCmdCtx.end_ptr_offset = (guint) (gulong) (self->mpBitStreamBufferCurPtr);
      }
      self->mDecCmdCtx.first_frame_display = self->mH265FramePTS;

      ret = self->iav_ctx->iav_al.f_request_bsb(self->iav_ctx->iav_fd, (guchar)self->mDecId, 1024, self->mpBitStreamBufferCurPtr);
      if (DDECODER_STOPPED == ret) {
        err = GST_FLOW_OK;
        goto drain_exit;
      } else if (0 > ret) {
        DPRINT_ERROR("request bsb fail in drain, return %d\n", ret);
        err = GST_FLOW_ERROR;
        goto drain_exit;
      }

      ret = self->iav_ctx->iav_al.f_decode(self->iav_ctx->iav_fd, &self->mDecCmdCtx);
      //DPRINT_NOTICE("f_decode drain last frame: ret: %d\n", ret);
      if (ret < 0 && ret != DDECODER_STOPPED) {
        DPRINT_ERROR("decode drain failed, return %d\n", ret);
        err = GST_FLOW_ERROR;
      }
    }
    // Reset frame tracking
    self->mH265FrameStartPtr = NULL;
    self->mH265FramePTS = 0;
  }

drain_exit:
#endif
  /* dpb will be cleared by this method */
  return err;
}

static GstFlowReturn
gst_amba_hwvdec_finish (GstVideoDecoder * decoder)
{
  GstAmbaHwvdec *self = GST_AMBAHWVDEC (decoder);
  guint8 avc_end_of_stream[] = {0x00, 0x00, 0x00, 0x01, ENalType_END_OF_STREAM};
  guint8 hevc_end_of_stream[] = {0x00, 0x00, 0x00, 0x01, (EHEVCNalType_EOB << 1), 0x00};
  amba_dsp_decode_eos_timestamp_t eos_timestamp = {0};
  int ret = 0;
  GstFlowReturn flow_ret = GST_FLOW_OK;
  guchar *frame_start_ptr = NULL;

  if (!self->mbDumpOnly) {
    // Finish all active decoders
    for (int i = 0; i < DAMBADSP_MAX_DECODER_NUMBER; i++) {
      if (!self->is_decoder_created[i]) {
        continue;
      }

      GstAmbaHwvdecDecoderCtx *ctx = &self->decoder_ctx[i];

      if (ctx->mpBitStreamBufferCurPtr == ctx->mpBitSreamBufferEnd) {
        ctx->mpBitStreamBufferCurPtr = ctx->mpBitSreamBufferStart;
      }

      ret = self->iav_ctx->iav_al.f_request_bsb(self->iav_ctx->iav_fd, i, 1024, ctx->mpBitStreamBufferCurPtr);
      if (DDECODER_STOPPED == ret) {
        continue;
      } else if (0 > ret) {
        GST_ERROR("request bsb fail for decoder %d, return %d\n", i, ret);
        flow_ret = GST_FLOW_ERROR;
        continue;
      }

      /* for h265, there should be a frame, already in the bsb buffer, to be sent to hw decoder.
       * sent the frame together with the eos marker.
       */
      if (StreamFormat_H264 == self->mCodecFormat[i]
          || StreamFormat_H264_BYTE == self->mCodecFormat[i]) {
        frame_start_ptr = ctx->mpBitStreamBufferCurPtr;
        ctx->mpBitStreamBufferCurPtr = copy_data_to_bsb(self, i, ctx->mpBitStreamBufferCurPtr, avc_end_of_stream, sizeof(avc_end_of_stream));
      } else if (StreamFormat_H265 == self->mCodecFormat[i]
          || StreamFormat_H265_BYTE == self->mCodecFormat[i]) {
        ctx->mpBitStreamBufferCurPtr = copy_data_to_bsb(self, i, ctx->mpBitStreamBufferCurPtr, hevc_end_of_stream, sizeof(hevc_end_of_stream));
        frame_start_ptr = ctx->mH265FrameStartPtr;
      }

      if (!self->mbAutoMapBSB) {
        ctx->mDecCmdCtx.start_ptr_offset = (guint) (gulong) (frame_start_ptr - ctx->mpBitSreamBufferStart);
        ctx->mDecCmdCtx.end_ptr_offset = (guint) (gulong) (ctx->mpBitStreamBufferCurPtr - ctx->mpBitSreamBufferStart);
      } else {
        ctx->mDecCmdCtx.start_ptr_offset = (guint) (gulong) (frame_start_ptr);
        ctx->mDecCmdCtx.end_ptr_offset = (guint) (gulong) (ctx->mpBitStreamBufferCurPtr);
      }

      ret = self->iav_ctx->iav_al.f_decode(self->iav_ctx->iav_fd, &ctx->mDecCmdCtx);
      if (ret < 0 && ret != DDECODER_STOPPED) {
        GST_ERROR("decode failed for decoder %d, return %d\n", i, ret);
        flow_ret = GST_FLOW_ERROR;
        continue;
      }

      eos_timestamp.decoder_id = i;
      ret = self->iav_ctx->iav_al.f_decode_wait_eos(self->iav_ctx->iav_fd, &eos_timestamp);
      if (ret < 0) {
        DPRINT_ERROR("decode wait eos failed for decoder %d, return %d\n", i, ret);
        flow_ret = GST_FLOW_ERROR;
      }
    }
  }

  guint total_frames = 0;
  for (int i = 0; i < DAMBADSP_MAX_DECODER_NUMBER; i++) {
    total_frames += self->decoder_ctx[i].mFrameCount;
  }
  GST_INFO_OBJECT (self, "Total decoded frames: %u", total_frames);

  flow_ret = gst_amba_hwvdec_drain (decoder);
  return flow_ret;
}


static void
gst_amba_hw_vdec_format_from_caps (GstAmbaHwvdec * self, GstCaps * caps,
    StreamFormat * format)
{
  if (format) {
    *format = StreamFormat_Invalid;
  }

  if (!gst_caps_is_fixed (caps)) {
    GST_WARNING_OBJECT (self, "Caps wasn't fixed");
    return;
  }

  GST_DEBUG_OBJECT (self, "parsing caps: %" GST_PTR_FORMAT, caps);

  if (caps && gst_caps_get_size (caps) > 0) {
    GstStructure *s = gst_caps_get_structure (caps, 0);
    const gchar *str = NULL;

    if ((str = gst_structure_get_string (s, "stream-format"))) {
      if (strcmp (str, "avc") == 0 || strcmp (str, "avc3") == 0) {
        *format = StreamFormat_H264;
        DPRINT_NOTICE("h264: size+payload\n");
      } else if (strcmp (str, "hvc1") == 0 || strcmp (str, "hev1") == 0) {
        *format = StreamFormat_H265;
        DPRINT_NOTICE("h265: size+payload\n");
      } else if (strcmp (str, "byte-stream") == 0) {
        *format = StreamFormat_BYTE;
        DPRINT_NOTICE("byte stream\n");
      } else {
        DPRINT_ERROR("not supported stream format: %s\n", str);
        *format = StreamFormat_H264;
      }
    }

    if (gst_structure_has_name(s, "video/x-h264")
      && *format == StreamFormat_BYTE) {
      *format = StreamFormat_H264_BYTE;
      DPRINT_NOTICE("h264: byte stream\n");
    } else if (gst_structure_has_name(s, "video/x-h265")
      && *format == StreamFormat_BYTE) {
      *format = StreamFormat_H265_BYTE;
      DPRINT_NOTICE("h265: byte stream\n");
    }

  }
}

static gboolean
gst_amba_hwvdec_set_format (GstVideoDecoder * decoder,
  GstVideoCodecState * state)
{
  GstAmbaHwvdec *self = GST_AMBAHWVDEC (decoder);
  GstQuery *query;

  GST_INFO_OBJECT (decoder, "Set format for decoder_id=%u", self->mDecId);
  GST_INFO_OBJECT (decoder, "Set format called, mDecId=%u", self->mDecId);

  if (self->input_state) {
    gst_video_codec_state_unref (self->input_state);
    self->input_state = NULL;
  }

  self->input_state = gst_video_codec_state_ref (state);

  /* in case live streaming, we will run on low-latency mode */
  self->is_live = FALSE;
  query = gst_query_new_latency ();
  if (gst_pad_peer_query (GST_VIDEO_DECODER_SINK_PAD (self), query))
    gst_query_parse_latency (query, &self->is_live, NULL, NULL);
  gst_query_unref (query);

  if (self->is_live)
    GST_DEBUG_OBJECT (self, "Live source, will run on low-latency mode");

  if (state->caps) {
    StreamFormat format;

    gst_amba_hw_vdec_format_from_caps (self, state->caps, &format);
    DPRINT_NOTICE("format 0x%02x for decoder_id=%u\n", format, self->mDecId);

    if (gst_caps_is_fixed (state->caps) && gst_caps_get_size (state->caps) > 0) {
      GstStructure *s = gst_caps_get_structure (state->caps, 0);
      const GValue *fps_value = gst_structure_get_value (s, "framerate");

      if (fps_value && GST_VALUE_HOLDS_FRACTION (fps_value)) {
        gint fps_num = gst_value_get_fraction_numerator (fps_value);
        gint fps_den = gst_value_get_fraction_denominator (fps_value);

        if (fps_num > 0 && fps_den > 0) {
          self->mFrameRateNum = fps_num;
          self->mFrameRateDen = fps_den;
          self->mSpecifiedTimeScale = fps_num;
          self->mSpecifiedFrameTick = fps_den;
        }
      }
    }

    // Also check if input_state->info has framerate set
    if (GST_VIDEO_INFO_FPS_N (&state->info) > 0 && GST_VIDEO_INFO_FPS_D (&state->info) > 0) {
      if (self->mFrameRateNum == 0 || self->mFrameRateDen == 0) {
        self->mFrameRateNum = GST_VIDEO_INFO_FPS_N (&state->info);
        self->mFrameRateDen = GST_VIDEO_INFO_FPS_D (&state->info);
        self->mSpecifiedTimeScale = self->mFrameRateNum;
        self->mSpecifiedFrameTick = self->mFrameRateDen;
      }
    }

    if (format == StreamFormat_Invalid) {
      /* codec_data implies avc */
      if (state->codec_data) {
        GST_WARNING_OBJECT (self,
            "video/x-h264 caps with codec_data but no stream-format=avc");
        format = StreamFormat_H264;
      } else {
        /* otherwise assume bytestream input */
        GST_WARNING_OBJECT (self,
            "video/x-h264 caps without codec_data or stream-format");
        format = StreamFormat_BYTE;
      }
    } else if (format == StreamFormat_H264) {
      /* AVC requires codec_data, AVC3 might have one and/or SPS/PPS inline */
      if (!state->codec_data) {
        /* Try it with size 4 anyway */
        self->nal_length_size = 4;
        GST_WARNING_OBJECT (self,
            "avc format without codec data, assuming nal length size is 4");
      }

    } else if (format == StreamFormat_H265) {
      if (!state->codec_data) {
        /* Try it with size 4 anyway */
        self->nal_length_size = 4;
        GST_WARNING_OBJECT (self,
            "packetized format without codec data, assuming nal length size is 4");
      }
    } else if (format == StreamFormat_BYTE
      || format == StreamFormat_H264_BYTE
      || format == StreamFormat_H265_BYTE) {
      if (state->codec_data) {
        GST_WARNING_OBJECT (self, "bytestream with codec data");
      }
    }

    self->mCodecFormat[self->mDecId] = format;
    DPRINT_NOTICE("Set codec format 0x%02x for decoder %u\n", format, self->mDecId);
  }

  if (state->codec_data) {
    GstMapInfo map;

    gst_buffer_map (state->codec_data, &map, GST_MAP_READ);
    //gst_h264_decoder_parse_codec_data
    if (set_codec_data (self, map.data, map.size) != 0) {
      /* keep going without error.
       * Probably inband SPS/PPS might be valid data */
      GST_WARNING_OBJECT (self, "Failed to handle codec data");
    }
    gst_buffer_unmap (state->codec_data, &map);
  }

  if (self->b_setup_ctx_done == 0) {
    setup_context(self);
  }

  return TRUE;
}

static GstFlowReturn
gst_amba_hwvdec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame)
{
  GstAmbaHwvdec *self = GST_AMBAHWVDEC (decoder);
  GstBuffer *in_buf = frame->input_buffer;
  GstFlowReturn ret = GST_FLOW_OK;
  guchar decoder_id = self->mDecId;
  GstAmbaHwvdecDecoderCtx *ctx = &self->decoder_ctx[decoder_id];

  GST_LOG_OBJECT (decoder, "Submitting frame with PTS %" GST_TIME_FORMAT
      " and frame ref %" G_GUINT64_FORMAT,
      GST_TIME_ARGS (frame->pts), (guint64) frame->system_frame_number);

  if (frame->pts != GST_CLOCK_TIME_NONE) {
    ctx->provided_pts = frame->pts;
  } else if (GST_BUFFER_PTS_IS_VALID (in_buf)) {
    ctx->provided_pts = GST_BUFFER_PTS (in_buf);
  } else if (frame->dts != GST_CLOCK_TIME_NONE) {
    ctx->provided_pts = frame->dts;
  } else {
    ctx->provided_pts = GST_CLOCK_TIME_NONE;
  }

  ret = decode_direct(self, decoder_id, in_buf);

  if (ret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (decoder, "decode_direct failed.");
    gst_video_codec_frame_unref (frame);
    frame = NULL;
    return ret;
  }

  // Check output state for current decoder
  if (G_UNLIKELY (ctx->output_state == NULL
      || ctx->width != (guint) ctx->output_state->info.width
      || ctx->height != (guint) ctx->output_state->info.height)) {
    if (!ctx->width || !ctx->height) {
      ctx->width = self->mCapMaxCodedWidth[decoder_id];
      ctx->height = self->mCapMaxCodedHeight[decoder_id];
    }
    GstVideoCodecState *state =
        gst_video_decoder_set_output_state (decoder, GST_VIDEO_FORMAT_NV12,
        ctx->width, ctx->height, self->input_state);
    if (state == NULL) {
      GST_ERROR_OBJECT (self, "Failed to set output state for decoder %d", decoder_id);
      gst_video_codec_frame_unref (frame);
      frame = NULL;
      return GST_FLOW_ERROR;
    }

    // Set framerate to ensure proper timestamp synchronization
    // This is critical for stable playback even when frames are dropped
    if (self->mFrameRateNum && self->mFrameRateDen) {
      state->info.fps_n = self->mFrameRateNum;
      state->info.fps_d = self->mFrameRateDen;
    } else if (self->input_state && GST_VIDEO_INFO_FPS_N (&self->input_state->info) > 0) {
      // Use input framerate if available
      state->info.fps_n = GST_VIDEO_INFO_FPS_N (&self->input_state->info);
      state->info.fps_d = GST_VIDEO_INFO_FPS_D (&self->input_state->info);
    }

    if (!gst_video_decoder_negotiate (decoder)) {
      GST_ERROR_OBJECT (self,
          "Failed to negotiate with downstream elements");
      gst_video_codec_state_unref (state);
      gst_video_codec_frame_unref (frame);
      frame = NULL;
      return GST_FLOW_NOT_NEGOTIATED;
    }
    if (ctx->output_state != NULL) {
      gst_video_codec_state_unref (ctx->output_state);
    }
    ctx->output_state = state;
    // Also update legacy output_state for backward compatibility
    if (self->output_state != NULL) {
      gst_video_codec_state_unref (self->output_state);
    }
    self->output_state = gst_video_codec_state_ref(state);
  }

  // Use finish_frame only for first frame
  // First frame: complete preroll and allow pipeline to enter PLAYING state
  // Other frames: use drop_frame to save memory
  if (ctx->b_dec_1_frame_done) {
    if (ctx->is_first_frame) {
      GstBuffer *out_buf = gst_buffer_new_allocate (NULL, 1, NULL);
      if (out_buf == NULL) {
        GST_ERROR_OBJECT (self, "Failed to allocate output buffer");
        gst_video_codec_frame_unref (frame);
        frame = NULL;
        return GST_FLOW_ERROR;
      }

      // Set timestamp from frame to pass to downstream
      if (ctx->provided_pts != GST_CLOCK_TIME_NONE) {
        GST_BUFFER_PTS (out_buf) = ctx->provided_pts;
      } else {
        GST_WARNING_OBJECT (self, "No valid PTS available for first frame, using 0");
        GST_BUFFER_PTS (out_buf) = 0;
      }

      if (frame->dts != GST_CLOCK_TIME_NONE) {
        GST_BUFFER_DTS (out_buf) = frame->dts;
      }
      if (frame->duration != GST_CLOCK_TIME_NONE) {
        GST_BUFFER_DURATION (out_buf) = frame->duration;
      }

      frame->output_buffer = out_buf;
      ret = gst_video_decoder_finish_frame (decoder, frame);
      frame = NULL;
      if (ret != GST_FLOW_OK) {
        GST_ERROR_OBJECT (self,
            "Failed to finish frame: ret=%d", ret);
        return ret;
      }

      ctx->is_first_frame = 0;

    } else {
      // Intermediate frames: use drop_frame to save memory
      // Pipeline is already in PLAYING state, so drop_frame won't block state transition
      ret = gst_video_decoder_drop_frame (decoder, frame);
      frame = NULL;
      if (ret != GST_FLOW_OK) {
        GST_ERROR_OBJECT (self,
            "Failed to drop frame: ret=%d", ret);
        return ret;
      }
    }
    ctx->b_dec_1_frame_done = 0;
    ctx->mFrameCount++;
  } else {
    // Before decode a complete frame, just unref (no timestamp to pass yet)
    gst_video_codec_frame_unref (frame);
    frame = NULL;
    ret = GST_FLOW_OK;
  }

  return ret;
}
