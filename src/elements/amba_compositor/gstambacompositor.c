/*
 * gstambacompositor.c
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

/**
 * SECTION: element-ambacompositor
 * @title: ambacompositor
 *
 * |[
 * gst-launch-1.0 -e \
 *   ambacompositor name=comp grid-cols=1 grid-rows=1 \
 *     output-width=768 output-height=432 output-pitch=896 \
 *     dump-output=true dump-output-path=/tmp/comp.nv12 dump-max-frames=10 \
 *   amshmem_src implem-method=cyclonedds ! \
 *     video/x-raw,format=NV12,width=768,height=432 ! queue ! comp.sink_0 \
 *   comp.src ! queue ! fakesink sync=false
 * ]|
 *
 * Request pads: sink_0 .. sink_(grid-cols*grid-rows - 1). Pad index must match
 * AmShMem_Msg dec_id. Input: amshmem_src NV12 with side-data (PHYS_NV12 or
 * POOL_OFFSET_NV12). Output: video/x-raw NV12 (Cavalry pool, output-pitch stride).
 *
 * test-mode=TRUE: NV12 without AmShMem meta (e.g. filesrc+decode). frame_id is
 * taken from PTS in milliseconds; each buffer needs a valid, increasing PTS.
 *
 * trace-frames=TRUE: stderr trace per composed frame (after blit): sync_fid,
 * per-input fid/buffer_index/phys_y and Y luminance samples, output Y samples.
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdarg.h>

/* Unconditional trace to stderr (flushed). GST_DEBUG/GST_DEBUG_FILE need env;
 * use this when diagnosing SIGSEGV before any debug category prints. */
static void
gst_ambacompositor_ftrace (const char *fmt, ...)
{
  va_list ap;

  fprintf (stderr, "[ambacompositor] ");
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fprintf (stderr, "\n");
  fflush (stderr);
}

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/base/gstaggregator.h>

#include "gstambacompositor.h"
#include "gstambaframesync.h"
#include "gstamshmemtypes.h"
#include "gstamshmemsrcbufferpool.h"
#include "gstamshmemcommonslot.h"
#include "gst_amba_cavalry_bufferpool.h"
#include "gst_amba_cavalry_allocator.h"
#include "gst_amba_pitch_align.h"

GST_DEBUG_CATEGORY_STATIC (gst_ambacompositor_debug);
#define GST_CAT_DEFAULT gst_ambacompositor_debug

/* Match hwvdecv2 Cavalry pool slab policy */
#define GST_AMBACOMPOSITOR_CAVALRY_POOL_CONTIGUOUS TRUE

#define DEFAULT_GRID_COLS        1
#define DEFAULT_GRID_ROWS        1
#define DEFAULT_OUTPUT_WIDTH     1920
#define DEFAULT_OUTPUT_HEIGHT    1080
/* Cavalry: Y stride 128 * odd (here 1920 = 128 * 15 for default 1920-wide out). */
#define DEFAULT_OUTPUT_PITCH     1920
#define DEFAULT_DUMP_OUTPUT      FALSE
#define DEFAULT_DUMP_OUTPUT_PATH "/tmp/ambacompositor_dump.nv12"
/* Two independent decoders often skew by a few frames; depth 16 fills fast. */
#define DEFAULT_SYNC_QUEUE_DEPTH 32
#define DEFAULT_DUMP_MAX_FRAMES  0
#define DEFAULT_TEST_MODE        FALSE
#define DEFAULT_TRACE_FRAMES     FALSE
#define AMBACOMP_TRACE_FID_NONE  ((guint32) 0xffffffffu)

enum {
  PROP_0,
  PROP_GRID_COLS,
  PROP_GRID_ROWS,
  PROP_OUTPUT_WIDTH,
  PROP_OUTPUT_HEIGHT,
  PROP_OUTPUT_PITCH,
  PROP_SYNC_QUEUE_DEPTH,
  PROP_DUMP_OUTPUT,
  PROP_DUMP_OUTPUT_PATH,
  PROP_DUMP_MAX_FRAMES,
  PROP_TEST_MODE,
  PROP_TRACE_FRAMES,
  PROP_LAST
};

static GstPadTemplate *sink_templ = NULL;
static GstPadTemplate *src_templ = NULL;

#define gst_ambacompositor_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (Gstambacompositor, gst_ambacompositor, GST_TYPE_AGGREGATOR,
    GST_DEBUG_CATEGORY_INIT (gst_ambacompositor_debug, "ambacompositor", 0,
        "AmShMem NV12 grid compositor (GstAggregator)"));

static void gst_ambacompositor_finalize (GObject *object);
static void gst_ambacompositor_set_property (GObject *object, guint prop_id,
    const GValue *value, GParamSpec *pspec);
static void gst_ambacompositor_get_property (GObject *object, guint prop_id,
    GValue *value, GParamSpec *pspec);

static gboolean gst_ambacompositor_start (GstAggregator *agg);
static gboolean gst_ambacompositor_stop (GstAggregator *agg);
static GstFlowReturn gst_ambacompositor_aggregate (GstAggregator *agg,
    gboolean timeout);
static GstAggregatorPad *gst_ambacompositor_create_new_pad (GstAggregator *agg,
    GstPadTemplate *templ, const gchar *req_name, const GstCaps *caps);
static GstFlowReturn gst_ambacompositor_update_src_caps (GstAggregator *agg,
    GstCaps *caps, GstCaps **ret);

static gboolean gst_ambacompositor_validate_geometry (Gstambacompositor *self);
static void gst_ambacompositor_destroy_out_pool (Gstambacompositor *self);
static gboolean gst_ambacompositor_create_out_pool (Gstambacompositor *self);
static GstFlowReturn gst_ambacompositor_collect_sink_pads (Gstambacompositor *self,
    GstAggregatorPad **pads_out);
static gboolean gst_ambacompositor_buffer_get_frame_id (Gstambacompositor *self,
    GstBuffer *buf, guint32 *fid);
