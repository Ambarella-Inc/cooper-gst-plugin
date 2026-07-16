/*
 * GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2022 PengXue Duan <<pxduan@ambarella.com>>
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
 * SECTION:element-ambavencoverlay
 * @title: amba_venc_overlay
 * @see_also: amba_overlay_src
 *
 * This element draws data from input memories on Ambarella overlay.
 *
 * <refsect2>
 * <title>draw bmp</title>
 * |[
 * gst-launch-1.0 amba_overlay_src osd=area_id:0,enable:1,roi:0.0.256.128,bg_color:8080eb00,buf_num:2,type:picture,bmp:/tmp/picture/Ambarella-256x128-8bit.bmp \
 * ! amba_venc_overlay stream_id=0 osd_offset =0 osd_size=81920 sync=false
 * ]|
 * </refsect2>
 */

#include <stdint.h>
#include <unistd.h>
#include <stdio.h>

#include "iav_ctx.h"
#include "common_err_code_c.h"
#include "debug_log.h"
#include "platform_al.h"
#ifdef GST_USE_IMG_SCALE
#include "cvlib_if.h"
#endif

#include "internal.h"
#include "overlay_common.h"
#include "amba_private_data.h"
#include "gstambavencoverlay.h"

//default max stream num 12->0xfff
#define DEFAULT_MAX_STREAM_ID           0xfff
//default max vin num 2->0x3
#define DEFAULT_MAX_VIN_ID              0x3

GST_DEBUG_CATEGORY_STATIC (gst_amba_venc_overlay_debug);
#define GST_CAT_DEFAULT gst_amba_venc_overlay_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_ENCODE_SET,
  PROP_STREAM_ID,
  PROP_OSD_OFFSET,
  PROP_OSD_SIZE,
  PROP_OSD_INSERT_ALWAYS,
  PROP_SYNC_WITH_PTS,
  PROP_NO_ROTATE,
};

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS_ALL))
    );


#define gst_amba_venc_overlay_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstAmbaVencOverlay, gst_amba_venc_overlay, GST_TYPE_VIDEO_SINK,
  GST_DEBUG_CATEGORY_INIT(gst_amba_venc_overlay_debug, "amba_venc_overlay", 0,
  "overlay sink"));

#define AMBAVENC_DEFAULT_ENC_FORMAT "stream_id:0"

static void gst_amba_venc_overlay_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_amba_venc_overlay_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static void gst_amba_venc_overlay_finalize (GObject *gobject);

static void gst_amba_venc_overlay_get_times (GstBaseSink * sink,
    GstBuffer * buffer, GstClockTime * start, GstClockTime * end);
static gboolean gst_amba_venc_overlay_start (GstBaseSink * sink);
static gboolean gst_amba_venc_overlay_stop (GstBaseSink * sink);
//static gboolean gst_amba_venc_overlay_set_caps (GstBaseSink * sink,
    //GstCaps * caps);
static GstFlowReturn gst_amba_venc_overlay_show_frame (GstVideoSink * sink,
    GstBuffer * buffer);

/* GObject vmethod implementations */

