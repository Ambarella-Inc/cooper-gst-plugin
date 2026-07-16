/*
 * gstambacamsrc.c
 *
 * History:
 *    5/15/2025 - [Yang Yu] created file
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
 * SECTION: element-amba_camsrc2
 * @title: amba_camsrc2
 *
 * amba_camsrc2 can be used to capture video frames from Amba device.
 *
 */

#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>
#include <gst/allocators/gstdmabuf.h>
#include <unistd.h>

#include "stdio.h"

#include "common_err_code_c.h"

#include "bitstream_state.h"
#include "iav_al.h"
#include "iav_ctx.h"

#include "internal.h"
#include "debug_log.h"

#include "gst_amba_pitch_align.h"
#include "gst_amba_cavalry_allocator.h"
#include "buffer_utils.h"
#include "amba_private_data.h"

#include "gstambacamsrc2.h"

#define AMBACAMSRC_DEFAULT_PIXEL_FORMAT "NV12"
#define AMBACAMSRC_DEFAULT_WIDTH 1920
#define AMBACAMSRC_DEFAULT_HEIGHT 1080
#define AMBACAMSRC_DEFAULT_FRAMERATE 29.97
#define AMBACAMSRC_DEFAULT_BUF_ID 0
#define AMBACAMSRC_DEFAULT_PYRAMID_LAYER 0

#define AMBACAMSRC_MIN_WIDTH 180
#if defined (BUILD_DSP_AMBA_V5)
#define AMBACAMSRC_MAX_WIDTH 4096
#elif defined (BUILD_DSP_AMBA_V6)
#define AMBACAMSRC_MAX_WIDTH 8192
#else
#define AMBACAMSRC_MAX_WIDTH 3840
#endif

#define AMBACAMSRC_MIN_HEIGHT 120
#if defined (BUILD_DSP_AMBA_V5)
#define AMBACAMSRC_MAX_HEIGHT 3000
#elif defined (BUILD_DSP_AMBA_V6)
#define AMBACAMSRC_MAX_HEIGHT 5120
#else
#define AMBACAMSRC_MAX_HEIGHT 2160
#endif

#define AMBACAMSRC_DEFAULT_GDMA_COPY 1

#define DEFAULT_PROVIDE_CLOCK        FALSE

// if discard, would drop some gops (pts < base time) at starting
#define DEFAULT_DISCARD_PREVIOUS_FRAMES TRUE

// debug when pts from yuv frame is not valid, use fake pts
//#define DEBUG_USE_FAKE_PTS

// use timestamp from hw timer
//#define DEBUG_USE_HWTIMER_AS_PTS

GST_DEBUG_CATEGORY_STATIC (gst_amba_camsrc2_debug);
#define GST_CAT_DEFAULT gst_amba_camsrc2_debug

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
  PROP_DUMP_FILE,
  PROP_GDMA,
  PROP_DMA_BUF,
  PROP_PROVIDE_CLOCK,
  PROP_CAPTURE_TYPE,
  PROP_PYRAMID_LAYER,
  PROP_CHANNEL_ID,
  PROP_DISCARD_PREV,
  PROP_CAPTURE_ME0,
  PROP_CAPTURE_ME1,
};

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (AMBACAMSRC_DEFAULT_PIXEL_FORMAT)));

#define gst_amba_camsrc2_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstAmbaCamsrc2, gst_amba_camsrc2, GST_TYPE_PUSH_SRC,
    GST_DEBUG_CATEGORY_INIT (gst_amba_camsrc2_debug, "amba_camsrc2", 0,
        "amba_camsrc2") );

static GstFlowReturn gst_amba_camsrc2_create (GstPushSrc *psrc,
    GstBuffer **outbuf);
static void gst_amba_camsrc2_finalize (GObject *gobject);
static void gst_amba_camsrc2_set_property (GObject *object, guint prop_id,
    const GValue *value, GParamSpec *pspec);
static void gst_amba_camsrc2_get_property (GObject *object, guint prop_id,
    GValue *value, GParamSpec *pspec);

static GstStateChangeReturn
gst_amba_camsrc2_change_state (GstElement *element, GstStateChange transition);

static GstCaps * gst_amba_camsrc2_get_caps (GstBaseSrc * bsrc, GstCaps * caps);
static gboolean gst_amba_camsrc2_start (GstBaseSrc * bsrc);
static gboolean gst_amba_camsrc2_decide_allocation (GstBaseSrc * bsrc,
    GstQuery * query);

static GstClock *
gst_amba_camsrc2_provide_clock (GstElement * element);


static gboolean
gst_amba_camsrc2_send_event (GstElement * element, GstEvent * event);

