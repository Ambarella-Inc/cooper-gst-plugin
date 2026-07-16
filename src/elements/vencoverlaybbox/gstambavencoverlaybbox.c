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
 * @title: amba_venc_overlay_bbox
 * @see_also: mlinference
 *
 * This element draws bounding boxes from mlinference on Ambarella overlay.
 *
 * <refsect2>
 * <title>yolov5 with post-processing, drawing bboxes on overlay</title>
 * |[
 * gst-launch-1.0 amba_camsrc buf-id = 0 ! queue ! mlinference in_name = images out_name = 1037 out_name = 1017 out_name = 997 \
 * label = /tmp/nn/in/coco_class_names.txt model = /tmp/nn/model/onnx_yolov5s_cavalry.bin \
 * type = yolov5s conf_threshold = 0.25 nms = 0.45 top_k = 100 ! queue ! amba_venc_overlay_bbox stream_id = 0 \
 * osd_offset =0 osd_size=4163584 area=0 buf_num=2 font = /tmp/arial.ttf score_lmt = 0.25 sync=false
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
#ifdef BUILD_AMBARELLA_GST_DRAW_TEXT
#include "font_ft.h"
#include "draw_text.h"
#endif

#include "internal.h"
#include "overlay_common.h"
#include "gstmlinference.h"
#include "amba_private_data.h"
#include "gstambavencoverlaybbox.h"
#include "../overlaysrc/amba_draw_data_picture.h"


GST_DEBUG_CATEGORY_STATIC (gst_amba_venc_overlay_bbox_debug);
#define GST_CAT_DEFAULT gst_amba_venc_overlay_bbox_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_STREAM_ID,
  PROP_OSD_OFFSET,
  PROP_OSD_SIZE,
  PROP_OSD_INSERT_ALWAYS,
  PROP_OVERLAY_AREA_ID,
  PROP_ROI,
  PROP_BUF_NUM,
  PROP_SCORE_LIMIT,
  PROP_FONT,
  PROP_SYNC_WITH_PTS,
  PROP_BMP,
  PROP_BMP_AREA_ID,
  PROP_BMP_ROI,
};

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS_ALL))//to do
    );


#define gst_amba_venc_overlay_bbox_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstAmbaVencOverlayBbox, gst_amba_venc_overlay_bbox, GST_TYPE_VIDEO_SINK,
  GST_DEBUG_CATEGORY_INIT(gst_amba_venc_overlay_bbox_debug, "amba_venc_overlay_bbox", 0,
  "overlay sink for bounding box"));


static void gst_amba_venc_overlay_bbox_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_amba_venc_overlay_bbox_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static void gst_amba_venc_overlay_bbox_finalize (GObject *gobject);

static void gst_amba_venc_overlay_bbox_get_times (GstBaseSink * sink,
    GstBuffer * buffer, GstClockTime * start, GstClockTime * end);
static gboolean gst_amba_venc_overlay_bbox_start (GstBaseSink * sink);
static gboolean gst_amba_venc_overlay_bbox_stop (GstBaseSink * sink);
//static gboolean gst_amba_venc_overlay_bbox_set_caps (GstBaseSink * sink,
    //GstCaps * caps);
static GstFlowReturn gst_amba_venc_overlay_bbox_show_frame (GstVideoSink * sink,
    GstBuffer * buffer);

static unsigned short y_table[256] = {
  5, 191, 0, 191, 0, 191, 0, 192, 128, 255, 0, 255, 0, 255, 0, 255,
  0, 51, 102, 153, 204, 255, 0, 51, 102, 153, 204, 255, 0, 51, 102, 153,
  204, 255, 0, 51, 102, 153, 204, 255, 0, 51, 102, 153, 204, 255, 0, 51,
  102, 153, 204, 255, 0, 51, 102, 153, 204, 255, 0, 51, 102, 153, 204, 255,
  0, 51, 102, 153, 204, 255, 0, 51, 102, 153, 204, 255, 0, 51, 102, 153,
  204, 255, 0, 51, 102, 153, 204, 255, 0, 51, 102, 153, 204, 255, 0, 51,
  102, 153, 204, 255, 0, 51, 102, 153, 204, 255, 0, 51, 102, 153, 204, 255,
  0, 51, 102, 153, 204, 255, 0, 51, 102, 153, 204, 255, 0, 51, 102, 153,
  204, 255, 0, 51, 102, 153, 204, 255, 0, 51, 102, 153, 204, 255, 0, 51,
  102, 153, 204, 255, 0, 51, 102, 153, 204, 255, 0, 51, 102, 153, 204, 255,
  0, 51, 102, 153, 204, 255, 0, 51, 102, 153, 204, 255, 0, 51, 102, 153,
  204, 255, 0, 51, 102, 153, 204, 255, 0, 51, 102, 153, 204, 255, 0, 51,
  102, 153, 204, 255, 0, 51, 102, 153, 204, 255, 0, 51, 102, 153, 204, 255,
  0, 51, 102, 153, 204, 255, 0, 51, 102, 153, 204, 255, 0, 51, 102, 153,
  204, 255, 0, 51, 102, 153, 204, 255, 0, 17, 34, 51, 68, 85, 102, 119,
  136, 153, 170, 187, 204, 221, 238, 255, 0, 0, 0, 0, 0, 0, 204, 242,
};

static unsigned short u_table[256] = {
  4, 0, 191, 191, 0, 0, 191, 192, 128, 0, 255, 255, 0, 0, 255, 255,
  0, 0, 0, 0, 0, 0, 51, 51, 51, 51, 51, 51, 102, 102, 102, 102,
  102, 102, 153, 153, 153, 153, 153, 153, 204, 204, 204, 204, 204, 204, 255, 255,
  255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 51, 51, 51, 51, 51, 51,
  102, 102, 102, 102, 102, 102, 153, 153, 153, 153, 153, 153, 204, 204, 204, 204,
  204, 204, 255, 255, 255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 51, 51,
  51, 51, 51, 51, 102, 102, 102, 102, 102, 102, 153, 153, 153, 153, 153, 153,
  204, 204, 204, 204, 204, 204, 255, 255, 255, 255, 255, 255, 0, 0, 0, 0,
  0, 0, 51, 51, 51, 51, 51, 51, 102, 102, 102, 102, 102, 102, 153, 153,
  153, 153, 153, 153, 204, 204, 204, 204, 204, 204, 255, 255, 255, 255, 255, 255,
  0, 0, 0, 0, 0, 0, 51, 51, 51, 51, 51, 51, 102, 102, 102, 102,
  102, 102, 153, 153, 153, 153, 153, 153, 204, 204, 204, 204, 204, 204, 255, 255,
  255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 51, 51, 51, 51, 51, 51,
  102, 102, 102, 102, 102, 102, 153, 153, 153, 153, 153, 153, 204, 204, 204, 204,
  204, 204, 255, 255, 255, 255, 255, 255, 0, 17, 34, 51, 68, 85, 102, 119,
  136, 153, 170, 187, 204, 221, 238, 255, 0, 0, 0, 0, 0, 0, 0, 102,
};

