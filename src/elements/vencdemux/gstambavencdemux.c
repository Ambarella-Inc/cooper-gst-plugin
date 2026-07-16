/*
 * gstambavencdemux.c
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
 * SECTION:element-amba_vencdemux
 * @title: amba_vencdemux
 * @see_also: amba_venccap
 *
 * This element demux encoded video bit-stream from Ambarella platform.
 *
 * ## Example pipelines, single channel for h264
 * |[
 * gst-launch-1.0 -v -e amba_venccap ! amba_vencdemux name=dm dm.stream0 ! queue ! h264parse ! mp4mux ! filesink location=h264.mp4
 * ]|
 *  Read h264 encoded bit-stream and recorded into mp4.
 *
 * ## Example pipelines, single channel for h265
 * |[
 * gst-launch-1.0 -v -e amba_venccap ! amba_vencdemux name=dm dm.stream0 ! queue ! h265parse ! mp4mux ! filesink location=h265.mp4
 * ]|
 *  Read h265 encoded bit-stream and recorded into mp4.
 *
 * ## Example pipelines, h265 -> mp4, with heic file sink
 * |[
 * gst-launch-1.0 -v -e amba_venccap ! amba_vencdemux heic-mode=1 heic-period=10 name=dm dm.stream0 ! queue ! h265parse ! mp4mux ! filesink location=h265.mp4 dm.heic_src ! queue ! amba_heicfilesink filename-base=/tmp/amba_%06d.heic
 * ]|
 *  Read h265 encoded bit-stream and recorded into mp4, with heic file sink
 *
 * ## Example pipelines, h265 -> mp4, with heic local dump
 * |[
 * gst-launch-1.0 -v -e amba_venccap ! amba_vencdemux heic-mode=1 heic-period=10 heic-dump-local=1 filename-base=/tmp/amba%06d.heic name=dm dm.stream0 ! queue ! h265parse ! mp4mux ! filesink location=h265.mp4
 * ]|
 *  Read h265 encoded bit-stream and recorded into mp4, with heic local dump
 *
 */

#include "common_err_code_c.h"

#include "internal.h"
#include "debug_log.h"
#include "iav_al.h"

#include "gstambavencdemux.h"

#include "element_common.h"

#include "buffer_utils.h"

#define DEFAULT_SYNC                    TRUE
#define DEFAULT_TS_OFFSET               0
#define DEFAULT_SYNC_TO_FIRST           FALSE

static gchar * gs_src_pad_names [ D_MAX_STREAM_NUM ] =
{
  "stream0",
  "stream1",
  "stream2",
  "stream3",
  "stream4",
  "stream5",
  "stream6",
  "stream7",
  "stream8",
  "stream9",
  "stream10",
  "stream11",
  "stream12",
  "stream13",
  "stream14",
  "stream15",
  "stream16",
  "stream17",
  "stream18",
  "stream19",
};

GST_DEBUG_CATEGORY_STATIC (ambavencdemux_debug);
#define GST_CAT_DEFAULT ambavencdemux_debug

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY"));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("stream_%u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("ANY"));

static GstStaticPadTemplate src_avc_template = GST_STATIC_PAD_TEMPLATE ("stream_%u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("ANY"));

static GstStaticPadTemplate src_hevc_template = GST_STATIC_PAD_TEMPLATE ("stream_%u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("ANY"));

static GstStaticPadTemplate src_heic_template = GST_STATIC_PAD_TEMPLATE ("heic",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY"));

enum
{
  HEIC_MODE_DISABLE = 0,  // 0: disable,
  HEIC_MODE_PERIOD = 1,   // 1: periodical(per IDR)
  HEIC_MODE_TRIGGER = 2,  // 2: trigger
};

enum
{
  PROP_0,
  PROP_FILENAME_BASE, // heic local dump
  PROP_HEIC_MODE, // see HEIC_MODE_xxx
  PROP_HEIC_CAPTURE_ID,
  PROP_HEIC_CAPTURE_CLOSE_ID,
  PROP_LAST,
};

#if 0
#define GST_AMBAVENCDEMUX_MUTEX_LOCK(q) G_STMT_START {                          \
  g_mutex_lock (&q->qlock);                                              \
} G_STMT_END

#define GST_AMBAVENCDEMUX_MUTEX_LOCK_CHECK(q,res,label) G_STMT_START {         \
  GST_AMBAVENCDEMUX_MUTEX_LOCK (q);                                            \
  if (res != GST_FLOW_OK)                                               \
    goto label;                                                         \
} G_STMT_END

#define GST_AMBAVENCDEMUX_MUTEX_UNLOCK(q) G_STMT_START {                        \
  g_mutex_unlock (&q->qlock);                                            \
} G_STMT_END

#define GST_AMBAVENCDEMUX_WAIT_ADD_CHECK(q, res, o, label) G_STMT_START {       \
  STATUS (q, q->srcpad, "wait for ADD");                            \
  q->waiting_add = TRUE;                                                \
  q->waiting_offset = o;                                                \
  g_cond_wait (&q->item_add, &q->qlock);                                \
  q->waiting_add = FALSE;                                               \
  if (res != GST_FLOW_OK) {                                             \
    STATUS (q, q->srcpad, "received ADD wakeup");                   \
    goto label;                                                         \
  }                                                                     \
  STATUS (q, q->srcpad, "received ADD");                            \
} G_STMT_END