/* initialize the amba_camsrc2 class */
static void gst_amba_camsrc2_class_init (GstAmbaCamsrc2Class *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstPushSrcClass *gstpushsrc_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);
  gstbasesrc_class = GST_BASE_SRC_CLASS (klass);
  gstpushsrc_class = GST_PUSH_SRC_CLASS (klass);
  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_amba_camsrc2_change_state);
  gstelement_class->provide_clock = GST_DEBUG_FUNCPTR (gst_amba_camsrc2_provide_clock);
  gstelement_class->send_event = GST_DEBUG_FUNCPTR (gst_amba_camsrc2_send_event);

  gobject_class->finalize = gst_amba_camsrc2_finalize;
  gobject_class->set_property = gst_amba_camsrc2_set_property;
  gobject_class->get_property = gst_amba_camsrc2_get_property;

  g_object_class_install_property (gobject_class, PROP_PIXEL_FORMAT,
    g_param_spec_string ("pixel-format", "Pixel-Format", "Pixel format of the buffer, default is nv12",
      AMBACAMSRC_DEFAULT_PIXEL_FORMAT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS) );

  g_object_class_install_property (gobject_class, PROP_WIDTH,
    g_param_spec_uint ("width", "Width", "Width of the source buffer or canvas",
      0, AMBACAMSRC_MAX_WIDTH,
      0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS) );

  g_object_class_install_property (gobject_class, PROP_HEIGHT,
    g_param_spec_uint ("height", "Height", "Height of the source buffer or canvas",
      0, AMBACAMSRC_MAX_HEIGHT,
      0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS) );

  g_object_class_install_property (gobject_class, PROP_BUF_ID,
    g_param_spec_uint ("buf-id", "Buf-Id", "Specify the source buffer id",
      0, IAV_MAX_CANVAS_BUF_NUM,
      AMBACAMSRC_DEFAULT_BUF_ID, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS) );

  g_object_class_install_property (gobject_class, PROP_FRAMERATE_NUM,
    g_param_spec_uint ("framerate-num", "Framerate-Num", "framerate numerator",
      1, 0xffffffff,
      DDefaultVideoFramerateNum, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS) );

  g_object_class_install_property (gobject_class, PROP_FRAMERATE_DEN,
    g_param_spec_uint ("framerate-den", "Framerate-Den", "framerate denominator",
      1, 0xffffffff,
      DDefaultVideoFramerateDen, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS) );

  g_object_class_install_property (gobject_class, PROP_DUMP_FILE,
      g_param_spec_string ("dump", "DumpFile", "dump file name ?",
          NULL, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_GDMA,
      g_param_spec_uchar ("gdma", "GDMA", "gdma copy enable",
          0, 1,
          AMBACAMSRC_DEFAULT_GDMA_COPY, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS) );

  g_object_class_install_property (gobject_class, PROP_DMA_BUF,
      g_param_spec_uchar ("dma", "DMA", "enable dma buffer",
          0, 1,
          0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS) );

  g_object_class_install_property (gobject_class, PROP_PROVIDE_CLOCK,
      g_param_spec_boolean ("provide-clock", "Provide Clock",
          "Provide a clock to be used as the global pipeline clock",
          DEFAULT_PROVIDE_CLOCK, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_CAPTURE_TYPE,
      g_param_spec_string ("capture_type", "CaptureType", "select captured type?",
          NULL, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_PYRAMID_LAYER,
    g_param_spec_uint ("pyramid_layer", "PyramidLayer", "Specify the pyramid layer id",
      0, IAV_MAX_PYRAMID_LAYERS,
      AMBACAMSRC_DEFAULT_PYRAMID_LAYER, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_CHANNEL_ID,
    g_param_spec_uint ("ch_id", "Channel-Id", "Specify the channel id",
      0, CONFIG_AMBARELLA_MAX_CHANNEL_NUM,
      0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_DISCARD_PREV,
      g_param_spec_boolean ("discard_prev", "DiscardPrevious",
      "Discard previous frames", DEFAULT_DISCARD_PREVIOUS_FRAMES,
      G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_CAPTURE_ME0,
      g_param_spec_uchar ("me0", "ME0", "Capture ME0 buffer",
          0, 1,
          0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS) );

  g_object_class_install_property (gobject_class, PROP_CAPTURE_ME1,
      g_param_spec_uchar ("me1", "ME1", "Capture ME1 buffer",
          0, 1,
          0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS) );

  gst_element_class_set_static_metadata (gstelement_class,
    "Amba Camera Source, output YUV frames",
    "Source/Video",
    "Reads video frames from sensors which connected to Ambarella platform",
    "Zhi He <zhe@ambarella.com>");

  gst_element_class_add_static_pad_template (gstelement_class, &src_factory);

  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_amba_camsrc2_start);
  gstbasesrc_class->get_caps = GST_DEBUG_FUNCPTR (gst_amba_camsrc2_get_caps);
  gstbasesrc_class->decide_allocation = GST_DEBUG_FUNCPTR (gst_amba_camsrc2_decide_allocation);
  gstpushsrc_class->create = GST_DEBUG_FUNCPTR (gst_amba_camsrc2_create);
}

static void gst_amba_camsrc2_init (GstAmbaCamsrc2 *thiz)
{
  // mem init
  gst_amba_cavalry_allocator_init_once();
  //amba_direct_mem_init ();

  // iav context
  thiz->iav_ctx = acquire_iav_ctx (1);
  if (!thiz->iav_ctx) {
    GST_ERROR("acquire_iav_ctx failed\n");
    return;
  }

  thiz->buf_id = 0;
  thiz->width = 0;//AMBACAMSRC_DEFAULT_WIDTH;
  thiz->height = 0;//AMBACAMSRC_DEFAULT_HEIGHT;
  thiz->framerate_num = DDefaultVideoFramerateNum;
  thiz->framerate_den = DDefaultVideoFramerateDen;
  thiz->framerate = 0;//AMBACAMSRC_DEFAULT_FRAMERATE;
  thiz->pixel_format_fourcc = 0;
  thiz->capture_type = CAPTURE_PREVIEW_BUFFER;
  g_strlcpy(thiz->str_pixel_format, AMBACAMSRC_DEFAULT_PIXEL_FORMAT,
      sizeof(thiz->str_pixel_format));

  thiz->dump_num = 1;

  thiz->gdma_copy_enable = AMBACAMSRC_DEFAULT_GDMA_COPY;
  thiz->discard_prev_frame = DEFAULT_DISCARD_PREVIOUS_FRAMES;

  for (int i = 0; i < DAMBA_MAX_YUV_BUF_NUM; i++) {
    thiz->gdma_ctx[i].dma_buf_fd = -1;
    thiz->gdma_ctx[i].gdma_part_id = -1;
  }

  gst_base_src_set_live (GST_BASE_SRC (thiz), TRUE);
  gst_base_src_set_do_timestamp (GST_BASE_SRC (thiz), TRUE);
  gst_base_src_set_format (GST_BASE_SRC (thiz), GST_FORMAT_TIME);

  thiz->gst_clok_time = 0;
  thiz->is_clock_started = 0;

  thiz->give_clock = DEFAULT_PROVIDE_CLOCK;

  thiz->dmabuf_allocator = gst_dmabuf_allocator_new();

  thiz->gdma_yuv_pitch = 0;
  thiz->gdma_me0_pitch = 0;
  thiz->gdma_me1_pitch = 0;

  if (thiz->give_clock)
    GST_OBJECT_FLAG_SET (thiz, GST_ELEMENT_FLAG_PROVIDE_CLOCK);
  else
    GST_OBJECT_FLAG_UNSET (thiz, GST_ELEMENT_FLAG_PROVIDE_CLOCK);

  thiz->total_yuv_query_count = 0;
  thiz->logged_stride_once = FALSE;

#ifdef DEBUG_USE_FAKE_PTS
  thiz->fake_pts = 0;
  thiz->fake_pts_frame_tick = 33333333;
#endif

  thiz->provided_clock = gst_amba_hw_clock_obtain();
  if (!thiz->provided_clock) {
    GST_ERROR("Failed to create hardware clock");
    thiz->give_clock = FALSE;
  }

  thiz->clock = create_clock (NULL,
      thiz->provided_clock->outfreq / 10000, GST_SECOND / 10000);
  if (!thiz->clock) {
    DPRINT_ERROR("clock create failed\n");
    thiz->is_clock_setup = 0;
  } else {
    thiz->is_clock_setup = 1;
  }

  /* check iav state */
  if (thiz->iav_ctx->iav_al.f_check_iav_state(thiz->iav_ctx->iav_fd, &thiz->decode_mode) < 0) {
    GST_ERROR("check_iav_state failed\n");
    return;
  }

  /* prepare system configuration */
  if (!thiz->decode_mode) {
    if (thiz->iav_ctx->iav_al.f_get_resource_info(thiz->iav_ctx->iav_fd, &thiz->res_info) < 0) {
      GST_ERROR("get_resource_info failed\n");
      return;
    }
  }

}

static void gst_amba_camsrc2_finalize (GObject *gobject)
{
  GstAmbaCamsrc2 * thiz = GST_AMBA_CAMSRC2 (gobject);

  GST_INFO("camsrc finalize: total yuv query count: %llu\n", (unsigned long long)thiz->total_yuv_query_count);

  if (thiz->iav_ctx) {
    for (int i = 0; i < DAMBA_MAX_YUV_BUF_NUM; i++) {
      // we should have freed it in gst_amba_camsrc2_start
#if defined (BUILD_DSP_AMBA_V5)
      //thiz->iav_ctx->iav_al.f_gdma_free_buf(thiz->iav_ctx->iav_fd, &thiz->gdma_ctx[i]);
#elif defined (BUILD_DSP_AMBA_V6)
      //thiz->iav_ctx->iav_al.f_gdma_free_buf(thiz->iav_ctx->iav_fd, &thiz->gdma_ctx[i]);
#endif
    }

    release_iav_ctx (1);
    thiz->iav_ctx = NULL;
  }

  if (thiz->clock) {
    destroy_clock(thiz->clock);
    thiz->clock = NULL;
  }

  if (thiz->provided_clock) {
    gst_object_unref (thiz->provided_clock);
  }

  if (thiz->dmabuf_allocator) {
    gst_object_unref (thiz->dmabuf_allocator);
    thiz->dmabuf_allocator = NULL;
  }

#ifndef D_OS_AMRTOS
  if (thiz->dump_fd) {
    fclose(thiz->dump_fd);
    thiz->dump_fd = NULL;
  }
#endif

  G_OBJECT_CLASS (parent_class)->finalize (gobject);
}

static GstClock *
gst_amba_camsrc2_provide_clock (GstElement * element)
{
  GstAmbaCamsrc2 * src = GST_AMBA_CAMSRC2 (element);
  //return gst_system_clock_obtain ();
  GstClock *clock;

  GST_OBJECT_LOCK (src);

  if (!GST_OBJECT_FLAG_IS_SET (src, GST_ELEMENT_FLAG_PROVIDE_CLOCK)) {
    GST_DEBUG_OBJECT (src, "clock provide disabled");
    GST_OBJECT_UNLOCK (src);
    return NULL;
  }

  clock = GST_CLOCK_CAST (gst_object_ref (src->provided_clock));
  GST_OBJECT_UNLOCK (src);

  return clock;
}

static void
gst_amba_camsrc2_set_provide_clock (GstAmbaCamsrc2 * src, gboolean provide)
{
  GST_OBJECT_LOCK (src);
  if (provide)
    GST_OBJECT_FLAG_SET (src, GST_ELEMENT_FLAG_PROVIDE_CLOCK);
  else
    GST_OBJECT_FLAG_UNSET (src, GST_ELEMENT_FLAG_PROVIDE_CLOCK);
  GST_OBJECT_UNLOCK (src);
}

static gboolean
gst_amba_camsrc2_get_provide_clock (GstAmbaCamsrc2 * src)
{
  gboolean result;

  GST_OBJECT_LOCK (src);
  result = GST_OBJECT_FLAG_IS_SET (src, GST_ELEMENT_FLAG_PROVIDE_CLOCK);
  GST_OBJECT_UNLOCK (src);

  return result;
}

static guchar get_capture_type(const char *type)
{
  if (!strcmp(type, ECAPTURE_TYPE_NAME_PREVIEW)) {
    return CAPTURE_PREVIEW_BUFFER;
  } else if (!strcmp(type, ECAPTURE_TYPE_NAME_PYRAMID)) {
    return CAPTURE_PYRAMID_BUFFER;
  } else {
    DPRINT_ERROR("unsupported captured type(%s)\n", type);
    return CAPTURE_NONE;
  }

}

static void gst_amba_camsrc2_set_property (GObject *object, guint prop_id,
    const GValue *value, GParamSpec *pspec)
{
  GstAmbaCamsrc2 * thiz = GST_AMBA_CAMSRC2 (object);

  switch (prop_id) {

    case PROP_PIXEL_FORMAT: {
      const gchar *new_format = g_value_get_string (value);

      if (!new_format) {
        GST_WARNING_OBJECT(thiz, "Received NULL pixel format string");
        break;
      }
      g_strlcpy(thiz->str_pixel_format, new_format, sizeof(thiz->str_pixel_format));
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
      guint v = g_value_get_uint (value);
      if (v != thiz->buf_id)
        thiz->logged_stride_once = FALSE;
      thiz->buf_id = v;
      thiz->buf_map |= (1 << thiz->buf_id);
    }
    break;

    case PROP_DUMP_FILE: {
      strncpy(thiz->dump_file, g_value_get_string (value), DMAX_FILE_NAME_LENGTH);
    }
    break;

    case PROP_GDMA: {
      guchar v = g_value_get_uchar (value);
      if (v != thiz->gdma_copy_enable)
        thiz->logged_stride_once = FALSE;
      thiz->gdma_copy_enable = v;
    }
    break;

    case PROP_DMA_BUF: {
      if (g_value_get_uchar (value) > 0) {
        thiz->canvas_map_thru_dmabuf |= (1 << thiz->buf_id);
      } else {
        thiz->canvas_map_thru_dmabuf &= ~(1 << thiz->buf_id);
      }
    }
    break;

    case PROP_PROVIDE_CLOCK:
      gst_amba_camsrc2_set_provide_clock (thiz, g_value_get_boolean (value));
      thiz->give_clock = g_value_get_boolean (value);
      break;
    case PROP_CAPTURE_TYPE:
      strncpy(thiz->capture_type_name, g_value_get_string (value), 127);
      thiz->capture_type = get_capture_type(thiz->capture_type_name);
      break;
    case PROP_PYRAMID_LAYER: {
      thiz->pyramid_layer_id = g_value_get_uint (value);
      thiz->pyramid_buffer_map |= (1 << thiz->pyramid_layer_id);
    } break;
    case PROP_CHANNEL_ID: {
      thiz->current_channel = g_value_get_uint (value);
      if (/*thiz->current_channel >= CONFIG_AMBARELLA_MAX_CHANNEL_NUM || */
          thiz->current_channel >= thiz->res_info.channel_num) {
        GST_ERROR("Invalid channel id[%d], cannot excess channel_num[%d].",
        thiz->current_channel, thiz->res_info.channel_num);
      }
    } break;
    case PROP_DISCARD_PREV:
      thiz->discard_prev_frame = g_value_get_boolean (value);
      break;
    case PROP_CAPTURE_ME0:
      thiz->capture_me0 = g_value_get_uchar (value);
      break;
    case PROP_CAPTURE_ME1:
      thiz->capture_me1 = g_value_get_uchar (value);
      break;
    default: {
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
    break;
  }

  if (thiz->capture_type != CAPTURE_RAW_BUFFER && thiz->capture_type != CAPTURE_PYRAMID_BUFFER) {
    thiz->canvas_map_thru_dmabuf &= thiz->buf_map;
  }
}

static void gst_amba_camsrc2_get_property (GObject *object, guint prop_id,
    GValue *value, GParamSpec *pspec)
{
  GstAmbaCamsrc2 *thiz = GST_AMBA_CAMSRC2 (object);

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

    case PROP_DUMP_FILE: {
      g_value_set_string (value, thiz->dump_file);
    }
    break;

    case PROP_GDMA: {
      g_value_set_uchar (value, thiz->gdma_copy_enable);
    }
    break;

    case PROP_DMA_BUF: {
      g_value_set_uchar (value, (guchar) (!!(thiz->canvas_map_thru_dmabuf & (1 << thiz->buf_id))));
    }
    break;

    case PROP_PROVIDE_CLOCK:
      g_value_set_boolean (value, gst_amba_camsrc2_get_provide_clock (thiz));
      break;
    case PROP_CAPTURE_TYPE:
      g_value_set_string (value, thiz->capture_type_name);
      break;
    case PROP_PYRAMID_LAYER:
      g_value_set_uint (value, thiz->pyramid_layer_id);
      break;
    case PROP_CHANNEL_ID:
      g_value_set_uint (value, thiz->current_channel);
      break;
    case PROP_DISCARD_PREV:
      g_value_set_boolean (value, thiz->discard_prev_frame);
      break;
    case PROP_CAPTURE_ME0:
      g_value_set_uchar (value, thiz->capture_me0);
      break;
    case PROP_CAPTURE_ME1:
      g_value_set_uchar (value, thiz->capture_me1);
      break;
    default: {
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
    break;
  }
}

// refer to gst_video_test_src_decide_allocation
static gboolean
gst_amba_camsrc2_decide_allocation (GstBaseSrc * bsrc, GstQuery * query)
{
  GstAmbaCamsrc2 *thiz;
  GstAllocator *allocator;
  GstBufferPool *pool;
  gboolean update;
  guint size, cur_size, min, max;
  GstStructure *config;
  GstCaps *caps = NULL;

  thiz = GST_AMBA_CAMSRC2 (bsrc);

  guint yuv_p = AMBA_ALIGN_PITCH_DSP_AND_CAVALRY (thiz->pitch);
  guint m0_p = AMBA_ALIGN_PITCH_DSP_AND_CAVALRY (thiz->me0_pitch);
  guint m1_p = AMBA_ALIGN_PITCH_DSP_AND_CAVALRY (thiz->me1_pitch);
  gsize raw = 0;

  thiz->gdma_yuv_pitch = yuv_p;
  thiz->gdma_me0_pitch = m0_p;
  thiz->gdma_me1_pitch = m1_p;

  raw = (gsize) ROUND_UP(thiz->height, 16) * yuv_p * 3 / 2
      + (gsize) ROUND_UP(thiz->me0_height, 16) * m0_p
      + (gsize) ROUND_UP(thiz->me1_height, 16) * m1_p;
  cur_size = AMBA_ROUND_SIZE_ODD_DSP_PITCH_UNITS (raw);

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);

    /* adjust size */
    size = MAX (size, cur_size);
    update = TRUE;
  } else {
    pool = NULL;
    size = cur_size;
    min = max = 0;
    update = FALSE;
  }
  allocator = gst_amba_cavalry_allocator_get();
  if (!allocator)
  {
    GST_WARNING("gst_amba_cavalry_allocator_get return NULL, use default!\n");
  }

  /* no downstream pool, make our own */
  if (pool == NULL){
    pool = gst_buffer_pool_new ();
    GST_INFO("Make bufferpool for AmbaCamsrc2 !\n" );
  }

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_allocator (config, allocator, NULL);

  gst_query_parse_allocation (query, &caps, NULL);
  if (caps)
    gst_buffer_pool_config_set_params (config, caps, size, min, max);

  if (gst_query_find_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL)) {
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);
  }
  gst_buffer_pool_set_config (pool, config);

  if (update)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  if (pool)
    gst_object_unref (pool);
  if (allocator)
  {
    gst_object_unref(allocator);
    return TRUE;
  }
  return GST_BASE_SRC_CLASS (parent_class)->decide_allocation (bsrc, query);

}

/* GstElement vmethod implementations */
static GstFlowReturn
gst_amba_camsrc2_create (GstPushSrc * psrc,
    GstBuffer ** outbuf)
{
  GstAmbaCamsrc2 * thiz = GST_AMBA_CAMSRC2 (psrc);
  GstBaseSrc * bsrc = GST_BASE_SRC (psrc);
  GstFlowReturn flow_ret = GST_FLOW_OK;
  GstBuffer * p_out_buf = NULL;
  GstBufferPool *pool = gst_base_src_get_buffer_pool (bsrc);
  GstMemory *mem0 = NULL, *mem1 = NULL;
  GstMapInfo map0;
  GstVideoMeta *video_meta = NULL;
  AmbaPrivateDataMeta *meta = NULL;
#if 0
  GstAllocator * allocator = gst_amba_cavalry_allocator_get();
  if (!allocator)
  {
    GST_WARNING("gst_amba_cavalry_allocator_get return NULL, use default allocator!\n");
  }
#endif
  iav_ctx_t * iav_ctx = thiz->iav_ctx;
  iav_al_t * iav_al = &iav_ctx->iav_al;
  int ret = 0;
  gsize me0_size = 0, me1_size = 0;
  gsize offset[GST_VIDEO_MAX_PLANES] = {0};
  gint stride[GST_VIDEO_MAX_PLANES] = {0};

  amba_dsp_query_yuv_buffer_t yuv_buf;
  iav_release_canvas_cfg_t canvas_cfg = {0};
  guint cur_id = 0;
  GstClockTimeDiff cur_pts = GST_CLOCK_TIME_NONE;
  guint dma_size = 0;

#ifdef DEBUG_USE_HWTIMER_AS_PTS
  GstClockTimeDiff cur_hw_timestamp = 0;
#endif


  while (1) {
    memset(&yuv_buf, 0x0, sizeof(yuv_buf));
    yuv_buf.capture_select = thiz->capture_type;
    switch (thiz->capture_type) {
      case CAPTURE_PREVIEW_BUFFER: {
        cur_id = thiz->buf_id;
        amba_canvas_opt_t *opt = &yuv_buf.canvas_options;
        opt->canvas_num = thiz->res_info.canvas_num;
        opt->canvas_buffer_map = thiz->buf_map;
        opt->capture_me0 = thiz->capture_me0;
        opt->capture_me1 = thiz->capture_me1;
        memcpy(opt->canvas_mf_enable, thiz->res_info.canvas_mf_enable, IAV_MAX_CANVAS_BUF_NUM);
        memcpy(opt->canvas_ext_mem_enable, thiz->res_info.canvas_ext_mem_enable, IAV_MAX_CANVAS_BUF_NUM);
        memcpy(opt->canvas_yuv_buffer_disable, thiz->res_info.canvas_yuv_buffer_disable, IAV_MAX_CANVAS_BUF_NUM);
        memcpy(opt->canvas_me_buffer_disable, thiz->res_info.canvas_me_buffer_disable, IAV_MAX_CANVAS_BUF_NUM);
      } break;
      case CAPTURE_PYRAMID_BUFFER: {
        cur_id = thiz->pyramid_layer_id;
        amba_pyramid_opt_t *opt = &yuv_buf.pyramid_options;
        opt->channel_id = thiz->current_channel;
        opt->pyramid_buffer_map = thiz->pyramid_buffer_map;
        memcpy(opt->pyramid_manual_feed, thiz->res_info.pyramid_manual_feed, IAV_MAX_CHANNEL_NUM);
        memcpy(opt->pyramid_ext_mem, thiz->res_info.pyramid_ext_mem, IAV_MAX_CHANNEL_NUM);
      } break;
      default:
        GST_ERROR("unknown captured type: %d.", thiz->capture_type);
        flow_ret = GST_FLOW_ERROR;
        break;
    }

    yuv_buf.canvas_map_thru_dmabuf = thiz->canvas_map_thru_dmabuf;
    yuv_buf.decode_mode = thiz->decode_mode;
    // TODO: don't need to hack the gdma_ctx
    if (thiz->gdma_copy_enable) {
      /* alloc buffer then get the memory */
      flow_ret = gst_buffer_pool_acquire_buffer(pool, &p_out_buf, NULL);
      if (flow_ret != GST_FLOW_OK)
        break;
      mem0 = gst_buffer_get_memory(p_out_buf, 0);
      if (!mem0)
      {
        GST_ERROR("Failed to access memory at index 0.");
        gst_buffer_unref (p_out_buf);
        flow_ret = GST_FLOW_ERROR;
        break;
      }

      memset(&map0, 0x0, sizeof(GstMapInfo));
      if (!gst_memory_map(mem0, &map0, GST_MAP_READWRITE))
      {
        GST_ERROR("Failed to map memory at index 0.");
        flow_ret = GST_FLOW_ERROR;
        gst_memory_unref(mem0);
        gst_buffer_unref (p_out_buf);
        break;
      }
      thiz->gdma_ctx[cur_id].gdma_buf = (unsigned char *) map0.data;
      thiz->gdma_ctx[cur_id].gdma_buf_size = map0.size;
      thiz->gdma_ctx[cur_id].dma_buf_fd = gst_amba_cavalry_memory_get_fd(mem0);
      thiz->gdma_ctx[cur_id].dst_yuv_pitch = thiz->gdma_yuv_pitch;
      thiz->gdma_ctx[cur_id].dst_me0_pitch = thiz->gdma_me0_pitch;
      thiz->gdma_ctx[cur_id].dst_me1_pitch = thiz->gdma_me1_pitch;
      yuv_buf.gdma_copy_enable = thiz->gdma_copy_enable;
      yuv_buf.gdma_ctx = &thiz->gdma_ctx[0];
    } else {
      if (thiz->canvas_map_thru_dmabuf == 0) {
        GST_ERROR_OBJECT (thiz, "should setup dmabuf, canvas_map_thru_dmabuf: %x.", thiz->canvas_map_thru_dmabuf);
        flow_ret = GST_FLOW_ERROR;
        break;
      }
    }

    ret = iav_al->f_query_yuv_buffer(iav_ctx->iav_fd, &yuv_buf);
    if (ret == (int) COM_ECODE_BAD_STATE) {
      DPRINT_ERROR("f_query_yuv_buffer failed, ret 0x%08x\n", ret);
      if (p_out_buf) {
        gst_memory_unmap(mem0, &map0);
        gst_memory_unref(mem0);
        gst_buffer_unref (p_out_buf);
      }
      flow_ret = GST_FLOW_ERROR;
      break;
    } else if (ret == (int) COM_ECODE_TRY_AGAIN) {
      if (p_out_buf) {
        gst_memory_unmap(mem0, &map0);
        gst_memory_unref(mem0);
        gst_buffer_unref (p_out_buf);
      }
      goto IF_EOS_COM;
    }

    thiz->total_yuv_query_count++;

#ifdef DEBUG_USE_FAKE_PTS
    cur_pts = thiz->fake_pts;
    thiz->fake_pts += thiz->fake_pts_frame_tick;
#else

#ifdef DEBUG_USE_HWTIMER_AS_PTS
    cur_hw_timestamp = amba_hwtimer_get_raw_time(thiz->provided_clock);
    yuv_buf.yuv_ctx[cur_id].mono_pts = cur_hw_timestamp;
#endif
    if (thiz->discard_prev_frame) {
      if (yuv_buf.yuv_ctx[cur_id].mono_pts < thiz->first_mono_pts) {
        if (thiz->canvas_map_thru_dmabuf) {
          if (yuv_buf.yuv_ctx[cur_id].yuv_dma_buf_fd >= 0) {
            close(yuv_buf.yuv_ctx[cur_id].yuv_dma_buf_fd);
            yuv_buf.yuv_ctx[cur_id].yuv_dma_buf_fd = -1;
          }
          if (yuv_buf.me0_ctx[cur_id].me_dma_buf_fd >= 0) {
            close(yuv_buf.me0_ctx[cur_id].me_dma_buf_fd);
            yuv_buf.me0_ctx[cur_id].me_dma_buf_fd = -1;
            yuv_buf.me1_ctx[cur_id].me_dma_buf_fd = -1;
          }
        } else if (thiz->back_pressure_enable) {
          memset(&canvas_cfg, 0, sizeof(iav_release_canvas_cfg_t));
          canvas_cfg.canvas_id = cur_id;
          canvas_cfg.seq_num = yuv_buf.yuv_ctx[cur_id].seq_num;
          if (thiz->iav_ctx->iav_al.f_release_canvas_buffer(thiz->iav_ctx->iav_fd, &canvas_cfg) < 0) {
            DPRINT_ERROR("f_release_canvas_buffer failed\n");
          }
        }
        if (p_out_buf) {
          gst_memory_unmap(mem0, &map0);
          gst_memory_unref(mem0);
          gst_buffer_unref (p_out_buf);
        }
        continue;
      }
    }

    if (!thiz->is_clock_started) {
      thiz->clock->base_src_time = yuv_buf.yuv_ctx[cur_id].mono_pts;
      thiz->is_clock_started = 1;
      DPRINT_NOTICE("base src PTS: %lu (90k)\n", thiz->clock->base_src_time);
    }
    cur_pts = get_cur_clock_dummy(thiz->clock, yuv_buf.yuv_ctx[cur_id].mono_pts);
#endif
    if (thiz->gdma_copy_enable) {
      stride[0] = (gint) thiz->gdma_yuv_pitch;
      stride[1] = (gint) thiz->gdma_yuv_pitch;
      me0_size = (gsize) ROUND_UP(yuv_buf.me0_ctx[cur_id].height, 16) * thiz->gdma_me0_pitch;
      me1_size = (gsize) ROUND_UP(yuv_buf.me1_ctx[cur_id].height, 16) * thiz->gdma_me1_pitch;
    } else {
      stride[0] = yuv_buf.yuv_ctx[cur_id].pitch;
      stride[1] = yuv_buf.yuv_ctx[cur_id].pitch;
      me0_size = yuv_buf.me0_ctx[cur_id].pitch * yuv_buf.me0_ctx[cur_id].height;
      me1_size = yuv_buf.me1_ctx[cur_id].pitch * yuv_buf.me1_ctx[cur_id].height;
    }

    if (thiz->gdma_copy_enable) {
      offset[0] = 0;
      offset[1] = (gsize) thiz->gdma_yuv_pitch * yuv_buf.yuv_ctx[cur_id].height;
      // we may need a reszie as the height is round up to 16
      gst_buffer_resize(p_out_buf, 0, offset[1] * 3 / 2 + me0_size + me1_size);

      meta = amba_buffer_add_private_data_meta(p_out_buf);
      if (meta == NULL) {
        GST_ERROR_OBJECT(thiz, "Failed to add custom meta to buffer - meta is NULL");

        if (thiz->canvas_map_thru_dmabuf) {
          if (yuv_buf.yuv_ctx[cur_id].yuv_dma_buf_fd >= 0) {
            close(yuv_buf.yuv_ctx[cur_id].yuv_dma_buf_fd);
            yuv_buf.yuv_ctx[cur_id].yuv_dma_buf_fd = -1;
          }
          if (yuv_buf.me0_ctx[cur_id].me_dma_buf_fd >= 0) {
            close(yuv_buf.me0_ctx[cur_id].me_dma_buf_fd);
            yuv_buf.me0_ctx[cur_id].me_dma_buf_fd = -1;
            yuv_buf.me1_ctx[cur_id].me_dma_buf_fd = -1;
          }
        } else if (thiz->back_pressure_enable) {
          memset(&canvas_cfg, 0, sizeof(iav_release_canvas_cfg_t));
          canvas_cfg.canvas_id = cur_id;
          canvas_cfg.seq_num = yuv_buf.yuv_ctx[cur_id].seq_num;
          if (thiz->iav_ctx->iav_al.f_release_canvas_buffer(thiz->iav_ctx->iav_fd, &canvas_cfg) < 0) {
            DPRINT_ERROR("f_release_canvas_buffer failed\n");
          }
        }

        gst_memory_unmap(mem0, &map0);
        gst_memory_unref(mem0);
        gst_buffer_unref (p_out_buf);
        flow_ret = GST_FLOW_ERROR;
        break;
      }
      meta->dsp_pts = yuv_buf.yuv_ctx[cur_id].dsp_pts;
      meta->mono_pts = yuv_buf.yuv_ctx[cur_id].mono_pts;
      meta->yuv_seq_num = yuv_buf.yuv_ctx[cur_id].seq_num;
      meta->private_data_count = 0;
      if (thiz->capture_me0) {
        meta->private_data[meta->private_data_count].index = 0;
        meta->private_data[meta->private_data_count].offset = yuv_buf.gdma_ctx[cur_id].me0_addr_offset;
        meta->private_data[meta->private_data_count].size = (guint) me0_size;
        meta->private_data[meta->private_data_count].width = yuv_buf.me0_ctx[cur_id].width;
        meta->private_data[meta->private_data_count].height = yuv_buf.me0_ctx[cur_id].height;
        meta->private_data[meta->private_data_count].pitch = thiz->gdma_me0_pitch;
        meta->private_data[meta->private_data_count].format = AMBA_PRIVATE_FORMAT_ME0;

        GST_DEBUG_OBJECT(thiz, "Private Meta entry %u: index=%u, offset=%u, size=%u, format=ME0, width=%u, height=%u, pitch=%u",
            meta->private_data_count,
            meta->private_data[meta->private_data_count].index,
            meta->private_data[meta->private_data_count].offset,
            meta->private_data[meta->private_data_count].size,
            meta->private_data[meta->private_data_count].width,
            meta->private_data[meta->private_data_count].height,
            meta->private_data[meta->private_data_count].pitch);

        meta->private_data_count ++;
      }

      if (thiz->capture_me1) {
        meta->private_data[meta->private_data_count].index = 0;
        meta->private_data[meta->private_data_count].offset = yuv_buf.gdma_ctx[cur_id].me1_addr_offset;
        meta->private_data[meta->private_data_count].size = (guint) me1_size;
        meta->private_data[meta->private_data_count].width = yuv_buf.me1_ctx[cur_id].width;
        meta->private_data[meta->private_data_count].height = yuv_buf.me1_ctx[cur_id].height;
        meta->private_data[meta->private_data_count].pitch = thiz->gdma_me1_pitch;
        meta->private_data[meta->private_data_count].format = AMBA_PRIVATE_FORMAT_ME1;

        GST_DEBUG_OBJECT(thiz, "Private Meta entry %u: index=%u, offset=%u, size=%u, format=ME1, width=%u, height=%u, pitch=%u",
            meta->private_data_count,
            meta->private_data[meta->private_data_count].index,
            meta->private_data[meta->private_data_count].offset,
            meta->private_data[meta->private_data_count].size,
            meta->private_data[meta->private_data_count].width,
            meta->private_data[meta->private_data_count].height,
            meta->private_data[meta->private_data_count].pitch);

        meta->private_data_count ++;
      }

      if (thiz->canvas_map_thru_dmabuf) {
        if (yuv_buf.yuv_ctx[cur_id].yuv_dma_buf_fd >= 0) {
          close(yuv_buf.yuv_ctx[cur_id].yuv_dma_buf_fd);
          yuv_buf.yuv_ctx[cur_id].yuv_dma_buf_fd = -1;
        }
        if (yuv_buf.me0_ctx[cur_id].me_dma_buf_fd >= 0) {
          close(yuv_buf.me0_ctx[cur_id].me_dma_buf_fd);
          yuv_buf.me0_ctx[cur_id].me_dma_buf_fd = -1;
          yuv_buf.me1_ctx[cur_id].me_dma_buf_fd = -1;
        }
      } else if (thiz->back_pressure_enable) {
        memset(&canvas_cfg, 0, sizeof(iav_release_canvas_cfg_t));
        canvas_cfg.canvas_id = cur_id;
        canvas_cfg.seq_num = yuv_buf.yuv_ctx[cur_id].seq_num;
        if (thiz->iav_ctx->iav_al.f_release_canvas_buffer(thiz->iav_ctx->iav_fd, &canvas_cfg) < 0) {
          DPRINT_ERROR("f_release_canvas_buffer failed\n");
          gst_memory_unmap(mem0, &map0);
          gst_memory_unref(mem0);
          gst_buffer_unref (p_out_buf);
          flow_ret = GST_FLOW_ERROR;
          break;
        }
      }

      gst_memory_unmap(mem0, &map0);
      gst_memory_unref(mem0);
    } else {
      offset[0] = yuv_buf.yuv_ctx[cur_id].y_addr_offset;
      offset[1] = yuv_buf.yuv_ctx[cur_id].uv_addr_offset;

      if (yuv_buf.yuv_ctx[cur_id].yuv_dma_buf_fd < 0) {
        GST_ERROR_OBJECT(thiz, "Invalid yuv DMA-BUF fd: %d", yuv_buf.yuv_ctx[cur_id].yuv_dma_buf_fd);
        if (yuv_buf.me0_ctx[cur_id].me_dma_buf_fd >= 0) {
          close(yuv_buf.me0_ctx[cur_id].me_dma_buf_fd);
          yuv_buf.me0_ctx[cur_id].me_dma_buf_fd = -1;
          yuv_buf.me1_ctx[cur_id].me_dma_buf_fd = -1;
        }
        flow_ret = GST_FLOW_ERROR;
        break;
      }
      p_out_buf = gst_buffer_new();
      dma_size = lseek(yuv_buf.yuv_ctx[cur_id].yuv_dma_buf_fd, 0, SEEK_END);
      mem0 = gst_dmabuf_allocator_alloc (thiz->dmabuf_allocator,
          yuv_buf.yuv_ctx[cur_id].yuv_dma_buf_fd, dma_size);

      gst_buffer_append_memory (p_out_buf, mem0);
      meta = amba_buffer_add_private_data_meta(p_out_buf);
      if (meta == NULL) {
        GST_ERROR_OBJECT(thiz, "Failed to add custom meta to buffer - meta is NULL");
        if (yuv_buf.me0_ctx[cur_id].me_dma_buf_fd >= 0) {
          close(yuv_buf.me0_ctx[cur_id].me_dma_buf_fd);
          yuv_buf.me0_ctx[cur_id].me_dma_buf_fd = -1;
          yuv_buf.me1_ctx[cur_id].me_dma_buf_fd = -1;
        }
        gst_buffer_unref (p_out_buf);
        flow_ret = GST_FLOW_ERROR;
        break;
      }
      meta->dsp_pts = yuv_buf.yuv_ctx[cur_id].dsp_pts;
      meta->mono_pts = yuv_buf.yuv_ctx[cur_id].mono_pts;
      meta->yuv_seq_num = yuv_buf.yuv_ctx[cur_id].seq_num;

      meta->private_data_count = 0;
      if (thiz->capture_me0) {
        if (yuv_buf.me0_ctx[cur_id].me_dma_buf_fd < 0) {
          GST_ERROR_OBJECT(thiz, "Invalid me DMA-BUF fd: %d", yuv_buf.yuv_ctx[cur_id].yuv_dma_buf_fd);
          gst_buffer_unref (p_out_buf);
          flow_ret = GST_FLOW_ERROR;
          break;
        }
        dma_size = lseek(yuv_buf.me0_ctx[cur_id].me_dma_buf_fd, 0, SEEK_END);
        mem1 = gst_dmabuf_allocator_alloc (thiz->dmabuf_allocator,
            yuv_buf.me0_ctx[cur_id].me_dma_buf_fd, dma_size);

        gst_buffer_append_memory (p_out_buf, mem1);

        meta->private_data[meta->private_data_count].index = 1;
        meta->private_data[meta->private_data_count].offset = yuv_buf.me0_ctx[cur_id].data_addr_offset;
        meta->private_data[meta->private_data_count].size = yuv_buf.me0_ctx[cur_id].pitch * yuv_buf.me0_ctx[cur_id].height;
        meta->private_data[meta->private_data_count].width = yuv_buf.me0_ctx[cur_id].width;
        meta->private_data[meta->private_data_count].height = yuv_buf.me0_ctx[cur_id].height;
        meta->private_data[meta->private_data_count].pitch = yuv_buf.me0_ctx[cur_id].pitch;
        meta->private_data[meta->private_data_count].format = AMBA_PRIVATE_FORMAT_ME0;

        GST_DEBUG_OBJECT(thiz, "Private Meta entry %u: index=%u, offset=%u, size=%u, format=ME0, width=%u, height=%u, pitch=%u",
            meta->private_data_count,
            meta->private_data[meta->private_data_count].index,
            meta->private_data[meta->private_data_count].offset,
            meta->private_data[meta->private_data_count].size,
            meta->private_data[meta->private_data_count].width,
            meta->private_data[meta->private_data_count].height,
            meta->private_data[meta->private_data_count].pitch);

        meta->private_data_count ++;
      }

      if (thiz->capture_me1) {
        if (!thiz->capture_me0) {
          if (yuv_buf.me1_ctx[cur_id].me_dma_buf_fd < 0) {
            GST_ERROR_OBJECT(thiz, "Invalid me DMA-BUF fd: %d", yuv_buf.yuv_ctx[cur_id].yuv_dma_buf_fd);
            gst_buffer_unref (p_out_buf);
            flow_ret = GST_FLOW_ERROR;
            break;
          }
          dma_size = lseek(yuv_buf.me1_ctx[cur_id].me_dma_buf_fd, 0, SEEK_END);
          mem1 = gst_dmabuf_allocator_alloc (thiz->dmabuf_allocator,
              yuv_buf.me1_ctx[cur_id].me_dma_buf_fd, dma_size);

          gst_buffer_append_memory (p_out_buf, mem1);
        }

        meta->private_data[meta->private_data_count].index = 1;
        meta->private_data[meta->private_data_count].offset = yuv_buf.me1_ctx[cur_id].data_addr_offset;
        meta->private_data[meta->private_data_count].size = yuv_buf.me1_ctx[cur_id].pitch * yuv_buf.me1_ctx[cur_id].height;
        meta->private_data[meta->private_data_count].width = yuv_buf.me1_ctx[cur_id].width;
        meta->private_data[meta->private_data_count].height = yuv_buf.me1_ctx[cur_id].height;
        meta->private_data[meta->private_data_count].pitch = yuv_buf.me1_ctx[cur_id].pitch;
        meta->private_data[meta->private_data_count].format = AMBA_PRIVATE_FORMAT_ME1;

        GST_DEBUG_OBJECT(thiz, "Private Meta entry %u: index=%u, offset=%u, size=%u, format=ME1, width=%u, height=%u, pitch=%u",
            meta->private_data_count,
            meta->private_data[meta->private_data_count].index,
            meta->private_data[meta->private_data_count].offset,
            meta->private_data[meta->private_data_count].size,
            meta->private_data[meta->private_data_count].width,
            meta->private_data[meta->private_data_count].height,
            meta->private_data[meta->private_data_count].pitch);

        meta->private_data_count ++;
      }

    }

    video_meta = gst_buffer_get_video_meta(p_out_buf);
    if (video_meta) {
      video_meta->flags = GST_VIDEO_FRAME_FLAG_NONE;
      video_meta->format = GST_VIDEO_FORMAT_NV12;
      video_meta->width = yuv_buf.yuv_ctx[cur_id].width;
      video_meta->height = yuv_buf.yuv_ctx[cur_id].height;
      video_meta->n_planes = 2;
      video_meta->offset[0] = offset[0];
      video_meta->offset[1] = offset[1];
      video_meta->stride[0] = stride[0];
      video_meta->stride[1] = stride[1];
      GST_LOG_OBJECT(thiz, "update existing video meta stride=(%d,%d) offset=(%" G_GSIZE_FORMAT ",%" G_GSIZE_FORMAT ")",
          stride[0], stride[1], offset[0], offset[1]);
    } else {
      gst_buffer_add_video_meta_full (p_out_buf, GST_VIDEO_FRAME_FLAG_NONE,
          GST_VIDEO_FORMAT_NV12,
          yuv_buf.yuv_ctx[cur_id].width,
          yuv_buf.yuv_ctx[cur_id].height,
          2, offset, stride);
      GST_LOG_OBJECT(thiz, "add new video meta stride=(%d,%d) offset=(%" G_GSIZE_FORMAT ",%" G_GSIZE_FORMAT ")",
          stride[0], stride[1], offset[0], offset[1]);
    }

    if (!thiz->logged_stride_once) {
      gint y_mem_fd = -1;
      if (p_out_buf && gst_buffer_n_memory (p_out_buf) > 0) {
        GstMemory *ym = gst_buffer_peek_memory (p_out_buf, 0);
        if (gst_is_amba_cavalry_memory (ym))
          y_mem_fd = gst_amba_cavalry_memory_get_fd (ym);
        else if (gst_is_fd_memory (ym))
          y_mem_fd = gst_fd_memory_get_fd (ym);
      }
      g_printerr (
          "[amba_camsrc2]: NV12 cur_id=%u %ux%u "
          "video_meta_stride=%d/%d iav_yuv_pitch=%u gdma=%u gdma_dst_pitch=%u mem_fd=%d\n",
          cur_id,
          yuv_buf.yuv_ctx[cur_id].width, yuv_buf.yuv_ctx[cur_id].height,
          stride[0], stride[1], (unsigned) yuv_buf.yuv_ctx[cur_id].pitch,
          (unsigned) thiz->gdma_copy_enable, thiz->gdma_yuv_pitch,
          y_mem_fd);
      thiz->logged_stride_once = TRUE;
    }

    GST_BUFFER_PTS (p_out_buf) = cur_pts;

    if (thiz->framerate > 0) {
      GST_BUFFER_DURATION (p_out_buf) = gst_util_uint64_scale_int (GST_SECOND, 1, thiz->framerate);
    } else {
      GST_BUFFER_DURATION (p_out_buf) = GST_BUFFER_PTS (p_out_buf) - thiz->last_pts;
    }

    GST_DEBUG_OBJECT(thiz, "canvas buffer id: %d, PTS: %ld, PTS diff: %ld, duration: %ld\n",
        cur_id, GST_BUFFER_PTS (p_out_buf),
        GST_BUFFER_PTS (p_out_buf) - thiz->last_pts,
        GST_BUFFER_DURATION (p_out_buf));

    thiz->last_pts = GST_BUFFER_PTS (p_out_buf);
    *outbuf = p_out_buf;

#ifndef D_OS_AMRTOS
    if (thiz->dump_num && thiz->dump_file[0] != '\0') {
      if (thiz->dump_fd == NULL) {
        thiz->dump_fd = fopen(thiz->dump_file, "wb");
        if (thiz->dump_fd == NULL) {
          DPRINT_ERROR("fopen() failed on %s\n", thiz->dump_file);
          gst_buffer_unref (p_out_buf);
          flow_ret = GST_FLOW_ERROR;
          break;
        }
      }
/*
      fwrite((unsigned char *) iav_ctx->map_dsp.base + yuv_buf.yuv_ctx[cur_id].y_addr_offset,
          sizeof(guchar), yuv_buf.yuv_ctx[cur_id].height * yuv_buf.yuv_ctx[cur_id].pitch, thiz->dump_fd);
      fwrite((unsigned char *) iav_ctx->map_dsp.base + yuv_buf.yuv_ctx[cur_id].uv_addr_offset,
          sizeof(guchar), yuv_buf.yuv_ctx[cur_id].pitch * ((yuv_buf.yuv_ctx[cur_id].height + 1) / 2), thiz->dump_fd);
*/
      fwrite((unsigned char *) map0.data, sizeof(guchar), offset[1] * 3 / 2 , thiz->dump_fd);
      thiz->dump_num--;
      if (thiz->dump_num == 0) {
        fclose(thiz->dump_fd);
        thiz->dump_fd = NULL;
      }
    }
#endif
    *outbuf = p_out_buf;
    flow_ret = GST_FLOW_OK;
    {
      AmbaPrivateDataMeta *pm = amba_buffer_get_private_data_meta(p_out_buf);
      if (pm) {
        GST_INFO_OBJECT(thiz,
            "AmbaPrivateDataMeta trace: camsrc2 OUT canvas_id=%u yuv_seq_num=%u dsp_pts=%u mono_pts=%"
            G_GUINT64_FORMAT " GST_PTS=%" GST_TIME_FORMAT,
            (unsigned) cur_id, pm->yuv_seq_num, pm->dsp_pts, pm->mono_pts,
            GST_TIME_ARGS(GST_BUFFER_PTS(p_out_buf)));
      }
    }
    break;
IF_EOS_COM:
    if (thiz->is_eos_com) {
      flow_ret = GST_FLOW_EOS;
      break;
    }
  }
  if (pool)
    gst_object_unref(pool);
  return flow_ret;
}

static GstStateChangeReturn
gst_amba_camsrc2_change_state (GstElement *element, GstStateChange transition)
{
  GstAmbaCamsrc2 * src = GST_AMBA_CAMSRC2 (element);
  GstStateChangeReturn ret= GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      gst_amba_hw_clock_reset (src->provided_clock, 0);
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING: {
      src->first_mono_pts = amba_hwtimer_get_raw_time(src->provided_clock);
      if (src->give_clock && src->provided_clock) {
        gst_element_post_message (element,
            gst_message_new_clock_provide (GST_OBJECT_CAST (element),
                GST_CLOCK_CAST (src->provided_clock), TRUE));
      }
      if (!src->is_clock_started && src->discard_prev_frame) {
        src->clock->base_src_time = src->first_mono_pts;
        src->is_clock_started = 1;
        DPRINT_NOTICE("discard frames before base src PTS: %lu (90k)\n", src->clock->base_src_time);
      }

    } break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      /*gst_element_post_message (element,
          gst_message_new_clock_lost (GST_OBJECT_CAST (element),
              gst_system_clock_obtain ()));*/
      if (src->give_clock && src->provided_clock) {
        gst_element_post_message (element,
            gst_message_new_clock_lost (GST_OBJECT_CAST (element),
                GST_CLOCK_CAST (src->provided_clock)));
      }
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }
  return ret;

}


