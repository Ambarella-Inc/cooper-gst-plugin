/*
 * gstambadrawdatagen.h
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
 * Converts mlpostprocess output to draw data or video (ARGB).
 * Input: application/x-amba-ml-decoded, image/bmp, text/x-raw, video/x-raw GRAY8,
 * application/x-amba-drawdatagen-trigger (ignore payload; osd-only).
 * Output: application/x-amba-draw-data or video/x-raw ARGB.
 * Downstream: amba_overlay_draw or compositor.
 */

#ifndef __GST_AMBA_DRAW_DATA_GEN_H__
#define __GST_AMBA_DRAW_DATA_GEN_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>

#include "internal.h"
#include "element_common.h"
#include "overlay_common.h"
#include "amba_ml_decoded_result.h"

G_BEGIN_DECLS

#define GST_TYPE_AMBA_DRAW_DATA_GEN (gst_amba_draw_data_gen_get_type())
#define GST_AMBA_DRAW_DATA_GEN(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_AMBA_DRAW_DATA_GEN, GstAmbaDrawDataGen))
#define GST_AMBA_DRAW_DATA_GEN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_AMBA_DRAW_DATA_GEN, GstAmbaDrawDataGenClass))
#define GST_IS_AMBA_DRAW_DATA_GEN(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_AMBA_DRAW_DATA_GEN))
#define GST_IS_AMBA_DRAW_DATA_GEN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_AMBA_DRAW_DATA_GEN))

typedef struct _GstAmbaDrawDataGen GstAmbaDrawDataGen;
typedef struct _GstAmbaDrawDataGenClass GstAmbaDrawDataGenClass;

/* Per-area config from osd string.
 * New format: area_id:0;enable:1;roi:...;type:bbox;bbox:{line_color:...,font_color:...,bg_color:...}; ...
 * Mask: type:mask;mask:{y:0,u:128,v:0,a:128} - YUV+alpha for foreground color
 * Depth: type:depth - pseudo-color from GRAY8 (DepthAnything / mlpostprocess segmentation)
 * CLIP: type:clip_score - match label + score from embedding result */
typedef enum {
  DRAWDATAGEN_AREA_TYPE_BBOX = 0,
  DRAWDATAGEN_AREA_TYPE_PICTURE = 1,
  DRAWDATAGEN_AREA_TYPE_STRING = 2,
  DRAWDATAGEN_AREA_TYPE_TIME = 3,
  DRAWDATAGEN_AREA_TYPE_MASK = 4,
  DRAWDATAGEN_AREA_TYPE_POSE = 5,
  DRAWDATAGEN_AREA_TYPE_DEPTH = 6,
  DRAWDATAGEN_AREA_TYPE_CLIP_SCORE = 7,
} drawdatagen_area_type_t;

typedef struct {
  unsigned char enable;
  unsigned char type;  /* drawdatagen_area_type_t */
  amba_rect_t roi;
  char font_name[DMAX_FILE_NAME_LENGTH];
  int font_size;
  char bmp_file[DMAX_FILE_NAME_LENGTH];
  char str[AMBA_DRAW_STRING_MAX_NUM];
  char time_pre_str[AMBA_DRAW_STRING_MAX_NUM];
  char time_suf_str[AMBA_DRAW_STRING_MAX_NUM];
  int time_en_msec;
  int time_format;
  int time_is_12h;
  /* type=mask: foreground color (YUV+alpha), default y=0,u=128,v=0,a=128 */
  unsigned char mask_color_y;
  unsigned char mask_color_u;
  unsigned char mask_color_v;
  unsigned char mask_color_a;
  /* type=string/time: font color hex (0=use default). YUV: v/u/y/a; RGB565: b/g/r/a (MSB..LSB). */
  guint32 font_color_hex;
  /* Area background (bbox/picture/string/time); 0=transparent default. Legacy key: text_bg_color. Same hex layout as font_color. */
  guint32 bg_color_hex;
  /* bbox: box outline; 0=default (same BT.601 red as DRAWDATAGEN_BBOX_CLUT_*). Key: line_color. Same hex layout as font_color. */
  guint32 bbox_line_color_hex;
  /* string/time outline color; same hex layout as font_color. */
  guint32 ol_color_hex;
  int font_outline_w;
  int font_ver_bold;
  int font_hor_bold;
  /* static:1=cache rasterized output when source unchanged (saves CPU); source change forces redraw. */
  unsigned char static_content;
  /*update_interval:N=redraw bbox at most every N frames when type=bbox and input is ML decoded (1=every frame). Also used for bmp/string/time cache and time throttle. */
  unsigned int update_interval;
} drawdatagen_osd_area_t;

