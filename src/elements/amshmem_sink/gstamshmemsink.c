/*
 * gstamshmemsink.c
 *
 * History:
 *    3/11/2026 - [Da-Shun Pei] created file
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
 * SECTION: element-amshmem_sink
 * @title: amshmem_sink
 *
 *
 * ## Example pipelines
 * |[
 * gst-launch-1.0 -e amshmem_element_a num-buffers=20 ! queue ! amshmem_sink implem-method=cyclonedds
 * ]|
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <gst/video/video.h>

#include "cavalry_ioctl.h"
#include "cavalry_mem.h"
#include "gst_amba_cavalry_bufferpool.h"
#include "gstamshmemcommonslot.h"
#include "gstamshmemsink.h"
#include "gstamshmemtypes.h"

GST_DEBUG_CATEGORY_STATIC (gst_amshmem_sink_debug);
#define GST_CAT_DEFAULT gst_amshmem_sink_debug

/* Process-wide: cavalry_mem_init for soft NV12 staging and virt_to_phys (no hw allocator in graph). */
static GMutex amshmem_sink_cavalry_lock;
static gsize amshmem_sink_cavalry_mutex_once = 0;
static gint amshmem_sink_cavalry_refcount;
static gboolean amshmem_sink_cavalry_we_inited;
static gint amshmem_sink_cavalry_fd = -1;

static void
amshmem_sink_cavalry_lock_once_init (void)
{
  if (g_once_init_enter (&amshmem_sink_cavalry_mutex_once)) {
    g_mutex_init (&amshmem_sink_cavalry_lock);
    g_once_init_leave (&amshmem_sink_cavalry_mutex_once, 1);
  }
}

static gboolean
amshmem_sink_ensure_cavalry_mem (GstAmShMemSink * self)
{
  amshmem_sink_cavalry_lock_once_init ();
  g_mutex_lock (&amshmem_sink_cavalry_lock);
  if (amshmem_sink_cavalry_refcount > 0) {
    amshmem_sink_cavalry_refcount++;
    g_mutex_unlock (&amshmem_sink_cavalry_lock);
    return TRUE;
  }
  if (cavalry_mem_get_fd () >= 0) {
    amshmem_sink_cavalry_refcount = 1;
    amshmem_sink_cavalry_we_inited = FALSE;
    g_mutex_unlock (&amshmem_sink_cavalry_lock);
    GST_DEBUG_OBJECT (self, "cavalry_mem already initialized (external)");
    return TRUE;
  }
  amshmem_sink_cavalry_fd = open (CAVALRY_DEV_NODE, O_RDWR, 0);
  if (amshmem_sink_cavalry_fd < 0) {
    GST_ERROR_OBJECT (self, "open %s failed: %s", CAVALRY_DEV_NODE, g_strerror (errno));
    g_mutex_unlock (&amshmem_sink_cavalry_lock);
    return FALSE;
  }
  if (cavalry_mem_init (amshmem_sink_cavalry_fd, 0) < 0) {
    GST_ERROR_OBJECT (self, "cavalry_mem_init failed");
    close (amshmem_sink_cavalry_fd);
    amshmem_sink_cavalry_fd = -1;
    g_mutex_unlock (&amshmem_sink_cavalry_lock);
    return FALSE;
  }
  amshmem_sink_cavalry_refcount = 1;
  amshmem_sink_cavalry_we_inited = TRUE;
  g_mutex_unlock (&amshmem_sink_cavalry_lock);
  GST_DEBUG_OBJECT (self, "cavalry_mem initialized for amshmem_sink");
  return TRUE;
}

static void
amshmem_sink_release_cavalry_mem (GstAmShMemSink * self)
{
  amshmem_sink_cavalry_lock_once_init ();
  g_mutex_lock (&amshmem_sink_cavalry_lock);
  if (G_UNLIKELY (amshmem_sink_cavalry_refcount <= 0)) {
    g_mutex_unlock (&amshmem_sink_cavalry_lock);
    GST_WARNING_OBJECT (self, "cavalry refcount underflow (stop without start?)");
    return;
  }
  amshmem_sink_cavalry_refcount--;
  if (amshmem_sink_cavalry_refcount == 0 && amshmem_sink_cavalry_we_inited) {
    cavalry_mem_exit ();
    if (amshmem_sink_cavalry_fd >= 0) {
      close (amshmem_sink_cavalry_fd);
      amshmem_sink_cavalry_fd = -1;
    }
    amshmem_sink_cavalry_we_inited = FALSE;
    GST_DEBUG_OBJECT (self, "cavalry_mem_exit (last amshmem_sink refcount)");
  }
  g_mutex_unlock (&amshmem_sink_cavalry_lock);
}

#define DEFAULT_AMSHMEM_TOPIC "AmShMem_Msg"
#define DEFAULT_FREE_FRAME_TOPIC "FreeFrame_Msg"
#define DEFAULT_DOMAIN_ID 0
#define DEFAULT_DEC_ID 0
#define DEFAULT_SLAVE_ID 0
#define DEFAULT_DUMP_NV12_FRAME_ID 0
#define DEFAULT_DUMP_NV12_NUM_FRAMES 10

/* Soft-staging cavalry_mem_alloc size: round up to an odd multiple of 128 bytes. */
static gsize
am_shmem_cavalry_odd_align_alloc (gsize need)
{
  gsize u;

  u = (need + 127u) & ~(gsize) 127u;
  if (u == 0)
    u = 128u;
  if (((u / 128u) & 1u) == 0)
    u += 128u;
  return u;
}

enum {
  PROP_0,
  PROP_IMPLEM_METHOD,
  PROP_AMSHMEM_TOPIC,
  PROP_FREE_FRAME_TOPIC,
  PROP_DOMAIN_ID,
  PROP_DEC_ID,
  PROP_SLAVE_ID,
  PROP_SHM_HANDSHAKE_PATH,
  PROP_SHM_POOL_TOTAL_BYTES,
  PROP_DUMP_NV12_DIR,
  PROP_DUMP_NV12_FRAME_ID,
  PROP_DUMP_NV12_NUM_FRAMES,
  PROP_LAST
};

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

typedef struct {
  GstAmShMemSink *sink;
  guint32 buffer_index;
} GstAmShMemSinkFreeFrameUnrefData;

#define gst_amshmem_sink_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstAmShMemSink, gst_amshmem_sink, GST_TYPE_BASE_SINK,
    GST_DEBUG_CATEGORY_INIT (gst_amshmem_sink_debug, "amshmem_sink", 0,
        "AmShMem CycloneDDS sink"));