#define GST_AMBAVENCDEMUX_SIGNAL_ADD(q) G_STMT_START {                       \
  if (q->waiting_add) {                       \
    STATUS (q, q->sinkpad, "signal ADD");                               \
    g_cond_signal (&q->item_add);                                       \
  }                                                                     \
} G_STMT_END
#endif

#define gst_ambavencdemux_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstAmbaVencdemux, gst_ambavencdemux, GST_TYPE_ELEMENT,
  GST_DEBUG_CATEGORY_INIT(ambavencdemux_debug, "ambavencdemux", 0,
  "Demux element for ambavenccap"));

static void gst_ambavencdemux_finalize (GObject * object);

/* query functions */
static gboolean
gst_ambavencdemux_src_query (GstPad * pad, GstObject * parent, GstQuery * query);
static gboolean
gst_ambavencdemux_sink_query (GstPad * pad, GstObject * parent, GstQuery * query);

/* event functions */
static gboolean
gst_ambavencdemux_handle_src_event (GstPad * pad, GstObject * parent, GstEvent * event);
static gboolean
gst_ambavencdemux_handle_sink_event (GstPad * pad, GstObject * parent, GstEvent * event);

/* scheduling functions */
static GstFlowReturn
gst_ambavencdemux_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer);

/* state change functions */
static GstStateChangeReturn
gst_ambavencdemux_change_state (GstElement * element, GstStateChange transition);

static GstPad *
gst_ambavencdemux_add_pad (GstAmbaVencdemux * ambavencdemux, GstStaticPadTemplate * templ,
  GstCaps * caps, char* pad_name, unsigned int stream_idx);

static void
gst_ambavencdemux_remove_pads (GstAmbaVencdemux * ambavencdemux);

static void
gst_ambavencdemux_set_property (GObject * object, guint prop_id,
  const GValue * value, GParamSpec * pspec);

static void
gst_ambavencdemux_get_property (GObject * object, guint prop_id,
  GValue * value, GParamSpec * pspec);
#if 0
static gboolean
gst_ambavencdemux_src_activate_mode (GstPad * pad,
  GstObject * parent, GstPadMode mode, gboolean active);
static gboolean
gst_ambavencdemux_sink_activate_mode (GstPad * pad,
  GstObject * parent, GstPadMode mode, gboolean active);
#endif

