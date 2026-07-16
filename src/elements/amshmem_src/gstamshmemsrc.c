/*
 * gstamshmemsrc.c
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
 * SECTION: element-amshmem_src
 * @title: amshmem_src
 *
 *
 * ## Example pipelines
 * |[
 * gst-launch-1.0 -e amshmem_src implem-method=cyclonedds ! queue ! amshmem_element_b
 * ]|
 *
 * FreeFrame_Msg: pool buffers from release_buffer; PHYS_NV12 mmap buffers when the
 * GstBuffer is finally unreffed (e.g. compositor finished with the frame).
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>

#if defined(__linux__)
#include <sys/mman.h>
#include <unistd.h>
#endif

#include <gst/gst.h>
#include <gst/base/gstbasesrc.h>
#include <gst/video/video.h>

#include "dds_msgs/AmShMem_Msg.h"
#include "dds_msgs/FreeFrame_Msg.h"
#include "gst_amba_cavalry_bufferpool.h"
#include "gst_amshmem_phys_mmap.h"
#include "gst_amshmem_scm.h"
#include "gstamshmemcommonslot.h"
#include "gstamshmemsrc.h"
#include "gstamshmemsrcbufferpool.h"
#include "gstamshmemtypes.h"

GST_DEBUG_CATEGORY_STATIC (gst_amshmem_src_debug);
#define GST_CAT_DEFAULT gst_amshmem_src_debug

#define DEFAULT_AMSHMEM_TOPIC "AmShMem_Msg"
#define DEFAULT_FREE_FRAME_TOPIC "FreeFrame_Msg"
#define DEFAULT_DOMAIN_ID 0
#define DEFAULT_DUMP_NV12_FRAME_ID 0
#define DEFAULT_DUMP_NV12_NUM_FRAMES 10

enum {
  PROP_0,
  PROP_IMPLEM_METHOD,
  PROP_AMSHMEM_TOPIC,
  PROP_FREE_FRAME_TOPIC,
  PROP_DOMAIN_ID,
  PROP_SHM_HANDSHAKE_PATH,
  PROP_DUMP_NV12_DIR,
  PROP_DUMP_NV12_FRAME_ID,
  PROP_DUMP_NV12_NUM_FRAMES,
  PROP_LAST
};

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (
        GST_AMSHMEM_MSG_CAPS "; "
        "video/x-raw, format=(string)NV12, width=(int)[1,8192], height=(int)[1,8192]"));

#define gst_amshmem_src_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstAmShMemSrc, gst_amshmem_src, GST_TYPE_PUSH_SRC,
    GST_DEBUG_CATEGORY_INIT (gst_amshmem_src_debug, "amshmem_src", 0,
        "AmShMem CycloneDDS source"));

static void gst_amshmem_src_finalize (GObject *object);
static gboolean gst_amshmem_src_start (GstBaseSrc *src);
static gboolean gst_amshmem_src_stop (GstBaseSrc *src);
static GstFlowReturn gst_amshmem_src_create (GstPushSrc *psrc, GstBuffer **outbuf);
static gboolean gst_amshmem_src_decide_allocation (GstBaseSrc *bsrc,
    GstQuery *query);
static void gst_amshmem_src_set_property (GObject *object, guint prop_id,
    const GValue *value, GParamSpec *pspec);
static void gst_amshmem_src_get_property (GObject *object, guint prop_id,
    GValue *value, GParamSpec *pspec);

static gpointer gst_amshmem_src_shm_thread (gpointer data);

typedef struct {
  GstAmShMemSrc *src;
  AmShMem_Msg msg;
} GstAmShMemSrcPhysFreeJob;

static void gst_amshmem_src_phys_buffer_attach_freeframe_notify (GstAmShMemSrc * self,
    GstBuffer * buf, const AmShMem_Msg * msg);

static gboolean
gst_amshmem_src_use_scm_pool (const GstAmShMemSrc * self)
{
  return self->shm_handshake_path && self->shm_handshake_path[0] != '\0';
}

static void
gst_amshmem_src_maybe_dump_nv12 (GstAmShMemSrc * self, const AmShMem_Msg * pub,
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
  path = g_strdup_printf ("%s/src_%05" PRIu32 ".nv12", self->dump_nv12_dir,
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

static void
gst_amshmem_src_class_init (GstAmShMemSrcClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GstBaseSrcClass *gstbasesrc_class = GST_BASE_SRC_CLASS (klass);
  GstPushSrcClass *gstpushsrc_class = GST_PUSH_SRC_CLASS (klass);

  gobject_class->finalize = gst_amshmem_src_finalize;
  gobject_class->set_property = gst_amshmem_src_set_property;
  gobject_class->get_property = gst_amshmem_src_get_property;

  g_object_class_install_property (gobject_class, PROP_IMPLEM_METHOD,
      g_param_spec_enum ("implem-method", "Implementation method",
          "Transport: socket, cyclonedds, or fastdds (only cyclonedds implemented)",
          GST_TYPE_AMSHMEM_IMPLEM_METHOD, GST_AMSHMEM_IMPLEM_CYCLONEDDS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_AMSHMEM_TOPIC,
      g_param_spec_string ("amshmem-topic", "AmShMem topic",
          "CycloneDDS topic name for AmShMem_Msg (must match paired amshmem_sink).",
          DEFAULT_AMSHMEM_TOPIC,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_FREE_FRAME_TOPIC,
      g_param_spec_string ("free-frame-topic", "FreeFrame topic",
          "CycloneDDS topic name for FreeFrame_Msg (must match paired amshmem_sink).",
          DEFAULT_FREE_FRAME_TOPIC,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DOMAIN_ID,
      g_param_spec_uint ("domain-id", "Domain ID",
          "CycloneDDS domain id for this element's participants (must match paired amshmem_sink).",
          0, 230, DEFAULT_DOMAIN_ID,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SHM_HANDSHAKE_PATH,
      g_param_spec_string ("shm-handshake-path", "SHM handshake Unix path",
          "Empty (default): peer sends PHYS_NV12 (CPU physical); no SCM. Non-empty: "
          "receive pool fd via SCM_RIGHTS for POOL_OFFSET_NV12 (must match amshmem_sink).",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DUMP_NV12_DIR,
      g_param_spec_string ("dump-nv12-dir", "Dump NV12 directory",
          "If set, write src_%05u.nv12 for debugging; empty disables.",
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

  gst_element_class_add_static_pad_template (gstelement_class, &src_factory);
  gst_element_class_set_static_metadata (gstelement_class,
      "AmShMem CycloneDDS source",
      "Source/Network",
      "Subscribes AmShMem_Msg; publishes FreeFrame_Msg when pool buffers are released",
      "Da-shun Pei <dspei@ambarella.com>");

  g_type_ensure (gst_amshmem_src_buffer_pool_get_type ());

  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_amshmem_src_start);
  gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_amshmem_src_stop);
  gstbasesrc_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_amshmem_src_decide_allocation);
  gstpushsrc_class->create = GST_DEBUG_FUNCPTR (gst_amshmem_src_create);
}

static void
gst_amshmem_src_init (GstAmShMemSrc *self)
{
  self->implem_method = GST_AMSHMEM_IMPLEM_CYCLONEDDS;
  self->amshmem_topic = g_strdup (DEFAULT_AMSHMEM_TOPIC);
  self->free_frame_topic = g_strdup (DEFAULT_FREE_FRAME_TOPIC);
  self->domain_id = DEFAULT_DOMAIN_ID;

  memset (&self->amshmem_sub, 0, sizeof (self->amshmem_sub));
  memset (&self->free_frame_pub, 0, sizeof (self->free_frame_pub));

  self->amshmem_waitset = DDS_ENTITY_NIL;
  self->amshmem_readcond = DDS_ENTITY_NIL;

  gst_base_src_set_live (GST_BASE_SRC (self), TRUE);
  gst_base_src_set_format (GST_BASE_SRC (self), GST_FORMAT_TIME);

  self->shm_handshake_path = NULL;
  self->dump_nv12_dir = NULL;
  self->dump_nv12_frame_id = DEFAULT_DUMP_NV12_FRAME_ID;
  self->dump_nv12_num_frames = DEFAULT_DUMP_NV12_NUM_FRAMES;
  self->shm_mmap = NULL;
  self->shm_mmap_len = 0;
  self->shm_thread = NULL;
  self->shm_ready = FALSE;
  self->shm_thread_failed = FALSE;
  self->pool_slot_counter = 0;
  g_mutex_init (&self->shm_mtx);
  g_cond_init (&self->shm_cnd);
}

static void
gst_amshmem_src_finalize (GObject *object)
{
  GstAmShMemSrc *self = GST_AMSHMEM_SRC (object);

  g_free (self->amshmem_topic);
  self->amshmem_topic = NULL;
  g_free (self->free_frame_topic);
  self->free_frame_topic = NULL;
  g_free (self->shm_handshake_path);
  self->shm_handshake_path = NULL;
  g_free (self->dump_nv12_dir);
  self->dump_nv12_dir = NULL;

  g_mutex_clear (&self->shm_mtx);
  g_cond_clear (&self->shm_cnd);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_amshmem_src_set_property (GObject *object, guint prop_id,
    const GValue *value, GParamSpec *pspec)
{
  GstAmShMemSrc *self = GST_AMSHMEM_SRC (object);

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
    case PROP_SHM_HANDSHAKE_PATH:
      g_free (self->shm_handshake_path);
      self->shm_handshake_path = g_value_dup_string (value);
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
gst_amshmem_src_get_property (GObject *object, guint prop_id,
    GValue *value, GParamSpec *pspec)
{
  GstAmShMemSrc *self = GST_AMSHMEM_SRC (object);

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
    case PROP_SHM_HANDSHAKE_PATH:
      g_value_set_string (value, self->shm_handshake_path);
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

static gpointer
gst_amshmem_src_shm_thread (gpointer data)
{
  GstAmShMemSrc *self = GST_AMSHMEM_SRC (data);
  gint fd = -1;
  guint64 sz = 0;

#if !defined(__linux__)
  (void) fd;
  (void) sz;
  g_mutex_lock (&self->shm_mtx);
  self->shm_thread_failed = TRUE;
  self->shm_ready = TRUE;
  g_cond_broadcast (&self->shm_cnd);
  g_mutex_unlock (&self->shm_mtx);
  return NULL;
#else
  if (!gst_amshmem_scm_client_recv_pool (self->shm_handshake_path, &fd, &sz)) {
    GST_ERROR_OBJECT (self, "SCM recv on %s failed", self->shm_handshake_path);
    g_mutex_lock (&self->shm_mtx);
    self->shm_thread_failed = TRUE;
    self->shm_ready = TRUE;
    g_cond_broadcast (&self->shm_cnd);
    g_mutex_unlock (&self->shm_mtx);
    return NULL;
  }

  self->shm_mmap =
      (guint8 *) mmap (NULL, (size_t) sz, PROT_READ, MAP_SHARED, fd, 0);
  close (fd);
  if (self->shm_mmap == MAP_FAILED) {
    self->shm_mmap = NULL;
    GST_ERROR_OBJECT (self, "mmap %" G_GUINT64_FORMAT " bytes failed", sz);
    g_mutex_lock (&self->shm_mtx);
    self->shm_thread_failed = TRUE;
    self->shm_ready = TRUE;
    g_cond_broadcast (&self->shm_cnd);
    g_mutex_unlock (&self->shm_mtx);
    return NULL;
  }

  self->shm_mmap_len = (gsize) sz;
  g_mutex_lock (&self->shm_mtx);
  self->shm_thread_failed = FALSE;
  self->shm_ready = TRUE;
  g_cond_broadcast (&self->shm_cnd);
  g_mutex_unlock (&self->shm_mtx);
  return NULL;
#endif
}

static GQuark
gst_amshmem_src_phys_free_job_quark (void)
{
  static gsize once = 0;
  static GQuark q;

  if (g_once_init_enter (&once)) {
    q = g_quark_from_static_string ("amshmem.src.phys-free-async");
    g_once_init_leave (&once, 1);
  }
  return q;
}

static void
gst_amshmem_src_phys_free_job_async_done (gpointer user_data)
{
  GstAmShMemSrcPhysFreeJob *job = user_data;

  gst_object_unref (job->src);
  g_free (job);
}

static void
gst_amshmem_src_phys_freeframe_publish (GstElement * element, gpointer user_data)
{
  GstAmShMemSrc *self = GST_AMSHMEM_SRC (element);
  GstAmShMemSrcPhysFreeJob *job = user_data;
  const AmShMem_Msg *m = &job->msg;
  FreeFrame_Msg *fm;

  if (self->implem_method != GST_AMSHMEM_IMPLEM_CYCLONEDDS)
    return;

  fm = &self->free_frame_pub.free_frame_msg;
  memset (fm, 0, sizeof (*fm));
  fm->msg_id = m->dec_id;
  fm->frame_id = m->frame_id;
  fm->buffer_index = m->buffer_index;
  fm->phys_y_addr = m->phys_y_addr;
  fm->phys_uv_addr = m->phys_uv_addr;
  fm->playback_pts = m->playback_pts;
  fm->system_time = (uint64_t) g_get_real_time ();

  if (run_publisher (&self->free_frame_pub, FREE_FRAME_MSGTYPE) != 0)
    GST_WARNING_OBJECT (self, "run_publisher FreeFrame (phys) failed");
  /* FreeFrame dds_write diagnostics: data_publisher.cpp (run_publisher). */
}

