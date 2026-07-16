/*
 * gstambavprocimgcvt.c
 *
 * History:
 *    8/4/2025 - [Cheng Chen] created file
 *
 * Copyright (C) 2025 Ambarella International LP
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION: element-amba_img_cvt
 * @title: amba_img_cvt
 *
 * amba_img_cvt can be used to do YUV420 Planar image transform YUV420 image with Amba VProc HW.
 *
 * ## Example pipeline
 * [[
 * gst-launch-1.0 -e -v filesrc location=I420 blocksize=3110400 ! "video/x-raw, format=I420,width=1920,height=1080,framerate=1/1" ! amba_img_cvt !
 filesink location=/tmp/NV12
 * ]]
 *  Reads YUV420 Planar images transform to NV12 with Amba Vproc.
 */

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <gst/video/video.h>
#include <gst/gst.h>
#include <gst/gstmemory.h>
#include "debug_log.h"
#include "gstambavprocimgcvt.h"
#include "platform_al.h"
#include "internal.h"
#include "amba_private_data.h"
#include "common_err_code_c.h"
#include "cavalry_mem.h"
#include "vproc.h"
#include "gst_amba_pitch_align.h"
#include "gst_amba_cavalry_allocator.h"

enum {
  PROP_0,
  PROP_ZERO_COPY,
};

GST_DEBUG_CATEGORY_STATIC (gst_amba_img_cvt_debug);
#define GST_CAT_DEFAULT gst_amba_img_cvt_debug

#define gst_amba_vproc_imcvt_parent_class parent_class


G_DEFINE_TYPE (GstAmbaVprocImcvt, gst_amba_vproc_imcvt, GST_TYPE_BASE_TRANSFORM);

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (
      GST_VIDEO_CAPS_MAKE ("{I420, NV12}"))
);

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (
      GST_VIDEO_CAPS_MAKE ("{NV12, RGBP}") ","
        "colorimetry = (string) { bt601, bt709, bt2020 },"
        "range = (string) { full, limited }")
);

static yuv2rgb_mat_t yuv2rgb_mat_bt601_full = {
    .yc = 1,
    .rv = 1.402,
    .gu = 0.344136,
    .gv = 0.714136,
    .bu = 1.772,
    .yb = 0,
};

static yuv2rgb_mat_t yuv2rgb_mat_bt709_full = {
    .yc = 1,
    .rv = 1.5748,
    .gu = 0.187328,
    .gv = 0.468124,
    .bu = 1.8556,
    .yb = 0,
};

static yuv2rgb_mat_t yuv2rgb_mat_bt2020_full = {
    .yc = 1,
    .rv = 1.4746,
    .gu = 0.1768,
    .gv = 0.4556,
    .bu = 1.8814,
    .yb = 0,
};

static yuv2rgb_mat_t yuv2rgb_mat_bt601_limited = {
    .yc = 1.0f,
    .rv = 1.402f,
    .gu = 0.344136f,
    .gv = 0.714136f,
    .bu = 1.772f,
    .yb = 16.0f / 255.0f,
};

static yuv2rgb_mat_t yuv2rgb_mat_bt709_limited = {
    .yc = 1.0f,
    .rv = 1.5748f,
    .gu = 0.187328f,
    .gv = 0.468124f,
    .bu = 1.8556f,
    .yb = 16.0f/255.0f,
};

static yuv2rgb_mat_t yuv2rgb_mat_bt2020_limited = {
    .yc = 1.0f,
    .rv = 1.4746f,
    .gu = 0.1768f,
    .gv = 0.4556f,
    .bu = 1.8814f,
    .yb = 16.0f/255.0f,
};

#define LPDDR4_ALIGN  (64)
/* Row bytes: multiple of lcm(IAV_DSP_BUF_PITCH_ALIGN, CAVALRY_PORT_PITCH_ALIGN) */
#define ALIGN_PITCH(x)                (AMBA_ALIGN_PITCH_DSP_AND_CAVALRY (x))
#define IMG_CVT_PITCH_ALIGN           ((guint) gst_amba_pitch_lcm_dsp_and_cavalry_step ())

static void gst_amba_vproc_imcvt_finalize (GObject *gobject);
static void gst_amba_vproc_imcvt_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_amba_vproc_imcvt_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstCaps *gst_amba_vproc_imcvt_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static gboolean gst_amba_vproc_imcvt_set_caps (GstBaseTransform *trans,
    GstCaps *incaps, GstCaps *outcaps);
static gboolean gst_amba_vproc_imcvt_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query);
static gboolean gst_amba_vproc_imcvt_decide_allocation (GstBaseTransform * trans, GstQuery * query);
static GstFlowReturn gst_amba_vproc_imcvt_transform(GstBaseTransform *trans,
    GstBuffer *inbuf, GstBuffer *outbuf);
static GstFlowReturn gst_amba_vproc_imcvt_prepare_output_buffer (GstBaseTransform * base,
    GstBuffer * inbuf, GstBuffer ** outbuf);

