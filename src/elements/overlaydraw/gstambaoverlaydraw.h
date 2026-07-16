/*
 * gstambaoverlaydraw.h
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
 * SECTION:element-amba_overlay_draw
 * @title: amba_overlay_draw
 * @see_also: amba_draw_data_gen, mlinference2, mlpostprocess
 *
 * Sink: receives draw data from amba_draw_data_gen, draws onto overlay hardware.
 * Input: application/x-amba-draw-data. Downstream: none (sink).
 */

#ifndef __GST_AMBA_OVERLAY_DRAW_H__
#define __GST_AMBA_OVERLAY_DRAW_H__

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>

#include "platform_al.h"
#include "element_common.h"
#include "iav_al.h"
#include "overlay_common.h"

G_BEGIN_DECLS

#define GST_TYPE_AMBA_OVERLAY_DRAW (gst_amba_overlay_draw_get_type())
#define GST_AMBA_OVERLAY_DRAW(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_AMBA_OVERLAY_DRAW, GstAmbaOverlayDraw))
#define GST_AMBA_OVERLAY_DRAW_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_AMBA_OVERLAY_DRAW, GstAmbaOverlayDrawClass))
#define GST_IS_AMBA_OVERLAY_DRAW(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_AMBA_OVERLAY_DRAW))
#define GST_IS_AMBA_OVERLAY_DRAW_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_AMBA_OVERLAY_DRAW))

typedef struct _GstAmbaOverlayDraw GstAmbaOverlayDraw;
typedef struct _GstAmbaOverlayDrawClass GstAmbaOverlayDrawClass;

/* One amba_overlay_draw element drives a single encode stream; osd/pixel/overlay state is not indexed by stream. */
typedef struct {
  gulong osd_offset;
  gulong osd_size;
  int coord_res_w;   /* bbox coordinate resolution (from mlpostprocess coord_res) */
  int coord_res_h;
  int stream_id;     /* IAV stream id for this instance (property stream_id) */
  guint pixel_size;  /* bytes per overlay pixel for this stream (from IAV on V6) */
  gint pixel_fmt;
  iav_set_overlay_t overlay_set;
  stream_param_t stream_params;
  gint fps_n;
  gint fps_d;
  unsigned char sync_with_pts;
  unsigned char insert_always;
  guint32 last_dsp_pts; /* last valid dsp_pts from buffer meta; used for disable apply_frame_sync */
  unsigned int buf_num;  /* Buffer count per area for double/triple buffering (1..OSD_MAX_BUFFER_NUM) */
  guint32 last_draw_hash[MAX_OVERLAY_AREA_NUM];  /* Per-area hash for skip-unchanged (disabled when sync_with_pts) */
  gsize last_draw_size[MAX_OVERLAY_AREA_NUM];
  unsigned int last_draw_area_count;  /* Number of areas last drawn, for skip comparison */
  guint sleep_time_us;  /* Sleep (us) after each buffer when sync_with_pts=FALSE (0=no sleep, reduces multifilesrc read rate) */
  guint refresh_interval; /* 1=every frame; N>1 draw overlay every Nth buffer only */
  guint refresh_frame_index; /* monotonic, reset on sink start */
  iav_ctx_t *iav_ctx;
  /* Multi-input: cache buffers from request pads (sink_1, sink_2, ...) */
  GHashTable *aux_cached_buffers;  /* pad -> GstBuffer* */
  GMutex aux_cache_lock;
} overlay_draw_priv_t;

struct _GstAmbaOverlayDraw {
  GstBin parent;
  overlay_draw_priv_t *priv;
};

struct _GstAmbaOverlayDrawClass {
  GstBinClass parent_class;
};

GType gst_amba_overlay_draw_get_type(void);

G_END_DECLS

#endif /* __GST_AMBA_OVERLAY_DRAW_H__ */