static void gst_amshmem_sink_finalize (GObject *object);
static gboolean gst_amshmem_sink_start (GstBaseSink *sink);
static gboolean gst_amshmem_sink_stop (GstBaseSink *sink);
static GstFlowReturn gst_amshmem_sink_render (GstBaseSink *sink, GstBuffer *buf);
static void gst_amshmem_sink_set_property (GObject *object, guint prop_id,
    const GValue *value, GParamSpec *pspec);
static void gst_amshmem_sink_get_property (GObject *object, guint prop_id,
    GValue *value, GParamSpec *pspec);

static void *gst_amshmem_sink_free_frame_thread (void *arg);
static void gst_amshmem_sink_freeframe_unref_async (GstElement *element,
    gpointer user_data);
static void gst_amshmem_sink_clear_held (GstAmShMemSink *self);
static gboolean gst_amshmem_sink_event (GstBaseSink *sink, GstEvent *event);

static gboolean
gst_amshmem_sink_use_scm_pool (const GstAmShMemSink * self)
{
  return self->shm_handshake_path && self->shm_handshake_path[0] != '\0';
}

typedef struct {
  unsigned long alloc_size;
  unsigned long phys;
  void *virt;
} GstAmShMemSinkCavalryWrap;

static void
gst_amshmem_sink_cavalry_wrap_free (gpointer data)
{
  GstAmShMemSinkCavalryWrap *w = data;

  if (w && w->alloc_size && w->virt)
    cavalry_mem_free (w->alloc_size, w->phys, w->virt);
  g_free (w);
}

/**
 * Software NV12 (no GstAmbaCavalryBufferMeta): allocate CV memory with
 * share_to_dsp=0, copy planes, wrap as GstBuffer + meta so fill/render match hw path.
 * PA comes from cavalry_mem_alloc_with_attr (not cavalry_mem_virt_to_phys on heap).
 */
static GstBuffer *
gst_amshmem_sink_stage_nv12_to_cavalry (GstAmShMemSink * self, GstBuffer * src,
    GstVideoMeta * vm)
{
  guint w, h, spitch, al_h, al_uv;
  gsize nv12_need, alloc_sz;
  unsigned long phys = 0;
  void *virt = NULL;
  struct cavalry_mem_attr attr;
  GstAmShMemSinkCavalryWrap *wrap = NULL;
  GstBuffer *out = NULL;
  GstMapInfo smap;
  guint8 *d;
  const guint8 *sy, *suv;
  gsize off[GST_VIDEO_MAX_PLANES];
  gint str[GST_VIDEO_MAX_PLANES];

  g_return_val_if_fail (GST_IS_AMSHMEM_SINK (self), NULL);
  g_return_val_if_fail (GST_IS_BUFFER (src), NULL);
  g_return_val_if_fail (vm && vm->format == GST_VIDEO_FORMAT_NV12, NULL);

  w = vm->width;
  h = vm->height;
  spitch = (guint) vm->stride[0];
  al_h = (h + 15u) & ~15u;
  al_uv = al_h / 2;
  nv12_need = (gsize) spitch * (gsize) al_h * 3 / 2;
  alloc_sz = am_shmem_cavalry_odd_align_alloc (nv12_need);

  memset (&attr, 0, sizeof (attr));
  attr.cache_en = 1;
  attr.share_to_dsp = 0;

  if (cavalry_mem_alloc_with_attr ((unsigned long) alloc_sz, &phys, &virt, &attr) < 0) {
    GST_ERROR_OBJECT (self,
        "cavalry_mem_alloc_with_attr (soft NV12 staging) failed size=%" G_GSIZE_FORMAT,
        alloc_sz);
    return NULL;
  }

  if (!gst_buffer_map (src, &smap, GST_MAP_READ)) {
    GST_ERROR_OBJECT (self, "map source NV12 failed");
    cavalry_mem_free ((unsigned long) alloc_sz, phys, virt);
    return NULL;
  }

  d = (guint8 *) virt;
  sy = smap.data + vm->offset[0];
  suv = smap.data + vm->offset[1];

  for (guint y = 0; y < h; y++)
    memcpy (d + (gsize) y * spitch, sy + (gsize) y * spitch, w);
  if (al_h > h)
    memset (d + (gsize) h * spitch, 0, (gsize) (al_h - h) * spitch);

  {
    guint8 *duv = d + (gsize) spitch * al_h;
    guint uv_rows = h / 2;

    for (guint y = 0; y < uv_rows; y++)
      memcpy (duv + (gsize) y * spitch, suv + (gsize) y * spitch, w);
    if (al_uv > uv_rows) {
      memset (duv + (gsize) uv_rows * spitch, 0x80,
          (gsize) (al_uv - uv_rows) * spitch);
    }
  }

  gst_buffer_unmap (src, &smap);

  wrap = g_new (GstAmShMemSinkCavalryWrap, 1);
  wrap->alloc_size = (unsigned long) alloc_sz;
  wrap->phys = phys;
  wrap->virt = virt;

  out = gst_buffer_new_wrapped_full (0, virt, (gssize) alloc_sz,
      0, (gssize) nv12_need, wrap, gst_amshmem_sink_cavalry_wrap_free);
  if (!out) {
    GST_ERROR_OBJECT (self, "gst_buffer_new_wrapped_full failed");
    gst_amshmem_sink_cavalry_wrap_free (wrap);
    return NULL;
  }

  memset (off, 0, sizeof (off));
  memset (str, 0, sizeof (str));
  off[0] = 0;
  off[1] = (gsize) spitch * al_h;
  str[0] = (gint) spitch;
  str[1] = (gint) spitch;
  if (!gst_buffer_add_video_meta_full (out, GST_VIDEO_FRAME_FLAG_NONE,
          GST_VIDEO_FORMAT_NV12, w, h, 2, off, str)) {
    GST_ERROR_OBJECT (self, "gst_buffer_add_video_meta_full failed");
    gst_buffer_unref (out);
    return NULL;
  }

  gst_buffer_add_amba_cavalry_meta (out, -1, 0, alloc_sz, FALSE, -1, (guint64) phys);

  GST_DEBUG_OBJECT (self,
      "staged soft NV12 to cavalry: %ux%u pitch=%u PA=0x%lx alloc=%" G_GSIZE_FORMAT,
      w, h, spitch, phys, alloc_sz);

  return out;
}

