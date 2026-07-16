/*
 * gstambaframesync.c
 *
 * History:
 *    4/11/2026 - [Da-Shun Pei] created file
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


#include "gstambaframesync.h"

#include "gstamshmemsrcbufferpool.h"

struct _GstAmBaFrameSync
{
  guint n_streams;
  guint max_depth;
  GQueue *queues;               /* length n_streams */
};

GstAmBaFrameSync *
gst_amba_frame_sync_new (guint n_streams, guint max_depth)
{
  GstAmBaFrameSync *s;

  g_return_val_if_fail (n_streams >= 1, NULL);
  g_return_val_if_fail (max_depth >= 1, NULL);

  s = g_new0 (GstAmBaFrameSync, 1);
  s->n_streams = n_streams;
  s->max_depth = max_depth;
  s->queues = g_new0 (GQueue, n_streams);
  for (guint i = 0; i < n_streams; i++)
    g_queue_init (&s->queues[i]);

  return s;
}

void
gst_amba_frame_sync_flush (GstAmBaFrameSync * sync)
{
  if (!sync)
    return;
  for (guint i = 0; i < sync->n_streams; i++) {
    while (!g_queue_is_empty (&sync->queues[i])) {
      GstBuffer *b = g_queue_pop_head (&sync->queues[i]);
      gst_buffer_unref (b);
    }
  }
}

void
gst_amba_frame_sync_free (GstAmBaFrameSync * sync)
{
  if (!sync)
    return;
  gst_amba_frame_sync_flush (sync);
  for (guint i = 0; i < sync->n_streams; i++)
    g_queue_clear (&sync->queues[i]);     /* release list nodes */
  g_free (sync->queues);
  g_free (sync);
}

void
gst_amba_frame_sync_set_max_depth (GstAmBaFrameSync * sync, guint max_depth)
{
  g_return_if_fail (sync);
  g_return_if_fail (max_depth >= 1);
  sync->max_depth = max_depth;
}

guint
gst_amba_frame_sync_get_max_depth (const GstAmBaFrameSync * sync)
{
  g_return_val_if_fail (sync, 0);
  return sync->max_depth;
}

static gboolean
queue_peek_head_fid (GQueue * q, GstAmBaFrameSyncGetFrameIdFunc get_frame_id,
    gpointer user_data, guint32 * fid)
{
  GstBuffer *b;

  if (g_queue_is_empty (q))
    return FALSE;
  b = g_queue_peek_head (q);
  return get_frame_id (b, fid, user_data);
}

static gboolean
queue_peek_tail_fid (GQueue * q, GstAmBaFrameSyncGetFrameIdFunc get_frame_id,
    gpointer user_data, guint32 * fid)
{
  GstBuffer *b;

  if (g_queue_is_empty (q))
    return FALSE;
  b = g_queue_peek_tail (q);
  return get_frame_id (b, fid, user_data);
}

/* Same list key as nexus displayer_simulation (frame_count*100+buffer_index; 0,0 -> 1). */
static guint64
frame_sync_list_key (GstBuffer * b,
    GstAmBaFrameSyncGetFrameIdFunc get_frame_id, gpointer user_data)
{
  const AmShMem_Msg *m = gst_amshmem_src_buffer_peek_nv12_msg (b);

  if (m) {
    if (m->frame_id == 0 && m->buffer_index == 0)
      return 1;
    return (guint64) m->frame_id * 100u + (guint64) m->buffer_index;
  }
  if (get_frame_id) {
    guint32 fid = 0;

    if (get_frame_id (b, &fid, user_data))
      return (guint64) fid;
  }
  return G_MAXUINT64;
}

G_GNUC_UNUSED static void
frame_sync_debug_dump_queues (GstAmBaFrameSync * sync, guint n_pads,
    GstAmBaFrameSyncGetFrameIdFunc get_frame_id, gpointer user_data)
{
  GString *line;

  line = g_string_new ("[frame sync queues]");
  for (guint u = 0; u < n_pads; u++) {
    GQueue *q = &sync->queues[u];
    guint len = g_queue_get_length (q);

    g_string_append_printf (line, " q%u(len=%u):", u, len);
    if (len == 0) {
      g_string_append (line, " (empty)");
      continue;
    }
    for (guint n = 0; n < len; n++) {
      GstBuffer *b = (GstBuffer *) g_queue_peek_nth (q, n);
      guint64 key = frame_sync_list_key (b, get_frame_id, user_data);

      if (key == G_MAXUINT64)
        g_string_append_printf (line, n == 0 ? " ?" : "-->?");
      else
        g_string_append_printf (line, n == 0 ? " %" G_GUINT64_FORMAT
            : "-->%" G_GUINT64_FORMAT, key);
    }
    g_string_append (line, "-->NULL");
  }
  g_print ("%s\n", line->str);
  g_string_free (line, TRUE);
}

