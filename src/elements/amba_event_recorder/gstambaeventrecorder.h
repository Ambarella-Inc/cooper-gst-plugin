/*
 * gstambaeventrecorder.h
 *
 * History:
 *    03/26/2026 - [Yang Yu] created file
 *
 * Copyright (C) 2026 Ambarella International LP
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

#ifndef __GST_AMBA_EVENT_RECORDER_H__
#define __GST_AMBA_EVENT_RECORDER_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_AMBA_EVENT_RECORDER (gst_amba_event_recorder_get_type())
#define GST_AMBA_EVENT_RECORDER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_AMBA_EVENT_RECORDER, GstAmbaEventRecorder))
#define GST_AMBA_EVENT_RECORDER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_AMBA_EVENT_RECORDER, GstAmbaEventRecorderClass))
#define GST_IS_AMBA_EVENT_RECORDER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_AMBA_EVENT_RECORDER))
#define GST_IS_AMBA_EVENT_RECORDER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_AMBA_EVENT_RECORDER))

typedef struct _GstAmbaEventRecorder GstAmbaEventRecorder;
typedef struct _GstAmbaEventRecorderClass GstAmbaEventRecorderClass;

struct _GstAmbaEventRecorder {
  GstBin parent;
};

struct _GstAmbaEventRecorderClass {
  GstBinClass parent_class;
};

GType gst_amba_event_recorder_get_type (void);

G_END_DECLS

#endif /* __GST_AMBA_EVENT_RECORDER_H__ */
