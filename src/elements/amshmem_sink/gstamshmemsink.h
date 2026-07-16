  /*
 * gstamshmemsink.h
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
 * Publishes AmShMem_Msg over CycloneDDS when implem_method=cyclonedds.
 * Subscribes FreeFrame_Msg and releases held GstBuffer refs (pool backpressure).
 *
 * Default (empty shm-handshake-path): NV12 with GstAmbaCavalryBufferMeta is
 * published as PHYS_NV12. Software NV12 (no Cavalry meta) is copied into
 * cavalry_mem_alloc_with_attr (share_to_dsp=0) then published the same way.
 * Non-empty shm-handshake path:
 * POOL_OFFSET_NV12 plus SCM_RIGHTS pool fd for legacy same-host consumers.
 *
 * Upstream sink pad accepts any caps (GST_STATIC_CAPS_ANY). Buffer metadata
 * for AmShMem_Msg (video size, phys addresses, etc.) should be supplied via
 * GstVideoMeta and/or negotiated caps when possible.
 *
 * CycloneDDS: domain-id, amshmem-topic, and free-frame-topic must match the
 * paired amshmem_src. Use distinct free-frame-topic per stream on the same domain.
 */


#ifndef __GST_AMSHMEM_SINK_H__
#define __GST_AMSHMEM_SINK_H__

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>
#include <pthread.h>
#include "gstamshmemtypes.h"
#include "dds/dds.h"
#include "msg_type.h"
#include "include/data_publisher.h"
#include "include/data_subscriber.h"
#include "gstamshmemcommonslot.h"
#include "gst_amshmem_scm.h"

G_BEGIN_DECLS

#define GST_TYPE_AMSHMEM_SINK (gst_amshmem_sink_get_type ())
#define GST_AMSHMEM_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_AMSHMEM_SINK, GstAmShMemSink))
#define GST_AMSHMEM_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_AMSHMEM_SINK, GstAmShMemSinkClass))
#define GST_IS_AMSHMEM_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_AMSHMEM_SINK))
#define GST_IS_AMSHMEM_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_AMSHMEM_SINK))

typedef struct _GstAmShMemSink GstAmShMemSink;
typedef struct _GstAmShMemSinkClass GstAmShMemSinkClass;

struct _GstAmShMemSink {
  GstBaseSink parent;

  GstAmShMemImplemMethod implem_method;
  guint domain_id;
  gchar *amshmem_topic;
  gchar *free_frame_topic;
  guint8 dec_id;
  guint8 slave_id;

  dds_publisher_ctx amshmem_pub;
  dds_subscriber_ctx free_frame_sub;

  dds_entity_t free_frame_waitset;
  dds_entity_t free_frame_readcond;
  dds_attach_t free_frame_wsresults[1];

  volatile gboolean free_frame_thread_run;
  pthread_t free_frame_thread;

  guint32 frame_id_seq;
  guint pool_slot_counter;
  GMutex free_frame_mutex;

  GMutex held_lock;
  GstBuffer *held_buffers[GST_AMSHMEM_POOL_MAX_BUFFERS];

  gchar *shm_handshake_path;
  guint64 shm_pool_total_bytes;
  GstAmShMemScmServer *scm_server;
  gboolean scm_pool_announced;

  gchar *dump_nv12_dir;
  gint dump_nv12_frame_id;
  guint dump_nv12_num_frames;
};

struct _GstAmShMemSinkClass {
  GstBaseSinkClass parent_class;
};

GType gst_amshmem_sink_get_type (void);

G_END_DECLS

#endif /* __GST_AMSHMEM_SINK_H__ */
