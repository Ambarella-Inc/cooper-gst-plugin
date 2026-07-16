/*
 * gstambadrawdatagen.c
 *
 * History:
 *    3/3/2026 - [pxduan] created file
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
 * SECTION:element-amba_draw_data_gen
 * @title: amba_draw_data_gen
 * @see_also: mlpostprocess, amba_overlay_draw
 *
 * Converts mlpostprocess output (application/x-amba-ml-decoded) to draw data or video.
 * Supports: bbox, pose (RTMPose skeleton), classification (text), clip_score, BMP, string, segmentation, depth.
 * output-mode=draw-data for amba_overlay_draw; output-mode=video for compositor (ARGB).
 *
 * Input: application/x-amba-ml-decoded, image/bmp, text/x-raw, video/x-raw GRAY8,
 * application/x-amba-drawdatagen-trigger (payload ignored; osd-only draw-data).
 * Output: application/x-amba-draw-data (draw-data) or video/x-raw,format=ARGB (video).
 * Downstream: amba_overlay_draw (draw-data) or compositor/videoconvert (video).
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * mlpostprocess ! amba_draw_data_gen ! amba_overlay_draw
 * mlpostprocess ! amba_draw_data_gen output-mode=video ! compositor
 * ]|
 * </refsect2>
 */

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>

#include "internal.h"
#include "debug_log.h"
#include "platform_al.h"
#include "iav_al.h"
#include <gst/video/gstvideometa.h>
#include <gst/video/video.h>

#ifndef OVERLAY_WIDTH_ALIGN
#define OVERLAY_WIDTH_ALIGN 4
#endif
#ifndef OVERLAY_HEIGHT_ALIGN
#define OVERLAY_HEIGHT_ALIGN 4
#endif
#include "gstambadrawdatagen.h"
#include "amba_ml_decoded_result.h"
#include "drawdatagen_common.h"
#include "drawdatagen_classification.h"
#include "drawdatagen_pose.h"
#include "drawdatagen_depth.h"
#include "drawdatagen_clip_score.h"
#include "drawdatagen.h"
#include "draw_data_caps.h"
#include "amba_private_data.h"

/* input_media_type values (see gst_amba_draw_data_gen_set_caps) */
#define INPUT_TYPE_ML_DECODED  0
#define INPUT_TYPE_IMAGE_BMP  1
#define INPUT_TYPE_TEXT      2
#define INPUT_TYPE_VIDEO_GRAY8 3
#define INPUT_TYPE_OSD_TRIGGER 4

GST_DEBUG_CATEGORY_STATIC(gst_amba_draw_data_gen_debug);
#define GST_CAT_DEFAULT gst_amba_draw_data_gen_debug

enum {
  PROP_0,
  PROP_COORD_RES,
  PROP_OSD,
  PROP_OUTPUT_MODE,
  PROP_DRAW_FORMAT,
};

#define DRAWDATAGEN_FORMAT_8BIT    "8bit"
#define DRAWDATAGEN_FORMAT_RGB565  "rgb565"
#define DRAWDATAGEN_FORMAT_AYUV8888 "ayuv8888"
#define DRAWDATAGEN_FORMAT_ARGB8888 "argb8888"

/* Sink: ANY for max compatibility. set_caps validates at runtime. */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY
    );

/* Include draw-data and video so overlay/compositor can link. ANY would cause
 * transform_caps filter (upstream image/bmp) to yield empty intersection. */
static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS(GST_AMBA_DRAW_DATA_CAPS "; video/x-raw, format=(string)ARGB")
    );

#define gst_amba_draw_data_gen_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE(GstAmbaDrawDataGen, gst_amba_draw_data_gen, GST_TYPE_BASE_TRANSFORM,
    GST_DEBUG_CATEGORY_INIT(gst_amba_draw_data_gen_debug, "amba_draw_data_gen", 0, "draw data generator (overlay, blur, etc.)"));

#define GST_TYPE_AMBA_DRAW_DATA_GEN_OUTPUT_MODE (gst_amba_draw_data_gen_output_mode_get_type())
static GType gst_amba_draw_data_gen_output_mode_get_type(void)
{
  static GType type = 0;
  if (!type) {
    static const GEnumValue values[] = {
      { OUTPUT_MODE_DRAW_DATA, "draw-data", "draw-data" },
      { OUTPUT_MODE_VIDEO, "video", "video" },
      { 0, NULL, NULL },
    };
    type = g_enum_register_static("GstAmbaDrawDataGenOutputMode", values);
  }
  return type;
}

static void gst_amba_draw_data_gen_finalize(GObject *object);
static void gst_amba_draw_data_gen_set_property(GObject *object, guint prop_id,
    const GValue *value, GParamSpec *pspec);
static void gst_amba_draw_data_gen_get_property(GObject *object, guint prop_id,
    GValue *value, GParamSpec *pspec);

static GstFlowReturn gst_amba_draw_data_gen_prepare_output_buffer(GstBaseTransform *trans,
    GstBuffer *inbuf, GstBuffer **outbuf);
static GstFlowReturn gst_amba_draw_data_gen_transform(GstBaseTransform *trans,
    GstBuffer *inbuf, GstBuffer *outbuf);
static gboolean gst_amba_draw_data_gen_set_caps(GstBaseTransform *trans,
    GstCaps *incaps, GstCaps *outcaps);
static gboolean gst_amba_draw_data_gen_transform_size(GstBaseTransform *trans,
    GstPadDirection direction, GstCaps *caps, gsize size, GstCaps *othercaps, gsize *othersize);
static GstCaps *gst_amba_draw_data_gen_transform_caps(GstBaseTransform *trans,
    GstPadDirection direction, GstCaps *caps, GstCaps *filter);
static GstCaps *gst_amba_draw_data_gen_fixate_caps(GstBaseTransform *trans,
    GstPadDirection direction, GstCaps *caps, GstCaps *othercaps);
static gboolean gst_amba_draw_data_gen_query(GstBaseTransform *trans,
    GstPadDirection direction, GstQuery *query);
static gboolean gst_amba_draw_data_gen_propose_allocation(GstBaseTransform *trans,
    GstQuery *decide_query, GstQuery *query);
static gboolean gst_amba_draw_data_gen_decide_allocation(GstBaseTransform *trans,
    GstQuery *query);

int drawdatagen_parse_format(const char *name, guint *pix_size)
{
  if (!name || !pix_size)
    return AMBA_DRAW_FORMAT_NONE;
  if (g_ascii_strcasecmp(name, DRAWDATAGEN_FORMAT_8BIT) == 0) {
    *pix_size = 1;
    return AMBA_DRAW_FORMAT_8BIT_CLUT;
  }
  if (g_ascii_strcasecmp(name, "uyv565") == 0) {
    *pix_size = 2;
    return AMBA_DRAW_FORMAT_UYV565;
  }
  if (g_ascii_strcasecmp(name, "bgr565") == 0) {
    *pix_size = 2;
    return AMBA_DRAW_FORMAT_BGR565;
  }
  if (g_ascii_strcasecmp(name, "ayuv4444") == 0) {
    *pix_size = 2;
    return AMBA_DRAW_FORMAT_AYUV4444;
  }
  if (g_ascii_strcasecmp(name, "rgba4444") == 0) {
    *pix_size = 2;
    return AMBA_DRAW_FORMAT_RGBA4444;
  }
  if (g_ascii_strcasecmp(name, "bgra4444") == 0) {
    *pix_size = 2;
    return AMBA_DRAW_FORMAT_BGRA4444;
  }
  if (g_ascii_strcasecmp(name, "abgr4444") == 0) {
    *pix_size = 2;
    return AMBA_DRAW_FORMAT_ABGR4444;
  }
  if (g_ascii_strcasecmp(name, "argb4444") == 0) {
    *pix_size = 2;
    return AMBA_DRAW_FORMAT_ARGB4444;
  }
  if (g_ascii_strcasecmp(name, "ayuv1555") == 0) {
    *pix_size = 2;
    return AMBA_DRAW_FORMAT_AYUV1555;
  }
  if (g_ascii_strcasecmp(name, "yuv1555") == 0) {
    *pix_size = 2;
    return AMBA_DRAW_FORMAT_YUV1555;
  }
  if (g_ascii_strcasecmp(name, "rgba5551") == 0) {
    *pix_size = 2;
    return AMBA_DRAW_FORMAT_RGBA5551;
  }
  if (g_ascii_strcasecmp(name, "bgra5551") == 0) {
    *pix_size = 2;
    return AMBA_DRAW_FORMAT_BGRA5551;
  }
  if (g_ascii_strcasecmp(name, "abgr1555") == 0) {
    *pix_size = 2;
    return AMBA_DRAW_FORMAT_ABGR1555;
  }
  if (g_ascii_strcasecmp(name, "argb1555") == 0) {
    *pix_size = 2;
    return AMBA_DRAW_FORMAT_ARGB1555;
  }
  if (g_ascii_strcasecmp(name, DRAWDATAGEN_FORMAT_RGB565) == 0) {
    *pix_size = 2;
    return AMBA_DRAW_FORMAT_RGB565;
  }
  if (g_ascii_strcasecmp(name, DRAWDATAGEN_FORMAT_AYUV8888) == 0) {
    *pix_size = 4;
    return AMBA_DRAW_FORMAT_AYUV8888;
  }
  if (g_ascii_strcasecmp(name, "rgba8888") == 0) {
    *pix_size = 4;
    return AMBA_DRAW_FORMAT_RGBA8888;
  }
  if (g_ascii_strcasecmp(name, "bgra8888") == 0) {
    *pix_size = 4;
    return AMBA_DRAW_FORMAT_BGRA8888;
  }
  if (g_ascii_strcasecmp(name, "abgr8888") == 0) {
    *pix_size = 4;
    return AMBA_DRAW_FORMAT_ABGR8888;
  }
  if (g_ascii_strcasecmp(name, DRAWDATAGEN_FORMAT_ARGB8888) == 0) {
    *pix_size = 4;
    return AMBA_DRAW_FORMAT_ARGB8888;
  }
  return AMBA_DRAW_FORMAT_NONE;
}

static int parse_roi(const char *s, amba_rect_t *roi)
{
  int x = 0, y = 0, w = 0, h = 0;
  if (!s || !s[0])
    return -1;
  if (sscanf(s, "%d.%d.%d.%d", &x, &y, &w, &h) >= 4) {
    roi->x = x;
    roi->y = y;
    roi->width = w;
    roi->height = h;
    return 0;
  }
  if (sscanf(s, "%d.%d", &w, &h) >= 2) {
    roi->x = 0;
    roi->y = 0;
    roi->width = w;
    roi->height = h;
    return 0;
  }
  return -1;
}

/* Apply parsed key:value to area (for block content). */
static void parse_apply_block_kv(drawdatagen_osd_area_t *a, const char *key, const char *val)
{
  if (!a || !key || !val)
    return;
  if (g_ascii_strcasecmp(key, "font_name") == 0 || g_ascii_strcasecmp(key, "font") == 0) {
    g_strlcpy(a->font_name, val, sizeof(a->font_name));
  } else if (g_ascii_strcasecmp(key, "font_size") == 0) {
    a->font_size = (int)g_ascii_strtoll(val, NULL, 10);
  } else if (g_ascii_strcasecmp(key, "bmp") == 0) {
    g_strlcpy(a->bmp_file, val, sizeof(a->bmp_file));
  } else if (g_ascii_strcasecmp(key, "str") == 0) {
    g_strlcpy(a->str, val, sizeof(a->str));
  } else if (g_ascii_strcasecmp(key, "pre_str") == 0) {
    g_strlcpy(a->time_pre_str, val, sizeof(a->time_pre_str));
  } else if (g_ascii_strcasecmp(key, "suf_str") == 0) {
    g_strlcpy(a->time_suf_str, val, sizeof(a->time_suf_str));
  } else if (g_ascii_strcasecmp(key, "en_msec") == 0) {
    a->time_en_msec = !!(guint)g_ascii_strtoll(val, NULL, 10);
  } else if (g_ascii_strcasecmp(key, "format") == 0) {
    a->time_format = (int)g_ascii_strtoll(val, NULL, 10);
  } else if (g_ascii_strcasecmp(key, "is_12h") == 0) {
    a->time_is_12h = !!(guint)g_ascii_strtoll(val, NULL, 10);
  } else if (g_ascii_strcasecmp(key, "y") == 0) {
    a->mask_color_y = (unsigned char)g_ascii_strtoll(val, NULL, 10);
  } else if (g_ascii_strcasecmp(key, "u") == 0) {
    a->mask_color_u = (unsigned char)g_ascii_strtoll(val, NULL, 10);
  } else if (g_ascii_strcasecmp(key, "v") == 0) {
    a->mask_color_v = (unsigned char)g_ascii_strtoll(val, NULL, 10);
  } else if (g_ascii_strcasecmp(key, "a") == 0) {
    a->mask_color_a = (unsigned char)g_ascii_strtoll(val, NULL, 10);
  } else if (g_ascii_strcasecmp(key, "font_color") == 0) {
    a->font_color_hex = (guint32)g_ascii_strtoull(val, NULL, 16);
  } else if (g_ascii_strcasecmp(key, "bg_color") == 0) {
    a->bg_color_hex = (guint32)g_ascii_strtoull(val, NULL, 16);
  } else if (g_ascii_strcasecmp(key, "line_color") == 0) {
    a->bbox_line_color_hex = (guint32)g_ascii_strtoull(val, NULL, 16);
  } else if (g_ascii_strcasecmp(key, "ol_color") == 0) {
    a->ol_color_hex = (guint32)g_ascii_strtoull(val, NULL, 16);
  } else if (g_ascii_strcasecmp(key, "font_outline_w") == 0) {
    a->font_outline_w = (int)g_ascii_strtoll(val, NULL, 10);
  } else if (g_ascii_strcasecmp(key, "font_ver_bold") == 0) {
    a->font_ver_bold = (int)g_ascii_strtoll(val, NULL, 10);
  } else if (g_ascii_strcasecmp(key, "font_hor_bold") == 0) {
    a->font_hor_bold = (int)g_ascii_strtoll(val, NULL, 10);
  } else if (g_ascii_strcasecmp(key, "static") == 0) {
    a->static_content = !!(guint)g_ascii_strtoll(val, NULL, 10);
  } else if (g_ascii_strcasecmp(key, "update_interval") == 0) {
    a->update_interval = (unsigned int)g_ascii_strtoll(val, NULL, 10);
  }
}

/* Apply string/time colors from osd area to text box.
 * YUV draw formats: hex MSB..LSB = v/u/y/a. RGB-packed formats: b/g/r/a (clut y,u,v = r,g,b). */
static void apply_string_colors_from_area(amba_draw_text_box_t *text,
    const drawdatagen_osd_area_t *a, int draw_format)
{
  if (!a || !text)
    return;
  if (a->font_color_hex != 0) {
    guint32 t = a->font_color_hex;
    text->font_color.id = AMBA_DRAW_COLOR_CUSTOM;
    if (drawdatagen_osd_hex_is_rgb_packed(draw_format)) {
      text->font_color.color.v = (unsigned char)((t >> 24) & 0xff);
      text->font_color.color.u = (unsigned char)((t >> 16) & 0xff);
      text->font_color.color.y = (unsigned char)((t >> 8) & 0xff);
      text->font_color.color.a = (unsigned char)(t & 0xff);
    } else {
      text->font_color.color.v = (unsigned char)((t >> 24) & 0xff);
      text->font_color.color.u = (unsigned char)((t >> 16) & 0xff);
      text->font_color.color.y = (unsigned char)((t >> 8) & 0xff);
      text->font_color.color.a = (unsigned char)(t & 0xff);
    }
  }
  if (a->bg_color_hex != 0) {
    guint32 t = a->bg_color_hex;
    if (drawdatagen_osd_hex_is_rgb_packed(draw_format)) {
      text->background_color.v = (unsigned char)((t >> 24) & 0xff);
      text->background_color.u = (unsigned char)((t >> 16) & 0xff);
      text->background_color.y = (unsigned char)((t >> 8) & 0xff);
      text->background_color.a = (unsigned char)(t & 0xff);
    } else {
      text->background_color.v = (unsigned char)((t >> 24) & 0xff);
      text->background_color.u = (unsigned char)((t >> 16) & 0xff);
      text->background_color.y = (unsigned char)((t >> 8) & 0xff);
      text->background_color.a = (unsigned char)(t & 0xff);
    }
  }
  if (a->ol_color_hex != 0) {
    guint32 t = a->ol_color_hex;
    if (drawdatagen_osd_hex_is_rgb_packed(draw_format)) {
      text->outline_color.v = (unsigned char)((t >> 24) & 0xff);
      text->outline_color.u = (unsigned char)((t >> 16) & 0xff);
      text->outline_color.y = (unsigned char)((t >> 8) & 0xff);
      text->outline_color.a = (unsigned char)(t & 0xff);
    } else {
      text->outline_color.v = (unsigned char)((t >> 24) & 0xff);
      text->outline_color.u = (unsigned char)((t >> 16) & 0xff);
      text->outline_color.y = (unsigned char)((t >> 8) & 0xff);
      text->outline_color.a = (unsigned char)(t & 0xff);
    }
  }
  if (a->font_outline_w > 0)
    text->font.outline_width = (unsigned int)a->font_outline_w;
  if (a->font_ver_bold > 0)
    text->font.ver_bold = a->font_ver_bold;
  if (a->font_hor_bold > 0)
    text->font.hor_bold = a->font_hor_bold;
}