static void
gst_ambavencdemux_class_init (GstAmbaVencdemuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->finalize = gst_ambavencdemux_finalize;

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_ambavencdemux_change_state);

  gobject_class->set_property = gst_ambavencdemux_set_property;
  gobject_class->get_property = gst_ambavencdemux_get_property;

  g_object_class_install_property (gobject_class, PROP_FILENAME_BASE,
      g_param_spec_string ("filename-base", "Filename-Base",
          "Location of the file to write", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_HEIC_MODE,
      g_param_spec_uint ("heic-mode", "HEIC-Mode",
          "HEIC mode: disable, periodical, trigger", (guint) HEIC_MODE_DISABLE,
          (guint) HEIC_MODE_TRIGGER, (guint) HEIC_MODE_DISABLE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_HEIC_CAPTURE_ID,
      g_param_spec_string ("heic-capture-id", "HEIC-Capture-ID",
          "HEIC capture stream ids", "-1",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_HEIC_CAPTURE_CLOSE_ID,
      g_param_spec_string ("heic-capture-close-id", "HEIC-Capture-CLOSE-ID",
          "close HEIC capture stream ids", "-1",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_static_pad_template (gstelement_class, &sink_template);
  gst_element_class_add_static_pad_template (gstelement_class, &src_template);
  gst_element_class_add_static_pad_template (gstelement_class, &src_heic_template);

  gst_element_class_set_static_metadata (gstelement_class,
      "Amba Video Encoder, demux",
      "ambavencdemux",
      "Demux encoded bistreams",
      "Zhi He <zhe@ambarella.com>");
}

static void
gst_ambavencdemux_init (GstAmbaVencdemux * thiz)
{
  guint i = 0;
  thiz->sink_pad = gst_pad_new_from_static_template (&sink_template, "sink");

  /* for push mode, this is the chain function */
  gst_pad_set_chain_function (thiz->sink_pad,
      GST_DEBUG_FUNCPTR (gst_ambavencdemux_chain));
#if 0
  gst_pad_set_activatemode_function (thiz->sinkpad,
      GST_DEBUG_FUNCPTR (gst_ambavencdemux_sink_activate_mode));
#endif
  /* handling events (in push mode only) */
  gst_pad_set_event_function (thiz->sink_pad,
      GST_DEBUG_FUNCPTR (gst_ambavencdemux_handle_sink_event));
  /* query functions */
  gst_pad_set_query_function (thiz->sink_pad,
      GST_DEBUG_FUNCPTR (gst_ambavencdemux_sink_query));

  /* now add the pad */
  gst_element_add_pad (GST_ELEMENT (thiz), thiz->sink_pad);

  /* src pads will be created in the chain function */
  for (i = 0; i < IAV_STREAM_MAX_NUM_ALL; i++) {
    thiz->src_pads[i] = NULL;

    gst_video_info_init (&thiz->info[i]);
    gst_video_info_set_format (&thiz->info[i], GST_VIDEO_FORMAT_ENCODED, 0, 0);

    // for heic
    thiz->heic_cur_idr[i] = 0;
  }

  thiz->ts_offset = DEFAULT_TS_OFFSET;
  thiz->sync = DEFAULT_SYNC;
  thiz->sync_to_first = DEFAULT_SYNC_TO_FIRST;

  thiz->heic_mode = HEIC_MODE_DISABLE;

  thiz->filename_base = NULL;

  file_dumper_init(&thiz->file_dump);

}

static void
gst_ambavencdemux_finalize (GObject * object)
{
  GstAmbaVencdemux *thiz = GST_AMBAVENCDEMUX (object);
  gst_ambavencdemux_remove_pads(thiz);

  if (thiz->filename_base) {
    g_free(thiz->filename_base);
    thiz->filename_base = NULL;
  }
  file_dumper_deinit(&thiz->file_dump);

  for (int i = 0; i < IAV_STREAM_MAX_NUM_ALL; i++) {
    if (thiz->segment[i]) {
      gst_segment_free(thiz->segment[i]);
      thiz->segment[i] = NULL;
    }
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstPad *
gst_ambavencdemux_add_pad (GstAmbaVencdemux * thiz,
  GstStaticPadTemplate * templ,
  GstCaps * caps, gchar * pad_name, unsigned int stream_idx)
{
  GstPad *pad = NULL;
  GstEvent *event = NULL;
  gchar *stream_id = NULL;
  GstSegment *segment = NULL;

  do {
    if (NULL == (pad = gst_pad_new_from_static_template (templ, pad_name))) {
      GST_ERROR("Failed to create pad %s from template", pad_name);
      break;
    }
#if 0
    gst_pad_set_activatemode_function (pad,
        GST_DEBUG_FUNCPTR (gst_ambavencdemux_src_activate_mode));
#endif
    gst_pad_set_query_function (pad,
      GST_DEBUG_FUNCPTR (gst_ambavencdemux_src_query));
    gst_pad_set_event_function (pad,
      GST_DEBUG_FUNCPTR (gst_ambavencdemux_handle_src_event));
    gst_pad_use_fixed_caps (pad);
    gst_pad_set_active (pad, TRUE);

    if (NULL == (stream_id = gst_pad_create_stream_id (pad,
        GST_ELEMENT_CAST (thiz), pad_name))) {
      GST_ERROR("Failed to create stream-id for pad %s", pad_name);
      break;
    }
    if (NULL == (event = gst_event_new_stream_start (stream_id))) {
      GST_ERROR("Failed to create new stream start event for pad %s", pad_name);
      break;
    }
    gst_pad_push_event (pad, event);
    g_free (stream_id);

    if (caps && gst_pad_set_caps (pad, caps)) {
      GST_INFO("caps are set successfully for pad %s", pad_name);
    }

    if (NULL == (segment = gst_segment_new())) {
      GST_ERROR("Failed to create segment for pad %s", pad_name);
      break;
    }
    gst_segment_init (segment, GST_FORMAT_TIME);

    if (NULL == (event = gst_event_new_segment (segment))) {
      GST_ERROR("Failed to create new segment event for pad %s", pad_name);
      break;
    }
    gst_pad_push_event (pad, event);

    gst_element_add_pad (GST_ELEMENT (thiz), pad);
    thiz->segment[stream_idx] = segment;
  } while(0);

  return pad;
}

static void
gst_ambavencdemux_remove_pads (GstAmbaVencdemux * thiz)
{
  guint i = 0;

  for (i = 0; i < IAV_STREAM_MAX_NUM_ALL; i++) {
    if (thiz->src_pads[i]) {
      gst_pad_set_active (thiz->src_pads[i], FALSE);
      gst_element_remove_pad (GST_ELEMENT (thiz), thiz->src_pads[i]);
      thiz->src_pads[i] = NULL;
    }
  }
}

static int parse_id (const char *custom_properties, guint *id_mask)
{
  if (custom_properties) {
    char **options;
    unsigned int i = 0, len = 0;

    options = g_strsplit (custom_properties, ",", -1);
    len = g_strv_length (options);

    for (i = 0; i < len; ++i) {
      g_strstrip (options[i]);
      gint cur_stream_id = (gint) g_ascii_strtoll (options[i], NULL, 10);
      if (cur_stream_id >= 0) {
        *id_mask |= (1 << cur_stream_id);
      }
    }
    g_strfreev (options);

  } else {
    printf("no params\n");
    return -1;
  }

  return 0;
}

static void
gst_ambavencdemux_set_property (GObject * object, guint prop_id,
  const GValue * value, GParamSpec * pspec)
{
  GstAmbaVencdemux * thiz = GST_AMBAVENCDEMUX (object);

  switch (prop_id) {
    case PROP_FILENAME_BASE: {
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
    }break;
    case PROP_HEIC_MODE: {
      thiz->heic_mode = g_value_get_uint (value);
    }break;
    case PROP_HEIC_CAPTURE_ID: {
      strncpy(thiz->heic_capture_id_str, g_value_get_string (value), 127);
      if (parse_id(thiz->heic_capture_id_str, &thiz->heic_capture_id) < 0) {
        DPRINT_ERROR("parse_id (%s) failed\n",
          g_value_get_string (value));
        return;
      }
      if (thiz->heic_mode == HEIC_MODE_DISABLE) {
        thiz->heic_mode = HEIC_MODE_PERIOD;
      }
    }break;
    case PROP_HEIC_CAPTURE_CLOSE_ID: {
      guint close_id = 0;
      strncpy(thiz->heic_capture_close_id_str, g_value_get_string (value), 127);
      if (parse_id(thiz->heic_capture_close_id_str, &close_id) < 0) {
        DPRINT_ERROR("parse_id (%s) failed\n",
          g_value_get_string (value));
        return;
      }
      thiz->heic_capture_id &= (~close_id);
    }break;
    default: {
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }break;
  }
}

static void
gst_ambavencdemux_get_property (GObject * object, guint prop_id,
  GValue * value, GParamSpec * pspec)
{
  GstAmbaVencdemux *thiz = GST_AMBAVENCDEMUX (object);

  switch (prop_id) {
    case PROP_FILENAME_BASE: {
      g_value_set_string (value, thiz->filename_base);
    }break;
    case PROP_HEIC_MODE: {
      g_value_set_uint (value, thiz->heic_mode);
    }break;
    case PROP_HEIC_CAPTURE_ID: {
      g_value_set_string (value, thiz->heic_capture_id_str);
    }break;
    case PROP_HEIC_CAPTURE_CLOSE_ID: {
      g_value_set_string (value, thiz->heic_capture_close_id_str);
    }break;
    default: {
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }break;
  }
}

static gboolean
gst_ambavencdemux_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean res = TRUE;
  GstAmbaVencdemux *thiz = GST_AMBAVENCDEMUX (parent);
  gint stream_id = -1;
  gchar *str = NULL;

  str = strchr (GST_PAD_NAME(pad), 'm');
  if (str) {
    stream_id = (gint) g_ascii_strtoll ((str + 1), NULL, 10);
  }

  /* Handle any necessary src queries */
  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONVERT: {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      GST_OBJECT_LOCK (thiz);
      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      res =
          gst_video_info_convert (&thiz->info[stream_id], src_fmt, src_val, dest_fmt,
          &dest_val);
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      GST_OBJECT_UNLOCK (thiz);
    } break;
#if 0
    case GST_QUERY_LATENCY:
    {
      GST_OBJECT_LOCK (thiz);
      if (thiz->info[stream_id].fps_n > 0) {
        GstClockTime latency;
        latency =
            gst_util_uint64_scale (GST_SECOND, thiz->info[stream_id].fps_d,
            thiz->info[stream_id].fps_n);
        thiz->upstream_latency[stream_id] = latency;
        GST_OBJECT_UNLOCK (thiz);
        gst_query_set_latency (query, TRUE, latency,
            GST_CLOCK_TIME_NONE);

        GST_DEBUG_OBJECT (thiz, "Reporting latency of %" GST_TIME_FORMAT,
            GST_TIME_ARGS (latency));

        res = TRUE;
      } else {
        GST_OBJECT_UNLOCK (thiz);
      }
      break;
    }
#endif
    case GST_QUERY_DURATION: {
      GstFormat format;
      gst_query_parse_duration (query, &format, NULL);
      switch (format) {
        case GST_FORMAT_TIME: {
          gint64 dur;

          GST_OBJECT_LOCK (thiz);
          if (thiz->info[stream_id].fps_n > 0) {
            dur = gst_util_uint64_scale_int_round (GST_SECOND, thiz->info[stream_id].fps_d, thiz->info[stream_id].fps_n);
            res = TRUE;
            gst_query_set_duration (query, GST_FORMAT_TIME, dur);
          }
          GST_OBJECT_UNLOCK (thiz);
          goto done;
        }
        case GST_FORMAT_BYTES:
          GST_OBJECT_LOCK (thiz);
          res = TRUE;
          gst_query_set_duration (query, GST_FORMAT_BYTES,
              thiz->info[stream_id].size);
          GST_OBJECT_UNLOCK (thiz);
          goto done;
        default:
          break;
      }
    } break;
    default: {
      res = gst_pad_query_default (pad, parent, query);
    } break;
  }

done:
  return res;
}

static gboolean
gst_ambavencdemux_sink_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean ret = TRUE;
  /* Handle any sink queries */
  switch (GST_QUERY_TYPE (query)) {
    default: {
      ret = gst_pad_query_default (pad, parent, query);
    }break;
  }

  return ret;
}

static gboolean
gst_ambavencdemux_push_event (GstAmbaVencdemux * thiz, GstEvent * event)
{
  guint i;
  gboolean ret = FALSE;
  gboolean has_srcpad = FALSE;

  for (i = 0; i < IAV_STREAM_MAX_NUM_ALL; i++) {
    if (thiz->src_pads[i]) {
      has_srcpad = TRUE;
      gst_event_ref (event);
      GST_LOG_OBJECT (thiz, "push event to src_pads [%d]", i);
      ret |= gst_pad_push_event (thiz->src_pads[i], event);
    }
  }
  if (!has_srcpad) {
    gst_event_unref (event);
    ret = TRUE;
  }
  return ret;
}

static gboolean
gst_ambavencdemux_handle_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstAmbaVencdemux * thiz = GST_AMBAVENCDEMUX (parent);
  gboolean ret = TRUE;

  const GstStructure *s;
  const gchar *tstr;
  gchar *sstr;

  GST_OBJECT_LOCK (thiz);

  tstr = gst_event_type_get_name (GST_EVENT_TYPE (event));

  if ((s = gst_event_get_structure (event)))
    sstr = gst_structure_to_string (s);
  else
    sstr = g_strdup ("");

  GST_DEBUG_OBJECT (thiz, "event   ******* E (type: %s (%d), %s) %p\n",
      tstr, GST_EVENT_TYPE (event), sstr, event);
  g_free (sstr);
  GST_OBJECT_UNLOCK (thiz);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START: {
      GST_OBJECT_LOCK (thiz);
      thiz->flushing = TRUE;
      g_cond_signal (&thiz->blocked_cond);
      if (thiz->clock_id) {
        GST_DEBUG_OBJECT (thiz, "unlock clock wait");
        gst_clock_id_unschedule (thiz->clock_id);
      }
      GST_OBJECT_UNLOCK (thiz);
      ret = gst_pad_event_default (pad, parent, event);
    } break;
    case GST_EVENT_FLUSH_STOP: {
      GST_OBJECT_LOCK (thiz);
      thiz->flushing = FALSE;
      for (int i = 0; i < IAV_STREAM_MAX_NUM_ALL; i++) {
        if (thiz->segment[i]) {
          gst_segment_init (thiz->segment[i], GST_FORMAT_UNDEFINED);
        }
      }
      GST_OBJECT_UNLOCK (thiz);
      thiz->is_first = TRUE;
      ret = gst_pad_event_default (pad, parent, event);
    } break;
    case GST_EVENT_EOS: {
      ret = gst_pad_event_default (pad, parent, event);
    } break;
    default: {
      ret = gst_ambavencdemux_push_event (thiz, event);
    } break;
  }

  return ret;
}

static gboolean
gst_ambavencdemux_handle_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  DUNUSED(pad);
  gboolean ret = FALSE;
  GstAmbaVencdemux * thiz = GST_AMBAVENCDEMUX (parent);
  switch (GST_EVENT_TYPE (event)) {
    default:
      ret = gst_pad_push_event (thiz->sink_pad, event);
      break;
  }

  return ret;
}