static void
gst_amshmem_sink_maybe_dump_nv12 (GstAmShMemSink * self, const AmShMem_Msg * pub,
    GstBuffer * buf)
{
  GstMapInfo map;
  gchar *path;
  FILE *fp;
  guint al_h;
  gsize sz, n;

  if (!self->dump_nv12_dir || !self->dump_nv12_dir[0])
    return;
  if (pub->data_format != AM_SHMEM_DATA_FORMAT_PHYS_NV12
      && pub->data_format != AM_SHMEM_DATA_FORMAT_POOL_OFFSET_NV12)
    return;
  if (pub->width == 0 || pub->height == 0 || pub->pitch == 0)
    return;
  if ((gint) pub->frame_id < self->dump_nv12_frame_id)
    return;
  if ((guint) (pub->frame_id - (guint) self->dump_nv12_frame_id) >=
      self->dump_nv12_num_frames)
    return;

  al_h = (pub->height + 15u) & ~15u;
  sz = (gsize) pub->pitch * (gsize) al_h * 3 / 2;

  if (!gst_buffer_map (buf, &map, GST_MAP_READ))
    return;

  n = map.size < sz ? map.size : sz;
  path = g_strdup_printf ("%s/sink_%05" PRIu32 ".nv12", self->dump_nv12_dir,
      pub->frame_id);
  fp = fopen (path, "wb");
  if (fp) {
    if (fwrite (map.data, 1, n, fp) != n)
      GST_WARNING_OBJECT (self, "dump nv12 fwrite short %s", path);
    fclose (fp);
  } else {
    GST_WARNING_OBJECT (self, "dump nv12 open failed %s", path);
  }
  g_free (path);
  gst_buffer_unmap (buf, &map);
}

GType
gst_amshmem_implem_method_get_type (void)
{
  static gsize gtype_id = 0;

  if (g_once_init_enter (&gtype_id)) {
    static const GEnumValue values[] = {
      { GST_AMSHMEM_IMPLEM_SOCKET, "socket", "socket" },
      { GST_AMSHMEM_IMPLEM_CYCLONEDDS, "cyclonedds", "cyclonedds" },
      { GST_AMSHMEM_IMPLEM_FASTDDS, "fastdds", "fastdds" },
      { 0, NULL, NULL }
    };
    GType type = g_enum_register_static ("GstAmShMemImplemMethod", values);
    g_once_init_leave (&gtype_id, (gsize) type);
  }

  return (GType) gtype_id;
}