static int drawdatagen_draw_ml_overlay(GstAmbaDrawDataGen *self, amba_rect_t *roi,
    amba_ml_bbox_result_t *ml_result, uint32_t det_num,
    const drawdatagen_osd_area_t *area_override,
    amba_draw_clut_t *clut, unsigned char *data_buf, unsigned int area_pitch,
    int draw_format, int append_mode, int area_type)
{
  draw_data_gen_priv_t *priv = self->priv;
  uint32_t bg = area_override ? area_override->bg_color_hex : 0;
  uint32_t line = area_override ? area_override->bbox_line_color_hex : 0;

  if (area_type == DRAWDATAGEN_AREA_TYPE_POSE ||
      (area_type == DRAWDATAGEN_AREA_TYPE_BBOX && priv->pending_ml_pose)) {
    if (!priv->pending_ml_pose)
      return 0;
    return drawdatagen_pose_draw(roi, priv->pending_ml_pose,
        DRAWDATAGEN_POSE_CLUT_INDEX, 2, 3, clut, data_buf, area_pitch,
        draw_format, append_mode, bg, line);
  }

  if (priv->pending_ml_classification) {
    const char *ff = (area_override && area_override->font_name[0]) ? area_override->font_name : priv->font_file;
    int fs = (area_override && area_override->font_size > 0) ? area_override->font_size : priv->font_size;

    if (!ff || !ff[0])
      ff = DRAWDATAGEN_DEFAULT_FONT_PATH;
    if (area_override)
      apply_string_colors_from_area(&priv->area_str.data.text, area_override, draw_format);
    priv->area_str.data.text.background_color.y = 235;
    priv->area_str.data.text.background_color.u = 128;
    priv->area_str.data.text.background_color.v = 128;
    priv->area_str.data.text.background_color.a = 0;
    return drawdatagen_classification_draw(roi, priv->pending_ml_classification, ff, fs,
        &priv->area_str, &priv->bitmap, clut, data_buf, area_pitch,
        draw_format, 0, bg, append_mode);
  }

  {
    const char *ff = (area_override && area_override->font_name[0]) ? area_override->font_name : priv->font_file;
    int fs = (area_override && area_override->font_size > 0) ? area_override->font_size : priv->font_size;

    if (!ff || !ff[0])
      ff = DRAWDATAGEN_DEFAULT_FONT_PATH;
    if (area_override)
      apply_string_colors_from_area(&priv->area_str.data.text, area_override, draw_format);
    return drawdatagen_bbox_draw(roi, ml_result, det_num,
        DRAWDATAGEN_BBOX_CLUT_INDEX, 3, clut, data_buf, area_pitch,
        ff, fs, &priv->area_str, &priv->bitmap, draw_format, append_mode, bg, line);
  }
}

/* Parse block content "key1:val1,key2:val2,..." and apply to area. */
static void parse_block_content(drawdatagen_osd_area_t *a, const char *content)
{
  char **pairs;
  guint i, n;
  if (!a || !content)
    return;
  pairs = g_strsplit(content, ",", -1);
  n = g_strv_length(pairs);
  for (i = 0; i < n; i++) {
    char **kv = g_strsplit(pairs[i], ":", 2);
    if (g_strv_length(kv) >= 2) {
      g_strstrip(kv[0]);
      g_strstrip(kv[1]);
      parse_apply_block_kv(a, kv[0], kv[1]);
    }
    g_strfreev(kv);
  }
  g_strfreev(pairs);
}

static void drawdatagen_area_cache_clear(draw_data_gen_priv_t *priv)
{
  guint i;
  for (i = 0; i < MAX_OVERLAY_AREA_NUM; i++) {
    if (priv->area_cache[i]) {
      gst_memory_unref(priv->area_cache[i]);
      priv->area_cache[i] = NULL;
    }
  }
  memset(priv->last_static_source_ready, 0, sizeof(priv->last_static_source_ready));
}

static guint32 drawdatagen_hash_block_content(const guint8 *data, gsize len)
{
  guint32 h = 5381;
  gsize i;
  for (i = 0; i < len; i++) {
    h = ((h << 5) + h) + (guint32)data[i];
  }
  return h;
}

/* Source fingerprint for static_content cache (string bytes or bmp buffer / file stat). */
static guint32 drawdatagen_compute_static_source_hash(draw_data_gen_priv_t *priv,
    drawdatagen_osd_area_t *a, const char *str_ov,
    const unsigned char *bmp_data, size_t bmp_size)
{
  if (a->type == DRAWDATAGEN_AREA_TYPE_STRING) {
    const char *s = str_ov;
    if (!s || !s[0]) {
      if (a->str[0])
        s = a->str;
      else if (priv->str_text[0])
        s = priv->str_text;
      else
        s = "";
    }
    return drawdatagen_hash_block_content((const guint8 *)s, strlen(s));
  }
  if (a->type == DRAWDATAGEN_AREA_TYPE_PICTURE) {
    if (bmp_data && bmp_size > 0)
      return drawdatagen_hash_block_content(bmp_data, bmp_size);
    {
      const char *path = (a->bmp_file[0]) ? a->bmp_file : priv->bmp_file;
      if (path && path[0]) {
        struct stat st;
        if (stat(path, &st) == 0) {
          return (guint32)st.st_mtime ^ (guint32)st.st_size ^
              (guint32)(st.st_ino & 0xffffffffu);
        }
      }
    }
    return 0;
  }
  return 0;
}

static gboolean drawdatagen_area_cache_eligible(draw_data_gen_priv_t *priv, drawdatagen_osd_area_t *a)
{
  if (!a || !priv->osd_param.use_osd)
    return FALSE;
  if (priv->input_media_type == INPUT_TYPE_VIDEO_GRAY8)
    return FALSE;
  if (priv->input_media_type == INPUT_TYPE_ML_DECODED) {
    /* Bbox rasterization only; update_interval>1 reuses last draw-data block on skipped frames. */
    return (a->type == DRAWDATAGEN_AREA_TYPE_BBOX && a->update_interval > 1) ||
        (a->type == DRAWDATAGEN_AREA_TYPE_POSE && a->update_interval > 1);
  }
  if (a->type == DRAWDATAGEN_AREA_TYPE_PICTURE &&
      (priv->input_media_type == INPUT_TYPE_IMAGE_BMP ||
          (priv->input_media_type == INPUT_TYPE_OSD_TRIGGER && a->bmp_file[0])))
    return TRUE;
  if (a->type == DRAWDATAGEN_AREA_TYPE_STRING &&
      (priv->input_media_type == INPUT_TYPE_TEXT ||
          (priv->input_media_type == INPUT_TYPE_OSD_TRIGGER && a->str[0])))
    return TRUE;
  if (a->type == DRAWDATAGEN_AREA_TYPE_TIME)
    return TRUE;
  return FALSE;
}

static gboolean drawdatagen_area_should_use_cache(draw_data_gen_priv_t *priv,
    drawdatagen_osd_area_t *a, unsigned int aid,
    const char *str_ov, const unsigned char *bmp_data, size_t bmp_size)
{
  if (!drawdatagen_area_cache_eligible(priv, a))
    return FALSE;
  if (!priv->area_cache[aid])
    return FALSE;
  /* ML bbox: throttle by update_interval only (ignore static_content — detections change every frame). */
  if (priv->input_media_type == INPUT_TYPE_ML_DECODED &&
      (a->type == DRAWDATAGEN_AREA_TYPE_BBOX || a->type == DRAWDATAGEN_AREA_TYPE_POSE)) {
    if (a->update_interval > 1)
      return (priv->frame_counter % a->update_interval) != 0;
    return FALSE;
  }
  /* TIME: wall-clock pixels change; throttle by update_interval only (not static alone).
   * If update_interval>1, it wins over static so clock can refresh every N frames. */
  if (a->type == DRAWDATAGEN_AREA_TYPE_TIME) {
    if (a->update_interval > 1)
      return (priv->frame_counter % a->update_interval) != 0;
    if (a->static_content)
      return TRUE;
    return FALSE;
  }
  if (a->static_content) {
    guint32 h = drawdatagen_compute_static_source_hash(priv, a, str_ov, bmp_data, bmp_size);
    if (!priv->last_static_source_ready[aid] || priv->last_static_source_hash[aid] != h)
      return FALSE;
    return TRUE;
  }
  if (a->update_interval > 1)
    return (priv->frame_counter % a->update_interval) != 0;
  return FALSE;
}

static gboolean drawdatagen_rect_equal(const amba_rect_t *a, const amba_rect_t *b)
{
  return a->x == b->x && a->y == b->y && a->width == b->width && a->height == b->height && a->pitch == b->pitch;
}

/* attr_change: roi/enable/draw_format vs last frame; data_change: CLUT+pixel bytes vs last hash.
 * Flags are stored in GstAmbaDrawDataAreaFlagsMeta on @outbuf (not in osd_area_block_header_t). */
static void drawdatagen_apply_block_change_flags(draw_data_gen_priv_t *priv,
    osd_area_block_header_t *block_hdr, const amba_rect_t *roi, unsigned int slot_idx,
    const guint8 *content_start, gsize content_len,
    GstBuffer *outbuf)
{
  guint32 h = drawdatagen_hash_block_content(content_start, content_len);
  gboolean first = !priv->last_area_ready[slot_idx];
  guint8 attr_f = 1;
  guint8 data_f = 1;

  if (slot_idx >= MAX_OVERLAY_AREA_NUM)
    return;
  if (first) {
    attr_f = 1;
    data_f = 1;
  } else {
    gboolean attr_changed =
        (block_hdr->enable != priv->last_area_enable[slot_idx]) ||
        !drawdatagen_rect_equal(roi, &priv->last_area_rect[slot_idx]) ||
        (block_hdr->draw_format != priv->last_area_draw_format[slot_idx]);
    gboolean data_changed = (h != priv->last_area_content_hash[slot_idx]);
    attr_f = attr_changed ? 1 : 0;
    data_f = data_changed ? 1 : 0;
  }
  priv->last_area_rect[slot_idx] = *roi;
  priv->last_area_enable[slot_idx] = block_hdr->enable;
  priv->last_area_draw_format[slot_idx] = block_hdr->draw_format;
  priv->last_area_content_hash[slot_idx] = h;
  priv->last_area_ready[slot_idx] = 1;
  if (outbuf) {
    guint mem_idx = gst_buffer_n_memory(outbuf) - 1;
    unsigned int area_slot = osd_area_block_resolve_slot(block_hdr, mem_idx);
    guint8 f = 0;
    if (attr_f)
      f |= AMBA_DRAW_AREA_FLAG_ATTR_CHANGED;
    if (data_f)
      f |= AMBA_DRAW_AREA_FLAG_DATA_CHANGED;
    if (area_slot < MAX_OVERLAY_AREA_NUM)
      gst_amba_draw_data_area_flags_meta_set(outbuf, area_slot, f);
  }
}

static GstFlowReturn render_draw_blocks_to_video(GstBuffer *draw_buf, guint8 *vid_buf,
    int vid_width, int vid_height, int vid_stride);

/* Append ARGB plane built from draw-data blocks in @draw_blocks onto @outbuf. */
static GstFlowReturn drawdatagen_emit_video_from_draw_blocks(GstAmbaDrawDataGen *self,
    GstBuffer *draw_blocks, GstBuffer *outbuf, guint det_num)
{
  draw_data_gen_priv_t *priv = self->priv;
  gsize vid_size = (gsize)priv->map_width * priv->map_height * 4;
  GstMemory *vid_mem = gst_allocator_alloc(NULL, vid_size, NULL);

  if (!vid_mem) {
    GST_ERROR_OBJECT(self, "Failed to allocate video buffer %dx%dx4", priv->map_width, priv->map_height);
    return GST_FLOW_ERROR;
  }
  {
    GstMapInfo vmap;
    if (gst_memory_map(vid_mem, &vmap, GST_MAP_WRITE)) {
      render_draw_blocks_to_video(draw_blocks, vmap.data, priv->map_width, priv->map_height,
          priv->map_width * 4);
      if (det_num > 0) {
        guint32 *p = (guint32 *)vmap.data;
        gsize n = (gsize)priv->map_width * priv->map_height, cnt = 0;
        gsize i;
        for (i = 0; i < n; i++) {
          if (p[i] != 0)
            cnt++;
        }
        GST_INFO_OBJECT(self, "video overlay: det_num=%u non_zero_pixels=%" G_GSIZE_FORMAT,
            (unsigned)det_num, cnt);
      }
      gst_memory_unmap(vid_mem, &vmap);
    }
    gst_buffer_append_memory(outbuf, vid_mem);
    gst_buffer_add_video_meta(outbuf, GST_VIDEO_FRAME_FLAG_NONE,
        GST_VIDEO_FORMAT_ARGB, priv->map_width, priv->map_height);
  }
  return GST_FLOW_OK;
}

/* Parse osd string. New format: ; separates area; {} for type-specific blocks.
 *   area_id:0;enable:1;roi:0.0.1920.1080;type:bbox;bbox:{font_name:/usr/share/fonts/DroidSans.ttf,font_size:24};area_id:1;...
 * Legacy format (comma): area_id:0,enable:1,roi:0.0.1920.1080,type:bbox,...
 * coord_res: use property only, not in osd. */
