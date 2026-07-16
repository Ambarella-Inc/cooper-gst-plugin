/*
 * gstambaheicfilesink.c
 *
 * History:
 *    6/11/2022 - [Zhi He] created file
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
 * SECTION:element-amba_venccap
 * @title: amba_venccap
 * @see_also: amba_vencdemux
 *
 * This element reads encoded video bit-stream from Ambarella platform.
 *
 * ## Example pipelines, single channel for h265 -> mp4 + heic
 * |[
 * gst-launch-1.0 -v -e amba_venccap ! amba_vencdemux heic-mode=1 heic-period=10 name=dm dm.stream0 ! queue ! h265parse ! mp4mux ! filesink location=h265.mp4 dm.heic_src ! amba_heicfilesink filename-base=/tmp/amba_%06d.heic
 * ]|
 *  Read h265 encoded bit-stream and recorded into mp4, and with a heic file sink
 *
 */

#include "common_err_code_c.h"

#include "debug_log.h"

#include "internal.h"

#include "element_common.h"

#include "gstambaheicfilesink.h"

GST_DEBUG_CATEGORY_STATIC (amba_heicfilesink_debug);
#define GST_CAT_DEFAULT amba_heicfilesink_debug

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

enum
{
  PROP_0,
  PROP_FILENAME_BASE,
  PROP_LAST
};

static void gst_amba_heicfilesink_dispose (GObject * object);

static void gst_amba_heicfilesink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_amba_heicfilesink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_amba_heicfilesink_start (GstBaseSink * sink);
static gboolean gst_amba_heicfilesink_stop (GstBaseSink * sink);
static gboolean gst_amba_heicfilesink_event (GstBaseSink * sink, GstEvent * event);
static GstFlowReturn gst_amba_heicfilesink_render (GstBaseSink * sink,
    GstBuffer * buffer);
static gboolean gst_amba_heicfilesink_unlock (GstBaseSink * sink);
static gboolean gst_amba_heicfilesink_unlock_stop (GstBaseSink * sink);

static gboolean gst_amba_heicfilesink_query (GstBaseSink * bsink, GstQuery * query);

#define gst_amba_heicfilesink_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstAmbaHeicfilesink, gst_amba_heicfilesink, GST_TYPE_BASE_SINK,
  GST_DEBUG_CATEGORY_INIT(amba_heicfilesink_debug, "ambaheicfilesink", 0,
  "File sink for HEIC"));

