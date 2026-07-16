/*
 * gstambaefr.c
 *
 * History:
 *    5/20/2025 - [Ji Zhang] created file
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
 * SECTION:element-amba_efr
 * @title: amba_efr
 *
 * ## Example launch lines
 * |[
 * gst-launch-1.0 filesrc location=test.yuv blocksize=3110400 ! amba_efr location=cap_nv12.yuv
 * ]|
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <gst/gst.h>
#include <gst/base/gstbasesink.h>
#include "unistd.h"

#include "internal.h"
#include "iav_al.h"
#include "iav_ctx.h"
#include "gstambaefr.h"

GST_DEBUG_CATEGORY_STATIC (gst_amba_efr_debug);
#define GST_CAT_DEFAULT gst_amba_efr_debug

#if defined (BUILD_DSP_AMBA_V5)
#define AMBA_GST_EFR_MAX_WIDTH 4096
#elif defined (BUILD_DSP_AMBA_V6)
#define AMBA_GST_EFR_MAX_WIDTH 8192
#else
#define AMBA_GST_EFR_MAX_WIDTH 3840
#endif

#if defined (BUILD_DSP_AMBA_V5)
#define AMBA_GST_EFR_MAX_HEIGHT 3000
#elif defined (BUILD_DSP_AMBA_V6)
#define AMBA_GST_EFR_MAX_HEIGHT 5120
#else
#define AMBA_GST_EFR_MAX_HEIGHT 2160
#endif

#define AMBA_GST_EFR_VINC_ID_MAX (VIN_CONTROLLER_NUM - 1)

enum
{
  AMBA_GST_EFR_TYPE_RAW_RGB = 0,
  AMBA_GST_EFR_TYPE_RAW_YUV = 1,	//NV16
  AMBA_GST_EFR_TYPE_RAW_NV12 = 2,
  AMBA_GST_EFR_TYPE_RAW_NUM,
  AMBA_GST_EFR_TYPE_RAW_FIRST = AMBA_GST_EFR_TYPE_RAW_RGB,
  AMBA_GST_EFR_TYPE_RAW_LAST = AMBA_GST_EFR_TYPE_RAW_NUM,
};

enum
{
  PROP_EFR_0,
  PROP_EFR_DEBUG_SAVE_FILE_LOCATION,
  PROP_EFR_FILE_WIDTH,
  PROP_EFR_FILE_HEIGHT,
  PROP_EFR_FILE_TYPE,
  PROP_EFR_VINC_ID,
  PROP_EFR_LIVE_MODE,
  PROP_LAST
};

static guint64 render_delay = 0;
static u64 count = 0;
static GstClockTime copy_duration = 0;
static GstClockTime curr_time = 0, prev_time = 0;
static u8 prepare_done = 0;
static gchar GST_AMBA_AUDIO_TICK[] = "/proc/ambarella/ambarella_hwtimer_audio_tick";

/*             amba_efr element register, init, class init                */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE (
    "sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

G_DEFINE_TYPE_WITH_CODE (GstAmbaEfr, gst_amba_efr, GST_TYPE_BASE_SINK,
    GST_DEBUG_CATEGORY_INIT (gst_amba_efr_debug, "amba_efr", 0,
        "amba_efr"));

static void gst_amba_efr_set_property (GObject * object, guint property_id,
  const GValue * value, GParamSpec * pspec);
static void gst_amba_efr_get_property (GObject * object, guint property_id,
  GValue * value, GParamSpec * pspec);
static void gst_amba_efr_finalize (GObject *gobject);
static GstFlowReturn gst_amba_efr_run (GstBaseSink * sink, GstBuffer * buffer);

static void gst_amba_efr_class_init (GstAmbaEfrClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstBaseSinkClass *base_sink_class = GST_BASE_SINK_CLASS (klass);

  object_class->set_property = gst_amba_efr_set_property;
  object_class->get_property = gst_amba_efr_get_property;
  object_class->finalize = gst_amba_efr_finalize;

  g_object_class_install_property (object_class, PROP_EFR_DEBUG_SAVE_FILE_LOCATION,
    g_param_spec_string ("location", "File Location",
      "Location of the file to write for debug purpose", NULL,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS) );

  g_object_class_install_property (object_class, PROP_EFR_FILE_WIDTH,
    g_param_spec_uint ("width", "File Width", "Specify width of the file",
      0, AMBA_GST_EFR_MAX_WIDTH,
      0, G_PARAM_READWRITE) );

  g_object_class_install_property (object_class, PROP_EFR_FILE_HEIGHT,
    g_param_spec_uint ("height", "File Height", "Specify height of the file",
      0, AMBA_GST_EFR_MAX_HEIGHT,
      0, G_PARAM_READWRITE) );

  g_object_class_install_property (object_class, PROP_EFR_FILE_TYPE,
    g_param_spec_uint ("type", "File Type", "Specify type of the file, 0: RGB Raw, 1: NV16 Raw, 2: NV12 Raw",
      0, AMBA_GST_EFR_TYPE_RAW_NUM - 1,
      0, G_PARAM_READWRITE) );

  g_object_class_install_property (object_class, PROP_EFR_VINC_ID,
    g_param_spec_uint ("vinc_id", "Vinc Id", "Specify Virtual VIN ID for raw encode",
      0, AMBA_GST_EFR_VINC_ID_MAX,
      0, G_PARAM_READWRITE) );

  g_object_class_install_property (object_class, PROP_EFR_LIVE_MODE,
    g_param_spec_boolean ("live-mode", "Live Mode",
      "Skip render-delay throttling for live upstream input",
      FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS) );

  base_sink_class->render = gst_amba_efr_run;

  gst_element_class_add_static_pad_template (element_class, &sink_factory);
  gst_element_class_set_static_metadata (element_class, "Amba EFR Setup",
      "Sink/Efr",
      "Setup EFR pipeline on Ambarella SoC",
      "Ji Zhang <jzhanga@ambarella.com>");
}

static void gst_amba_efr_init (GstAmbaEfr * thiz)
{
  thiz->location = NULL;
  thiz->file = NULL;
  thiz->width = 0;
  thiz->height = 0;
  thiz->file_type = 0;
  thiz->vinc_id = 0;
  thiz->live_mode = FALSE;
  thiz->fd_audio_tick = -1;

  prepare_done = 0;
  curr_time = 0;
  prev_time = 0;

  return;
}

static void gst_amba_efr_finalize (GObject *gobject)
{
  GstAmbaEfr *thiz = GST_AMBA_EFR (gobject);

  if (thiz->iav_ctx) {
    // release USR partition
#if defined (BUILD_DSP_AMBA_V5)
      if (thiz->iav_partition.request_size != 0) {
        thiz->iav_ctx->iav_al.f_unmap_efr_mem(thiz->iav_ctx->iav_fd, &thiz->iav_partition);
      }
#elif defined (BUILD_DSP_AMBA_V6)
      if (thiz->iav_partition.request_size != 0) {
        thiz->iav_ctx->iav_al.f_unmap_efr_mem(thiz->iav_ctx->iav_fd, &thiz->iav_partition);
      }
#endif

    release_iav_ctx (1);
    thiz->iav_ctx = NULL;
  }

  if (thiz->file) {
    fclose (thiz->file);
    thiz->file = NULL;
  }
  if (thiz->location) {
    g_free (thiz->location);
    thiz->location = NULL;
  }
  if (thiz->fd_audio_tick) {
    close(thiz->fd_audio_tick);
    thiz->fd_audio_tick = -1;
  }
  G_OBJECT_CLASS (gst_amba_efr_parent_class)->finalize (gobject);
}

static void gst_amba_efr_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAmbaEfr *thiz = GST_AMBA_EFR (object);
  switch (property_id) {
    case PROP_EFR_DEBUG_SAVE_FILE_LOCATION:
      if (thiz->file) {
        fclose (thiz->file);
        thiz->file = NULL;
      }
      if (thiz->location) {
        g_free (thiz->location);
        thiz->location = NULL;
      }
      thiz->location = g_value_dup_string (value);
      if (thiz->location) {
        thiz->file = fopen (thiz->location, "wb");
        if (!thiz->file) {
          GST_ERROR_OBJECT (thiz, "Failed to open file: %s", thiz->location);
        }
      }
      break;
    case PROP_EFR_FILE_WIDTH:
      thiz->width = g_value_get_uint (value);
      break;
    case PROP_EFR_FILE_HEIGHT:
      thiz->height = g_value_get_uint (value);
      break;
    case PROP_EFR_FILE_TYPE:
      thiz->file_type = g_value_get_uint (value);
      break;
    case PROP_EFR_VINC_ID:
      thiz->vinc_id = g_value_get_uint (value);
      break;
    case PROP_EFR_LIVE_MODE:
      thiz->live_mode = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void gst_amba_efr_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstAmbaEfr *thiz = GST_AMBA_EFR (object);
  switch (property_id) {
    case PROP_EFR_DEBUG_SAVE_FILE_LOCATION:
      g_value_set_string (value, thiz->location);
      break;
    case PROP_EFR_FILE_WIDTH:
      g_value_set_uint (value, thiz->width);
      break;
    case PROP_EFR_FILE_HEIGHT:
      g_value_set_uint (value, thiz->height);
      break;
    case PROP_EFR_FILE_TYPE:
      g_value_set_uint (value, thiz->file_type);
      break;
    case PROP_EFR_VINC_ID:
      g_value_set_uint (value, thiz->vinc_id);
      break;
    case PROP_EFR_LIVE_MODE:
      g_value_set_boolean (value, thiz->live_mode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static GstFlowReturn gst_amba_dump_file(GstAmbaEfr *thiz, GstBuffer * buffer)
{
  GstMapInfo map;

  memset(&map, 0, sizeof(map));

  if (gst_buffer_map (buffer, &map, GST_MAP_READ)) {
    size_t written = fwrite (map.data, 1, map.size, thiz->file);
    if (written != map.size) {
      GST_ERROR_OBJECT (thiz, "Failed to write all data to file");
      gst_buffer_unmap (buffer, &map);
      return GST_FLOW_ERROR;
    }
    gst_buffer_unmap (buffer, &map);
  } else {
    GST_ERROR_OBJECT (thiz, "Failed to map buffer");
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}

/*             amba_efr element run dsp efr pipeline                */
static int gst_amba_efr_params_check(GstAmbaEfr *thiz)
{
  if (!thiz->resource_info.enc_raw_rgb && !thiz->resource_info.enc_raw_nv12 &&
      !thiz->resource_info.enc_raw_yuv) {
      GST_ERROR_OBJECT (thiz, "EFR is not enabled, please enable it when entering preview!");
      return -1;
  }

  if (!thiz->resource_info.enc_raw_rgb && thiz->file_type == AMBA_GST_EFR_TYPE_RAW_RGB) {
    GST_ERROR_OBJECT (thiz, "Please set enc_raw_rgb = 1 to enter preview, when the fed file type is RGB raw!");
    return -1;
  } else if (thiz->resource_info.enc_raw_rgb && thiz->file_type != AMBA_GST_EFR_TYPE_RAW_RGB) {
    GST_ERROR_OBJECT (thiz, "Please set file_type = 0 when it is EFR from RGB raw!");
    return -1;
  } else {
    // do nothing
  }

  if (!thiz->resource_info.enc_raw_nv12 && thiz->file_type == AMBA_GST_EFR_TYPE_RAW_NV12) {
    GST_ERROR_OBJECT (thiz, "Please set enc_raw_nv12 = 1 to enter preview, when the fed file type is NV12 raw!");
    return -1;
  } else if (thiz->resource_info.enc_raw_nv12 && thiz->file_type != AMBA_GST_EFR_TYPE_RAW_NV12) {
    //GST_ERROR_OBJECT (thiz, "Please set file_type = 2 when it is EFR from NV12 yuv raw!");
    printf("Please set file_type = 2 when it is EFR from NV12 yuv raw!");
    return -1;
  } else {
    // do nothing
  }

  if (!thiz->resource_info.enc_raw_yuv && thiz->file_type == AMBA_GST_EFR_TYPE_RAW_YUV) {
    GST_ERROR_OBJECT (thiz, "Please set enc_raw_yuv = 1 to enter preview, when the fed file type is NV12 raw!");
    return -1;
  } else if (thiz->resource_info.enc_raw_yuv && thiz->file_type != AMBA_GST_EFR_TYPE_RAW_YUV) {
    GST_ERROR_OBJECT (thiz, "Please set file_type = 1 when it is EFR from NV16 yuv raw!");
    return -1;
  } else {
    // do nothing
  }

  if (!thiz->width || !thiz->height) {
    GST_ERROR_OBJECT (thiz, "Please set width [%d] or height[%d]!\n", thiz->width, thiz->height);
    return -1;
  }

  return 0;
}

static u32 gst_amba_efr_get_frame_mem_size(GstAmbaEfr *thiz)
{
  u32 single_size = 0, single_ce_size = 0;

  switch (thiz->file_type) {
  case AMBA_GST_EFR_TYPE_RAW_RGB:
    single_size = ROUND_UP(thiz->width, GST_AMBA_EFR_PITCH_ALIGN) * thiz->height * 2;
    single_ce_size = ROUND_UP(thiz->width >> 2, GST_AMBA_EFR_PITCH_ALIGN) * thiz->height * 2;
    single_size = single_size + single_ce_size;
    break;
  case AMBA_GST_EFR_TYPE_RAW_NV12:
    single_size = ROUND_UP(thiz->width, GST_AMBA_EFR_PITCH_ALIGN) * thiz->height * 3 / 2;
    break;
  case AMBA_GST_EFR_TYPE_RAW_YUV:
    single_size = ROUND_UP(thiz->width, GST_AMBA_EFR_PITCH_ALIGN) * thiz->height * 2;
    break;
  default:
    break;
  }

  return single_size;
}

static int gst_amba_efr_prepare_memory(GstAmbaEfr *thiz)
{
  amba_efr_setup_t *efr_setup = &thiz->efr_setup;
  gint ret = 0;

  memset(&thiz->iav_partition, 0, sizeof(thiz->iav_partition));

  if (thiz->iav_ctx->iav_al.f_alloc_map_efr_mem == NULL) {
    GST_ERROR_OBJECT (thiz, "Please register f_alloc_map_efr_mem first");
    return -1;
  }

  /* For V6 and V5, EFR memory can be from anonymous partition */
  thiz->iav_partition.request_size = gst_amba_efr_get_frame_mem_size(thiz) * GST_EFR_RAW_BUF_NUM;
  ret = thiz->iav_ctx->iav_al.f_alloc_map_efr_mem(thiz->iav_ctx->iav_fd, &thiz->iav_partition);
  if (ret < 0) {
    GST_ERROR_OBJECT (thiz, "Fail to allocate and map efr memory");
  }

  memset(efr_setup, 0, sizeof(amba_efr_setup_t));
  efr_setup->buf_idx = 0;
  efr_setup->buf_num = GST_EFR_RAW_BUF_NUM;

  return ret;
}

static void gst_amba_efr_generate_hdec_raw(GstAmbaEfr *thiz, u8 *hdec, u8 *raw, u32 hdec_pitch, u32 raw_pitch)
{
  u16 *hds_ptr = NULL, *rgb_ptr = NULL;
  u32 i = 0, j = 0;

  for (i = 0; i < thiz->height; ++i) {
    hds_ptr = (u16 *)(hdec + i * hdec_pitch);
    rgb_ptr = (u16 *)(raw + i * raw_pitch);
    for (j = 0; j < thiz->width / 4; ++j) {
      hds_ptr[j] = *(rgb_ptr + 4 * j);
    }
  }

  return;
}

#if defined (BUILD_DSP_AMBA_V5)
static int do_gst_amba_efr_feed_frame(GstAmbaEfr *thiz)
{
#define AMBA_GST_PTS_SW_CLK (90000)
  amba_efr_setup_t *efr_setup = &thiz->efr_setup;
  gchar audio_tick_buf[32] = {0};
  int ret = 0;
  u32 raw_pitch = 0, raw_hdec_pitch = 0, raw_frame_size = 0, total_size = 0;
  u32 fps = 0;
  u8 raw_buf_idx = thiz->efr_setup.buf_idx;
  u8 is_rgb_raw = (thiz->file_type == AMBA_GST_EFR_TYPE_RAW_RGB);

  switch (thiz->file_type) {
  case AMBA_GST_EFR_TYPE_RAW_NV12:
  case AMBA_GST_EFR_TYPE_RAW_YUV:
    raw_pitch = ROUND_UP(thiz->width, GST_AMBA_EFR_PITCH_ALIGN);
    raw_frame_size = gst_amba_efr_get_frame_mem_size(thiz);
    total_size = raw_frame_size;
    break;
  case AMBA_GST_EFR_TYPE_RAW_RGB:
    raw_pitch = ROUND_UP(thiz->width << 1, GST_AMBA_EFR_PITCH_ALIGN);
    raw_hdec_pitch = ROUND_UP(raw_pitch >> 2, GST_AMBA_EFR_PITCH_ALIGN);
    raw_frame_size = raw_pitch * thiz->height;
    total_size = gst_amba_efr_get_frame_mem_size(thiz);
    break;
  default:
    break;
  }

  efr_setup->raw_format = is_rgb_raw ? IAV_RAW_FORMAT_DEFAULT : 0;
  efr_setup->hdec_raw_format = efr_setup->raw_format;
  efr_setup->pitch = raw_pitch;
  efr_setup->raw_hdec_dpitch = raw_hdec_pitch;
  efr_setup->raw_frame_size = raw_frame_size;

  efr_setup->vinc_id = thiz->vinc_id;
  efr_setup->raw_frame_num = 1;
  efr_setup->use_ext_buf = 1;
  efr_setup->ext_buf_addr = thiz->iav_partition.phys_addr;
  efr_setup->raw_daddr_offset = raw_buf_idx * total_size;
  if (is_rgb_raw) {
    efr_setup->raw_hdec_daddr_offset = efr_setup->raw_daddr_offset + raw_frame_size;
  } else {
    efr_setup->uv_daddr_offset = 0;
  }

  ret = read(thiz->fd_audio_tick, audio_tick_buf, sizeof(audio_tick_buf));
  if (ret < 0) {
    GST_ERROR_OBJECT (thiz, "Failed to read audio tick");
    return -1;
  }

  fps = (1ULL * GST_SECOND) / render_delay;

  efr_setup->frame_hw_pts = atoi(audio_tick_buf);
  efr_setup->frame_pts += (AMBA_GST_PTS_SW_CLK / fps);

  ret = thiz->iav_ctx->iav_al.f_set_efr_setup(thiz->iav_ctx->iav_fd, efr_setup);
  if (ret < 0) {
    GST_ERROR_OBJECT (thiz, "Failed to set efr setup");
    return -1;
  }

  efr_setup->buf_idx = (raw_buf_idx + 1) % efr_setup->buf_num;

  return 0;
}

static int gst_amba_efr_feed_frame(GstAmbaEfr *thiz, GstBuffer * buffer)
{
  GstBaseSink *base_sink = GST_BASE_SINK(thiz);
  u8 *addr = NULL, *cur_addr = NULL, *gst_buf_addr = NULL;
  u8 *raw_addr = NULL, *hdec_addr = NULL;
  amba_efr_setup_t *efr_setup = &thiz->efr_setup;
  GstMapInfo map;
  GstClockTime start_time = 0, end_time = 0;
  int ret = 0;
  u32 single_size = 0;
  u32 raw_pitch = 0, chroma_height = 0, i = 0, line_size = 0, raw_size = 0;
  u32 hdec_pitch = 0, expected_size = 0;
  u8 raw_buf_idx = 0, is_raw_rgb = 0;

  start_time = gst_clock_get_time (GST_ELEMENT_CLOCK (base_sink));

  memset(&map, 0, sizeof(map));
  if (!gst_buffer_map (buffer, &map, GST_MAP_READ)) {
    GST_ERROR_OBJECT (thiz, "Failed to map buffer");
    return -1;
  }

  raw_buf_idx = efr_setup->buf_idx;
  single_size = gst_amba_efr_get_frame_mem_size(thiz);

  switch (thiz->file_type) {
  case AMBA_GST_EFR_TYPE_RAW_RGB:
    line_size = thiz->width << 1;
    raw_pitch = ROUND_UP(line_size, GST_AMBA_EFR_PITCH_ALIGN);
    raw_size = raw_pitch * thiz->height;
    hdec_pitch = ROUND_UP(line_size >> 2, GST_AMBA_EFR_PITCH_ALIGN);
    expected_size = thiz->width * thiz->height * 2;
    is_raw_rgb = 1;
    break;
  case AMBA_GST_EFR_TYPE_RAW_NV12:
    line_size = thiz->width;
    raw_pitch = ROUND_UP(line_size, GST_AMBA_EFR_PITCH_ALIGN);
    chroma_height = thiz->height / 2;
    expected_size = thiz->width * thiz->height * 3 / 2;
    is_raw_rgb = 0;
    break;
  case AMBA_GST_EFR_TYPE_RAW_YUV:
    line_size = thiz->width;
    raw_pitch = ROUND_UP(line_size, GST_AMBA_EFR_PITCH_ALIGN);
    chroma_height = thiz->height;
    expected_size = thiz->width * thiz->height * 2;
    is_raw_rgb = 0;
    break;
  default :
    GST_ERROR_OBJECT (thiz, "Unsupported file type: %d", thiz->file_type);
    gst_buffer_unmap (buffer, &map);
    return -1;
    break;
  }

  if (expected_size != map.size) {
    GST_ERROR_OBJECT (thiz, "Data size mismatch: expected %u, got %zu", expected_size, map.size);
    gst_buffer_unmap (buffer, &map);
    return -1;
  }

  cur_addr = thiz->iav_partition.virt_addr + raw_buf_idx * single_size;

  if (raw_pitch == line_size) {
    addr = cur_addr;
    memcpy(addr, map.data, map.size);

    // prepare hdec
    if (is_raw_rgb) {
      raw_addr = cur_addr;
      hdec_addr = thiz->iav_partition.virt_addr + raw_buf_idx * single_size + raw_size;
      gst_amba_efr_generate_hdec_raw(thiz, hdec_addr, raw_addr, hdec_pitch, raw_pitch);
    }
  } else {
    if (is_raw_rgb) {
      // copy raw
      for (i = 0; i < thiz->height; ++i) {
        addr = cur_addr + i * raw_pitch;
        gst_buf_addr = map.data + i * line_size;
        memcpy(addr, gst_buf_addr, line_size);
      }

      // prepare hdec
      raw_addr = cur_addr;
      hdec_addr = thiz->iav_partition.virt_addr + raw_buf_idx * single_size + raw_size;
      gst_amba_efr_generate_hdec_raw(thiz, hdec_addr, raw_addr, hdec_pitch, raw_pitch);
    } else {
      // copy luma
      for (i = 0; i < thiz->height; ++i) {
        addr = cur_addr + i * raw_pitch;
        gst_buf_addr = map.data + i * line_size;
        memcpy(addr, gst_buf_addr, line_size);
      }

      // copy chroma
      for (i = 0; i < chroma_height; ++i) {
        addr = cur_addr + (thiz->height + i) * raw_pitch;
        gst_buf_addr = map.data + (thiz->height + i) * line_size;
        memcpy(addr, gst_buf_addr, line_size);
      }
    }
  }

  gst_buffer_unmap (buffer, &map);

  end_time = gst_clock_get_time (GST_ELEMENT_CLOCK (base_sink));
  copy_duration = end_time - start_time;

  ret = do_gst_amba_efr_feed_frame(thiz);
  if (ret < 0) {
    GST_ERROR_OBJECT (thiz, "Failed to do_gst_amba_efr_feed_frame");
    return -1;
  }

  return 0;
}

#elif defined (BUILD_DSP_AMBA_V6)
static int gst_amba_efr_feed_frame(GstAmbaEfr *thiz, GstBuffer * buffer)
{
  GstBaseSink *base_sink = GST_BASE_SINK(thiz);
  u8 *addr = NULL, *cur_addr = NULL, *gst_buf_addr = NULL;
  u8 *raw_addr = NULL, *hdec_addr = NULL;
  amba_efr_setup_t *efr_setup = &thiz->efr_setup;
  GstMapInfo map;
  GstClockTime start_time = 0, end_time = 0;
  gchar audio_tick_buf[32] = {0};
  int ret = 0;
  u32 single_size = 0, single_hdec_size = 0;
  u32 raw_pitch = 0, chroma_height = 0, i = 0, line_size = 0;
  u32 hdec_line_size = 0, hdec_pitch = 0, expected_size = 0;
  u8 raw_buf_idx = 0, is_raw_rgb = 0;

  start_time = gst_clock_get_time (GST_ELEMENT_CLOCK (base_sink));

  memset(&map, 0, sizeof(map));
  if (!gst_buffer_map (buffer, &map, GST_MAP_READ)) {
    GST_ERROR_OBJECT (thiz, "Failed to map buffer");
    return -1;
  }

  raw_buf_idx = efr_setup->buf_idx;

  switch (thiz->file_type) {
  case AMBA_GST_EFR_TYPE_RAW_RGB:
    line_size = thiz->width << 1;
    raw_pitch = ROUND_UP(line_size, GST_AMBA_EFR_PITCH_ALIGN);
    single_size = raw_pitch * thiz->height;
    hdec_line_size = line_size >> 2;
    hdec_pitch = ROUND_UP(hdec_line_size, GST_AMBA_EFR_PITCH_ALIGN);
    single_hdec_size = hdec_pitch * thiz->height;
    expected_size = thiz->width * thiz->height * 2;
    is_raw_rgb = 1;
    break;
  case AMBA_GST_EFR_TYPE_RAW_NV12:
    line_size = thiz->width;
    raw_pitch = ROUND_UP(line_size, GST_AMBA_EFR_PITCH_ALIGN);
    chroma_height = thiz->height / 2;
    single_size = raw_pitch * (thiz->height + chroma_height);
    expected_size = thiz->width * thiz->height * 3 / 2;
    is_raw_rgb = 0;
    break;
  case AMBA_GST_EFR_TYPE_RAW_YUV:
    line_size = thiz->width;
    raw_pitch = ROUND_UP(line_size, GST_AMBA_EFR_PITCH_ALIGN);
    chroma_height = thiz->height;
    single_size = raw_pitch * (thiz->height + chroma_height);
    expected_size = thiz->width * thiz->height * 2;
    is_raw_rgb = 0;
    break;
  default :
    GST_ERROR_OBJECT (thiz, "Unsupported file type: %d", thiz->file_type);
    gst_buffer_unmap (buffer, &map);
    return -1;
    break;
  }

  if (expected_size != map.size) {
    GST_ERROR_OBJECT (thiz, "Data size mismatch: expected %u, got %zu", expected_size, map.size);
    gst_buffer_unmap (buffer, &map);
    return -1;
  }

  cur_addr = thiz->iav_partition.virt_addr + raw_buf_idx * single_size;

  if (raw_pitch == line_size) {
    addr = cur_addr;
    memcpy(addr, map.data, map.size);

    // prepare hdec
    if (is_raw_rgb) {
      raw_addr = cur_addr;
      hdec_addr = thiz->iav_partition.virt_addr + efr_setup->buf_num * single_size +
        raw_buf_idx * single_hdec_size;
      gst_amba_efr_generate_hdec_raw(thiz, hdec_addr, raw_addr, hdec_pitch, raw_pitch);
    }
  } else {
    if (is_raw_rgb) {
      // copy raw
      for (i = 0; i < thiz->height; ++i) {
        addr = cur_addr + i * raw_pitch;
        gst_buf_addr = map.data + i * line_size;
        memcpy(addr, gst_buf_addr, line_size);
      }

      // prepare hdec
      raw_addr = cur_addr;
      hdec_addr = thiz->iav_partition.virt_addr + efr_setup->buf_num * single_size +
        raw_buf_idx * single_hdec_size;
      gst_amba_efr_generate_hdec_raw(thiz, hdec_addr, raw_addr, hdec_pitch, raw_pitch);
    } else {
      // copy luma
      for (i = 0; i < thiz->height; ++i) {
        addr = cur_addr + i * raw_pitch;
        gst_buf_addr = map.data + i * line_size;
        memcpy(addr, gst_buf_addr, line_size);
      }

      // copy chroma
      for (i = 0; i < chroma_height; ++i) {
        addr = cur_addr + (thiz->height + i) * raw_pitch;
        gst_buf_addr = map.data + (thiz->height + i) * line_size;
        memcpy(addr, gst_buf_addr, line_size);
      }
    }
  }

  gst_buffer_unmap (buffer, &map);

  end_time = gst_clock_get_time (GST_ELEMENT_CLOCK (base_sink));
  copy_duration = end_time - start_time;

  ret = read(thiz->fd_audio_tick, audio_tick_buf, sizeof(audio_tick_buf));
  if (ret < 0) {
    GST_ERROR_OBJECT (thiz, "Failed to read audio tick");
    return -1;
  }
  efr_setup->frame_pts = atoi(audio_tick_buf);
  ret = thiz->iav_ctx->iav_al.f_set_efr_setup(thiz->iav_ctx->iav_fd, efr_setup);
  if (ret < 0) {
    GST_ERROR_OBJECT (thiz, "Failed to set efr setup");
    return -1;
  }

  efr_setup->buf_idx = (efr_setup->buf_idx + 1) % efr_setup->buf_num;

  return 0;
}
#endif

static int gst_amba_efr_wait_done(GstAmbaEfr *thiz)
{
  int ret = 0;

  ret = thiz->iav_ctx->iav_al.f_wait_efr_done(thiz->iav_ctx->iav_fd, thiz->vinc_id);
  if (ret < 0) {
    GST_ERROR_OBJECT (thiz, "Failed to wait efr done");
    return -1;
  }

  return 0;
}

#if defined (BUILD_DSP_AMBA_V5)
static int do_gst_amba_efr_run(GstAmbaEfr *thiz, GstBuffer * buffer)
{
  GstBaseSink *base_sink = GST_BASE_SINK(thiz);
  GstClockTime duration = 0;
  guint64 delay = 0;
  gfloat fps = 30.0;  // 30fps by default
  gint ret = 0;

  if (prepare_done == 0) {
    g_object_get (G_OBJECT (base_sink), "render-delay", &delay, NULL);
    render_delay = (delay != 0) ? delay : (1000 / fps) * GST_MSECOND;

    // 0. check EFR parameters
    ret = gst_amba_efr_params_check(thiz);
    if (ret < 0) {
      GST_ERROR_OBJECT (thiz, "Failed to check efr params");
      return -1;
    }

    // 1. allocate EFR memory first
    ret = gst_amba_efr_prepare_memory(thiz);
    if (ret < 0) {
      GST_ERROR_OBJECT (thiz, "Failed to prepare efr memory");
      return -1;
    }

    thiz->fd_audio_tick = open(GST_AMBA_AUDIO_TICK, O_RDONLY);
    if (thiz->fd_audio_tick < 0) {
      GST_ERROR_OBJECT (thiz, "Failed to open %s", GST_AMBA_AUDIO_TICK);
      return -1;
    }

    prepare_done = 1;

    GST_INFO_OBJECT (thiz, "START feeding RAW frames%s",
      thiz->live_mode ? " (live-mode: skip render-delay throttling)" : ":");
  }

  GST_INFO_OBJECT (thiz, "  Feed frame %llu,", count);

  curr_time = gst_clock_get_time (GST_ELEMENT_CLOCK (base_sink));
  duration = curr_time - prev_time;
  if (!thiz->live_mode && render_delay > 0 && (duration + copy_duration) < render_delay) {
    duration = render_delay - (duration + copy_duration);
    g_usleep(duration / 1000);
  }

  ret = gst_amba_efr_feed_frame(thiz, buffer);
  if (ret < 0) {
      GST_ERROR_OBJECT (thiz, "Failed to feed efr frame");
      return -1;
  }

  prev_time = gst_clock_get_time (GST_ELEMENT_CLOCK (base_sink));

  GST_INFO_OBJECT (thiz, "  done");

  GST_INFO_OBJECT (thiz, "  Wait frame %llu", count);
  ret = gst_amba_efr_wait_done(thiz);
  if (ret < 0) {
      GST_ERROR_OBJECT (thiz, "Failed to wait efr done");
      return -1;
  }
  GST_INFO_OBJECT (thiz, "  done");

  count += 1;

  return 0;
}
#elif defined (BUILD_DSP_AMBA_V6)
static int gst_amba_efr_get_vinc_id(GstAmbaEfr *thiz)
{
  amba_dsp_vin_info_t vininfo;
  int ret = 0;
  u8 i = 0;

  if (thiz->vinc_id == 0) {
    for (i = 0; i < thiz->resource_info.channel_num; ++i) {
      memset(&vininfo, 0, sizeof(vininfo));
      vininfo.vsrc_id = thiz->resource_info.vsrc_id[i];
      ret = thiz->iav_ctx->iav_al.f_get_vin_info(thiz->iav_ctx->iav_fd, &vininfo);
      if (ret < 0) {
        GST_ERROR_OBJECT (thiz, "Failed to vin info");
        return -1;
      }

      thiz->vinc_id = vininfo.vinc_id;
    }
  }

  return 0;
}

static int gst_amba_efr_setup_cfg(GstAmbaEfr *thiz)
{
  amba_efr_cfg_t efr_cfg;
  int ret = 0;

  // 1. get efr setup to know cur buf_idx
  thiz->efr_setup.vinc_id = thiz->vinc_id;
  thiz->iav_ctx->iav_al.f_get_efr_setup(thiz->iav_ctx->iav_fd, &thiz->efr_setup);

  // 2. setup efr for the first time
  if (thiz->efr_setup.mem_init_needed) {
    memset(&efr_cfg, 0, sizeof(efr_cfg));
    efr_cfg.vinc_id = thiz->vinc_id;
    efr_cfg.buf_num = GST_EFR_RAW_BUF_NUM;
    efr_cfg.raw_buf_addr = thiz->iav_partition.phys_addr;

    switch (thiz->file_type) {
    case AMBA_GST_EFR_TYPE_RAW_NV12:
    case AMBA_GST_EFR_TYPE_RAW_YUV:
      efr_cfg.raw_pitch = ROUND_UP(thiz->width, GST_AMBA_EFR_PITCH_ALIGN);
      efr_cfg.raw_width = thiz->width;
      efr_cfg.raw_height = thiz->height;
      break;
    case AMBA_GST_EFR_TYPE_RAW_RGB:
      efr_cfg.raw_pitch = ROUND_UP(thiz->width << 1, GST_AMBA_EFR_PITCH_ALIGN);
      efr_cfg.raw_width = thiz->width;
      efr_cfg.raw_height = thiz->height;
      efr_cfg.raw_hdec_pitch = ROUND_UP(efr_cfg.raw_pitch >> 2, GST_AMBA_EFR_PITCH_ALIGN);
      efr_cfg.raw_hdec_width = efr_cfg.raw_width >> 2;
      efr_cfg.raw_hdec_height = thiz->height;
      efr_cfg.raw_hdec_buf_addr = efr_cfg.raw_buf_addr + efr_cfg.raw_pitch * efr_cfg.raw_height * efr_cfg.buf_num;
      break;
    default:
      break;
    }

    ret = thiz->iav_ctx->iav_al.f_set_efr_cfg(thiz->iav_ctx->iav_fd, &efr_cfg);
    if (ret < 0) {
      GST_ERROR_OBJECT (thiz, "Failed to set efr cfg");
      return -1;
    }
  }

  return 0;
}

static int do_gst_amba_efr_run(GstAmbaEfr *thiz, GstBuffer * buffer)
{
  GstBaseSink *base_sink = GST_BASE_SINK(thiz);
  GstClockTime duration = 0;
  guint64 delay = 0;
  gfloat fps = 30.0;  // 30fps by default
  gint ret = 0;

  if (prepare_done == 0) {
    g_object_get (G_OBJECT (base_sink), "render-delay", &delay, NULL);
    render_delay = (delay != 0) ? delay : (1000 / fps) * GST_MSECOND;

    // 0. check EFR parameters
    ret = gst_amba_efr_params_check(thiz);
    if (ret < 0) {
      GST_ERROR_OBJECT (thiz, "Failed to check efr params");
      return -1;
    }

    // 1. allocate EFR memory first
    ret = gst_amba_efr_prepare_memory(thiz);
    if (ret < 0) {
      GST_ERROR_OBJECT (thiz, "Failed to prepare efr memory");
      return -1;
    }

    // 2. get vinc_id
    ret = gst_amba_efr_get_vinc_id(thiz);
    if (ret < 0) {
      GST_ERROR_OBJECT (thiz, "Failed to get vinc id for EFR");
      return -1;
    }

    // 3. setup efr cfg for first time
    ret = gst_amba_efr_setup_cfg(thiz);
    if (ret < 0) {
      GST_ERROR_OBJECT (thiz, "Failed to setup efr cfg");
      return -1;
    }

    thiz->fd_audio_tick = open(GST_AMBA_AUDIO_TICK, O_RDONLY);
    if (thiz->fd_audio_tick < 0) {
      GST_ERROR_OBJECT (thiz, "Failed to open %s", GST_AMBA_AUDIO_TICK);
      return -1;
    }

    prepare_done = 1;

    GST_INFO_OBJECT (thiz, "START feeding RAW frames%s",
      thiz->live_mode ? " (live-mode: skip render-delay throttling)" : ":");
  }

  GST_INFO_OBJECT (thiz, "  Feed frame %llu,", count);

  curr_time = gst_clock_get_time (GST_ELEMENT_CLOCK (base_sink));
  duration = curr_time - prev_time;
  if (!thiz->live_mode && render_delay > 0 && (duration + copy_duration) < render_delay) {
    duration = render_delay - (duration + copy_duration);
    g_usleep(duration / 1000);
  }

  ret = gst_amba_efr_feed_frame(thiz, buffer);
  if (ret < 0) {
      GST_ERROR_OBJECT (thiz, "Failed to feed efr frame");
      return -1;
  }

  prev_time = gst_clock_get_time (GST_ELEMENT_CLOCK (base_sink));

  GST_INFO_OBJECT (thiz, "  done");

  GST_INFO_OBJECT (thiz, "  Wait frame %llu", count);
  ret = gst_amba_efr_wait_done(thiz);
  if (ret < 0) {
      GST_ERROR_OBJECT (thiz, "Failed to wait efr done");
      return -1;
  }
  GST_INFO_OBJECT (thiz, "  done");

  count += 1;

  return 0;
}
#else
static int do_gst_amba_efr_run(GstAmbaEfr *thiz, GstBuffer * buffer)
{
  // do nothing
  return 0;
}
#endif

static int gst_amba_get_iav_ctx(GstAmbaEfr *thiz)
{
  // get iav context
  thiz->iav_ctx = acquire_iav_ctx (1);
  if (!thiz->iav_ctx) {
    GST_ERROR_OBJECT (thiz, "acquire_iav_ctx failed");
    return -1;
  }

  // check iav state
  if (thiz->iav_ctx->iav_al.f_check_iav_state(thiz->iav_ctx->iav_fd, NULL) < 0) {
    GST_ERROR_OBJECT (thiz, "check_iav_state failed");
    return -1;
  }

  memset(&thiz->resource_info, 0, sizeof(thiz->resource_info));
  if (thiz->iav_ctx->iav_al.f_get_resource_info(thiz->iav_ctx->iav_fd, &thiz->resource_info) < 0) {
    GST_ERROR_OBJECT (thiz, "get_resource_info failed");
    return -1;
  }

  return 0;
}

static GstFlowReturn gst_amba_efr_run (GstBaseSink * sink, GstBuffer * buffer)
{
  GstAmbaEfr *thiz = GST_AMBA_EFR (sink);

  if (thiz->file != NULL) {
    if (gst_amba_dump_file(thiz, buffer) < 0) {
      GST_ERROR_OBJECT (thiz, "Failed to dump file");
      return GST_FLOW_ERROR;
    }
  } else {
    if (gst_amba_get_iav_ctx(thiz) < 0) {
      GST_ERROR_OBJECT (thiz, "Failed to get iav ctx");
      return GST_FLOW_ERROR;
    }

    if (do_gst_amba_efr_run(thiz, buffer) < 0) {
      GST_ERROR_OBJECT (thiz, "Failed to do efr");
      return GST_FLOW_ERROR;
    }
  }

  return GST_FLOW_OK;
}