static gboolean gst_ambacompositor_sync_get_frame_id (GstBuffer *buf,
    guint32 *fid, gpointer user_data);
static gboolean gst_ambacompositor_validate_all_inputs (Gstambacompositor *self,
    GstBuffer **bufs);
static void gst_ambacompositor_blit_nv12 (guint8 *dst, guint dpitch, guint al_oh,
    guint dx, guint dy, guint tw, guint th, const guint8 *sy, const guint8 *suv,
    guint spitch);
static GstFlowReturn gst_ambacompositor_do_composite_blit (Gstambacompositor *self,
    GstBuffer **inbufs, GstBuffer *outbuf);
static void gst_ambacompositor_trace_frame_bundle (Gstambacompositor *self,
    GstBuffer **inbufs, GstBuffer *outbuf, guint32 sync_fid);

static gboolean
geometry_ok_when_ready (Gstambacompositor *self)
{
  GstState state, pending;
  gst_element_get_state (GST_ELEMENT (self), &state, &pending, 0);
  return (state <= GST_STATE_READY && pending == GST_STATE_VOID_PENDING);
}

static gboolean
gst_ambacompositor_validate_geometry (Gstambacompositor *self)
{
  guint tile_w, tile_h;

  if (self->grid_cols < 1 || self->grid_rows < 1) {
    GST_ERROR_OBJECT (self, "grid must be at least 1x1");
    return FALSE;
  }
  self->all_dec_num = self->grid_cols * self->grid_rows;
  if (self->all_dec_num > MAX_DEC_NUM) {
    GST_ERROR_OBJECT (self, "grid %ux%u exceeds MAX_DEC_NUM %u",
        self->grid_cols, self->grid_rows, MAX_DEC_NUM);
    return FALSE;
  }
  if (self->output_width % self->grid_cols != 0
      || self->output_height % self->grid_rows != 0) {
    GST_ERROR_OBJECT (self,
        "output %ux%u not divisible by grid %ux%u",
        self->output_width, self->output_height, self->grid_cols, self->grid_rows);
    return FALSE;
  }
  tile_w = self->output_width / self->grid_cols;
  tile_h = self->output_height / self->grid_rows;
  if (self->output_pitch < self->output_width) {
    GST_ERROR_OBJECT (self, "output-pitch %u < output-width %u",
        self->output_pitch, self->output_width);
    return FALSE;
  }
  if (!AMBA_ALIGN_IS_ODD_DSP_PITCH (self->output_pitch)) {
    GST_ERROR_OBJECT (self,
        "output-pitch %u must be 128 times an odd integer (Cavalry allocator)",
        self->output_pitch);
    return FALSE;
  }
  GST_DEBUG_OBJECT (self, "tile %ux%u all_dec_num=%u pitch=%u", tile_w, tile_h,
      self->all_dec_num, self->output_pitch);
  return TRUE;
}

static void
gst_ambacompositor_destroy_out_pool (Gstambacompositor *self)
{
  if (self->out_pool) {
    gst_buffer_pool_set_active (self->out_pool, FALSE);
    gst_object_unref (self->out_pool);
    self->out_pool = NULL;
  }
  self->out_pool_buf_size = 0;
}

static gboolean
gst_ambacompositor_create_out_pool (Gstambacompositor *self)
{
  GstCaps *pool_caps;
  GstBufferPool *pool;
  GstStructure *config;
  gsize size;
  GstAllocator *alloc;
  guint w = self->output_width;
  guint h = self->output_height;
  guint al_h = (h + 15) & ~15u;

  GST_DEBUG_OBJECT (self, "[comp] create_out_pool: begin %ux%u pitch=%u al_h=%u",
      w, h, self->output_pitch, al_h);
  gst_ambacompositor_ftrace ("create_out_pool begin %ux%u pitch=%u al_h=%u",
      w, h, self->output_pitch, al_h);

  gst_ambacompositor_destroy_out_pool (self);

  size = (gsize) self->output_pitch * (gsize) al_h * 3 / 2;
  pool_caps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, (gint) w,
      "height", G_TYPE_INT, (gint) h,
      NULL);
  if (!pool_caps)
    return FALSE;

  pool = gst_amba_cavalry_buffer_pool_new ();
  GST_DEBUG_OBJECT (self, "[comp] create_out_pool: cavalry pool new -> %p", (void *) pool);
  gst_amba_cavalry_buffer_pool_set_contiguous_memory (
      GST_AMBA_CAVALRY_BUFFER_POOL (pool), GST_AMBACOMPOSITOR_CAVALRY_POOL_CONTIGUOUS);

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, pool_caps, size,
      GST_AMSHMEM_POOL_MAX_BUFFERS, GST_AMSHMEM_POOL_MAX_BUFFERS);
  gst_caps_unref (pool_caps);

  gst_amba_cavalry_allocator_init_once ();
  GST_DEBUG_OBJECT (self, "[comp] create_out_pool: allocator_init_once done");
  alloc = gst_amba_cavalry_allocator_get ();
  if (!alloc) {
    GST_ERROR_OBJECT (self, "gst_amba_cavalry_allocator_get failed");
    gst_object_unref (pool);
    return FALSE;
  }
  gst_buffer_pool_config_set_allocator (config, alloc, NULL);
  gst_object_unref (alloc);

  GST_DEBUG_OBJECT (self, "[comp] create_out_pool: set_config ...");
  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (self, "cavalry buffer pool set_config failed");
    gst_object_unref (pool);
    return FALSE;
  }

  GST_DEBUG_OBJECT (self, "[comp] create_out_pool: set_active(TRUE) ...");
  gst_ambacompositor_ftrace ("create_out_pool: about to set_active(TRUE) pool=%p",
      (void *) pool);
  if (!gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (self, "failed to activate compositor output pool");
    gst_object_unref (pool);
    return FALSE;
  }

  self->out_pool = pool;
  self->out_pool_buf_size = size;
  GST_DEBUG_OBJECT (self, "[comp] create_out_pool: done active pool");
  gst_ambacompositor_ftrace ("create_out_pool: set_active(TRUE) ok");
  GST_INFO_OBJECT (self,
      "output pool NV12 %ux%u pitch=%u al_h=%u size=%" G_GSIZE_FORMAT " buffers=%d",
      w, h, self->output_pitch, al_h, size, GST_AMSHMEM_POOL_MAX_BUFFERS);
  return TRUE;
}