static void
gst_ambavencdemux_sync_update_ts_offset (GstAmbaVencdemux * thiz,
    GstClockTime runtimestamp)
{
  GstClock *clock;
  GstClockTimeDiff ts_offset = 0;
  GstClockTime running_time;

  if (!thiz->sync_to_first || !thiz->is_first || !thiz->sync)
    return;

  GST_OBJECT_LOCK (thiz);
  clock = GST_ELEMENT_CLOCK (thiz);
  if (!clock) {
    GST_DEBUG_OBJECT (thiz, "We have no clock");
    GST_OBJECT_UNLOCK (thiz);
    return;
  }

  running_time = gst_clock_get_time (clock) -
      GST_ELEMENT_CAST (thiz)->base_time;
  ts_offset = GST_CLOCK_DIFF (runtimestamp, running_time);
  GST_OBJECT_UNLOCK (thiz);

  GST_DEBUG_OBJECT (thiz, "Running time %" GST_TIME_FORMAT
      ", running time stamp %" GST_TIME_FORMAT ", calculated ts-offset %"
      GST_STIME_FORMAT, GST_TIME_ARGS (running_time),
      GST_TIME_ARGS (runtimestamp), GST_STIME_ARGS (ts_offset));

  thiz->is_first = FALSE;
  if (ts_offset != thiz->ts_offset) {
    thiz->ts_offset = ts_offset;
  }
}

