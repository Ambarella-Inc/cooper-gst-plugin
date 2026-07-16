/*
 * gstambacompositor.h
 *
 * History:
 *    5/1/2025 - [Da-shun Pei] created file
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
 * SECTION: element-ambacompositor
 * @title: ambacompositor
 *
 * GstAggregator: request pads sink_%u (index = dec_id). Synchronizes on frame_id
 * via bounded per-sink queues (gstambaframesync), composites NV12 tiles into one
 * Cavalry-pooled output (output-pitch stride). Inputs: amshmem_src buffers with
 * AmShMem side-data (PHYS_NV12 or POOL_OFFSET_NV12), or plain NV12 with
 * test-mode=TRUE (frame_id from PTS ms). dump-output with optional
 * dump-max-frames appends composed NV12 to dump-output-path for inspection.
 * gst_ambacompositor_do_release() remains an optional downstream hook.
 *
 * trace-frames=TRUE: stderr line per composed frame (frame_id, phys_y, Y samples)
 * to verify inputs/output advance together (GST_DEBUG not required).
 */

#ifndef __GST_ambacompositor_H__
#define __GST_ambacompositor_H__

#include <stdio.h>

#include <gst/gst.h>
#include <gst/base/gstaggregator.h>
#include "dds_msgs/AmShMem_Msg.h"
#include "dds_msgs/FreeFrame_Msg.h"

typedef struct _GstAmBaFrameSync GstAmBaFrameSync;

G_BEGIN_DECLS

#define MAX_DEC_NUM 100

#define GST_TYPE_ambacompositor (gst_ambacompositor_get_type ())
#define GST_ambacompositor(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_ambacompositor, Gstambacompositor))
#define GST_ambacompositor_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_ambacompositor, GstambacompositorClass))
#define GST_IS_ambacompositor(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_ambacompositor))
#define GST_IS_ambacompositor_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_ambacompositor))
#define GST_ambacompositor_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_ambacompositor, GstambacompositorClass))

typedef struct _Gstambacompositor Gstambacompositor;
typedef struct _GstambacompositorClass GstambacompositorClass;

struct _Gstambacompositor {
  GstAggregator parent;

  guint grid_cols;
  guint grid_rows;
  guint output_width;
  guint output_height;
  guint output_pitch;
  gboolean dump_output;
  gchar *dump_output_path;
  guint dump_max_frames;

  guint all_dec_num;
  GstBufferPool *out_pool;
  gsize out_pool_buf_size;
  FILE *dump_fp;
  guint64 dump_frame_seq;

  GstAmBaFrameSync *frame_sync;
  guint sync_queue_depth;

  /* When TRUE: plain NV12 (e.g. filesrc+decode) without AmShMem side-data; frame_id
   * from buffer PTS (ms); validate via GstVideoMeta only. */
  gboolean test_mode;

  gboolean trace_frames;
  guint64 trace_comp_seq;
  guint32 trace_last_fid;
};

struct _GstambacompositorClass {
  GstAggregatorClass parent_class;
};

GType gst_ambacompositor_get_type (void);

void gst_ambacompositor_do_release (Gstambacompositor *self,
    const FreeFrame_Msg *fm);

G_END_DECLS

#endif /* __GST_ambacompositor_H__ */