static GstCaps *
gst_amba_camsrc2_get_caps (GstBaseSrc * bsrc, GstCaps * filter)
{
  GstAmbaCamsrc2 *src = GST_AMBA_CAMSRC2 (bsrc);
  GstBaseSrcClass *bclass = GST_BASE_SRC_GET_CLASS (bsrc);
  GstPadTemplate *pad_template;
  GstCaps *caps = NULL, *temp = NULL;
  GstStructure *structure;
  GstVideoFormat video_format;
  GValue format = { 0 };
  gint width = 0, height = 0, pitch = 0;

  if (src->width && src->height && src->pitch) {
    width = src->width;
    height = src->height;
    pitch = src->pitch;
    if (src->gdma_copy_enable) {
      pitch = AMBA_ALIGN_PITCH_DSP_AND_CAVALRY (src->pitch);
    }
  } else {
    width = AMBACAMSRC_DEFAULT_WIDTH;
    pitch = AMBACAMSRC_DEFAULT_WIDTH;
    height = AMBACAMSRC_DEFAULT_HEIGHT;
  }

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (bclass), "src");
  if (pad_template != NULL) {
    caps = gst_pad_template_get_caps (pad_template);
  } else {
    caps = gst_caps_new_empty ();
    video_format = GST_VIDEO_FORMAT_NV12;
    g_value_init (&format, G_TYPE_STRING);
    g_value_set_string (&format, gst_video_format_to_string (video_format));
    structure = gst_structure_new_empty ("video/x-raw");
    gst_structure_set_value (structure, "format", &format);
    gst_caps_append_structure (caps, structure);
    g_value_unset (&format);
  }

  temp = gst_caps_copy (caps);

  guint n_caps = gst_caps_get_size (caps);

  for (guint i = 0; i < n_caps; i++) {
    GstStructure *s = gst_caps_get_structure (temp, i);
    //gst_structure_set_value (s, "format", G_TYPE_STRING, AMBACAMSRC_DEFAULT_PIXEL_FORMAT, NULL);
    gst_structure_set (s, "width", G_TYPE_INT, width, NULL);
    gst_structure_set (s, "height", G_TYPE_INT, height, NULL);
    gst_structure_set (s, "stride", G_TYPE_INT, pitch, NULL);
    /* because the framerate is unknown */
    gst_structure_set (s, "framerate", GST_TYPE_FRACTION, src->framerate, 1, NULL);
    gst_structure_set (s, "pixel-aspect-ratio",
        GST_TYPE_FRACTION, 1, 1, NULL);
  }

  gst_caps_unref (caps);
  caps = temp;

  if (filter) {
    GstCaps *intersection;

    intersection =
        gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (caps);
    caps = intersection;
  }

  return caps;

}