GstFlowReturn
gst_amba_frame_sync_collect (GstAmBaFrameSync * sync,
    GstAggregatorPad ** pads,
    guint n_pads,
    GstBuffer ** out_bufs,
    GstObject * log_obj,
    GstAmBaFrameSyncGetFrameIdFunc get_frame_id,
    gpointer user_data)
{
  guint32 f0, fi;
  gboolean mismatch;
  gboolean any_full = FALSE;

  g_return_val_if_fail (sync, GST_FLOW_ERROR);
  g_return_val_if_fail (pads, GST_FLOW_ERROR);
  g_return_val_if_fail (out_bufs, GST_FLOW_ERROR);
  g_return_val_if_fail (get_frame_id, GST_FLOW_ERROR);
  g_return_val_if_fail (n_pads == sync->n_streams, GST_FLOW_ERROR);

  /* Pull at most one buffer per stream per collect. Draining pad0 completely
   * before touching pad1 allowed queue0 to run one frame_id ahead (e.g. head
   * fid 1 vs 0 on queue1), which can never match with monotonic per-stream ids. */
  for (guint i = 0; i < n_pads; i++) {
    guint32 new_fid;

    if (!gst_aggregator_pad_peek_buffer (pads[i]))
      continue;

    if (g_queue_get_length (&sync->queues[i]) >= sync->max_depth) {
      if (log_obj) {
        GST_ERROR_OBJECT (log_obj,
            "frame sync: queue full on stream %u (max-depth %u)", i,
            sync->max_depth);
      }
      return GST_FLOW_ERROR;
    }

    {
      GstBuffer *b = gst_aggregator_pad_pop_buffer (pads[i]);
      if (!b)
        continue;
      if (!get_frame_id (b, &new_fid, user_data)) {
        if (log_obj)
          GST_ERROR_OBJECT (log_obj,
              "frame sync: could not read frame_id on stream %u", i);
        gst_buffer_unref (b);
        return GST_FLOW_ERROR;
      }
      if (!g_queue_is_empty (&sync->queues[i])) {
        guint32 prev_fid;
        if (!queue_peek_tail_fid (&sync->queues[i], get_frame_id, user_data,
                &prev_fid)) {
          gst_buffer_unref (b);
          return GST_FLOW_ERROR;
        }
        if (new_fid <= prev_fid) {
          if (log_obj) {
            GST_ERROR_OBJECT (log_obj,
                "frame sync: non-monotonic frame_id on stream %u (%u -> %u)",
                i, prev_fid, new_fid);
          }
          gst_buffer_unref (b);
          return GST_FLOW_ERROR;
        }
      }
      g_queue_push_tail (&sync->queues[i], b);
    }
  }

  // print the queues
  // frame_sync_debug_dump_queues (sync, n_pads, get_frame_id, user_data);

  for (guint i = 0; i < n_pads; i++) {
    if (g_queue_is_empty (&sync->queues[i]))
      return GST_AGGREGATOR_FLOW_NEED_DATA;
  }

  if (!queue_peek_head_fid (&sync->queues[0], get_frame_id, user_data, &f0))
    return GST_FLOW_ERROR;

  mismatch = FALSE;
  for (guint i = 1; i < n_pads; i++) {
    if (!queue_peek_head_fid (&sync->queues[i], get_frame_id, user_data, &fi))
      return GST_FLOW_ERROR;
    if (fi != f0) {
      mismatch = TRUE;
      break;
    }
  }

  /* Barrier: pop one buffer from every queue only when all peek-head frame_ids
   * match (compositor unrefs -> FreeFrame). */
  if (mismatch) {
    for (guint i = 0; i < n_pads; i++) {
      if (g_queue_get_length (&sync->queues[i]) >= sync->max_depth)
        any_full = TRUE;
    }
    if (any_full) {
      if (log_obj) {
        GString *lens = g_string_new ("frame sync: heads mismatch, queue lens:");
        for (guint i = 0; i < n_pads; i++)
          g_string_append_printf (lens, " %u=%u", i,
              (unsigned) g_queue_get_length (&sync->queues[i]));
        g_string_append_printf (lens, " (max-depth %u)", sync->max_depth);
        GST_ERROR_OBJECT (log_obj, "%s", lens->str);
        g_string_free (lens, TRUE);
        GST_ERROR_OBJECT (log_obj,
            "frame sync: frame_id mismatch while queue full (cannot wait further)");
      }
      return GST_FLOW_ERROR;
    }
    return GST_AGGREGATOR_FLOW_NEED_DATA;
  }

  for (guint i = 0; i < n_pads; i++) {
    out_bufs[i] = g_queue_pop_head (&sync->queues[i]);
    if (!out_bufs[i])
      return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}