static GstFlowReturn
gst_ambacompositor_collect_sink_pads (Gstambacompositor *self,
    GstAggregatorPad **pads_out)
{
  GList *l;

  memset (pads_out, 0, sizeof (GstAggregatorPad *) * self->all_dec_num);
  for (l = GST_ELEMENT (self)->sinkpads; l; l = l->next) {
    GstPad *p = GST_PAD (l->data);
    const gchar *n;
    guint idx;

    n = GST_OBJECT_NAME (p);
    if (!g_str_has_prefix (n, "sink_"))
      continue;
    idx = (guint) g_ascii_strtoull (n + 5, NULL, 10);
    if (idx >= self->all_dec_num) {
      GST_ERROR_OBJECT (self, "sink index %u >= all_dec_num %u", idx,
          self->all_dec_num);
      return GST_FLOW_ERROR;
    }
    if (pads_out[idx]) {
      GST_ERROR_OBJECT (self, "duplicate sink index %u", idx);
      return GST_FLOW_ERROR;
    }
    pads_out[idx] = GST_AGGREGATOR_PAD (p);
  }

  for (guint i = 0; i < self->all_dec_num; i++) {
    if (!pads_out[i]) {
      GST_ERROR_OBJECT (self, "missing sink_%u (need %u pads)", i,
          self->all_dec_num);
      return GST_FLOW_ERROR;
    }
  }
  return GST_FLOW_OK;
}

static gboolean
gst_ambacompositor_buffer_get_frame_id (Gstambacompositor *self,
    GstBuffer *buf, guint32 *fid)
{
  if (self->test_mode) {
    if (!GST_BUFFER_PTS_IS_VALID (buf)) {
      GST_ERROR_OBJECT (self,
          "test-mode: buffer has no PTS (need valid PTS for sync frame id)");
      return FALSE;
    }
    /* Monotonic ms timeline for decoder output; avoids AmShMem frame_id meta. */
    *fid = (guint32) GST_TIME_AS_MSECONDS (GST_BUFFER_PTS (buf));
    return TRUE;
  }
  return gst_amshmem_src_buffer_peek_nv12_frame_id (buf, fid);
}

static gboolean
gst_ambacompositor_sync_get_frame_id (GstBuffer *buf, guint32 *fid,
    gpointer user_data)
{
  Gstambacompositor *self = GST_ambacompositor (user_data);

  g_return_val_if_fail (self != NULL, FALSE);
  return gst_ambacompositor_buffer_get_frame_id (self, buf, fid);
}

static gboolean
gst_ambacompositor_validate_all_inputs (Gstambacompositor *self, GstBuffer **bufs)
{
  guint tile_w = self->output_width / self->grid_cols;
  guint tile_h = self->output_height / self->grid_rows;
  guint32 expect_pitch = 0;

  if (self->test_mode) {
    guint32 tpm = 0;
    for (guint i = 0; i < self->all_dec_num; i++) {
      GstVideoMeta *vm;

      vm = gst_buffer_get_video_meta (bufs[i]);
      if (!vm || vm->format != GST_VIDEO_FORMAT_NV12) {
        GST_ERROR_OBJECT (self, "sink_%u: test-mode needs NV12 GstVideoMeta", i);
        return FALSE;
      }
      if ((guint) vm->width != tile_w || (guint) vm->height != tile_h) {
        GST_ERROR_OBJECT (self,
            "sink_%u: test-mode video %ux%u != tile %ux%u", i, vm->width,
            vm->height, tile_w, tile_h);
        return FALSE;
      }
      if (i == 0)
        tpm = (guint32) vm->stride[0];
      else if ((guint32) vm->stride[0] != tpm) {
        GST_ERROR_OBJECT (self, "sink_%u: pitch %d != %u", i, vm->stride[0],
            tpm);
        return FALSE;
      }
    }
    return TRUE;
  }

  for (guint i = 0; i < self->all_dec_num; i++) {
    const AmShMem_Msg *msg = gst_amshmem_src_buffer_peek_nv12_msg (bufs[i]);
    GstVideoMeta *vm;

    if (!msg) {
      GST_ERROR_OBJECT (self,
          "sink_%u: missing AmShMem side-data (amshmem_src NV12 with attach_nv12_side)",
          i);
      return FALSE;
    }

    if (msg->dec_id != (uint8_t) i) {
      GST_ERROR_OBJECT (self, "sink_%u: dec_id %u mismatch", i, msg->dec_id);
      return FALSE;
    }
    if (msg->data_format != AM_SHMEM_DATA_FORMAT_POOL_OFFSET_NV12
        && msg->data_format != AM_SHMEM_DATA_FORMAT_PHYS_NV12) {
      GST_ERROR_OBJECT (self, "sink_%u: need PHYS_NV12 or POOL_OFFSET_NV12", i);
      return FALSE;
    }
    if (msg->width != tile_w || msg->height != tile_h) {
      GST_ERROR_OBJECT (self,
          "sink_%u: AmShMem %ux%u != tile %ux%u", i, msg->width, msg->height,
          tile_w, tile_h);
      return FALSE;
    }
    if (i == 0)
      expect_pitch = msg->pitch;
    else if (msg->pitch != expect_pitch) {
      GST_ERROR_OBJECT (self, "sink_%u: pitch %u != %u", i, msg->pitch,
          expect_pitch);
      return FALSE;
    }

    vm = gst_buffer_get_video_meta (bufs[i]);
    if (!vm || vm->format != GST_VIDEO_FORMAT_NV12) {
      GST_ERROR_OBJECT (self, "sink_%u: missing NV12 GstVideoMeta", i);
      return FALSE;
    }
    if ((guint) vm->width != tile_w || (guint) vm->height != tile_h
        || (guint) vm->stride[0] != msg->pitch) {
      GST_ERROR_OBJECT (self, "sink_%u: video meta mismatch (meta %dx%d stride %d)",
          i, vm->width, vm->height, vm->stride[0]);
      return FALSE;
    }
  }
  return TRUE;
}

