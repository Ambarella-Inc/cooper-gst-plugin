/*
 * gstambahwvdecsrc.c
 *
 * History:
 *    6/16/2022 - [Zhi He] created file
 *
 * Copyright (C) 2022 Ambarella International LP
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
 * SECTION: element-amba_hwvdecsrc
 * @title: amba_hwvdecsrc
 *
 * amba_hwvdecsrc can be used to capture video frames from Amba HW decoder.
 *
 */

#include "stdio.h"

#include "common_err_code_c.h"

#include "bitstream_state.h"
#include "iav_al.h"
#include "iav_ctx.h"

#include "internal.h"
#include "debug_log.h"

#include "amba_direct_mem.h"

#include "element_common.h"

#include "buffer_utils.h"

#include "gstambahwvdecsrc.h"

#define AMBACAMSRC_DEFAULT_PIXEL_FORMAT "nv12"
#define AMBACAMSRC_DEFAULT_WIDTH 1920
#define AMBACAMSRC_DEFAULT_HEIGHT 1080
#define AMBACAMSRC_DEFAULT_FRAMERATE_NUM 90000
#define AMBACAMSRC_DEFAULT_FRAMERATE_DEN 3003
#define AMBACAMSRC_DEFAULT_FRAMERATE 29.97
#define AMBACAMSRC_DEFAULT_BUF_ID 0
#define AMBACAMSRC_MIN_BUF_ID 0
#define AMBACAMSRC_MAX_BUF_ID 32

#define AMBACAMSRC_MIN_WIDTH 180
#define AMBACAMSRC_MAX_WIDTH 3840

#define AMBACAMSRC_MIN_HEIGHT 120
#define AMBACAMSRC_MAX_HEIGHT 2160


GST_DEBUG_CATEGORY_STATIC (gst_amba_hwvdecsrc_debug);
#define GST_CAT_DEFAULT gst_amba_hwvdecsrc_debug

/* Filter signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  PROP_0,
  PROP_PIXEL_FORMAT,
  PROP_WIDTH,
  PROP_HEIGHT,
  PROP_FRAMERATE_NUM,
  PROP_FRAMERATE_DEN,
  PROP_FRAMERATE,
  PROP_BUF_ID,
  PROP_FILE_PATH,
};

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY"));

#define gst_amba_hwvdecsrc_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstAmbaHwvdecsrc, gst_amba_hwvdecsrc, GST_TYPE_PUSH_SRC,
    GST_DEBUG_CATEGORY_INIT (gst_amba_hwvdecsrc_debug, "amba_hwvdecsrc", 0,
        "amba_hwvdecsrc") );

static GstFlowReturn gst_amba_hwvdecsrc_create (GstPushSrc *psrc,
    GstBuffer **outbuf);
static void gst_amba_hwvdecsrc_finalize (GObject *gobject);
static void gst_amba_hwvdecsrc_set_property (GObject *object, guint prop_id,
    const GValue *value, GParamSpec *pspec);
static void gst_amba_hwvdecsrc_get_property (GObject *object, guint prop_id,
    GValue *value, GParamSpec *pspec);

static GstStateChangeReturn
gst_amba_hwvdecsrc_change_state (GstElement *element, GstStateChange transition);

/* initialize the amba_hwvdecsrc class */
static void gst_amba_hwvdecsrc_class_init (GstAmbaHwvdecsrcClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstPushSrcClass *gstpushsrc_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);
  gstbasesrc_class = GST_BASE_SRC_CLASS (klass);
  gstpushsrc_class = GST_PUSH_SRC_CLASS (klass);
  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_amba_hwvdecsrc_change_state);

  gobject_class->finalize = gst_amba_hwvdecsrc_finalize;
  gobject_class->set_property = gst_amba_hwvdecsrc_set_property;
  gobject_class->get_property = gst_amba_hwvdecsrc_get_property;

  g_object_class_install_property (gobject_class, PROP_PIXEL_FORMAT,
    g_param_spec_string ("pixel-format", "Pixel-Format", "Pixel format of the buffer, default is nv12",
      AMBACAMSRC_DEFAULT_PIXEL_FORMAT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS) );

  g_object_class_install_property (gobject_class, PROP_WIDTH,
    g_param_spec_uint ("width", "Width", "Width of the source buffer or canvas",
      AMBACAMSRC_MIN_WIDTH, AMBACAMSRC_MAX_WIDTH,
      AMBACAMSRC_DEFAULT_WIDTH, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS) );

  g_object_class_install_property (gobject_class, PROP_HEIGHT,
    g_param_spec_uint ("height", "Height", "Height of the source buffer or canvas",
      AMBACAMSRC_MIN_HEIGHT, AMBACAMSRC_MAX_HEIGHT,
      AMBACAMSRC_DEFAULT_HEIGHT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS) );

  g_object_class_install_property (gobject_class, PROP_BUF_ID,
    g_param_spec_uint ("buf-id", "Buf-Id", "Specify the source buffer id",
      AMBACAMSRC_MIN_BUF_ID, AMBACAMSRC_MAX_BUF_ID,
      AMBACAMSRC_DEFAULT_BUF_ID, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS) );

  g_object_class_install_property (gobject_class, PROP_FRAMERATE_NUM,
    g_param_spec_uint ("framerate-num", "Framerate-Num", "framerate numerator",
      1, 0xffffffff,
      90000, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS) );

  g_object_class_install_property (gobject_class, PROP_FRAMERATE_DEN,
    g_param_spec_uint ("framerate-den", "Framerate-Den", "framerate denominator",
      1, 0xffffffff,
      3003, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS) );

  gst_element_class_set_static_metadata (gstelement_class,
    "Amba HW decode Source, output YUV frames",
    "Source/Video",
    "Reads video frames from HW decoder to Ambarella platform",
    "Zhi He <zhe@ambarella.com>");

  gst_element_class_add_static_pad_template (gstelement_class, &src_factory);

  gstbasesrc_class->start = NULL;
  gstbasesrc_class->stop = NULL;
  gstpushsrc_class->create = gst_amba_hwvdecsrc_create;
}