typedef struct {
  unsigned char use_osd;
  unsigned char area_num;
  drawdatagen_osd_area_t area[MAX_OVERLAY_AREA_NUM];
} drawdatagen_osd_param_t;

#define DRAWDATAGEN_OSD_STR_MAX 2048

typedef struct {
  int draw_format;
  guint draw_pix_size;
  int map_width;
  int map_height;

  /* OSD string config (when osd property set) */
  drawdatagen_osd_param_t osd_param;
  char osd_str[DRAWDATAGEN_OSD_STR_MAX];

  /* Legacy: Area 0 bbox */
  int bbox_area_enable;
  amba_rect_t bbox_roi;

  /* Legacy: Area 1 BMP */
  int bmp_area_enable;
  char bmp_file[DMAX_FILE_NAME_LENGTH];
  amba_rect_t bmp_roi;

  /* Legacy: Area 2 string */
  int str_area_enable;
  char str_text[256];
  char font_file[DMAX_FILE_NAME_LENGTH];
  int font_size;
  amba_rect_t str_roi;

  /* Legacy: Area 3 time */
  int time_area_enable;
  amba_rect_t time_roi;
  char time_pre_str[AMBA_DRAW_STRING_MAX_NUM];
  char time_suf_str[AMBA_DRAW_STRING_MAX_NUM];
  int time_en_msec;
  int time_format;
  int time_is_12h;

  bitmap_buffer_t bitmap;
  amba_overlay_area_param_t area_str;
  amba_overlay_area_param_t area_time;

  /* Input media type: 0=ml-decoded, 1=image/bmp, 2=text, 3=video/x-raw GRAY8 */
  int input_media_type;
  /* When input is video/x-raw GRAY8: ROI for mask area (default full frame) */
  amba_rect_t gray8_roi;
  /* When input is image/bmp: temp file path for buffer content */
  char bmp_from_buffer_path[DMAX_FILE_NAME_LENGTH];
  /* When input is text: string from buffer (null-terminated) */
  char str_from_buffer[AMBA_DRAW_STRING_MAX_NUM];

  /* Classification: valid only while ML-decoded input buffer is mapped in transform */
  const amba_ml_classification_body_t *pending_ml_classification;

  /* RTMPose: valid only while ML-decoded input buffer is mapped in transform */
  const amba_ml_pose_body_t *pending_ml_pose;

  /* CLIP embedding: valid only while ML-decoded input buffer is mapped in transform */
  const amba_ml_embedding_body_t *pending_ml_embedding;

  /* Output mode: 0=draw-data (default), 1=video (ARGB for compositor) */
  int output_mode;

  /* draw_format string for property (e.g. "8bit", "rgb565", "ayuv8888") */
  char draw_format_str[32];

  /* Per-area cached GstMemory for static_content / update_interval (multi-area supported). */
  GstMemory *area_cache[MAX_OVERLAY_AREA_NUM];
  /* Last source fingerprint per area (string/bmp) for static_content invalidation. */
  guint32 last_static_source_hash[MAX_OVERLAY_AREA_NUM];
  unsigned char last_static_source_ready[MAX_OVERLAY_AREA_NUM];
  guint frame_counter;

  /* Per output-memory slot (append order): attr vs data change vs last frame (for overlay flags). */
  unsigned int out_area_slot;
  unsigned char last_area_ready[MAX_OVERLAY_AREA_NUM];
  amba_rect_t last_area_rect[MAX_OVERLAY_AREA_NUM];
  unsigned char last_area_enable[MAX_OVERLAY_AREA_NUM];
  unsigned char last_area_draw_format[MAX_OVERLAY_AREA_NUM];
  guint32 last_area_content_hash[MAX_OVERLAY_AREA_NUM];
} draw_data_gen_priv_t;

/* draw_format string to enum/pix_size. Returns AMBA_DRAW_FORMAT_NONE on invalid. */
int drawdatagen_parse_format(const char *name, guint *pix_size);

#define OUTPUT_MODE_DRAW_DATA  0
#define OUTPUT_MODE_VIDEO      1

struct _GstAmbaDrawDataGen {
  GstBaseTransform parent;
  draw_data_gen_priv_t *priv;
};

struct _GstAmbaDrawDataGenClass {
  GstBaseTransformClass parent_class;
};

GType gst_amba_draw_data_gen_get_type(void);

G_END_DECLS

#endif /* __GST_AMBA_DRAW_DATA_GEN_H__ */