static void
gst_ambacompositor_blit_nv12 (guint8 *dst, guint dpitch, guint al_oh,
    guint dx, guint dy, guint tw, guint th, const guint8 *sy, const guint8 *suv,
    guint spitch)
{
  guint y;
  guint8 *dy0 = dst + dy * dpitch + dx;

  for (y = 0; y < th; y++)
    memcpy (dy0 + y * dpitch, sy + y * spitch, tw);

  {
    guint8 *duv = dst + dpitch * al_oh + (dy / 2) * dpitch + dx;
    for (y = 0; y < th / 2; y++)
      memcpy (duv + y * dpitch, suv + y * spitch, tw);
  }
}

static GstFlowReturn
gst_ambacompositor_do_composite_blit (Gstambacompositor *self,
    GstBuffer **inbufs, GstBuffer *outbuf)
{
  GstMapInfo outmap;
  GstVideoMeta *outvm;
  guint tile_w = self->output_width / self->grid_cols;
  guint tile_h = self->output_height / self->grid_rows;
  guint op = self->output_pitch;
  guint al_oh = (self->output_height + 15) & ~15u;

  if (!gst_buffer_map (outbuf, &outmap, GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (self, "output map failed");
    return GST_FLOW_ERROR;
  }

  outvm = gst_buffer_get_video_meta (outbuf);
  if (!outvm || outvm->format != GST_VIDEO_FORMAT_NV12) {
    GST_ERROR_OBJECT (self, "output missing NV12 GstVideoMeta");
    gst_buffer_unmap (outbuf, &outmap);
    return GST_FLOW_ERROR;
  }

  memset (outmap.data, 0, op * al_oh);
  memset (outmap.data + op * al_oh, 0x80, op * al_oh / 2);

  for (guint i = 0; i < self->all_dec_num; i++) {
    GstBuffer *ib = inbufs[i];
    GstVideoMeta *ivm = gst_buffer_get_video_meta (ib);
    GstMapInfo imap;
    const guint8 *sy, *suv;
    guint col = i % self->grid_cols;
    guint row = i / self->grid_cols;
    guint dx = col * tile_w;
    guint dy = row * tile_h;

    if (!gst_buffer_map (ib, &imap, GST_MAP_READ)) {
      GST_ERROR_OBJECT (self, "sink_%u map failed", i);
      gst_buffer_unmap (outbuf, &outmap);
      return GST_FLOW_ERROR;
    }

    sy = imap.data + ivm->offset[0];
    suv = imap.data + ivm->offset[1];
    gst_ambacompositor_blit_nv12 (outmap.data, op, al_oh, dx, dy, tile_w, tile_h,
        sy, suv, (guint) ivm->stride[0]);
    gst_buffer_unmap (ib, &imap);
  }

  gst_buffer_unmap (outbuf, &outmap);
  return GST_FLOW_OK;
}

/* After blit: log frame_id / PA / Y luminance samples so a moving scene should
 * change Y[0] and Y[mid] every frame when sync_fid steps by 1. */
static void
gst_ambacompositor_trace_frame_bundle (Gstambacompositor *self,
    GstBuffer **inbufs, GstBuffer *outbuf, guint32 sync_fid)
{
  GString *line;
  GstMapInfo omap;
  guint o0 = 0, omid = 0;

  line = g_string_new ("trace-frame");

  g_string_append_printf (line, " comp#%" G_GUINT64_FORMAT " sync_fid=%" G_GUINT32_FORMAT,
      self->trace_comp_seq, sync_fid);

  if (self->trace_last_fid != AMBACOMP_TRACE_FID_NONE) {
    gint df = (gint) sync_fid - (gint) self->trace_last_fid;

    g_string_append_printf (line, " delta_fid=%d", df);
    if (df != 1)
      g_string_append (line, " (expect 1 if decoders stay locked)");
  }
  self->trace_last_fid = sync_fid;
  self->trace_comp_seq++;

  for (guint i = 0; i < self->all_dec_num; i++) {
    GstBuffer *ib = inbufs[i];
    GstVideoMeta *vm = gst_buffer_get_video_meta (ib);
    GstMapInfo imap;
    guint y0 = 0, ymid = 0;

    if (vm && gst_buffer_map (ib, &imap, GST_MAP_READ)) {
      if (vm->n_planes >= 1) {
        gsize off0 = vm->offset[0];
        guint pitch = (guint) vm->stride[0];
        guint tw = vm->width, th = vm->height;

        y0 = imap.data[off0];
        if (tw > 0 && th > 0 && pitch > 0)
          ymid = imap.data[off0 + (gsize) (th / 2) * pitch + (tw / 2)];
      }
      gst_buffer_unmap (ib, &imap);
    }

    if (!self->test_mode) {
      const AmShMem_Msg *m = gst_amshmem_src_buffer_peek_nv12_msg (ib);

      if (m) {
        g_string_append_printf (line,
            " | in%u dec=%u fid=%u idx=%u py=0x%" PRIx64 " Y00=%u Ymid=%u",
            i, (guint) m->dec_id, m->frame_id, m->buffer_index,
            (guint64) m->phys_y_addr, y0, ymid);
      } else {
        g_string_append_printf (line, " | in%u (no AmShMem side) Y00=%u Ymid=%u",
            i, y0, ymid);
      }
    } else {
      guint32 tf = 0;

      if (gst_ambacompositor_buffer_get_frame_id (self, ib, &tf)) {
        g_string_append_printf (line, " | in%u pts_fid=%u Y00=%u Ymid=%u",
            i, tf, y0, ymid);
      }
    }
  }

  if (outbuf && gst_buffer_get_video_meta (outbuf)
      && gst_buffer_map (outbuf, &omap, GST_MAP_READ)) {
    GstVideoMeta *ovm = gst_buffer_get_video_meta (outbuf);

    if (ovm->n_planes >= 1) {
      gsize ooff = ovm->offset[0];
      guint op = (guint) ovm->stride[0];
      guint ow = ovm->width, oh = ovm->height;

      o0 = omap.data[ooff];
      if (ow > 0 && oh > 0 && op > 0)
        omid = omap.data[ooff + (gsize) (oh / 2) * op + (ow / 2)];
    }
    gst_buffer_unmap (outbuf, &omap);
  }
  g_string_append_printf (line, " | OUT Y00=%u Yctr=%u buf=%p", o0, omid,
      (void *) outbuf);

  gst_ambacompositor_ftrace ("%s", line->str);
  g_string_free (line, TRUE);
}

static void
gst_ambacompositor_class_init (GstambacompositorClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstAggregatorClass *agg_class = GST_AGGREGATOR_CLASS (klass);
  GstCaps *sink_caps;
  GstCaps *src_caps;

  gst_ambacompositor_ftrace ("class_init (GstAggregator subclass setup)");

  gobject_class->finalize = gst_ambacompositor_finalize;
  gobject_class->set_property = gst_ambacompositor_set_property;
  gobject_class->get_property = gst_ambacompositor_get_property;

  g_object_class_install_property (gobject_class, PROP_GRID_COLS,
      g_param_spec_uint ("grid-cols", "Grid columns",
          "Number of columns (all_dec_num = grid-cols * grid-rows)", 1,
          MAX_DEC_NUM, DEFAULT_GRID_COLS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_GRID_ROWS,
      g_param_spec_uint ("grid-rows", "Grid rows",
          "Number of rows", 1, MAX_DEC_NUM, DEFAULT_GRID_ROWS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_OUTPUT_WIDTH,
      g_param_spec_uint ("output-width", "Output width",
          "Composed NV12 width in pixels", 16, 8192, DEFAULT_OUTPUT_WIDTH,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_OUTPUT_HEIGHT,
      g_param_spec_uint ("output-height", "Output height",
          "Composed NV12 height in pixels", 16, 8192, DEFAULT_OUTPUT_HEIGHT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_OUTPUT_PITCH,
      g_param_spec_uint ("output-pitch", "Output Y stride",
          "Y/UV row stride in bytes (>= output-width, 128 * odd for Cavalry pool)",
          128, G_MAXUINT32 / 4, DEFAULT_OUTPUT_PITCH,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SYNC_QUEUE_DEPTH,
      g_param_spec_uint ("sync-queue-depth", "Frame sync queue depth",
          "Max buffers per sink held while waiting for matching frame_id (no drop until full)",
          1, 64, DEFAULT_SYNC_QUEUE_DEPTH,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DUMP_OUTPUT,
      g_param_spec_boolean ("dump-output", "Dump composed NV12",
          "When TRUE, write each composed frame to dump-output-path", DEFAULT_DUMP_OUTPUT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DUMP_OUTPUT_PATH,
      g_param_spec_string ("dump-output-path", "Dump file path",
          "Path for raw NV12 dump when dump-output is TRUE",
          DEFAULT_DUMP_OUTPUT_PATH,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DUMP_MAX_FRAMES,
      g_param_spec_uint ("dump-max-frames", "Max dump frames",
          "When dump-output is TRUE, append at most this many composed frames "
          "(0 = no limit)", 0, 4096, DEFAULT_DUMP_MAX_FRAMES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TEST_MODE,
      g_param_spec_boolean ("test-mode", "Test mode",
          "TRUE: plain NV12 (e.g. filesrc/dec) without AmShMem side-data; sync uses "
          "PTS in milliseconds as frame id (buffers need valid PTS). "
          "FALSE: AmShMem production path.",
          DEFAULT_TEST_MODE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TRACE_FRAMES,
      g_param_spec_boolean ("trace-frames", "Trace composed frames",
          "When TRUE, print one stderr line per output frame: sync frame_id, per-input "
          "AmShMem fid/buffer_index/phys_y and Y luminance samples, plus output Y "
          "samples (useful to confirm no stale buffer reuse).",
          DEFAULT_TRACE_FRAMES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  sink_caps = gst_caps_from_string (
      "video/x-raw,format=(string)NV12,width=(int)[1,8192],height=(int)[1,8192]");
  sink_templ = gst_pad_template_new ("sink_%u", GST_PAD_SINK, GST_PAD_REQUEST,
      sink_caps);
  gst_caps_unref (sink_caps);
  gst_element_class_add_pad_template (element_class, sink_templ);

  src_caps = gst_caps_from_string (
      "video/x-raw,format=(string)NV12,width=(int)[1,8192],height=(int)[1,8192]");
  src_templ = gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, src_caps);
  gst_caps_unref (src_caps);
  gst_element_class_add_pad_template (element_class, src_templ);

  gst_element_class_set_static_metadata (element_class,
      "ambacompositor",
      "Filter/Compositor",
      "AmShMem NV12 grid compositor (GstAggregator, Cavalry output pool). "
      "Property test-mode for plain NV12 (filesrc) without AmShMem side-data.",
      "Da-shun Pei <dspei@ambarella.com>");

  agg_class->start = GST_DEBUG_FUNCPTR (gst_ambacompositor_start);
  agg_class->stop = GST_DEBUG_FUNCPTR (gst_ambacompositor_stop);
  agg_class->aggregate = GST_DEBUG_FUNCPTR (gst_ambacompositor_aggregate);
  agg_class->create_new_pad = GST_DEBUG_FUNCPTR (gst_ambacompositor_create_new_pad);
  agg_class->update_src_caps =
      GST_DEBUG_FUNCPTR (gst_ambacompositor_update_src_caps);
}

static void
gst_ambacompositor_init (Gstambacompositor *self)
{
  gst_ambacompositor_ftrace ("instance_init %p", (void *) self);

  self->grid_cols = DEFAULT_GRID_COLS;
  self->grid_rows = DEFAULT_GRID_ROWS;
  self->output_width = DEFAULT_OUTPUT_WIDTH;
  self->output_height = DEFAULT_OUTPUT_HEIGHT;
  self->output_pitch = DEFAULT_OUTPUT_PITCH;
  self->dump_output = DEFAULT_DUMP_OUTPUT;
  self->dump_output_path = g_strdup (DEFAULT_DUMP_OUTPUT_PATH);
  self->dump_max_frames = DEFAULT_DUMP_MAX_FRAMES;
  self->all_dec_num = self->grid_cols * self->grid_rows;
  self->out_pool = NULL;
  self->out_pool_buf_size = 0;
  self->dump_fp = NULL;
  self->dump_frame_seq = 0;
  self->frame_sync = NULL;
  self->sync_queue_depth = DEFAULT_SYNC_QUEUE_DEPTH;
  self->test_mode = DEFAULT_TEST_MODE;
  self->trace_frames = DEFAULT_TRACE_FRAMES;
  self->trace_comp_seq = 0;
  self->trace_last_fid = AMBACOMP_TRACE_FID_NONE;
}

static void
gst_ambacompositor_finalize (GObject *object)
{
  Gstambacompositor *self = GST_ambacompositor (object);

  if (self->dump_fp) {
    fclose (self->dump_fp);
    self->dump_fp = NULL;
  }
  if (self->frame_sync) {
    gst_amba_frame_sync_free (self->frame_sync);
    self->frame_sync = NULL;
  }
  gst_ambacompositor_destroy_out_pool (self);
  g_free (self->dump_output_path);

  G_OBJECT_CLASS (gst_ambacompositor_parent_class)->finalize (object);
}

static void
gst_ambacompositor_set_property (GObject *object, guint prop_id,
    const GValue *value, GParamSpec *pspec)
{
  Gstambacompositor *self = GST_ambacompositor (object);

  switch (prop_id) {
    case PROP_GRID_COLS:
      if (!geometry_ok_when_ready (self)) {
        GST_ERROR_OBJECT (self, "grid-cols: change only in NULL or READY");
        break;
      }
      self->grid_cols = g_value_get_uint (value);
      self->all_dec_num = self->grid_cols * self->grid_rows;
      break;
    case PROP_GRID_ROWS:
      if (!geometry_ok_when_ready (self)) {
        GST_ERROR_OBJECT (self, "grid-rows: change only in NULL or READY");
        break;
      }
      self->grid_rows = g_value_get_uint (value);
      self->all_dec_num = self->grid_cols * self->grid_rows;
      break;
    case PROP_OUTPUT_WIDTH:
      if (!geometry_ok_when_ready (self)) {
        GST_ERROR_OBJECT (self, "output-width: change only in NULL or READY");
        break;
      }
      self->output_width = g_value_get_uint (value);
      break;
    case PROP_OUTPUT_HEIGHT:
      if (!geometry_ok_when_ready (self)) {
        GST_ERROR_OBJECT (self, "output-height: change only in NULL or READY");
        break;
      }
      self->output_height = g_value_get_uint (value);
      break;
    case PROP_OUTPUT_PITCH:
      if (!geometry_ok_when_ready (self)) {
        GST_ERROR_OBJECT (self, "output-pitch: change only in NULL or READY");
        break;
      }
      self->output_pitch = g_value_get_uint (value);
      break;
    case PROP_SYNC_QUEUE_DEPTH:
      if (!geometry_ok_when_ready (self)) {
        GST_ERROR_OBJECT (self, "sync-queue-depth: change only in NULL or READY");
        break;
      }
      self->sync_queue_depth = g_value_get_uint (value);
      if (self->frame_sync)
        gst_amba_frame_sync_set_max_depth (self->frame_sync, self->sync_queue_depth);
      break;
    case PROP_DUMP_OUTPUT:
      self->dump_output = g_value_get_boolean (value);
      break;
    case PROP_DUMP_OUTPUT_PATH:
      g_free (self->dump_output_path);
      self->dump_output_path = g_value_dup_string (value);
      if (!self->dump_output_path)
        self->dump_output_path = g_strdup (DEFAULT_DUMP_OUTPUT_PATH);
      break;
    case PROP_DUMP_MAX_FRAMES:
      self->dump_max_frames = g_value_get_uint (value);
      break;
    case PROP_TEST_MODE:
      if (!geometry_ok_when_ready (self)) {
        GST_ERROR_OBJECT (self, "test-mode: change only in NULL or READY");
        break;
      }
      self->test_mode = g_value_get_boolean (value);
      break;
    case PROP_TRACE_FRAMES:
      self->trace_frames = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ambacompositor_get_property (GObject *object, guint prop_id,
    GValue *value, GParamSpec *pspec)
{
  Gstambacompositor *self = GST_ambacompositor (object);

  switch (prop_id) {
    case PROP_GRID_COLS:
      g_value_set_uint (value, self->grid_cols);
      break;
    case PROP_GRID_ROWS:
      g_value_set_uint (value, self->grid_rows);
      break;
    case PROP_OUTPUT_WIDTH:
      g_value_set_uint (value, self->output_width);
      break;
    case PROP_OUTPUT_HEIGHT:
      g_value_set_uint (value, self->output_height);
      break;
    case PROP_OUTPUT_PITCH:
      g_value_set_uint (value, self->output_pitch);
      break;
    case PROP_SYNC_QUEUE_DEPTH:
      g_value_set_uint (value, self->sync_queue_depth);
      break;
    case PROP_DUMP_OUTPUT:
      g_value_set_boolean (value, self->dump_output);
      break;
    case PROP_DUMP_OUTPUT_PATH:
      g_value_set_string (value, self->dump_output_path);
      break;
    case PROP_DUMP_MAX_FRAMES:
      g_value_set_uint (value, self->dump_max_frames);
      break;
    case PROP_TEST_MODE:
      g_value_set_boolean (value, self->test_mode);
      break;
    case PROP_TRACE_FRAMES:
      g_value_set_boolean (value, self->trace_frames);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstAggregatorPad *
gst_ambacompositor_create_new_pad (GstAggregator *agg, GstPadTemplate *templ,
    const gchar *req_name, const GstCaps *caps)
{
  Gstambacompositor *self = GST_ambacompositor (agg);
  guint idx = G_MAXUINT;

  (void) caps;
  if (!req_name || sscanf (req_name, "sink_%u", &idx) != 1) {
    GST_ERROR_OBJECT (self, "invalid request pad name \"%s\"", req_name);
    return NULL;
  }

  if (idx >= self->all_dec_num) {
    GST_ERROR_OBJECT (self,
        "sink_%u invalid for grid %ux%u (expect sink_0..sink_%u)", idx,
        self->grid_cols, self->grid_rows, self->all_dec_num - 1);
    return NULL;
  }

  GST_DEBUG_OBJECT (self, "[comp] create_new_pad: %s (grid %ux%u)", req_name,
      self->grid_cols, self->grid_rows);

  return GST_AGGREGATOR_PAD (g_object_new (GST_TYPE_AGGREGATOR_PAD,
          "name", req_name, "direction", GST_PAD_SINK, "template", templ, NULL));
}

/* Downstream-compatible NV12 WxH caps; GstAggregator negotiation calls this —
 * avoid gst_aggregator_set_src_caps from start(): mandatory events belong to
 * negotiate/finish_buffer (see GstAggregator docs). */
static GstFlowReturn
gst_ambacompositor_update_src_caps (GstAggregator *agg, GstCaps *caps,
    GstCaps **ret)
{
  Gstambacompositor *self = GST_ambacompositor (agg);
  GstCaps *ours;

  g_return_val_if_fail (ret != NULL, GST_FLOW_ERROR);
  *ret = NULL;

  if (!gst_ambacompositor_validate_geometry (self)) {
    GST_ERROR_OBJECT (self, "[comp] update_src_caps: invalid geometry");
    return GST_FLOW_NOT_NEGOTIATED;
  }

  ours = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, (gint) self->output_width,
      "height", G_TYPE_INT, (gint) self->output_height,
      NULL);

  if (!caps || gst_caps_is_any (caps)) {
    *ret = ours;
    GST_DEBUG_OBJECT (self,
        "[comp] update_src_caps: downstream ANY/NULL -> fixed WxH nv12 caps");
    return GST_FLOW_OK;
  }

  *ret = gst_caps_intersect_full (ours, caps, GST_CAPS_INTERSECT_FIRST);
  gst_caps_unref (ours);

  if (!*ret || gst_caps_is_empty (*ret)) {
    if (*ret)
      gst_caps_unref (*ret);
    *ret = NULL;
    GST_WARNING_OBJECT (self,
        "[comp] update_src_caps: no intersection with %" GST_PTR_FORMAT, caps);
    return GST_FLOW_NOT_NEGOTIATED;
  }

  GST_DEBUG_OBJECT (self,
      "[comp] update_src_caps: ok -> %" GST_PTR_FORMAT, *ret);
  return GST_FLOW_OK;
}

static gboolean
gst_ambacompositor_start (GstAggregator *agg)
{
  Gstambacompositor *self = GST_ambacompositor (agg);

  GST_DEBUG_OBJECT (self, "[comp] start: enter");
  gst_ambacompositor_ftrace ("start: enter self=%p", (void *) self);

  self->all_dec_num = self->grid_cols * self->grid_rows;
  if (!gst_ambacompositor_validate_geometry (self)) {
    GST_DEBUG_OBJECT (self, "[comp] start: validate_geometry failed");
    return FALSE;
  }

  GST_DEBUG_OBJECT (self, "[comp] start: all_dec_num=%u sync_queue_depth=%u",
      self->all_dec_num, self->sync_queue_depth);

  if (self->frame_sync)
    gst_amba_frame_sync_free (self->frame_sync);
  self->frame_sync =
      gst_amba_frame_sync_new (self->all_dec_num, self->sync_queue_depth);
  if (!self->frame_sync) {
    GST_DEBUG_OBJECT (self, "[comp] start: frame_sync_new failed");
    return FALSE;
  }
  GST_DEBUG_OBJECT (self, "[comp] start: frame_sync_new ok");
  gst_ambacompositor_ftrace ("start: frame_sync_new ok");

  if (!gst_ambacompositor_create_out_pool (self)) {
    GST_DEBUG_OBJECT (self, "[comp] start: create_out_pool failed");
    gst_amba_frame_sync_free (self->frame_sync);
    self->frame_sync = NULL;
    return FALSE;
  }
  GST_DEBUG_OBJECT (self, "[comp] start: create_out_pool ok");
  gst_ambacompositor_ftrace ("start: create_out_pool ok");

  if (self->dump_output) {
    self->dump_fp = fopen (self->dump_output_path, "wb");
    if (!self->dump_fp)
      GST_WARNING_OBJECT (self, "could not open \"%s\" for dump",
          self->dump_output_path);
    self->dump_frame_seq = 0;
  }

  self->trace_comp_seq = 0;
  self->trace_last_fid = AMBACOMP_TRACE_FID_NONE;

  /* Internal gst_aggregator_change_state (READY→PAUSED) already runs
   * gst_aggregator_start() which resets priv and *then* calls klass->start.
   * GstAggregatorClass::{start,stop} base pointers are NULL — never chain to
   * GST_AGGREGATOR_CLASS (parent_class)->start/stop here (that dereferences NULL). */

  GST_DEBUG_OBJECT (self, "[comp] start: leave ok");
  gst_ambacompositor_ftrace ("start: leave ok");
  return TRUE;
}

static gboolean
gst_ambacompositor_stop (GstAggregator *agg)
{
  Gstambacompositor *self = GST_ambacompositor (agg);

  GST_DEBUG_OBJECT (self, "[comp] stop: enter");

  /* Invoked from internal gst_aggregator_stop() mid-teardown — release only our
   * resources (see comment in gst_ambacompositor_start). */

  if (self->frame_sync) {
    gst_amba_frame_sync_free (self->frame_sync);
    self->frame_sync = NULL;
  }

  if (self->dump_fp) {
    fclose (self->dump_fp);
    self->dump_fp = NULL;
  }

  gst_ambacompositor_destroy_out_pool (self);

  GST_DEBUG_OBJECT (self, "[comp] stop: leave TRUE");
  return TRUE;
}

static GstFlowReturn
gst_ambacompositor_aggregate (GstAggregator *agg, gboolean timeout)
{
  Gstambacompositor *self = GST_ambacompositor (agg);
  GstAggregatorPad *pads[MAX_DEC_NUM];
  GstFlowReturn fret;
  gboolean any_eos = FALSE;
  gboolean all_eos = TRUE;

  (void) timeout;

  if (self->all_dec_num < 1 || self->all_dec_num > MAX_DEC_NUM)
    return GST_FLOW_ERROR;

  fret = gst_ambacompositor_collect_sink_pads (self, pads);
  if (fret != GST_FLOW_OK)
    return fret;

  for (guint i = 0; i < self->all_dec_num; i++) {
    if (gst_aggregator_pad_is_eos (pads[i])) {
      any_eos = TRUE;
    } else {
      all_eos = FALSE;
    }
  }

  if (any_eos && !all_eos) {
    GST_ERROR_OBJECT (self, "EOS on some sink pads but not all");
    return GST_FLOW_ERROR;
  }
  if (all_eos) {
    if (self->frame_sync)
      gst_amba_frame_sync_flush (self->frame_sync);
    return GST_FLOW_EOS;
  }

  if (!self->frame_sync) {
    GST_ERROR_OBJECT (self, "frame_sync not allocated");
    return GST_FLOW_ERROR;
  }

  { // composite the frames into one output buffer
    GstBuffer *inbufs[MAX_DEC_NUM];
    GstBuffer *outbuf = NULL;
    GstFlowReturn acq;
    guint al_h;

    fret = gst_amba_frame_sync_collect (self->frame_sync, pads, self->all_dec_num,
        inbufs, GST_OBJECT (self), gst_ambacompositor_sync_get_frame_id, self);
    if (fret == GST_AGGREGATOR_FLOW_NEED_DATA)
      return fret;
    if (fret != GST_FLOW_OK)
      return fret;

    if (!gst_ambacompositor_validate_all_inputs (self, inbufs)) {
      for (guint i = 0; i < self->all_dec_num; i++)
        gst_buffer_unref (inbufs[i]);
      return GST_FLOW_ERROR;
    }

    acq = gst_buffer_pool_acquire_buffer (self->out_pool, &outbuf, NULL);
    if (acq != GST_FLOW_OK || !outbuf) {
      GST_ERROR_OBJECT (self, "acquire output buffer: %s",
          gst_flow_get_name (acq));
      for (guint i = 0; i < self->all_dec_num; i++)
        gst_buffer_unref (inbufs[i]);
      return acq != GST_FLOW_OK ? acq : GST_FLOW_ERROR;
    }

    al_h = (self->output_height + 15) & ~15u;
    if (!gst_buffer_get_video_meta (outbuf)) {
      gsize off[GST_VIDEO_MAX_PLANES] = { 0, };
      gint str[GST_VIDEO_MAX_PLANES] = { 0, };
      off[0] = 0;
      off[1] = (gsize) self->output_pitch * (gsize) al_h;
      str[0] = (gint) self->output_pitch;
      str[1] = (gint) self->output_pitch;
      gst_buffer_add_video_meta_full (outbuf, GST_VIDEO_FRAME_FLAG_NONE,
          GST_VIDEO_FORMAT_NV12, self->output_width, self->output_height, 2, off,
          str);
    }

    fret = gst_ambacompositor_do_composite_blit (self, inbufs, outbuf);
    if (fret != GST_FLOW_OK) {
      gst_buffer_unref (outbuf);
      for (guint i = 0; i < self->all_dec_num; i++)
        gst_buffer_unref (inbufs[i]);
      return fret;
    }

    if (self->trace_frames) {
      guint32 sfid = 0;

      if (gst_ambacompositor_buffer_get_frame_id (self, inbufs[0], &sfid))
        gst_ambacompositor_trace_frame_bundle (self, inbufs, outbuf, sfid);
    }

    if (GST_BUFFER_PTS_IS_VALID (inbufs[0]))
      GST_BUFFER_PTS (outbuf) = GST_BUFFER_PTS (inbufs[0]);
    else
      GST_BUFFER_PTS (outbuf) = GST_CLOCK_TIME_NONE;

    if (self->dump_fp && self->dump_output) {
      gboolean under_cap =
          (self->dump_max_frames == 0
          || self->dump_frame_seq < self->dump_max_frames);
      if (under_cap) {
        GstMapInfo dm;
        if (gst_buffer_map (outbuf, &dm, GST_MAP_READ)) {
          if (fwrite (dm.data, 1, self->out_pool_buf_size, self->dump_fp)
              != self->out_pool_buf_size)
            GST_WARNING_OBJECT (self, "short fwrite on dump");
          gst_buffer_unmap (outbuf, &dm);
        }
        self->dump_frame_seq++;
      }
    }

    for (guint i = 0; i < self->all_dec_num; i++)
      gst_buffer_unref (inbufs[i]);

    return gst_aggregator_finish_buffer (agg, outbuf);
  }
}

//not needed: release the output buffer, just unref the buffers by downstream
void
gst_ambacompositor_do_release (Gstambacompositor *self,
    const FreeFrame_Msg *fm)
{
  (void) self;
  (void) fm;
}