static GstFlowReturn
gst_ambavencdemux_do_sync (GstAmbaVencdemux * thiz,
    GstClockTimeDiff running_time, unsigned int stream_idx)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstClock *clock;

  //thiz->current_rstart = GST_CLOCK_TIME_NONE;

  if (!thiz->sync)
    return GST_FLOW_OK;

  if (running_time == -1)
    return GST_FLOW_OK;         /* Can't sync on an invalid time either way */

  if (thiz->segment[stream_idx]->format != GST_FORMAT_TIME)
    return GST_FLOW_OK;

  GST_OBJECT_LOCK (thiz);

  if (thiz->flushing) {
    GST_OBJECT_UNLOCK (thiz);
    return GST_FLOW_FLUSHING;
  }

  while (thiz->blocked && !thiz->flushing)
    g_cond_wait (&thiz->blocked_cond, GST_OBJECT_GET_LOCK (thiz));

  if (thiz->flushing) {
    GST_OBJECT_UNLOCK (thiz);
    return GST_FLOW_FLUSHING;
  }

  if ((clock = GST_ELEMENT (thiz)->clock)) {
    GstClockReturn cret;
    GstClockTime timestamp;
    GstClockTimeDiff ts_offset = thiz->ts_offset;
    GstClockTimeDiff jitter;

    timestamp = running_time + GST_ELEMENT (thiz)->base_time +
        thiz->upstream_latency[stream_idx];

    GST_DEBUG_OBJECT (thiz,
        "running time: %" GST_TIME_FORMAT " base time: %" GST_TIME_FORMAT
        " upstream latency: %" GST_TIME_FORMAT, GST_TIME_ARGS (running_time),
        GST_TIME_ARGS (GST_ELEMENT (thiz)->base_time),
        GST_TIME_ARGS (thiz->upstream_latency[stream_idx]));

    GST_DEBUG_OBJECT (thiz,
        "Waiting for clock time %" GST_TIME_FORMAT " ts offset: %"
        GST_STIME_FORMAT, GST_TIME_ARGS (timestamp),
        GST_STIME_ARGS (ts_offset));

    if (ts_offset < 0) {
      ts_offset = -ts_offset;
      if (ts_offset < (GstClockTimeDiff) timestamp)
        timestamp -= ts_offset;
      else
        timestamp = 0;
    } else {
      timestamp += ts_offset;
    }

    GST_DEBUG_OBJECT (thiz, "Offset clock time %" GST_TIME_FORMAT,
        GST_TIME_ARGS (timestamp));

    /* save id if we need to unlock */
    thiz->clock_id = gst_clock_new_single_shot_id (clock, timestamp);
    GST_OBJECT_UNLOCK (thiz);

    cret = gst_clock_id_wait (thiz->clock_id, &jitter);

    GST_DEBUG_OBJECT (thiz, "Clock returned %d, jitter %" GST_STIME_FORMAT,
        cret, GST_STIME_ARGS (jitter));

    GST_OBJECT_LOCK (thiz);
    if (thiz->clock_id) {
      gst_clock_id_unref (thiz->clock_id);
      thiz->clock_id = NULL;
    }
    if (cret == GST_CLOCK_UNSCHEDULED || thiz->flushing)
      ret = GST_FLOW_FLUSHING;

  }

  GST_OBJECT_UNLOCK (thiz);

  return ret;
}

