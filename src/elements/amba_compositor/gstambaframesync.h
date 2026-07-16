/*
 * gstambaframesync.h
 *
 * History:
 *    5/15/2025 - [Da-shun Pei] created file
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
 * @title: gstambaframesync
 * Per-stream bounded queues + frame_id barrier for ambacompositor (GstAggregator).
*/

#ifndef __GST_AMBA_FRAME_SYNC_H__
#define __GST_AMBA_FRAME_SYNC_H__

#include <gst/gst.h>
#include <gst/base/gstaggregator.h>

G_BEGIN_DECLS

typedef struct _GstAmBaFrameSync GstAmBaFrameSync;

typedef gboolean (*GstAmBaFrameSyncGetFrameIdFunc) (GstBuffer * buf,
    guint32 * fid_out, gpointer user_data);

GstAmBaFrameSync *gst_amba_frame_sync_new (guint n_streams, guint max_depth);

void gst_amba_frame_sync_free (GstAmBaFrameSync * sync);

void gst_amba_frame_sync_flush (GstAmBaFrameSync * sync);

void gst_amba_frame_sync_set_max_depth (GstAmBaFrameSync * sync, guint max_depth);

guint gst_amba_frame_sync_get_max_depth (const GstAmBaFrameSync * sync);

/**
 * gst_amba_frame_sync_collect:
 * @sync: sync state
 * @pads: sink aggregator pads, length @n_pads (same order as stream index)
 * @n_pads: must equal sync's n_streams
 * @out_bufs: on GST_FLOW_OK, filled with transferred refs (caller must unref after use)
 * @log_obj: for GST_ERROR_OBJECT (may be NULL)
 * @get_frame_id: reads frame_id from a buffer
 * @user_data: passed to @get_frame_id
 *
 * Drains at most one buffer per stream from aggregator pads into internal queues (bounded).
 * When every queue is non-empty and all heads share the same frame_id, pops one buffer
 * per stream into @out_bufs.
 *
 * Prints both internal queues with g_print() each collect (same key style as nexus
 * displayer_simulation: frame_id*100+buffer_index, with 0,0 printed as 1).
 *
 * Returns GST_AGGREGATOR_FLOW_NEED_DATA if waiting for alignment or more input.
 * Returns GST_FLOW_ERROR on invalid args, parse failure, non-monotonic ids, full queue, or
 * frame_id mismatch while a queue is at max depth.
 */
GstFlowReturn gst_amba_frame_sync_collect (GstAmBaFrameSync * sync,
    GstAggregatorPad ** pads,
    guint n_pads,
    GstBuffer ** out_bufs,
    GstObject * log_obj,
    GstAmBaFrameSyncGetFrameIdFunc get_frame_id,
    gpointer user_data);

G_END_DECLS

#endif /* __GST_AMBA_FRAME_SYNC_H__ */