static unsigned short v_table[256] = {
  3, 0, 0, 0, 191, 191, 191, 192, 128, 0, 0, 0, 255, 255, 255, 255,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 51, 51, 51, 51, 51, 51, 51, 51, 51, 51, 51, 51,
  51, 51, 51, 51, 51, 51, 51, 51, 51, 51, 51, 51, 51, 51, 51, 51,
  51, 51, 51, 51, 51, 51, 51, 51, 102, 102, 102, 102, 102, 102, 102, 102,
  102, 102, 102, 102, 102, 102, 102, 102, 102, 102, 102, 102, 102, 102, 102, 102,
  102, 102, 102, 102, 102, 102, 102, 102, 102, 102, 102, 102, 153, 153, 153, 153,
  153, 153, 153, 153, 153, 153, 153, 153, 153, 153, 153, 153, 153, 153, 153, 153,
  153, 153, 153, 153, 153, 153, 153, 153, 153, 153, 153, 153, 153, 153, 153, 153,
  204, 204, 204, 204, 204, 204, 204, 204, 204, 204, 204, 204, 204, 204, 204, 204,
  204, 204, 204, 204, 204, 204, 204, 204, 204, 204, 204, 204, 204, 204, 204, 204,
  204, 204, 204, 204, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 0, 17, 34, 51, 68, 85, 102, 119,
  136, 153, 170, 187, 204, 221, 238, 255, 0, 0, 0, 0, 0, 0, 0, 34,
};

/* Fill CLUT with full palette for bbox (predefined colors). */
static int fill_overlay_clut(priv_venc_overlay_bbox_ctx_t *thiz,
    unsigned long clut_addr_offset)
{
  unsigned int i = 0;
  amba_draw_clut_t *clut_data = (amba_draw_clut_t *)(thiz->iav_ctx->map_overlay.base + clut_addr_offset);
  for (i = 0; i < OVERLAY_CLUT_MAX_NUM; i++) {
    clut_data[i].v = v_table[i];
    clut_data[i].u = u_table[i];
    clut_data[i].y = y_table[i];
    clut_data[i].a = 255;
  }
  clut_data[AMBA_DRAW_CLUT_ENTRY_BACKGROUND].v = 128;
  clut_data[AMBA_DRAW_CLUT_ENTRY_BACKGROUND].u = 128;
  clut_data[AMBA_DRAW_CLUT_ENTRY_BACKGROUND].y = 235;
  clut_data[AMBA_DRAW_CLUT_ENTRY_BACKGROUND].a = 0;
  return 0;
}

/* Init CLUT for bitmap area only: background + empty slots so amba_draw_pic_data can add colors. */
static int fill_overlay_clut_bitmap_area(priv_venc_overlay_bbox_ctx_t *thiz,
    unsigned long clut_addr_offset)
{
  amba_draw_clut_t *clut_data = (amba_draw_clut_t *)(thiz->iav_ctx->map_overlay.base + clut_addr_offset);
  memset(clut_data, 0, OVERLAY_CLUT_SIZE);
  clut_data[AMBA_DRAW_CLUT_ENTRY_BACKGROUND].v = 128;
  clut_data[AMBA_DRAW_CLUT_ENTRY_BACKGROUND].u = 128;
  clut_data[AMBA_DRAW_CLUT_ENTRY_BACKGROUND].y = 235;
  clut_data[AMBA_DRAW_CLUT_ENTRY_BACKGROUND].a = 0;
  return 0;
}


/* GObject vmethod implementations */