static void
gst_amshmem_src_phys_free_job_qdata_free (gpointer data)
{
  GstAmShMemSrcPhysFreeJob *job = data;

  /* Same pattern as amshmem_sink: DDS / element work off the streaming thread. */
  gst_element_call_async (GST_ELEMENT (job->src),
      gst_amshmem_src_phys_freeframe_publish, job,
      gst_amshmem_src_phys_free_job_async_done);
}

static void
gst_amshmem_src_phys_buffer_attach_freeframe_notify (GstAmShMemSrc * self,
    GstBuffer * buf, const AmShMem_Msg * msg)
{
  GstAmShMemSrcPhysFreeJob *job;

  g_return_if_fail (GST_IS_AMSHMEM_SRC (self));
  g_return_if_fail (GST_IS_BUFFER (buf));
  g_return_if_fail (msg != NULL);

  job = g_new0 (GstAmShMemSrcPhysFreeJob, 1);
  job->src = gst_object_ref (self);
  memcpy (&job->msg, msg, sizeof (job->msg));
  gst_mini_object_set_qdata (GST_MINI_OBJECT (buf),
      gst_amshmem_src_phys_free_job_quark (), job,
      gst_amshmem_src_phys_free_job_qdata_free);
}

static gboolean
gst_amshmem_src_start (GstBaseSrc *src)
{
  GstAmShMemSrc *self = GST_AMSHMEM_SRC (src);

  GST_DEBUG_OBJECT (self, "[src] start: enter");
  fprintf (stderr, "[amshmem_src] start: enter self=%p\n", (void *) self);
  fflush (stderr);

  if (self->implem_method == GST_AMSHMEM_IMPLEM_SOCKET ||
      self->implem_method == GST_AMSHMEM_IMPLEM_FASTDDS) {
    GST_ERROR_OBJECT (self,
        "implem-method %d not implemented (only cyclonedds is supported)",
        (int) self->implem_method);
    return FALSE;
  }

  if (init_subscriber (&self->amshmem_sub, AM_SHMEM_MSGTYPE,
          (uint32_t) self->domain_id, self->amshmem_topic) != 0) {
    GST_ERROR_OBJECT (self, "init_subscriber AmShMem failed");
    return FALSE;
  }
  GST_DEBUG_OBJECT (self, "[src] start: init_subscriber AmShMem ok");

  self->amshmem_waitset = dds_create_waitset (self->amshmem_sub.participant);
  if (self->amshmem_waitset < 0) {
    GST_ERROR_OBJECT (self, "dds_create_waitset failed: %s",
        dds_strretcode (-self->amshmem_waitset));
    delete_subscriber (&self->amshmem_sub, AM_SHMEM_MSGTYPE);
    return FALSE;
  }

  self->amshmem_readcond = dds_create_readcondition (
      self->amshmem_sub.reader, DDS_ANY_STATE);
  if (self->amshmem_readcond < 0) {
    GST_ERROR_OBJECT (self, "dds_create_readcondition failed: %s",
        dds_strretcode (-self->amshmem_readcond));
    dds_delete (self->amshmem_waitset);
    self->amshmem_waitset = DDS_ENTITY_NIL;
    delete_subscriber (&self->amshmem_sub, AM_SHMEM_MSGTYPE);
    return FALSE;
  }

  if (dds_waitset_attach (self->amshmem_waitset,
          self->amshmem_readcond, 0) != DDS_RETCODE_OK) {
    GST_ERROR_OBJECT (self, "dds_waitset_attach failed");
    dds_delete (self->amshmem_readcond);
    self->amshmem_readcond = DDS_ENTITY_NIL;
    dds_delete (self->amshmem_waitset);
    self->amshmem_waitset = DDS_ENTITY_NIL;
    delete_subscriber (&self->amshmem_sub, AM_SHMEM_MSGTYPE);
    return FALSE;
  }
  GST_DEBUG_OBJECT (self, "[src] start: waitset + readcond ok");

  if (init_publisher (&self->free_frame_pub, FREE_FRAME_MSGTYPE,
          (uint32_t) self->domain_id, self->free_frame_topic) != 0) {
    GST_ERROR_OBJECT (self, "init_publisher FreeFrame failed");
    dds_waitset_detach (self->amshmem_waitset, self->amshmem_readcond);
    dds_delete (self->amshmem_readcond);
    self->amshmem_readcond = DDS_ENTITY_NIL;
    dds_delete (self->amshmem_waitset);
    self->amshmem_waitset = DDS_ENTITY_NIL;
    delete_subscriber (&self->amshmem_sub, AM_SHMEM_MSGTYPE);
    return FALSE;
  }
  GST_DEBUG_OBJECT (self, "[src] start: init_publisher FreeFrame ok");

  self->pool_slot_counter = 0;
  if (self->shm_handshake_path && self->shm_handshake_path[0]) {
    self->shm_ready = FALSE;
    self->shm_thread_failed = FALSE;
    self->shm_mmap = NULL;
    self->shm_mmap_len = 0;
    self->shm_thread = g_thread_new ("amshmem-scm-cli", gst_amshmem_src_shm_thread,
        self);
    if (!self->shm_thread) {
      GST_ERROR_OBJECT (self, "failed to spawn SCM recv thread");
      delete_publisher (&self->free_frame_pub);
      dds_waitset_detach (self->amshmem_waitset, self->amshmem_readcond);
      dds_delete (self->amshmem_readcond);
      self->amshmem_readcond = DDS_ENTITY_NIL;
      dds_delete (self->amshmem_waitset);
      self->amshmem_waitset = DDS_ENTITY_NIL;
      delete_subscriber (&self->amshmem_sub, AM_SHMEM_MSGTYPE);
      return FALSE;
    }
  } else {
    self->shm_ready = TRUE;
    self->shm_thread_failed = FALSE;
  }

  GST_DEBUG_OBJECT (self, "[src] start: leave ok (CycloneDDS ready)");
  fprintf (stderr, "[amshmem_src] start: leave ok\n");
  fflush (stderr);
  return TRUE;
}