static gboolean gst_amba_camsrc2_start (GstBaseSrc * bsrc)
{
  GstAmbaCamsrc2 *src = GST_AMBA_CAMSRC2 (bsrc);
  amba_resource_info_t resource_info = {0};

  if (src->iav_ctx->iav_al.f_get_resource_info(src->iav_ctx->iav_fd, &resource_info) < 0) {
    DPRINT_ERROR("f_get_resource_info failed\n");
    return FALSE;
  }
  switch (src->capture_type) {
    case CAPTURE_PREVIEW_BUFFER: {
       src->framerate = resource_info.canvas_fps[src->buf_id];
       src->back_pressure_enable = resource_info.back_pressure_enable[src->buf_id];
    } break;
    case CAPTURE_PYRAMID_BUFFER: {
       src->framerate = resource_info.pyramid_fps[src->pyramid_layer_id];
    } break;
    default:
      GST_ERROR("unknown captured type: %d.", src->capture_type);
      return FALSE;
  }

  if (src->width == 0 || src->height == 0 || src->pitch == 0) {
    switch (src->capture_type) {
      case CAPTURE_PREVIEW_BUFFER: {
        amba_canvas_info_t info;
        memset(&info, 0x0, sizeof(amba_canvas_info_t));
        info.canvas_id = src->buf_id;
        if (src->iav_ctx->iav_al.f_query_canvas_info(src->iav_ctx->iav_fd, &info) < 0) {
          GST_ERROR ("f_query_canvas_info failed.");
          return FALSE;
        }

        src->width = info.width;
        src->height = info.height;
        src->pitch = info.yuv_pitch;

        src->me0_width = info.me0_width;
        src->me0_height = info.me0_height;
        src->me0_pitch = info.me0_pitch;

        src->me1_pitch = info.me1_pitch;
        src->me1_width = info.me1_width;
        src->me1_height = info.me1_height;
      } break;
      case CAPTURE_PYRAMID_BUFFER: {
        amba_pyramid_info_t info;
        memset(&info, 0x0, sizeof(amba_pyramid_info_t));
        info.channel_id = src->current_channel;
        if (src->iav_ctx->iav_al.f_query_pyramid_info(src->iav_ctx->iav_fd, &info) < 0) {
          GST_ERROR ("f_query_pyramid_info failed.");
          return FALSE;
        }

        src->width = info.layer_size[src->pyramid_layer_id].width;
        src->height = info.layer_size[src->pyramid_layer_id].height;
        src->pitch = info.layer_size[src->pyramid_layer_id].pitch;
      } break;
      default:
        GST_ERROR("unknown captured type: %d.", src->capture_type);
        return FALSE;
    }
  }

  return TRUE;
}

static gboolean
gst_amba_camsrc2_send_event (GstElement * element, GstEvent * event)
{
  GstAmbaCamsrc2 *src = GST_AMBA_CAMSRC2 (element);

  const GstStructure *s;
  const gchar *tstr;
  gchar *sstr;

  GST_OBJECT_LOCK (src);

  tstr = gst_event_type_get_name (GST_EVENT_TYPE (event));

  if ((s = gst_event_get_structure (event)))
    sstr = gst_structure_to_string (s);
  else
    sstr = g_strdup ("");
  GST_DEBUG_OBJECT (src, "send event   ******* (%s:%s) E (type: %s (%d), %s) %p\n",
      GST_DEBUG_PAD_NAME (GST_BASE_SRC_CAST (src)->srcpad),
      tstr, GST_EVENT_TYPE (event), sstr, event);
  g_free (sstr);

  if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
    src->is_eos_com = 1;
  }
  GST_OBJECT_UNLOCK (src);

  return GST_ELEMENT_CLASS (parent_class)->send_event (element, event);
}