static void
gst_amshmem_sink_class_init (GstAmShMemSinkClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GstBaseSinkClass *gstbasesink_class = GST_BASE_SINK_CLASS (klass);

  gobject_class->finalize = gst_amshmem_sink_finalize;
  gobject_class->set_property = gst_amshmem_sink_set_property;
  gobject_class->get_property = gst_amshmem_sink_get_property;

  g_object_class_install_property (gobject_class, PROP_IMPLEM_METHOD,
      g_param_spec_enum ("implem-method", "Implementation method",
          "Transport: socket, cyclonedds, or fastdds (only cyclonedds implemented)",
          GST_TYPE_AMSHMEM_IMPLEM_METHOD, GST_AMSHMEM_IMPLEM_CYCLONEDDS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_AMSHMEM_TOPIC,
      g_param_spec_string ("amshmem-topic", "AmShMem topic",
          "CycloneDDS topic name for AmShMem_Msg (must match paired amshmem_src).",
          DEFAULT_AMSHMEM_TOPIC,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_FREE_FRAME_TOPIC,
      g_param_spec_string ("free-frame-topic", "FreeFrame topic",
          "CycloneDDS topic name for FreeFrame_Msg (must match paired amshmem_src).",
          DEFAULT_FREE_FRAME_TOPIC,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DOMAIN_ID,
      g_param_spec_uint ("domain-id", "Domain ID",
          "CycloneDDS domain id for this element's participants (must match paired amshmem_src).",
          0, 230, DEFAULT_DOMAIN_ID,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DEC_ID,
      g_param_spec_uint ("dec-id", "Decoder ID",
          "dec_id field in AmShMem_Msg", 0, 255, DEFAULT_DEC_ID,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SLAVE_ID,
      g_param_spec_uint ("slave-id", "Slave ID",
          "slave_id field in AmShMem_Msg", 0, 255, DEFAULT_SLAVE_ID,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SHM_HANDSHAKE_PATH,
      g_param_spec_string ("shm-handshake-path", "SHM handshake Unix path",
          "Empty (default): DDS sends CPU physical NV12 (PHYS_NV12). Non-empty: "
          "pool byte offsets (POOL_OFFSET_NV12) and pass pool fd via SCM_RIGHTS; "
          "amshmem_src must use the same path.",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SHM_POOL_TOTAL_BYTES,
      g_param_spec_uint64 ("shm-pool-total-bytes", "Pool mmap size",
          "Total bytes to mmap on receiver (0 = estimate from first buffer: "
          "block_size * " G_STRINGIFY (GST_AMSHMEM_POOL_MAX_BUFFERS) ")",
          0, G_MAXUINT64, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DUMP_NV12_DIR,
      g_param_spec_string ("dump-nv12-dir", "Dump NV12 directory",
          "If set, write sink_%05u.nv12 (first dump-nv12-num-frames frames from "
          "dump-nv12-frame-id) for debugging; empty disables.",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DUMP_NV12_FRAME_ID,
      g_param_spec_int ("dump-nv12-frame-id", "First dump NV12 frame id",
          "First AmShMem_Msg.frame_id to dump (inclusive).", 0, G_MAXINT32,
          DEFAULT_DUMP_NV12_FRAME_ID, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DUMP_NV12_NUM_FRAMES,
      g_param_spec_uint ("dump-nv12-num-frames", "Number of NV12 frames to dump",
          "Max frames to dump after dump-nv12-frame-id.", 0, 4096,
          DEFAULT_DUMP_NV12_NUM_FRAMES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_static_pad_template (gstelement_class, &sink_factory);
  gst_element_class_set_static_metadata (gstelement_class,
      "AmShMem CycloneDDS sink",
      "Sink/Network",
      "Publishes AmShMem_Msg; subscribes FreeFrame_Msg; holds buffer refs until FreeFrame",
      "Da-shun Pei <dspei@ambarella.com>");

  gstbasesink_class->start = GST_DEBUG_FUNCPTR (gst_amshmem_sink_start);
  gstbasesink_class->stop = GST_DEBUG_FUNCPTR (gst_amshmem_sink_stop);
  gstbasesink_class->render = GST_DEBUG_FUNCPTR (gst_amshmem_sink_render);
  gstbasesink_class->event = GST_DEBUG_FUNCPTR (gst_amshmem_sink_event);
}

static void
gst_amshmem_sink_init (GstAmShMemSink *self)
{
  self->implem_method = GST_AMSHMEM_IMPLEM_CYCLONEDDS;
  self->amshmem_topic = g_strdup (DEFAULT_AMSHMEM_TOPIC);
  self->free_frame_topic = g_strdup (DEFAULT_FREE_FRAME_TOPIC);
  self->domain_id = DEFAULT_DOMAIN_ID;
  self->dec_id = (guint8) DEFAULT_DEC_ID;
  self->slave_id = (guint8) DEFAULT_SLAVE_ID;

  memset (&self->amshmem_pub, 0, sizeof (self->amshmem_pub));
  memset (&self->free_frame_sub, 0, sizeof (self->free_frame_sub));

  self->free_frame_waitset = DDS_ENTITY_NIL;
  self->free_frame_readcond = DDS_ENTITY_NIL;
  self->free_frame_thread_run = FALSE;
  self->free_frame_thread = (pthread_t) 0;
  self->frame_id_seq = 0;
  self->pool_slot_counter = 0;
  self->shm_handshake_path = NULL;
  self->shm_pool_total_bytes = 0;
  self->scm_server = NULL;
  self->scm_pool_announced = FALSE;

  self->dump_nv12_dir = NULL;
  self->dump_nv12_frame_id = DEFAULT_DUMP_NV12_FRAME_ID;
  self->dump_nv12_num_frames = DEFAULT_DUMP_NV12_NUM_FRAMES;

  g_mutex_init (&self->free_frame_mutex);
  g_mutex_init (&self->held_lock);
  memset (self->held_buffers, 0, sizeof (self->held_buffers));
}

static void
gst_amshmem_sink_finalize (GObject *object)
{
  GstAmShMemSink *self = GST_AMSHMEM_SINK (object);

  gst_amshmem_sink_clear_held (self);

  if (self->scm_server) {
    gst_amshmem_scm_server_free (self->scm_server);
    self->scm_server = NULL;
  }
  g_free (self->shm_handshake_path);
  self->shm_handshake_path = NULL;
  g_free (self->dump_nv12_dir);
  self->dump_nv12_dir = NULL;

  g_free (self->amshmem_topic);
  self->amshmem_topic = NULL;
  g_free (self->free_frame_topic);
  self->free_frame_topic = NULL;

  g_mutex_clear (&self->free_frame_mutex);
  g_mutex_clear (&self->held_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_amshmem_sink_set_property (GObject *object, guint prop_id,
    const GValue *value, GParamSpec *pspec)
{
  GstAmShMemSink *self = GST_AMSHMEM_SINK (object);

  switch (prop_id) {
    case PROP_IMPLEM_METHOD:
      self->implem_method = g_value_get_enum (value);
      break;
    case PROP_AMSHMEM_TOPIC:
      g_free (self->amshmem_topic);
      self->amshmem_topic = g_value_dup_string (value);
      break;
    case PROP_FREE_FRAME_TOPIC:
      g_free (self->free_frame_topic);
      self->free_frame_topic = g_value_dup_string (value);
      break;
    case PROP_DOMAIN_ID:
      self->domain_id = g_value_get_uint (value);
      break;
    case PROP_DEC_ID:
      self->dec_id = (guint8) g_value_get_uint (value);
      break;
    case PROP_SLAVE_ID:
      self->slave_id = (guint8) g_value_get_uint (value);
      break;
    case PROP_SHM_HANDSHAKE_PATH:
      g_free (self->shm_handshake_path);
      self->shm_handshake_path = g_value_dup_string (value);
      break;
    case PROP_SHM_POOL_TOTAL_BYTES:
      self->shm_pool_total_bytes = g_value_get_uint64 (value);
      break;
    case PROP_DUMP_NV12_DIR:
      g_free (self->dump_nv12_dir);
      self->dump_nv12_dir = g_value_dup_string (value);
      break;
    case PROP_DUMP_NV12_FRAME_ID:
      self->dump_nv12_frame_id = g_value_get_int (value);
      break;
    case PROP_DUMP_NV12_NUM_FRAMES:
      self->dump_nv12_num_frames = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_amshmem_sink_get_property (GObject *object, guint prop_id,
    GValue *value, GParamSpec *pspec)
{
  GstAmShMemSink *self = GST_AMSHMEM_SINK (object);

  switch (prop_id) {
    case PROP_IMPLEM_METHOD:
      g_value_set_enum (value, self->implem_method);
      break;
    case PROP_AMSHMEM_TOPIC:
      g_value_set_string (value, self->amshmem_topic);
      break;
    case PROP_FREE_FRAME_TOPIC:
      g_value_set_string (value, self->free_frame_topic);
      break;
    case PROP_DOMAIN_ID:
      g_value_set_uint (value, self->domain_id);
      break;
    case PROP_DEC_ID:
      g_value_set_uint (value, self->dec_id);
      break;
    case PROP_SLAVE_ID:
      g_value_set_uint (value, self->slave_id);
      break;
    case PROP_SHM_HANDSHAKE_PATH:
      g_value_set_string (value, self->shm_handshake_path);
      break;
    case PROP_SHM_POOL_TOTAL_BYTES:
      g_value_set_uint64 (value, self->shm_pool_total_bytes);
      break;
    case PROP_DUMP_NV12_DIR:
      g_value_set_string (value, self->dump_nv12_dir);
      break;
    case PROP_DUMP_NV12_FRAME_ID:
      g_value_set_int (value, self->dump_nv12_frame_id);
      break;
    case PROP_DUMP_NV12_NUM_FRAMES:
      g_value_set_uint (value, self->dump_nv12_num_frames);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
fill_am_shmem_msg_from_buffer (GstAmShMemSink * self, GstBuffer * buf,
    AmShMem_Msg * msg)
{
  GstVideoMeta *vmeta;
  GstCaps *pcaps;
  GstVideoInfo info;
  gsize sz;
  const GstStructure *st;
  const gchar *name;
  gboolean use_scm = gst_amshmem_sink_use_scm_pool (self);

  memset (msg, 0, sizeof (*msg));
  msg->dec_id = self->dec_id;
  msg->slave_id = self->slave_id;
  msg->playback_pts = GST_BUFFER_PTS (buf) != GST_CLOCK_TIME_NONE
      ? (uint64_t) GST_BUFFER_PTS (buf) : 0;
  msg->system_time = (uint64_t) g_get_real_time ();

  sz = gst_buffer_get_size (buf);
  pcaps = gst_pad_get_current_caps (GST_BASE_SINK (self)->sinkpad);
  name = NULL;
  if (pcaps && gst_caps_get_size (pcaps) > 0) {
    st = gst_caps_get_structure (pcaps, 0);
    name = gst_structure_get_name (st);
  }

  if (name && g_strcmp0 (name, GST_AMSHMEM_MSG_CAPS) == 0
      && sz >= sizeof (AmShMem_Msg)) {
    GstMapInfo map;
    if (gst_buffer_map (buf, &map, GST_MAP_READ)) {
      memcpy (msg, map.data, sizeof (AmShMem_Msg));
      gst_buffer_unmap (buf, &map);
      if (GST_BUFFER_PTS (buf) != GST_CLOCK_TIME_NONE)
        msg->playback_pts = (uint64_t) GST_BUFFER_PTS (buf);
      msg->system_time = (uint64_t) g_get_real_time ();
      if (pcaps)
        gst_caps_unref (pcaps);
      return TRUE;
    }
  }

  msg->frame_id = self->frame_id_seq++;
  msg->data_format = AM_SHMEM_DATA_FORMAT_PHYS_NV12;

  vmeta = gst_buffer_get_video_meta (buf);
  if (vmeta && vmeta->format == GST_VIDEO_FORMAT_NV12
      && gst_amba_cavalry_buffer_has_meta (buf)) {
    gint cav_fd = gst_amba_cavalry_buffer_get_fd (buf);
    gsize block_off = gst_amba_cavalry_buffer_get_block_offset (buf);
    gint cbi = gst_amba_cavalry_buffer_get_block_index (buf);
    guint al_h = (vmeta->height + 15) & ~15u;
    guint64 slab_phys = gst_amba_cavalry_buffer_get_slab_phys (buf);

    if (use_scm && cav_fd >= 0) {
      msg->width = (uint32_t) vmeta->width;
      msg->height = (uint32_t) vmeta->height;
      msg->pitch = (uint32_t) vmeta->stride[0];
      msg->data_format = AM_SHMEM_DATA_FORMAT_POOL_OFFSET_NV12;
      msg->phys_y_addr = (uint64_t) block_off;
      msg->phys_uv_addr =
          (uint64_t) block_off + (uint64_t) msg->pitch * (uint64_t) al_h;
    } else if (slab_phys != 0) {
      msg->width = (uint32_t) vmeta->width;
      msg->height = (uint32_t) vmeta->height;
      msg->pitch = (uint32_t) vmeta->stride[0];
      msg->data_format = AM_SHMEM_DATA_FORMAT_PHYS_NV12;
      msg->phys_y_addr = (uint64_t) (slab_phys + (guint64) vmeta->offset[0]);
      msg->phys_uv_addr = (uint64_t) (slab_phys + (guint64) vmeta->offset[1]);
    } else if (cav_fd >= 0) {
      GstMapInfo map;
      const guint8 *y_ptr;
      const guint8 *uv_ptr;
      unsigned long py, puv;

      msg->width = (uint32_t) vmeta->width;
      msg->height = (uint32_t) vmeta->height;
      msg->pitch = (uint32_t) vmeta->stride[0];

      if (!gst_buffer_map (buf, &map, GST_MAP_READ)) {
        if (pcaps)
          gst_caps_unref (pcaps);
        return FALSE;
      }
      y_ptr = map.data + vmeta->offset[0];
      uv_ptr = map.data + vmeta->offset[1];
      py = cavalry_mem_virt_to_phys ((void *) (guintptr) y_ptr);
      puv = cavalry_mem_virt_to_phys ((void *) (guintptr) uv_ptr);
      gst_buffer_unmap (buf, &map);

      if (py == 0 || puv == 0) {
        GST_ERROR_OBJECT (self, "cavalry_mem_virt_to_phys failed (y=%lu uv=%lu)",
            py, puv);
        if (pcaps)
          gst_caps_unref (pcaps);
        return FALSE;
      }
      msg->data_format = AM_SHMEM_DATA_FORMAT_PHYS_NV12;
      msg->phys_y_addr = (uint64_t) py;
      msg->phys_uv_addr = (uint64_t) puv;
    } else {
      GST_ERROR_OBJECT (self,
          "cavalry buffer meta but no dmabuf fd and no slab phys (use cavalry-phys-alloc on amba_hwvdecv2)");
      if (pcaps)
        gst_caps_unref (pcaps);
      return FALSE;
    }

    if (cbi >= 0)
      msg->buffer_index = (uint32_t) cbi;
    else
      msg->buffer_index =
          (uint32_t) gst_amshmem_buffer_get_or_assign_slot (buf,
          &self->pool_slot_counter);
    if (pcaps)
      gst_caps_unref (pcaps);
    return TRUE;
  }

  if (pcaps) {
    if (!vmeta && gst_video_info_from_caps (&info, pcaps)) {
      msg->width = (uint32_t) GST_VIDEO_INFO_WIDTH (&info);
      msg->height = (uint32_t) GST_VIDEO_INFO_HEIGHT (&info);
      msg->pitch = (uint32_t) GST_VIDEO_INFO_PLANE_STRIDE (&info, 0);
      if (GST_VIDEO_INFO_FORMAT (&info) == GST_VIDEO_FORMAT_NV12
          && gst_amba_cavalry_buffer_has_meta (buf)) {
        gint cav_fd = gst_amba_cavalry_buffer_get_fd (buf);
        gsize block_off = gst_amba_cavalry_buffer_get_block_offset (buf);
        guint al_h = (msg->height + 15) & ~15u;
        guint64 slab_phys = gst_amba_cavalry_buffer_get_slab_phys (buf);

        if (use_scm && cav_fd >= 0) {
          msg->data_format = AM_SHMEM_DATA_FORMAT_POOL_OFFSET_NV12;
          msg->phys_y_addr = (uint64_t) block_off;
          msg->phys_uv_addr =
              (uint64_t) block_off + (uint64_t) msg->pitch * (uint64_t) al_h;
        } else if (slab_phys != 0) {
          msg->data_format = AM_SHMEM_DATA_FORMAT_PHYS_NV12;
          msg->phys_y_addr =
              (uint64_t) (slab_phys + (guint64) GST_VIDEO_INFO_PLANE_OFFSET (&info, 0));
          msg->phys_uv_addr =
              (uint64_t) (slab_phys + (guint64) GST_VIDEO_INFO_PLANE_OFFSET (&info, 1));
        } else if (cav_fd >= 0) {
          GstMapInfo map;
          const guint8 *y_ptr;
          const guint8 *uv_ptr;
          unsigned long py, puv;

          if (!gst_buffer_map (buf, &map, GST_MAP_READ)) {
            gst_caps_unref (pcaps);
            return FALSE;
          }
          y_ptr = map.data + GST_VIDEO_INFO_PLANE_OFFSET (&info, 0);
          uv_ptr = map.data + GST_VIDEO_INFO_PLANE_OFFSET (&info, 1);
          py = cavalry_mem_virt_to_phys ((void *) (guintptr) y_ptr);
          puv = cavalry_mem_virt_to_phys ((void *) (guintptr) uv_ptr);
          gst_buffer_unmap (buf, &map);
          if (py == 0 || puv == 0) {
            GST_ERROR_OBJECT (self, "cavalry_mem_virt_to_phys failed (caps path)");
            gst_caps_unref (pcaps);
            return FALSE;
          }
          msg->data_format = AM_SHMEM_DATA_FORMAT_PHYS_NV12;
          msg->phys_y_addr = (uint64_t) py;
          msg->phys_uv_addr = (uint64_t) puv;
        } else {
          GST_ERROR_OBJECT (self,
              "cavalry meta but no fd and no slab phys (caps path)");
          gst_caps_unref (pcaps);
          return FALSE;
        }
        msg->buffer_index =
            (uint32_t) gst_amshmem_buffer_get_or_assign_slot (buf,
            &self->pool_slot_counter);
      }
    }
    gst_caps_unref (pcaps);
  }

  if (msg->width == 0 && vmeta) {
    msg->width = (uint32_t) vmeta->width;
    msg->height = (uint32_t) vmeta->height;
    msg->pitch = (uint32_t) vmeta->stride[0];
  }

  g_print ("[shmemsink send] dec_id=%" PRIu8 " slave_id=%" PRIu8 " frame_id=%"
      PRIu32 " fmt=%" PRIu32 " phys_y=0x%" PRIx64 "\n", msg->dec_id,
      msg->slave_id, msg->frame_id, msg->data_format, (guint64) msg->phys_y_addr);

  return TRUE;
}

static void
gst_amshmem_sink_clear_held (GstAmShMemSink *self)
{
  guint i;

  g_mutex_lock (&self->held_lock);
  for (i = 0; i < GST_AMSHMEM_POOL_MAX_BUFFERS; i++) {
    if (self->held_buffers[i]) {
      gst_buffer_unref (self->held_buffers[i]);
      self->held_buffers[i] = NULL;
    }
  }
  g_mutex_unlock (&self->held_lock);
}

static void
gst_amshmem_sink_freeframe_unref_async (GstElement *element, gpointer user_data)
{
  GstAmShMemSinkFreeFrameUnrefData *d = user_data;
  GstAmShMemSink *self = d->sink;
  guint32 idx = d->buffer_index;
  GstBuffer *b = NULL;

  (void) element;

  if (idx < GST_AMSHMEM_POOL_MAX_BUFFERS) {
    g_mutex_lock (&self->held_lock);
    if (self->held_buffers[idx]) {
      b = self->held_buffers[idx];
      self->held_buffers[idx] = NULL;
    }
    g_mutex_unlock (&self->held_lock);
  }

  if (b) {
    gst_buffer_unref (b);
  } else {
    GST_DEBUG_OBJECT (self, "FreeFrame buffer_index=%" G_GUINT32_FORMAT
        ": no held buffer", idx);
  }

  gst_object_unref (self);
  g_free (d);
}

static gboolean
gst_amshmem_sink_event (GstBaseSink *sink, GstEvent *event)
{
  GstAmShMemSink *self = GST_AMSHMEM_SINK (sink);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      gst_amshmem_sink_clear_held (self);
      self->scm_pool_announced = FALSE;
      self->pool_slot_counter = 0;
      break;
    default:
      break;
  }

  return GST_BASE_SINK_CLASS (parent_class)->event (sink, event);
}

static void *
gst_amshmem_sink_free_frame_thread (void *arg)
{
  GstAmShMemSink *self = GST_AMSHMEM_SINK (arg);
  dds_return_t ws_ret;
  int ret;

  while (self->free_frame_thread_run) {
    ws_ret = dds_waitset_wait (self->free_frame_waitset,
        self->free_frame_wsresults, 1, DDS_MSECS (30));

    if (ws_ret < 0) {
      GST_DEBUG_OBJECT (self, "dds_waitset_wait: %s",
          dds_strretcode (-ws_ret));
      continue;
    }

    if (ws_ret == 0)
      continue;

    ret = run_subscriber (&self->free_frame_sub, FREE_FRAME_MSGTYPE);
    if (ret < 0) {
      GST_DEBUG_OBJECT (self, "run_subscriber FreeFrame failed");
    } else if (ret > 0 && self->free_frame_sub.free_frame_msg) {

      FreeFrame_Msg *fm = self->free_frame_sub.free_frame_msg;
      GstAmShMemSinkFreeFrameUnrefData *d;

      g_print("[shmemsink sub freeframe] msg_id=%" PRIu8 " frame_id=%" PRIu32 " buffer_index=%" PRIu32 " phys_y=0x%" PRIx64 "\n",
        fm->msg_id, fm->frame_id, fm->buffer_index, (guint64) fm->phys_y_addr);

      g_mutex_lock (&self->free_frame_mutex);
      d = g_new0 (GstAmShMemSinkFreeFrameUnrefData, 1);
      d->sink = gst_object_ref (self);
      d->buffer_index = fm->buffer_index;
      g_mutex_unlock (&self->free_frame_mutex);

      gst_element_call_async (GST_ELEMENT (self),
          gst_amshmem_sink_freeframe_unref_async, d, NULL);
    }
  }

  return NULL;
}

static gboolean
gst_amshmem_sink_start (GstBaseSink *sink)
{
  GstAmShMemSink *self = GST_AMSHMEM_SINK (sink);

  if (self->implem_method == GST_AMSHMEM_IMPLEM_SOCKET ||
      self->implem_method == GST_AMSHMEM_IMPLEM_FASTDDS) {
    GST_ERROR_OBJECT (self,
        "implem-method %d not implemented (only cyclonedds is supported)",
        (int) self->implem_method);
    return FALSE;
  }

  if (!amshmem_sink_ensure_cavalry_mem (self))
    return FALSE;

  if (self->shm_handshake_path && self->shm_handshake_path[0]) {
    self->scm_server = gst_amshmem_scm_server_new (self->shm_handshake_path);
    if (!gst_amshmem_scm_server_start (self->scm_server)) {
      GST_ERROR_OBJECT (self, "SCM server listen on %s failed",
          self->shm_handshake_path);
      gst_amshmem_scm_server_free (self->scm_server);
      self->scm_server = NULL;
      amshmem_sink_release_cavalry_mem (self);
      return FALSE;
    }
    self->scm_pool_announced = FALSE;
  }

  if (init_publisher (&self->amshmem_pub, AM_SHMEM_MSGTYPE,
          (uint32_t) self->domain_id, self->amshmem_topic) != 0) {
    GST_ERROR_OBJECT (self, "init_publisher AmShMem failed");
    if (self->scm_server) {
      gst_amshmem_scm_server_free (self->scm_server);
      self->scm_server = NULL;
    }
    amshmem_sink_release_cavalry_mem (self);
    return FALSE;
  }

  if (init_subscriber (&self->free_frame_sub, FREE_FRAME_MSGTYPE,
          (uint32_t) self->domain_id, self->free_frame_topic) != 0) {
    GST_ERROR_OBJECT (self, "init_subscriber FreeFrame failed");
    delete_publisher (&self->amshmem_pub);
    if (self->scm_server) {
      gst_amshmem_scm_server_free (self->scm_server);
      self->scm_server = NULL;
    }
    amshmem_sink_release_cavalry_mem (self);
    return FALSE;
  }

  self->free_frame_waitset = dds_create_waitset (
      self->free_frame_sub.participant);
  if (self->free_frame_waitset < 0) {
    GST_ERROR_OBJECT (self, "dds_create_waitset failed: %s",
        dds_strretcode (-self->free_frame_waitset));
    delete_subscriber (&self->free_frame_sub, FREE_FRAME_MSGTYPE);
    delete_publisher (&self->amshmem_pub);
    if (self->scm_server) {
      gst_amshmem_scm_server_free (self->scm_server);
      self->scm_server = NULL;
    }
    amshmem_sink_release_cavalry_mem (self);
    return FALSE;
  }

  self->free_frame_readcond = dds_create_readcondition (
      self->free_frame_sub.reader, DDS_ANY_STATE);
  if (self->free_frame_readcond < 0) {
    GST_ERROR_OBJECT (self, "dds_create_readcondition failed: %s",
        dds_strretcode (-self->free_frame_readcond));
    dds_delete (self->free_frame_waitset);
    self->free_frame_waitset = DDS_ENTITY_NIL;
    delete_subscriber (&self->free_frame_sub, FREE_FRAME_MSGTYPE);
    delete_publisher (&self->amshmem_pub);
    if (self->scm_server) {
      gst_amshmem_scm_server_free (self->scm_server);
      self->scm_server = NULL;
    }
    amshmem_sink_release_cavalry_mem (self);
    return FALSE;
  }

  if (dds_waitset_attach (self->free_frame_waitset,
          self->free_frame_readcond, 0) != DDS_RETCODE_OK) {
    GST_ERROR_OBJECT (self, "dds_waitset_attach failed");
    dds_delete (self->free_frame_readcond);
    self->free_frame_readcond = DDS_ENTITY_NIL;
    dds_delete (self->free_frame_waitset);
    self->free_frame_waitset = DDS_ENTITY_NIL;
    delete_subscriber (&self->free_frame_sub, FREE_FRAME_MSGTYPE);
    delete_publisher (&self->amshmem_pub);
    if (self->scm_server) {
      gst_amshmem_scm_server_free (self->scm_server);
      self->scm_server = NULL;
    }
    amshmem_sink_release_cavalry_mem (self);
    return FALSE;
  }

  self->free_frame_thread_run = TRUE;
  if (pthread_create (&self->free_frame_thread, NULL,
          gst_amshmem_sink_free_frame_thread, self) != 0) {
    GST_ERROR_OBJECT (self, "pthread_create FreeFrame thread failed");
    self->free_frame_thread_run = FALSE;
    dds_waitset_detach (self->free_frame_waitset, self->free_frame_readcond);
    dds_delete (self->free_frame_readcond);
    self->free_frame_readcond = DDS_ENTITY_NIL;
    dds_delete (self->free_frame_waitset);
    self->free_frame_waitset = DDS_ENTITY_NIL;
    delete_subscriber (&self->free_frame_sub, FREE_FRAME_MSGTYPE);
    delete_publisher (&self->amshmem_pub);
    if (self->scm_server) {
      gst_amshmem_scm_server_free (self->scm_server);
      self->scm_server = NULL;
    }
    amshmem_sink_release_cavalry_mem (self);
    return FALSE;
  }

  GST_DEBUG_OBJECT (self, "amshmem_sink CycloneDDS started");
  return TRUE;
}

static gboolean
gst_amshmem_sink_stop (GstBaseSink *sink)
{
  GstAmShMemSink *self = GST_AMSHMEM_SINK (sink);

  gst_amshmem_sink_clear_held (self);

  if (self->scm_server) {
    gst_amshmem_scm_server_free (self->scm_server);
    self->scm_server = NULL;
  }

  self->free_frame_thread_run = FALSE;
  if (self->free_frame_thread) {
    pthread_join (self->free_frame_thread, NULL);
    self->free_frame_thread = (pthread_t) 0;
  }

  if (self->free_frame_waitset > 0 && self->free_frame_readcond > 0) {
    dds_waitset_detach (self->free_frame_waitset, self->free_frame_readcond);
  }
  if (self->free_frame_readcond > 0) {
    dds_delete (self->free_frame_readcond);
    self->free_frame_readcond = DDS_ENTITY_NIL;
  }
  if (self->free_frame_waitset > 0) {
    dds_delete (self->free_frame_waitset);
    self->free_frame_waitset = DDS_ENTITY_NIL;
  }

  delete_subscriber (&self->free_frame_sub, FREE_FRAME_MSGTYPE);
  delete_publisher (&self->amshmem_pub);

  amshmem_sink_release_cavalry_mem (self);

  GST_DEBUG_OBJECT (self, "amshmem_sink CycloneDDS stopped");
  return TRUE;
}

static GstFlowReturn
gst_amshmem_sink_render (GstBaseSink *sink, GstBuffer *buf)
{
  GstAmShMemSink *self = GST_AMSHMEM_SINK (sink);
  GstBuffer *work = buf;
  GstBuffer *staged = NULL;
  int ret;

  if (self->implem_method != GST_AMSHMEM_IMPLEM_CYCLONEDDS) {
    GST_ERROR_OBJECT (self, "render called with non-cyclonedds implem-method");
    return GST_FLOW_ERROR;
  }

  if (!gst_amba_cavalry_buffer_has_meta (buf)) {
    GstVideoMeta *vm0 = gst_buffer_get_video_meta (buf);

    if (!vm0 || vm0->format != GST_VIDEO_FORMAT_NV12) {
      GST_ERROR_OBJECT (self,
          "expect NV12 + GstVideoMeta when upstream has no GstAmbaCavalryBufferMeta "
          "(software decode: videoconvert ! video/x-raw,format=NV12)");
      return GST_FLOW_ERROR;
    }
    if (gst_amshmem_sink_use_scm_pool (self)) {
      GST_ERROR_OBJECT (self,
          "shm-handshake-path (POOL_OFFSET) needs cavalry upstream; incompatible with plain NV12");
      return GST_FLOW_ERROR;
    }
    staged = gst_amshmem_sink_stage_nv12_to_cavalry (self, buf, vm0);
    if (!staged)
      return GST_FLOW_ERROR;
    work = staged;
  }

  if (!fill_am_shmem_msg_from_buffer (self, work, &self->amshmem_pub.am_shmem_msg)) {
    GST_ERROR_OBJECT (self, "fill_am_shmem_msg_from_buffer failed");
    if (staged)
      gst_buffer_unref (staged);
    return GST_FLOW_ERROR;
  }

  {
    AmShMem_Msg *pub = &self->amshmem_pub.am_shmem_msg;

    if (pub->data_format == AM_SHMEM_DATA_FORMAT_PHYS_NV12
        && gst_amba_cavalry_buffer_has_meta (work)) {
      gint cav_fd = gst_amba_cavalry_buffer_get_fd (work);
      gsize block_off = gst_amba_cavalry_buffer_get_block_offset (work);
      guint64 slab_phys = gst_amba_cavalry_buffer_get_slab_phys (work);
      guint al_h = (pub->height + 15u) & ~15u;
      gsize nv12_sz = (gsize) pub->pitch * (gsize) al_h * 3 / 2;

      if (cav_fd >= 0 && nv12_sz > 0) {
        if (cavalry_mem_sync_cache_mfd ((unsigned long) nv12_sz,
                (unsigned long) block_off, cav_fd, 0, 1) < 0) {
          GST_WARNING_OBJECT (self,
              "cavalry_mem_sync_cache_mfd (invalidate) failed for PHY publish");
        }
      } else if (staged != NULL && slab_phys != 0 && nv12_sz > 0
          && pub->phys_y_addr != 0) {
        /* CPU-filled staging only; hw-decoded slabs use fd invalidate path or HW coherency. */
        if (cavalry_mem_sync_cache ((unsigned long) nv12_sz,
                (unsigned long) pub->phys_y_addr, 1, 0) < 0) {
          GST_WARNING_OBJECT (self,
              "cavalry_mem_sync_cache (clean before DDS) failed for soft-staged PHY");
        }
      }
    }
  }

  gst_amshmem_sink_maybe_dump_nv12 (self, &self->amshmem_pub.am_shmem_msg, work);

  if (self->scm_server && !self->scm_pool_announced
      && self->shm_handshake_path && self->shm_handshake_path[0]
      && gst_amba_cavalry_buffer_has_meta (work)) {
    gint pfd = gst_amba_cavalry_buffer_get_fd (work);
    guint64 tot = self->shm_pool_total_bytes;

    if (tot == 0) {
      gsize bs = gst_amba_cavalry_buffer_get_block_size (work);
      tot = (guint64) bs * (guint64) GST_AMSHMEM_POOL_MAX_BUFFERS;
    }
    if (pfd >= 0 && tot > 0) {
      gst_amshmem_scm_server_set_pool (self->scm_server, pfd, tot);
      self->scm_pool_announced = TRUE;
    }
  }

  ret = run_publisher (&self->amshmem_pub, AM_SHMEM_MSGTYPE);
  if (ret != 0) {
    GST_ERROR_OBJECT (self, "run_publisher AmShMem failed");
    if (staged)
      gst_buffer_unref (staged);
    return GST_FLOW_ERROR;
  }

//   g_print("[pub amshmem_msg]: %p\n", &self->amshmem_pub.am_shmem_msg);
//   g_print("->dec_id: %d\n", self->amshmem_pub.am_shmem_msg.dec_id);
//   g_print("->frame_id: %d\n", self->amshmem_pub.am_shmem_msg.frame_id);
//   g_print("->buffer_index: %d\n", self->amshmem_pub.am_shmem_msg.buffer_index);
//   g_print("->w: %d, h: %d, pitch: %d\n",
//     self->amshmem_pub.am_shmem_msg.width, self->amshmem_pub.am_shmem_msg.height,
//     self->amshmem_pub.am_shmem_msg.pitch);
//   g_print("->phys_y_addr: %ld\n", self->amshmem_pub.am_shmem_msg.phys_y_addr);
//   g_print("->phys_uv_addr: %ld\n", self->amshmem_pub.am_shmem_msg.phys_uv_addr);

  {
    AmShMem_Msg *pub = &self->amshmem_pub.am_shmem_msg;
    guint idx = (guint) pub->buffer_index;

    if (idx >= GST_AMSHMEM_POOL_MAX_BUFFERS) {
      GST_ERROR_OBJECT (self, "buffer_index %" G_GUINT32_FORMAT " out of range",
          pub->buffer_index);
      if (staged)
        gst_buffer_unref (staged);
      return GST_FLOW_ERROR;
    }

    gst_buffer_ref (work);

    g_mutex_lock (&self->held_lock);
    if (self->held_buffers[idx]) {
      GST_WARNING_OBJECT (self, "Replacing held buffer at index %u", idx);
      gst_buffer_unref (self->held_buffers[idx]);
    }
    self->held_buffers[idx] = work;
    g_mutex_unlock (&self->held_lock);
  }

  if (staged)
    gst_buffer_unref (staged);

  return GST_FLOW_OK;
}