/* initialize the ambavencoverlay's class */
static void
gst_amba_venc_overlay_class_init (GstAmbaVencOverlayClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GstBaseSinkClass *base_sink_class = GST_BASE_SINK_CLASS (klass);
  GstVideoSinkClass *video_sink_class = GST_VIDEO_SINK_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_amba_venc_overlay_debug,
      "amba_venc_overlay", 0, "debug category for amba_venc_overlay element");


  gobject_class->set_property = gst_amba_venc_overlay_set_property;
  gobject_class->get_property = gst_amba_venc_overlay_get_property;
  gobject_class->finalize = gst_amba_venc_overlay_finalize;

  base_sink_class->get_times = GST_DEBUG_FUNCPTR (gst_amba_venc_overlay_get_times);
  base_sink_class->start = GST_DEBUG_FUNCPTR (gst_amba_venc_overlay_start);
  base_sink_class->stop = GST_DEBUG_FUNCPTR (gst_amba_venc_overlay_stop);
  //base_sink_class->set_caps = GST_DEBUG_FUNCPTR (gst_amba_venc_overlay_set_caps);

  video_sink_class->show_frame = GST_DEBUG_FUNCPTR (gst_amba_venc_overlay_show_frame);

  g_object_class_install_property (gobject_class, PROP_ENCODE_SET,
      g_param_spec_string ("enc", "Enc",
          "encode setting", AMBAVENC_DEFAULT_ENC_FORMAT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_STREAM_ID,
      g_param_spec_uint ("stream_id", "StreamId", "Provide stream id ?",
          0, IAV_STREAM_MAX_NUM_ALL, 0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_OSD_OFFSET,
      g_param_spec_ulong ("osd_offset", "OverlayOffset", "Provide overlay offset address ?",
          0, G_MAXULONG, 0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_OSD_SIZE,
      g_param_spec_ulong ("osd_size", "OverlaySize", "Provide overlay size for current stream ?",
          0, G_MAXULONG, 0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_OSD_INSERT_ALWAYS,
      g_param_spec_uchar ("insert_always", "InsertAlways", "Always insert OSD including skipped frame",
          0, 1, 0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_SYNC_WITH_PTS,
      g_param_spec_uchar ("sync_pts", "SyncPTS", "OSD sync with pts",
          0, 1, 0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_NO_ROTATE,
      g_param_spec_uchar ("norotate", "NoRotate", "OSD is consistent with stream orientation",
          0, 1, 0, G_PARAM_READWRITE));

  gst_element_class_add_static_pad_template (gstelement_class,
      &sink_factory);

  gst_element_class_set_static_metadata (gstelement_class,
      "Amba Venc Overlay",
      "Sink/Video",
      "Virtual video sink for amba venc overlay",
      "pxduan <pxduan@ambarella.com>");


}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad callback functions
 * initialize instance structure
 */
static void
gst_amba_venc_overlay_init (GstAmbaVencOverlay * self)
{
  priv_venc_overlay_ctx_t * filter = (priv_venc_overlay_ctx_t *) malloc (sizeof(priv_venc_overlay_ctx_t));

  if (!filter) {
    DPRINT_ERROR("no memory\n");
    return;
  }

  memset(filter, 0x0, sizeof(priv_venc_overlay_ctx_t));
  self->priv_ctx = filter;
  filter->score_limit = 0.7;
  filter->text_buffer_width = 320;
  filter->text_buffer_height = 64;
  filter->text_buffer_origin_x = 8;
  filter->text_buffer_origin_y = 20;

  filter->stream_id = -1;

  // iav context
  filter->iav_ctx = acquire_iav_ctx (1);
  if (!filter->iav_ctx) {
    DPRINT_ERROR("acquire_iav_ctx failed\n");
    free (filter);
    self->priv_ctx = NULL;
    return;
  }

}

static void gst_amba_venc_overlay_finalize (GObject *gobject)
{
  GstAmbaVencOverlay *self = GST_AMBAVENCOVERLAY (gobject);
  priv_venc_overlay_ctx_t * filter = self->priv_ctx;

  if (filter) {
    for (int i = 0; i < IAV_STREAM_MAX_NUM_ALL; ++i) {
      if (filter->stream_id_map & (1 << i)) {
        filter->overlay_set[i].overlay_insert.enable = 0;
        if (filter->iav_ctx->iav_al.f_set_overlay(filter->iav_ctx->iav_fd, &filter->overlay_set[i]) < 0) {
          DPRINT_ERROR("set_overlay error!\n");
        }
      }
#ifdef GST_USE_IMG_SCALE
      if (filter->img_ctx[i]) {
        destroy_common_img_scale_ctx(filter->img_ctx[i]);
      }
#endif
    }

    if (filter->enc_info) {
      g_free(filter->enc_info);
      filter->enc_info = NULL;
    }

    if (filter->iav_ctx) {
      release_iav_ctx (1);
      filter->iav_ctx = NULL;
    }

    free(filter);
    filter = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (gobject);
}


static void
gst_amba_venc_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAmbaVencOverlay *self = GST_AMBAVENCOVERLAY (object);
  priv_venc_overlay_ctx_t * filter = self->priv_ctx;
  iav_ctx_t * iav_ctx = filter->iav_ctx;
  int ret = 0;

  switch (prop_id) {
    case PROP_ENCODE_SET: {
      enc_config_t config;
      ret = parse_enc(iav_ctx->iav_fd, g_value_get_string (value), &config);
      if (ret) {
        DPRINT_ERROR("parse_enc (%s) failed\n",
          g_value_get_string (value));
        return;
      }
      if (!iav_ctx->iav_fd_opened) {
        DPRINT_ERROR("iav not opened\n");
        return;
      }
      ret = update_enc (iav_ctx->iav_fd, &config);
      if (ret) {
        DPRINT_ERROR("update encode setting (%s) failed\n",
          g_value_get_string (value));
        return;
      }

      amba_resource_info_t resource_info = {0};
      if (iav_ctx->iav_al.f_get_resource_info(iav_ctx->iav_fd, &resource_info) < 0) {
        DPRINT_ERROR("f_get_resource_info failed\n");
        return;
      } else {
        for (unsigned int i = 0; i < resource_info.max_stream_num; i++) {
          get_frame_rate(iav_ctx->iav_fd, &config, i);
          filter->fps[i] = config.stream_fps[i];
          filter->fps_d[i] = config.framerate_factor[i][1];
          filter->fps_n[i] = filter->fps[i] * filter->fps_d[i];//config.framerate_factor[i][0];
        }
      }
    } break;
    case PROP_STREAM_ID: {
      filter->stream_id = (gint) g_value_get_uint (value);
      if (filter->stream_id < 0 || filter->stream_id > IAV_STREAM_MAX_NUM_ALL) {
        GST_ERROR("Stream id %d must be in the range [0, %d).\n",
            filter->stream_id, IAV_STREAM_MAX_NUM_ALL);
        return;
      }
      filter->stream_id_map |= (1 << filter->stream_id);

      } break;
    case PROP_OSD_OFFSET: {
      filter->osd_offset[filter->stream_id] = g_value_get_ulong (value);
      if (filter->osd_offset[filter->stream_id] >= filter->iav_ctx->map_overlay.size) {
        GST_ERROR("overlay address offset %lu was out of range %lu.",
            filter->osd_offset[filter->stream_id],
            filter->iav_ctx->map_overlay.size);
        return;
      }
    } break;
    case PROP_OSD_SIZE: {
      filter->osd_size[filter->stream_id] = g_value_get_ulong (value);
      if (filter->osd_size[filter->stream_id] <= OVERLAY_YUV_OFFSET) {
        GST_ERROR("overlay size %lu <= clut size %u in stream %d.\n",
            filter->osd_size[filter->stream_id],
            OVERLAY_YUV_OFFSET,
            filter->stream_id);
        return;
      } else if (filter->osd_offset[filter->stream_id] + filter->osd_size[filter->stream_id] > filter->iav_ctx->map_overlay.size) {
        GST_ERROR("overlay address offset %lu + size %lu was out of range %lu in stream %d.",
            filter->osd_offset[filter->stream_id],
            filter->osd_size[filter->stream_id],
            filter->iav_ctx->map_overlay.size,
            filter->stream_id);
        return;
      }
    } break;
    case PROP_OSD_INSERT_ALWAYS:
      filter->overlay_set[filter->stream_id].overlay_insert.osd_insert_always = !!g_value_get_uchar (value);
      break;
    case PROP_SYNC_WITH_PTS:
      filter->overlay_set[filter->stream_id].sync_with_pts = !!g_value_get_uchar (value);
      break;
    case PROP_NO_ROTATE:
      if (g_value_get_uchar (value) > 0) {
        filter->overlay_set[filter->stream_id].rotate = AMBA_DRAW_AUTO_ROTATE;
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_amba_venc_overlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAmbaVencOverlay *self = GST_AMBAVENCOVERLAY (object);
  priv_venc_overlay_ctx_t * filter = self->priv_ctx;
  iav_ctx_t * iav_ctx = filter->iav_ctx;

  switch (prop_id) {
    case PROP_ENCODE_SET: {
      if (!iav_ctx->iav_fd_opened) {
        DPRINT_ERROR("iav not opened\n");
        return;
      }
      if (filter->enc_info) {
        g_free(filter->enc_info);
        filter->enc_info = NULL;
      }
      enc_config_t config;
      filter->enc_info = get_enc_info_string (iav_ctx->iav_fd, &config);
      if (filter->enc_info == NULL) {
        DPRINT_ERROR("convert encoding configure information to string failed\n");
        return;
      }

      g_value_set_string (value, filter->enc_info);

    } break;
    case PROP_STREAM_ID:
      g_value_set_uint (value, (guint) filter->stream_id);
      break;
    case PROP_OSD_OFFSET:
      g_value_set_ulong (value, filter->osd_offset[filter->stream_id]);
      break;
    case PROP_OSD_SIZE:
      g_value_set_ulong (value, filter->osd_size[filter->stream_id]);
      break;
    case PROP_OSD_INSERT_ALWAYS:
      g_value_set_uchar (value, filter->overlay_set[filter->stream_id].overlay_insert.osd_insert_always);
      break;
    case PROP_SYNC_WITH_PTS:
      g_value_set_uchar (value, filter->overlay_set[filter->stream_id].sync_with_pts);
      break;
    case PROP_NO_ROTATE:
      g_value_set_uchar (value, filter->overlay_set[filter->stream_id].rotate);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_amba_venc_overlay_get_times (GstBaseSink * sink, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end)
{
  GstAmbaVencOverlay *self = GST_AMBAVENCOVERLAY (sink);
  priv_venc_overlay_ctx_t * filter = self->priv_ctx;
#if 0
  if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer)) {
    *start = GST_BUFFER_TIMESTAMP (buffer);
    if (GST_BUFFER_DURATION_IS_VALID (buffer)) {
      *end = *start + GST_BUFFER_DURATION (buffer);
    } else {
      if (filter->info.fps_n > 0) {
        *end = *start +
            gst_util_uint64_scale_int (GST_SECOND, filter->info.fps_d,
            filter->info.fps_n);
      }
    }
  }
#endif
  if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer)) {
    *start = GST_BUFFER_TIMESTAMP (buffer);
    if (GST_BUFFER_DURATION_IS_VALID (buffer)) {
      *end = *start + GST_BUFFER_DURATION (buffer);
    } else {
      if ((filter->fps_n[filter->stream_id] > 0)
          && (filter->fps_d[filter->stream_id] >= 0)) {
        *end = *start +
            gst_util_uint64_scale_int (GST_SECOND, filter->fps_d[filter->stream_id],
            filter->fps_n[filter->stream_id]);
      }
    }
  } else {
    *start = -1;
    *end = -1;
  }
}

static int is_valid_rotate(unsigned char rotate)
{
  switch (rotate) {
    case AMBA_DRAW_NO_ROTATE_FLIP:
    case AMBA_DRAW_CW_ROTATE_90:
    case AMBA_DRAW_CW_ROTATE_180:
    case AMBA_DRAW_CW_ROTATE_270:
    case AMBA_DRAW_AUTO_ROTATE:
      return 1;
    default:
      return 0;
  }
}

#if defined (BUILD_DSP_AMBA_V6)

static int get_stream_overlay_pixel_format(iav_ctx_t *ctx, int stream_id)
{
  if (!ctx || !ctx->iav_al.f_get_stream_overlay_pixel_format)
    return -1;
  return ctx->iav_al.f_get_stream_overlay_pixel_format(ctx->iav_fd, stream_id);
}

static unsigned int get_pixel_size(unsigned int overlay_format)
{
  unsigned int byte_per_pixel = 0;

  if (overlay_format >= IAV_OVERLAY_FORMAT_32BIT_FIRST &&
    overlay_format < IAV_OVERLAY_FORMAT_32BIT_LAST) {
    byte_per_pixel = 4;
  } else if (overlay_format >= IAV_OVERLAY_FORMAT_16BIT_FIRST &&
    overlay_format < IAV_OVERLAY_FORMAT_16BIT_LAST) {
    byte_per_pixel = 2;
  } else {
    byte_per_pixel = 1;
  }

  return byte_per_pixel;
}

static int is_valid_pixel_format(int draw_fmt, int pixel_fmt)
{
  int ret = 0;
  switch (draw_fmt) {
    case AMBA_DRAW_FORMAT_8BIT_CLUT:
      if (pixel_fmt == IAV_OVERLAY_FORMAT_8BIT_CLUT8) {
        ret = 1;
      }
      break;
    case AMBA_DRAW_FORMAT_RGB565:
      if (pixel_fmt == IAV_OVERLAY_FORMAT_16BIT_RGB565) {
        ret = 1;
      }
      break;
    case AMBA_DRAW_FORMAT_UYV565:
      if (pixel_fmt == IAV_OVERLAY_FORMAT_16BIT_UYV565) {
        ret = 1;
      }
      break;
    case AMBA_DRAW_FORMAT_BGR565:
      if (pixel_fmt == IAV_OVERLAY_FORMAT_16BIT_BGR565) {
      ret = 1;
      }
      break;
    case AMBA_DRAW_FORMAT_AYUV4444:
      if (pixel_fmt == IAV_OVERLAY_FORMAT_16BIT_AYUV4444) {
      ret = 1;
      }
      break;
    case AMBA_DRAW_FORMAT_RGBA4444:
      if (pixel_fmt == IAV_OVERLAY_FORMAT_16BIT_RGBA4444) {
      ret = 1;
      }
      break;
    case AMBA_DRAW_FORMAT_BGRA4444:
      if (pixel_fmt == IAV_OVERLAY_FORMAT_16BIT_BGRA4444) {
      ret = 1;
      }
      break;
    case AMBA_DRAW_FORMAT_ABGR4444:
      if (pixel_fmt == IAV_OVERLAY_FORMAT_16BIT_ABGR4444) {
      ret = 1;
      }
      break;
    case AMBA_DRAW_FORMAT_ARGB4444:
      if (pixel_fmt == IAV_OVERLAY_FORMAT_16BIT_ARGB4444) {
      ret = 1;
      }
      break;
    case AMBA_DRAW_FORMAT_AYUV1555:
      if (pixel_fmt == IAV_OVERLAY_FORMAT_16BIT_AYUV1555) {
      ret = 1;
      }
      break;
    case AMBA_DRAW_FORMAT_YUV1555:
      if (pixel_fmt == IAV_OVERLAY_FORMAT_16BIT_YUV1555) {
      ret = 1;
      }
      break;
    case AMBA_DRAW_FORMAT_RGBA5551:
      if (pixel_fmt == IAV_OVERLAY_FORMAT_16BIT_RGBA5551) {
        ret = 1;
      }
      break;
    case AMBA_DRAW_FORMAT_BGRA5551:
      if (pixel_fmt == IAV_OVERLAY_FORMAT_16BIT_BGRA5551) {
        ret = 1;
      }
      break;
    case AMBA_DRAW_FORMAT_ABGR1555:
      if (pixel_fmt == IAV_OVERLAY_FORMAT_16BIT_ABGR1555) {
        ret = 1;
      }
      break;
    case AMBA_DRAW_FORMAT_ARGB1555:
      if (pixel_fmt == IAV_OVERLAY_FORMAT_16BIT_ARGB1555) {
        ret = 1;
      }
      break;
    case AMBA_DRAW_FORMAT_AYUV8888:
      if (pixel_fmt == IAV_OVERLAY_FORMAT_32BIT_AYUV8888) {
        ret = 1;
      }
      break;
    case AMBA_DRAW_FORMAT_RGBA8888:
      if (pixel_fmt == IAV_OVERLAY_FORMAT_32BIT_RGBA8888) {
        ret = 1;
      }
      break;
    case AMBA_DRAW_FORMAT_BGRA8888:
      if (pixel_fmt == IAV_OVERLAY_FORMAT_32BIT_BGRA8888) {
        ret = 1;
      }
      break;
    case AMBA_DRAW_FORMAT_ABGR8888:
      if (pixel_fmt == IAV_OVERLAY_FORMAT_32BIT_ABGR8888) {
        ret = 1;
      }
      break;
    case AMBA_DRAW_FORMAT_ARGB8888:
      if (pixel_fmt == IAV_OVERLAY_FORMAT_32BIT_ARGB8888) {
        ret = 1;
      }
      break;
    default:
      break;
  }
  return ret;
}

#endif

static gboolean
gst_amba_venc_overlay_open (GstAmbaVencOverlay *self)
{
  priv_venc_overlay_ctx_t * filter = self->priv_ctx;
  int iav_state = IAV_STATE_INIT;
  struct iav_system_resource resource;
  struct iav_stream_cfg stream_cfg;
  struct iav_stream_format *stream_format = NULL;
  enc_config_t config;

  if (!filter->iav_ctx->iav_fd_opened) {
    GST_ERROR("iav not opened\n");
    return FALSE;
  }

  /* IAV must be in ENOCDE or PREVIEW state */
  if (ioctl(filter->iav_ctx->iav_fd, IAV_IOC_GET_IAV_STATE, &iav_state) < 0) {
    perror("IAV_IOC_GET_IAV_STATE");
    return FALSE;
  }

  if ((iav_state != IAV_STATE_PREVIEW) &&
      (iav_state != IAV_STATE_ENCODING)) {
    GST_ERROR_OBJECT(self, "IAV must be in PREVIEW or ENCODE for text OSD.\n");
    return FALSE;
  }

  if (filter->stream_id < 0 || filter->stream_id >= IAV_STREAM_MAX_NUM_ALL) {
    GST_ERROR_OBJECT(self, "Stream id %d must be in the range [0, %d).\n",
        filter->stream_id, IAV_STREAM_MAX_NUM_ALL);
    return FALSE;
  }
  memset(&stream_cfg, 0, sizeof(stream_cfg));
  stream_cfg.id = filter->stream_id;
  stream_cfg.cid = IAV_STMCFG_FORMAT;
  if (ioctl(filter->iav_ctx->iav_fd, IAV_IOC_GET_STREAM_CONFIG, &stream_cfg) < 0) {
    perror("IAV_IOC_GET_STREAM_CONFIG");
    return FALSE;
  }

  stream_format = &stream_cfg.arg.format;
  filter->stream_params[filter->stream_id].stream_type = stream_format->type;
  filter->stream_params[filter->stream_id].enc_src_id = stream_format->enc_src_id;

  if (filter->overlay_set[filter->stream_id].rotate == AMBA_DRAW_AUTO_ROTATE) {
    filter->overlay_set[filter->stream_id].rotate = AMBA_DRAW_NO_ROTATE_FLIP;
    filter->overlay_set[filter->stream_id].rotate |= (stream_format->rotate_cw ? AMBA_DRAW_ROTATE_90 : 0);
    filter->overlay_set[filter->stream_id].rotate |= (stream_format->hflip ? AMBA_DRAW_HORIZONTAL_FLIP : 0);
    filter->overlay_set[filter->stream_id].rotate |= (stream_format->vflip ? AMBA_DRAW_VERTICAL_FLIP : 0);
    if (!is_valid_rotate(filter->overlay_set[filter->stream_id].rotate)) {
      GST_WARNING_OBJECT(self, "Stream %c Unknown rotate type. OSD is "
          "consistent with VIN orientation.\n", filter->stream_id);
      filter->overlay_set[filter->stream_id].rotate = AMBA_DRAW_NO_ROTATE_FLIP;
    }
  }

  if (filter->overlay_set[filter->stream_id].rotate & AMBA_DRAW_ROTATE_90) {
    filter->stream_params[filter->stream_id].encode_width = stream_format->enc_win.height;
    filter->stream_params[filter->stream_id].encode_height = stream_format->enc_win.width;
  } else {
    filter->stream_params[filter->stream_id].encode_width = stream_format->enc_win.width;
    filter->stream_params[filter->stream_id].encode_height = stream_format->enc_win.height;
  }

  memset(&resource, 0, sizeof(resource));
  resource.encode_mode = DSP_CURRENT_MODE;
  if (ioctl(filter->iav_ctx->iav_fd, IAV_IOC_GET_SYSTEM_RESOURCE, &resource) < 0) {
    perror("IAV_IOC_GET_SYSTEM_RESOURCE");
    return FALSE;
  }
  memset(&config, 0x0, sizeof(enc_config_t));
  for (unsigned int i = 0; i < resource.max_stream_num; i++) {
    get_frame_rate(filter->iav_ctx->iav_fd, &config, i);
    filter->fps[i] = config.stream_fps[i];
    filter->fps_d[i] = config.framerate_factor[i][1];
    filter->fps_n[i] = filter->fps[i] * filter->fps_d[i];//config.framerate_factor[i][0];
  }

#if defined (BUILD_DSP_AMBA_V5)
  if (resource.canvas_cfg[filter->stream_params[filter->stream_id].enc_src_id].enc_dummy_latency == 0 &&
      filter->overlay_set[filter->stream_id].sync_with_pts) {
    GST_ERROR_OBJECT(self, "Please configure encode dummy latency with test_encode first, and the value should be > 0!\n");
    return FALSE;
  }

  filter->pixel_size[filter->stream_id] = 1;

#elif defined (BUILD_DSP_AMBA_V6)
  struct iav_canvas_cfg canvas_cfg;
  memset(&canvas_cfg, 0, sizeof(canvas_cfg));
  canvas_cfg.canvas_id = filter->stream_params[filter->stream_id].enc_src_id;
  if (ioctl(filter->iav_ctx->iav_fd, IAV_IOC_GET_CANVAS_CONFIG, &canvas_cfg) < 0) {
    perror("IAV_IOC_GET_CANVAS_CONFIG");
    return FALSE;
  }
  if (canvas_cfg.enc_dummy_latency == 0 && filter->overlay_set[filter->stream_id].sync_with_pts) {
    GST_ERROR_OBJECT(self, "Please configure encode dummy latency with test_encode first, and the value should be > 0!\n");
    return FALSE;
  }

  filter->pixel_fmt[filter->stream_id] = get_stream_overlay_pixel_format(filter->iav_ctx, filter->stream_id);
  if (!(filter->pixel_fmt[filter->stream_id] >= IAV_OVERLAY_FORMAT_8BIT_FIRST &&
    filter->pixel_fmt[filter->stream_id] < IAV_OVERLAY_FORMAT_32BIT_LAST)) {
    GST_ERROR_OBJECT(self, "Get incorrect overlay pixel format [%d] in stream %d.",
    filter->pixel_fmt[filter->stream_id], filter->stream_id);
    return FALSE;
  }

  filter->pixel_size[filter->stream_id] = get_pixel_size(filter->pixel_fmt[filter->stream_id]);
#endif

  if (filter->osd_size[filter->stream_id] <= OVERLAY_YUV_OFFSET) {
    GST_ERROR("overlay size %lu <= clut size %u in stream %d.\n",
        filter->osd_size[filter->stream_id],
        OVERLAY_YUV_OFFSET,
        filter->stream_id);
    return FALSE;
  } else if (filter->osd_offset[filter->stream_id] + filter->osd_size[filter->stream_id] > filter->iav_ctx->map_overlay.size) {
    GST_ERROR("overlay address offset %lu + size %lu was out of range %lu in stream %d.",
        filter->osd_offset[filter->stream_id],
        filter->osd_size[filter->stream_id],
        filter->iav_ctx->map_overlay.size,
        filter->stream_id);
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_amba_venc_overlay_close (GstAmbaVencOverlay *self)
{
  DUNUSED(self);
  return TRUE;
}

static gboolean gst_amba_venc_overlay_start (GstBaseSink * sink)
{
  GstAmbaVencOverlay *self = GST_AMBAVENCOVERLAY (sink);

  return gst_amba_venc_overlay_open(self);
}
static gboolean gst_amba_venc_overlay_stop (GstBaseSink * sink)
{
  GstAmbaVencOverlay *self = GST_AMBAVENCOVERLAY (sink);
  gst_amba_venc_overlay_close(self);

  return TRUE;
}

static int fill_overlay_data(priv_venc_overlay_ctx_t *thiz,
    unsigned int area_id, amba_overlay_area_attr_t *attr, guchar *content)
{
  int row, col, area_pitch, area_height, area_width, buf_id;
  u8 *dst, *tmp_dst, *src;
  guint pixel_size = thiz->pixel_size[thiz->stream_id];
  guint pix = 0;

  iav_set_overlay_t *overlay_set = &thiz->overlay_set[thiz->stream_id];

  buf_id = overlay_set->osd[area_id].buf_id + 1;
  buf_id = (buf_id >= attr->buf_num ? 0 : buf_id);
  overlay_set->overlay_insert.area[area_id].data_addr_offset = overlay_set->osd[area_id].buf_data[buf_id];
  dst = thiz->iav_ctx->map_overlay.base + overlay_set->osd[area_id].buf_data[buf_id];
  area_pitch = overlay_set->overlay_insert.area[area_id].pitch;
  area_height = overlay_set->overlay_insert.area[area_id].height;
  area_width = overlay_set->overlay_insert.area[area_id].width;

  memset(dst, AMBA_DRAW_CLUT_ENTRY_BACKGROUND, overlay_set->overlay_insert.area[area_id].total_size);

  switch (overlay_set->rotate) {
    case AMBA_DRAW_NO_ROTATE_FLIP: {
      src = content;
      if (area_pitch == attr->rect.pitch) {
        memcpy(dst, src, area_pitch * area_height);
      } else {
        for (row = 0; row < area_height; row++) {
          src = content + row * attr->rect.pitch;
          memcpy(dst, src, area_width * pixel_size);
          dst = dst + area_pitch;
        }
      }
    } break;
    case AMBA_DRAW_CW_ROTATE_90: {
      tmp_dst = dst;
      for (row = 0; row < area_height; row++) {
        src = content + (area_height - 1 - row) * pixel_size;
        dst = tmp_dst;
        for (col = 0; col < area_width; col++) {
          for (pix = 0; pix < pixel_size; pix++) {
            *dst = src[pix];
            dst++;
          }
          src += attr->rect.pitch;//area_height
        }
        tmp_dst =tmp_dst+ area_pitch;
      }
    } break;
    case AMBA_DRAW_CW_ROTATE_180: {
      tmp_dst = dst;
      for (row = 0; row < area_height; row++) {
        src = content + attr->rect.pitch * (area_height - 1) + (area_width - 1) * pixel_size - row * attr->rect.pitch;
        dst = tmp_dst;
        for (col = 0; col < area_width; col++) {
          for (pix = 0; pix < pixel_size; pix++) {
            *dst = src[pix];
            dst++;
          }
          src -= pixel_size;
        }
        tmp_dst = tmp_dst + area_pitch;
      }
    } break;
    case AMBA_DRAW_CW_ROTATE_270: {
      tmp_dst = dst;
      for (row = 0; row < area_height; row++) {
        src = content + (area_width - 1) * attr->rect.pitch + row * pixel_size;//(width - 1) * height
        dst = tmp_dst;
        for (col = 0; col < area_width; col++) {
          for (pix = 0; pix < pixel_size; pix++) {
            *dst = src[pix];
            dst++;
          }
          src -= attr->rect.pitch;//height
        }
        tmp_dst = tmp_dst + area_pitch;
      }
    } break;
    default:
      GST_ERROR("Unknown rotate type %u.", overlay_set->rotate);
      return -1;
  }
  overlay_set->osd[area_id].buf_id = buf_id;

  return 0;
}

/* chain function
 * this function does the actual processing
 */
static GstFlowReturn
gst_amba_venc_overlay_show_frame (GstVideoSink * sink, GstBuffer * buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;

  unsigned int n = 0, j = 0, buf_id = 0;//i = 0
  unsigned int num = 0;
  unsigned int dsp_pts = 0;
  GstMemory *mem0 = NULL, *mem = NULL;
  GstMapInfo map0, map;

  osd_header_param_t *header = NULL;

  iav_set_overlay_t *overlay_set = NULL;
  struct iav_overlay_area *area = NULL;
  osd_info_t *osd = NULL;
  gulong total_size = 0;
  gulong overlay_data_offset = 0;
  AmbaPrivateDataMeta *priv_meta = NULL;
  unsigned char update = 0;

  GstAmbaVencOverlay *self = GST_AMBAVENCOVERLAY (sink);
  priv_venc_overlay_ctx_t * filter = self->priv_ctx;

  GST_DEBUG_OBJECT (filter, "render ts %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_PTS (buffer)));


  do {

    num = gst_buffer_n_memory(buffer);

    mem0 = gst_buffer_get_memory(buffer, 0);
    if (!mem0) {
      GST_ERROR_OBJECT (sink, "Failed to access memory at index 0.");
      ret = GST_FLOW_ERROR;
      break;
    }

    memset(&map0, 0x0, sizeof(GstMapInfo));
    if (!gst_memory_map (mem0, &map0, GST_MAP_READ)) {
      GST_ERROR_OBJECT (sink, "Failed to map memory at index 0.");
      ret = GST_FLOW_ERROR;
      gst_memory_unref (mem0);
      break;
    }

    header = (osd_header_param_t *) map0.data;

    overlay_set = &filter->overlay_set[filter->stream_id];
    overlay_set->overlay_max_num = header->area_num;
    overlay_set->overlay_insert.id = filter->stream_id;
    overlay_set->overlay_insert.enable = 0;

    for (j = 0; j < header->area_num; j++) {
      if (header->attr[j].enable) {
        overlay_set->overlay_insert.enable = 1;
        break;
      }
    }

    for (j = 0; j < header->area_num; j++) {
      if (header->refresh || header->attr_change_flag[j] || header->data_change_flag[j]) {
        update = 1;
        break;
      }
    }

    if (overlay_set->overlay_insert.enable) {
#if defined (BUILD_DSP_AMBA_V6)
      if (!is_valid_pixel_format(header->draw_format, filter->pixel_fmt[filter->stream_id])) {
        GST_ERROR_OBJECT(sink, "The image format %d does not match the stream_%u overlay pixel format %d\n",
            header->draw_format, filter->stream_id, filter->pixel_fmt[filter->stream_id]);
      }
#else
      if (header->draw_format != AMBA_DRAW_FORMAT_8BIT_CLUT) {
        GST_ERROR_OBJECT(sink, "unsupported image format %d in stream %d\n",
            header->draw_format, filter->stream_id);
      }
#endif

      overlay_data_offset = filter->osd_offset[filter->stream_id] + OVERLAY_YUV_OFFSET;
      n = 1;
      for (j = 0, total_size = 0; j < header->area_num; j++) {
        if (header->refresh || header->attr_change_flag[j] || header->data_change_flag[j]) {
          if (n >= num) {
            GST_ERROR_OBJECT (sink, "no input memory for area %d.", j);
            ret = GST_FLOW_ERROR;
            goto __SETUP_AREA_FAILED;
          }

          mem = gst_buffer_get_memory(buffer, n);
          if (!mem) {
            GST_ERROR_OBJECT (sink, "Failed to access memory at index %u.", n);
            ret = GST_FLOW_ERROR;
            break;
          }
          memset(&map, 0x0, sizeof(GstMapInfo));
          if (!gst_memory_map (mem, &map, GST_MAP_READ)) {
            GST_ERROR_OBJECT (sink, "Failed to map memory at index %u.", n);
            ret = GST_FLOW_ERROR;
            gst_memory_unref (mem);
            break;
          }

          osd = &overlay_set->osd[j];
          area = &overlay_set->overlay_insert.area[j];
          osd->enable = header->attr[j].enable;
          if (header->refresh || header->attr_change_flag[j]) {
            osd->width = header->attr[j].rect.width;
            osd->height = header->attr[j].rect.height;
            osd->x = header->attr[j].rect.x;
            osd->y = header->attr[j].rect.y;
            if (osd->width <= 0 || osd->height <= 0) {
              GST_ERROR_OBJECT(sink, "Stream %d Area %d width[%d] or height"
                  "[%d] shall be positive.\n", filter->stream_id, j, osd->width, osd->height);
              ret = GST_FLOW_ERROR;
              goto __SETUP_AREA_FAILED;
            }

            if (overlay_set->rotate & AMBA_DRAW_CW_ROTATE_90) {
              area->width = osd->height = ROUND_DOWN(osd->height, OVERLAY_WIDTH_ALIGN);
              area->height = osd->width = ROUND_DOWN(osd->width, OVERLAY_HEIGHT_ALIGN);
            } else {
              area->width = osd->width = ROUND_DOWN(osd->width, OVERLAY_WIDTH_ALIGN);
              area->height = osd->height = ROUND_DOWN(osd->height, OVERLAY_HEIGHT_ALIGN);
            }
            switch (overlay_set->rotate) {
              case AMBA_DRAW_NO_ROTATE_FLIP:
                area->start_x = osd->x = ROUND_DOWN(osd->x, OVERLAY_X_OFFSET_ALIGN);
                area->start_y = osd->y = ROUND_DOWN(osd->y, OVERLAY_Y_OFFSET_ALIGN);
                break;
              case AMBA_DRAW_CW_ROTATE_90:
                area->start_x = osd->y = ROUND_DOWN(osd->y, OVERLAY_X_OFFSET_ALIGN);
                area->start_y = ROUND_DOWN(
                    filter->stream_params[filter->stream_id].encode_width - osd->x - osd->width, OVERLAY_Y_OFFSET_ALIGN);
                osd->x = filter->stream_params[filter->stream_id].encode_width - osd->width - area->start_y;
                break;
              case AMBA_DRAW_CW_ROTATE_180:
                area->start_x = ROUND_DOWN(
                    filter->stream_params[filter->stream_id].encode_width - osd->x - osd->width, OVERLAY_X_OFFSET_ALIGN);
                area->start_y = ROUND_DOWN(
                    filter->stream_params[filter->stream_id].encode_height - osd->y - osd->height, OVERLAY_Y_OFFSET_ALIGN);
                osd->x = filter->stream_params[filter->stream_id].encode_width - osd->width - area->start_x;
                osd->y = filter->stream_params[filter->stream_id].encode_height - osd->height -  area->start_y;
                break;
              case AMBA_DRAW_CW_ROTATE_270:
                area->start_x = ROUND_DOWN(
                    filter->stream_params[filter->stream_id].encode_height - osd->y - osd->height, OVERLAY_X_OFFSET_ALIGN);
                area->start_y = osd->x = ROUND_DOWN(osd->x, OVERLAY_Y_OFFSET_ALIGN);
                osd->y = filter->stream_params[filter->stream_id].encode_height - osd->height -  area->start_x;
                break;
              default:
                GST_ERROR_OBJECT(sink, "unknown rotate type %d", overlay_set->rotate);
                ret = GST_FLOW_ERROR;
                goto __SETUP_AREA_FAILED;
            }

            if (!area->width || !area->height) {
              GST_ERROR_OBJECT(sink, "The area width / area height cannot be smaller than %d / %d.\n", OVERLAY_WIDTH_ALIGN,
                  OVERLAY_HEIGHT_ALIGN);
              ret = GST_FLOW_ERROR;
              goto __SETUP_AREA_FAILED;
            }

            if ((int) (osd->x + osd->width) > filter->stream_params[filter->stream_id].encode_width ||
                  (int) (osd->y + osd->height) > filter->stream_params[filter->stream_id].encode_height) {
              GST_ERROR_OBJECT(sink, "The overlay start_x %u + width %u and start_y "
                  "%u + height %u is out of the stream width %d and "
                  "height %d.\n", osd->x, osd->width,
                  osd->y, osd->height, filter->stream_params[filter->stream_id].encode_width,
                  filter->stream_params[filter->stream_id].encode_height);
              ret = GST_FLOW_ERROR;
              goto __SETUP_AREA_FAILED;

            }
            area->pitch = ROUND_UP(ROUND_UP(area->width, OSD_BUF_WIDTH_ALIGN) * filter->pixel_size[filter->stream_id], OSD_BUF_PITCH_ALIGN);
            area->total_size = area->pitch * area->height;
            area->clut_addr_offset = filter->osd_offset[filter->stream_id] +
                j * OVERLAY_CLUT_SIZE;
            area->enable = osd->enable;
            osd->buf_id = 0;
            osd->buf_num = header->attr[j].buf_num;
            for (buf_id = 0; buf_id < osd->buf_num; buf_id++) {
              osd->buf_data[buf_id] = overlay_data_offset + total_size +
                area->total_size * buf_id;
            }
            total_size += area->total_size * osd->buf_num;
            if (total_size > (filter->osd_size[filter->stream_id] - OVERLAY_YUV_OFFSET)) {
              GST_ERROR_OBJECT(sink, "OSD buffer memory is not enough, please increase it! The total OSD size is %ld (should be <= %ld).\n",
                total_size, (filter->osd_size[filter->stream_id] - OVERLAY_YUV_OFFSET));
              ret = GST_FLOW_ERROR;
              goto __SETUP_AREA_FAILED;
            }
            memcpy(filter->iav_ctx->map_overlay.base + area->clut_addr_offset,
                map.data, OVERLAY_CLUT_SIZE);
          }

          if (fill_overlay_data(filter, j, &header->attr[j], map.data + OVERLAY_CLUT_SIZE) < 0) {
            GST_ERROR_OBJECT (sink, "fill_overlay_data failed at area %d.", j);
            ret = GST_FLOW_ERROR;
            gst_memory_unmap (mem, &map);
            gst_memory_unref (mem);
            goto __SETUP_AREA_FAILED;
          }

          gst_memory_unmap (mem, &map);
          gst_memory_unref (mem);
          n++;
        }
      }
    }

    if (update) {
      if (overlay_set->sync_with_pts) {
        if (filter->iav_ctx->iav_al.f_set_frame_sync(filter->iav_ctx->iav_fd, overlay_set) < 0) {
          GST_ERROR_OBJECT(sink, "f_set_frame_sync error!\n");
          ret = GST_FLOW_ERROR;
          goto __SETUP_AREA_FAILED;
        }
      } else {
        if (filter->iav_ctx->iav_al.f_set_overlay(filter->iav_ctx->iav_fd, overlay_set) < 0) {
          GST_ERROR_OBJECT(sink, "f_set_overlay error!\n");
          ret = GST_FLOW_ERROR;
          goto __SETUP_AREA_FAILED;
        }

      }
    }

    if (update && overlay_set->sync_with_pts) {
      priv_meta = amba_buffer_get_private_data_meta (buffer);
      if (priv_meta == NULL) {
        GST_ERROR_OBJECT (sink, "failed to get amba private meta data");
        ret = GST_FLOW_ERROR;
        break;
      }
      dsp_pts = priv_meta->dsp_pts;

      if (filter->iav_ctx->iav_al.f_apply_frame_sync(filter->iav_ctx->iav_fd, dsp_pts,
            (1U << filter->stream_id), 0) < 0) {
        GST_ERROR_OBJECT(sink, "f_apply_frame_sync error!\n");
        ret = GST_FLOW_ERROR;
      }
    }

__SETUP_AREA_FAILED:
    gst_memory_unmap (mem0, &map0);
    gst_memory_unref (mem0);
  }while(0);

  return ret;
}


