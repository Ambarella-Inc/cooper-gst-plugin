/*
 * gstambacamsrc.h
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

#ifndef __GST_AMBA_CAMSRC2_H__
#define __GST_AMBA_CAMSRC2_H__

#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>
#include "element_common.h"

#include "clock_if.h"
#include "gstambahwclock.h"

G_BEGIN_DECLS

#define GST_TYPE_AMBA_CAMSRC2 \
  (gst_amba_camsrc2_get_type())
#define GST_AMBA_CAMSRC2(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AMBA_CAMSRC2,GstAmbaCamsrc2))
#define GST_AMBA_CAMSRC2_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AMBA_CAMSRC2,GstAmbaCamsrc2Class))
#define GST_IS_AMBA_CAMSRC2(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AMBA_CAMSRC2))
#define GST_IS_AMBA_CAMSRC2_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AMBA_CAMSRC2))
#define GST_AMBA_CAMSRC2_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_AMBA_CAMSRC2,GstAmbaCamsrc2Class))

#define ECAPTURE_TYPE_NAME_NONE "none"
#define ECAPTURE_TYPE_NAME_PREVIEW "preview"
#define ECAPTURE_TYPE_NAME_PYRAMID "pyramid"

typedef struct _GstAmbaCamsrc2 GstAmbaCamsrc2;
typedef struct _GstAmbaCamsrc2Class GstAmbaCamsrc2Class;

struct _GstAmbaCamsrc2 {
  GstPushSrc pushsrc;

  /*< private >*/
  gchar str_pixel_format[128];

  guint buf_id;
  guint buf_map;

  guint pyramid_layer_id;
  guint pyramid_buffer_map;
  guint current_channel;
  guint canvas_map_thru_dmabuf;

  guint pitch;
  guint width;
  guint height;

  guint me0_pitch;
  guint me0_width;
  guint me0_height;

  guint me1_pitch;
  guint me1_width;
  guint me1_height;

  /* GDMA dst layout (lcm IAV_DSP_BUF_PITCH_ALIGN & CAVALRY_PORT_PITCH_ALIGN); zero if gdma off */
  guint gdma_yuv_pitch;
  guint gdma_me0_pitch;
  guint gdma_me1_pitch;

  guint framerate_num;
  guint framerate_den;
  guint framerate;

  guint pixel_format_fourcc;

  // clock related
  GstClockTime gst_clok_time;
  guint is_clock_setup;
  guint is_clock_started;
  clock_ctx_t * clock;

  iav_ctx_t * iav_ctx;

  timestamp_info_t timestamp[DAMBA_MAX_YUV_BUF_NUM];
  GstClockTime first_mono_pts;

  unsigned int dump_num;
  char dump_file[DMAX_FILE_NAME_LENGTH + 64];
#ifndef D_OS_AMRTOS
  FILE *dump_fd;
#endif

  amba_resource_info_t res_info;

  //gdma
  amba_gdma_buf_t gdma_ctx[DAMBA_MAX_YUV_BUF_NUM];

  gboolean give_clock;
  GstAmbaHwClock * provided_clock;

  GstClockTime last_pts;

  gchar capture_type_name[128];
  gboolean discard_prev_frame;

  guchar gdma_copy_enable;
  guchar decode_mode;
  guchar capture_type;
  guchar back_pressure_enable;

  guchar is_eos_com;
  guchar capture_me0;
  guchar capture_me1;
  guchar reserved;

  GstAllocator *dmabuf_allocator;

  guint64 total_yuv_query_count;

  /** One-shot g_printerr for width/height vs stride (reset on buf-id / gdma change). */
  gboolean logged_stride_once;

#ifdef DEBUG_USE_FAKE_PTS
  GstClockTimeDiff fake_pts;
  GstClockTimeDiff fake_pts_frame_tick;
#endif
};

struct _GstAmbaCamsrc2Class {
  GstPushSrcClass parent_class;
};

GType gst_amba_camsrc2_get_type (void);

G_END_DECLS

#endif /* __GST_AMBA_CAMSRC2_H__ */