/* initialize the ambavencoverlay's class */
static void
gst_amba_venc_overlay_bbox_class_init (GstAmbaVencOverlayBboxClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GstBaseSinkClass *base_sink_class = GST_BASE_SINK_CLASS (klass);
  GstVideoSinkClass *video_sink_class = GST_VIDEO_SINK_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_amba_venc_overlay_bbox_debug,
      "amba_venc_overlay_bbox", 0, "debug category for amba_venc_overlay_bbox element");


  gobject_class->set_property = gst_amba_venc_overlay_bbox_set_property;
  gobject_class->get_property = gst_amba_venc_overlay_bbox_get_property;
  gobject_class->finalize = gst_amba_venc_overlay_bbox_finalize;

  base_sink_class->get_times = GST_DEBUG_FUNCPTR (gst_amba_venc_overlay_bbox_get_times);
  base_sink_class->start = GST_DEBUG_FUNCPTR (gst_amba_venc_overlay_bbox_start);
  base_sink_class->stop = GST_DEBUG_FUNCPTR (gst_amba_venc_overlay_bbox_stop);
  //base_sink_class->set_caps = GST_DEBUG_FUNCPTR (gst_amba_venc_overlay_bbox_set_caps);

  video_sink_class->show_frame = GST_DEBUG_FUNCPTR (gst_amba_venc_overlay_bbox_show_frame);

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
  g_object_class_install_property (gobject_class, PROP_ROI,
      g_param_spec_string ("roi", "ROI", "roi of input ?",
          NULL, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_OVERLAY_AREA_ID,
      g_param_spec_uint ("area", "Area", "Set overlay current area id ?",
          0, MAX_OVERLAY_AREA_NUM, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_BUF_NUM,
      g_param_spec_uint ("buf_num", "BufNum", "Set buffer numbers for area ?",
          0, OSD_MAX_BUFFER_NUM, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_SCORE_LIMIT,
      g_param_spec_float ("score_lmt", "ScoreLimit", "Set box's score limit ?",
          0, 1, 0.7, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_FONT,
      g_param_spec_string ("font", "Font", "Set font *.ttf file ?",
          "arial.ttf", G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_SYNC_WITH_PTS,
      g_param_spec_uchar ("sync_pts", "SyncPTS", "OSD sync with pts",
          0, 1, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_BMP,
      g_param_spec_string ("bmp", "BMP", "Set BMP picture file path for overlay ?",
          NULL, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_BMP_AREA_ID,
      g_param_spec_uint ("bmp_area", "BmpArea", "Set overlay area id for BMP picture ?",
          0, MAX_OVERLAY_AREA_NUM, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_BMP_ROI,
      g_param_spec_string ("bmp_roi", "BmpROI", "roi of BMP area ?",
          NULL, G_PARAM_READWRITE));

  gst_element_class_add_static_pad_template (gstelement_class,
      &sink_factory);

  gst_element_class_set_static_metadata (gstelement_class,
      "Amba Venc Overlay for Boundingbox",
      "Sink/Video",
      "Virtual video sink for amba venc overlay with bounding box",
      "pxduan <pxduan@ambarella.com>");


}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad callback functions
 * initialize instance structure
 */
static void
gst_amba_venc_overlay_bbox_init (GstAmbaVencOverlayBbox * self)
{
  priv_venc_overlay_bbox_ctx_t * filter = (priv_venc_overlay_bbox_ctx_t *) malloc (sizeof(priv_venc_overlay_bbox_ctx_t));

  if (!filter) {
    DPRINT_ERROR("no memory\n");
    return;
  }

  memset(filter, 0x0, sizeof(priv_venc_overlay_bbox_ctx_t));
  self->priv_ctx = filter;
  filter->score_limit = 0.7;
  filter->text_buffer_width = 320;
  filter->text_buffer_height = 64;
  filter->text_buffer_origin_x = 8;
  filter->text_buffer_origin_y = 20;
  filter->display.font_size_w = 32;
  filter->display.font_size_h = 24;

  filter->display.border_thickness = 3;
  filter->display.box_color = BOX_COLOR;
  filter->display.text_fore_color = TEXT_FORE_COLOR;
  filter->display.text_back_color = TEXT_BACK_COLOR;

  filter->stream_id = -1;
  filter->area_id = 0;
  filter->bmp_area_id = 1;
  filter->bitmap_dirty = 1;  /* draw bitmap on first frame when bmp_file is set */
  filter->bitmap.buf = NULL;
  filter->bitmap.size = 0;
  filter->bmp_file[0] = '\0';

  for (int i = 0; i < IAV_STREAM_MAX_NUM_ALL; i++) {
    filter->overlay_set[i].sync_with_pts = 1;
  }

  // iav context
  filter->iav_ctx = acquire_iav_ctx (1);
  if (!filter->iav_ctx) {
    DPRINT_ERROR("acquire_iav_ctx failed\n");
    free(filter);
    self->priv_ctx = NULL;
    return;
  }

}

static void gst_amba_venc_overlay_bbox_finalize (GObject *gobject)
{
  GstAmbaVencOverlayBbox *self = GST_AMBAVENCOVERLAYBBOX (gobject);
  priv_venc_overlay_bbox_ctx_t * filter = self->priv_ctx;

  if (filter) {
    for (int i = 0; i < IAV_STREAM_MAX_NUM_ALL; ++i) {
      if (filter->stream_bit_map & (1 << i)) {
        iav_set_overlay_t *overlay_set = &filter->overlay_set[i];

        overlay_set->overlay_insert.enable = 0;
        if (overlay_set->sync_with_pts) {
          if (filter->last_dsp_pts[i]) {
            if (filter->iav_ctx->iav_al.f_set_frame_sync(filter->iav_ctx->iav_fd, overlay_set) < 0) {
              GST_ERROR("f_set_frame_sync error!\n");
              break;
            } else if (filter->iav_ctx->iav_al.f_apply_frame_sync(filter->iav_ctx->iav_fd,
                filter->last_dsp_pts[i], (1U << i), 1) < 0) {
              GST_ERROR("f_apply_frame_sync error!\n");
              break;
            }
          } else {
            if (filter->iav_ctx->iav_al.f_set_overlay(filter->iav_ctx->iav_fd, overlay_set) < 0) {
              GST_ERROR("set_overlay error!\n");
              break;
            }
          }
        } else {
          if (filter->iav_ctx->iav_al.f_set_overlay(filter->iav_ctx->iav_fd, overlay_set) < 0) {
            GST_ERROR("set_overlay error!\n");
            break;
          }
        }
      }

#ifdef GST_USE_IMG_SCALE
      if (filter->img_ctx[i]) {
        destroy_common_img_scale_ctx(filter->img_ctx[i]);
      }
#endif
    }

    if (filter->bitmap.buf) {
      free(filter->bitmap.buf);
      filter->bitmap.buf = NULL;
    }

    free(filter);
    filter = NULL;
  }

  if (filter && filter->iav_ctx) {
    release_iav_ctx(1);
  }

  G_OBJECT_CLASS (parent_class)->finalize (gobject);
}

static int parse_roi (const char *custom_properties, roi_info_t *roi)
{
  if (custom_properties) {
    char **options;
    unsigned int len = 0;

    options = g_strsplit (custom_properties, ".", -1);
    len = g_strv_length (options);

    if (len == 2) {
      roi->x = 0;
      roi->y = 0;
      roi->w = (guint) g_ascii_strtoll (options[0], NULL, 10);
      roi->h = (guint) g_ascii_strtoll (options[1], NULL, 10);
    } else if (len == 4) {
      roi->x = (guint) g_ascii_strtoll (options[0], NULL, 10);
      roi->y = (guint) g_ascii_strtoll (options[1], NULL, 10);
      roi->w = (guint) g_ascii_strtoll (options[2], NULL, 10);
      roi->h = (guint) g_ascii_strtoll (options[3], NULL, 10);
    } else {
      DPRINT_ERROR ("Invalid param, should be roi:offset_x.offset_y.width.height\n");
      return -1;
    }

    g_strfreev (options);

  } else {
    printf("no params\n");
    return -1;
  }

  return 0;
}

static void
gst_amba_venc_overlay_bbox_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAmbaVencOverlayBbox *self = GST_AMBAVENCOVERLAYBBOX (object);
  priv_venc_overlay_bbox_ctx_t * filter = self->priv_ctx;

  switch (prop_id) {
    case PROP_STREAM_ID: {
      filter->stream_id = (gint) g_value_get_uint (value);
      if (filter->stream_id < 0 || filter->stream_id > IAV_STREAM_MAX_NUM_ALL) {
        GST_ERROR("Stream id %d must be in the range [0, %d).\n",
            filter->stream_id, IAV_STREAM_MAX_NUM_ALL);
        return;
      }
      filter->overlay_set[filter->stream_id].overlay_insert.enable = 1;
      filter->stream_bit_map |= (1 << filter->stream_id);
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
      if (filter->osd_offset[filter->stream_id] + filter->osd_size[filter->stream_id] > filter->iav_ctx->map_overlay.size) {
        GST_ERROR("overlay address offset %lu + size %lu was out of range %lu.",
            filter->osd_offset[filter->stream_id],
            filter->osd_size[filter->stream_id],
            filter->iav_ctx->map_overlay.size);
        return;
      }
    } break;
    case PROP_OSD_INSERT_ALWAYS:
      filter->overlay_set[filter->stream_id].overlay_insert.osd_insert_always = !!g_value_get_uchar (value);
      break;
    case PROP_OVERLAY_AREA_ID: {
      filter->area_id = g_value_get_uint (value);
      if (filter->area_id > MAX_OVERLAY_AREA_NUM) {
        GST_ERROR("overlay area id %u was out of range [0, %d).", filter->area_id, MAX_OVERLAY_AREA_NUM);
        return;
      }
      filter->overlay_set[filter->stream_id].osd[filter->area_id].enable = 1;
      if (filter->overlay_set[filter->stream_id].overlay_max_num <= filter->area_id) {
        filter->overlay_set[filter->stream_id].overlay_max_num = filter->area_id + 1;
      }
    } break;
    case PROP_BUF_NUM: {
      unsigned int buf_num = g_value_get_uint (value);
      if (buf_num <= 0 || buf_num > OSD_MAX_BUFFER_NUM) {
        GST_ERROR("area buffer number %u was out of range (0, %d].", buf_num, OSD_MAX_BUFFER_NUM);
        return;
      }
      filter->overlay_set[filter->stream_id].osd[filter->area_id].buf_num = buf_num;
    } break;
    case PROP_ROI: {
      roi_info_t roi = {0};
      if (parse_roi(g_value_get_string (value), &roi) < 0) {
        DPRINT_ERROR("parse_roi (%s) failed\n", g_value_get_string (value));
        return;
      }
      filter->overlay_set[filter->stream_id].osd[filter->area_id].width = roi.w;
      filter->overlay_set[filter->stream_id].osd[filter->area_id].height = roi.h;
      filter->overlay_set[filter->stream_id].osd[filter->area_id].x = roi.x;
      filter->overlay_set[filter->stream_id].osd[filter->area_id].y = roi.y;
    } break;
    case PROP_SCORE_LIMIT:
      filter->score_limit = g_value_get_float (value);
      break;
    case PROP_FONT:
      strncpy(filter->font_file, g_value_get_string (value), DMAX_FILE_NAME_LENGTH - 1);
      break;
    case PROP_SYNC_WITH_PTS:
      filter->overlay_set[filter->stream_id].sync_with_pts = !!g_value_get_uchar (value);
      break;
    case PROP_BMP:
      strncpy(filter->bmp_file, g_value_get_string (value), DMAX_FILE_NAME_LENGTH - 1);
      filter->bmp_file[DMAX_FILE_NAME_LENGTH - 1] = '\0';
      filter->bitmap_dirty = 1;
      break;
    case PROP_BMP_AREA_ID: {
      unsigned int bmp_area_id = g_value_get_uint (value);
      if (bmp_area_id > MAX_OVERLAY_AREA_NUM) {
        GST_ERROR("bmp_area id %u was out of range [0, %d).", bmp_area_id, MAX_OVERLAY_AREA_NUM);
        return;
      }
      filter->bmp_area_id = (unsigned char) bmp_area_id;
      filter->overlay_set[filter->stream_id].osd[filter->bmp_area_id].enable = 1;
      if (filter->overlay_set[filter->stream_id].overlay_max_num <= filter->bmp_area_id) {
        filter->overlay_set[filter->stream_id].overlay_max_num = filter->bmp_area_id + 1;
      }
      filter->bitmap_dirty = 1;
    } break;
    case PROP_BMP_ROI: {
      roi_info_t roi = {0};
      if (parse_roi(g_value_get_string (value), &roi) < 0) {
        DPRINT_ERROR("parse_roi for bmp_area (%s) failed\n", g_value_get_string (value));
        return;
      }
      filter->overlay_set[filter->stream_id].osd[filter->bmp_area_id].width = roi.w;
      filter->overlay_set[filter->stream_id].osd[filter->bmp_area_id].height = roi.h;
      filter->overlay_set[filter->stream_id].osd[filter->bmp_area_id].x = roi.x;
      filter->overlay_set[filter->stream_id].osd[filter->bmp_area_id].y = roi.y;
      filter->bitmap_dirty = 1;
    } break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_amba_venc_overlay_bbox_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAmbaVencOverlayBbox *self = GST_AMBAVENCOVERLAYBBOX (object);
  priv_venc_overlay_bbox_ctx_t * filter = self->priv_ctx;

  switch (prop_id) {
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
    case PROP_OVERLAY_AREA_ID:
      g_value_set_uint (value, filter->area_id);
      break;
    case PROP_BUF_NUM:
      g_value_set_uint (value, filter->overlay_set[filter->stream_id].osd[filter->area_id].buf_num);
      break;
    case PROP_ROI: {
      char roi_str[256] = {0};
      snprintf(roi_str, sizeof(roi_str) - 1, "%d.%d.%d.%d",
          filter->overlay_set[filter->stream_id].osd[filter->area_id].x,
          filter->overlay_set[filter->stream_id].osd[filter->area_id].y,
          filter->overlay_set[filter->stream_id].osd[filter->area_id].width,
          filter->overlay_set[filter->stream_id].osd[filter->area_id].height);
      g_value_set_string (value, roi_str);
    }break;
    case PROP_SCORE_LIMIT:
      g_value_set_float (value, filter->score_limit);
      break;
    case PROP_FONT:
      g_value_set_string(value, filter->font_file);
      break;
    case PROP_SYNC_WITH_PTS:
      g_value_set_uchar (value, filter->overlay_set[filter->stream_id].sync_with_pts);
      break;
    case PROP_BMP:
      g_value_set_string (value, filter->bmp_file);
      break;
    case PROP_BMP_AREA_ID:
      g_value_set_uint (value, filter->bmp_area_id);
      break;
    case PROP_BMP_ROI: {
      char roi_str[256] = {0};
      snprintf(roi_str, sizeof(roi_str) - 1, "%d.%d.%d.%d",
          filter->overlay_set[filter->stream_id].osd[filter->bmp_area_id].x,
          filter->overlay_set[filter->stream_id].osd[filter->bmp_area_id].y,
          filter->overlay_set[filter->stream_id].osd[filter->bmp_area_id].width,
          filter->overlay_set[filter->stream_id].osd[filter->bmp_area_id].height);
      g_value_set_string (value, roi_str);
    } break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_amba_venc_overlay_bbox_get_times (GstBaseSink * sink, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end)
{
  GstAmbaVencOverlayBbox *self = GST_AMBAVENCOVERLAYBBOX (sink);
  priv_venc_overlay_bbox_ctx_t * filter = self->priv_ctx;

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
}


static gboolean
gst_amba_venc_overlay_bbox_open (GstAmbaVencOverlayBbox *self)
{
  priv_venc_overlay_bbox_ctx_t * filter = self->priv_ctx;
  int iav_state = IAV_STATE_INIT;
  struct iav_system_resource resource;
  struct iav_stream_cfg stream_cfg;
  struct iav_stream_format *stream_format = NULL;
  unsigned int canvas_map = 0;
  int i = 0;

#ifdef BUILD_AMBARELLA_GST_DRAW_TEXT
  if (!filter->p_text_font_buffer) {
    filter->p_text_font_buffer =
      (unsigned char *) malloc(filter->text_buffer_width * filter->text_buffer_height);

    if (!filter->p_text_font_buffer) {
      GST_ERROR_OBJECT(self, "no memory\n");
      return FALSE;
    }
  }

  if (access(filter->font_file, F_OK) == 0) {
    filter->p_font = setup_font(filter->font_file);
    if (!filter->p_font) {
      GST_ERROR_OBJECT(self, "create font fail\n");
      goto open_fail;
    }
    font_set_size(filter->p_font, filter->display.font_size_w, filter->display.font_size_h);
  } else {
    GST_ERROR_OBJECT(self, "font file (%s) not exists.\n", filter->font_file);
    goto open_fail;
  }
#endif


  /* IAV must be in ENOCDE or PREVIEW state */
  if (ioctl(filter->iav_ctx->iav_fd, IAV_IOC_GET_IAV_STATE, &iav_state) < 0) {
    perror("IAV_IOC_GET_IAV_STATE");
    goto open_fail;
  }

  if ((iav_state != IAV_STATE_PREVIEW) &&
      (iav_state != IAV_STATE_ENCODING)) {
    GST_ERROR_OBJECT(self, "IAV must be in PREVIEW or ENCODE for text OSD.\n");
    goto open_fail;
  }

  for (i = 0; i < IAV_STREAM_MAX_NUM_ALL; i++) {
    if (filter->stream_bit_map & (1 << i)) {
      memset(&stream_cfg, 0, sizeof(stream_cfg));
      stream_cfg.id = i;
      stream_cfg.cid = IAV_STMCFG_FORMAT;
      if (ioctl(filter->iav_ctx->iav_fd, IAV_IOC_GET_STREAM_CONFIG, &stream_cfg) < 0) {
        perror("IAV_IOC_GET_STREAM_CONFIG");
        goto open_fail;
      }

      stream_format = &stream_cfg.arg.format;
      filter->stream_params[i].stream_type = stream_format->type;
      filter->stream_params[i].enc_src_id = stream_format->enc_src_id;
      if (stream_format->rotate_cw == 0) {
        filter->stream_params[i].encode_width = stream_format->enc_win.width;
        filter->stream_params[i].encode_height = stream_format->enc_win.height;
      } else {
        filter->stream_params[i].encode_width = stream_format->enc_win.height;
        filter->stream_params[i].encode_height = stream_format->enc_win.width;
      }
      canvas_map |= (1 << filter->stream_params[i].enc_src_id);
    }
  }

  memset(&resource, 0, sizeof(resource));
  resource.encode_mode = DSP_CURRENT_MODE;
  if (ioctl(filter->iav_ctx->iav_fd, IAV_IOC_GET_SYSTEM_RESOURCE, &resource) < 0) {
    perror("IAV_IOC_GET_SYSTEM_RESOURCE");
    goto open_fail;
  }
#if defined (BUILD_DSP_AMBA_V5)
  for (i = 0; i < IAV_MAX_CANVAS_BUF_NUM; i++) {
    if (canvas_map & (1 << i)) {
      if (resource.canvas_cfg[i].enc_dummy_latency == 0) {
        GST_ERROR_OBJECT(self, "Please configure encode dummy latency with test_encode first, and the value should be > 0!\n");
        goto open_fail;
      }
    }
  }

#elif defined (BUILD_DSP_AMBA_V6)
  struct iav_canvas_cfg canvas_cfg;
  for (i = 0; i < IAV_MAX_CANVAS_BUF_NUM; i++) {
    if (canvas_map & (1 << i)) {
      memset(&canvas_cfg, 0, sizeof(canvas_cfg));
      canvas_cfg.canvas_id = i;
      if (ioctl(filter->iav_ctx->iav_fd, IAV_IOC_GET_CANVAS_CONFIG, &canvas_cfg) < 0) {
        perror("IAV_IOC_GET_CANVAS_CONFIG");
        goto open_fail;
      }
      if (canvas_cfg.enc_dummy_latency == 0) {
        GST_ERROR_OBJECT(self, "Please configure encode dummy latency with test_encode first, and the value should be > 0!\n");
        goto open_fail;
      }
    }
  }
#endif

  if (filter->stream_id < 0) {
    GST_ERROR_OBJECT(self, "not specify stream id!\n");
    goto open_fail;
  }

  struct iav_overlay_area *area = NULL;
  osd_info_t *osd = NULL;
  unsigned int j = 0, buf_id = 0;
  unsigned long total_size = 0;
  unsigned long overlay_data_offset = 0;
  iav_set_overlay_t *overlay_set = &filter->overlay_set[filter->stream_id];

  overlay_set->overlay_insert.id = filter->stream_id;

  if (overlay_set->overlay_insert.enable) {
    if (filter->osd_size[filter->stream_id] <= OVERLAY_YUV_OFFSET) {
      GST_ERROR_OBJECT(self, "overlay size %lu <= clut size %u in stream %d.\n",
          filter->osd_size[filter->stream_id],
          OVERLAY_YUV_OFFSET,
          filter->stream_id);
      return FALSE;
    } else if (filter->osd_offset[filter->stream_id] +
        filter->osd_size[filter->stream_id] > filter->iav_ctx->map_overlay.size) {
      GST_ERROR_OBJECT(self, "overlay offset %lu + size %lu > total size %lu in stream %d.\n",
          filter->osd_offset[filter->stream_id],
          filter->osd_size[filter->stream_id],
          filter->iav_ctx->map_overlay.size,
          filter->stream_id);
      goto open_fail;
    }

    overlay_data_offset = filter->osd_offset[filter->stream_id] + OVERLAY_YUV_OFFSET;

    if (overlay_set->overlay_max_num == 0) {
      overlay_set->osd[0].enable = 1;
      overlay_set->overlay_max_num = 1;
    }

    for (j = 0, total_size = 0; j < overlay_set->overlay_max_num; j++) {
      osd = &overlay_set->osd[j];
      area = &overlay_set->overlay_insert.area[j];
      if (osd->enable) {
        if (osd->width <= 0 || osd->height <= 0) {
          osd->width = filter->stream_params[filter->stream_id].encode_width;
          osd->height = filter->stream_params[filter->stream_id].encode_height;
        }

        area->width = osd->width = ROUND_DOWN(osd->width, OVERLAY_WIDTH_ALIGN);
        area->height = osd->height = ROUND_DOWN(osd->height, OVERLAY_HEIGHT_ALIGN);
        area->start_x = osd->x = ROUND_DOWN(osd->x, OVERLAY_X_OFFSET_ALIGN);
        area->start_y = osd->y = ROUND_DOWN(osd->y, OVERLAY_Y_OFFSET_ALIGN);
        if (area->start_x + area->width > filter->stream_params[filter->stream_id].encode_width ||
            area->start_y + area->height > filter->stream_params[filter->stream_id].encode_height) {
          GST_ERROR_OBJECT(self, "The overlay start_x %u + width %u and start_y "
            "%u + height %u is out of the stream width %d and "
            "height %d.\n", area->start_x, area->width,
            area->start_y, area->height, filter->stream_params[filter->stream_id].encode_width,
            filter->stream_params[filter->stream_id].encode_height);
          goto open_fail;
        }
        area->pitch = ROUND_UP(area->width, OSD_BUF_PITCH_ALIGN);
        area->total_size = area->pitch * area->height;
        /* Each area has its own CLUT (bbox and bitmap use separate CLUT regions). */
        area->clut_addr_offset = filter->osd_offset[filter->stream_id] +
            j * OVERLAY_CLUT_SIZE;
        area->enable = 1;
        osd->buf_num = osd->buf_num > 0 ? osd->buf_num : 1;
        osd->buf_id = 0;
        for (buf_id = 0; buf_id < osd->buf_num; buf_id++) {
          osd->buf_data[buf_id] = overlay_data_offset + total_size + area->total_size * buf_id;
        }
        total_size += area->total_size * osd->buf_num;
        if (total_size > (filter->osd_size[filter->stream_id] - OVERLAY_YUV_OFFSET)) {
          GST_ERROR_OBJECT(self, "OSD buffer memory is not enough, please increase it! The total OSD size is %lu (should be <= %lu).\n",
            (unsigned long)total_size, (unsigned long)(filter->osd_size[filter->stream_id] - OVERLAY_YUV_OFFSET));
          goto open_fail;
        }
        /* Bbox area: full palette; bitmap area: background only so pic_data can add colors. */
        if (j == filter->area_id) {
          fill_overlay_clut(filter, area->clut_addr_offset);
        } else if (j == filter->bmp_area_id) {
          fill_overlay_clut_bitmap_area(filter, area->clut_addr_offset);
          filter->bitmap_dirty = 1;  /* force redraw after overlay area reconfig */
        }
      }
    }

  }


  return TRUE;

open_fail:
#ifdef BUILD_AMBARELLA_GST_DRAW_TEXT
  if (filter->p_text_font_buffer) {
    free(filter->p_text_font_buffer);
    filter->p_text_font_buffer = NULL;
  }
  if (filter->p_font) {
    destroy_font(filter->p_font);
    filter->p_font = NULL;
  }
#endif
  return FALSE;
}

static gboolean
gst_amba_venc_overlay_bbox_close (GstAmbaVencOverlayBbox *self)
{
  priv_venc_overlay_bbox_ctx_t * filter = self->priv_ctx;

  if (!filter) {
    return TRUE;
  }

#ifdef BUILD_AMBARELLA_GST_DRAW_TEXT
  if (filter->p_text_font_buffer) {
    free(filter->p_text_font_buffer);
    filter->p_text_font_buffer = NULL;
  }
  if (filter->p_font) {
    destroy_font(filter->p_font);
    filter->p_font = NULL;
  }
#endif

  return TRUE;
}

static gboolean gst_amba_venc_overlay_bbox_start (GstBaseSink * sink)
{
  GstAmbaVencOverlayBbox *self = GST_AMBAVENCOVERLAYBBOX (sink);

  return gst_amba_venc_overlay_bbox_open(self);
}
static gboolean gst_amba_venc_overlay_bbox_stop (GstBaseSink * sink)
{
  GstAmbaVencOverlayBbox *self = GST_AMBAVENCOVERLAYBBOX (sink);
  gst_amba_venc_overlay_bbox_close(self);

  return TRUE;
}

static int draw_box(
    gst_overlay_box_t *overlay_box, unsigned char *overlay_mem)
{
  unsigned int overlay_x = overlay_box->offset_x;
  unsigned int overlay_y = overlay_box->offset_y;
  unsigned int line_color = overlay_box->color;
  unsigned int line_thickness = overlay_box->line_thickness;
  unsigned int blank_width, rect_width, rect_height;
  unsigned int i = 0;

  rect_width = overlay_box->width;
  rect_height = overlay_box->height;
  if (((line_thickness << 1) > rect_width) || ((line_thickness << 1) > rect_height)) {
    blank_width = 0;
  } else {
    blank_width = rect_width - line_thickness * 2;
  }
  if (line_thickness > 0) {
    if (blank_width > 0) {
      for (i = 0; i < line_thickness; i++) {
        memset(overlay_mem + (i + overlay_y) * overlay_box->area_pitch + overlay_x,
            line_color, rect_width);
      }
      for (i = line_thickness; i < (rect_height - line_thickness); i++) {
        memset(overlay_mem + (i + overlay_y) * overlay_box->area_pitch + overlay_x,
            line_color, line_thickness);
        memset(overlay_mem + (i + overlay_y) * overlay_box->area_pitch + (overlay_x + rect_width - line_thickness),
            line_color, line_thickness);
      }
      for (i = (rect_height - line_thickness); i < rect_height; i++) {
        memset(overlay_mem + (i + overlay_y) * overlay_box->area_pitch + overlay_x,
            line_color, rect_width);
      }
    } else {
      for (i = 0; i < rect_height; i++) {
        memset(overlay_mem + (i + overlay_y) * overlay_box->area_pitch + overlay_x,
            line_color, rect_width);
      }
    }
  }


  return 0;
}

static void __draw_8bit(priv_venc_overlay_bbox_ctx_t *thiz,
    unsigned char *p_source, unsigned int src_stride, unsigned int dst_stride,
    unsigned int off_x, unsigned int off_y,
    unsigned int size_x, unsigned int size_y,
    unsigned char fore_color, unsigned char back_color,
    unsigned char *p_mem)
{
  if (thiz && p_mem) {
    unsigned int i, j;
    unsigned char *pd;
    unsigned char *ps;

    for (j = 0; j < size_y; j ++) {
      pd = p_mem + dst_stride * (off_y + j) + off_x;
      ps = p_source + src_stride * j;
      for (i = 0; i < size_x; i ++) {
        if (ps[i] != 0) {
          pd[i] = fore_color;//ps[i];
        } else {
          pd[i] = back_color;
        }
      }
    }
  }
}

#ifdef BUILD_AMBARELLA_GST_DRAW_TEXT

static void draw_textbox(priv_venc_overlay_bbox_ctx_t *thiz,
    gst_overlay_text_t *overlay_textbox, guchar *content)
{
  int dst_x, dst_y, dst_w, dst_h;
  int bound_ver, bound_hor;

  dst_x = overlay_textbox->box->offset_x;
  dst_y = overlay_textbox->box->offset_y;
  dst_w = overlay_textbox->box->width;
  dst_h = overlay_textbox->box->height;

  //draw text
  memset(thiz->p_text_font_buffer, 0x0, thiz->text_buffer_width * thiz->text_buffer_height);
  draw_text_ansi_string_one_line(thiz->p_text_font_buffer,
      thiz->text_buffer_width, thiz->text_buffer_height,
      thiz->text_buffer_origin_x, thiz->text_buffer_origin_y,
      overlay_textbox->text, overlay_textbox->text_length,
      &bound_hor, &bound_ver,
      thiz->p_font);

  if (((dst_x + dst_w + 3 + bound_hor) < (int) overlay_textbox->box->area_w)
      && ((dst_y + bound_ver) < (int) overlay_textbox->box->area_h)) {
    //right side
    __draw_8bit(thiz,
        thiz->p_text_font_buffer, thiz->text_buffer_width,
        overlay_textbox->box->area_pitch,
        dst_x + dst_w + 3, dst_y,
        bound_hor, bound_ver,
        overlay_textbox->fore_color,
        overlay_textbox->back_color,
        content);
  } else if ((dst_y > (bound_ver + 3))
      && ((dst_x + bound_hor + 3) < (int) overlay_textbox->box->area_w)) {
    //top side
    __draw_8bit(thiz,
        thiz->p_text_font_buffer, thiz->text_buffer_width,
        overlay_textbox->box->area_pitch,
        dst_x, dst_y - bound_ver - 3,
        bound_hor, bound_ver,
        overlay_textbox->fore_color,
        overlay_textbox->back_color,
        content);
  } else if ((dst_x > (bound_hor + 3))
      && ((dst_y + bound_ver + 3) < (int) overlay_textbox->box->area_h)) {
    //left side
    __draw_8bit(thiz,
        thiz->p_text_font_buffer, thiz->text_buffer_width,
        overlay_textbox->box->area_pitch,
        dst_x - bound_hor - 3, dst_y,
        bound_hor, bound_ver,
        overlay_textbox->fore_color,
        overlay_textbox->back_color,
        content);
  } else if (((dst_x + bound_hor + 3) < (int) overlay_textbox->box->area_w)
      && ((dst_y + dst_h + 3 + bound_ver) < (int) overlay_textbox->box->area_h)) {
    //botton side
    __draw_8bit(thiz,
        thiz->p_text_font_buffer, thiz->text_buffer_width,
        overlay_textbox->box->area_pitch,
        dst_x, dst_y + dst_h + 3,
        bound_hor, bound_ver,
        overlay_textbox->fore_color,
        overlay_textbox->back_color,
        content);
  } else {
    //center
    __draw_8bit(thiz,
        thiz->p_text_font_buffer, thiz->text_buffer_width,
        overlay_textbox->box->area_pitch,
        dst_x, dst_y,
        bound_hor, bound_ver,
        overlay_textbox->fore_color,
        overlay_textbox->back_color,
        content);
  }

}
#endif
static int display_osd_set_bbox(priv_venc_overlay_bbox_ctx_t *thiz,
    const char *title, unsigned int title_length,
    float x, float y, float w, float h, int stream_id, int area_id,
    guchar *content)
{
  int rval = 0;
  gst_overlay_box_t overlay_box = {0};
  gst_overlay_text_t overlay_text = {0};
  gst_display_obj_params_t *params = &thiz->display;

  do {
    if (x < 0 || y < 0 || w < 0 || h < 0) {
      rval = -1;
      break;
    }
    if (x >= 1 || x + w > 1) {
      rval = -1;
      break;
    }
    if (y >= 1 || y + h > 1) {
      rval = -1;
      break;
    }

    overlay_box.color = params->box_color;
    overlay_box.line_thickness = params->border_thickness;
    overlay_box.area_h = thiz->overlay_set[stream_id].overlay_insert.area[area_id].height;
    overlay_box.area_w = thiz->overlay_set[stream_id].overlay_insert.area[area_id].width;
    overlay_box.area_pitch = thiz->overlay_set[stream_id].overlay_insert.area[area_id].pitch;
    overlay_box.width = w * overlay_box.area_w;
    overlay_box.height = h * overlay_box.area_h;
    overlay_box.offset_x = x * overlay_box.area_w;
    overlay_box.offset_y = y * overlay_box.area_h;

    draw_box(&overlay_box, content);

#ifdef BUILD_AMBARELLA_GST_DRAW_TEXT
    if (title && title_length) {
      overlay_text.box = &overlay_box;
      overlay_text.fore_color = params->text_fore_color;
      overlay_text.back_color = params->text_back_color;
      overlay_text.text = title;
      overlay_text.text_length = title_length;
      draw_textbox(thiz, &overlay_text, content);
    }
#endif
  } while (0);

  return rval;
}

static int fill_box_info(priv_venc_overlay_bbox_ctx_t *thiz, int stream_id, int area_id, guchar *content)
{
  unsigned int i = 0;
  bounding_boxes_t *net_result = (bounding_boxes_t *) thiz->net_result;
  det_object_t *detection = NULL;
  gst_display_obj_params_t *params = &thiz->display;
  for (i = 0; i < net_result->det_num; i++) {
    detection = &net_result->detections[i];
    if (strstr(detection->label, "invalid") == NULL &&
      (detection->score >= thiz->score_limit)) {

      params->box_color = BOX_COLOR;

      display_osd_set_bbox(thiz, detection->label, strlen(detection->label),
          detection->x_start, detection->y_start,
          detection->x_end - detection->x_start,
          detection->y_end - detection->y_start,
          stream_id, area_id, content);
    }
  }

  return 0;
}

static int draw_bmp_picture(priv_venc_overlay_bbox_ctx_t *thiz, int stream_id, int area_id, guchar *content)
{
  int result = 0;
  amba_overlay_area_param_t area_param = {0};
  amba_draw_clut_t *clut_data = NULL;
  unsigned long clut_addr_offset = 0;
  struct iav_overlay_area *overlay_area = NULL;
  size_t fn_max;

  do {
    if (!thiz || !content || thiz->bmp_file[0] == '\0') {
      result = -1;
      break;
    }

    overlay_area = &thiz->overlay_set[stream_id].overlay_insert.area[area_id];
    clut_addr_offset = overlay_area->clut_addr_offset;

    /* Prepare area_param structure */
    area_param.attr.rect.x = overlay_area->start_x;
    area_param.attr.rect.y = overlay_area->start_y;
    area_param.attr.rect.width = overlay_area->width;
    area_param.attr.rect.height = overlay_area->height;
    area_param.attr.rect.pitch = overlay_area->pitch;
    area_param.data.type = AMBA_DRAW_DATA_TYPE_PICTURE;
    fn_max = sizeof(area_param.data.pic.filename);
    (void)snprintf(area_param.data.pic.filename, fn_max, "%.*s",
        (int)fn_max - 1, thiz->bmp_file);
    area_param.data.pic.use_bmp_alpha = 0;
    area_param.data.pic.alpha = 255;
    area_param.data.pic.colorkey.color.y = 0;
    area_param.data.pic.colorkey.color.u = 0;
    area_param.data.pic.colorkey.color.v = 0;
    area_param.data.pic.colorkey.color.a = 0xff;
    area_param.data.pic.colorkey.range = 0;

    // Get CLUT data
    clut_data = (amba_draw_clut_t *)(thiz->iav_ctx->map_overlay.base + clut_addr_offset);

    // Draw BMP picture
    if (amba_draw_pic_data(&area_param, clut_data, content, &thiz->bitmap, AMBA_DRAW_FORMAT_8BIT_CLUT) < 0) {
      GST_ERROR("amba_draw_pic_data failed for file: %s\n", thiz->bmp_file);
      result = -1;
      break;
    }

  } while (0);

  return result;
}

/* chain function
 * this function does the actual processing
 */
static GstFlowReturn
gst_amba_venc_overlay_bbox_show_frame (GstVideoSink * sink, GstBuffer * buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;

  unsigned int i = 0, j = 0, buf_id = 0;

  unsigned int dsp_pts = 0;

  GstVideoMeta *vmeta = NULL;
  AmbaPrivateDataMeta *priv_meta = NULL;
  guint num_roi = 0;
  gpointer state = NULL;
  bounding_boxes_t boxs;
  guchar *content = NULL;

  GstAmbaVencOverlayBbox *self = GST_AMBAVENCOVERLAYBBOX (sink);
  priv_venc_overlay_bbox_ctx_t * filter = self->priv_ctx;

  iav_set_overlay_t *overlay_set = NULL;

  GST_DEBUG_OBJECT (sink, "render ts %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_PTS (buffer)));


  do {
    vmeta = gst_buffer_get_video_meta (buffer);

    if (!vmeta) {
      GST_ERROR_OBJECT (sink, "missing video meta");
      ret = GST_FLOW_ERROR;
      break;
    }

    if (vmeta->n_planes > GST_VIDEO_MAX_PLANES) {
      GST_ERROR_OBJECT (sink, "too many planes in video meta");
      ret = GST_FLOW_ERROR;
      break;
    }

    priv_meta = amba_buffer_get_private_data_meta (buffer);
    if (priv_meta == NULL) {
      GST_ERROR_OBJECT (sink, "failed to get amba private meta data");
      ret = GST_FLOW_ERROR;
      break;
    }
    dsp_pts = priv_meta->dsp_pts;
    if (dsp_pts) {
      filter->last_dsp_pts[filter->stream_id] = dsp_pts;
    }

    num_roi =
        gst_buffer_get_n_meta (buffer, GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE);

    memset(&boxs, 0x0, sizeof(bounding_boxes_t));

    for (i = 0; i < num_roi; i++) {
      GstVideoRegionOfInterestMeta *roi;
      GstStructure *s;
      det_object_t *b = &boxs.detections[i];

      roi = (GstVideoRegionOfInterestMeta *)
          gst_buffer_iterate_meta_filtered (buffer, &state,
          GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE);
      if (!roi)
        continue;

      /* ignore roi if overflow */
      if ((roi->x > G_MAXINT16) || (roi->y > G_MAXINT16)
          || (roi->w > G_MAXUINT16) || (roi->h > G_MAXUINT16))
        continue;

      GST_LOG ("Input buffer ROI: type=%s id=%d (%d, %d) %dx%d",
          g_quark_to_string (roi->roi_type), roi->id, roi->x, roi->y, roi->w,
          roi->h);

      s = gst_video_region_of_interest_meta_get_param (roi, GST_MLINFERENCE_META_PARAM_NAME);
      if (s) {
        gdouble fv = 0;
        gint iv = 0;
        const gchar *sv = NULL;

        sv = gst_structure_get_string (s, GST_MLINFERENCE_META_FIELD_LABEL);
        if (sv == NULL)
          continue;
        strncpy(b->label, sv, DMAX_LABEL_LEN);

        if (!gst_structure_get_int (s, GST_MLINFERENCE_META_FIELD_ID, &iv))
          continue;
        b->id = iv;

        if (!gst_structure_get_double (s, GST_MLINFERENCE_META_FIELD_SCORE, &fv))
          continue;
        b->score = (float) fv;

        if (gst_structure_get_double (s, GST_MLINFERENCE_META_FIELD_X_START, &fv)) {
          b->x_start = (float) fv;
        } else {
          b->x_start = (float) roi->x / (float) vmeta->width;
        }

        if (gst_structure_get_double (s, GST_MLINFERENCE_META_FIELD_Y_START, &fv)) {
          b->y_start = (float) fv;
        } else {
          b->y_start = (float) roi->y / (float) vmeta->height;
        }

        if (gst_structure_get_double (s, GST_MLINFERENCE_META_FIELD_X_END, &fv)) {
          b->x_end = (float) fv;
        } else {
          b->x_end = (float) (roi->x + roi->w) / (float) vmeta->width;
        }

        if (gst_structure_get_double (s, GST_MLINFERENCE_META_FIELD_Y_END, &fv)) {
          b->y_end = (float) fv;
        } else {
          b->x_end = (float) (roi->y + roi->h) / (float) vmeta->height;
        }
      } else {
        DPRINT_WARNING ("No ROI value specified upstream, skip\n");
        continue;
      }

      boxs.det_num++;

    }

    overlay_set = &filter->overlay_set[filter->stream_id];
    if (overlay_set->overlay_insert.enable) {
      for (j = 0; j < overlay_set->overlay_max_num; ++j) {
        if (overlay_set->overlay_insert.area[j].enable) {
          /* Draw bounding boxes if this is the bbox area and ROI exists */
          if (j == filter->area_id) {
            buf_id = overlay_set->osd[j].buf_id + 1;
            buf_id = (buf_id >= overlay_set->osd[j].buf_num ? 0 : buf_id);
            content = filter->iav_ctx->map_overlay.base + overlay_set->osd[j].buf_data[buf_id];
            memset(content, AMBA_DRAW_CLUT_ENTRY_BACKGROUND, overlay_set->overlay_insert.area[j].total_size);
            if (num_roi) {
              filter->net_result = &boxs;
              fill_box_info(filter, filter->stream_id, j, content);
            }
            overlay_set->osd[j].buf_id = buf_id;
          } else if (j == filter->bmp_area_id && filter->bmp_file[0] != '\0') {
            if (filter->bitmap_dirty) {
              buf_id = overlay_set->osd[j].buf_id + 1;
              buf_id = (buf_id >= overlay_set->osd[j].buf_num ? 0 : buf_id);
              content = filter->iav_ctx->map_overlay.base + overlay_set->osd[j].buf_data[buf_id];
              memset(content, AMBA_DRAW_CLUT_ENTRY_BACKGROUND, overlay_set->overlay_insert.area[j].total_size);

              if (draw_bmp_picture(filter, filter->stream_id, j, content) < 0) {
                GST_WARNING_OBJECT(sink, "Failed to draw BMP picture in area %d, continue...\n", j);
              } else {
                filter->bitmap_dirty = 0;
              }
              overlay_set->osd[j].buf_id = buf_id;
            }
          }
        }
      }

      if (overlay_set->sync_with_pts) {
        if (filter->iav_ctx->iav_al.f_set_frame_sync(filter->iav_ctx->iav_fd, overlay_set) < 0) {
          GST_ERROR_OBJECT(self, "f_set_frame_sync error!\n");
          ret = GST_FLOW_ERROR;
          break;
        }
      } else {
        if (filter->iav_ctx->iav_al.f_set_overlay(filter->iav_ctx->iav_fd, overlay_set) < 0) {
          GST_ERROR_OBJECT(sink, "f_set_overlay error!\n");
          ret = GST_FLOW_ERROR;
          break;
        }
      }
    }

    if (overlay_set->sync_with_pts) {
      if (filter->iav_ctx->iav_al.f_apply_frame_sync(filter->iav_ctx->iav_fd, dsp_pts,
            (1U << filter->stream_id), 0) < 0) {
        GST_ERROR_OBJECT(self, "f_apply_frame_sync error!\n");
        ret = GST_FLOW_ERROR;
        break;
      }
    }

  }while(0);

  return ret;
}