static gboolean
gst_amshmem_src_stop (GstBaseSrc *src)
{
  GstAmShMemSrc *self = GST_AMSHMEM_SRC (src);

  GST_DEBUG_OBJECT (self, "[src] stop: enter");

  if (self->shm_thread) {
    g_thread_join (self->shm_thread);
    self->shm_thread = NULL;
  }
#if defined(__linux__)
  if (self->shm_mmap && self->shm_mmap_len > 0) {
    munmap (self->shm_mmap, self->shm_mmap_len);
    self->shm_mmap = NULL;
    self->shm_mmap_len = 0;
  }
#endif
  self->shm_ready = FALSE;

  delete_publisher (&self->free_frame_pub);

  if (self->amshmem_waitset > 0 && self->amshmem_readcond > 0) {
    dds_waitset_detach (self->amshmem_waitset, self->amshmem_readcond);
  }
  if (self->amshmem_readcond > 0) {
    dds_delete (self->amshmem_readcond);
    self->amshmem_readcond = DDS_ENTITY_NIL;
  }
  if (self->amshmem_waitset > 0) {
    dds_delete (self->amshmem_waitset);
    self->amshmem_waitset = DDS_ENTITY_NIL;
  }

  delete_subscriber (&self->amshmem_sub, AM_SHMEM_MSGTYPE);

  GST_DEBUG_OBJECT (self, "[src] stop: leave ok");
  return TRUE;
}