/* initialize the amba_vproc_imcvt class */
static void gst_amba_vproc_imcvt_class_init (GstAmbaVprocImcvtClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstBaseTransformClass *gsttrans_class = (GstBaseTransformClass *) klass;

  GST_DEBUG_CATEGORY_INIT (gst_amba_img_cvt_debug, "amba_img_cvt", 0,
      "Amba img cvt");

  gobject_class->finalize = gst_amba_vproc_imcvt_finalize;
  gobject_class->set_property = gst_amba_vproc_imcvt_set_property;
  gobject_class->get_property = gst_amba_vproc_imcvt_get_property;

  g_object_class_install_property (gobject_class, PROP_ZERO_COPY,
      g_param_spec_boolean ("zero-copy", "Zero Copy",
          "Use zero-copy mode when possible (TRUE) or always allocate internal buffers (FALSE)",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (gstelement_class,
    "Vproc Image Convert",
    "Filter/Image Convert",
    "Reads YUV420 Planar images transform to NV12 with Amba platform",
    "Cheng Chen <cchen@ambarella.com>");

  gst_element_class_add_static_pad_template (gstelement_class, &sink_template);
  gst_element_class_add_static_pad_template (gstelement_class, &src_template);

  gsttrans_class->transform = GST_DEBUG_FUNCPTR (gst_amba_vproc_imcvt_transform);
  gsttrans_class->prepare_output_buffer = GST_DEBUG_FUNCPTR (gst_amba_vproc_imcvt_prepare_output_buffer);
  gsttrans_class->transform_caps = GST_DEBUG_FUNCPTR (gst_amba_vproc_imcvt_transform_caps);
  gsttrans_class->set_caps = GST_DEBUG_FUNCPTR (gst_amba_vproc_imcvt_set_caps);
  gsttrans_class->propose_allocation = GST_DEBUG_FUNCPTR (gst_amba_vproc_imcvt_propose_allocation);
  gsttrans_class->decide_allocation = GST_DEBUG_FUNCPTR (gst_amba_vproc_imcvt_decide_allocation);

  return;
}

static void gst_amba_vproc_imcvt_init (GstAmbaVprocImcvt *thiz)
{
  thiz->cv_ctx = acquire_cv_vproc_ctx (1, 1);
  thiz->iav_ctx = acquire_iav_ctx (1);

  gst_amba_cavalry_allocator_init_once();
  thiz->cavalry_allocator = gst_amba_cavalry_allocator_get();

  thiz->ic = 2;
  thiz->oc = 2;

  thiz->iw = 0;
  thiz->ih = 0;
  thiz->ow = 0;
  thiz->oh = 0;

  thiz->ip = 0;
  thiz->op = 0;

  thiz->zero_copy = TRUE;
  thiz->logged_input_path_once = FALSE;

  thiz->load_vect_data = NULL;
  thiz->load_vect_data_sz = 0;
  thiz->load_vect_data_inited = FALSE;

  return;
}

static void
gst_amba_vproc_imcvt_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAmbaVprocImcvt *self = GST_AMBA_VPROC_IMCVT (object);

  switch (prop_id) {
    case PROP_ZERO_COPY:
      self->zero_copy = g_value_get_boolean (value);
      self->logged_input_path_once = FALSE;
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_amba_vproc_imcvt_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAmbaVprocImcvt *self = GST_AMBA_VPROC_IMCVT (object);

  switch (prop_id) {
    case PROP_ZERO_COPY:
      g_value_set_boolean (value, self->zero_copy);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* start -> set_caps */
/* set_property -> set_caps */
static gboolean gst_amba_vproc_imcvt_set_caps (GstBaseTransform *trans,
    GstCaps *incaps, GstCaps *outcaps)
{
  GstAmbaVprocImcvt *self = GST_AMBA_VPROC_IMCVT(trans);
  GstStructure *in_structure = gst_caps_get_structure (incaps, 0);
  GstStructure *out_structure = gst_caps_get_structure (outcaps, 0);
  const gchar *in_format = NULL, *out_format = NULL, *colorimetry = NULL, *range = NULL;

  self->incaps = gst_caps_copy(incaps);

  in_format = gst_structure_get_string (in_structure, "format");
  if (!in_format || (g_strcmp0 (in_format, "I420") != 0 && g_strcmp0 (in_format, "NV12") != 0)) {
    GST_ERROR_OBJECT (trans, "Input format must be I420/NV12");
    return FALSE;
  }
  colorimetry = gst_structure_get_string(out_structure, "colorimetry");
  if (!colorimetry) {
    colorimetry = "bt601";
  }

  range = gst_structure_get_string(out_structure, "range");
  if (!range) {
    range = "full";
  }

  self->colorimetry = g_strdup(colorimetry);
  self->range = g_strdup(range);

  gst_structure_get_int (in_structure, "width", &self->iw);
  gst_structure_get_int (in_structure, "height", &self->ih);

  out_format = gst_structure_get_string (out_structure, "format");
  self->iw = self->ow = self->iw >> 1;
  self->ih = self->oh = self->ih >> 1;
  self->ip = ALIGN_PITCH(self->iw);
  self->op = ALIGN_PITCH(self->oc * self->ow);

  if (!g_strcmp0 (in_format, "I420") && !g_strcmp0 (out_format, "NV12")) {
    self->format_cvt_type = IYUV_TO_NV12;
  } else if (!g_strcmp0 (in_format, "NV12") && !g_strcmp0 (out_format, "RGBP")) {
    self->format_cvt_type = NV12_TO_RGBP;
  } else if (!g_strcmp0 (in_format, "I420") && !g_strcmp0 (out_format, "RGBP")) {
    self->format_cvt_type = IYUV_TO_RGBP;
  } else {
    GST_ERROR_OBJECT (trans, "Invalid format conversion");
    return FALSE;
  }

  self->logged_input_path_once = FALSE;

  return TRUE;
}

static GstCaps *gst_amba_vproc_imcvt_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstAmbaVprocImcvt *self = GST_AMBA_VPROC_IMCVT(trans);
  GstCaps *result = NULL;
  GstStructure *s;
  guint i = 0;
  const gchar *in_fmt = NULL;
  const gchar *in_colorimetry = NULL;
  const gchar *in_range = NULL;
  gint width, height, framerate_n, framerate_d;

  for (i = 0; i < gst_caps_get_size(caps); i++) {
    s = gst_caps_get_structure(caps, i);
    if (!gst_structure_has_name(s, "video/x-raw")) {
      continue;
    }

    in_fmt = gst_structure_get_string(s, "format");
    in_colorimetry = gst_structure_get_string(s, "colorimetry");
    in_range = gst_structure_get_string(s, "range");
    if (!in_fmt) {
      continue;
    }
    gst_structure_get_int (s, "width", &width);
    gst_structure_get_int (s, "height", &height);
    gst_structure_get_fraction(s, "framerate", &framerate_n, &framerate_d);

    //in_colorimetry = in_colorimetry ? in_colorimetry : "bt601";
    //in_range = in_range ? in_range : "full";

    if (direction == GST_PAD_SINK) {
      if (g_strcmp0(in_fmt, "I420") == 0) {
        result = gst_caps_from_string(g_strdup_printf(
          "video/x-raw, format=(string){NV12,RGBP}, "
          "width=(int)%d, height=(int)%d, "
          "framerate=(fraction)%d/%d, "
          "colorimetry=(string){bt601,bt709}, range=(string){full,limited}",
          width, height, framerate_n, framerate_d));
      } else if (g_strcmp0(in_fmt, "NV12") == 0) {
        result = gst_caps_from_string(g_strdup_printf(
          "video/x-raw, format=(string)RGBP, "
          "width=(int)%d, height=(int)%d, "
          "framerate=(fraction)%d/%d, "
          "colorimetry=(string){bt601,bt709}, range=(string){full,limited}",
          width, height, framerate_n, framerate_d));
      }
    } else {
      if (g_strcmp0(in_fmt, "NV12") == 0) {
        result = gst_caps_new_simple("video/x-raw",
          "format", G_TYPE_STRING, "I420",
          "width", G_TYPE_INT, width,
          "height", G_TYPE_INT, height,
          "framerate", GST_TYPE_FRACTION, framerate_n, framerate_d,
          NULL);
      } else if (g_strcmp0(in_fmt, "RGBP") == 0) {
        result = gst_caps_from_string(g_strdup_printf(
          "video/x-raw, format=(string){I420,NV12}, "
          "width=(int)%d, height=(int)%d, "
          "framerate=(fraction)%d/%d",
          width, height, framerate_n, framerate_d));
      }
    }

    if (result)
      break;
  }

  if (!result) {
    result = (direction == GST_PAD_SINK) ?
      gst_static_pad_template_get_caps(&src_template) :
      gst_static_pad_template_get_caps(&sink_template);
  }

  if (filter && result) {
    GstCaps *tmp = result;
    result = gst_caps_intersect(tmp, filter);
    gst_caps_unref(tmp);
  }

  GST_DEBUG_OBJECT(self,
    "Direction: %s, Input format: %s, Colorimetry: %s, Range: %s, Result caps: %" GST_PTR_FORMAT,
    (direction == GST_PAD_SINK) ? "sink" : "src",
    in_fmt ? in_fmt : "null",
    in_colorimetry ? in_colorimetry : "null",
    in_range ? in_range : "null",
    result);

  return result;
}

static GstFlowReturn gst_amba_vproc_imcvt_prepare_output_buffer (GstBaseTransform * base,
    GstBuffer * inbuf, GstBuffer ** outbuf)
{
  GstAmbaVprocImcvt *self = GST_AMBA_VPROC_IMCVT(base);
  gint yuv_width = self->iw << 1;
  gint yuv_height = self->ih << 1;
  gsize size = 0;

  if (self->format_cvt_type == IYUV_TO_NV12) {
    size = AMBA_ROUND_GSIZE_ODD_DSP_PITCH_UNITS ((gsize) ALIGN_PITCH (yuv_width) * yuv_height * 3 / 2);
  } else if ((self->format_cvt_type == NV12_TO_RGBP) || (self->format_cvt_type == IYUV_TO_RGBP)) {
    size = AMBA_ROUND_GSIZE_ODD_DSP_PITCH_UNITS ((gsize) yuv_height * ALIGN_PITCH (yuv_width) * 3);
  } else {
    /* do nothing */
  }

  *outbuf = gst_buffer_new_allocate (self->cavalry_allocator, size, NULL);
  if (!*outbuf) {
    GST_ERROR_OBJECT (self, "Failed to allocate output buffer (size=%" G_GSIZE_FORMAT ")", size);
    return GST_FLOW_ERROR;
  }

  /* GstVideoMeta: actual pitch may be ALIGN_PITCH(width); caps alone often lack stride and
   * downstream (e.g. mlinference2) would otherwise use GstVideoInfo defaults and misplace
   * G/B planes for RGBP. */
  if (self->format_cvt_type == IYUV_TO_NV12) {
    gint yuv_pitch = ALIGN_PITCH (yuv_width);
    gsize offset[4] = { 0, };
    gint stride[4] = { 0, };

    offset[0] = 0;
    offset[1] = (gsize) yuv_pitch * yuv_height;
    stride[0] = yuv_pitch;
    stride[1] = yuv_pitch;
    gst_buffer_add_video_meta_full (*outbuf, GST_VIDEO_FRAME_FLAG_NONE,
        GST_VIDEO_FORMAT_NV12, yuv_width, yuv_height, 2, offset, stride);
  } else if ((self->format_cvt_type == NV12_TO_RGBP) ||
      (self->format_cvt_type == IYUV_TO_RGBP)) {
    gint yuv_pitch = ALIGN_PITCH (yuv_width);
    gsize offset[4] = { 0, };
    gint stride[4] = { 0, };

    offset[0] = 0;
    offset[1] = (gsize) yuv_pitch * yuv_height;
    offset[2] = 2 * (gsize) yuv_pitch * yuv_height;
    stride[0] = yuv_pitch;
    stride[1] = yuv_pitch;
    stride[2] = yuv_pitch;
    gst_buffer_add_video_meta_full (*outbuf, GST_VIDEO_FRAME_FLAG_NONE,
        GST_VIDEO_FORMAT_RGBP, yuv_width, yuv_height, 3, offset, stride);
  }

  // Copy metadata from input buffer to output buffer
  if (inbuf && *outbuf) {
    // Copy PTS, DTS, duration
    if (GST_BUFFER_PTS_IS_VALID(inbuf)) {
      GST_BUFFER_PTS(*outbuf) = GST_BUFFER_PTS(inbuf);
    }
    if (GST_BUFFER_DTS_IS_VALID(inbuf)) {
      GST_BUFFER_DTS(*outbuf) = GST_BUFFER_DTS(inbuf);
    }
    if (GST_BUFFER_DURATION_IS_VALID(inbuf)) {
      GST_BUFFER_DURATION(*outbuf) = GST_BUFFER_DURATION(inbuf);
    }
    // Copy offset and offset_end if available
    if (GST_BUFFER_OFFSET_IS_VALID(inbuf)) {
      GST_BUFFER_OFFSET(*outbuf) = GST_BUFFER_OFFSET(inbuf);
    }
    if (GST_BUFFER_OFFSET_END_IS_VALID(inbuf)) {
      GST_BUFFER_OFFSET_END(*outbuf) = GST_BUFFER_OFFSET_END(inbuf);
    }
    // Copy buffer flags
    GST_BUFFER_FLAGS(*outbuf) = GST_BUFFER_FLAGS(inbuf);
  }

  return GST_FLOW_OK;
}

static gboolean gst_amba_vproc_imcvt_propose_allocation (GstBaseTransform * trans,
  GstQuery * decide_query, GstQuery * query)
{
#define BUF_POOL_ITEM_NUM            (2)

  GstAmbaVprocImcvt *self = GST_AMBA_VPROC_IMCVT(trans);
  GstAllocator *allocator = NULL;
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocationParams params;
  GstVideoAlignment align;
  gint yuv_width = self->iw << 1;
  gint yuv_height = self->ih << 1;
  gint yuv_pitch = ALIGN_PITCH(yuv_width);
  guint size, min, max;
  gboolean ret = FALSE;

  if (decide_query && gst_query_get_n_allocation_params (decide_query) > 0) {
    gst_query_parse_nth_allocation_param (decide_query, 0, &allocator, &params);
    if (!allocator) {
      GST_ERROR_OBJECT (self, "Failed to parse allocator!\n");
      ret = FALSE;
      goto GST_AMBA_VPROC_IMCVT_PROPOSE_ALLOCATION_EXIT;
	}
  } else {
    /* If no allocator proposed by downstream, use cavalry allocator */
    allocator = gst_amba_cavalry_allocator_get ();
    if (!allocator) {
      GST_ERROR_OBJECT (self, "Failed to get cavalry allocator!\n");
      ret = FALSE;
      goto GST_AMBA_VPROC_IMCVT_PROPOSE_ALLOCATION_EXIT;
    }
    gst_allocation_params_init (&params);
  }

  if (gst_query_get_n_allocation_params (query) <= 0) {
    gst_query_add_allocation_param (query, allocator, &params);
  } else {
    GST_DEBUG_OBJECT (self, "query already has allocator, skip proposing cavalry allocator!\n");
  }

  if (decide_query) {
    if (gst_query_get_n_allocation_pools (query) > 0) {
      gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
      if (pool) {
        GST_DEBUG_OBJECT (self, "query already has pool, skip creating the pool\n");
        gst_object_unref (pool);
        pool = NULL;
      }
    } else {
      pool = gst_buffer_pool_new ();
      min = BUF_POOL_ITEM_NUM;
      max = 0;

      if ((self->format_cvt_type == IYUV_TO_NV12) || (self->format_cvt_type == IYUV_TO_RGBP)) {
        gsize raw = (gsize) yuv_width * yuv_height + 2 * (gsize) self->ip * self->ih;
        size = (guint) AMBA_ROUND_GSIZE_ODD_DSP_PITCH_UNITS (raw);
      } else if (self->format_cvt_type == NV12_TO_RGBP) {
        gsize raw = (gsize) yuv_pitch * yuv_height * 3 / 2;
        size = (guint) AMBA_ROUND_GSIZE_ODD_DSP_PITCH_UNITS (raw);
      } else {
        /* do nothing */
      }
      config = gst_buffer_pool_get_config (pool);
      gst_buffer_pool_config_set_params (config, self->incaps, size, min, max);
      gst_buffer_pool_config_set_allocator (config, allocator, &params);
      gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

      /* Set video alignment in config */
      gst_video_alignment_reset (&align);
      if ((self->format_cvt_type == IYUV_TO_NV12) || (self->format_cvt_type == IYUV_TO_RGBP)) {
        align.stride_align[1] = IMG_CVT_PITCH_ALIGN - 1;  /* U plane */
        align.stride_align[2] = IMG_CVT_PITCH_ALIGN - 1;  /* V plane */
      } else if (self->format_cvt_type == NV12_TO_RGBP) {
        align.stride_align[0] = IMG_CVT_PITCH_ALIGN - 1;
        align.stride_align[1] = IMG_CVT_PITCH_ALIGN - 1;
        align.stride_align[2] = 0;
      } else {
        /* do nothing */
      }
      gst_buffer_pool_config_set_video_alignment (config, &align);
      gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);

      /* Apply the configuration */
      if (!gst_buffer_pool_set_config (pool, config)) {
        GST_WARNING_OBJECT (self, "Failed to set buffer pool config");
        ret = FALSE;
        goto GST_AMBA_VPROC_IMCVT_PROPOSE_ALLOCATION_EXIT;
      }
      gst_query_add_allocation_pool (query, pool, size, 0, 0);
    }
  } else {
    /* If decide_query is NULL, only need to prepare the allocator, no need to propose buffer pool. */
    GST_DEBUG_OBJECT (self, "Decide_query is NULL, skip proposing buffer pool.\n");
  }

  ret = GST_BASE_TRANSFORM_CLASS (parent_class)->propose_allocation (trans, decide_query, query);
  if (!ret) {
    GST_DEBUG_OBJECT(self, "propose_allocation failed.\n");
    ret = FALSE;
  }

GST_AMBA_VPROC_IMCVT_PROPOSE_ALLOCATION_EXIT:
  if (allocator && !decide_query) {
    gst_object_unref (allocator);
  }

  if (pool) {
    gst_object_unref (pool);
  }

  GST_DEBUG_OBJECT (self, "Propose allocation %s", ret ? "succeeded" : "failed");
  return ret;
}

static gboolean gst_amba_vproc_imcvt_decide_allocation (GstBaseTransform * trans, GstQuery * query)
{
  GstAmbaVprocImcvt *self = GST_AMBA_VPROC_IMCVT (trans);
  GstQuery * old_query = self->decide_query;
  GstAllocationParams params = {0};
  gboolean ret = FALSE;

  /* If no allocator proposed by downstream, use cavalry allocator */
  if (gst_query_get_n_allocation_params (query) <= 0) {
    GstAllocator *allocator = gst_amba_cavalry_allocator_get ();
    if (allocator) {
      gst_allocation_params_init (&params);
      gst_query_add_allocation_param (query, allocator, &params);
      gst_object_unref (allocator);
    }
  }

  ret = GST_BASE_TRANSFORM_CLASS (parent_class)->decide_allocation (trans,
      query);
  self->decide_query = gst_query_copy(query);
  if (old_query) {
    gst_query_unref(old_query);
  }

  return ret;
}

static int load_vect_buf (guint8 *input_addr, GstAmbaVprocImcvt *self, void *dst_addr,
   vect_desc_mfd_t *vect_desc, guint32 *size)
{
  guint8 *data = NULL;
  gint yuv_width = self->iw << 1;
  gint yuv_height = self->ih << 1;
  gint yuv_size = yuv_width * yuv_height * 3 / 2;
  gint uv_size = self->iw * (self->ih << 1);
  guint32 i, ch, ht, wd, pch, llen = 0, lwd = 0, data_sz = 0;
  guint32 len = 0;
  gint ret = GST_FLOW_OK;

  ch = vect_desc->shape.d;
  ht = vect_desc->shape.h;
  wd = vect_desc->shape.w;
  pch = vect_desc->pitch;
  data_sz = (1 << vect_desc->data_format.datasize);

  if ((self->format_cvt_type == IYUV_TO_NV12) || (self->format_cvt_type == IYUV_TO_RGBP)) {
    *size = ch * ht * pch;
    llen = ch * ht;
    lwd = wd;
    len = uv_size;
  } else if (self->format_cvt_type == NV12_TO_RGBP) {
    *size = 3 * ht / 2 * pch;
    llen = 3 * ht / 2;
    lwd = wd;
    len = yuv_size;
  } else {
    GST_ERROR_OBJECT(self, "Not supported convert type!");
    ret = GST_FLOW_ERROR;
	goto LOAD_VECT_BUF_EXIT;
  }

  if (!self->load_vect_data_inited || self->load_vect_data_sz < len) {
    if (self->load_vect_data) {
      g_free(self->load_vect_data);
      self->load_vect_data = NULL;
    }
    self->load_vect_data = (guint8 *)g_malloc(len);
    if (!self->load_vect_data) {
      GST_ERROR_OBJECT(self, "Failed to allocate load_vect data buffer (size: %u)", len);
      ret = GST_FLOW_ERROR;
      goto LOAD_VECT_BUF_EXIT;
    }
    self->load_vect_data_sz = len;
    self->load_vect_data_inited = TRUE;
   }

   data = self->load_vect_data;
  if ((self->format_cvt_type == IYUV_TO_NV12) || (self->format_cvt_type == IYUV_TO_RGBP)) {
    memcpy(data, input_addr + yuv_width * yuv_height, len);
  } else if (self->format_cvt_type == NV12_TO_RGBP) {
    memcpy(data, input_addr, len);
  }

  // data length
  if (len == *size) {
    memcpy(dst_addr, data, len);
  } else {
      for (i = 0; i < llen; i++) {
        memcpy(dst_addr + i * pch, data + i * data_sz * lwd, data_sz * lwd);
        memset(dst_addr + (i * pch + lwd * data_sz), 0, pch - lwd * data_sz);
    }
  }

LOAD_VECT_BUF_EXIT:
  return ret;
}

static void init_iyuv_descriptors(GstAmbaVprocImcvt *self, vect_desc_mfd_t *it, vect_desc_mfd_t *ot,
  vect_desc_mfd_t *u, vect_desc_mfd_t *v, guint32 *in_size, GstBuffer *out_buf)
{
  gint yuv_width = self->iw << 1;
  gint yuv_height = self->ih << 1;
  gint yuv_pitch = ALIGN_PITCH(yuv_width);

  it->shape.w = self->iw;
  it->shape.h = self->ih;
  it->shape.d = self->ic;
  it->pitch = self->ip;

  ot->shape.w = self->ow;
  ot->shape.h = self->oh;
  ot->shape.d = self->oc;
  ot->pitch = self->op;

  u->shape.w = self->iw;
  u->shape.h = self->ih;
  u->shape.d = 1;
  u->pitch = self->ip;
  v->shape.w = self->iw;
  v->shape.h = self->ih;
  v->shape.d = 1;
  v->pitch = self->ip;

  it->color_space = CS_VECT;
  *in_size = it->shape.d * it->shape.h * it->pitch;

  ot->color_space = CS_ITL;
  u->shape.w = self->iw;
  u->shape.h = self->ih;
  u->shape.d = 1;
  u->pitch = self->ip;
  u->color_space = CS_VECT;
  v->shape.w = self->iw;
  v->shape.h = self->ih;
  v->shape.d = 1;
  v->pitch = self->ip;
  v->color_space = CS_VECT;

  ot->data_addr_fd = gst_fd_memory_get_fd(gst_buffer_peek_memory(out_buf, 0));
  ot->data_addr_offset = yuv_pitch * yuv_height;

  return;
}

static void init_nv12_descriptors(GstAmbaVprocImcvt *self, vect_desc_mfd_t *y, vect_desc_mfd_t *uv,
	vect_desc_mfd_t *rgb, guint32 *in_size, GstBuffer *out_buf)
{
  gint yuv_width = self->iw << 1;
  gint yuv_height = self->ih << 1;

  y->shape.w = yuv_width;
  y->shape.h = yuv_height;
  y->shape.d = 1;
  y->pitch = ALIGN_PITCH(yuv_width);
  y->color_space = CS_NV12;

  memset(&y->roi, 0, sizeof(y->roi));
  uv->shape.w = yuv_width / 2;
  uv->shape.h = yuv_height / 2;
  uv->shape.d = 2;
  uv->pitch = ALIGN_PITCH(yuv_width);
  uv->color_space = CS_NV12;
  memset(&uv->roi, 0, sizeof(uv->roi));
  *in_size = y->shape.h * y->pitch + uv->shape.h * uv->pitch;

  rgb->shape.w = yuv_width;
  rgb->shape.h = yuv_height;
  rgb->shape.d = 3;
  rgb->pitch = ALIGN_PITCH(rgb->shape.w);
  rgb->color_space = CS_RGB;

  rgb->data_addr_fd = gst_fd_memory_get_fd(gst_buffer_peek_memory(out_buf, 0));
  rgb->data_addr_offset = 0;

  return;
}

/* out_mat receives pointer to static matrix; caller passes &ptr (ptr starts NULL). */
static int get_yuv2rgb_matrix_format(GstAmbaVprocImcvt *self, yuv2rgb_mat_t **out_mat)
{
  gint ret = GST_FLOW_OK;
  yuv2rgb_mat_t *yuv2rgb_mat = NULL;

  if (g_strcmp0(self->colorimetry, "bt601") == 0) {
    if (g_strcmp0(self->range, "full") == 0) {
      yuv2rgb_mat = &yuv2rgb_mat_bt601_full;
    } else if (g_strcmp0(self->range, "limited") == 0){
      yuv2rgb_mat = &yuv2rgb_mat_bt601_limited;
    } else {
      GST_ERROR("Bt601 only support full or limited range!");
      ret = GST_FLOW_ERROR;
      goto GET_YUV2RGB_MATRIX_FORMAT_EXIT;
    }
  } else if (g_strcmp0(self->colorimetry, "bt709") == 0) {
    if (g_strcmp0(self->range, "full") == 0) {
      yuv2rgb_mat = &yuv2rgb_mat_bt709_full;
    } else if (g_strcmp0(self->range, "limited") == 0){
      yuv2rgb_mat = &yuv2rgb_mat_bt709_limited;
    } else {
      GST_ERROR("Bt709 only support full or limited range!");
      ret = GST_FLOW_ERROR;
      goto GET_YUV2RGB_MATRIX_FORMAT_EXIT;
    }
  } else if (g_strcmp0(self->colorimetry, "bt2020") == 0) {
	if (g_strcmp0(self->range, "full") == 0) {
      yuv2rgb_mat = &yuv2rgb_mat_bt2020_full;
    } else if (g_strcmp0(self->range, "limited") == 0){
      yuv2rgb_mat = &yuv2rgb_mat_bt2020_limited;
    } else {
      GST_ERROR("Bt2020 only support full or limited range!");
      ret = GST_FLOW_ERROR;
      goto GET_YUV2RGB_MATRIX_FORMAT_EXIT;
    }
  } else {
    GST_ERROR("No supported matrix format!");
    ret = GST_FLOW_ERROR;
    goto GET_YUV2RGB_MATRIX_FORMAT_EXIT;
  }
  yuv2rgb_mat->high_accuracy_mode = 0;
  *out_mat = yuv2rgb_mat;

GET_YUV2RGB_MATRIX_FORMAT_EXIT:
  return ret;
}

static GstFlowReturn gst_amba_vproc_imcvt_transform(GstBaseTransform *trans,
    GstBuffer *inbuf, GstBuffer *outbuf)
{
  GstAmbaVprocImcvt *self = GST_AMBA_VPROC_IMCVT(trans);
  iav_al_t * iav_al = &self->iav_ctx->iav_al;
  GstMemory *inbuf_mem = NULL;
  GstMemory *incopy_mem = NULL;
  GstMapInfo src_map, out_map, info;
  gboolean src_mapped = FALSE, out_mapped = FALSE, nv12_temp_mapped = FALSE;
  GstVideoFrame in_frame;
  GstVideoInfo ii_info;
  amba_gdma_copy_t copy = {0};
  void *vect_buf_ptr = NULL;
  gint yuv_width = self->iw << 1;
  gint yuv_height = self->ih << 1;
  gint yuv_pitch = ALIGN_PITCH(yuv_width);;
  guint32 size = 0, in_size = 0;
  vect_desc_mfd_t it, ot, u, v;
  vect_desc_mfd_t y, uv, rgb;
  yuv2rgb_mat_t *yuv2rgb_mat = NULL;
  gboolean input_copy = FALSE;
  gint stride_u = 0, stride_v = 0, stride_y = 0, stride_uv = 0;
  gint i = 0, ret = GST_FLOW_OK;

  GstBuffer *nv12_temp_buf = NULL;
  GstMemory *nv12_temp_mem = NULL;
  GstMapInfo nv12_temp_map;
  GstBuffer *out_buf = outbuf;
  guint8 *dst_addr = NULL;

  memset(&it, 0, sizeof(it));
  memset(&ot, 0, sizeof(ot));
  memset(&y, 0, sizeof(y));
  memset(&uv, 0, sizeof(uv));
  memset(&rgb, 0, sizeof(rgb));

  gst_video_info_init(&ii_info);
  if (!gst_video_info_from_caps(&ii_info, self->incaps)) {
    GST_ERROR_OBJECT(self, "Failed to parse caps: %" GST_PTR_FORMAT, self->incaps);
    ret = GST_FLOW_ERROR;
    goto GST_AMBA_VPROC_IMCVT_TRANSFORM_EXIT;
  }

  if (!gst_video_frame_map (&in_frame, &ii_info, inbuf, GST_MAP_READ)) {
    GST_ERROR_OBJECT (self, "Failed to map input video frame");
    ret = GST_FLOW_ERROR;
    goto GST_AMBA_VPROC_IMCVT_TRANSFORM_EXIT;
  }

  switch (self->format_cvt_type) {
    case IYUV_TO_NV12:
    case IYUV_TO_RGBP:
      stride_u = GST_VIDEO_FRAME_PLANE_STRIDE(&in_frame, 1);
      stride_v = GST_VIDEO_FRAME_PLANE_STRIDE(&in_frame, 2);
      break;
    case NV12_TO_RGBP:
      stride_y = GST_VIDEO_FRAME_PLANE_STRIDE(&in_frame, 0);
      stride_uv = GST_VIDEO_FRAME_PLANE_STRIDE(&in_frame, 1);
      break;
    default:
      break;
  }

  gst_video_frame_unmap(&in_frame);

  gst_buffer_map(inbuf, &src_map, GST_MAP_READ);
  src_mapped = TRUE;
  gst_buffer_map(outbuf, &out_map, GST_MAP_WRITE);
  out_mapped = TRUE;

  inbuf_mem = gst_buffer_peek_memory(inbuf, 0);
  switch (self->format_cvt_type) {
    case IYUV_TO_NV12:
    case IYUV_TO_RGBP:
      if (!self->zero_copy || !gst_is_amba_cavalry_memory(inbuf_mem) ||
        stride_u % IMG_CVT_PITCH_ALIGN != 0 || stride_v % IMG_CVT_PITCH_ALIGN != 0) {
        input_copy = TRUE;
      }
      if (self->format_cvt_type == IYUV_TO_NV12) {
        out_buf = outbuf;
      } else {
        {
          gsize raw = (gsize) yuv_pitch * yuv_height * 3 / 2;
          gsize alloc_sz = AMBA_ROUND_GSIZE_ODD_DSP_PITCH_UNITS (raw);
          nv12_temp_mem = gst_allocator_alloc (self->cavalry_allocator, alloc_sz, NULL);
        }
        if (nv12_temp_mem == NULL) {
          GST_ERROR_OBJECT (self, "Failed to allocate NV12 temp buffer");
          ret = GST_FLOW_ERROR;
          goto GST_AMBA_VPROC_IMCVT_TRANSFORM_EXIT;
        }
        nv12_temp_buf = gst_buffer_new();
        gst_buffer_append_memory(nv12_temp_buf, nv12_temp_mem);
        out_buf = nv12_temp_buf;
      }
      init_iyuv_descriptors(self, &it, &ot, &u, &v, &in_size, out_buf);
      break;
    case NV12_TO_RGBP:
      if (!self->zero_copy || !gst_is_amba_cavalry_memory(inbuf_mem) ||
          stride_y % IMG_CVT_PITCH_ALIGN != 0 ||
          stride_uv % IMG_CVT_PITCH_ALIGN != 0 || stride_y != stride_uv) {
        input_copy = TRUE;
      }
      init_nv12_descriptors(self, &y, &uv, &rgb, &in_size, outbuf);
      /* Zero-copy: vproc must use actual row stride (e.g. IAV pitch 896), not ALIGN_PITCH(width) only. */
      if (!input_copy) {
        y.pitch = (guint32) stride_y;
        uv.pitch = (guint32) stride_uv;
      }
      break;
    default:
     break;
  }

  if (!self->logged_input_path_once) {
    gint in_mfd = -1, out_mfd = -1;

    if (gst_is_amba_cavalry_memory (inbuf_mem))
      in_mfd = gst_amba_cavalry_memory_get_fd (inbuf_mem);
    else if (gst_is_fd_memory (inbuf_mem))
      in_mfd = gst_fd_memory_get_fd (inbuf_mem);
    {
      GstMemory *om = gst_buffer_peek_memory (out_buf, 0);
      if (gst_is_amba_cavalry_memory (om))
        out_mfd = gst_amba_cavalry_memory_get_fd (om);
      else if (gst_is_fd_memory (om))
        out_mfd = gst_fd_memory_get_fd (om);
    }
    if (input_copy) {
      const char *reason;
      if (!self->zero_copy) {
        reason = "zero-copy disabled";
      } else if (!gst_is_amba_cavalry_memory(inbuf_mem)) {
        reason = "input is not Cavalry memory";
      } else if (self->format_cvt_type == NV12_TO_RGBP) {
        reason = "NV12 Y/UV stride not aligned to Cavalry pitch";
      } else {
        reason = "I420 U/V stride not aligned to Cavalry pitch";
      }
      g_printerr ("[amba_img_cvt]: copy path for input: %s in_mem_fd=%d out_mem_fd=%d\n",
          reason, in_mfd, out_mfd);
    } else {
      if (self->format_cvt_type == NV12_TO_RGBP) {
        g_printerr ("[amba_img_cvt]: zero-copy input (NV12->RGBP), stride_y=%d stride_uv=%d "
            "in_mem_fd=%d out_mem_fd=%d\n",
            stride_y, stride_uv, in_mfd, out_mfd);
      } else if (self->format_cvt_type == IYUV_TO_NV12 ||
          self->format_cvt_type == IYUV_TO_RGBP) {
        g_printerr ("[amba_img_cvt]: zero-copy input (I420 path), stride_u=%d stride_v=%d "
            "in_mem_fd=%d out_mem_fd=%d\n",
            stride_u, stride_v, in_mfd, out_mfd);
      } else {
        g_printerr ("[amba_img_cvt]: zero-copy input path in_mem_fd=%d out_mem_fd=%d\n",
            in_mfd, out_mfd);
      }
    }
    self->logged_input_path_once = TRUE;
  }

  if (input_copy) {
    incopy_mem = gst_allocator_alloc(self->cavalry_allocator, in_size, NULL);
    if (incopy_mem == NULL) {
      GST_ERROR_OBJECT(self, "Failed to allocate buffer");
      ret = GST_FLOW_ERROR;
      goto GST_AMBA_VPROC_IMCVT_TRANSFORM_EXIT;
    }
    gst_memory_map(incopy_mem, &info, GST_MAP_WRITE);
    if ((self->format_cvt_type == IYUV_TO_NV12) || (self->format_cvt_type == IYUV_TO_RGBP)) {
      it.data_addr_fd = gst_fd_memory_get_fd(incopy_mem);
      it.data_addr_offset = 0;
      u.data_addr_fd = gst_fd_memory_get_fd(incopy_mem);
      v.data_addr_fd = gst_fd_memory_get_fd(incopy_mem);
      u.data_addr_offset = 0;
      v.data_addr_offset = self->ih * self->ip;
      vect_buf_ptr = &it;
     } else if (self->format_cvt_type == NV12_TO_RGBP) {
      y.data_addr_fd = gst_fd_memory_get_fd(incopy_mem);
      y.data_addr_offset = 0;
      uv.data_addr_fd = gst_fd_memory_get_fd(incopy_mem);
      uv.data_addr_offset = y.shape.h * y.pitch;
      vect_buf_ptr = &y;
    } else {
       /*do nothing */
    }
    if (load_vect_buf(src_map.data, self, info.data, vect_buf_ptr, &size) < 0) {
      GST_ERROR("load input fails!");
      ret = GST_FLOW_ERROR;
      gst_memory_unmap(incopy_mem, &info);
      goto GST_AMBA_VPROC_IMCVT_TRANSFORM_EXIT;
    }
    gst_memory_unmap(incopy_mem, &info);
  } else {
    if ((self->format_cvt_type == IYUV_TO_NV12) || (self->format_cvt_type == IYUV_TO_RGBP)) {
      it.data_addr_fd = gst_fd_memory_get_fd(gst_buffer_peek_memory(inbuf, 0));
      u.data_addr_fd = gst_fd_memory_get_fd(gst_buffer_peek_memory(inbuf, 0));
      v.data_addr_fd = gst_fd_memory_get_fd(gst_buffer_peek_memory(inbuf, 0));
      it.data_addr_offset = 0;
      u.data_addr_offset = yuv_width * yuv_height;
      v.data_addr_offset = yuv_width * yuv_height + self->ip * self->ih;
    } else if (self->format_cvt_type == NV12_TO_RGBP) {
      y.data_addr_fd = gst_fd_memory_get_fd(gst_buffer_peek_memory(inbuf, 0));
      y.data_addr_offset = 0;
      uv.data_addr_fd = gst_fd_memory_get_fd(gst_buffer_peek_memory(inbuf, 0));
      uv.data_addr_offset = y.shape.h * y.pitch;
    } else {
      /* do nothing */
    }
  }
  switch (self->format_cvt_type) {
    case IYUV_TO_NV12:
      if (vproc_merge_uv_mfd(&u, &v, &ot) < 0) {
        GST_ERROR("merge uv fails!");
        ret = GST_FLOW_ERROR;
        goto GST_AMBA_VPROC_IMCVT_TRANSFORM_EXIT;
      }
      break;
    case NV12_TO_RGBP:
      if (get_yuv2rgb_matrix_format(self, &yuv2rgb_mat) < 0) {
        GST_ERROR("get matrix format failed!");
        ret = GST_FLOW_ERROR;
        goto GST_AMBA_VPROC_IMCVT_TRANSFORM_EXIT;
      }
#ifdef DBUILD_AMBA_CAVALRY_V3
      if (vproc_yuv2rgb_mfd(&y, &uv, &rgb, yuv2rgb_mat) < 0) {
        GST_ERROR("covert RGB fails!");
        ret = GST_FLOW_ERROR;
        goto GST_AMBA_VPROC_IMCVT_TRANSFORM_EXIT;
      }
#else
      if (vproc_yuv2rgb_420_mfd(&y, &uv, &rgb, yuv2rgb_mat) < 0) {
        GST_ERROR("covert RGB fails!");
        ret = GST_FLOW_ERROR;
        goto GST_AMBA_VPROC_IMCVT_TRANSFORM_EXIT;
      }
#endif
      break;
    case IYUV_TO_RGBP:
      if (vproc_merge_uv_mfd(&u, &v, &ot) < 0) {
        GST_ERROR("merge uv fails!");
        ret = GST_FLOW_ERROR;
        goto GST_AMBA_VPROC_IMCVT_TRANSFORM_EXIT;
      }
      break;
    default:
      break;
  }

  if ((self->format_cvt_type == IYUV_TO_NV12) || (self->format_cvt_type == IYUV_TO_RGBP)) {
    if (!gst_is_amba_cavalry_memory(inbuf_mem)) {
      if (self->format_cvt_type == IYUV_TO_NV12) {
        dst_addr = out_map.data;
      } else {
        if (!gst_buffer_map(nv12_temp_buf, &nv12_temp_map, GST_MAP_WRITE)) {
          GST_ERROR_OBJECT(self, "Failed to map NV12 temp buffer");
          ret = GST_FLOW_ERROR;
          goto GST_AMBA_VPROC_IMCVT_TRANSFORM_EXIT;
        }
        nv12_temp_mapped = TRUE;
        dst_addr = nv12_temp_map.data;
     }
     for (i = 0; i < yuv_height; i++) {
       memcpy(dst_addr + i * yuv_pitch, src_map.data + i * yuv_pitch, yuv_width);
     }
     if (nv12_temp_mapped) {
       gst_buffer_unmap(nv12_temp_buf, &nv12_temp_map);
     }
    } else {
      copy.src_offset = 0;
      copy.dst_offset = 0;
      copy.src_pitch = yuv_pitch;
      copy.dst_pitch = yuv_pitch;
      copy.width = yuv_width;
      copy.height = yuv_height;
      copy.src_dma_buf_fd = gst_fd_memory_get_fd(gst_buffer_peek_memory(inbuf, 0));
      if (self->format_cvt_type == IYUV_TO_NV12) {
        copy.dst_dma_buf_fd = gst_fd_memory_get_fd(gst_buffer_peek_memory(outbuf, 0));
      } else {
        copy.dst_dma_buf_fd = gst_fd_memory_get_fd(gst_buffer_peek_memory(nv12_temp_buf, 0));
      }
      iav_al->f_gdma_copy(self->iav_ctx->iav_fd, &copy);
    }
  }

  /* Temp NV12 convert to RGBP */
  if (self->format_cvt_type == IYUV_TO_RGBP) {
    memset(&y, 0, sizeof(y));
    memset(&uv, 0, sizeof(uv));
    memset(&rgb, 0, sizeof(rgb));
    init_nv12_descriptors(self, &y, &uv, &rgb, &in_size, outbuf);
    y.data_addr_fd = gst_fd_memory_get_fd(gst_buffer_peek_memory(nv12_temp_buf, 0));
    y.data_addr_offset = 0;
    uv.data_addr_fd = gst_fd_memory_get_fd(gst_buffer_peek_memory(nv12_temp_buf, 0));
    uv.data_addr_offset = yuv_height * yuv_pitch;
    if (get_yuv2rgb_matrix_format(self, &yuv2rgb_mat) < 0) {
      GST_ERROR("get matrix format failed!");
      ret = GST_FLOW_ERROR;
      goto GST_AMBA_VPROC_IMCVT_TRANSFORM_EXIT;
    }
#ifdef DBUILD_AMBA_CAVALRY_V3
    if (vproc_yuv2rgb_mfd(&y, &uv, &rgb, yuv2rgb_mat) < 0) {
      GST_ERROR("covert RGB fails!");
      ret = GST_FLOW_ERROR;
      goto GST_AMBA_VPROC_IMCVT_TRANSFORM_EXIT;
    }
#else
    if (vproc_yuv2rgb_420_mfd(&y, &uv, &rgb, yuv2rgb_mat) < 0) {
      GST_ERROR("covert RGB fails!");
      ret = GST_FLOW_ERROR;
      goto GST_AMBA_VPROC_IMCVT_TRANSFORM_EXIT;
    }
#endif
  }

  if (ret == GST_FLOW_OK) {
    /* prepare_output_buffer allocates a new buffer; meta is often missing on out after transform. */
    if (!amba_buffer_get_private_data_meta(outbuf) &&
        amba_buffer_get_private_data_meta(inbuf)) {
      amba_buffer_copy_private_data_meta(outbuf, inbuf);
    }
  }

GST_AMBA_VPROC_IMCVT_TRANSFORM_EXIT:
  if (nv12_temp_buf) {
    gst_buffer_unref(nv12_temp_buf);
  }
  if (src_mapped) {
    gst_buffer_unmap (inbuf, &src_map);
    src_mapped = FALSE;
  }
  if (out_mapped) {
    gst_buffer_unmap (outbuf, &out_map);
    out_mapped = FALSE;
  }
  if (incopy_mem) {
    gst_memory_unref (incopy_mem);
    incopy_mem = NULL;
  }

  return ret;
}

static void gst_amba_vproc_imcvt_finalize(GObject * gobject)
{
  GstAmbaVprocImcvt *self = GST_AMBA_VPROC_IMCVT (gobject);

  release_cv_vproc_ctx(1);

  if (self->cavalry_allocator) {
    gst_object_unref(self->cavalry_allocator);
    self->cavalry_allocator = NULL;
  }

  if (self->incaps) {
    gst_caps_unref(self->incaps);
    self->incaps = NULL;
  }

  if (self->colorimetry) {
    g_free(self->colorimetry);
    self->colorimetry = NULL;
  }

  if (self->range) {
    g_free(self->range);
    self->range = NULL;
  }

  if (self->load_vect_data) {
    g_free(self->load_vect_data);
    self->load_vect_data = NULL;
  }
  self->load_vect_data_sz = 0;
  self->load_vect_data_inited = FALSE;

  G_OBJECT_CLASS (parent_class)->finalize (gobject);

  return;
}