static GstFlowReturn
gst_ambavencdemux_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  DUNUSED(pad);
  GstAmbaVencdemux * thiz = GST_AMBAVENCDEMUX (parent);
  GstFlowReturn flow_ret = GST_FLOW_OK;
  shared_stream_info_u shared_stream_info;
  guint stream_fmt, stream_idx;
  int ret = 0;
  GstVideoRegionOfInterestMeta *vmeta = NULL;
  GstStructure *s = NULL;
  int fps_n = -1, fps_d = -1;

  do {
    memset(&shared_stream_info, 0x0, sizeof(shared_stream_info_u));

    vmeta = (GstVideoRegionOfInterestMeta *)
        gst_buffer_get_meta (buffer, GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE);

    if (vmeta == NULL) {
      DPRINT_ERROR("gst_buffer_iterate_meta_filtered failed\n");
      gst_buffer_unref(buffer);
      flow_ret = GST_FLOW_ERROR;
      break;
    }

    s = gst_video_region_of_interest_meta_get_param (vmeta, GST_VENCCAP_META_PARAM_NAME);
    if (s) {
#if 0
      if (!gst_structure_get (s,
            GST_VENCCAP_META_FIELD_STREAM_ID, G_TYPE_UINT, &shared_stream_info.info_v.stream_idx,
            GST_VENCCAP_META_FIELD_STREAM_FORMAT, G_TYPE_UINT, &shared_stream_info.info_v.stream_fmt,
            GST_VENCCAP_META_FIELD_KEY_FRAME, G_TYPE_UINT, &shared_stream_info.info_v.is_key_frame,
            GST_VENCCAP_META_FIELD_FRAME_START, G_TYPE_UINT, &shared_stream_info.info_v.is_frame_start,
            GST_VENCCAP_META_FIELD_FORMAT_CHANGE, G_TYPE_UINT, &shared_stream_info.info_v.is_format_changed,
            GST_VENCCAP_META_FIELD_EXTRADATA, G_TYPE_UINT, &shared_stream_info.info_v.with_extradata,
            NULL)) {
        DPRINT_ERROR("get structure fields failed\n");
        gst_buffer_unmap (buffer, &map);
        gst_buffer_unref(buffer);
        flow_ret = GST_FLOW_ERROR;
        break;
      }
#else
      guint iv = 0;
      if (!gst_structure_get_uint (s, GST_VENCCAP_META_FIELD_STREAM_ID, &iv)) {
        DPRINT_ERROR("get stream id failed\n");
        gst_buffer_unref(buffer);
        flow_ret = GST_FLOW_ERROR;
        break;
      }
      shared_stream_info.info_v.stream_idx = iv;

      if (!gst_structure_get_uint (s, GST_VENCCAP_META_FIELD_STREAM_FORMAT, &iv)) {
        DPRINT_ERROR("get stream format failed\n");
        gst_buffer_unref(buffer);
        flow_ret = GST_FLOW_ERROR;
        break;
      }
      shared_stream_info.info_v.stream_fmt = iv;

      if (!gst_structure_get_uint (s, GST_VENCCAP_META_FIELD_KEY_FRAME, &iv)) {
        DPRINT_ERROR("cannot confirm key frame\n");
        gst_buffer_unref(buffer);
        flow_ret = GST_FLOW_ERROR;
        break;
      }
      shared_stream_info.info_v.is_key_frame = iv;

      if (!gst_structure_get_uint (s, GST_VENCCAP_META_FIELD_FRAME_START, &iv)) {
        DPRINT_ERROR("cannot confirm frame start\n");
        gst_buffer_unref(buffer);
        flow_ret = GST_FLOW_ERROR;
        break;
      }
      shared_stream_info.info_v.is_frame_start = iv;

      if (!gst_structure_get_uint (s, GST_VENCCAP_META_FIELD_FORMAT_CHANGE, &iv)) {
        DPRINT_ERROR("cannot confirm format change\n");
        gst_buffer_unref(buffer);
        flow_ret = GST_FLOW_ERROR;
        break;
      }
      shared_stream_info.info_v.is_format_changed = iv;

      if (!gst_structure_get_uint (s, GST_VENCCAP_META_FIELD_EXTRADATA, &iv)) {
        DPRINT_ERROR("cannot confirm frame with extradata\n");
        gst_buffer_unref(buffer);
        flow_ret = GST_FLOW_ERROR;
        break;
      }
      shared_stream_info.info_v.with_extradata = iv;

      if (!gst_structure_get_int (s, GST_VENCCAP_META_FIELD_FPS_N, &fps_n)) {
        DPRINT_ERROR("cannot confirm frame with fps_n\n");
        gst_buffer_unref(buffer);
        flow_ret = GST_FLOW_ERROR;
        break;
      }

      if (!gst_structure_get_int (s, GST_VENCCAP_META_FIELD_FPS_D, &fps_d)) {
        DPRINT_ERROR("cannot confirm frame with fps_d\n");
        gst_buffer_unref(buffer);
        flow_ret = GST_FLOW_ERROR;
        break;
      }
#endif
    } else {
      DPRINT_ERROR("no structure in metadata\n");
      gst_buffer_unref(buffer);
      flow_ret = GST_FLOW_ERROR;
      break;
    }

    stream_idx = shared_stream_info.info_v.stream_idx;
    stream_fmt = shared_stream_info.info_v.stream_fmt;

    if ( stream_idx >= IAV_STREAM_MAX_NUM_ALL ) {
      DPRINT_ERROR("Bad stream index %d\n", stream_idx);
      gst_buffer_unref (buffer);
      flow_ret = GST_FLOW_ERROR;
      break;
    }
    thiz->info[stream_idx].width = vmeta->w;
    thiz->info[stream_idx].height = vmeta->h;
    thiz->info[stream_idx].size = gst_buffer_get_size(buffer);
    thiz->info[stream_idx].fps_n = fps_n;
    thiz->info[stream_idx].fps_d = fps_d;

    gst_buffer_remove_meta (buffer, (GstMeta *) vmeta);
    vmeta = NULL;

    if (!thiz->src_pads[stream_idx]) {
      if (StreamFormat_H264 == stream_fmt) {
        thiz->src_pads[stream_idx] =
          gst_ambavencdemux_add_pad (thiz, &src_avc_template,
            NULL, gs_src_pad_names[stream_idx], stream_idx);
      } else if (StreamFormat_H265 == stream_fmt) {
        thiz->src_pads[stream_idx] =
          gst_ambavencdemux_add_pad (thiz, &src_hevc_template,
            NULL, gs_src_pad_names[stream_idx], stream_idx);
      } else if (StreamFormat_JPEG == stream_fmt) {
        thiz->src_pads[stream_idx] =
          gst_ambavencdemux_add_pad (thiz, &src_template,
            NULL, gs_src_pad_names[stream_idx], stream_idx);
      } else {
        DPRINT_ERROR("bad stream format %x\n", stream_fmt);
        gst_buffer_unref (buffer);
        flow_ret = GST_FLOW_ERROR;
        break;
      }
    }

    if ((StreamFormat_H265 == stream_fmt) && ((1 << stream_idx) & thiz->heic_capture_id)) {
      if ((thiz->heic_mode == HEIC_MODE_TRIGGER) ||
          (thiz->heic_mode == HEIC_MODE_PERIOD && shared_stream_info.info_v.is_key_frame)) {
        ret = file_dumper_write_from_buffer_v2(&thiz->file_dump, (void *) buffer,
            shared_stream_info.info_v.is_frame_start);
        if (ret) {
          DPRINT_ERROR("write file (%s) from buffer failed, ret %d\n",
              thiz->file_dump.p_filename_buf, ret);
          gst_buffer_unref (buffer);
          flow_ret = GST_FLOW_ERROR;
          break;
        }
      }
    }

    if (thiz->segment[stream_idx]->format == GST_FORMAT_TIME) {
      GstClockTime runtimestamp = 0;
      GstClockTime rundts, runpts;

      if (thiz->segment[stream_idx]->rate > 0.0) {
        rundts = gst_segment_to_running_time (thiz->segment[stream_idx],
            GST_FORMAT_TIME, GST_BUFFER_DTS (buffer));
        runpts = gst_segment_to_running_time (thiz->segment[stream_idx],
            GST_FORMAT_TIME, GST_BUFFER_PTS (buffer));
      } else {
        runpts = gst_segment_to_running_time (thiz->segment[stream_idx],
            GST_FORMAT_TIME, GST_CLOCK_TIME_IS_VALID (buffer->duration)
            && GST_CLOCK_TIME_IS_VALID (buffer->pts) ? buffer->pts +
            buffer->duration : buffer->pts);
        rundts = gst_segment_to_running_time (thiz->segment[stream_idx],
            GST_FORMAT_TIME, GST_CLOCK_TIME_IS_VALID (buffer->duration)
            && GST_CLOCK_TIME_IS_VALID (buffer->dts) ? buffer->dts +
            buffer->duration : buffer->dts);
      }

      if (GST_CLOCK_TIME_IS_VALID (rundts))
        runtimestamp = rundts;
      else if (GST_CLOCK_TIME_IS_VALID (runpts))
        runtimestamp = runpts;

      gst_ambavencdemux_sync_update_ts_offset (thiz, runtimestamp);

      flow_ret = gst_ambavencdemux_do_sync (thiz, runtimestamp, stream_idx);
      if (flow_ret != GST_FLOW_OK) {
        GST_LOG_OBJECT (thiz,
            "Interrupted while waiting on the clock. Dropping buffer.");
        gst_buffer_unref (buffer);
        break;
      }

    }

    if (gst_pad_is_linked(thiz->src_pads[stream_idx])) {
      flow_ret = gst_pad_push(thiz->src_pads[stream_idx], buffer);
      if (flow_ret != GST_FLOW_OK) {
        GST_FIXME_OBJECT (thiz,
            "gst_pad_push failed, reason: %s", gst_flow_get_name (flow_ret));
        gst_buffer_unref (buffer);
      }
      break;
    } else {
      gst_buffer_unref (buffer);
    }

    flow_ret = GST_FLOW_OK;
  } while (0);

  return flow_ret;
}