static void
gst_amba_heicfilesink_class_init (GstAmbaHeicfilesinkClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GstBaseSinkClass *gstbasesink_class = GST_BASE_SINK_CLASS (klass);

  gobject_class->dispose = gst_amba_heicfilesink_dispose;

  gobject_class->set_property = gst_amba_heicfilesink_set_property;
  gobject_class->get_property = gst_amba_heicfilesink_get_property;

  g_object_class_install_property (gobject_class, PROP_FILENAME_BASE,
      g_param_spec_string ("filename-base", "Filename-Base",
          "Location of the file to write", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (gstelement_class,
      "Ambarella HEIC File Sink",
      "Sink/File", "Write HEIC to files",
      "Zhi He <zhe@ambarella.com>");
  gst_element_class_add_static_pad_template (gstelement_class, &sinktemplate);

  gstbasesink_class->start = GST_DEBUG_FUNCPTR (gst_amba_heicfilesink_start);
  gstbasesink_class->stop = GST_DEBUG_FUNCPTR (gst_amba_heicfilesink_stop);
  gstbasesink_class->query = GST_DEBUG_FUNCPTR (gst_amba_heicfilesink_query);
  gstbasesink_class->render = GST_DEBUG_FUNCPTR (gst_amba_heicfilesink_render);
  gstbasesink_class->event = GST_DEBUG_FUNCPTR (gst_amba_heicfilesink_event);
  gstbasesink_class->unlock = GST_DEBUG_FUNCPTR (gst_amba_heicfilesink_unlock);
  gstbasesink_class->unlock_stop =
      GST_DEBUG_FUNCPTR (gst_amba_heicfilesink_unlock_stop);
}

static void
gst_amba_heicfilesink_init (GstAmbaHeicfilesink * thiz)
{
  thiz->filename_base = NULL;

  file_dumper_init(&thiz->file_dump);

  gst_base_sink_set_sync (GST_BASE_SINK (thiz), FALSE);
}

static void
gst_amba_heicfilesink_dispose (GObject * object)
{
  GstAmbaHeicfilesink *thiz = GST_AMBAHEICFILESINK_CAST (object);

  G_OBJECT_CLASS (parent_class)->dispose (object);

  if (thiz->filename_base) {
    g_free(thiz->filename_base);
    thiz->filename_base = NULL;
  }
  file_dumper_deinit(&thiz->file_dump);
}

static void
gst_amba_heicfilesink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAmbaHeicfilesink * thiz = GST_AMBAHEICFILESINK_CAST (object);

  switch (prop_id) {
    case PROP_FILENAME_BASE:
      if (thiz->filename_base) {
        g_free(thiz->filename_base);
        thiz->filename_base = NULL;
      }
      thiz->filename_base = g_strdup(g_value_get_string (value));
      if (thiz->file_dump.filename_base) {
        free(thiz->file_dump.filename_base);
        thiz->file_dump.filename_base = NULL;
      }
      thiz->file_dump.filename_base = strdup (thiz->filename_base);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_amba_heicfilesink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAmbaHeicfilesink * thiz = GST_AMBAHEICFILESINK_CAST (object);

  switch (prop_id) {
    case PROP_FILENAME_BASE:
      g_value_set_string (value, thiz->filename_base);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_amba_heicfilesink_query (GstBaseSink * bsink, GstQuery * query)
{
  gboolean res;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_FORMATS:
      gst_query_set_formats (query, 2, GST_FORMAT_DEFAULT, GST_FORMAT_BYTES);
      res = TRUE;
      break;

    default:
      res = GST_BASE_SINK_CLASS (parent_class)->query (bsink, query);
      break;
  }
  return res;
}

/* handle events (search) */
static gboolean
gst_amba_heicfilesink_event (GstBaseSink * sink, GstEvent * event)
{
  GstEventType type;

  type = GST_EVENT_TYPE (event);

  switch (type) {
    default:
      break;
  }

  return GST_BASE_SINK_CLASS (parent_class)->event (sink, event);
}

static GstFlowReturn
gst_amba_heicfilesink_render (GstBaseSink * sink, GstBuffer * buffer)
{
  GstAmbaHeicfilesink * thiz;
  int ret;

  unsigned int is_frame_end = 0;

  thiz = GST_AMBAHEICFILESINK_CAST (sink);

  if (gst_buffer_has_flags(buffer, GST_BUFFER_FLAG_MARKER)) {
    is_frame_end = 1;
  }
  gst_buffer_unset_flags(buffer, GST_BUFFER_FLAG_MARKER);

  ret = file_dumper_write_from_buffer_v3 (&thiz->file_dump, (void *) buffer,
      is_frame_end);
  if (ret) {
      DPRINT_ERROR("write file (%s) from buffer failed, ret %d\n",
        thiz->file_dump.p_filename_buf, ret);
      return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}

static gboolean
gst_amba_heicfilesink_start (GstBaseSink * basesink)
{
  DUNUSED(basesink);
  return TRUE;
}

static gboolean
gst_amba_heicfilesink_stop (GstBaseSink * basesink)
{
  DUNUSED(basesink);
  return TRUE;
}

static gboolean
gst_amba_heicfilesink_unlock (GstBaseSink * basesink)
{
  DUNUSED(basesink);
  return TRUE;
}

static gboolean
gst_amba_heicfilesink_unlock_stop (GstBaseSink * basesink)
{
  DUNUSED(basesink);
  return TRUE;
}