static void gst_amba_hwvdecsrc_init (GstAmbaHwvdecsrc *thiz)
{
  // mem init
  amba_direct_mem_init ();

  // iav context
  thiz->iav_ctx = acquire_iav_ctx (1);
  if (!thiz->iav_ctx) {
    GST_ERROR("acquire_iav_ctx failed\n");
    return;
  }

  thiz->buf_id = 0;

  thiz->width = AMBACAMSRC_DEFAULT_WIDTH;
  thiz->height = AMBACAMSRC_DEFAULT_HEIGHT;
  thiz->framerate_num = AMBACAMSRC_DEFAULT_FRAMERATE_NUM;
  thiz->framerate_den = AMBACAMSRC_DEFAULT_FRAMERATE_DEN;
  thiz->framerate = AMBACAMSRC_DEFAULT_FRAMERATE;
  thiz->pixel_format_fourcc = 0;

  gst_base_src_set_live (GST_BASE_SRC (thiz), TRUE);
  gst_base_src_set_do_timestamp (GST_BASE_SRC (thiz), TRUE);
  gst_base_src_set_format (GST_BASE_SRC (thiz), GST_FORMAT_TIME);

}

static void gst_amba_hwvdecsrc_finalize (GObject *gobject)
{
  GstAmbaHwvdecsrc * thiz = GST_AMBA_HWVDECSRC (gobject);

  if (thiz->iav_ctx) {
    release_iav_ctx (1);
    thiz->iav_ctx = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (gobject);
}

static void gst_amba_hwvdecsrc_set_property (GObject *object, guint prop_id,
    const GValue *value, GParamSpec *pspec)
{
  GstAmbaHwvdecsrc * thiz = GST_AMBA_HWVDECSRC (object);

  switch (prop_id) {

    case PROP_PIXEL_FORMAT: {
      if (thiz->str_pixel_format) {
        g_free(thiz->str_pixel_format);
        thiz->str_pixel_format = NULL;
      }
      thiz->str_pixel_format = g_strdup(g_value_get_string (value));
    }
    break;

    case PROP_WIDTH: {
      thiz->width = g_value_get_uint (value);
    }
    break;

    case PROP_HEIGHT: {
      thiz->height = g_value_get_uint (value);
    }
    break;

    case PROP_FRAMERATE_NUM: {
      thiz->framerate_num = g_value_get_uint (value);
    }
    break;

    case PROP_FRAMERATE_DEN: {
      thiz->framerate_den = g_value_get_uint (value);
    }
    break;

    case PROP_BUF_ID: {
      thiz->buf_id = g_value_get_uint (value);
    }
    break;

    default: {
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
    break;
  }
}

static void gst_amba_hwvdecsrc_get_property (GObject *object, guint prop_id,
    GValue *value, GParamSpec *pspec)
{
  GstAmbaHwvdecsrc *thiz = GST_AMBA_HWVDECSRC (object);

  switch (prop_id) {
    case PROP_PIXEL_FORMAT: {
      g_value_set_string (value, thiz->str_pixel_format);
    }
    break;

    case PROP_WIDTH: {
      g_value_set_uint (value, thiz->width);
    }
    break;

    case PROP_HEIGHT: {
      g_value_set_uint (value, thiz->height);
    }
    break;

    case PROP_FRAMERATE_NUM: {
      g_value_set_uint (value, thiz->framerate_num);
    }
    break;

    case PROP_FRAMERATE_DEN: {
      g_value_set_uint (value, thiz->framerate_den);
    }
    break;

    case PROP_BUF_ID: {
      g_value_set_uint (value, thiz->buf_id);
    }
    break;

    default: {
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
    break;
  }
}

/* GstElement vmethod implementations */
static GstFlowReturn
gst_amba_hwvdecsrc_create (GstPushSrc * psrc,
    GstBuffer ** outbuf)
{
  GstAmbaHwvdecsrc * thiz = GST_AMBA_HWVDECSRC (psrc);
  GstFlowReturn flow_ret = GST_FLOW_OK;
  GstBuffer * p_out_buf = NULL;

  iav_ctx_t * iav_ctx = thiz->iav_ctx;
  iav_al_t * iav_al = &iav_ctx->iav_al;
  int ret = 0;

  amba_dsp_query_yuv_buffer_t yuv_buf;

  do {
    memset(&yuv_buf, 0x0, sizeof(yuv_buf));
    yuv_buf.canvas_options.canvas_buffer_map |= 1 << thiz->buf_id;
    ret = iav_al->f_query_yuv_buffer(iav_ctx->iav_fd, &yuv_buf);
    if (ret) {
      DPRINT_ERROR("f_query_yuv_buffer failed, ret %d\n", ret);
      flow_ret = GST_FLOW_ERROR;
      break;
    }

    p_out_buf = alloc_gst_buffer_amba_direct_mem_nv12(
      yuv_buf.yuv_ctx[thiz->buf_id].width, yuv_buf.yuv_ctx[thiz->buf_id].height,
      yuv_buf.yuv_ctx[thiz->buf_id].pitch, yuv_buf.yuv_ctx[thiz->buf_id].pitch,
      (unsigned char *) iav_ctx->map_dsp.base + yuv_buf.yuv_ctx[thiz->buf_id].y_addr_offset,
      (unsigned char *) iav_ctx->map_dsp.base + yuv_buf.yuv_ctx[thiz->buf_id].uv_addr_offset,
      0,
      NULL);
    if (!p_out_buf) {
      DPRINT_ERROR("alloc_gst_buffer_amba_direct_mem_nv12 failed\n");
      flow_ret = GST_FLOW_ERROR;
      break;
    }

    *outbuf = p_out_buf;
    flow_ret = GST_FLOW_OK;
  } while (0);

  return flow_ret;
}

static GstStateChangeReturn
gst_amba_hwvdecsrc_change_state (GstElement *element, GstStateChange transition)
{
  switch (transition) {
    default: {
      break;
    }
  }

  return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
}

