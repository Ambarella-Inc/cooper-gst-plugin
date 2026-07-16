/*
 * gstamshmemsrc.c
 *
 * History:
 *    3/30/2026 - [Da-Shun Pei] created file
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
 * SECTION: element-amshmem_src_buffer_pool
 * @title: amshmem_src_buffer_pool
 *
 */

#include <string.h>
#include <inttypes.h>

#include <gst/gst.h>

#include "dds_msgs/AmShMem_Msg.h"
#include "dds_msgs/FreeFrame_Msg.h"
#include "gstamshmemsrcbufferpool.h"
#include "gstamshmemsrc.h"
#include "gstamshmemtypes.h"
#include "msg_type.h"
#include "include/data_publisher.h"

GST_DEBUG_CATEGORY_STATIC (gst_amshmem_src_buffer_pool_debug);
#define GST_CAT_DEFAULT gst_amshmem_src_buffer_pool_debug

typedef struct {
  AmShMem_Msg msg;
} AmShMemNv12Side;

static GQuark
amshmem_src_dds_filled_quark (void)
{
  static gsize once = 0;
  static GQuark q;

  if (g_once_init_enter (&once)) {
    q = g_quark_from_static_string ("amshmem.src.dds-filled");
    g_once_init_leave (&once, 1);
  }
  return q;
}

static GQuark
amshmem_src_nv12_side_quark (void)
{
  static gsize once = 0;
  static GQuark q;

  if (g_once_init_enter (&once)) {
    q = g_quark_from_static_string ("amshmem.src.nv12-side");
    g_once_init_leave (&once, 1);
  }
  return q;
}

void
gst_amshmem_src_buffer_mark_dds_filled (GstBuffer *buf)
{
  g_return_if_fail (GST_IS_BUFFER (buf));
  gst_mini_object_set_qdata (GST_MINI_OBJECT (buf), amshmem_src_dds_filled_quark (),
      GUINT_TO_POINTER (1), NULL);
}

void
gst_amshmem_src_buffer_attach_nv12_side (GstBuffer *buf, const AmShMem_Msg *msg)
{
  AmShMemNv12Side *side;

  g_return_if_fail (GST_IS_BUFFER (buf));
  g_return_if_fail (msg != NULL);

  side = g_new (AmShMemNv12Side, 1);
  memcpy (&side->msg, msg, sizeof (side->msg));
  gst_mini_object_set_qdata (GST_MINI_OBJECT (buf), amshmem_src_nv12_side_quark (),
      side, (GDestroyNotify) g_free);
}

gboolean
gst_amshmem_src_buffer_peek_nv12_frame_id (GstBuffer *buf, guint32 *frame_id_out)
{
  AmShMemNv12Side *side;

  g_return_val_if_fail (GST_IS_BUFFER (buf), FALSE);
  g_return_val_if_fail (frame_id_out != NULL, FALSE);

  side = gst_mini_object_get_qdata (GST_MINI_OBJECT (buf),
      amshmem_src_nv12_side_quark ());
  if (!side)
    return FALSE;
  *frame_id_out = side->msg.frame_id;
  return TRUE;
}

const AmShMem_Msg *
gst_amshmem_src_buffer_peek_nv12_msg (GstBuffer *buf)
{
  AmShMemNv12Side *side;

  g_return_val_if_fail (GST_IS_BUFFER (buf), NULL);
  side = gst_mini_object_get_qdata (GST_MINI_OBJECT (buf),
      amshmem_src_nv12_side_quark ());
  if (!side)
    return NULL;
  return &side->msg;
}

struct _GstAmShMemSrcBufferPool
{
  GstBufferPool parent;

  GstAmShMemSrc *src;
};

struct _GstAmShMemSrcBufferPoolClass
{
  GstBufferPoolClass parent_class;
};

G_DEFINE_TYPE_WITH_CODE (GstAmShMemSrcBufferPool, gst_amshmem_src_buffer_pool,
    GST_TYPE_BUFFER_POOL,
    GST_DEBUG_CATEGORY_INIT (gst_amshmem_src_buffer_pool_debug,
        "amshmem_src_buffer_pool", 0, "AmShMem src buffer pool"));

static gboolean gst_amshmem_src_buffer_pool_start (GstBufferPool *pool);
static gboolean gst_amshmem_src_buffer_pool_stop (GstBufferPool *pool);
static void gst_amshmem_src_buffer_pool_release_buffer (GstBufferPool *pool,
    GstBuffer *buffer);
static void gst_amshmem_src_buffer_pool_finalize (GObject *object);

static void
gst_amshmem_src_buffer_pool_class_init (GstAmShMemSrcBufferPoolClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBufferPoolClass *pool_class = GST_BUFFER_POOL_CLASS (klass);

  gobject_class->finalize = gst_amshmem_src_buffer_pool_finalize;

  pool_class->start = GST_DEBUG_FUNCPTR (gst_amshmem_src_buffer_pool_start);
  pool_class->stop = GST_DEBUG_FUNCPTR (gst_amshmem_src_buffer_pool_stop);
  pool_class->release_buffer =
      GST_DEBUG_FUNCPTR (gst_amshmem_src_buffer_pool_release_buffer);
}