static gboolean
gst_amshmem_src_decide_allocation (GstBaseSrc *bsrc, GstQuery *query)
{
  GstAmShMemSrc *self = GST_AMSHMEM_SRC (bsrc);
  GstCaps *caps = NULL;
  GstBufferPool *pool;
  GstStructure *config;
  gsize size = sizeof (AmShMem_Msg);
  gboolean ret;

  (void) self;

  gst_query_parse_allocation (query, &caps, NULL);

  if (caps && !gst_caps_is_empty (caps)) {
    GstVideoInfo vi;
    if (gst_video_info_from_caps (&vi, caps)
        && GST_VIDEO_INFO_FORMAT (&vi) == GST_VIDEO_FORMAT_NV12)
      size = GST_VIDEO_INFO_SIZE (&vi);
  }

  pool = gst_amshmem_src_buffer_pool_new (GST_AMSHMEM_SRC (bsrc));
  if (!pool) {
    if (caps)
      gst_caps_unref (caps);
    return FALSE;
  }

  config = gst_buffer_pool_get_config (pool);
  if (!caps)
    caps = gst_pad_get_pad_template_caps (GST_BASE_SRC_PAD (bsrc));

  gst_buffer_pool_config_set_params (config, caps, size,
      GST_AMSHMEM_POOL_MAX_BUFFERS, GST_AMSHMEM_POOL_MAX_BUFFERS);
  {
    GstAllocator *alloc = gst_allocator_find (NULL);

    if (!alloc) {
      GST_ERROR_OBJECT (self, "gst_allocator_find (NULL) failed");
      gst_object_unref (pool);
      gst_caps_unref (caps);
      return FALSE;
    }
    gst_buffer_pool_config_set_allocator (config, alloc, NULL);
    gst_object_unref (alloc);
  }

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (self, "buffer pool set_config failed");
    gst_object_unref (pool);
    gst_caps_unref (caps);
    return FALSE;
  }

  gst_query_add_allocation_pool (query, pool, size,
      GST_AMSHMEM_POOL_MAX_BUFFERS, GST_AMSHMEM_POOL_MAX_BUFFERS);
  gst_object_unref (pool);
  gst_caps_unref (caps);

  ret = GST_BASE_SRC_CLASS (parent_class)->decide_allocation (bsrc, query);
  return ret;
}

