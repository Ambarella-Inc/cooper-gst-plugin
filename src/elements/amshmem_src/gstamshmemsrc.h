/*
 * gstamshmemsrc.h
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
 * Subscribes to AmShMem_Msg on CycloneDDS (implem_method=cyclonedds) and pushes
 * buffers. Peers may send PHYS_NV12 (CPU physical; mapped via /dev/mem on Linux)
 * or POOL_OFFSET_NV12 (requires shm-handshake-path and SCM pool fd).
 *
 * Uses a negotiated GstBufferPool; when downstream returns buffers to the pool,
 * release_buffer publishes FreeFrame_Msg on DDS. For PHYS_NV12 (/dev/mem mmap)
 * buffers (not from the pool), FreeFrame_Msg is published when the GstBuffer is
 * finally unreffed (e.g. after compositor copies), via gst_element_call_async.
 *
 * CycloneDDS: domain-id, amshmem-topic, and free-frame-topic must match the
 * paired amshmem_sink. Use distinct free-frame-topic per stream on the same domain.
 */


#ifndef __GST_AMSHMEM_SRC_H__
#define __GST_AMSHMEM_SRC_H__

#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>
#include <glib.h>
#include "gstamshmemtypes.h"
#include "dds/dds.h"
#include "msg_type.h"
#include "include/data_subscriber.h"
#include "include/data_publisher.h"

G_BEGIN_DECLS

#define GST_TYPE_AMSHMEM_SRC (gst_amshmem_src_get_type ())
#define GST_AMSHMEM_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_AMSHMEM_SRC, GstAmShMemSrc))
#define GST_AMSHMEM_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_AMSHMEM_SRC, GstAmShMemSrcClass))
#define GST_IS_AMSHMEM_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_AMSHMEM_SRC))
#define GST_IS_AMSHMEM_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_AMSHMEM_SRC))

typedef struct _GstAmShMemSrc GstAmShMemSrc;
typedef struct _GstAmShMemSrcClass GstAmShMemSrcClass;

struct _GstAmShMemSrc {
  GstPushSrc parent;

  GstAmShMemImplemMethod implem_method;
  guint domain_id;
  gchar *amshmem_topic;
  gchar *free_frame_topic;

  dds_subscriber_ctx amshmem_sub;
  dds_publisher_ctx free_frame_pub;

  dds_entity_t amshmem_waitset;
  dds_entity_t amshmem_readcond;
  dds_attach_t amshmem_wsresults[1];

  gchar *shm_handshake_path;
  gchar *dump_nv12_dir;
  gint dump_nv12_frame_id;
  guint dump_nv12_num_frames;
  guint8 *shm_mmap;
  gsize shm_mmap_len;
  GMutex shm_mtx;
  GCond shm_cnd;
  GThread *shm_thread;
  gboolean shm_ready;
  gboolean shm_thread_failed;
  guint pool_slot_counter;
};

struct _GstAmShMemSrcClass {
  GstPushSrcClass parent_class;
};

GType gst_amshmem_src_get_type (void);

G_END_DECLS

#endif /* __GST_AMSHMEM_SRC_H__ */