static GstStateChangeReturn
gst_ambavencdemux_change_state (GstElement * element, GstStateChange transition)
{
  GstAmbaVencdemux * src = GST_AMBAVENCDEMUX (element);
  GstStateChangeReturn ret= GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      GST_DEBUG_OBJECT(src, "GST_STATE_CHANGE_NULL_TO_READY\n");
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_OBJECT_LOCK (src);
      src->flushing = FALSE;
      src->blocked = TRUE;
      GST_OBJECT_UNLOCK (src);
      src->is_first = TRUE;
      GST_DEBUG_OBJECT(src, "GST_STATE_CHANGE_READY_TO_PAUSED\n");
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      GST_OBJECT_LOCK (src);
      src->blocked = FALSE;
      g_cond_signal (&src->blocked_cond);
      GST_OBJECT_UNLOCK (src);
      GST_DEBUG_OBJECT(src, "GST_STATE_CHANGE_PAUSED_TO_PLAYING\n");
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      GST_OBJECT_LOCK (src);
      memset(src->upstream_latency, 0, sizeof(src->upstream_latency));
      src->blocked = TRUE;
      GST_OBJECT_UNLOCK (src);
      //gst_clock_sync_reset_qos (src);
      break;
      GST_DEBUG_OBJECT(src, "GST_STATE_CHANGE_PLAYING_TO_PAUSED\n");
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_ambavencdemux_remove_pads (src);
      GST_OBJECT_LOCK (src);
      src->flushing = TRUE;
      if (src->clock_id) {
        GST_DEBUG_OBJECT (src, "unlock clock wait");
        gst_clock_id_unschedule (src->clock_id);
      }
      src->blocked = FALSE;
      g_cond_signal (&src->blocked_cond);
      GST_OBJECT_UNLOCK (src);
      GST_DEBUG_OBJECT(src, "GST_STATE_CHANGE_PAUSED_TO_READY\n");
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      GST_DEBUG_OBJECT(src, "GST_STATE_CHANGE_READY_TO_NULL\n");
      break;
    default:
      break;
  }
  return ret;

}