static void
gst_amshmem_src_buffer_pool_init (GstAmShMemSrcBufferPool *self)
{
  self->src = NULL;
}

static void
gst_amshmem_src_buffer_pool_finalize (GObject *object)
{
  GstAmShMemSrcBufferPool *self = GST_AMSHMEM_SRC_BUFFER_POOL (object);

  if (self->src) {
    gst_object_unref (self->src);
    self->src = NULL;
  }

  G_OBJECT_CLASS (gst_amshmem_src_buffer_pool_parent_class)->finalize (object);
}

static gboolean
gst_amshmem_src_buffer_pool_start (GstBufferPool *pool)
{
  return GST_BUFFER_POOL_CLASS (gst_amshmem_src_buffer_pool_parent_class)->start (pool);
}

static gboolean
gst_amshmem_src_buffer_pool_stop (GstBufferPool *pool)
{
  return GST_BUFFER_POOL_CLASS (gst_amshmem_src_buffer_pool_parent_class)->stop (pool);
}

static void
gst_amshmem_src_buffer_pool_release_buffer (GstBufferPool *pool,
    GstBuffer *buffer)
{
  GstAmShMemSrcBufferPool *self = GST_AMSHMEM_SRC_BUFFER_POOL (pool);

  /* Pool start() preallocates buffers and releases them before any create();
   * only buffers filled in gst_amshmem_src_create() carry qdata. */
  if (self->src != NULL &&
      self->src->implem_method == GST_AMSHMEM_IMPLEM_CYCLONEDDS) {
    AmShMemNv12Side *side = gst_mini_object_get_qdata (GST_MINI_OBJECT (buffer),
        amshmem_src_nv12_side_quark ());

    if (side != NULL) {
      const AmShMem_Msg *m = &side->msg;
      FreeFrame_Msg *fm = &self->src->free_frame_pub.free_frame_msg;

      gst_mini_object_steal_qdata (GST_MINI_OBJECT (buffer),
          amshmem_src_nv12_side_quark ());

      memset (fm, 0, sizeof (*fm));
      fm->msg_id = m->dec_id;
      fm->frame_id = m->frame_id;
      fm->buffer_index = m->buffer_index;
      fm->phys_y_addr = m->phys_y_addr;
      fm->phys_uv_addr = m->phys_uv_addr;
      fm->playback_pts = m->playback_pts;
      fm->system_time = (uint64_t) g_get_real_time ();

      g_print ("[amshmem_src pool release] NV12 FreeFrame frame_id=%" PRIu32
          " buffer_index=%" PRIu32 "\n", fm->frame_id, fm->buffer_index);

      if (run_publisher (&self->src->free_frame_pub, FREE_FRAME_MSGTYPE) != 0) {
        GST_WARNING_OBJECT (self->src, "run_publisher FreeFrame failed");
      }

      g_free (side);
    } else if (gst_buffer_get_size (buffer) >= sizeof (AmShMem_Msg) &&
        gst_mini_object_get_qdata (GST_MINI_OBJECT (buffer),
            amshmem_src_dds_filled_quark ()) != NULL) {
      GstMapInfo map;

      gst_mini_object_set_qdata (GST_MINI_OBJECT (buffer),
          amshmem_src_dds_filled_quark (), NULL, NULL);

      if (gst_buffer_map (buffer, &map, GST_MAP_READ)) {
        const AmShMem_Msg *m = (const AmShMem_Msg *) map.data;
        FreeFrame_Msg *fm = &self->src->free_frame_pub.free_frame_msg;

        memset (fm, 0, sizeof (*fm));
        fm->msg_id = m->dec_id;
        fm->frame_id = m->frame_id;
        fm->buffer_index = m->buffer_index;
        fm->phys_y_addr = m->phys_y_addr;
        fm->phys_uv_addr = m->phys_uv_addr;
        fm->playback_pts = m->playback_pts;
        fm->system_time = (uint64_t) g_get_real_time ();

        g_print ("[amshmem_src pool release] publish FreeFrame frame_id=%" PRIu32
            " buffer_index=%" PRIu32 " phys_y=0x%" PRIx64 "\n",
            fm->frame_id, fm->buffer_index, (guint64) fm->phys_y_addr);

        if (run_publisher (&self->src->free_frame_pub, FREE_FRAME_MSGTYPE) != 0) {
          GST_WARNING_OBJECT (self->src, "run_publisher FreeFrame failed");
        }

        gst_buffer_unmap (buffer, &map);
      }
    }
  }

  GST_BUFFER_POOL_CLASS (gst_amshmem_src_buffer_pool_parent_class)->release_buffer (pool,
      buffer);
}

GstBufferPool *
gst_amshmem_src_buffer_pool_new (GstAmShMemSrc *src)
{
  GstAmShMemSrcBufferPool *pool;

  g_return_val_if_fail (GST_IS_AMSHMEM_SRC (src), NULL);

  pool = g_object_new (GST_TYPE_AMSHMEM_SRC_BUFFER_POOL, NULL);
  pool->src = gst_object_ref (src);

  return GST_BUFFER_POOL (pool);
}
