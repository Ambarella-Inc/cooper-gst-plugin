/*
 * gstambavencdemux.h
 *
 * History:
 *    6/3/2022 - [Zhi He] created file
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
 * SECTION: element-amba_vencdemux
 * @title: amba_amba_vencdemux
 *
 * amba_vencdemux can be used to demux encoded video bitstreams from Amba device.
 *
 */

#ifndef __GST_AMBAVENCDEMUX_H__
#define __GST_AMBAVENCDEMUX_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>


#include "stdio.h"

#include "file_dumper.h"

G_BEGIN_DECLS

#define GST_TYPE_AMBAVENCDEMUX \
  (gst_ambavencdemux_get_type())
#define GST_AMBAVENCDEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AMBAVENCDEMUX,GstAmbaVencdemux))
#define GST_AMBAVENCDEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AMBAVENCDEMUX,GstAmbaVencdemuxClass))
#define GST_IS_AMBAVENCDEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AMBAVENCDEMUX))
#define GST_IS_AMBAVENCDEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AMBAVENCDEMUX))

typedef struct _GstAmbaVencdemux GstAmbaVencdemux;
typedef struct _GstAmbaVencdemuxClass GstAmbaVencdemuxClass;

struct _GstAmbaVencdemux
{
  GstElement  element;

  GstSegment *segment[IAV_STREAM_MAX_NUM_ALL];

  GstPad  *sink_pad;
  GstPad  *src_pads[IAV_STREAM_MAX_NUM_ALL];

  GstPad  *heic_src_pad;

  // for heic
  guint heic_cur_idr[IAV_STREAM_MAX_NUM_ALL];

  guint heic_mode;

  guint heic_capture_id;
  gchar heic_capture_id_str[128];
  gchar heic_capture_close_id_str[128];

  // heic local dump
  gchar * filename_base;
  file_dump_t file_dump;

  /* video state */
  GstVideoInfo info[IAV_STREAM_MAX_NUM_ALL]; /* protected by the object or stream lock */
  GstClockID clock_id;
  gboolean flushing;

  gboolean sync;

  GCond blocked_cond;
  gboolean blocked;
  GstClockTimeDiff  ts_offset;
  gboolean sync_to_first;
  gboolean is_first;

  GstClockTime   upstream_latency[IAV_STREAM_MAX_NUM_ALL];
#if 0
  /* flowreturn when srcpad is paused */
  GstFlowReturn srcresult;
  GstFlowReturn sinkresult;
  gboolean unexpected;

  GMutex qlock;                /* lock for queue (vs object lock) */
  gboolean waiting_add;
  GCond item_add;              /* signals buffers now available for reading */
  guint64 waiting_offset;
  gboolean waiting_del;
  GCond item_del;      /* signals space now available for writing */
#endif
};

struct _GstAmbaVencdemuxClass
{
  GstElementClass parent_class;
};

GType gst_ambavencdemux_get_type (void);

G_END_DECLS

#endif /* __GST_AMBADEMUX_H__ */

