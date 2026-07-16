/*
 * gstambavencblur.c
 *
 * History:
 *    1/22/2026 - [Cheng Chen] created file
 *
 * Copyright (C) 2026 Ambarella International LP
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
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
#include "amba_private_data.h"
#include "gstambavencblur.h"
#include "osd_draw_types.h"

#define DEFAULT_MAX_STREAM_ID           0xfff
#define DEFAULT_MAX_VIN_ID              0x3

GST_DEBUG_CATEGORY_STATIC (gst_amba_venc_blur_debug);
#define GST_CAT_DEFAULT gst_amba_venc_blur_debug

enum {
  LAST_SIGNAL
};

enum {
  PROP_0,
  PROP_STREAM_ID,
  PROP_STRENGTH,
  PROP_IS_BLOCKY,
  PROP_IS_COEFF,
  PROP_AUTO_ROTATE,
  PROP_BLUR_COLOR_INDEX,
  PROP_BLUR_TYPE,
  PROP_SYNC_WITH_PTS,
};

typedef enum blur_status_s {
  INIT = 0,
  BLUR_ENABLE,
  BLUR_DISABLE,
} blur_status_t;

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_AMBA_ML_DECODED_CAPS));

#define gst_amba_venc_blur_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstAmbaVencBlur, gst_amba_venc_blur, GST_TYPE_BASE_SINK,
  GST_DEBUG_CATEGORY_INIT(gst_amba_venc_blur_debug, "amba_venc_blur", 0,
  "blur sink"));

#define AMBAVENC_DEFAULT_ENC_FORMAT "stream_id:0"

static void gst_amba_venc_blur_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_amba_venc_blur_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_amba_venc_blur_finalize (GObject *gobject);
static gboolean gst_amba_venc_blur_start (GstBaseSink * sink);
static gboolean gst_amba_venc_blur_stop (GstBaseSink * sink);
static GstFlowReturn gst_amba_venc_blur_draw_render(GstBaseSink *sink, GstBuffer *buffer);

static int set_blur(GstAmbaVencBlur *self)
{
  iav_blur_apply_cfg_t apply = {0};
  guint8 stream_id = self->stream_id;
  blur_stream_info_t stream_info = self->stream_info[stream_id];
  iav_ctx_t *iav_ctx = self->iav_ctx;
  int ret = 0;

  if (stream_info.blur_enable == BLUR_DISABLE) {
    self->blur_insert[stream_id].enable = 0;
  } else {
    self->blur_insert[stream_id].enable = 1;
  }

  if (iav_ctx->iav_al.f_blur_set_stream_cfg(&self->blur_insert[stream_id]) < 0) {
    GST_ERROR("f_blur_set_stream_cfg failed");
    ret = -1;
    return ret;
  }

  apply.stream_map |= (1 << stream_id);
  apply.frame_sync = 1;
  if (iav_ctx->iav_al.f_blur_apply(&apply) < 0) {
    GST_ERROR("f_blur_apply failed");
    ret = -1;
    return ret;
  }

  return ret;
}

static int prepare_blur_init_config(amba_ml_detection_result_t *detections, GstAmbaVencBlur *self,
  int *act_blur_area_num)
{
  guint8 stream_id = self->stream_id;
  blur_stream_info_t *stream_info = &self->stream_info[stream_id];
  blur_info_t *blur;
  int i, rval = 0;
  int blur_area_num = 0;
  int32_t det_num = detections->det_num;

  for (i = 0; i < det_num; i++) {
    if (i >= MAX_NUM_BLUR_AREA - 1) {
      GST_WARNING("Loop index i(%d) exceeds MAX_NUM_BLUR_AREA(%d)", i, MAX_NUM_BLUR_AREA - 1);
      break;
    }

    blur = &stream_info->blurs[i];
    blur->width = detections->detections[i].x_end - detections->detections[i].x_start;
    blur->height = detections->detections[i].y_end - detections->detections[i].y_start;
    blur->x = detections->detections[i].x_start;
    blur->y = detections->detections[i].y_start;

    blur_area_num++;
    if (blur->width == 0 || blur->height == 0) {
      GST_ERROR("Stream%d blur area %d width[%d] or height[%d] is invalid",
          stream_id, i, blur->width, blur->height);
      rval = -1;
      goto PREPARE_BLUR_INIT_CONFIG_EXIT;
    }
  }

  *act_blur_area_num = blur_area_num;

PREPARE_BLUR_INIT_CONFIG_EXIT:
  return rval;
}

static int prepare_blur_config(amba_ml_detection_result_t *detections, GstAmbaVencBlur *self)
{
  guint8 stream_id = self->stream_id;
  blur_stream_info_t *stream_info = &self->stream_info[stream_id];
  blur_info_t *blur;
  iav_blur_area_cfg_t *area;
  int win_width, win_height, ret = 0;
  unsigned int i;
  int act_blur_area_num = 0;

  if (prepare_blur_init_config(detections, self, &act_blur_area_num) < 0) {
    ret = -1;
    goto PREPARE_BLUR_CONFIG_EXIT;
  }

  win_width = stream_info->blur_win_width;
  win_height = stream_info->blur_win_height;
  self->blur_insert[stream_id].act_area_num = act_blur_area_num;
  self->blur_insert[stream_id].type = stream_info->blur_type;

  for (i = 0; i < detections->det_num; i++) {
    if (i >= MAX_NUM_BLUR_AREA - 1) {
      GST_WARNING("Loop index i(%d) exceeds MAX_NUM_BLUR_AREA(%d)", i, MAX_NUM_BLUR_AREA - 1);
      break;
    }

    blur = &stream_info->blurs[i];
    area = &self->blur_insert[stream_id].area[i];

    if (stream_info->blur_rotate & AMBA_DRAW_ROTATE_90) {
      area->act_width = blur->height = ROUND_DOWN(blur->height, BLUR_WIDTH_ALIGN);
      area->act_height = blur->width = ROUND_DOWN(blur->width, BLUR_HEIGHT_ALIGN);
    } else {
      area->act_width = blur->width = ROUND_DOWN(blur->width, BLUR_WIDTH_ALIGN);
      area->act_height = blur->height = ROUND_DOWN(blur->height, BLUR_HEIGHT_ALIGN);
    }

    area->strength = blur->strength;
    area->is_blocky = blur->is_blocky;
    area->coeff = blur->coeff;
    area->color_enable = blur->color_enable;

    if (area->color_enable) {
      area->color_idx = blur->color_idx;
    }

    switch (stream_info->blur_rotate) {
      case AMBA_DRAW_NO_ROTATE_FLIP:
        area->start_x = blur->x = ROUND_DOWN(blur->x, BLUR_OFFSET_ALIGN);
        area->start_y = blur->y = ROUND_DOWN(blur->y, BLUR_OFFSET_ALIGN);
        break;
      case AMBA_DRAW_CW_ROTATE_90:
        area->start_x = blur->y = ROUND_DOWN(blur->y, BLUR_OFFSET_ALIGN);
        area->start_y = ROUND_DOWN(win_width - blur->x - blur->width, BLUR_OFFSET_ALIGN);
        blur->x = win_width - blur->width - area->start_y;
        break;
      case AMBA_DRAW_CW_ROTATE_180:
        area->start_x = ROUND_DOWN(win_width - blur->x - blur->width, BLUR_OFFSET_ALIGN);
        area->start_y = ROUND_DOWN(win_height - blur->y - blur->height, BLUR_OFFSET_ALIGN);
        blur->x = win_width - blur->width - area->start_x;
        blur->y = win_height - blur->height - area->start_y;
        break;
      case AMBA_DRAW_CW_ROTATE_270:
        area->start_x = ROUND_DOWN(win_height - blur->y - blur->height, BLUR_OFFSET_ALIGN);
        area->start_y = blur->x = ROUND_DOWN(blur->x, BLUR_OFFSET_ALIGN);
        blur->y = win_height - blur->height - area->start_x;
        break;
      default:
        GST_ERROR("unknown rotate type");
        ret = -1;
        goto PREPARE_BLUR_CONFIG_EXIT;
    }

    if (area->start_x + area->act_width > self->enc_win.width ||
        area->start_y + area->act_height > self->enc_win.height) {
      GST_ERROR("Blur area %d out of stream bounds", i);
      ret = -1;
      goto PREPARE_BLUR_CONFIG_EXIT;
    }

    area->start_x += self->enc_win.x;
    area->start_y += self->enc_win.y;
    area->enable = 1;
  }

PREPARE_BLUR_CONFIG_EXIT:
  return ret;
}

static int set_blur_color(GstAmbaVencBlur *self)
{
  iav_ctx_t *iav_ctx = self->iav_ctx;
  iav_blur_color_cfg_t *blur_color_info = &self->blur_color_info;
  int i;

  iav_ctx->iav_al.f_blur_set_color(blur_color_info);
  iav_ctx->iav_al.f_blur_get_color(blur_color_info);

  GST_INFO("Blur Color Table:");
  for (i = 0; i < MAX_NUM_BLUR_COLOR; i++) {
    if ((1 << i) & blur_color_info->color_idx_map) {
      GST_INFO("  Color Idx[%2d]: U = %3d, V = %3d",
          i, blur_color_info->U[i], blur_color_info->V[i]);
    }
  }

  return 0;
}

static void gst_amba_venc_blur_class_init (GstAmbaVencBlurClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GstBaseSinkClass *base_sink_class = GST_BASE_SINK_CLASS (klass);

  gobject_class->set_property = gst_amba_venc_blur_set_property;
  gobject_class->get_property = gst_amba_venc_blur_get_property;
  gobject_class->finalize = gst_amba_venc_blur_finalize;

  base_sink_class->start = GST_DEBUG_FUNCPTR (gst_amba_venc_blur_start);
  base_sink_class->stop = GST_DEBUG_FUNCPTR (gst_amba_venc_blur_stop);
  base_sink_class->render = GST_DEBUG_FUNCPTR (gst_amba_venc_blur_draw_render);

  g_object_class_install_property (gobject_class, PROP_STREAM_ID,
      g_param_spec_uint ("stream_id", "StreamId", "Provide stream id",
          0, IAV_STREAM_MAX_NUM_ALL, 0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_STRENGTH,
      g_param_spec_uint ("strength", "Strength", "specify the blur strength of blur area",
          0, 2, 0, G_PARAM_READWRITE));

#if defined (BUILD_DSP_AMBA_V6)
  g_object_class_install_property (gobject_class, PROP_IS_BLOCKY,
      g_param_spec_uint ("is_blocky", "Is_Blocky", "enable/disable blocky blur effect",
          0, 1, 0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_IS_COEFF,
      g_param_spec_uint ("is_coeff", "Is_Coeff", "set blur coeff",
          0, MAX_BLUR_COEFF_SMOOTH, 0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_BLUR_TYPE,
      g_param_spec_int ("blur_type", "Blur_Type", "Set blur type",
          0, 1, 0, G_PARAM_READWRITE));
#endif

  g_object_class_install_property (gobject_class, PROP_AUTO_ROTATE,
      g_param_spec_uint ("autorotate", "AutoRotate", "Blur is consistent with stream orientation",
          0, 1, 0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_BLUR_COLOR_INDEX,
      g_param_spec_uint ("color_idx", "Color_Idx", "Set color idx of blur area",
          0, MAX_NUM_BLUR_COLOR, 0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_SYNC_WITH_PTS,
      g_param_spec_uint ("sync_pts", "SyncPTS", "Blur sync with pts",
          0, 1, 0, G_PARAM_READWRITE));

  gst_element_class_add_static_pad_template (gstelement_class, &sink_factory);
  gst_element_class_set_static_metadata (gstelement_class,
      "Amba Venc Blur",
      "Sink/Video",
      "Virtual video sink for amba venc blur",
      "Cheng Chen <cchen@ambarella.com>");
}

static void gst_amba_venc_blur_init (GstAmbaVencBlur *self)
{
  uint8_t color_table_u[MAX_NUM_BLUR_COLOR] = {128, 118, 110, 90, 85, 71, 47, 45,
                             53, 139, 174, 202, 237, 220, 210};
  uint8_t color_table_v[MAX_NUM_BLUR_COLOR] = {128, 148, 180, 240, 198, 182, 150, 127,
                             104, 10, 11, 46, 148, 202, 227};
  uint8_t i = 0;

  self->stream_id = 0;
  self->iav_ctx = acquire_iav_ctx(1);
  if (!self->iav_ctx) {
    DPRINT_ERROR("acquire_iav_ctx failed");
    return;
  }

  memset(&self->blur_color_info, 0, sizeof(self->blur_color_info));
  for (i = 0; i < MAX_NUM_BLUR_COLOR; i++) {
    self->blur_color_info.U[i] = color_table_u[i];
    self->blur_color_info.V[i] = color_table_v[i];
    self->blur_color_info.color_idx_map |= (1 << i);
  }
}

static void gst_amba_venc_blur_finalize (GObject *gobject)
{
  GstAmbaVencBlur *self = GST_AMBAVENCBLUR (gobject);
  int i = 0;

  for (i = 0; i < IAV_STREAM_MAX_NUM_ALL; ++i) {
    if (self->stream_id_map & (1 << i)) {
      self->stream_info[i].blur_enable = BLUR_DISABLE;
      if (set_blur(self) < 0) {
        GST_ERROR("set_blur error");
        break;
      }
    }
  }

  if (self->iav_ctx->iav_al.f_apply_frame_sync(self->iav_ctx->iav_fd, self->dsp_pts,
    self->stream_id_map, 1) < 0) {
    GST_ERROR("f_apply_frame_sync error");
    return;
  }

  if (self->iav_ctx) {
    release_iav_ctx(1);
    self->iav_ctx = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (gobject);
}

static void gst_amba_venc_blur_set_property (GObject *object, guint prop_id,
    const GValue *value, GParamSpec *pspec)
{
  GstAmbaVencBlur *self = GST_AMBAVENCBLUR (object);
  blur_stream_info_t *stream_info = self->stream_info;
  int i = 0;

  switch (prop_id) {
    case PROP_STREAM_ID:
      self->stream_id = g_value_get_uint(value);
      if (self->stream_id >= IAV_STREAM_MAX_NUM_ALL) {
        GST_ERROR("Invalid stream id %u", self->stream_id);
        return;
      }
      stream_info[self->stream_id].blur_enable = BLUR_ENABLE;
      self->stream_id_map |= (1 << self->stream_id);
      break;

    case PROP_STRENGTH:
      for (i = 0; i < MAX_BLUR_AREA_NUM; i++) {
        stream_info[self->stream_id].blurs[i].strength = g_value_get_uint(value);
      }
      if (stream_info[self->stream_id].blurs[0].strength > MAX_BLUR_STRENGTH) {
        GST_ERROR("Invalid strength %u", stream_info[self->stream_id].blurs[0].strength);
      }
      break;

#if defined (BUILD_DSP_AMBA_V6)
    case PROP_IS_BLOCKY:
      for (i = 0; i < MAX_BLUR_AREA_NUM; i++) {
        stream_info[self->stream_id].blurs[i].is_blocky = g_value_get_uint(value);
      }
      break;

    case PROP_IS_COEFF:
      for (i = 0; i < MAX_BLUR_AREA_NUM; i++) {
        stream_info[self->stream_id].blurs[i].coeff = g_value_get_uint(value);
      }
      if (stream_info[self->stream_id].blurs[0].coeff > MAX_BLUR_COEFF_SMOOTH) {
        GST_ERROR("Invalid coeff %u", stream_info[self->stream_id].blurs[0].coeff);
      }
      break;

    case PROP_BLUR_TYPE:
      stream_info[self->stream_id].blur_type = g_value_get_uint(value);
      if (stream_info[self->stream_id].blur_type >= IAV_BLUR_TYPE_NUM) {
        GST_ERROR("Invalid blur type %u", stream_info[self->stream_id].blur_type);
      }
      break;
#endif
    case PROP_AUTO_ROTATE:
      if (g_value_get_uchar(value)) {
        stream_info[self->stream_id].blur_rotate = AMBA_DRAW_AUTO_ROTATE;
      }
      break;

    case PROP_BLUR_COLOR_INDEX:
      for (i = 0; i < MAX_BLUR_AREA_NUM; i++) {
        stream_info[self->stream_id].blurs[i].color_enable = 1;
        stream_info[self->stream_id].blurs[i].color_idx = g_value_get_uint(value);
      }
      break;
    case PROP_SYNC_WITH_PTS:
      stream_info[self->stream_id].sync_with_pts = !!g_value_get_uint(value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
      break;
  }
}

static void gst_amba_venc_blur_get_property (GObject *object, guint prop_id,
    GValue *value, GParamSpec *pspec)
{
  GstAmbaVencBlur *self = GST_AMBAVENCBLUR (object);
  blur_stream_info_t *stream_info = self->stream_info;

  switch (prop_id) {
    case PROP_STREAM_ID:
      g_value_set_uint(value, self->stream_id);
      break;
    case PROP_STRENGTH:
      g_value_set_uint(value, stream_info[self->stream_id].blurs[0].strength);
      break;
#if defined (BUILD_DSP_AMBA_V6)
    case PROP_IS_BLOCKY:
      g_value_set_uint(value, stream_info[self->stream_id].blurs[0].is_blocky);
      break;
    case PROP_IS_COEFF:
      g_value_set_uint(value, stream_info[self->stream_id].blurs[0].coeff);
      break;
    case PROP_BLUR_TYPE:
      g_value_set_int(value, stream_info[self->stream_id].blur_type);
      break;
#endif
    case PROP_AUTO_ROTATE:
      g_value_set_uint(value, stream_info[self->stream_id].blur_rotate);
      break;
    case PROP_BLUR_COLOR_INDEX:
      g_value_set_uint(value, stream_info[self->stream_id].blurs[0].color_idx);
      break;
    case PROP_SYNC_WITH_PTS:
      g_value_set_uint(value, stream_info[self->stream_id].sync_with_pts);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
      break;
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

static gboolean gst_amba_venc_blur_start (GstBaseSink *sink)
{
  GstAmbaVencBlur *self = GST_AMBAVENCBLUR (sink);
  iav_ctx_t *iav_ctx = self->iav_ctx;
  struct iav_stream_cfg stream_cfg;
  struct iav_stream_format *stream_format = NULL;
  iav_blur_area_mem_cfg_t *area_mem_cfg;
  iav_blur_mem_cfg_t init_cfg;
  int i, win_width, win_height;
  guint8 stream_id = self->stream_id;
  int iav_state = IAV_STATE_INIT;

  if (iav_ctx->iav_al.f_blur_lib_init(iav_ctx->iav_fd) < 0) {
    GST_ERROR("Blur lib init failed");
    return FALSE;
  }

  if (self->blur_color_info.color_idx_map) {
    if (set_blur_color(self) < 0) {
      GST_ERROR("set_blur_color failed");
      return FALSE;
    }
  }

  if (ioctl(iav_ctx->iav_fd, IAV_IOC_GET_IAV_STATE, &iav_state) < 0) {
    GST_ERROR("Failed to get IAV state");
    return FALSE;
  }

  if (iav_state != IAV_STATE_PREVIEW && iav_state != IAV_STATE_ENCODING) {
    GST_ERROR("IAV not in preview or encoding state");
    return FALSE;
  }

  memset(&stream_cfg, 0, sizeof(stream_cfg));
  stream_cfg.id = stream_id;
  stream_cfg.cid = IAV_STMCFG_FORMAT;
  if (ioctl(iav_ctx->iav_fd, IAV_IOC_GET_STREAM_CONFIG, &stream_cfg) < 0) {
    GST_ERROR("Failed to get stream config");
    return FALSE;
  }

  stream_format = &stream_cfg.arg.format;
  if (self->stream_info[stream_id].blur_rotate == AMBA_DRAW_AUTO_ROTATE) {
    self->stream_info[stream_id].blur_rotate = AMBA_DRAW_NO_ROTATE_FLIP;
    self->stream_info[stream_id].blur_rotate |= (stream_format->rotate_cw ? AMBA_DRAW_ROTATE_90 : 0);
    self->stream_info[stream_id].blur_rotate |= (stream_format->hflip ? AMBA_DRAW_HORIZONTAL_FLIP : 0);
    self->stream_info[stream_id].blur_rotate |= (stream_format->vflip ? AMBA_DRAW_VERTICAL_FLIP : 0);

    if (!is_valid_rotate(self->stream_info[stream_id].blur_rotate)) {
      GST_WARNING("Invalid rotate type, use default");
      self->stream_info[stream_id].blur_rotate = AMBA_DRAW_NO_ROTATE_FLIP;
    }
  }

  if (self->stream_info[stream_id].blur_rotate & AMBA_DRAW_ROTATE_90) {
    self->stream_info[stream_id].blur_win_width = stream_format->enc_win.height;
    self->stream_info[stream_id].blur_win_height = stream_format->enc_win.width;
  } else {
    self->stream_info[stream_id].blur_win_width = stream_format->enc_win.width;
    self->stream_info[stream_id].blur_win_height = stream_format->enc_win.height;
  }

  self->enc_win.width = stream_format->enc_win.width;
  self->enc_win.height = stream_format->enc_win.height;
  self->enc_win.x = stream_format->enc_win.x;
  self->enc_win.y = stream_format->enc_win.y;

  memset(&init_cfg, 0, sizeof(init_cfg));
  win_width = self->stream_info[stream_id].blur_win_width;
  win_height = self->stream_info[stream_id].blur_win_height;

  for (i = 0; i < MAX_NUM_BLUR_AREA; i++) {
    area_mem_cfg = &init_cfg.area[stream_id][i];
    if (self->stream_info[stream_id].blur_rotate & AMBA_DRAW_ROTATE_90) {
      area_mem_cfg->max_width = win_height;
      area_mem_cfg->max_height = win_width;
    } else {
      area_mem_cfg->max_width = win_width;
      area_mem_cfg->max_height = win_height;
    }

    if (area_mem_cfg->max_width < MIN_BLUR_WIDTH) {
      area_mem_cfg->max_width = MIN_BLUR_WIDTH;
    }
    if (!area_mem_cfg->max_height) {
      area_mem_cfg->max_height = BLUR_HEIGHT_ALIGN;
    }
  }

  init_cfg.max_area_num[stream_id] = MAX_NUM_BLUR_AREA - 1;
  if (iav_ctx->iav_al.f_blur_set_mem_cfg(&init_cfg) < 0) {
    GST_ERROR("f_blur_set_mem_cfg failed");
    return FALSE;
  }

  self->blur_insert[stream_id].stream_id = stream_id;
  if (iav_ctx->iav_al.f_blur_get_stream_cfg(&self->blur_insert[stream_id]) < 0) {
    GST_ERROR("f_blur_get_stream_cfg failed");
    return FALSE;
  }

  return TRUE;
}

static gboolean gst_amba_venc_blur_stop (GstBaseSink *sink)
{
  return TRUE;
}

static GstFlowReturn
gst_amba_venc_blur_draw_render (GstBaseSink *sink, GstBuffer *buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstMemory *mem0 = NULL;
  GstMapInfo map0;
  amba_ml_bbox_result_t *result = NULL;
  AmbaPrivateDataMeta *priv_meta = NULL;
  GstAmbaVencBlur *self = GST_AMBAVENCBLUR (sink);
  unsigned int dsp_pts = 0;

  GST_DEBUG_OBJECT(sink, "render ts %" GST_TIME_FORMAT,
      GST_TIME_ARGS(GST_BUFFER_PTS(buffer)));

  do {
    mem0 = gst_buffer_get_memory(buffer, 0);
    if (!mem0) {
      GST_ERROR("Failed to get memory");
      ret = GST_FLOW_ERROR;
      break;
    }

    if (!gst_memory_map(mem0, &map0, GST_MAP_READ)) {
      GST_ERROR("Failed to map memory");
      ret = GST_FLOW_ERROR;
      gst_memory_unref(mem0);
      break;
    }

    result = (amba_ml_bbox_result_t *)map0.data;
    if (result->header.type != AMBA_ML_RESULT_TYPE_DETECTION_BBOX) {
      GST_ERROR("Invalid detection result type");
      ret = GST_FLOW_ERROR;
      goto BLUR_SHOW_FRAME_EXIT;
    }

    if (prepare_blur_config(&result->detections, self) < 0) {
      ret = GST_FLOW_ERROR;
      goto BLUR_SHOW_FRAME_EXIT;
    }

    if (set_blur(self) < 0) {
      ret = GST_FLOW_ERROR;
      goto BLUR_SHOW_FRAME_EXIT;
    }

    if (self->stream_info[self->stream_id].sync_with_pts) {
      priv_meta = amba_buffer_get_private_data_meta(buffer);
      if (!priv_meta) {
        GST_ERROR("No private meta");
        ret = GST_FLOW_ERROR;
        goto BLUR_SHOW_FRAME_EXIT;
      }

      dsp_pts = priv_meta->dsp_pts;
      self->dsp_pts = dsp_pts;
      if (self->iav_ctx->iav_al.f_apply_frame_sync(self->iav_ctx->iav_fd, dsp_pts,
        (1U << self->stream_id), 0) < 0) {
        GST_ERROR("f_apply_frame_sync failed");
        ret = GST_FLOW_ERROR;
        goto BLUR_SHOW_FRAME_EXIT;
      }
    }

BLUR_SHOW_FRAME_EXIT:
    gst_memory_unmap(mem0, &map0);
    gst_memory_unref(mem0);
  } while (0);

  return ret;
}