static int parse_osd_drawdatagen(draw_data_gen_priv_t *priv, const char *osd_str)
{
  int ret = 0;
  gint cur_area_id = -1;
  guint i, len;
  char **options;
  gboolean use_semicolon;

  if (!osd_str || !osd_str[0])
    return -1;

  memset(&priv->osd_param, 0, sizeof(priv->osd_param));
  memset(priv->last_area_ready, 0, sizeof(priv->last_area_ready));
  drawdatagen_area_cache_clear(priv);
  use_semicolon = (strchr(osd_str, ';') != NULL);
  options = g_strsplit(osd_str, use_semicolon ? ";" : ",", -1);
  len = g_strv_length(options);

  for (i = 0; i < len; i++) {
    char *seg = options[i];
    char *colon = strchr(seg, ':');
    char *brace_start = strchr(seg, '{');
    char *brace_end = seg ? strchr(seg, '}') : NULL;

    if (!colon)
      continue;
    g_strstrip(seg);

    if (use_semicolon && brace_start && brace_end && brace_start < brace_end) {
      /* Block format: key:{content} - key is before first ':', content is inside {} */
      char *content_start = brace_start + 1;
      char *content_end = brace_end;
      char *content_buf;

      content_buf = g_strndup(content_start, (size_t)(content_end - content_start));
      if (cur_area_id >= 0 && cur_area_id < MAX_OVERLAY_AREA_NUM) {
        drawdatagen_osd_area_t *a = &priv->osd_param.area[cur_area_id];
        parse_block_content(a, content_buf);
      }
      g_free(content_buf);
    } else {
      /* key:value format */
      char **opt = g_strsplit(seg, ":", 2);
      if (g_strv_length(opt) < 2) {
        g_strfreev(opt);
        continue;
      }
      g_strstrip(opt[0]);
      g_strstrip(opt[1]);

      if (g_ascii_strcasecmp(opt[0], "area_id") == 0) {
        cur_area_id = (gint)g_ascii_strtoll(opt[1], NULL, 10);
        if (cur_area_id >= 0 && cur_area_id < MAX_OVERLAY_AREA_NUM) {
          if (priv->osd_param.area_num <= (unsigned)cur_area_id)
            priv->osd_param.area_num = cur_area_id + 1;
        } else {
          ret = -1;
          g_strfreev(opt);
          break;
        }
      } else if (cur_area_id >= 0 && cur_area_id < MAX_OVERLAY_AREA_NUM) {
        drawdatagen_osd_area_t *a = &priv->osd_param.area[cur_area_id];
        if (g_ascii_strcasecmp(opt[0], "enable") == 0) {
          a->enable = !!(guint)g_ascii_strtoll(opt[1], NULL, 10);
        } else if (g_ascii_strcasecmp(opt[0], "roi") == 0) {
          parse_roi(opt[1], &a->roi);
        } else if (g_ascii_strcasecmp(opt[0], "type") == 0) {
          if (g_ascii_strcasecmp(opt[1], "bbox") == 0)
            a->type = DRAWDATAGEN_AREA_TYPE_BBOX;
          else if (g_ascii_strcasecmp(opt[1], "picture") == 0)
            a->type = DRAWDATAGEN_AREA_TYPE_PICTURE;
          else if (g_ascii_strcasecmp(opt[1], "string") == 0)
            a->type = DRAWDATAGEN_AREA_TYPE_STRING;
          else if (g_ascii_strcasecmp(opt[1], "time") == 0)
            a->type = DRAWDATAGEN_AREA_TYPE_TIME;
          else if (g_ascii_strcasecmp(opt[1], "mask") == 0) {
            a->type = DRAWDATAGEN_AREA_TYPE_MASK;
            a->mask_color_y = 0;
            a->mask_color_u = 128;
            a->mask_color_v = 0;
            a->mask_color_a = 128;
          } else if (g_ascii_strcasecmp(opt[1], "pose") == 0) {
            a->type = DRAWDATAGEN_AREA_TYPE_POSE;
          } else if (g_ascii_strcasecmp(opt[1], "depth") == 0) {
            a->type = DRAWDATAGEN_AREA_TYPE_DEPTH;
          } else if (g_ascii_strcasecmp(opt[1], "clip_score") == 0) {
            a->type = DRAWDATAGEN_AREA_TYPE_CLIP_SCORE;
          }
        } else if (g_ascii_strcasecmp(opt[0], "static") == 0) {
          a->static_content = !!(guint)g_ascii_strtoll(opt[1], NULL, 10);
        } else if (g_ascii_strcasecmp(opt[0], "update_interval") == 0) {
          a->update_interval = (unsigned int)g_ascii_strtoll(opt[1], NULL, 10);
        } else {
          parse_apply_block_kv(a, opt[0], opt[1]);
        }
      }
      g_strfreev(opt);
    }
  }
  g_strfreev(options);

  if (ret == 0 && priv->osd_param.area_num > 0) {
    priv->osd_param.use_osd = 1;
  }
  return ret;
}