static GstFlowReturn
gst_amshmem_src_create (GstPushSrc *psrc, GstBuffer **outbuf)
{
  GstAmShMemSrc *self = GST_AMSHMEM_SRC (psrc);
  GstBaseSrc *basesrc = GST_BASE_SRC (psrc);
  dds_return_t ws_ret;
  int ret;
  GstBuffer *buf = NULL;
  GstBufferPool *pool = NULL;
  GstMapInfo map;
  GstFlowReturn fret;
  AmShMem_Msg msg_copy;
  gboolean shm_fail;

  if (self->shm_handshake_path && self->shm_handshake_path[0]) {
    g_mutex_lock (&self->shm_mtx);
    while (!self->shm_ready)
      g_cond_wait (&self->shm_cnd, &self->shm_mtx);
    shm_fail = self->shm_thread_failed;
    g_mutex_unlock (&self->shm_mtx);
    if (shm_fail || self->shm_mmap == NULL) {
      GST_ERROR_OBJECT (self, "SCM pool not ready (handshake path %s)",
          self->shm_handshake_path);
      return GST_FLOW_ERROR;
    }
  }

  while (TRUE) {
    GstFlowReturn flow;

    flow = gst_base_src_wait_playing (basesrc);
    if (G_UNLIKELY (flow != GST_FLOW_OK)) {
      return flow;
    }

    ws_ret = dds_waitset_wait (self->amshmem_waitset,
        self->amshmem_wsresults, 1, DDS_MSECS (30));

    if (ws_ret < 0) {
      GST_ERROR_OBJECT (self, "dds_waitset_wait: %s", dds_strretcode (-ws_ret));
      return GST_FLOW_ERROR;
    }

    if (ws_ret == 0)
      continue;

    ret = run_subscriber (&self->amshmem_sub, AM_SHMEM_MSGTYPE);
    if (ret < 0) {
      GST_ERROR_OBJECT (self, "run_subscriber AmShMem failed");
      return GST_FLOW_ERROR;
    }

    if (ret > 0 && self->amshmem_sub.am_shmem_msg) {
      break;
    }
  }

  memcpy (&msg_copy, self->amshmem_sub.am_shmem_msg, sizeof (msg_copy));

//   g_print ("[amshmemsrc sub] dec_id=%" PRIu8 " frame_id=%" PRIu32
//       " fmt=%" PRIu32 " phys_y=0x%" PRIx64 "\n", msg_copy.dec_id, msg_copy.frame_id,
//       msg_copy.data_format, (guint64) msg_copy.phys_y_addr);

  if (msg_copy.data_format == AM_SHMEM_DATA_FORMAT_POOL_OFFSET_NV12
      && !gst_amshmem_src_use_scm_pool (self)) {
    GST_ERROR_OBJECT (self,
        "Received POOL_OFFSET_NV12 but shm-handshake-path is not set");
    return GST_FLOW_ERROR;
  }

  if (msg_copy.data_format == AM_SHMEM_DATA_FORMAT_PHYS_NV12) {
#if defined(__linux__)
    if (!gst_amshmem_phys_mmap_wrap_nv12 (msg_copy.phys_y_addr,
            msg_copy.width, msg_copy.height, msg_copy.pitch, &buf)) {
      GST_ERROR_OBJECT (self,
          "phys mmap NV12 failed (phys_y=0x%" PRIx64 ")",
          (guint64) msg_copy.phys_y_addr);
      return GST_FLOW_ERROR;
    }
    /* Same PA convention as amba_hwvdecv2 + amshmem_sink (slab = PA of map.data / Y origin). */
    if (!gst_buffer_add_amba_cavalry_meta (buf, -1, 0, gst_buffer_get_size (buf), FALSE,
            -1, (guint64) msg_copy.phys_y_addr)) {
      GST_WARNING_OBJECT (self, "failed to add GstAmbaCavalryBufferMeta (slab phys)");
    }
    gst_amshmem_src_buffer_attach_nv12_side (buf, &msg_copy);
    gst_amshmem_src_phys_buffer_attach_freeframe_notify (self, buf, &msg_copy);
#else
    GST_ERROR_OBJECT (self, "PHYS_NV12 requires Linux /dev/mem mmap support");
    return GST_FLOW_ERROR;
#endif
  } else {
    pool = gst_base_src_get_buffer_pool (basesrc);
    if (pool) {
      fret = gst_buffer_pool_acquire_buffer (pool, &buf, NULL);
      if (fret != GST_FLOW_OK) {
        GST_DEBUG_OBJECT (self, "acquire_buffer: %s", gst_flow_get_name (fret));
        return fret;
      }
    } else {
      buf = gst_buffer_new_allocate (NULL, sizeof (AmShMem_Msg), NULL);
      if (!buf) {
        GST_ERROR_OBJECT (self, "gst_buffer_new_allocate failed");
        return GST_FLOW_ERROR;
      }
    }

    if (msg_copy.data_format == AM_SHMEM_DATA_FORMAT_POOL_OFFSET_NV12) {
      guint al_h = (msg_copy.height + 15) & ~15u;
      gsize nv12_sz = (gsize) msg_copy.pitch * (gsize) al_h * 3 / 2;
      gsize yoff = (gsize) msg_copy.phys_y_addr;
      GstMemory *mem;
      gsize off[GST_VIDEO_MAX_PLANES];
      gint str[GST_VIDEO_MAX_PLANES];

      if (self->shm_mmap == NULL || yoff + nv12_sz > self->shm_mmap_len) {
        GST_ERROR_OBJECT (self, "NV12 frame out of mmap (off=%" G_GSIZE_FORMAT
            " sz=%" G_GSIZE_FORMAT " pool=%" G_GSIZE_FORMAT ")", yoff, nv12_sz,
            self->shm_mmap_len);
        gst_buffer_unref (buf);
        return GST_FLOW_ERROR;
      }

      gst_buffer_remove_all_memory (buf);
      mem = gst_memory_new_wrapped (GST_MEMORY_FLAG_READONLY,
          self->shm_mmap + yoff, nv12_sz, 0, nv12_sz, NULL, NULL);
      gst_buffer_append_memory (buf, mem);

      memset (off, 0, sizeof (off));
      memset (str, 0, sizeof (str));
      off[0] = 0;
      off[1] = (gsize) msg_copy.pitch * (gsize) al_h;
      str[0] = (gint) msg_copy.pitch;
      str[1] = (gint) msg_copy.pitch;
      gst_buffer_add_video_meta_full (buf, GST_VIDEO_FRAME_FLAG_NONE,
          GST_VIDEO_FORMAT_NV12, msg_copy.width, msg_copy.height, 2, off, str);

      gst_amshmem_src_buffer_attach_nv12_side (buf, &msg_copy);
    } else {
      if (!gst_buffer_map (buf, &map, GST_MAP_WRITE)) {
        GST_ERROR_OBJECT (self, "gst_buffer_map (WRITE) failed");
        gst_buffer_unref (buf);
        return GST_FLOW_ERROR;
      }
      memcpy (map.data, &msg_copy, sizeof (AmShMem_Msg));
      gst_buffer_unmap (buf, &map);
      gst_amshmem_src_buffer_mark_dds_filled (buf);
    }
  }

  GST_BUFFER_PTS (buf) = msg_copy.playback_pts != 0
      ? (GstClockTime) msg_copy.playback_pts : GST_CLOCK_TIME_NONE;
  GST_BUFFER_DURATION (buf) = GST_CLOCK_TIME_NONE;

  gst_amshmem_src_maybe_dump_nv12 (self, &msg_copy, buf);

  GST_DEBUG_OBJECT (self, "pushed frame_id=%" PRIu32, msg_copy.frame_id);

  *outbuf = buf;
  return GST_FLOW_OK;
}
