/*
 * gstambavsink.c
 *
 * History:
 *    6/16/2022 - [Zhi He] created file
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
 * SECTION:element-amba_vsink
 * @title: amba_vsink
 *
 * ## Example launch lines
 * |[
 * gst-launch-1.0 -e amba_camsrc ! amba_vsink --gst-debug-level 3
 * ]|
 *
 */

#include "stdio.h"

#include <gst/video/video-format.h>
#include <gst/video/video-frame.h>

#include "common_err_code_c.h"

#include "internal.h"
#include "debug_log.h"

#include "gstambavsink.h"

#include "element_common.h"

#include "buffer_utils.h"

GST_DEBUG_CATEGORY_STATIC (gst_amba_vsink_debug);
#define GST_CAT_DEFAULT gst_amba_vsink_debug

enum
{
  PROP_0,
  PROP_DEBUG,
  PROP_LAST
};

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES ("ANY",
            GST_VIDEO_FORMATS_ALL)));

#define gst_amba_vsink_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstAmbaVsink, gst_amba_vsink, GST_TYPE_VIDEO_SINK,
    GST_DEBUG_CATEGORY_INIT (gst_amba_vsink_debug, "amba_vsink", 0,
        "amba_vsink") );

static GstFlowReturn gst_amba_vsink_show_frame (GstVideoSink * sink,
    GstBuffer * buffer);

static void
gst_amba_vsink_init (GstAmbaVsink * thiz)
{

  thiz->debug = 0;
}

static void
gst_amba_vsink_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstAmbaVsink * thiz = GST_AMBA_VSINK (object);

  switch (property_id) {
    case PROP_DEBUG:
      g_value_set_uint(value, thiz->debug);
      break;
    default: {
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }break;
  }
}

static void
gst_amba_vsink_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAmbaVsink * thiz = GST_AMBA_VSINK (object);

  switch (property_id) {
    case PROP_DEBUG:
      thiz->debug = g_value_get_uint (value);
      break;
    default: {
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
    break;
  }
}

static void gst_amba_vsink_finalize (GObject *gobject)
{

  G_OBJECT_CLASS (parent_class)->finalize (gobject);
}

static void
gst_amba_vsink_get_times (GstBaseSink * sink, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end)
{
  GstAmbaVsink *self = GST_AMBA_VSINK (sink);

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer)) {
    *start = GST_BUFFER_TIMESTAMP (buffer);
    if (GST_BUFFER_DURATION_IS_VALID (buffer)) {
      *end = *start + GST_BUFFER_DURATION (buffer);
    } else {
      if (self->info.fps_n > 0) {
        *end = *start +
            gst_util_uint64_scale_int (GST_SECOND, self->info.fps_d,
            self->info.fps_n);
      }
    }
  }
}

static void
gst_amba_vsink_class_init (GstAmbaVsinkClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstBaseSinkClass *base_sink_class = GST_BASE_SINK_CLASS (klass);
  GstVideoSinkClass *video_sink_class = GST_VIDEO_SINK_CLASS (klass);

  object_class->get_property = gst_amba_vsink_get_property;
  object_class->set_property = gst_amba_vsink_set_property;
  object_class->finalize = gst_amba_vsink_finalize;

  base_sink_class->get_times = GST_DEBUG_FUNCPTR (gst_amba_vsink_get_times);

  video_sink_class->show_frame = GST_DEBUG_FUNCPTR (gst_amba_vsink_show_frame);

  gst_element_class_add_static_pad_template (element_class, &sink_factory);
  gst_element_class_set_static_metadata (element_class, "Amba Video Sink",
      "Video/Sink",
      "Video Sink",
      "Zhi He <zhe@ambarella.com>");
}


/* chain function
 * this function does the actual processing
 */
static GstFlowReturn
gst_amba_vsink_show_frame (GstVideoSink * sink, GstBuffer * buffer)
{
  DUNUSED(sink);
  DUNUSED(buffer);

  GstFlowReturn ret = GST_FLOW_OK;

  return ret;
}