static void gst_amba_draw_data_gen_class_init(GstAmbaDrawDataGenClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
  GstBaseTransformClass *btrans_class = GST_BASE_TRANSFORM_CLASS(klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS(klass);

  gobject_class->finalize = gst_amba_draw_data_gen_finalize;
  gobject_class->set_property = gst_amba_draw_data_gen_set_property;
  gobject_class->get_property = gst_amba_draw_data_gen_get_property;

  btrans_class->prepare_output_buffer = GST_DEBUG_FUNCPTR(gst_amba_draw_data_gen_prepare_output_buffer);
  btrans_class->transform = GST_DEBUG_FUNCPTR(gst_amba_draw_data_gen_transform);
  btrans_class->set_caps = GST_DEBUG_FUNCPTR(gst_amba_draw_data_gen_set_caps);
  btrans_class->transform_size = GST_DEBUG_FUNCPTR(gst_amba_draw_data_gen_transform_size);
  btrans_class->transform_caps = GST_DEBUG_FUNCPTR(gst_amba_draw_data_gen_transform_caps);
  btrans_class->fixate_caps = GST_DEBUG_FUNCPTR(gst_amba_draw_data_gen_fixate_caps);
  btrans_class->query = GST_DEBUG_FUNCPTR(gst_amba_draw_data_gen_query);
  btrans_class->propose_allocation = GST_DEBUG_FUNCPTR(gst_amba_draw_data_gen_propose_allocation);
  btrans_class->decide_allocation = GST_DEBUG_FUNCPTR(gst_amba_draw_data_gen_decide_allocation);

  g_object_class_install_property(gobject_class, PROP_COORD_RES,
      g_param_spec_string("coord_res", "CoordRes", "Coordinate resolution WIDTHxHEIGHT (e.g. 1920x1080)",
          "1920x1080", G_PARAM_READWRITE));
  g_object_class_install_property(gobject_class, PROP_OSD,
      g_param_spec_string("osd", "OSD",
          "Per-area config. New: area_id:0;enable:1;roi:0.0.1920.1080;type:bbox;static:0;update_interval:1;bg_color:hex;bbox:{line_color:...};area_id:1;type:depth;area_id:2;type:mask;mask:{y:0,u:128,v:0,a:128}."
          "Color-related keys (font_color, bg_color, ol_color, line_color): YUV family (8bit, uyv565, ayuv*, yuv1555) use hex v/u/y/a MSB..LSB; RGB family (rgb565, bgr565, rgba/bgra/abgr/argb *) use hex b/g/r/a MSB..LSB. bg_color (0=transparent) applies to all types."
          "Use coord_res property for resolution.",
          "", G_PARAM_READWRITE));
  g_object_class_install_property(gobject_class, PROP_OUTPUT_MODE,
      g_param_spec_enum("output-mode", "OutputMode",
          "Output format: draw-data (overlay hardware) or video (for compositor, ARGB)",
          GST_TYPE_AMBA_DRAW_DATA_GEN_OUTPUT_MODE, OUTPUT_MODE_DRAW_DATA,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property(gobject_class, PROP_DRAW_FORMAT,
      g_param_spec_string("draw-format", "DrawFormat",
          "Pixel format: 8bit, rgb565, ayuv4444, argb8888, ayuv8888, etc.",
          DRAWDATAGEN_FORMAT_8BIT, G_PARAM_READWRITE));

  gst_element_class_set_static_metadata(gstelement_class,
      "Amba Draw Data Generator",
      "Filter/Video",
      "Generate draw data (BMP, string, bbox, masks) from ML postprocess for overlay, blur, etc.",
      "pxduan <pxduan@ambarella.com>");

  gst_element_class_add_pad_template(gstelement_class,
      gst_static_pad_template_get(&sink_factory));
  gst_element_class_add_pad_template(gstelement_class,
      gst_static_pad_template_get(&src_factory));

  gst_amba_draw_data_area_flags_meta_get_info();
}

/* Draw-data caps include coord_res so amba_overlay_draw can match encoder ROI without a duplicate property. */
static GstCaps *
gst_amba_draw_data_gen_make_draw_data_caps(const draw_data_gen_priv_t *priv)
{
  gchar buf[32];
  int w = priv->map_width > 0 ? priv->map_width : 1920;
  int h = priv->map_height > 0 ? priv->map_height : 1080;

  g_snprintf(buf, sizeof(buf), "%dx%d", w, h);
  return gst_caps_new_simple(GST_AMBA_DRAW_DATA_CAPS,
      "coord_res", G_TYPE_STRING, buf,
      NULL);
}

/* Element query vfunc: handle GST_QUERY_CAPS on src pad to return draw-data caps.
 * BaseTransform default may not call transform_caps(SINK,...) for first branch in multi-pad link. */
static gboolean gst_amba_draw_data_gen_query(GstBaseTransform *trans,
    GstPadDirection direction, GstQuery *query)
{
  GstAmbaDrawDataGen *self = GST_AMBA_DRAW_DATA_GEN(trans);
  draw_data_gen_priv_t *priv = self->priv;

  if (GST_QUERY_TYPE(query) == GST_QUERY_CAPS && direction == GST_PAD_SRC) {
    GstCaps *filter, *result;

    gst_query_parse_caps(query, &filter);
    if (priv->output_mode == OUTPUT_MODE_VIDEO) {
      result = gst_caps_new_simple("video/x-raw",
          "format", G_TYPE_STRING, "ARGB",
          "width", G_TYPE_INT, priv->map_width,
          "height", G_TYPE_INT, priv->map_height,
          NULL);
    } else {
      result = gst_amba_draw_data_gen_make_draw_data_caps(priv);
    }
    if (filter && !gst_caps_is_any(filter) && !gst_caps_is_empty(filter)) {
      GstCaps *tmp = gst_caps_intersect_full(result, filter, GST_CAPS_INTERSECT_FIRST);
      gst_caps_unref(result);
      result = tmp;
    }
    {
      gchar *s = result && !gst_caps_is_empty(result) ? gst_caps_to_string(result) : NULL;
      GST_INFO_OBJECT(self, "query CAPS direction=SRC -> %s", s ? s : "empty");
      g_free(s);
    }
    gst_query_set_caps_result(query, result);
    gst_caps_unref(result);
    return TRUE;
  }
  return GST_BASE_TRANSFORM_CLASS(gst_amba_draw_data_gen_parent_class)->query(trans, direction, query);
}

static void gst_amba_draw_data_gen_init(GstAmbaDrawDataGen *self)
{
  draw_data_gen_priv_t *priv = g_malloc0(sizeof(draw_data_gen_priv_t));

  self->priv = priv;
  priv->draw_format = AMBA_DRAW_FORMAT_8BIT_CLUT;
  priv->draw_pix_size = 1;
  priv->map_width = 1920;
  priv->map_height = 1080;
  priv->bbox_area_enable = 1;
  priv->font_size = 16;
  priv->font_file[0] = '\0';
  priv->time_format = 0;
  priv->output_mode = OUTPUT_MODE_DRAW_DATA;
  g_strlcpy(priv->draw_format_str, DRAWDATAGEN_FORMAT_8BIT, sizeof(priv->draw_format_str));
}

static void gst_amba_draw_data_gen_finalize(GObject *object)
{
  GstAmbaDrawDataGen *self = GST_AMBA_DRAW_DATA_GEN(object);
  draw_data_gen_priv_t *priv;
  if (self->priv) {
    priv = self->priv;
    if (priv->bitmap.buf) {
      g_free(priv->bitmap.buf);
      priv->bitmap.buf = NULL;
    }
    drawdatagen_text_deinit(&priv->area_str, &priv->area_time);
    drawdatagen_area_cache_clear(priv);
    g_free(self->priv);
    self->priv = NULL;
  }
  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void gst_amba_draw_data_gen_set_property(GObject *object, guint prop_id,
    const GValue *value, GParamSpec *pspec)
{
  GstAmbaDrawDataGen *self = GST_AMBA_DRAW_DATA_GEN(object);
  draw_data_gen_priv_t *priv = self->priv;
  const char *s;

  switch (prop_id) {
    case PROP_COORD_RES:
      s = g_value_get_string(value);
      if (s && s[0] && sscanf(s, "%dx%d", &priv->map_width, &priv->map_height) >= 2) {
        if (priv->map_width <= 0)
          priv->map_width = 1920;
        if (priv->map_height <= 0)
          priv->map_height = 1080;
        memset(priv->last_area_ready, 0, sizeof(priv->last_area_ready));
      }
      break;
    case PROP_OSD: {
      const char *s = g_value_get_string(value);
      g_strlcpy(priv->osd_str, s ? s : "", sizeof(priv->osd_str));
      if (s && s[0]) {
        if (parse_osd_drawdatagen(priv, priv->osd_str) < 0)
          GST_WARNING_OBJECT(self, "parse_osd failed, using defaults");
      } else {
        priv->osd_param.use_osd = 0;
        memset(priv->last_area_ready, 0, sizeof(priv->last_area_ready));
        drawdatagen_area_cache_clear(priv);
      }
      break;
    }
    case PROP_OUTPUT_MODE:
      priv->output_mode = g_value_get_enum(value);
      if (priv->output_mode != OUTPUT_MODE_DRAW_DATA && priv->output_mode != OUTPUT_MODE_VIDEO)
        priv->output_mode = OUTPUT_MODE_DRAW_DATA;
      break;
    case PROP_DRAW_FORMAT: {
      const char *s = g_value_get_string(value);
      guint ps = 1;
      int fmt = drawdatagen_parse_format(s ? s : DRAWDATAGEN_FORMAT_8BIT, &ps);
      if (fmt != AMBA_DRAW_FORMAT_NONE) {
        priv->draw_format = fmt;
        priv->draw_pix_size = ps;
        g_strlcpy(priv->draw_format_str, s ? s : DRAWDATAGEN_FORMAT_8BIT, sizeof(priv->draw_format_str));
      }
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
      break;
  }
}

static void gst_amba_draw_data_gen_get_property(GObject *object, guint prop_id,
    GValue *value, GParamSpec *pspec)
{
  GstAmbaDrawDataGen *self = GST_AMBA_DRAW_DATA_GEN(object);
  draw_data_gen_priv_t *priv = self->priv;
  char buf[128];

  switch (prop_id) {
    case PROP_COORD_RES:
      g_snprintf(buf, sizeof(buf), "%dx%d", priv->map_width, priv->map_height);
      g_value_set_string(value, buf);
      break;
    case PROP_OSD:
      g_value_set_string(value, priv->osd_str);
      break;
    case PROP_OUTPUT_MODE:
      g_value_set_enum(value, priv->output_mode);
      break;
    case PROP_DRAW_FORMAT:
      g_value_set_string(value, priv->draw_format_str);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
      break;
  }
}

static gboolean gst_amba_draw_data_gen_set_caps(GstBaseTransform *trans,
    GstCaps *incaps, GstCaps *outcaps)
{
  GstAmbaDrawDataGen *self = GST_AMBA_DRAW_DATA_GEN(trans);
  draw_data_gen_priv_t *priv = self->priv;
  GstStructure *st;
  const char *name;

  if (priv->output_mode == OUTPUT_MODE_VIDEO && outcaps && gst_caps_get_size(outcaps) > 0) {
    GstStructure *ost = gst_caps_get_structure(outcaps, 0);
    const char *oname = ost ? gst_structure_get_name(ost) : NULL;
    if (!oname || g_strcmp0(oname, "video/x-raw") != 0) {
      GST_WARNING_OBJECT(self, "set_caps: output_mode=video requires video/x-raw outcaps");
      return FALSE;
    }
  }
  {
    gchar *s = incaps ? gst_caps_to_string(incaps) : NULL;
    GST_INFO_OBJECT(self, "set_caps: incaps=%s", s ? s : "(null)");
    g_free(s);
  }
  st = gst_caps_get_structure(incaps, 0);
  if (!st) {
    return FALSE;
  }
  name = gst_structure_get_name(st);

  if (g_strcmp0(name, GST_AMBA_ML_DECODED_CAPS) == 0) {
    priv->input_media_type = INPUT_TYPE_ML_DECODED;
    {
      const char *coord_res = gst_structure_get_string(st, "coord_res");
      if (coord_res && coord_res[0]) {
        int w = 0, h = 0;
        if (sscanf(coord_res, "%dx%d", &w, &h) >= 2 && w > 0 && h > 0) {
          priv->map_width = w;
          priv->map_height = h;
          if (!priv->bbox_roi.width) {
            priv->bbox_roi.x = 0;
            priv->bbox_roi.y = 0;
            priv->bbox_roi.width = w;
            priv->bbox_roi.height = h;
          }
        }
      }
    }
  } else if (g_strcmp0(name, "image/bmp") == 0) {
    priv->input_media_type = INPUT_TYPE_IMAGE_BMP;
  } else if (g_strcmp0(name, "text/x-raw") == 0) {
    priv->input_media_type = INPUT_TYPE_TEXT;
  } else if (g_strcmp0(name, "video/x-raw") == 0) {
    const char *fmt = gst_structure_get_string(st, "format");
    if (fmt && g_ascii_strcasecmp(fmt, "GRAY8") == 0) {
      gint w = 0, h = 0;
      gst_structure_get_int(st, "width", &w);
      gst_structure_get_int(st, "height", &h);
      if (w > 0 && h > 0) {
        priv->input_media_type = INPUT_TYPE_VIDEO_GRAY8;
        priv->map_width = w;
        priv->map_height = h;
        priv->gray8_roi.x = 0;
        priv->gray8_roi.y = 0;
        priv->gray8_roi.width = w;
        priv->gray8_roi.height = h;
      } else {
        GST_WARNING_OBJECT(self, "set_caps: video/x-raw GRAY8 requires width/height");
        return FALSE;
      }
    } else {
      GST_WARNING_OBJECT(self, "set_caps: video/x-raw only supports format=GRAY8");
      return FALSE;
    }
  } else if (g_strcmp0(name, GST_AMBA_DRAW_DATA_GEN_TRIGGER_CAPS) == 0) {
    priv->input_media_type = INPUT_TYPE_OSD_TRIGGER;
    {
      const char *coord_res = gst_structure_get_string(st, "coord_res");
      if (coord_res && coord_res[0]) {
        int w = 0, h = 0;
        if (sscanf(coord_res, "%dx%d", &w, &h) >= 2 && w > 0 && h > 0) {
          priv->map_width = w;
          priv->map_height = h;
        }
      }
    }
  } else {
    GST_WARNING_OBJECT(self, "set_caps: unsupported incaps name=%s", name ? name : "(null)");
    return FALSE;
  }
  GST_INFO_OBJECT(self, "set_caps: OK, input_media_type=%d map=%dx%d",
      priv->input_media_type, priv->map_width, priv->map_height);
  return TRUE;
}

/* transform_caps(direction, caps, filter): given caps on pad in direction, return
 * allowed caps on the other pad.
 * - SRC: given src caps (what we produce), return allowed sink caps (what we accept).
 * - SINK: given sink caps (what we receive), return allowed src caps (what we produce). */
static void get_map_dims_from_caps(GstAmbaDrawDataGen *self, GstCaps *caps,
    int *w, int *h)
{
  draw_data_gen_priv_t *priv = self->priv;
  *w = priv->map_width;
  *h = priv->map_height;
  if (caps && gst_caps_get_size(caps) > 0) {
    GstStructure *st = gst_caps_get_structure(caps, 0);
    const char *name = st ? gst_structure_get_name(st) : NULL;
    if (g_strcmp0(name, GST_AMBA_ML_DECODED_CAPS) == 0) {
      const char *coord_res = gst_structure_get_string(st, "coord_res");
      if (coord_res && sscanf(coord_res, "%dx%d", w, h) >= 2 && *w > 0 && *h > 0) {
        return;
      }
    } else if (g_strcmp0(name, "video/x-raw") == 0) {
      gint gw = 0, gh = 0;
      gst_structure_get_int(st, "width", &gw);
      gst_structure_get_int(st, "height", &gh);
      if (gw > 0 && gh > 0) {
        *w = gw;
        *h = gh;
        return;
      }
    } else if (g_strcmp0(name, GST_AMBA_DRAW_DATA_GEN_TRIGGER_CAPS) == 0) {
      const char *coord_res = gst_structure_get_string(st, "coord_res");
      if (coord_res && sscanf(coord_res, "%dx%d", w, h) >= 2 && *w > 0 && *h > 0) {
        return;
      }
    }
  }
}

static GstCaps *gst_amba_draw_data_gen_transform_caps(GstBaseTransform *trans,
    GstPadDirection direction, GstCaps *caps, GstCaps *filter)
{
  GstCaps *result;
  GstAmbaDrawDataGen *self = GST_AMBA_DRAW_DATA_GEN(trans);
  draw_data_gen_priv_t *priv = self->priv;

  GST_INFO_OBJECT(self, "transform_caps: direction=%s output_mode=%d",
      direction == GST_PAD_SRC ? "SRC" : "SINK", priv->output_mode);
  if (direction == GST_PAD_SRC) {
    /* Given we produce draw-data or video on src, we accept ml-decoded, image/bmp, text, video GRAY8 on sink */
    GstCaps *c1 = gst_caps_from_string(GST_AMBA_ML_DECODED_CAPS);
    GstCaps *c2 = gst_caps_from_string("image/bmp");
    GstCaps *c3 = gst_caps_from_string("text/x-raw");
    GstCaps *c4 = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "GRAY8", NULL);
    GstCaps *c5 = gst_caps_from_string(GST_AMBA_DRAW_DATA_GEN_TRIGGER_CAPS);
    result = gst_caps_copy(c1);
    gst_caps_unref(c1);
    gst_caps_append(result, c2);
    gst_caps_append(result, c3);
    gst_caps_append(result, c4);
    gst_caps_append(result, c5);
    if (filter && !gst_caps_is_any(filter) && !gst_caps_is_empty(filter)) {
      GstCaps *tmp = gst_caps_intersect_full(result, filter, GST_CAPS_INTERSECT_FIRST);
      gst_caps_unref(result);
      result = tmp;
    }
  } else {
    /* Given we receive on sink, we produce draw-data or video on src */
    if (priv->output_mode == OUTPUT_MODE_VIDEO) {
      int mw, mh;
      get_map_dims_from_caps(self, caps, &mw, &mh);
      result = gst_caps_new_simple("video/x-raw",
          "format", G_TYPE_STRING, "ARGB",
          "width", G_TYPE_INT, mw,
          "height", G_TYPE_INT, mh,
          NULL);
    } else {
      result = gst_amba_draw_data_gen_make_draw_data_caps(priv);
    }

    /* Only apply filter when intersection is non-empty. When linking to overlay,
     * base transform may pass upstream peer caps (e.g. image/bmp) as filter;
     * intersecting draw-data with image/bmp yields empty and breaks the link. */
    if (filter && !gst_caps_is_any(filter) && !gst_caps_is_empty(filter)) {
      GstCaps *tmp = gst_caps_intersect_full(result, filter, GST_CAPS_INTERSECT_FIRST);
      if (tmp && !gst_caps_is_empty(tmp)) {
        gst_caps_unref(result);
        result = tmp;
      } else {
        if (tmp)
          gst_caps_unref(tmp);
      }
    }
  }
  {
    gchar *s = result ? gst_caps_to_string(result) : NULL;
    GST_INFO_OBJECT(self, "transform_caps: result=%s", s ? s : "(null)");
    g_free(s);
  }
  return result;
}

/* fixate_caps(direction, caps, othercaps): fixate caps for the OTHER pad.
 * caps = pad in direction, othercaps = proposed caps for the other pad.
 * We must return fixated othercaps (sink->src: fixate draw-data; src->sink: fixate ml-decoded). */
static GstCaps *gst_amba_draw_data_gen_fixate_caps(GstBaseTransform *trans,
    GstPadDirection direction, GstCaps *caps, GstCaps *othercaps)
{
  GstCaps *result;
  GstAmbaDrawDataGen *self = GST_AMBA_DRAW_DATA_GEN(trans);
  draw_data_gen_priv_t *priv = self->priv;

  GST_INFO_OBJECT(self, "fixate_caps: direction=%s caps_any=%d other_any=%d output_mode=%d",
      direction == GST_PAD_SRC ? "SRC" : "SINK",
      gst_caps_is_any(caps), gst_caps_is_any(othercaps), priv->output_mode);
  if (gst_caps_is_any(caps) || gst_caps_is_any(othercaps)) {
    if (direction == GST_PAD_SRC) {
      result = gst_caps_from_string(
          GST_AMBA_ML_DECODED_CAPS ", coord_res=(string)1920x1080");
    } else {
      if (priv->output_mode == OUTPUT_MODE_VIDEO) {
        result = gst_caps_new_simple("video/x-raw",
            "format", G_TYPE_STRING, "ARGB",
            "width", G_TYPE_INT, priv->map_width,
            "height", G_TYPE_INT, priv->map_height,
            NULL);
      } else {
        result = gst_amba_draw_data_gen_make_draw_data_caps(priv);
      }
    }
    if (result)
      result = gst_caps_fixate(result);
    return result;
  }
  if (othercaps && !gst_caps_is_empty(othercaps)) {
    result = gst_caps_copy(othercaps);
    result = gst_caps_truncate(result);
    if (result)
      result = gst_caps_fixate(result);
  } else {
    if (direction == GST_PAD_SRC) {
      result = gst_caps_from_string(
          GST_AMBA_ML_DECODED_CAPS ", coord_res=(string)1920x1080");
    } else {
      if (priv->output_mode == OUTPUT_MODE_VIDEO) {
        result = gst_caps_new_simple("video/x-raw",
            "format", G_TYPE_STRING, "ARGB",
            "width", G_TYPE_INT, priv->map_width,
            "height", G_TYPE_INT, priv->map_height,
            NULL);
      } else {
        result = gst_amba_draw_data_gen_make_draw_data_caps(priv);
      }
    }
    if (result)
      result = gst_caps_fixate(result);
  }
  {
    gchar *s = result ? gst_caps_to_string(result) : NULL;
    GST_INFO_OBJECT(self, "fixate_caps: result=%s", s ? s : "(null)");
    g_free(s);
  }
  return result;
}

/* Return empty buffer so transform can append its own memory. Avoids modifying
 * pool-allocated buffers (gst_buffer_remove_all_memory) which causes
 * gst_mini_object_unref CRITICAL when the pool tries to reclaim them. */
static GstFlowReturn gst_amba_draw_data_gen_prepare_output_buffer(GstBaseTransform *trans,
    GstBuffer *inbuf, GstBuffer **outbuf)
{
  (void)inbuf;
  *outbuf = gst_buffer_new();
  if (!*outbuf) {
    GST_ERROR_OBJECT(trans, "Failed to allocate output buffer");
    return GST_FLOW_ERROR;
  }
  return GST_FLOW_OK;
}

/* Clear downstream pools so we never receive pool buffers. Must clear in both
 * propose_allocation and decide_allocation - negotiation may use either. */
static void clear_allocation_pools(GstQuery *query)
{
  guint n_pools = gst_query_get_n_allocation_pools(query);
  guint i;
  for (i = 0; i < n_pools; i++) {
    gst_query_set_nth_allocation_pool(query, i, NULL, 0, 0, 0);
  }
}

/* Skip allocation proposal entirely - we generate new buffers and don't need
 * to participate in allocation negotiation. Calling parent can cause deadlock
 * in some pipeline topologies (e.g. with progressreport). Clear pools so
 * GstBaseTransform never uses pool buffers. */
static gboolean gst_amba_draw_data_gen_propose_allocation(GstBaseTransform *trans,
    GstQuery *decide_query, GstQuery *query)
{
  (void)decide_query;
  clear_allocation_pools(query);
  GST_INFO_OBJECT(trans, "propose_allocation: cleared pools, skip allocation");
  return TRUE;
}

/* Skip parent decide_allocation - we use prepare_output_buffer (gst_buffer_new)
 * and never use a pool. Parent's pool setup can cause gst_mini_object_unref
 * CRITICAL when buffers are freed (pool expects buffers it allocated). */
static gboolean gst_amba_draw_data_gen_decide_allocation(GstBaseTransform *trans,
    GstQuery *query)
{
  clear_allocation_pools(query);
  GST_INFO_OBJECT(trans, "decide_allocation: cleared pools, use prepare_output_buffer only");
  return TRUE;
}

static gsize compute_area_size(draw_data_gen_priv_t *priv, amba_rect_t *roi)
{
  gsize pitch, h;
  if (!roi->width)
    roi->width = priv->map_width;
  if (!roi->height)
    roi->height = priv->map_height;
  roi->width = ROUND_DOWN(roi->width, OVERLAY_WIDTH_ALIGN);
  roi->height = ROUND_DOWN(roi->height, OVERLAY_HEIGHT_ALIGN);
  pitch = ROUND_UP(ROUND_UP(roi->width, OSD_BUF_WIDTH_ALIGN) * priv->draw_pix_size, OSD_BUF_PITCH_ALIGN);
  h = (gsize)roi->height;
  return OSD_AREA_BLOCK_HEADER_SIZE + OVERLAY_CLUT_SIZE + h * pitch;
}

static gboolean gst_amba_draw_data_gen_transform_size(GstBaseTransform *trans,
    GstPadDirection direction, GstCaps *caps, gsize size, GstCaps *othercaps, gsize *othersize)
{
  GstAmbaDrawDataGen *self = GST_AMBA_DRAW_DATA_GEN(trans);
  draw_data_gen_priv_t *priv = self->priv;
  gsize header_sz, total_sz = 0;
  amba_rect_t roi;
  gboolean has_bmp, has_str;

  GST_INFO_OBJECT(self, "transform_size: direction=%s size=%zu output_mode=%d",
      direction == GST_PAD_SRC ? "SRC" : "SINK", size, priv->output_mode);
  (void)size;

  if (priv->output_mode == OUTPUT_MODE_VIDEO && direction == GST_PAD_SRC) {
    /* Video output: size = width * height * 4 (ARGB) */
    int mw = priv->map_width, mh = priv->map_height;
    get_map_dims_from_caps(self, caps, &mw, &mh);
    *othersize = (gsize)mw * mh * 4;
    GST_INFO_OBJECT(self, "transform_size: video othersize=%zu (%dx%d)", *othersize, mw, mh);
    return TRUE;
  }

  (void)othercaps;

  /* When direction==GST_PAD_SRC, caps are sink caps - infer input type */
  if (priv->osd_param.use_osd) {
    unsigned int aid;
    int def_font_h;
    for (aid = 0; aid < priv->osd_param.area_num && aid < MAX_OVERLAY_AREA_NUM; aid++) {
      drawdatagen_osd_area_t *a = &priv->osd_param.area[aid];
      if (!a->enable)
        continue;
      if (a->type == DRAWDATAGEN_AREA_TYPE_PICTURE && !a->bmp_file[0])
        continue;
      if (a->type == DRAWDATAGEN_AREA_TYPE_STRING && !a->str[0])
        continue;
      roi = a->roi;
      def_font_h = (a->font_size > 0 ? a->font_size : priv->font_size) + 8;
      if (!roi.width)
        roi.width = (a->type == DRAWDATAGEN_AREA_TYPE_BBOX || a->type == DRAWDATAGEN_AREA_TYPE_POSE) ?
            priv->map_width : 256;
      if (!roi.height)
        roi.height = (a->type == DRAWDATAGEN_AREA_TYPE_BBOX || a->type == DRAWDATAGEN_AREA_TYPE_POSE) ?
            priv->map_height : def_font_h;
      total_sz += compute_area_size(priv, &roi);
    }
  } else {
    has_bmp = (priv->bmp_area_enable && priv->bmp_file[0]);
    has_str = (priv->str_area_enable && priv->str_text[0]);
    {
      gboolean has_bbox = priv->bbox_area_enable;
      if (direction == GST_PAD_SRC && caps && gst_caps_get_size(caps) > 0) {
        GstStructure *st = gst_caps_get_structure(caps, 0);
        const char *name = st ? gst_structure_get_name(st) : NULL;
        const char *fmt = st ? gst_structure_get_string(st, "format") : NULL;
        if (g_strcmp0(name, "image/bmp") == 0) {
          has_bmp = TRUE;
          has_bbox = FALSE;
        } else if (g_strcmp0(name, "text/x-raw") == 0) {
          has_str = TRUE;
          has_bbox = FALSE;
        } else if (g_strcmp0(name, "video/x-raw") == 0 && fmt && g_ascii_strcasecmp(fmt, "GRAY8") == 0) {
          gint w = 0, h = 0;
          gst_structure_get_int(st, "width", &w);
          gst_structure_get_int(st, "height", &h);
          if (w > 0 && h > 0) {
            roi.x = 0;
            roi.y = 0;
            roi.width = w;
            roi.height = h;
            total_sz = compute_area_size(priv, &roi);
            *othersize = total_sz;
            GST_INFO_OBJECT(self, "transform_size: GRAY8 othersize=%zu", *othersize);
            return TRUE;
          }
        }
      }
      header_sz = 0;
      if (has_bbox) {
        roi = priv->bbox_roi;
        total_sz += compute_area_size(priv, &roi);
      }
    }
    if (has_bmp) {
      roi = priv->bmp_roi;
      if (!roi.width)
        roi.width = 128;
      if (!roi.height)
        roi.height = 128;
      total_sz += compute_area_size(priv, &roi);
    }
    if (has_str) {
      roi = priv->str_roi;
      if (!roi.width)
        roi.width = 256;
      if (!roi.height)
        roi.height = priv->font_size + 8;
      total_sz += compute_area_size(priv, &roi);
    }
    if (priv->time_area_enable) {
      roi = priv->time_roi;
      if (!roi.width)
        roi.width = 256;
      if (!roi.height)
        roi.height = priv->font_size + 8;
      total_sz += compute_area_size(priv, &roi);
    }
  }
  if (total_sz == 0) {
    roi = priv->bbox_roi;
    if (!roi.width)
      roi.width = priv->map_width;
    if (!roi.height)
      roi.height = priv->map_height;
    if (priv->osd_param.use_osd && (!roi.width || !roi.height)) {
      roi.width = roi.width ? roi.width : 256;
      roi.height = roi.height ? roi.height : priv->font_size + 8;
    }
    total_sz = compute_area_size(priv, &roi);
  }
  *othersize = header_sz + total_sz;
  GST_INFO_OBJECT(self, "transform_size: othersize=%zu", *othersize);
  return TRUE;
}

/* Render draw blocks to video buffer (ARGB). Each block: [header][CLUT][pixels].
 * Supports: 8bit CLUT (bg=CLUT a==0), 16bit (transparent=0x0000), 32bit (transparent=0x00000000).
 * First block: draw all. Later blocks: when CLUT background entry a==0, skip bg to preserve lower layers. */
static GstFlowReturn render_draw_blocks_to_video(GstBuffer *draw_buf, guint8 *vid_buf,
    int vid_width, int vid_height, int vid_stride)
{
  guint i;
  if (vid_stride <= 0) {
    vid_stride = vid_width * 4;
  }
  memset(vid_buf, 0, (size_t)vid_stride * vid_height);
  for (i = 0; i < gst_buffer_n_memory(draw_buf); i++) {
    GstMemory *mem = gst_buffer_peek_memory(draw_buf, i);
    GstMapInfo map;
    if (!mem || !gst_memory_map(mem, &map, GST_MAP_READ)) {
      continue;
    }
    if (map.size < OSD_AREA_BLOCK_PIXEL_OFFSET) {
      goto next;
    }
    {
      osd_area_block_header_t *hdr = (osd_area_block_header_t *)map.data;
      if (hdr->magic != OSD_AREA_BLOCK_MAGIC || hdr->block_size > map.size) {
        goto next;
      }
      {
        int draw_fmt = (int)hdr->draw_format;
        guint8 *pixels = map.data + OSD_AREA_BLOCK_PIXEL_OFFSET;
        amba_rect_t *r = &hdr->rect;
        gsize area_pitch = (gsize)r->pitch;
        int pix_size = (draw_fmt == AMBA_DRAW_FORMAT_8BIT_CLUT) ? 1 :
            (draw_fmt >= AMBA_DRAW_FORMAT_16BIT_FIRST && draw_fmt < AMBA_DRAW_FORMAT_16BIT_LAST) ? 2 : 4;
        int by, bx;
        if (area_pitch == 0) {
          area_pitch = (gsize)r->width * pix_size;
        }
        /* First block (i==0): draw all. Later blocks: skip transparent bg to preserve lower layers. */
        amba_draw_clut_t *clut_base = (amba_draw_clut_t *)(map.data + OSD_AREA_BLOCK_CLUT_OFFSET);
        gboolean skip_bg = (i > 0 && clut_base[AMBA_DRAW_CLUT_ENTRY_BACKGROUND].a == 0);
        if (draw_fmt == AMBA_DRAW_FORMAT_8BIT_CLUT) {
          /* 8bit: skip transparent (a==0) so compositor shows video below; avoids yellow/opaque bg */
          amba_draw_clut_t *clut = clut_base;
          for (by = 0; by < r->height; by++) {
            int vy = r->y + by;
            if (vy < 0 || vy >= vid_height)
              continue;
            for (bx = 0; bx < r->width; bx++) {
              int vx = r->x + bx;
              if (vx < 0 || vx >= vid_width)
                continue;
              guint8 idx = pixels[by * area_pitch + bx];
              amba_draw_clut_t *c = &clut[idx];
              /* Skip transparent pixels so compositor preserves video layer (fixes full-screen yellow) */
              if (c->a == 0)
                continue;
              {
              uint32_t out_px = drawdatagen_clut_to_argb(c);
              /* GStreamer ARGB expects bytes [A,R,G,B] (A at lowest address). Our (a<<24)|(r<<16)|(g<<8)|b
               * gives [B,G,R,A] in LE. Need (b<<24)|(g<<16)|(r<<8)|a for [A,R,G,B]. */
              uint32_t a = (out_px >> 24) & 0xFF, r = (out_px >> 16) & 0xFF, g = (out_px >> 8) & 0xFF, b = out_px & 0xFF;
              out_px = ((uint32_t)b << 24) | ((uint32_t)g << 16) | ((uint32_t)r << 8) | (uint32_t)a;
              uint32_t *dst = (uint32_t *)(vid_buf + vy * vid_stride + vx * 4);
              *dst = out_px;
              }
            }
          }
        } else {
          /* 16bit/32bit: transparent = pixel==0 per format */
          for (by = 0; by < r->height; by++) {
            int vy = r->y + by;
            if (vy < 0 || vy >= vid_height)
              continue;
            for (bx = 0; bx < r->width; bx++) {
              int vx = r->x + bx;
              if (vx < 0 || vx >= vid_width)
                continue;
              uint32_t pixel;
              if (pix_size == 2) {
                uint16_t *row = (uint16_t *)(pixels + by * area_pitch);
                pixel = row[bx];
              } else {
                uint32_t *row = (uint32_t *)(pixels + by * area_pitch);
                pixel = row[bx];
              }
              if (skip_bg && pixel == 0)
                continue;
              {
                uint32_t out_px = drawdatagen_pixel_to_argb(pixel, draw_fmt);
                uint32_t a = (out_px >> 24) & 0xFF, r = (out_px >> 16) & 0xFF, g = (out_px >> 8) & 0xFF, b = out_px & 0xFF;
                out_px = ((uint32_t)b << 24) | ((uint32_t)g << 16) | ((uint32_t)r << 8) | (uint32_t)a;
                uint32_t *dst = (uint32_t *)(vid_buf + vy * vid_stride + vx * 4);
                *dst = out_px;
              }
            }
          }
        }
      }
    }
next:
    gst_memory_unmap(mem, &map);
  }
  return GST_FLOW_OK;
}

/* Per-area: output [area_header][CLUT][pixels] in one memory. No global header.
 * area_override: when non-NULL (from osd), use it for roi/font/bmp/str/time params.
 * bmp_data_override/bmp_size_override: when non-NULL and >0, draw BMP from memory (no temp file).
 * block_area_id: OSD slot for overlay (use osd area index); USE_INDEX = map by GstMemory order. */
static GstFlowReturn draw_one_area_per_area(GstAmbaDrawDataGen *self, int area_type,
    amba_rect_t *roi, amba_ml_bbox_result_t *ml_result, uint32_t det_num,
    GstBuffer *outbuf, const char *bmp_path_override, const char *str_override,
    const drawdatagen_osd_area_t *area_override,
    const unsigned char *bmp_data_override, size_t bmp_size_override,
    unsigned char block_area_id)
{
  draw_data_gen_priv_t *priv = self->priv;
  GstMemory *mem_area;
  GstMapInfo map_area;
  osd_area_block_header_t *block_hdr;
  amba_draw_clut_t *clut;
  guchar *data_buf;
  gsize area_pitch, block_size;
  int draw_format = priv->draw_format;
  unsigned char stream_rotate = 0;

  roi->width = ROUND_DOWN(roi->width, OVERLAY_WIDTH_ALIGN);
  roi->height = ROUND_DOWN(roi->height, OVERLAY_HEIGHT_ALIGN);
  area_pitch = ROUND_UP(ROUND_UP(roi->width, OSD_BUF_WIDTH_ALIGN) * priv->draw_pix_size, OSD_BUF_PITCH_ALIGN);
  roi->pitch = (int)area_pitch;
  block_size = OSD_AREA_BLOCK_HEADER_SIZE + OVERLAY_CLUT_SIZE + (gsize)roi->height * area_pitch;

  mem_area = gst_allocator_alloc(NULL, block_size, NULL);
  if (!mem_area)
    return GST_FLOW_ERROR;
  gst_buffer_append_memory(outbuf, mem_area);
  if (!gst_memory_map(mem_area, &map_area, GST_MAP_WRITE)) {
    return GST_FLOW_ERROR;
  }
  block_hdr = (osd_area_block_header_t *)map_area.data;
  memset(block_hdr, 0, OSD_AREA_BLOCK_HEADER_SIZE);
  block_hdr->magic = OSD_AREA_BLOCK_MAGIC;
  block_hdr->block_size = (unsigned int)block_size;
  block_hdr->enable = 1;
  block_hdr->draw_format = (unsigned char)draw_format;
  block_hdr->area_id = block_area_id;
  block_hdr->rect = *roi;

  clut = (amba_draw_clut_t *)(map_area.data + OSD_AREA_BLOCK_CLUT_OFFSET);
  data_buf = map_area.data + OSD_AREA_BLOCK_PIXEL_OFFSET;
  memset(clut, 0, OVERLAY_CLUT_SIZE);

  if (area_type == DRAWDATAGEN_AREA_TYPE_BBOX || area_type == DRAWDATAGEN_AREA_TYPE_POSE) {
    if (drawdatagen_draw_ml_overlay(self, roi, ml_result, det_num, area_override,
            clut, data_buf, (unsigned int)area_pitch, draw_format, 0, area_type) < 0) {
      GST_ERROR_OBJECT(self, "drawdatagen_draw_ml_overlay failed");
      gst_memory_unmap(mem_area, &map_area);
      return GST_FLOW_ERROR;
    }
  } else if (area_type == DRAWDATAGEN_AREA_TYPE_PICTURE) {
    drawdatagen_fill_background_for_area(clut, data_buf, roi->height, area_pitch, draw_format,
        area_override ? area_override->bg_color_hex : 0);
    if (bmp_data_override && bmp_size_override > 0) {
      if (drawdatagen_bmp_draw_from_buffer(bmp_data_override, bmp_size_override, roi, clut,
              data_buf, &priv->bitmap, draw_format, (unsigned int)area_pitch) < 0) {
        GST_ERROR_OBJECT(self, "drawdatagen_bmp_draw_from_buffer failed");
        gst_memory_unmap(mem_area, &map_area);
        return GST_FLOW_ERROR;
      }
    } else {
      const char *bmp_path = bmp_path_override ? bmp_path_override :
          (area_override && area_override->bmp_file[0]) ? area_override->bmp_file : priv->bmp_file;
      if (drawdatagen_bmp_draw(bmp_path, roi, clut, data_buf, &priv->bitmap, draw_format,
              (unsigned int)area_pitch) < 0) {
        GST_ERROR_OBJECT(self, "drawdatagen_bmp_draw failed");
        gst_memory_unmap(mem_area, &map_area);
        return GST_FLOW_ERROR;
      }
    }
  } else if (area_type == DRAWDATAGEN_AREA_TYPE_STRING) {
    const char *str_src = str_override ? str_override :
        (area_override && area_override->str[0]) ? area_override->str : priv->str_text;
    const char *ff = (area_override && area_override->font_name[0]) ? area_override->font_name : priv->font_file;
    if (!ff || !ff[0])
      ff = DRAWDATAGEN_DEFAULT_FONT_PATH;
    int fs = (area_override && area_override->font_size > 0) ? area_override->font_size : priv->font_size;
    if (area_override)
      apply_string_colors_from_area(&priv->area_str.data.text, area_override, draw_format);
    drawdatagen_fill_background_for_area(clut, data_buf, roi->height, area_pitch, draw_format,
        area_override ? area_override->bg_color_hex : 0);
    if (drawdatagen_str_draw(str_src, ff, fs, roi,
        &priv->area_str, clut, data_buf, &priv->bitmap, draw_format, stream_rotate) < 0) {
      GST_ERROR_OBJECT(self, "drawdatagen_str_draw failed");
      gst_memory_unmap(mem_area, &map_area);
      return GST_FLOW_ERROR;
    }
  } else if (area_type == DRAWDATAGEN_AREA_TYPE_CLIP_SCORE) {
    const char *ff = (area_override && area_override->font_name[0]) ? area_override->font_name : priv->font_file;
    int fs = (area_override && area_override->font_size > 0) ? area_override->font_size : priv->font_size;

    if (!priv->pending_ml_embedding)
      return GST_FLOW_OK;
    if (!ff || !ff[0])
      ff = DRAWDATAGEN_DEFAULT_FONT_PATH;
    if (area_override)
      apply_string_colors_from_area(&priv->area_str.data.text, area_override, draw_format);
    if (drawdatagen_clip_score_draw(roi, priv->pending_ml_embedding, ff, fs,
            &priv->area_str, &priv->bitmap, clut, data_buf, (unsigned int)area_pitch,
            draw_format, stream_rotate, area_override ? area_override->bg_color_hex : 0, 0) < 0) {
      gst_memory_unmap(mem_area, &map_area);
      return GST_FLOW_ERROR;
    }
  } else if (area_type == DRAWDATAGEN_AREA_TYPE_TIME) {
    const char *pre = (area_override && area_override->time_pre_str[0]) ? area_override->time_pre_str : priv->time_pre_str;
    const char *suf = (area_override && area_override->time_suf_str[0]) ? area_override->time_suf_str : priv->time_suf_str;
    int en_msec = area_override ? area_override->time_en_msec : priv->time_en_msec;
    int fmt = area_override ? area_override->time_format : priv->time_format;
    int is12 = area_override ? area_override->time_is_12h : priv->time_is_12h;
    const char *ff = (area_override && area_override->font_name[0]) ? area_override->font_name : priv->font_file;
    if (!ff || !ff[0])
      ff = DRAWDATAGEN_DEFAULT_FONT_PATH;
    int fs = (area_override && area_override->font_size > 0) ? area_override->font_size : priv->font_size;
    if (area_override)
      apply_string_colors_from_area(&priv->area_time.data.time.text, area_override, draw_format);
    drawdatagen_fill_background_for_area(clut, data_buf, roi->height, area_pitch, draw_format,
        area_override ? area_override->bg_color_hex : 0);
    if (drawdatagen_time_draw(pre, suf, en_msec, fmt, is12, ff, fs, roi,
        &priv->area_time, clut, data_buf, &priv->bitmap, draw_format, stream_rotate) < 0) {
      GST_ERROR_OBJECT(self, "drawdatagen_time_draw failed");
      gst_memory_unmap(mem_area, &map_area);
      return GST_FLOW_ERROR;
    }
  }

  {
    unsigned int slot_idx = (block_area_id != OSD_AREA_BLOCK_AREA_ID_USE_INDEX) ?
        (unsigned int)block_area_id : priv->out_area_slot;
    gsize clen = block_size - OSD_AREA_BLOCK_CLUT_OFFSET;
    drawdatagen_apply_block_change_flags(priv, block_hdr, roi, slot_idx,
        map_area.data + OSD_AREA_BLOCK_CLUT_OFFSET, clen, outbuf);
    priv->out_area_slot++;
  }
  gst_memory_unmap(mem_area, &map_area);
  return GST_FLOW_OK;
}

/* Draw GRAY8 mask to existing 8bit CLUT buffer. Non-zero mask -> color_idx.
 * Scales src to roi when dimensions differ (e.g. 640x640 seg -> 1920x1080 overlay).
 * apply_dilation: if true, dilate color_idx pixels 3x3 (for thin lane lines). */
static void draw_mask_to_buffer_8bit(const guint8 *gray8_src, int src_width, int src_height, int src_stride,
    guchar *data_buf, gsize area_pitch, int roi_width, int roi_height, guint8 color_idx, gboolean apply_dilation)
{
  int x, y;
  int draw_w = roi_width;
  int draw_h = roi_height;
  gboolean need_scale = (roi_width != src_width || roi_height != src_height) && roi_width > 0 && roi_height > 0;

  for (y = 0; y < draw_h; y++) {
    int sy = need_scale ? (roi_height > 0 ? (y * src_height / roi_height) : 0) : y;
    if (sy >= src_height)
      sy = src_height - 1;
    if (sy < 0)
      sy = 0;
    for (x = 0; x < draw_w; x++) {
      int sx = need_scale ? (roi_width > 0 ? (x * src_width / roi_width) : 0) : x;
      if (sx >= src_width)
        sx = src_width - 1;
      if (sx < 0)
        sx = 0;
      if (gray8_src[sy * src_stride + sx] != 0)
        data_buf[y * area_pitch + x] = color_idx;
    }
  }
  if (apply_dilation && draw_w >= 3 && draw_h >= 3) {
    guint8 *tmp = (guint8 *)g_malloc(area_pitch * (gsize)roi_height);
    if (tmp) {
      memcpy(tmp, data_buf, area_pitch * (gsize)roi_height);
      for (y = 1; y < draw_h - 1; y++) {
        for (x = 1; x < draw_w - 1; x++) {
          if (tmp[y * area_pitch + x] == color_idx) {
            data_buf[(y - 1) * area_pitch + (x - 1)] = color_idx;
            data_buf[(y - 1) * area_pitch + x] = color_idx;
            data_buf[(y - 1) * area_pitch + (x + 1)] = color_idx;
            data_buf[y * area_pitch + (x - 1)] = color_idx;
            data_buf[y * area_pitch + (x + 1)] = color_idx;
            data_buf[(y + 1) * area_pitch + (x - 1)] = color_idx;
            data_buf[(y + 1) * area_pitch + x] = color_idx;
            data_buf[(y + 1) * area_pitch + (x + 1)] = color_idx;
          }
        }
      }
      g_free(tmp);
    }
  }
}

/* Single area for ML_DECODED: seg masks + bbox in one block. Draw order: lane_line -> drive_area -> bbox. */
static GstFlowReturn draw_ml_decoded_single_area(GstAmbaDrawDataGen *self,
    GstBuffer *inbuf, GstBuffer *outbuf, GstAmbaMlDecodedMeta *ml_meta,
    amba_ml_bbox_result_t *ml_result, uint32_t det_num)
{
  draw_data_gen_priv_t *priv = self->priv;
  GstMemory *mem_area;
  GstMapInfo map_area;
  osd_area_block_header_t *block_hdr;
  amba_draw_clut_t *clut;
  guchar *data_buf;
  gsize area_pitch, block_size;
  int draw_format = priv->draw_format;
  amba_rect_t roi;
  guint ei;
  const char *ff = priv->font_file[0] ? priv->font_file : DRAWDATAGEN_DEFAULT_FONT_PATH;
  int fs = priv->font_size > 0 ? priv->font_size : 16;
  uint32_t osd_bbox_bg = 0;
  uint32_t osd_bbox_line = 0;

  roi.x = 0;
  roi.y = 0;
  roi.width = ROUND_DOWN((unsigned int)priv->map_width, OVERLAY_WIDTH_ALIGN);
  roi.height = ROUND_DOWN((unsigned int)priv->map_height, OVERLAY_HEIGHT_ALIGN);
  area_pitch = ROUND_UP(ROUND_UP(roi.width, OSD_BUF_WIDTH_ALIGN) * priv->draw_pix_size, OSD_BUF_PITCH_ALIGN);
  roi.pitch = (int)area_pitch;
  block_size = OSD_AREA_BLOCK_HEADER_SIZE + OVERLAY_CLUT_SIZE + (gsize)roi.height * area_pitch;

  mem_area = gst_allocator_alloc(NULL, block_size, NULL);
  if (!mem_area)
    return GST_FLOW_ERROR;
  gst_buffer_append_memory(outbuf, mem_area);
  if (!gst_memory_map(mem_area, &map_area, GST_MAP_WRITE))
    return GST_FLOW_ERROR;

  block_hdr = (osd_area_block_header_t *)map_area.data;
  memset(block_hdr, 0, OSD_AREA_BLOCK_HEADER_SIZE);
  block_hdr->magic = OSD_AREA_BLOCK_MAGIC;
  block_hdr->block_size = (unsigned int)block_size;
  block_hdr->enable = 1;
  block_hdr->draw_format = (unsigned char)draw_format;
  block_hdr->area_id = OSD_AREA_BLOCK_AREA_ID_USE_INDEX;
  block_hdr->rect = roi;

  clut = (amba_draw_clut_t *)(map_area.data + OSD_AREA_BLOCK_CLUT_OFFSET);
  data_buf = map_area.data + OSD_AREA_BLOCK_PIXEL_OFFSET;
  memset(clut, 0, OVERLAY_CLUT_SIZE);
  clut[AMBA_DRAW_CLUT_ENTRY_BACKGROUND].v = 128;
  clut[AMBA_DRAW_CLUT_ENTRY_BACKGROUND].u = 128;
  clut[AMBA_DRAW_CLUT_ENTRY_BACKGROUND].y = 235;
  clut[AMBA_DRAW_CLUT_ENTRY_BACKGROUND].a = 0;
  clut[DRAWDATAGEN_BBOX_CLUT_INDEX].y = DRAWDATAGEN_BBOX_CLUT_Y;
  clut[DRAWDATAGEN_BBOX_CLUT_INDEX].u = DRAWDATAGEN_BBOX_CLUT_U;
  clut[DRAWDATAGEN_BBOX_CLUT_INDEX].v = DRAWDATAGEN_BBOX_CLUT_V;
  clut[DRAWDATAGEN_BBOX_CLUT_INDEX].a = DRAWDATAGEN_BBOX_CLUT_A;
  clut[DRAWDATAGEN_SEG_LANE_LINE_CLUT_INDEX].y = DRAWDATAGEN_SEG_LANE_LINE_CLUT_Y;
  clut[DRAWDATAGEN_SEG_LANE_LINE_CLUT_INDEX].u = DRAWDATAGEN_SEG_LANE_LINE_CLUT_U;
  clut[DRAWDATAGEN_SEG_LANE_LINE_CLUT_INDEX].v = DRAWDATAGEN_SEG_LANE_LINE_CLUT_V;
  clut[DRAWDATAGEN_SEG_LANE_LINE_CLUT_INDEX].a = DRAWDATAGEN_SEG_LANE_LINE_CLUT_A;
  clut[DRAWDATAGEN_SEG_DRIVE_AREA_CLUT_INDEX].y = DRAWDATAGEN_SEG_DRIVE_AREA_CLUT_Y;
  clut[DRAWDATAGEN_SEG_DRIVE_AREA_CLUT_INDEX].u = DRAWDATAGEN_SEG_DRIVE_AREA_CLUT_U;
  clut[DRAWDATAGEN_SEG_DRIVE_AREA_CLUT_INDEX].v = DRAWDATAGEN_SEG_DRIVE_AREA_CLUT_V;
  clut[DRAWDATAGEN_SEG_DRIVE_AREA_CLUT_INDEX].a = DRAWDATAGEN_SEG_DRIVE_AREA_CLUT_A;

  drawdatagen_fill_background(data_buf, roi.height, area_pitch, draw_format);

  if (draw_format != AMBA_DRAW_FORMAT_8BIT_CLUT) {
    gst_memory_unmap(mem_area, &map_area);
    GST_ERROR_OBJECT(self, "draw_ml_decoded_single_area: only 8bit CLUT supported");
    return GST_FLOW_ERROR;
  }

  /* Draw seg masks: drive_area (ei1) first, then lane_line (ei2) on top so lane lines stay visible. */
  for (ei = 1; ei < ml_meta->n_entries; ei++) {
    if (ei >= gst_buffer_n_memory(inbuf)) {
      continue;
    }
    GstMemory *mem = gst_buffer_peek_memory(inbuf, ei);
    GstMapInfo mem_map;
    GstAmbaMlDecodedMetaEntry *ent = &ml_meta->entries[ei];
    if (!mem || !gst_memory_map(mem, &mem_map, GST_MAP_READ)) {
      continue;
    }
    if (ent->type != AMBA_ML_RESULT_TYPE_SEGMENTATION && ent->type != AMBA_ML_RESULT_TYPE_CUSTOM) {
      gst_memory_unmap(mem, &mem_map);
      continue;
    }
    {
      guint mw = ent->width ? ent->width : (guint)priv->map_width;
      guint mh = ent->height ? ent->height : (guint)priv->map_height;
      if (mw > 0 && mh > 0 && mem_map.size >= (gsize)mw * mh) {
        int seg_type = (ei == 1) ? 1 : 2;
        guint8 cidx = (guint8)(seg_type == 1 ? DRAWDATAGEN_SEG_DRIVE_AREA_CLUT_INDEX : DRAWDATAGEN_SEG_LANE_LINE_CLUT_INDEX);
        draw_mask_to_buffer_8bit((const guint8 *)mem_map.data, (int)mw, (int)mh, (int)mw,
            data_buf, area_pitch, (int)roi.width, (int)roi.height, cidx, (seg_type == 2));
      }
    }
    gst_memory_unmap(mem, &mem_map);
  }

  /* Set label background to transparent (default YUV values, a=0) to avoid "Use default backgroud color" debug print */
  priv->area_str.data.text.background_color.y = 235;
  priv->area_str.data.text.background_color.u = 128;
  priv->area_str.data.text.background_color.v = 128;
  priv->area_str.data.text.background_color.a = 0;

  /* YOLOP single-block path skips the main OSD loop (drew_ml_single_area); apply bbox OSD style here. */
  if (priv->osd_param.use_osd) {
    unsigned int aid;
    for (aid = 0; aid < priv->osd_param.area_num && aid < MAX_OVERLAY_AREA_NUM; aid++) {
      drawdatagen_osd_area_t *a = &priv->osd_param.area[aid];
      if (a->enable && (a->type == DRAWDATAGEN_AREA_TYPE_BBOX ||
          a->type == DRAWDATAGEN_AREA_TYPE_POSE)) {
        if (a->font_name[0]) {
          ff = a->font_name;
        }
        if (a->font_size > 0) {
          fs = a->font_size;
        }
        osd_bbox_bg = a->bg_color_hex;
        osd_bbox_line = a->bbox_line_color_hex;
        apply_string_colors_from_area(&priv->area_str.data.text, a, draw_format);
        break;
      }
    }
  }

  /* Draw pose / bbox / classification on top (append_mode=1). */
  {
    drawdatagen_osd_area_t osd_style;

    memset(&osd_style, 0, sizeof(osd_style));
    osd_style.bg_color_hex = osd_bbox_bg;
    osd_style.bbox_line_color_hex = osd_bbox_line;

    if (priv->pending_ml_classification) {
      if (drawdatagen_classification_draw(&roi, priv->pending_ml_classification, ff, fs,
          &priv->area_str, &priv->bitmap, clut, data_buf, (unsigned int)area_pitch,
          draw_format, 0, osd_bbox_bg, 1) < 0) {
        gst_memory_unmap(mem_area, &map_area);
        return GST_FLOW_ERROR;
      }
    } else if (drawdatagen_draw_ml_overlay(self, &roi, ml_result, det_num, &osd_style,
          clut, data_buf, (unsigned int)area_pitch, draw_format, 1,
          priv->pending_ml_pose ? DRAWDATAGEN_AREA_TYPE_POSE : DRAWDATAGEN_AREA_TYPE_BBOX) < 0) {
      gst_memory_unmap(mem_area, &map_area);
      return GST_FLOW_ERROR;
    }
  }
  /* init_text_info uses clut[5..13] for anti-aliasing; clut[0..4] reserved for text/bbox/seg. */

  {
    unsigned int slot_idx = (block_hdr->area_id != OSD_AREA_BLOCK_AREA_ID_USE_INDEX) ?
        (unsigned int)block_hdr->area_id : priv->out_area_slot;
    gsize clen = block_size - OSD_AREA_BLOCK_CLUT_OFFSET;
    drawdatagen_apply_block_change_flags(priv, block_hdr, &roi, slot_idx,
        map_area.data + OSD_AREA_BLOCK_CLUT_OFFSET, clen, outbuf);
    priv->out_area_slot++;
  }
  gst_memory_unmap(mem_area, &map_area);
  return GST_FLOW_OK;
}

/* Create one draw area from GRAY8 mask. GRAY8 0 -> transparent, non-zero -> foreground.
 * mask_override: when non-NULL, use its mask_color_* for foreground.
 * seg_type: 0=generic, 1=drive_area (cyan via magenta YUV for R/G-swap display), 2=lane_line (yellow) */
static GstFlowReturn draw_mask_area_from_gray8(GstAmbaDrawDataGen *self,
    const guint8 *gray8_src, int src_width, int src_height, int src_stride,
    amba_rect_t *roi, GstBuffer *outbuf, const drawdatagen_osd_area_t *mask_override,
    int seg_type)
{
  draw_data_gen_priv_t *priv = self->priv;
  GstMemory *mem_area;
  GstMapInfo map_area;
  osd_area_block_header_t *block_hdr;
  amba_draw_clut_t *clut;
  guchar *data_buf;
  gsize area_pitch, block_size;
  int draw_format = priv->draw_format;
  int x, y;
  amba_draw_clut_t fg_clut;

  roi->width = ROUND_DOWN(roi->width, OVERLAY_WIDTH_ALIGN);
  roi->height = ROUND_DOWN(roi->height, OVERLAY_HEIGHT_ALIGN);
  area_pitch = ROUND_UP(ROUND_UP(roi->width, OSD_BUF_WIDTH_ALIGN) * priv->draw_pix_size, OSD_BUF_PITCH_ALIGN);
  roi->pitch = (int)area_pitch;
  block_size = OSD_AREA_BLOCK_HEADER_SIZE + OVERLAY_CLUT_SIZE + (gsize)roi->height * area_pitch;

  mem_area = gst_allocator_alloc(NULL, block_size, NULL);
  if (!mem_area)
    return GST_FLOW_ERROR;
  gst_buffer_append_memory(outbuf, mem_area);
  if (!gst_memory_map(mem_area, &map_area, GST_MAP_WRITE)) {
    return GST_FLOW_ERROR;
  }
  block_hdr = (osd_area_block_header_t *)map_area.data;
  memset(block_hdr, 0, OSD_AREA_BLOCK_HEADER_SIZE);
  block_hdr->magic = OSD_AREA_BLOCK_MAGIC;
  block_hdr->block_size = (unsigned int)block_size;
  block_hdr->enable = 1;
  block_hdr->draw_format = (unsigned char)draw_format;
  block_hdr->area_id = OSD_AREA_BLOCK_AREA_ID_USE_INDEX;
  block_hdr->rect = *roi;

  clut = (amba_draw_clut_t *)(map_area.data + OSD_AREA_BLOCK_CLUT_OFFSET);
  data_buf = map_area.data + OSD_AREA_BLOCK_PIXEL_OFFSET;
  memset(clut, 0, OVERLAY_CLUT_SIZE);
  clut[AMBA_DRAW_CLUT_ENTRY_BACKGROUND].v = 128;
  clut[AMBA_DRAW_CLUT_ENTRY_BACKGROUND].u = 128;
  clut[AMBA_DRAW_CLUT_ENTRY_BACKGROUND].y = 235;
  clut[AMBA_DRAW_CLUT_ENTRY_BACKGROUND].a = 0;
  if (mask_override && 0) {
    fg_clut.y = mask_override->mask_color_y;
    fg_clut.u = mask_override->mask_color_u;
    fg_clut.v = mask_override->mask_color_v;
    fg_clut.a = mask_override->mask_color_a;
  } else if (seg_type == 1) {
    /* drive_area: cyan (unified with draw_ml_decoded_single_area) */
    fg_clut.y = DRAWDATAGEN_SEG_DRIVE_AREA_CLUT_Y;
    fg_clut.u = DRAWDATAGEN_SEG_DRIVE_AREA_CLUT_U;
    fg_clut.v = DRAWDATAGEN_SEG_DRIVE_AREA_CLUT_V;
    fg_clut.a = DRAWDATAGEN_SEG_DRIVE_AREA_CLUT_A;
  } else if (seg_type == 2) {
    /* lane_line: yellow (unified with draw_ml_decoded_single_area) */
    fg_clut.y = DRAWDATAGEN_SEG_LANE_LINE_CLUT_Y;
    fg_clut.u = DRAWDATAGEN_SEG_LANE_LINE_CLUT_U;
    fg_clut.v = DRAWDATAGEN_SEG_LANE_LINE_CLUT_V;
    fg_clut.a = DRAWDATAGEN_SEG_LANE_LINE_CLUT_A;
  } else {
    fg_clut.y = 0;
    fg_clut.u = 128;
    fg_clut.v = 0;
    fg_clut.a = 128;
  }
  clut[0] = fg_clut;

  if (draw_format == AMBA_DRAW_FORMAT_8BIT_CLUT) {
    for (y = 0; y < roi->height; y++) {
      int sy = roi->y + y;
      if (sy >= src_height)
        sy = src_height - 1;
      if (sy < 0)
        sy = 0;
      for (x = 0; x < roi->width; x++) {
        int sx = roi->x + x;
        if (sx >= src_width)
          sx = src_width - 1;
        if (sx < 0)
          sx = 0;
        guint8 v = gray8_src[sy * src_stride + sx];
        data_buf[y * area_pitch + x] = (v == 0) ? AMBA_DRAW_CLUT_ENTRY_BACKGROUND : 0;
      }
      for (x = roi->width; x < (int)area_pitch; x++) {
        data_buf[y * area_pitch + x] = AMBA_DRAW_CLUT_ENTRY_BACKGROUND;
      }
    }
    /* Lane line: dilate 3x3 to make thin lines visible (24-240 px in 2M is ~1px wide) */
    if (seg_type == 2 && roi->width >= 3 && roi->height >= 3) {
      guint8 *tmp = (guint8 *)g_malloc(area_pitch * (gsize)roi->height);
      if (tmp) {
        memcpy(tmp, data_buf, area_pitch * (gsize)roi->height);
        for (y = 1; y < roi->height - 1; y++) {
          for (x = 1; x < roi->width - 1; x++) {
            if (tmp[y * area_pitch + x] == 0) {
              data_buf[(y - 1) * area_pitch + (x - 1)] = 0;
              data_buf[(y - 1) * area_pitch + x] = 0;
              data_buf[(y - 1) * area_pitch + (x + 1)] = 0;
              data_buf[y * area_pitch + (x - 1)] = 0;
              data_buf[y * area_pitch + (x + 1)] = 0;
              data_buf[(y + 1) * area_pitch + (x - 1)] = 0;
              data_buf[(y + 1) * area_pitch + x] = 0;
              data_buf[(y + 1) * area_pitch + (x + 1)] = 0;
            }
          }
        }
        g_free(tmp);
      }
    }
  } else {
    uint32_t fg_pixel = drawdatagen_yuv_clut_to_pixel(&fg_clut, draw_format);
    drawdatagen_fill_background(data_buf, roi->height, area_pitch, draw_format);
    if (priv->draw_pix_size == 2) {
      for (y = 0; y < roi->height; y++) {
        int sy = roi->y + y;
        if (sy >= src_height)
          sy = src_height - 1;
        if (sy < 0)
          sy = 0;
        uint16_t *row = (uint16_t *)(data_buf + y * area_pitch);
        for (x = 0; x < roi->width; x++) {
          int sx = roi->x + x;
          if (sx >= src_width)
            sx = src_width - 1;
          if (sx < 0)
            sx = 0;
          if (gray8_src[sy * src_stride + sx] != 0)
            row[x] = (uint16_t)fg_pixel;
        }
      }
    } else if (priv->draw_pix_size == 4) {
      for (y = 0; y < roi->height; y++) {
        int sy = roi->y + y;
        if (sy >= src_height)
          sy = src_height - 1;
        if (sy < 0)
          sy = 0;
        uint32_t *row = (uint32_t *)(data_buf + y * area_pitch);
        for (x = 0; x < roi->width; x++) {
          int sx = roi->x + x;
          if (sx >= src_width)
            sx = src_width - 1;
          if (sx < 0)
            sx = 0;
          if (gray8_src[sy * src_stride + sx] != 0)
            row[x] = fg_pixel;
        }
      }
    }
  }

  {
    unsigned int slot_idx = (block_hdr->area_id != OSD_AREA_BLOCK_AREA_ID_USE_INDEX) ?
        (unsigned int)block_hdr->area_id : priv->out_area_slot;
    gsize clen = block_size - OSD_AREA_BLOCK_CLUT_OFFSET;
    drawdatagen_apply_block_change_flags(priv, block_hdr, roi, slot_idx,
        map_area.data + OSD_AREA_BLOCK_CLUT_OFFSET, clen, outbuf);
    priv->out_area_slot++;
  }
  gst_memory_unmap(mem_area, &map_area);
  return GST_FLOW_OK;
}

/* Create one draw area from GRAY8 depth map with nn_arm pseudo-color table.
 * GRAY8 0 -> transparent, 1..255 -> colormap index. */
static GstFlowReturn draw_depth_area_from_gray8(GstAmbaDrawDataGen *self,
    const guint8 *gray8_src, int src_width, int src_height, int src_stride,
    amba_rect_t *roi, GstBuffer *outbuf)
{
  draw_data_gen_priv_t *priv = self->priv;
  GstMemory *mem_area;
  GstMapInfo map_area;
  osd_area_block_header_t *block_hdr;
  amba_draw_clut_t *clut;
  guchar *data_buf;
  gsize area_pitch, block_size;
  int draw_format = priv->draw_format;
  uint32_t depth_pixel_lut[256];

  roi->width = ROUND_DOWN(roi->width, OVERLAY_WIDTH_ALIGN);
  roi->height = ROUND_DOWN(roi->height, OVERLAY_HEIGHT_ALIGN);
  area_pitch = ROUND_UP(ROUND_UP(roi->width, OSD_BUF_WIDTH_ALIGN) * priv->draw_pix_size, OSD_BUF_PITCH_ALIGN);
  roi->pitch = (int)area_pitch;
  block_size = OSD_AREA_BLOCK_HEADER_SIZE + OVERLAY_CLUT_SIZE + (gsize)roi->height * area_pitch;

  mem_area = gst_allocator_alloc(NULL, block_size, NULL);
  if (!mem_area)
    return GST_FLOW_ERROR;
  gst_buffer_append_memory(outbuf, mem_area);
  if (!gst_memory_map(mem_area, &map_area, GST_MAP_WRITE)) {
    return GST_FLOW_ERROR;
  }
  block_hdr = (osd_area_block_header_t *)map_area.data;
  memset(block_hdr, 0, OSD_AREA_BLOCK_HEADER_SIZE);
  block_hdr->magic = OSD_AREA_BLOCK_MAGIC;
  block_hdr->block_size = (unsigned int)block_size;
  block_hdr->enable = 1;
  block_hdr->draw_format = (unsigned char)draw_format;
  block_hdr->area_id = OSD_AREA_BLOCK_AREA_ID_USE_INDEX;
  block_hdr->rect = *roi;

  clut = (amba_draw_clut_t *)(map_area.data + OSD_AREA_BLOCK_CLUT_OFFSET);
  data_buf = map_area.data + OSD_AREA_BLOCK_PIXEL_OFFSET;
  drawdatagen_depth_init_clut(clut);

  if (draw_format == AMBA_DRAW_FORMAT_8BIT_CLUT) {
    drawdatagen_depth_gray8_to_clut(gray8_src, src_width, src_height, src_stride,
        data_buf, area_pitch, roi->x, roi->y, roi->width, roi->height);
  } else {
    drawdatagen_depth_build_pixel_lut(depth_pixel_lut, draw_format, clut);
    drawdatagen_depth_gray8_to_pixels(gray8_src, src_width, src_height, src_stride,
        data_buf, area_pitch, roi->x, roi->y, roi->width, roi->height,
        draw_format, priv->draw_pix_size, depth_pixel_lut);
  }

  {
    unsigned int slot_idx = (block_hdr->area_id != OSD_AREA_BLOCK_AREA_ID_USE_INDEX) ?
        (unsigned int)block_hdr->area_id : priv->out_area_slot;
    gsize clen = block_size - OSD_AREA_BLOCK_CLUT_OFFSET;
    drawdatagen_apply_block_change_flags(priv, block_hdr, roi, slot_idx,
        map_area.data + OSD_AREA_BLOCK_CLUT_OFFSET, clen, outbuf);
    priv->out_area_slot++;
  }
  gst_memory_unmap(mem_area, &map_area);
  return GST_FLOW_OK;
}

static GstFlowReturn gst_amba_draw_data_gen_transform(GstBaseTransform *trans,
    GstBuffer *inbuf, GstBuffer *outbuf)
{
  GstAmbaDrawDataGen *self = GST_AMBA_DRAW_DATA_GEN(trans);
  draw_data_gen_priv_t *priv = self->priv;
  GstMapInfo inmap;
  amba_ml_bbox_result_t *ml_result = NULL;
  uint32_t det_num = 0;
  GstAmbaMlDecodedMeta *ml_meta = NULL;  /* for multi-memory: draw masks after bbox */
  GstMemory *mem0_hold = NULL;
  GstMapInfo mem0_map_hold;
  gboolean need_unmap_inmap = FALSE;  /* legacy ML_DECODED uses inmap */
  GstBuffer *draw_target = outbuf;
  GstBuffer *temp_buf = NULL;

  gst_buffer_remove_all_memory(outbuf);
  priv->out_area_slot = 0;
  priv->pending_ml_classification = NULL;
  priv->pending_ml_pose = NULL;
  priv->pending_ml_embedding = NULL;
  if (priv->output_mode == OUTPUT_MODE_VIDEO) {
    temp_buf = gst_buffer_new();
    draw_target = temp_buf;
  }

  if (priv->input_media_type == INPUT_TYPE_ML_DECODED) {
    /* Multi-memory: use GstAmbaMlDecodedMeta. Memory 0=bbox, 1..n=SEGMENTATION masks.
     * Keep mem0 mapped for bbox draw; draw masks after bbox (output order). */
    ml_meta = gst_buffer_get_amba_ml_decoded_meta(inbuf);
    if (ml_meta && ml_meta->n_entries > 0 && gst_buffer_n_memory(inbuf) > 0) {
      GstMemory *mem = gst_buffer_peek_memory(inbuf, 0);
      if (mem && gst_memory_map(mem, &mem0_map_hold, GST_MAP_READ)) {
        mem0_hold = mem;
        if (ml_meta->entries[0].type == AMBA_ML_RESULT_TYPE_CLASSIFICATION &&
            mem0_map_hold.size >= sizeof(amba_ml_classification_result_t)) {
          amba_ml_classification_result_t *c = (amba_ml_classification_result_t *)mem0_map_hold.data;
          if (AMBA_ML_RESULT_IS_VALID(c->header) && c->header.type == AMBA_ML_RESULT_TYPE_CLASSIFICATION) {
            priv->pending_ml_classification = &c->body;
            if (c->body.top_k_out > 0) {
              GST_INFO_OBJECT(self, "classification top-1: id=%d score=%.4f label=\"%.64s\" (num_classes=%u top_k=%u)",
                  (int)c->body.ranked[0].class_id, (double)c->body.ranked[0].score,
                  c->body.ranked[0].label,
                  (unsigned)c->body.num_classes, (unsigned)c->body.top_k_out);
            } else {
              GST_INFO_OBJECT(self, "classification empty (num_classes=%u)",
                  (unsigned)c->body.num_classes);
            }
          }
        } else if (ml_meta->entries[0].type == AMBA_ML_RESULT_TYPE_POSE &&
            mem0_map_hold.size >= sizeof(amba_ml_pose_result_t)) {
          amba_ml_pose_result_t *p = (amba_ml_pose_result_t *)mem0_map_hold.data;
          if (AMBA_ML_RESULT_IS_VALID(p->header) && p->header.type == AMBA_ML_RESULT_TYPE_POSE) {
            priv->pending_ml_pose = &p->body;
            GST_INFO_OBJECT(self, "pose: kp0 score=%.3f @ (%d,%d)",
                (double)p->body.keypoints[0].score,
                (int)p->body.keypoints[0].x, (int)p->body.keypoints[0].y);
          }
        } else if (ml_meta->entries[0].type == AMBA_ML_RESULT_TYPE_EMBEDDING &&
            mem0_map_hold.size >= sizeof(amba_ml_embedding_result_t)) {
          amba_ml_embedding_result_t *e = (amba_ml_embedding_result_t *)mem0_map_hold.data;
          if (AMBA_ML_RESULT_IS_VALID(e->header) && e->header.type == AMBA_ML_RESULT_TYPE_EMBEDDING) {
            priv->pending_ml_embedding = &e->body;
            if (e->body.match_valid) {
              GST_INFO_OBJECT(self, "clip_score: \"%.64s\" score=%.4f dim=%u",
                  e->body.match_label, (double)e->body.match_score,
                  (unsigned)e->body.dim);
            } else {
              GST_INFO_OBJECT(self, "clip embedding dim=%u (no ref match)",
                  (unsigned)e->body.dim);
            }
          }
        } else if (ml_meta->entries[0].type == AMBA_ML_RESULT_TYPE_DETECTION_BBOX &&
            mem0_map_hold.size >= sizeof(amba_ml_result_header_t)) {
          amba_ml_result_header_t *h = (amba_ml_result_header_t *)mem0_map_hold.data;
          if (AMBA_ML_RESULT_IS_VALID(*h) && h->type == AMBA_ML_RESULT_TYPE_DETECTION_BBOX) {
            ml_result = (amba_ml_bbox_result_t *)mem0_map_hold.data;
            det_num = ml_result->detections.det_num;
            if (det_num > AMBA_ML_DETECTION_MAX_NUM) {
              det_num = AMBA_ML_DETECTION_MAX_NUM;
            }
            if (det_num > 0) {
              const amba_ml_detection_t *d0 =
                  &ml_result->detections.detections[0];

              if ((d0->flags & AMBA_ML_DETECTION_FLAG_HAS_LANDMARKS) != 0) {
                GST_INFO_OBJECT(self,
                    "bbox det_num=%u det0 landmarks (coord_res): "
                    "(%d,%d) (%d,%d) (%d,%d) (%d,%d) (%d,%d)",
                    det_num,
                    (int)d0->landmark_x[0], (int)d0->landmark_y[0],
                    (int)d0->landmark_x[1], (int)d0->landmark_y[1],
                    (int)d0->landmark_x[2], (int)d0->landmark_y[2],
                    (int)d0->landmark_x[3], (int)d0->landmark_y[3],
                    (int)d0->landmark_x[4], (int)d0->landmark_y[4]);
              } else {
                GST_INFO_OBJECT(self, "bbox det_num=%u", det_num);
              }
            }
          }
        }
      }
    } else {
      /* Legacy: single memory blob (bbox or classification) */
      if (gst_buffer_map(inbuf, &inmap, GST_MAP_READ)) {
        need_unmap_inmap = TRUE;
        if (inmap.size >= sizeof(amba_ml_result_header_t)) {
          amba_ml_result_header_t *h = (amba_ml_result_header_t *)inmap.data;
          if (AMBA_ML_RESULT_IS_VALID(*h) && h->type == AMBA_ML_RESULT_TYPE_CLASSIFICATION &&
              inmap.size >= sizeof(amba_ml_classification_result_t)) {
            amba_ml_classification_result_t *c = (amba_ml_classification_result_t *)inmap.data;
            if (c->header.type == AMBA_ML_RESULT_TYPE_CLASSIFICATION) {
              priv->pending_ml_classification = &c->body;
              if (c->body.top_k_out > 0) {
                GST_INFO_OBJECT(self, "classification top-1: id=%d score=%.4f (num_classes=%u top_k=%u)",
                    (int)c->body.ranked[0].class_id, (double)c->body.ranked[0].score,
                    (unsigned)c->body.num_classes, (unsigned)c->body.top_k_out);
              }
            }
          } else if (AMBA_ML_RESULT_IS_VALID(*h) && h->type == AMBA_ML_RESULT_TYPE_POSE &&
              inmap.size >= sizeof(amba_ml_pose_result_t)) {
            amba_ml_pose_result_t *p = (amba_ml_pose_result_t *)inmap.data;
            if (p->header.type == AMBA_ML_RESULT_TYPE_POSE) {
              priv->pending_ml_pose = &p->body;
              GST_INFO_OBJECT(self, "pose: kp0 score=%.3f @ (%d,%d)",
                  (double)p->body.keypoints[0].score,
                  (int)p->body.keypoints[0].x, (int)p->body.keypoints[0].y);
            }
          } else if (AMBA_ML_RESULT_IS_VALID(*h) && h->type == AMBA_ML_RESULT_TYPE_EMBEDDING &&
              inmap.size >= sizeof(amba_ml_embedding_result_t)) {
            amba_ml_embedding_result_t *e = (amba_ml_embedding_result_t *)inmap.data;
            if (e->header.type == AMBA_ML_RESULT_TYPE_EMBEDDING) {
              priv->pending_ml_embedding = &e->body;
              if (e->body.match_valid) {
                GST_INFO_OBJECT(self, "clip_score: \"%.64s\" score=%.4f dim=%u",
                    e->body.match_label, (double)e->body.match_score,
                    (unsigned)e->body.dim);
              }
            }
          } else if (AMBA_ML_RESULT_IS_VALID(*h) && h->type == AMBA_ML_RESULT_TYPE_DETECTION_BBOX) {
            ml_result = (amba_ml_bbox_result_t *)inmap.data;
            det_num = ml_result->detections.det_num;
            if (det_num > AMBA_ML_DETECTION_MAX_NUM) {
              det_num = AMBA_ML_DETECTION_MAX_NUM;
            }
          }
        }
      }
    }
  } else if (priv->input_media_type == INPUT_TYPE_OSD_TRIGGER) {
    if (!priv->osd_param.use_osd) {
      GST_ERROR_OBJECT(self, "application/x-amba-drawdatagen-trigger requires osd property");
      if (temp_buf) {
        gst_buffer_unref(temp_buf);
      }
      return GST_FLOW_ERROR;
    }
  } else if (!gst_buffer_map(inbuf, &inmap, GST_MAP_READ)) {
    GST_ERROR_OBJECT(self, "Failed to map input buffer");
    return GST_FLOW_ERROR;
  }

  if (priv->input_media_type == INPUT_TYPE_TEXT) {
    size_t len = inmap.size;
    if (len >= sizeof(priv->str_from_buffer)) {
     len = sizeof(priv->str_from_buffer) - 1;
    }
    if (len > 0) {
      memcpy(priv->str_from_buffer, inmap.data, len);
      priv->str_from_buffer[len] = '\0';
    } else {
      priv->str_from_buffer[0] = '\0';
    }
  }
  /* Unmap inmap for TEXT (copied to str_from_buffer); IMAGE_BMP keeps mapped for direct draw */
  if (priv->input_media_type == INPUT_TYPE_TEXT) {
    gst_buffer_unmap(inbuf, &inmap);
  }

  /* ML_DECODED multi-memory: draw all (seg + bbox) in one area. Draw order: lane_line -> drive_area -> bbox. */
  gboolean drew_ml_single_area = FALSE;
  if (priv->input_media_type == INPUT_TYPE_ML_DECODED && ml_meta && ml_meta->n_entries > 1) {
    if (draw_ml_decoded_single_area(self, inbuf, draw_target, ml_meta, ml_result, det_num) != GST_FLOW_OK) {
      if (mem0_hold) {
        gst_memory_unmap(mem0_hold, &mem0_map_hold);
        mem0_hold = NULL;
      }
      if (temp_buf) {
        gst_buffer_unref(temp_buf);
      }
      return GST_FLOW_ERROR;
    }
    drew_ml_single_area = TRUE;
  }
  if (priv->input_media_type == INPUT_TYPE_VIDEO_GRAY8) {
    /* GRAY8: depth pseudo-color or solid mask. Use osd depth/mask area if configured. */
    amba_rect_t roi = priv->gray8_roi;
    const drawdatagen_osd_area_t *mask_area = NULL;
    gboolean use_depth = FALSE;
    unsigned int aid;
    if (priv->osd_param.use_osd) {
      for (aid = 0; aid < priv->osd_param.area_num && aid < MAX_OVERLAY_AREA_NUM; aid++) {
        drawdatagen_osd_area_t *a = &priv->osd_param.area[aid];
        if (a->enable && a->type == DRAWDATAGEN_AREA_TYPE_DEPTH) {
          roi = a->roi;
          use_depth = TRUE;
          break;
        }
      }
      if (!use_depth) {
        for (aid = 0; aid < priv->osd_param.area_num && aid < MAX_OVERLAY_AREA_NUM; aid++) {
          drawdatagen_osd_area_t *a = &priv->osd_param.area[aid];
          if (a->enable && a->type == DRAWDATAGEN_AREA_TYPE_MASK) {
            roi = a->roi;
            mask_area = a;
            break;
          }
        }
      }
    }
    if (!roi.width) {
      roi.width = priv->map_width;
    }
    if (!roi.height) {
      roi.height = priv->map_height;
    }
    {
      int src_stride = priv->map_width;
      GstVideoMeta *vmeta = gst_buffer_get_video_meta(inbuf);
      if (vmeta && vmeta->n_planes > 0) {
        src_stride = vmeta->stride[0];
      }
      if (inmap.size < (gsize)src_stride * priv->map_height) {
        GST_WARNING_OBJECT(self, "GRAY8 buffer size %zu < expected %d", inmap.size, src_stride * priv->map_height);
        gst_buffer_unmap(inbuf, &inmap);
        if (temp_buf) {
          gst_buffer_unref(temp_buf);
        }
        return GST_FLOW_OK;  /* Skip frame, don't error */
      }
      if (use_depth) {
        if (draw_depth_area_from_gray8(self, inmap.data, priv->map_width, priv->map_height,
            src_stride, &roi, draw_target) != GST_FLOW_OK) {
          gst_buffer_unmap(inbuf, &inmap);
          if (temp_buf) {
            gst_buffer_unref(temp_buf);
          }
          return GST_FLOW_ERROR;
        }
      } else if (draw_mask_area_from_gray8(self, inmap.data, priv->map_width, priv->map_height,
          src_stride, &roi, draw_target, mask_area, 0) != GST_FLOW_OK) {
        gst_buffer_unmap(inbuf, &inmap);
        if (temp_buf) {
          gst_buffer_unref(temp_buf);
        }
        return GST_FLOW_ERROR;
      }
    }
    gst_buffer_unmap(inbuf, &inmap);
  } else if (priv->osd_param.use_osd) {
    unsigned int aid;
    for (aid = 0; aid < priv->osd_param.area_num && aid < MAX_OVERLAY_AREA_NUM; aid++) {
      drawdatagen_osd_area_t *a = &priv->osd_param.area[aid];
      if (!a->enable) {
        continue;
      }
      if (a->type == DRAWDATAGEN_AREA_TYPE_BBOX && drew_ml_single_area) {
        continue;  /* bbox/pose already in single area */
      }
      if (a->type == DRAWDATAGEN_AREA_TYPE_POSE && drew_ml_single_area) {
        continue;
      }
      amba_rect_t roi = a->roi;
      if (!roi.width) {
        roi.width = (a->type == DRAWDATAGEN_AREA_TYPE_BBOX || a->type == DRAWDATAGEN_AREA_TYPE_POSE) ?
            priv->map_width : 256;
      }
      if (!roi.height) {
        roi.height = (a->type == DRAWDATAGEN_AREA_TYPE_BBOX || a->type == DRAWDATAGEN_AREA_TYPE_POSE) ?
            priv->map_height : priv->font_size + 8;
      }
      if ((a->type == DRAWDATAGEN_AREA_TYPE_BBOX || a->type == DRAWDATAGEN_AREA_TYPE_POSE) &&
          priv->input_media_type != INPUT_TYPE_ML_DECODED) {
        continue;  /* skip bbox/pose when no ML */
      }
      if (a->type == DRAWDATAGEN_AREA_TYPE_POSE && !priv->pending_ml_pose) {
        continue;
      }
      if (a->type == DRAWDATAGEN_AREA_TYPE_CLIP_SCORE &&
          (priv->input_media_type != INPUT_TYPE_ML_DECODED || !priv->pending_ml_embedding)) {
        continue;
      }
      if (a->type == DRAWDATAGEN_AREA_TYPE_PICTURE && priv->input_media_type != INPUT_TYPE_IMAGE_BMP && !a->bmp_file[0]) {
        continue;  /* skip picture when no bmp */
      }
      if (a->type == DRAWDATAGEN_AREA_TYPE_STRING && priv->input_media_type != INPUT_TYPE_TEXT && !a->str[0]) {
        continue;
      }
      {
        const char *bmp_ov = (a->type == DRAWDATAGEN_AREA_TYPE_PICTURE && priv->input_media_type != INPUT_TYPE_IMAGE_BMP) ?
            (a->bmp_file[0] ? a->bmp_file : priv->bmp_file) : NULL;
        const char *str_ov = (a->type == DRAWDATAGEN_AREA_TYPE_STRING && priv->input_media_type == INPUT_TYPE_TEXT) ? priv->str_from_buffer : NULL;
        const unsigned char *bmp_data = (a->type == DRAWDATAGEN_AREA_TYPE_PICTURE && priv->input_media_type == INPUT_TYPE_IMAGE_BMP) ? (const unsigned char *)inmap.data : NULL;
        size_t bmp_size = (a->type == DRAWDATAGEN_AREA_TYPE_PICTURE && priv->input_media_type == INPUT_TYPE_IMAGE_BMP) ? inmap.size : 0;
        if (drawdatagen_area_should_use_cache(priv, a, aid, str_ov, bmp_data, bmp_size)) {
          GstMapInfo cmap;
          unsigned int area_slot = (unsigned int)aid;
          gst_buffer_append_memory(draw_target, gst_memory_ref(priv->area_cache[aid]));
          if (gst_memory_map(priv->area_cache[aid], &cmap, GST_MAP_READ) &&
              cmap.size >= OSD_AREA_BLOCK_HEADER_SIZE) {
            osd_area_block_header_t *hh = (osd_area_block_header_t *)cmap.data;
            area_slot = osd_area_block_resolve_slot(hh, gst_buffer_n_memory(draw_target) - 1);
            gst_memory_unmap(priv->area_cache[aid], &cmap);
          }
          gst_amba_draw_data_area_flags_meta_set(draw_target, area_slot, 0);
          priv->out_area_slot++;
        } else {
          if (draw_one_area_per_area(self, a->type, &roi, ml_result, det_num, draw_target, bmp_ov, str_ov, a, bmp_data, bmp_size,
                  (unsigned char)aid) != GST_FLOW_OK) {
            if (priv->input_media_type == INPUT_TYPE_IMAGE_BMP) {
              gst_buffer_unmap(inbuf, &inmap);
            }
            if (temp_buf) {
              gst_buffer_unref(temp_buf);
            }
            return GST_FLOW_ERROR;
          }
          if (drawdatagen_area_cache_eligible(priv, a) &&
              (a->static_content || a->update_interval > 1)) {
            GstMemory *m = gst_buffer_peek_memory(draw_target, gst_buffer_n_memory(draw_target) - 1);
            if (priv->area_cache[aid]) {
              gst_memory_unref(priv->area_cache[aid]);
            }
            priv->area_cache[aid] = gst_memory_ref(m);
            if (a->static_content) {
              priv->last_static_source_hash[aid] =
                  drawdatagen_compute_static_source_hash(priv, a, str_ov, bmp_data, bmp_size);
              priv->last_static_source_ready[aid] = 1;
            }
          }
        }
      }
    }
  } else {
    if (priv->bbox_area_enable && priv->input_media_type == INPUT_TYPE_ML_DECODED && !drew_ml_single_area) {
      amba_rect_t roi = priv->bbox_roi;
      if (!roi.width) {
        roi.width = priv->map_width;
      }
      if (!roi.height) {
        roi.height = priv->map_height;
      }
      if (priv->pending_ml_pose) {
        GST_INFO_OBJECT(self, "pose draw: roi=%dx%d map=%dx%d kp0 score=%.3f",
            roi.width, roi.height, priv->map_width, priv->map_height,
            (double)priv->pending_ml_pose->keypoints[0].score);
      } else if (det_num > 0 && ml_result) {
        const amba_ml_detection_t *d0 = &ml_result->detections.detections[0];
        GST_INFO_OBJECT(self, "bbox draw: roi=%dx%d map=%dx%d det0: x=%d..%d y=%d..%d score=%.2f",
            roi.width, roi.height, priv->map_width, priv->map_height,
            (int)d0->x_start, (int)d0->x_end, (int)d0->y_start, (int)d0->y_end, (double)d0->score);
      }
      if (draw_one_area_per_area(self,
              priv->pending_ml_pose ? DRAWDATAGEN_AREA_TYPE_POSE : DRAWDATAGEN_AREA_TYPE_BBOX,
              &roi, ml_result, det_num, draw_target, NULL, NULL, NULL, NULL, 0,
              OSD_AREA_BLOCK_AREA_ID_USE_INDEX) != GST_FLOW_OK) {
        if (priv->input_media_type == INPUT_TYPE_IMAGE_BMP) {
          gst_buffer_unmap(inbuf, &inmap);
        }
        if (temp_buf) {
          gst_buffer_unref(temp_buf);
        }
        return GST_FLOW_ERROR;
      }
    }
    if ((priv->bmp_area_enable && priv->bmp_file[0]) || priv->input_media_type == INPUT_TYPE_IMAGE_BMP) {
      amba_rect_t roi = priv->bmp_roi;
      if (!roi.width) {
        roi.width = 128;
      }
      if (!roi.height) {
        roi.height = 128;
      }
      const char *bmp_path = (priv->input_media_type != INPUT_TYPE_IMAGE_BMP) ? priv->bmp_file : NULL;
      const unsigned char *bmp_data = (priv->input_media_type == INPUT_TYPE_IMAGE_BMP) ? (const unsigned char *)inmap.data : NULL;
      size_t bmp_size = (priv->input_media_type == INPUT_TYPE_IMAGE_BMP) ? inmap.size : 0;
      if (draw_one_area_per_area(self, DRAWDATAGEN_AREA_TYPE_PICTURE, &roi, NULL, 0, draw_target, bmp_path, NULL, NULL, bmp_data, bmp_size,
              OSD_AREA_BLOCK_AREA_ID_USE_INDEX) != GST_FLOW_OK) {
        if (priv->input_media_type == INPUT_TYPE_IMAGE_BMP) {
          gst_buffer_unmap(inbuf, &inmap);
        }
        if (temp_buf) {
          gst_buffer_unref(temp_buf);
        }
        return GST_FLOW_ERROR;
      }
    }
    if ((priv->str_area_enable && priv->str_text[0]) || priv->input_media_type == INPUT_TYPE_TEXT) {
      amba_rect_t roi = priv->str_roi;
      if (!roi.width) {
        roi.width = 256;
      }
      if (!roi.height) {
        roi.height = priv->font_size + 8;
      }
      if (draw_one_area_per_area(self, DRAWDATAGEN_AREA_TYPE_STRING, &roi, NULL, 0, draw_target, NULL,
          (priv->input_media_type == INPUT_TYPE_TEXT) ? priv->str_from_buffer : NULL, NULL, NULL, 0,
              OSD_AREA_BLOCK_AREA_ID_USE_INDEX) != GST_FLOW_OK) {
        if (priv->input_media_type == INPUT_TYPE_IMAGE_BMP) {
          gst_buffer_unmap(inbuf, &inmap);
        }
        if (temp_buf) {
          gst_buffer_unref(temp_buf);
        }
        return GST_FLOW_ERROR;
      }
    }
    if (priv->time_area_enable) {
      amba_rect_t roi = priv->time_roi;
      if (!roi.width) {
        roi.width = 256;
      }
      if (!roi.height) {
        roi.height = priv->font_size + 8;
      }
      if (draw_one_area_per_area(self, DRAWDATAGEN_AREA_TYPE_TIME, &roi, NULL, 0, draw_target, NULL, NULL, NULL, NULL, 0,
              OSD_AREA_BLOCK_AREA_ID_USE_INDEX) != GST_FLOW_OK) {
        if (priv->input_media_type == INPUT_TYPE_IMAGE_BMP) {
          gst_buffer_unmap(inbuf, &inmap);
        }
        if (temp_buf) {
          gst_buffer_unref(temp_buf);
        }
        return GST_FLOW_ERROR;
      }
    }
  }
  if (priv->input_media_type == INPUT_TYPE_IMAGE_BMP) {
    gst_buffer_unmap(inbuf, &inmap);
  }
  priv->pending_ml_classification = NULL;
  priv->pending_ml_pose = NULL;
  priv->pending_ml_embedding = NULL;
  if (mem0_hold) {
    gst_memory_unmap(mem0_hold, &mem0_map_hold);
    mem0_hold = NULL;
  }
  if (need_unmap_inmap) {
    gst_buffer_unmap(inbuf, &inmap);
  }

  if (priv->output_mode == OUTPUT_MODE_VIDEO && temp_buf) {
    if (drawdatagen_emit_video_from_draw_blocks(self, temp_buf, outbuf, det_num) != GST_FLOW_OK) {
      gst_buffer_unref(temp_buf);
      return GST_FLOW_ERROR;
    }
    gst_buffer_unref(temp_buf);
  }
  GST_BUFFER_PTS(outbuf) = GST_BUFFER_PTS(inbuf);
  GST_BUFFER_DTS(outbuf) = GST_BUFFER_DTS(inbuf);

  priv->frame_counter++;

  /* prepare_output_buffer may drop GstBaseTransform default meta copy on outbuf. */
  if (!amba_buffer_get_private_data_meta(outbuf) &&
      amba_buffer_get_private_data_meta(inbuf)) {
    amba_buffer_copy_private_data_meta(outbuf, inbuf);
  }

  GST_DEBUG_OBJECT(self, "transform: pushing outbuf pts=%" GST_TIME_FORMAT " det_num=%u",
      GST_TIME_ARGS(GST_BUFFER_PTS(outbuf)), (unsigned)det_num);
  return GST_FLOW_OK;
}
