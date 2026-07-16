/*
 * gstambaeventrecorder.c
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

#include "gstambaeventrecorder.h"

#include "internal.h"

/**
 * SECTION:element-amba_event_recorder
 * @title: amba_event_recorder
 *
 * Request-pad bin: each sink_video_%u accepts H.264/H.265 in AU alignment; each sink_audio_%u
 * accepts common compressed or raw audio. Samples are held in per-track ring buffers until
 * #GstAmbaEventRecorder::trigger opens or extends a logical window; each window muxes to a file
 * using the mux-factory and location properties (printf-style pattern). The primary-pad track
 * drives running time, window end detection, and optional keyframe-aligned start.
 *
 * ## The `trigger` action signal
 *
 * The element exposes an action signal named `trigger` (see #GstAmbaEventRecorder::trigger). The
 * application must invoke it when an event should start or extend a clip; `gst-launch` alone cannot
 * fire it. Typical use: `g_signal_emit_by_name (recorder, "trigger", event_ts_ns, pre_ms, post_ms, event_id)`
 * or language bindings equivalent. Arguments:
 *
 * * `event_ts_ns` (`guint64`): event time as #GstClockTime in nanoseconds, or %GST_CLOCK_TIME_NONE to
 *   resolve from the primary track’s current running time (or cache head as fallback).
 * * `pre_ms` (`gint`): pre-roll duration in milliseconds; `0` means use the `pre-record-ms` property;
 *   negative values select “maximum cache” behaviour where supported.
 * * `post_ms` (`gint`): post-roll in milliseconds; `0` uses `post-record-ms`; `-1` records until EOS.
 * * `event_id` (`string`): optional identifier for logs (may be %NULL).
 *
 * ## Example launch line
 * Recorder first, then video chain, then audio (e.g. Ambarella H.264 AU + Opus). Adjust location, mux-factory, and primary-pad as needed.
 * gst-launch cannot start a recording window by itself: the controlling program must emit #GstAmbaEventRecorder::trigger (e.g. g_signal_emit_by_name on the recorder element) when each clip should begin or extend.
 * |[
 * gst-launch-1.0 -e \
 *   amba_event_recorder name=aer location=/tmp/amba-real-av-%05d.mp4 mux-factory=qtmux primary-pad=sink_video_0 \
 *   amba_venccap2 stream-id=0 ! queue ! h264parse ! aer.sink_video_0 \
 *   audiotestsrc is-live=true wave=ticks tick-interval=500000000 apply-tick-ramp=true volume=0.8 ! \
 *   audio/x-raw,rate=48000,channels=1 ! audioconvert ! audioresample ! opusenc bitrate=64000 ! \
 *   opusparse ! queue ! aer.sink_audio_0
 * ]|
 *
 */

GST_DEBUG_CATEGORY_STATIC (amba_event_recorder_debug);
#define GST_CAT_DEFAULT amba_event_recorder_debug

/*
 * One cached upstream sample (per track). The queue is time-ordered by running_time.
 */
typedef struct {
  GstBuffer *buffer;           /* Copy of the buffer (PTS/DTS rewritten to running_time for writer). */
  GstClockTime running_time;   /* Normalized segment running time for ordering and window cuts. */
  guint64 serial;              /* Monotonic id for primary-track node-aligned start (keyframe buffer). */
  gboolean keyframe;           /* TRUE if not GST_BUFFER_FLAG_DELTA_UNIT (video); audio uses same flag semantics. */
  gboolean written;            /* TRUE if already pushed to the current window writer (for retain/prune). */
} CachedBuffer;

/*
 * Per-sink-pad state: ingest (queue+appsink) and optional writer branch (appsrc[+parser]->mux).
 */
typedef struct {
  gchar *pad_name;             /* e.g. sink_video_0, sink_audio_0; hash key. */
  GstElement *queue;             /* Decouples upstream from appsink. */
  GstElement *appsink;           /* Pulls samples into the recorder lock. */
  GstPad *sink_ghost_pad;        /* Exposed sink pad for the bin. */

  GstElement *writer_appsrc;     /* Feeds one mux sink pad during an active window (NULL when idle). */
  GstElement *writer_parser;     /* h264parse/h265parse for video, NULL for audio. */
  GstPad *writer_mux_pad;        /* Ref to mux sink pad link (unlink on writer teardown). */

  GQueue cached_buffers;         /* Ring of CachedBuffer* for this track. */
  guint cached_count;            /* g_queue length (denormalized for limits). */
  guint64 cached_bytes;          /* Sum of buffer sizes (denormalized for limits). */
  GstClockTime last_running_time; /* Last seen running time (EOS / progress). */
  GstCaps *input_caps;           /* Negotiated caps on this track (writer appsrc caps). */
  gboolean warned_no_complete_gop_under_pressure; /* Rate-limit IDLE prune warning (GOP vs cache size). */
} TrackPadContext;

/*
 * Element private data: configuration, one logical recording window, and optional internal writer pipeline.
 * Most fields are guarded by lock; hot paths hold lock around cache + writer ops.
 */
typedef struct {

  GMutex lock;                   /* Protects private fields, all track caches, and writer state. */

  gchar *location;               /* Output filename pattern (printf-style, one file per window). */
  gchar *mux_factory;            /* e.g. mp4mux, qtmux, matroskamux. */
  gchar *mux_properties;         /* Optional comma-separated key=value for mux element. */
  gint pre_record_ms;            /* Default pre-roll before trigger (ms); -1 = max cache. */
  gint post_record_ms;           /* Default post-roll after trigger (ms); -1 = until EOS. */
  guint max_cache_ms;            /* IDLE ring time span; 0 disables time-based prune (buffers/bytes still apply). */
  guint max_buffers;             /* Per-track max cached buffers (0 = unlimited). */
  guint64 max_bytes;             /* Per-track max cached bytes (0 = unlimited). */
  gboolean align_start_keyframe; /* If TRUE, window start snaps to last keyframe <= hard start (video primary). */
  gchar *primary_pad_name;       /* Which sink pad drives window times and writer push/close (sync master). */

  /* Active window: logical [window_start, window_end] in running time; may extend on repeat trigger. */
  gboolean window_active;
  guint64 window_id;             /* Increments each new window (after previous closed). */
  GstClockTime window_start;     /* Hard or aligned start of current window. */
  GstClockTime window_end;       /* End of current window; may be GST_CLOCK_TIME_NONE (open end). */
  GstClockTime writer_start_rt;  /* Actual first sample time pushed to mux (after keyframe alignment). */
  guint64 writer_start_serial;   /* First primary buffer serial to push when using node alignment. */
  GstClockTime writer_last_written_rt; /* Last primary sample pushed (progress / close). */
  guint writer_written_count;    /* Buffers written on primary track this window (logging). */
  gint active_retain_pre_ms;     /* Max pre-roll ms to retain after write during ACTIVE (extends across triggers). */
  gchar *writer_location;        /* Path of file for current writer (logging / messages). */

  GstElement *writer_pipeline;   /* Internal bin: mux+filesink[+ per-track appsrc/parser]; NULL when idle. */

  gboolean upstream_eos;       /* At least one track saw EOS; triggers final window close. */
  gboolean fatal_error;          /* Primary writer setup failed; stop processing. */
  gboolean disposing;            /* Object teardown; release_pad warnings suppressed. */
  guint close_timer_id;          /* Periodic tick to close window when stream time passes window_end. */
  guint64 next_buffer_serial;    /* Global serial for CachedBuffer (primary alignment). */

  GHashTable *track_contexts;    /* Pad name -> TrackPadContext*; tracks created via request_new_pad only. */
} GstAmbaEventRecorderPrivate;

enum
{
  PROP_0,
  PROP_LOCATION,
  PROP_MUX_FACTORY,
  PROP_MUX_PROPERTIES,
  PROP_PRE_RECORD_MS,
  PROP_POST_RECORD_MS,
  PROP_MAX_CACHE_MS,
  PROP_MAX_BUFFERS,
  PROP_MAX_BYTES,
  PROP_ALIGN_START_KEYFRAME,
  PROP_PRIMARY_PAD,
  N_PROPERTIES
};

enum
{
  SIGNAL_TRIGGER,
  LAST_SIGNAL
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };
static guint signals[LAST_SIGNAL] = { 0, };

#define DEFAULT_LOCATION "event-%05d.mp4"
#define DEFAULT_MUX_FACTORY "mp4mux"
#define DEFAULT_PRE_RECORD_MS 5000
#define DEFAULT_POST_RECORD_MS 10000
#define DEFAULT_MAX_CACHE_MS 20000
#define DEFAULT_MAX_BUFFERS 0
#define DEFAULT_MAX_BYTES (64ULL * 1024ULL * 1024ULL)
#define DEFAULT_ALIGN_START_KEYFRAME TRUE
#define DEFAULT_PRIMARY_PAD_NAME "sink_video_0"
#define INVALID_BUFFER_SERIAL G_MAXUINT64

static GstStaticPadTemplate sink_video_request_template =
GST_STATIC_PAD_TEMPLATE ("sink_video_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("video/x-h264,alignment=(string)au; "
        "video/x-h265,alignment=(string)au"));

static GstStaticPadTemplate sink_audio_request_template =
GST_STATIC_PAD_TEMPLATE ("sink_audio_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("audio/mpeg; audio/x-opus; audio/x-raw"));

G_DEFINE_TYPE_WITH_PRIVATE (GstAmbaEventRecorder, gst_amba_event_recorder, GST_TYPE_BIN);

/* Typed accessor to instance private struct (declared before first use). */
static inline GstAmbaEventRecorderPrivate *
amba_event_recorder_get_priv (GstAmbaEventRecorder * self);

/* GLib timer (~100ms): while a window is active, advance writer and close when stream passes window_end. */
static gboolean amba_event_recorder_close_timer_cb (gpointer user_data);

/* Append sample to track cache, update caps, prune; open writer / push / maybe close window. */
static GstFlowReturn amba_event_recorder_process_sample (GstAmbaEventRecorder * self,
    TrackPadContext * ctx, GstSample * sample);

/* appsink "new-preroll": pull-preroll to unblock; refresh input_caps without duplicating first in cache. */
static GstFlowReturn amba_event_recorder_on_new_preroll (GstElement * appsink,
    gpointer user_data);

/* appsink "new-sample": pull-sample and dispatch to process_sample. */
static GstFlowReturn amba_event_recorder_on_new_sample (GstElement * appsink,
    gpointer user_data);

/* appsink "eos": set upstream_eos; force-close active window if any. */
static void amba_event_recorder_on_eos (GstElement * appsink, gpointer user_data);

/* Free one CachedBuffer (buffer unref + struct free). */
static void cached_buffer_free (CachedBuffer * cb);

/* GstElement vfunc: create queue+appsink+ghost pad for sink_video_%u or sink_audio_%u. */
static GstPad *amba_event_recorder_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * req_name, const GstCaps * caps);

/* GstElement vfunc: tear down track (refuses primary; active window; dispose quirks). */
static void amba_event_recorder_release_pad (GstElement * element, GstPad * pad);

/* Finalize current window: drain writer, post window-closed, clear writer_pipeline (caller holds lock). */
static void amba_event_recorder_close_active_window_unlocked (GstAmbaEventRecorder * self,
    GstClockTime close_rt, const gchar * reason);

/* Parse mux_properties and apply gst_util_set_object_arg on mux element. */
static void amba_event_recorder_apply_mux_properties (GstAmbaEventRecorder * self,
    GstElement * mux);

/* Lookup TrackPadContext for primary_pad_name (caller holds lock). */
static TrackPadContext *amba_event_recorder_get_primary_track_unlocked (
    GstAmbaEventRecorder * self);

/* Create appsrc[+parser] for one track and link to mux; primary failure is fatal. */
static gboolean amba_event_recorder_setup_track_writer_unlocked (
    GstAmbaEventRecorder * self, GstElement * pipeline, GstElement * mux,
    TrackPadContext * ctx, gboolean is_primary);

/* Trim whitespace in-place for mux-properties tokens. */
static gchar *
amba_event_recorder_strip_inplace (gchar *text)
{
  if (!text) {
    return NULL;
  }
  return g_strstrip (text);
}

/* Apply comma-separated key=value pairs from mux_properties to mux element. */
static void
amba_event_recorder_apply_mux_properties (GstAmbaEventRecorder * self, GstElement * mux)
{
  GstAmbaEventRecorderPrivate *priv = amba_event_recorder_get_priv (self);
  gchar **tokens = NULL;
  guint i = 0;

  if (!priv->mux_properties || !priv->mux_properties[0] || !mux) {
    return;
  }

  tokens = g_strsplit (priv->mux_properties, ",", -1);
  for (i = 0; tokens && tokens[i]; i++) {
    gchar *token = amba_event_recorder_strip_inplace (tokens[i]);
    gchar *eq = NULL;
    gchar *key = NULL;
    gchar *val = NULL;
    GParamSpec *pspec = NULL;

    if (!token || !token[0]) {
      continue;
    }
    eq = strchr (token, '=');
    if (!eq) {
      GST_WARNING_OBJECT (self,
          "invalid mux-properties token '%s', expected key=value", token);
      continue;
    }
    *eq = '\0';
    key = amba_event_recorder_strip_inplace (token);
    val = amba_event_recorder_strip_inplace (eq + 1);
    if (!key || !key[0]) {
      GST_WARNING_OBJECT (self, "invalid mux-properties token with empty key");
      continue;
    }
    if (!val) {
      val = "";
    }

    pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (mux), key);
    if (!pspec) {
      GST_WARNING_OBJECT (self, "unknown mux property '%s'", key);
      continue;
    }
    gst_util_set_object_arg (G_OBJECT (mux), key, val);
    GST_INFO_OBJECT (self, "mux-property applied %s=%s", key, val);
  }
  g_strfreev (tokens);
}

/* Typed pointer to G_DEFINE_TYPE private struct. */
static inline GstAmbaEventRecorderPrivate *
amba_event_recorder_get_priv (GstAmbaEventRecorder * self)
{
  return gst_amba_event_recorder_get_instance_private (self);
}

/* Allocate one track: empty cache queue and default last_running_time invalid. */
static TrackPadContext *
track_pad_context_new (const gchar *pad_name)
{
  TrackPadContext *ctx = g_new0 (TrackPadContext, 1);
  ctx->pad_name = g_strdup (pad_name ? pad_name : DEFAULT_PRIMARY_PAD_NAME);
  g_queue_init (&ctx->cached_buffers);
  ctx->last_running_time = GST_CLOCK_TIME_NONE;
  return ctx;
}

/* Drop all CachedBuffer on this track and reset size counters. */
static void
track_pad_context_clear_cache (TrackPadContext * ctx)
{
  if (!ctx) {
    return;
  }
  while (!g_queue_is_empty (&ctx->cached_buffers)) {
    CachedBuffer *cb = g_queue_pop_head (&ctx->cached_buffers);
    cached_buffer_free (cb);
  }
  ctx->cached_count = 0;
  ctx->cached_bytes = 0;
}

/* Free track: clear cache, unref input_caps, free struct. */
static void
track_pad_context_free (TrackPadContext * ctx)
{
  if (!ctx) {
    return;
  }
  track_pad_context_clear_cache (ctx);
  if (ctx->input_caps) {
    gst_caps_unref (ctx->input_caps);
    ctx->input_caps = NULL;
  }
  ctx->sink_ghost_pad = NULL;
  g_clear_pointer (&ctx->pad_name, g_free);
  g_free (ctx);
}

/* Hash lookup by pad name (caller holds lock). */
static TrackPadContext *
amba_event_recorder_lookup_track_unlocked (GstAmbaEventRecorderPrivate * priv,
    const gchar *pad_name)
{
  if (!priv || !priv->track_contexts || !pad_name) {
    return NULL;
  }
  return (TrackPadContext *) g_hash_table_lookup (priv->track_contexts, pad_name);
}

/* Find track whose internal appsink matches (signal callbacks). */
static TrackPadContext *
amba_event_recorder_lookup_track_by_appsink_unlocked (
    GstAmbaEventRecorderPrivate * priv, GstElement * appsink)
{
  GHashTableIter iter;
  gpointer value = NULL;

  if (!priv || !appsink || !priv->track_contexts) {
    return NULL;
  }
  g_hash_table_iter_init (&iter, priv->track_contexts);
  while (g_hash_table_iter_next (&iter, NULL, &value)) {
    TrackPadContext *ctx = (TrackPadContext *) value;
    if (ctx && ctx->appsink == appsink) {
      return ctx;
    }
  }
  return NULL;
}

/* TRUE if pad name is sink_video_%u (GOP / keyframe policy). */
static gboolean
amba_event_recorder_is_video_pad_name (const gchar * pad_name)
{
  return pad_name && g_str_has_prefix (pad_name, "sink_video_");
}

/* TRUE if pad name is sink_audio_%u. */
static gboolean
amba_event_recorder_is_audio_pad_name (const gchar * pad_name)
{
  return pad_name && g_str_has_prefix (pad_name, "sink_audio_");
}

/* Estimate GOP duration from last two keyframe PTS gaps in cache (clamped). Used for ACTIVE retain margin. */
static GstClockTime
amba_event_recorder_estimate_gop_ns_unlocked (TrackPadContext * ctx)
{
  GList *iter = NULL;
  GstClockTime latest_key = GST_CLOCK_TIME_NONE;
  GstClockTime prev_key = GST_CLOCK_TIME_NONE;
  GstClockTime delta = GST_CLOCK_TIME_NONE;

  if (!ctx || g_queue_is_empty (&ctx->cached_buffers)) {
    return GST_SECOND;
  }
  for (iter = ctx->cached_buffers.tail; iter; iter = iter->prev) {
    CachedBuffer *cb = (CachedBuffer *) iter->data;
    if (!cb || !cb->keyframe) {
      continue;
    }
    if (!GST_CLOCK_TIME_IS_VALID (latest_key)) {
      latest_key = cb->running_time;
    } else {
      prev_key = cb->running_time;
      break;
    }
  }
  if (GST_CLOCK_TIME_IS_VALID (latest_key) &&
      GST_CLOCK_TIME_IS_VALID (prev_key) &&
      latest_key > prev_key) {
    delta = latest_key - prev_key;
  }
  if (!GST_CLOCK_TIME_IS_VALID (delta) || delta == 0) {
    delta = GST_SECOND;
  }
  /* Bound abnormal estimates and keep a conservative margin. */
  if (delta < 100 * GST_MSECOND) {
    delta = 100 * GST_MSECOND;
  } else if (delta > 5 * GST_SECOND) {
    delta = 5 * GST_SECOND;
  }
  return delta;
}

/* Lookup primary_pad_name in track_contexts (NULL if pad not requested yet). */
static TrackPadContext *
amba_event_recorder_get_primary_track_unlocked (GstAmbaEventRecorder * self)
{
  GstAmbaEventRecorderPrivate *priv = amba_event_recorder_get_priv (self);
  const gchar *primary = NULL;

  if (!priv) {
    return NULL;
  }
  primary = (priv->primary_pad_name && priv->primary_pad_name[0]) ?
      priv->primary_pad_name : DEFAULT_PRIMARY_PAD_NAME;
  return amba_event_recorder_lookup_track_unlocked (priv, primary);
}

/* Next free sink_video_N or sink_audio_N name not yet in track_contexts. */
static gchar *
amba_event_recorder_pick_request_pad_name_unlocked (GstAmbaEventRecorderPrivate * priv,
    gboolean is_video)
{
  const gchar *prefix = is_video ? "sink_video_" : "sink_audio_";
  guint idx = 0;
  gchar *ret = NULL;

  if (priv && priv->track_contexts) {
    for (idx = 0; idx < G_MAXUINT; idx++) {
      gchar *candidate = g_strdup_printf ("%s%u", prefix, idx);
      if (!candidate) {
        break;
      }
      if (!g_hash_table_contains (priv->track_contexts, candidate)) {
        ret = candidate;
        break;
      }
      g_free (candidate);
    }
  }
  return ret;
}

/* Create queue+appsink, ghost sink pad, signals; insert into track_contexts. */
static TrackPadContext *
amba_event_recorder_add_track_unlocked (GstAmbaEventRecorder * self,
    const gchar * pad_name, gboolean is_video)
{
  GstAmbaEventRecorderPrivate *priv = amba_event_recorder_get_priv (self);
  TrackPadContext *ctx = NULL;
  GstPad *sink_pad = NULL;
  GstPadTemplate *sink_templ = NULL;
  gchar *queue_name = NULL;
  gchar *appsink_name = NULL;

  if (!pad_name || !pad_name[0]) {
    goto out;
  }
  if (amba_event_recorder_lookup_track_unlocked (priv, pad_name)) {
    goto out;
  }
  queue_name = g_strdup_printf ("amba-event-recorder-queue-%s", pad_name);
  appsink_name = g_strdup_printf ("amba-event-recorder-appsink-%s", pad_name);
  if (!queue_name || !appsink_name) {
    g_free (queue_name);
    g_free (appsink_name);
    goto out;
  }

  ctx = track_pad_context_new (pad_name);
  if (!ctx) {
    g_free (queue_name);
    g_free (appsink_name);
    goto out;
  }

  ctx->queue = gst_element_factory_make ("queue", queue_name);
  ctx->appsink = gst_element_factory_make ("appsink", appsink_name);
  g_free (queue_name);
  g_free (appsink_name);
  if (!ctx->queue || !ctx->appsink) {
    GST_ERROR_OBJECT (self, "failed to create ingest elements for %s", pad_name);
    track_pad_context_free (ctx);
    ctx = NULL;
    goto out;
  }

  g_object_set (ctx->queue,
      "max-size-time", (guint64) 0,
      "max-size-bytes", (guint) 0,
      "max-size-buffers", (guint) 0, NULL);
  g_object_set (ctx->appsink,
      "emit-signals", TRUE,
      "sync", FALSE,
      "async", FALSE,
      "max-buffers", (guint) 0,
      "drop", FALSE, NULL);

  gst_bin_add_many (GST_BIN (self), ctx->queue, ctx->appsink, NULL);
  if (!gst_element_link (ctx->queue, ctx->appsink)) {
    GST_ERROR_OBJECT (self, "failed link queue->appsink for %s", pad_name);
    gst_bin_remove_many (GST_BIN (self), ctx->queue, ctx->appsink, NULL);
    track_pad_context_free (ctx);
    ctx = NULL;
    goto out;
  }
  sink_pad = gst_element_get_static_pad (ctx->queue, "sink");
  if (!sink_pad) {
    GST_ERROR_OBJECT (self, "failed get queue sink pad for %s", pad_name);
    gst_bin_remove_many (GST_BIN (self), ctx->queue, ctx->appsink, NULL);
    track_pad_context_free (ctx);
    ctx = NULL;
    goto out;
  }

  sink_templ = gst_static_pad_template_get (is_video ?
      &sink_video_request_template : &sink_audio_request_template);
  ctx->sink_ghost_pad = gst_ghost_pad_new_from_template (pad_name, sink_pad, sink_templ);
  gst_object_unref (sink_templ);
  sink_templ = NULL;
  gst_object_unref (sink_pad);
  sink_pad = NULL;

  if (!ctx->sink_ghost_pad || !gst_element_add_pad (GST_ELEMENT (self), ctx->sink_ghost_pad)) {
    GST_ERROR_OBJECT (self, "failed adding sink ghost pad %s", pad_name);
    if (ctx->sink_ghost_pad) {
      gst_object_unref (ctx->sink_ghost_pad);
      ctx->sink_ghost_pad = NULL;
    }
    gst_bin_remove_many (GST_BIN (self), ctx->queue, ctx->appsink, NULL);
    track_pad_context_free (ctx);
    ctx = NULL;
    goto out;
  }

  g_signal_connect (ctx->appsink, "new-sample",
      G_CALLBACK (amba_event_recorder_on_new_sample), self);
  g_signal_connect (ctx->appsink, "new-preroll",
      G_CALLBACK (amba_event_recorder_on_new_preroll), self);
  g_signal_connect (ctx->appsink, "eos",
      G_CALLBACK (amba_event_recorder_on_eos), self);

  g_hash_table_insert (priv->track_contexts, g_strdup (pad_name), ctx);
  GST_INFO_OBJECT (self, "pad-added name=%s type=%s", pad_name, is_video ? "video" : "audio");

out:
  return ctx;
}

/* Unref cached GstBuffer and free CachedBuffer struct. */
static void
cached_buffer_free (CachedBuffer * cb)
{
  if (!cb) {
    return;
  }
  if (cb->buffer) {
    gst_buffer_unref (cb->buffer);
  }
  g_free (cb);
}

/* Post GstMessage element with window-id and start/end running times. */
static void
amba_event_recorder_post_window_message (GstAmbaEventRecorder * self,
    const gchar * name, guint64 window_id, GstClockTime start, GstClockTime end)
{
  GstStructure *s = gst_structure_new (name,
      "window-id", G_TYPE_UINT64, window_id,
      "start-running-time", G_TYPE_UINT64, start,
      "end-running-time", G_TYPE_UINT64, end, NULL);
  GstMessage *msg = gst_message_new_element (GST_OBJECT (self), s);
  gst_element_post_message (GST_ELEMENT (self), msg);
}

/* Post window-committed or window-commit-failed with path and buffer count. */
static void
amba_event_recorder_post_commit_message (GstAmbaEventRecorder * self,
    const gchar * name, guint64 window_id, const gchar *location, guint buffers, const gchar *detail)
{
  GstStructure *s = gst_structure_new (name,
      "window-id", G_TYPE_UINT64, window_id,
      "location", G_TYPE_STRING, location ? location : "",
      "buffer-count", G_TYPE_UINT, buffers,
      "detail", G_TYPE_STRING, detail ? detail : "", NULL);
  GstMessage *msg = gst_message_new_element (GST_OBJECT (self), s);
  gst_element_post_message (GST_ELEMENT (self), msg);
}

/* Element running time: clock_time - base_time (for fallback timestamps). */
static GstClockTime
amba_event_recorder_get_running_time (GstAmbaEventRecorder * self)
{
  GstClockTime ret = GST_CLOCK_TIME_NONE;
  GstClock *clock = gst_element_get_clock (GST_ELEMENT (self));

  if (!clock) {
    return GST_CLOCK_TIME_NONE;
  }

  {
    GstClockTime now = gst_clock_get_time (clock);
    GstClockTime base_time = gst_element_get_base_time (GST_ELEMENT (self));
    if (GST_CLOCK_TIME_IS_VALID (base_time) && now >= base_time) {
      ret = now - base_time;
    }
  }

  gst_object_unref (clock);
  return ret;
}

/* Choose per-buffer running time: segment time, else clock, else monotonic fallback; non-decreasing per track. */
static GstClockTime
amba_event_recorder_normalize_running_time_unlocked (TrackPadContext * ctx,
    GstClockTime buffer_rt, GstClockTime clock_rt)
{
  GstClockTime rt = GST_CLOCK_TIME_NONE;

  if (GST_CLOCK_TIME_IS_VALID (buffer_rt)) {
    rt = buffer_rt;
  } else if (GST_CLOCK_TIME_IS_VALID (clock_rt)) {
    rt = clock_rt;
  } else if (ctx && GST_CLOCK_TIME_IS_VALID (ctx->last_running_time)) {
    rt = ctx->last_running_time + GST_MSECOND;
  } else {
    rt = 0;
  }

  if (ctx && GST_CLOCK_TIME_IS_VALID (ctx->last_running_time) &&
      rt < ctx->last_running_time) {
    rt = ctx->last_running_time;
  }
  return rt;
}

/* Remove oldest cache entry and update cached_count / cached_bytes. */
static void
amba_event_recorder_drop_head_unlocked (TrackPadContext * ctx)
{
  CachedBuffer *head = g_queue_pop_head (&ctx->cached_buffers);
  if (!head) {
    return;
  }
  ctx->cached_count--;
  if (ctx->cached_bytes >= gst_buffer_get_size (head->buffer)) {
    ctx->cached_bytes -= gst_buffer_get_size (head->buffer);
  } else {
    ctx->cached_bytes = 0;
  }
  GST_DEBUG ("cache-drop-head track=%s rt=%" GST_TIME_FORMAT " key=%d remain-count=%u remain-bytes=%" G_GUINT64_FORMAT,
      ctx->pad_name ? ctx->pad_name : "unknown",
      GST_TIME_ARGS (head->running_time), head->keyframe,
      ctx->cached_count, ctx->cached_bytes);
  cached_buffer_free (head);
}

/* IDLE: drop head until cache starts at a keyframe (keep latest GOP tail only). */
static void
amba_event_recorder_prune_idle_gop_unlocked (TrackPadContext * ctx)
{
  GList *iter = NULL;
  GList *first_key_node = NULL;

  /* Find first keyframe node. */
  for (iter = ctx->cached_buffers.head; iter; iter = iter->next) {
    CachedBuffer *cb = (CachedBuffer *) iter->data;
    if (cb && cb->keyframe) {
      first_key_node = iter;
      break;
    }
  }
  if (!first_key_node) {
    return;
  }
  while (ctx->cached_buffers.head && ctx->cached_buffers.head != first_key_node) {
    amba_event_recorder_drop_head_unlocked (ctx);
  }
}

/* TRUE if cache exceeds time span (max_cache_ms), buffer count, or byte budget. */
static gboolean
amba_event_recorder_idle_over_limits_unlocked (TrackPadContext * ctx,
    GstAmbaEventRecorderPrivate * priv, GstClockTime current_rt)
{
  GstClockTime min_rt = GST_CLOCK_TIME_NONE;
  CachedBuffer *head = NULL;
  gboolean over = FALSE;

  if (priv->max_cache_ms > 0 && GST_CLOCK_TIME_IS_VALID (current_rt)) {
    GstClockTime cache_ns = (GstClockTime) priv->max_cache_ms * GST_MSECOND;
    min_rt = (current_rt > cache_ns) ? (current_rt - cache_ns) : 0;
  }

  head = (CachedBuffer *) g_queue_peek_head (&ctx->cached_buffers);
  if (head) {
    over = (GST_CLOCK_TIME_IS_VALID (min_rt) && head->running_time < min_rt)
        || (priv->max_buffers > 0 && ctx->cached_count > priv->max_buffers)
        || (priv->max_bytes > 0 && ctx->cached_bytes > priv->max_bytes);
  }
  return over;
}

/* Remove [first keyframe, next keyframe) from head; FALSE if fewer than two keyframes. */
static gboolean
amba_event_recorder_drop_oldest_complete_gop_unlocked (TrackPadContext * ctx)
{
  GList *iter = NULL;
  GList *first_key_node = NULL;
  GList *second_key_node = NULL;
  GstClockTime first_rt = GST_CLOCK_TIME_NONE;
  GstClockTime second_rt = GST_CLOCK_TIME_NONE;

  for (iter = ctx->cached_buffers.head; iter; iter = iter->next) {
    CachedBuffer *cb = (CachedBuffer *) iter->data;
    if (!cb || !cb->keyframe) {
      continue;
    }
    if (!first_key_node) {
      first_key_node = iter;
      first_rt = cb->running_time;
    } else if (cb->running_time > first_rt) {
      second_key_node = iter;
      second_rt = cb->running_time;
      break;
    }
  }

  if (!second_key_node) {
    return FALSE;
  }

  while (ctx->cached_buffers.head && ctx->cached_buffers.head != second_key_node) {
    amba_event_recorder_drop_head_unlocked (ctx);
  }
  GST_DEBUG ("cache-drop-gop track=%s gop-start=%" GST_TIME_FORMAT " next-key=%" GST_TIME_FORMAT
      " remain-count=%u remain-bytes=%" G_GUINT64_FORMAT,
      ctx->pad_name ? ctx->pad_name : "unknown",
      GST_TIME_ARGS (first_rt), GST_TIME_ARGS (second_rt),
      ctx->cached_count, ctx->cached_bytes);
  return TRUE;
}

/* TRUE if cache holds at least two keyframes (one full GOP interval). */
static gboolean
amba_event_recorder_has_complete_gop_unlocked (TrackPadContext * ctx)
{
  GList *iter = NULL;
  guint key_count = 0;

  if (!ctx) {
    return FALSE;
  }

  for (iter = ctx->cached_buffers.head; iter; iter = iter->next) {
    CachedBuffer *cb = (CachedBuffer *) iter->data;
    if (!cb || !cb->keyframe) {
      continue;
    }
    key_count++;
    if (key_count >= 2) {
      return TRUE;
    }
  }
  return FALSE;
}

/* IDLE under size limits: prefer dropping whole GOPs; else head-drop with optional warning. */
static void
amba_event_recorder_prune_limits_unlocked (TrackPadContext * ctx,
    GstAmbaEventRecorderPrivate * priv, GstClockTime current_rt)
{
  if (!ctx || !priv) {
    return;
  }

  if (!amba_event_recorder_idle_over_limits_unlocked (ctx, priv, current_rt)) {
    ctx->warned_no_complete_gop_under_pressure = FALSE;
    return;
  }

  while (amba_event_recorder_idle_over_limits_unlocked (ctx, priv, current_rt)) {
    if (amba_event_recorder_drop_oldest_complete_gop_unlocked (ctx)) {
      ctx->warned_no_complete_gop_under_pressure = FALSE;
      continue;
    }

    if (!ctx->warned_no_complete_gop_under_pressure) {
      const gchar *track_name = ctx->pad_name ? ctx->pad_name : "unknown";
      const gchar *cache_ms_hint = priv->max_cache_ms > 0 ? "set" : "unlimited";
      const gchar *cache_buf_hint = priv->max_buffers > 0 ? "set" : "unlimited";
      const gchar *cache_bytes_hint = priv->max_bytes > 0 ? "set" : "unlimited";
      gboolean has_complete_gop = amba_event_recorder_has_complete_gop_unlocked (ctx);
      GST_WARNING ("cache under pressure without complete GOP on %s: "
          "cached-count=%u cached-bytes=%" G_GUINT64_FORMAT
          " limits(ms=%u:%s buffers=%u:%s bytes=%" G_GUINT64_FORMAT ":%s) "
          "complete-gop=%d; fallback to buffer drop. Consider increasing cache limits.",
          track_name,
          ctx->cached_count, ctx->cached_bytes,
          priv->max_cache_ms, cache_ms_hint,
          priv->max_buffers, cache_buf_hint,
          priv->max_bytes, cache_bytes_hint,
          has_complete_gop);
      ctx->warned_no_complete_gop_under_pressure = TRUE;
    }
    amba_event_recorder_drop_head_unlocked (ctx);
  }

  if (amba_event_recorder_has_complete_gop_unlocked (ctx)) {
    ctx->warned_no_complete_gop_under_pressure = FALSE;
  }
}

/* ACTIVE: drop written buffers from head while keeping last retain_ns (pre-roll + optional GOP margin). */
static void
amba_event_recorder_prune_active_written_unlocked (TrackPadContext * ctx,
    GstAmbaEventRecorderPrivate * priv, GstClockTime current_rt)
{
  GstClockTime retain_ns = 0;
  GstClockTime min_keep_rt = 0;
  GstClockTime gop_margin_ns = 0;

  if (!ctx || !priv) {
    return;
  }
  if (priv->active_retain_pre_ms < 0) {
    if (priv->max_cache_ms > 0) {
      retain_ns = (GstClockTime) priv->max_cache_ms * GST_MSECOND;
    } else {
      /* pre=-1 with no cache cap: keep written buffers, rely on upper layers. */
      return;
    }
  } else if (priv->active_retain_pre_ms > 0) {
    retain_ns = (GstClockTime) priv->active_retain_pre_ms * GST_MSECOND;
  }
  if (priv->align_start_keyframe &&
      amba_event_recorder_is_video_pad_name (ctx->pad_name)) {
    /*
     * Preserve one extra GOP in ACTIVE mode to avoid boundary-trigger
     * pre-roll starvation after keyframe alignment.
     */
    gop_margin_ns = amba_event_recorder_estimate_gop_ns_unlocked (ctx);
    retain_ns += gop_margin_ns;
  }
  if (GST_CLOCK_TIME_IS_VALID (current_rt) && retain_ns > 0) {
    min_keep_rt = (current_rt > retain_ns) ? (current_rt - retain_ns) : 0;
  } else {
    min_keep_rt = 0;
  }

  while (!g_queue_is_empty (&ctx->cached_buffers)) {
    CachedBuffer *head = (CachedBuffer *) g_queue_peek_head (&ctx->cached_buffers);
    if (!head || !head->written || head->running_time >= min_keep_rt) {
      break;
    }
    amba_event_recorder_drop_head_unlocked (ctx);
  }
}

/* Clear written on all CachedBuffer in a track (new window may re-push pre-roll). */
static void
amba_event_recorder_reset_written_flags_unlocked (TrackPadContext * ctx)
{
  GList *iter = NULL;

  if (!ctx) {
    return;
  }
  for (iter = ctx->cached_buffers.head; iter; iter = iter->next) {
    CachedBuffer *cb = (CachedBuffer *) iter->data;
    if (cb) {
      cb->written = FALSE;
    }
  }
}

/*
 * Window start time for writer: primary track, align-start-keyframe uses last keyframe <= window_start
 * and returns its serial for node-aligned push; else first sample >= window_start; may warn and fallback.
 */
static GstClockTime
amba_event_recorder_select_start_unlocked (GstAmbaEventRecorder * self,
    guint64 * out_serial, gboolean * out_key_aligned)
{
  GstAmbaEventRecorderPrivate *priv = amba_event_recorder_get_priv (self);
  TrackPadContext *ctx = amba_event_recorder_get_primary_track_unlocked (self);
  GList *iter = NULL;
  GstClockTime selected = priv->window_start;
  guint64 selected_serial = INVALID_BUFFER_SERIAL;
  gboolean found = FALSE;

  if (out_serial) {
    *out_serial = INVALID_BUFFER_SERIAL;
  }
  if (out_key_aligned) {
    *out_key_aligned = FALSE;
  }
  if (!ctx) {
    GST_WARNING_OBJECT (self, "missing primary track context for start select");
    goto done;
  }

  if (!priv->align_start_keyframe) {
    for (iter = ctx->cached_buffers.head; iter; iter = iter->next) {
      CachedBuffer *cb = (CachedBuffer *) iter->data;
      if (!cb) {
        continue;
      }
      if (cb->running_time >= priv->window_start) {
        selected = cb->running_time;
        selected_serial = cb->serial;
        break;
      }
    }
    goto done;
  }

  for (iter = ctx->cached_buffers.head; iter; iter = iter->next) {
    CachedBuffer *cb = (CachedBuffer *) iter->data;
    if (!cb) {
      continue;
    }
    if (cb->running_time > priv->window_start) {
      break;
    }
    if (cb->keyframe) {
      selected = cb->running_time;
      selected_serial = cb->serial;
      found = TRUE;
    }
  }

  if (!found) {
    for (iter = ctx->cached_buffers.head; iter; iter = iter->next) {
      CachedBuffer *cb = (CachedBuffer *) iter->data;
      if (!cb) {
        continue;
      }
      if (cb->running_time >= priv->window_start) {
        selected = cb->running_time;
        selected_serial = cb->serial;
        break;
      }
    }
    GST_WARNING_OBJECT (self,
        "strict keyframe start unavailable for window=%" G_GUINT64_FORMAT
        ", fallback to hard-start=%" GST_TIME_FORMAT,
        priv->window_id, GST_TIME_ARGS (priv->window_start));
  } else {
    GST_INFO_OBJECT (self,
        "start-select window=%" G_GUINT64_FORMAT
        " hard-start=%" GST_TIME_FORMAT " selected-start=%" GST_TIME_FORMAT,
        priv->window_id, GST_TIME_ARGS (priv->window_start), GST_TIME_ARGS (selected));
  }

done:
  if (out_serial) {
    *out_serial = selected_serial;
  }
  if (out_key_aligned) {
    *out_key_aligned = found;
  }
  return selected;
}

/* Link writer tail (appsrc or parser src) to mux sink pad; retry mux pad query with NULL caps. */
static gboolean
amba_event_recorder_link_to_mux_unlocked (GstAmbaEventRecorder * self,
    GstElement * src_tail, GstElement * mux, TrackPadContext * ctx)
{
  GstPad *src_pad = NULL;
  GstPad *mux_pad = NULL;
  GstPadLinkReturn lret = GST_PAD_LINK_OK;
  gboolean ok = FALSE;

  src_pad = gst_element_get_static_pad (src_tail, "src");
  if (!src_pad) {
    GST_ERROR_OBJECT (self, "missing src pad on writer tail for %s",
        ctx->pad_name ? ctx->pad_name : "unknown");
    goto done;
  }
  mux_pad = gst_element_get_compatible_pad (mux, src_pad, ctx->input_caps);
  if (!mux_pad) {
    /* Some muxers reject over-specific caps here; retry with NULL caps filter. */
    mux_pad = gst_element_get_compatible_pad (mux, src_pad, NULL);
  }
  if (!mux_pad) {
    GST_WARNING_OBJECT (self, "no compatible mux pad for track=%s caps=%" GST_PTR_FORMAT,
        ctx->pad_name ? ctx->pad_name : "unknown", ctx->input_caps);
    gst_object_unref (src_pad);
    goto done;
  }
  lret = gst_pad_link (src_pad, mux_pad);
  gst_object_unref (src_pad);
  if (lret != GST_PAD_LINK_OK) {
    GST_WARNING_OBJECT (self, "failed linking track=%s to mux: %s",
        ctx->pad_name ? ctx->pad_name : "unknown",
        gst_pad_link_get_name (lret));
    gst_object_unref (mux_pad);
    goto done;
  }
  ctx->writer_mux_pad = mux_pad;
  ok = TRUE;

done:
  return ok;
}

/* Per-track writer branch: appsrc [+ h26x parse] -> mux (see forward decl). */
static gboolean
amba_event_recorder_setup_track_writer_unlocked (GstAmbaEventRecorder * self,
    GstElement * pipeline, GstElement * mux, TrackPadContext * ctx, gboolean is_primary)
{
  GstStructure *st = NULL;
  const gchar *caps_name = NULL;
  const gchar *parser_factory = NULL;
  GstElement *tail = NULL;
  gboolean ok = FALSE;

  if (!ctx || !ctx->input_caps) {
    goto done;
  }
  st = gst_caps_get_structure (ctx->input_caps, 0);
  caps_name = st ? gst_structure_get_name (st) : NULL;

  ctx->writer_appsrc = gst_element_factory_make ("appsrc", NULL);
  if (!ctx->writer_appsrc) {
    GST_ERROR_OBJECT (self, "failed creating appsrc for %s",
        ctx->pad_name ? ctx->pad_name : "unknown");
    goto done;
  }
  g_object_set (ctx->writer_appsrc,
      "is-live", FALSE,
      "format", GST_FORMAT_TIME,
      /*
       * Multi-track writer pushes under recorder lock. Keep appsrc non-blocking
       * to avoid deadlock when mux back-pressure appears during trigger path.
       */
      "block", FALSE,
      "max-bytes", (guint64) 0,
      "caps", ctx->input_caps,
      NULL);
  gst_bin_add (GST_BIN (pipeline), ctx->writer_appsrc);
  tail = ctx->writer_appsrc;

  if (caps_name && g_strcmp0 (caps_name, "video/x-h264") == 0) {
    parser_factory = "h264parse";
  } else if (caps_name && g_strcmp0 (caps_name, "video/x-h265") == 0) {
    parser_factory = "h265parse";
  }

  if (parser_factory) {
    ctx->writer_parser = gst_element_factory_make (parser_factory, NULL);
    if (!ctx->writer_parser) {
      GST_ERROR_OBJECT (self, "failed creating parser=%s for %s",
          parser_factory, ctx->pad_name ? ctx->pad_name : "unknown");
      goto done;
    }
    if (g_object_class_find_property (G_OBJECT_GET_CLASS (ctx->writer_parser), "config-interval")) {
      g_object_set (ctx->writer_parser, "config-interval", -1, NULL);
      GST_INFO_OBJECT (self, "writer-parser track=%s factory=%s config-interval=-1",
          ctx->pad_name ? ctx->pad_name : "unknown", parser_factory);
    }
    gst_bin_add (GST_BIN (pipeline), ctx->writer_parser);
    if (!gst_element_link (ctx->writer_appsrc, ctx->writer_parser)) {
      GST_ERROR_OBJECT (self, "failed link appsrc->parser for track=%s",
          ctx->pad_name ? ctx->pad_name : "unknown");
      goto done;
    }
    tail = ctx->writer_parser;
  }

  if (!amba_event_recorder_link_to_mux_unlocked (self, tail, mux, ctx)) {
    if (is_primary) {
      GST_ERROR_OBJECT (self, "primary track cannot link to mux: %s",
          ctx->pad_name ? ctx->pad_name : "unknown");
    } else {
      GST_WARNING_OBJECT (self,
          "skip non-primary track for this mux track=%s caps=%" GST_PTR_FORMAT,
          ctx->pad_name ? ctx->pad_name : "unknown", ctx->input_caps);
    }
    goto done;
  }
  GST_INFO_OBJECT (self, "writer-track-open track=%s caps=%s",
      ctx->pad_name ? ctx->pad_name : "unknown",
      caps_name ? caps_name : "unknown");
  ok = TRUE;

done:
  return ok;
}

/* Build internal mux+filesink pipeline, select_start, link all tracks with caps, set PLAYING. */
static gboolean
amba_event_recorder_open_writer_unlocked (GstAmbaEventRecorder * self)
{
  GstAmbaEventRecorderPrivate *priv = amba_event_recorder_get_priv (self);
  TrackPadContext *primary_ctx = amba_event_recorder_get_primary_track_unlocked (self);
  GstElement *pipeline = NULL;
  GstElement *mux = NULL;
  GstElement *sink = NULL;
  gchar *location = NULL;
  const gchar *mux_factory = NULL;
  GstClockTime selected_start = GST_CLOCK_TIME_NONE;
  guint64 selected_serial = INVALID_BUFFER_SERIAL;
  gboolean key_aligned = FALSE;
  GHashTableIter iter;
  gpointer value = NULL;
  guint linked_tracks = 0;
  gboolean ok = FALSE;

  if (priv->writer_pipeline) {
    ok = TRUE;
    goto out;
  }
  if (priv->fatal_error) {
    goto out;
  }
  if (!primary_ctx || !primary_ctx->input_caps) {
    GST_DEBUG_OBJECT (self, "writer-open-deferred reason=no-input-caps");
    goto out;
  }

  /*
   * Start a fresh write cycle for a new window:
   * retained pre-cache may contain buffers written by previous window,
   * but they must be eligible for re-push to preserve pre-roll continuity.
   */
  g_hash_table_iter_init (&iter, priv->track_contexts);
  while (g_hash_table_iter_next (&iter, NULL, &value)) {
    TrackPadContext *ctx = (TrackPadContext *) value;
    amba_event_recorder_reset_written_flags_unlocked (ctx);
  }

  selected_start = amba_event_recorder_select_start_unlocked (self,
      &selected_serial, &key_aligned);
  location = g_strdup_printf (priv->location, (gint) (priv->window_id - 1));
  if (!location) {
    goto out;
  }
  mux_factory = (priv->mux_factory && priv->mux_factory[0]) ?
      priv->mux_factory : DEFAULT_MUX_FACTORY;

  pipeline = gst_pipeline_new (NULL);
  mux = gst_element_factory_make (mux_factory, NULL);
  sink = gst_element_factory_make ("filesink", NULL);
  if (!pipeline || !mux || !sink) {
    GST_ERROR_OBJECT (self, "failed creating active writer elements (mux=%s)",
        mux_factory);
    if (pipeline) {
      gst_object_unref (pipeline);
    }
    if (mux) {
      gst_object_unref (mux);
    }
    if (sink) {
      gst_object_unref (sink);
    }
    g_free (location);
    goto out;
  }

  amba_event_recorder_apply_mux_properties (self, mux);
  g_object_set (sink, "location", location, "sync", FALSE, NULL);

  gst_bin_add_many (GST_BIN (pipeline), mux, sink, NULL);
  g_hash_table_iter_init (&iter, priv->track_contexts);
  while (g_hash_table_iter_next (&iter, NULL, &value)) {
    TrackPadContext *ctx = (TrackPadContext *) value;
    gboolean is_primary = (ctx == primary_ctx);
    if (!ctx || !ctx->input_caps) {
      continue;
    }
    ctx->writer_appsrc = NULL;
    ctx->writer_parser = NULL;
    if (ctx->writer_mux_pad) {
      gst_object_unref (ctx->writer_mux_pad);
      ctx->writer_mux_pad = NULL;
    }
    if (amba_event_recorder_setup_track_writer_unlocked (self, pipeline, mux, ctx, is_primary)) {
      linked_tracks++;
      continue;
    }
    if (is_primary) {
      GST_ERROR_OBJECT (self, "failed preparing primary writer track");
      priv->fatal_error = TRUE;
      GST_ELEMENT_ERROR (self, CORE, NEGOTIATION,
          ("primary writer track '%s' cannot link to mux '%s'",
              ctx->pad_name ? ctx->pad_name : "unknown", mux_factory),
          ("window=%" G_GUINT64_FORMAT, priv->window_id));
      gst_element_set_state (pipeline, GST_STATE_NULL);
      gst_object_unref (pipeline);
      g_free (location);
      goto out;
    }
  }
  if (linked_tracks == 0 ||
      !primary_ctx->writer_appsrc ||
      !gst_element_link (mux, sink) ||
      gst_element_set_state (pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
    GST_ERROR_OBJECT (self, "failed opening active writer");
    if (!primary_ctx->writer_appsrc || linked_tracks == 0) {
      priv->fatal_error = TRUE;
      GST_ELEMENT_ERROR (self, CORE, NEGOTIATION,
          ("writer open failed: no usable primary track '%s'",
              primary_ctx->pad_name ? primary_ctx->pad_name : "unknown"),
          ("window=%" G_GUINT64_FORMAT, priv->window_id));
    }
    gst_element_set_state (pipeline, GST_STATE_NULL);
    gst_object_unref (pipeline);
    g_free (location);
    goto out;
  }

  priv->writer_pipeline = pipeline;
  priv->writer_start_rt = selected_start;
  priv->writer_start_serial = selected_serial;
  priv->writer_last_written_rt = GST_CLOCK_TIME_NONE;
  priv->writer_written_count = 0;
  g_clear_pointer (&priv->writer_location, g_free);
  priv->writer_location = location;

  GST_INFO_OBJECT (self,
      "writer-open window=%" G_GUINT64_FORMAT " start=%" GST_TIME_FORMAT " end=%" GST_TIME_FORMAT
      " mux=%s tracks=%u start-serial=%" G_GUINT64_FORMAT " key-aligned=%d location=%s",
      priv->window_id, GST_TIME_ARGS (priv->writer_start_rt),
      GST_TIME_ARGS (priv->window_end), mux_factory, linked_tracks,
      priv->writer_start_serial, key_aligned, priv->writer_location);
  ok = TRUE;

out:
  return ok;
}

/* Push one cached buffer to writer appsrc; mark written and bump primary counter if applicable. */
static gboolean
amba_event_recorder_push_one_unlocked (GstAmbaEventRecorder * self,
    TrackPadContext * ctx, CachedBuffer * cb)
{
  GstAmbaEventRecorderPrivate *priv = amba_event_recorder_get_priv (self);
  GstFlowReturn flow_ret = GST_FLOW_OK;
  gboolean is_video = amba_event_recorder_is_video_pad_name (ctx ? ctx->pad_name : NULL);

  if (!cb || cb->written || !ctx || !ctx->writer_appsrc) {
    return FALSE;
  }
  if (!GST_CLOCK_TIME_IS_VALID (priv->writer_last_written_rt)) {
    GST_DEBUG_OBJECT (self,
        "first-push-ts window=%" G_GUINT64_FORMAT
        " pts-valid=%d dts-valid=%d pts=%" GST_TIME_FORMAT " dts=%" GST_TIME_FORMAT,
        priv->window_id,
        GST_BUFFER_PTS_IS_VALID (cb->buffer), GST_BUFFER_DTS_IS_VALID (cb->buffer),
        GST_TIME_ARGS (GST_BUFFER_PTS (cb->buffer)),
        GST_TIME_ARGS (GST_BUFFER_DTS (cb->buffer)));
  }
  g_signal_emit_by_name (ctx->writer_appsrc, "push-buffer", cb->buffer, &flow_ret);
  if (flow_ret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (self, "push-buffer failed track=%s err=%s",
        ctx->pad_name ? ctx->pad_name : "unknown", gst_flow_get_name (flow_ret));
    return FALSE;
  }
  if (!GST_CLOCK_TIME_IS_VALID (priv->writer_last_written_rt)) {
    GST_INFO_OBJECT (self,
        "first-push window=%" G_GUINT64_FORMAT " track=%s rt=%" GST_TIME_FORMAT " key=%d",
        priv->window_id, ctx->pad_name ? ctx->pad_name : "unknown",
        GST_TIME_ARGS (cb->running_time), cb->keyframe);
    if (is_video && !cb->keyframe) {
      GST_WARNING_OBJECT (self,
          "first-push is non-keyframe; parser may drop until next keyframe");
    }
  }
  cb->written = TRUE;
  if (ctx == amba_event_recorder_get_primary_track_unlocked (self)) {
    priv->writer_written_count++;
  }
  priv->writer_last_written_rt = cb->running_time;
  return TRUE;
}

/* Push cached buffers for one track up to upto_rt (primary uses writer_start_serial when set). */
static guint
amba_event_recorder_push_pending_track_unlocked (GstAmbaEventRecorder * self,
    TrackPadContext * ctx, GstClockTime upto_rt)
{
  GstAmbaEventRecorderPrivate *priv = amba_event_recorder_get_priv (self);
  TrackPadContext *primary_ctx = amba_event_recorder_get_primary_track_unlocked (self);
  gboolean is_primary = (ctx == primary_ctx);
  GList *iter = NULL;
  guint pushed = 0;

  if (!ctx || !ctx->writer_appsrc) {
    return 0;
  }
  for (iter = ctx->cached_buffers.head; iter; iter = iter->next) {
    CachedBuffer *cb = (CachedBuffer *) iter->data;
    if (!cb) {
      continue;
    }
    if (is_primary && priv->writer_start_serial != INVALID_BUFFER_SERIAL) {
      if (cb->serial < priv->writer_start_serial) {
        continue;
      }
    } else {
      if (cb->running_time < priv->writer_start_rt) {
        continue;
      }
    }
    if (cb->running_time > upto_rt) {
      break;
    }
    if (!cb->written && amba_event_recorder_push_one_unlocked (self, ctx, cb)) {
      pushed++;
    }
  }
  if (pushed > 0) {
    amba_event_recorder_prune_active_written_unlocked (ctx, priv, upto_rt);
  }
  return pushed;
}

/* Push all tracks up to min(upto_rt, each track last_running_time); then ACTIVE prune per track. */
static guint
amba_event_recorder_push_pending_unlocked (GstAmbaEventRecorder * self, GstClockTime upto_rt)
{
  GstAmbaEventRecorderPrivate *priv = amba_event_recorder_get_priv (self);
  GHashTableIter iter;
  gpointer value = NULL;
  guint pushed = 0;
  guint track_count = 0;

  if (!priv->writer_pipeline) {
    return 0;
  }
  g_hash_table_iter_init (&iter, priv->track_contexts);
  while (g_hash_table_iter_next (&iter, NULL, &value)) {
    TrackPadContext *ctx = (TrackPadContext *) value;
    GstClockTime track_upto = upto_rt;
    if (!ctx || !ctx->writer_appsrc || !GST_CLOCK_TIME_IS_VALID (ctx->last_running_time)) {
      continue;
    }
    track_upto = MIN (track_upto, ctx->last_running_time);
    pushed += amba_event_recorder_push_pending_track_unlocked (self, ctx, track_upto);
    track_count++;
  }
  if (pushed > 0) {
    GST_DEBUG_OBJECT (self,
        "push-pending window=%" G_GUINT64_FORMAT " upto=%" GST_TIME_FORMAT
        " pushed=%u tracks=%u",
        priv->window_id, GST_TIME_ARGS (upto_rt), pushed, track_count);
  }
  return pushed;
}

/* EOS each writer appsrc, wait for mux EOS/error, tear down pipeline and writer refs. */
static gboolean
amba_event_recorder_close_writer_unlocked (GstAmbaEventRecorder * self, const gchar * reason)
{
  GstAmbaEventRecorderPrivate *priv = amba_event_recorder_get_priv (self);
  TrackPadContext *ctx = NULL;
  GstFlowReturn flow_ret = GST_FLOW_OK;
  GstBus *bus = NULL;
  GstMessage *msg = NULL;
  gboolean ok = FALSE;
  GHashTableIter iter;
  gpointer value = NULL;

  if (!priv->writer_pipeline) {
    return FALSE;
  }

  g_hash_table_iter_init (&iter, priv->track_contexts);
  while (g_hash_table_iter_next (&iter, NULL, &value)) {
    ctx = (TrackPadContext *) value;
    if (!ctx || !ctx->writer_appsrc) {
      continue;
    }
    flow_ret = GST_FLOW_OK;
    g_signal_emit_by_name (ctx->writer_appsrc, "end-of-stream", &flow_ret);
    if (flow_ret != GST_FLOW_OK) {
      GST_ERROR_OBJECT (self, "writer EOS failed track=%s: %s",
          ctx->pad_name ? ctx->pad_name : "unknown",
          gst_flow_get_name (flow_ret));
      goto done;
    }
  }

  bus = gst_element_get_bus (priv->writer_pipeline);
  msg = gst_bus_timed_pop_filtered (bus, 5 * GST_SECOND,
      (GstMessageType) (GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
  if (!msg) {
    GST_ERROR_OBJECT (self, "writer finalize timeout");
    goto done;
  }
  if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR) {
    GError *err = NULL;
    gchar *dbg = NULL;
    gst_message_parse_error (msg, &err, &dbg);
    GST_ERROR_OBJECT (self, "writer error: %s (%s)",
        err ? err->message : "unknown", dbg ? dbg : "no-debug");
    if (err) {
      g_error_free (err);
    }
    g_free (dbg);
    goto done;
  }

  ok = TRUE;

done:
  if (msg) {
    gst_message_unref (msg);
  }
  if (bus) {
    gst_object_unref (bus);
  }

  gst_element_set_state (priv->writer_pipeline, GST_STATE_NULL);
  gst_object_unref (priv->writer_pipeline);
  priv->writer_pipeline = NULL;
  g_hash_table_iter_init (&iter, priv->track_contexts);
  while (g_hash_table_iter_next (&iter, NULL, &value)) {
    ctx = (TrackPadContext *) value;
    if (!ctx) {
      continue;
    }
    ctx->writer_appsrc = NULL;
    ctx->writer_parser = NULL;
    if (ctx->writer_mux_pad) {
      gst_object_unref (ctx->writer_mux_pad);
      ctx->writer_mux_pad = NULL;
    }
  }

  if (ok) {
    amba_event_recorder_post_commit_message (self, "window-committed",
        priv->window_id, priv->writer_location, priv->writer_written_count, reason);
  } else {
    amba_event_recorder_post_commit_message (self, "window-commit-failed",
        priv->window_id, priv->writer_location, priv->writer_written_count, reason);
  }
  GST_INFO_OBJECT (self,
      "writer-close window=%" G_GUINT64_FORMAT " reason=%s status=%s written=%u location=%s",
      priv->window_id, reason ? reason : "none", ok ? "ok" : "failed",
      priv->writer_written_count, priv->writer_location ? priv->writer_location : "none");
  priv->writer_written_count = 0;
  return ok;
}

/* If window has finite end and current_rt passed it, close window (primary-driven push first). */
static void
amba_event_recorder_maybe_close_window_unlocked (GstAmbaEventRecorder * self,
    GstClockTime current_rt, const gchar * reason)
{
  GstAmbaEventRecorderPrivate *priv = amba_event_recorder_get_priv (self);

  if (!priv->window_active || !GST_CLOCK_TIME_IS_VALID (current_rt)) {
    return;
  }
  if (!GST_CLOCK_TIME_IS_VALID (priv->window_end)) {
    return;
  }
  if (current_rt <= priv->window_end) {
    return;
  }
  amba_event_recorder_close_active_window_unlocked (self, current_rt, reason);
}

/* Drain writer to window_end, post window-closed, tear down writer, reset window fields, IDLE-prune primary. */
static void
amba_event_recorder_close_active_window_unlocked (GstAmbaEventRecorder * self,
    GstClockTime close_rt, const gchar * reason)
{
  GstAmbaEventRecorderPrivate *priv = amba_event_recorder_get_priv (self);
  TrackPadContext *ctx = amba_event_recorder_get_primary_track_unlocked (self);
  GstClockTime final_end = close_rt;

  if (!priv->window_active) {
    return;
  }
  if (!GST_CLOCK_TIME_IS_VALID (final_end)) {
    final_end = ctx ? ctx->last_running_time : GST_CLOCK_TIME_NONE;
  }
  if (!GST_CLOCK_TIME_IS_VALID (final_end)) {
    final_end = amba_event_recorder_get_running_time (self);
  }
  if (!GST_CLOCK_TIME_IS_VALID (final_end)) {
    final_end = priv->window_start;
  }

  if (!GST_CLOCK_TIME_IS_VALID (priv->window_end) || priv->window_end > final_end) {
    priv->window_end = final_end;
  }

  if (priv->writer_pipeline && ctx) {
    (void) amba_event_recorder_push_pending_unlocked (self,
        MIN (priv->window_end, ctx->last_running_time));
  }
  amba_event_recorder_post_window_message (self, "window-closed",
      priv->window_id, priv->window_start, priv->window_end);
  (void) amba_event_recorder_close_writer_unlocked (self, reason);
  priv->window_active = FALSE;
  priv->writer_start_rt = GST_CLOCK_TIME_NONE;
  priv->writer_start_serial = INVALID_BUFFER_SERIAL;
  priv->writer_last_written_rt = GST_CLOCK_TIME_NONE;
  priv->active_retain_pre_ms = 0;
  g_clear_pointer (&priv->writer_location, g_free);

  if (ctx) {
    amba_event_recorder_prune_idle_gop_unlocked (ctx);
    amba_event_recorder_prune_limits_unlocked (ctx, priv, final_end);
  }
}

/* ~100ms tick: push primary up to window_end, then maybe_close when clock running time passes window_end. */
static gboolean
amba_event_recorder_close_timer_cb (gpointer user_data)
{
  GstAmbaEventRecorder *self = GST_AMBA_EVENT_RECORDER (user_data);
  GstAmbaEventRecorderPrivate *priv = amba_event_recorder_get_priv (self);
  TrackPadContext *ctx = NULL;
  GstClockTime now_rt = amba_event_recorder_get_running_time (self);

  g_mutex_lock (&priv->lock);
  ctx = amba_event_recorder_get_primary_track_unlocked (self);
  if (priv->window_active && priv->writer_pipeline && ctx) {
    (void) amba_event_recorder_push_pending_unlocked (self,
        MIN (priv->window_end, ctx->last_running_time));
  }
  amba_event_recorder_maybe_close_window_unlocked (self, now_rt, "window-end");
  g_mutex_unlock (&priv->lock);

  return G_SOURCE_CONTINUE;
}

/* Normalize PTS, append to track cache, prune IDLE/ACTIVE; open writer and push on primary when window active. */
static GstFlowReturn
amba_event_recorder_process_sample (GstAmbaEventRecorder * self,
    TrackPadContext * ctx, GstSample * sample)
{
  GstAmbaEventRecorderPrivate *priv = amba_event_recorder_get_priv (self);
  TrackPadContext *primary_ctx = NULL;
  GstBuffer *buffer = NULL;
  GstCaps *caps = NULL;
  const GstSegment *segment = NULL;
  CachedBuffer *cb = NULL;
  GstClockTime ts = GST_CLOCK_TIME_NONE;
  GstClockTime buffer_rt = GST_CLOCK_TIME_NONE;
  GstClockTime clock_rt = GST_CLOCK_TIME_NONE;
  GstClockTime rt = GST_CLOCK_TIME_NONE;
  GstFlowReturn ret = GST_FLOW_OK;

  if (!sample) {
    return GST_FLOW_OK;
  }

  buffer = gst_sample_get_buffer (sample);
  caps = gst_sample_get_caps (sample);
  segment = gst_sample_get_segment (sample);
  if (!buffer) {
    goto done;
  }

  ts = GST_BUFFER_PTS_IS_VALID (buffer) ? GST_BUFFER_PTS (buffer) : GST_BUFFER_DTS (buffer);
  if (segment && GST_CLOCK_TIME_IS_VALID (ts)) {
    buffer_rt = gst_segment_to_running_time (segment, GST_FORMAT_TIME, ts);
  }
  clock_rt = amba_event_recorder_get_running_time (self);

  g_mutex_lock (&priv->lock);
  if (priv->fatal_error) {
    g_mutex_unlock (&priv->lock);
    ret = GST_FLOW_ERROR;
    goto done;
  }
  if (!ctx) {
    g_mutex_unlock (&priv->lock);
    ret = GST_FLOW_ERROR;
    goto done;
  }
  primary_ctx = amba_event_recorder_get_primary_track_unlocked (self);
  rt = amba_event_recorder_normalize_running_time_unlocked (ctx, buffer_rt, clock_rt);
  ctx->last_running_time = rt;

  if (caps) {
    if (!ctx->input_caps || !gst_caps_is_equal (ctx->input_caps, caps)) {
      if (ctx->input_caps) {
        gst_caps_unref (ctx->input_caps);
      }
      ctx->input_caps = gst_caps_ref (caps);
      GST_DEBUG_OBJECT (self, "caps-update track=%s caps=%" GST_PTR_FORMAT,
          ctx->pad_name, ctx->input_caps);
    }
  }

  cb = g_new0 (CachedBuffer, 1);
  cb->buffer = gst_buffer_copy (buffer);
  if (!cb->buffer) {
    cb->buffer = gst_buffer_ref (buffer);
  }
  cb->running_time = rt;
  cb->serial = ++priv->next_buffer_serial;
  cb->keyframe = !GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);
  cb->written = FALSE;
  /*
   * Normalize writer timestamps to running-time so each committed file has
   * monotonic timeline and qtmux never sees missing PTS on segment start.
   */
  GST_BUFFER_PTS (cb->buffer) = rt;
  GST_BUFFER_DTS (cb->buffer) = rt;
  g_queue_push_tail (&ctx->cached_buffers, cb);
  ctx->cached_count++;
  ctx->cached_bytes += gst_buffer_get_size (buffer);
  if (cb->keyframe || (ctx->cached_count % 30) == 0) {
    GST_DEBUG_OBJECT (self,
        "cache-append track=%s rt=%" GST_TIME_FORMAT " key=%d count=%u bytes=%" G_GUINT64_FORMAT
        " window-active=%d",
        ctx->pad_name ? ctx->pad_name : "unknown",
        GST_TIME_ARGS (rt), cb->keyframe, ctx->cached_count, ctx->cached_bytes,
        priv->window_active);
  }

  if (priv->window_active) {
    if (!priv->writer_pipeline) {
      (void) amba_event_recorder_open_writer_unlocked (self);
    }
    if (priv->writer_pipeline && primary_ctx && primary_ctx == ctx) {
      (void) amba_event_recorder_push_pending_unlocked (self, MIN (rt, priv->window_end));
    }
    if (primary_ctx && primary_ctx == ctx) {
      amba_event_recorder_maybe_close_window_unlocked (self, rt, "stream-progress");
    } else {
      /* Non-primary tracks are cached for future AV phase; still enforce limits. */
      amba_event_recorder_prune_limits_unlocked (ctx, priv, rt);
    }
  } else {
    amba_event_recorder_prune_idle_gop_unlocked (ctx);
    amba_event_recorder_prune_limits_unlocked (ctx, priv, rt);
  }

  g_mutex_unlock (&priv->lock);

done:
  gst_sample_unref (sample);
  return ret;
}

static GstFlowReturn
amba_event_recorder_on_new_preroll (GstElement * appsink, gpointer user_data)
{
  GstAmbaEventRecorder *self = GST_AMBA_EVENT_RECORDER (user_data);
  GstAmbaEventRecorderPrivate *priv = amba_event_recorder_get_priv (self);
  TrackPadContext *ctx = NULL;
  GstSample *sample = NULL;
  GstCaps *caps = NULL;

  /*
   * Always drain preroll from appsink.
   * If preroll is left pending, some pipelines (notably audio-only chains)
   * may never progress to new-sample callbacks.
   */
  g_signal_emit_by_name (appsink, "pull-preroll", &sample);
  if (!sample) {
    return GST_FLOW_OK;
  }

  caps = gst_sample_get_caps (sample);
  if (caps) {
    g_mutex_lock (&priv->lock);
    ctx = amba_event_recorder_lookup_track_by_appsink_unlocked (priv, appsink);
    if (ctx && (!ctx->input_caps || !gst_caps_is_equal (ctx->input_caps, caps))) {
      if (ctx->input_caps) {
        gst_caps_unref (ctx->input_caps);
      }
      ctx->input_caps = gst_caps_ref (caps);
      GST_DEBUG_OBJECT (self, "preroll-caps-update track=%s caps=%" GST_PTR_FORMAT,
          ctx->pad_name ? ctx->pad_name : "unknown", ctx->input_caps);
    }
    g_mutex_unlock (&priv->lock);
  }
  gst_sample_unref (sample);
  return GST_FLOW_OK;
}

/* Resolve track from appsink, pull-sample, enqueue via process_sample. */
static GstFlowReturn
amba_event_recorder_on_new_sample (GstElement * appsink, gpointer user_data)
{
  GstAmbaEventRecorder *self = GST_AMBA_EVENT_RECORDER (user_data);
  GstAmbaEventRecorderPrivate *priv = amba_event_recorder_get_priv (self);
  TrackPadContext *ctx = NULL;
  GstSample *sample = NULL;

  g_mutex_lock (&priv->lock);
  ctx = amba_event_recorder_lookup_track_by_appsink_unlocked (priv, appsink);
  g_mutex_unlock (&priv->lock);
  if (!ctx) {
    GST_WARNING_OBJECT (self, "sample from unknown appsink");
    return GST_FLOW_OK;
  }
  g_signal_emit_by_name (appsink, "pull-sample", &sample);
  return amba_event_recorder_process_sample (self, ctx, sample);
}

/* Mark upstream EOS and force-close any active window (infinite post-roll included). */
static void
amba_event_recorder_on_eos (GstElement * appsink, gpointer user_data)
{
  GstAmbaEventRecorder *self = GST_AMBA_EVENT_RECORDER (user_data);
  GstAmbaEventRecorderPrivate *priv = amba_event_recorder_get_priv (self);
  TrackPadContext *ctx = NULL;

  g_mutex_lock (&priv->lock);
  ctx = amba_event_recorder_lookup_track_by_appsink_unlocked (priv, appsink);
  priv->upstream_eos = TRUE;
  GST_INFO_OBJECT (self, "upstream EOS track=%s",
      ctx && ctx->pad_name ? ctx->pad_name : "unknown");

  if (priv->window_active) {
    /* Force close active window at EOS, including infinite-post mode. */
    amba_event_recorder_close_active_window_unlocked (self,
        ctx ? ctx->last_running_time : GST_CLOCK_TIME_NONE, "upstream-eos");
  }
  g_mutex_unlock (&priv->lock);
}

/* TRUE if element state is PAUSED or higher (some GObject props locked while running). */
static gboolean
amba_event_recorder_is_runtime_locked (GstAmbaEventRecorder * self)
{
  GstState cur = GST_STATE_NULL;
  GstState pending = GST_STATE_VOID_PENDING;
  (void) gst_element_get_state (GST_ELEMENT (self), &cur, &pending, 0);
  DUNUSED (pending);
  return cur >= GST_STATE_PAUSED;
}

/* Compute [window_start, window_end] from event time and pre/post ms; open or extend window; may open writer. */
static void
amba_event_recorder_trigger (GstAmbaEventRecorder * self, guint64 event_ts_ns,
    gint pre_ms, gint post_ms, const gchar * event_id, gpointer user_data)
{
  GstAmbaEventRecorderPrivate *priv = amba_event_recorder_get_priv (self);
  TrackPadContext *ctx = NULL;
  GstClockTime event_ts = (GstClockTime) event_ts_ns;
  GstClockTime pre_ns = 0;
  GstClockTime post_ns = 0;
  GstClockTime start = 0;
  GstClockTime end = 0;
  gboolean unlimited_post = FALSE;
  gboolean open_new = FALSE;
  gint resolved_pre_ms = 0;
  gint resolved_post_ms = 0;
  CachedBuffer *head_cb = NULL;

  DUNUSED (user_data);

  g_mutex_lock (&priv->lock);
  ctx = amba_event_recorder_get_primary_track_unlocked (self);
  if (priv->fatal_error) {
    g_mutex_unlock (&priv->lock);
    GST_WARNING_OBJECT (self, "ignore trigger after fatal writer error");
    return;
  }
  if (!ctx) {
    GST_WARNING_OBJECT (self,
        "primary-pad '%s' not available, trigger uses fallback timing only",
        priv->primary_pad_name ? priv->primary_pad_name : DEFAULT_PRIMARY_PAD_NAME);
  }
  if (priv->upstream_eos) {
    g_mutex_unlock (&priv->lock);
    GST_WARNING_OBJECT (self, "ignore trigger after EOS");
    return;
  }

  resolved_pre_ms = (pre_ms != 0) ? pre_ms : priv->pre_record_ms;
  resolved_post_ms = (post_ms != 0) ? post_ms : priv->post_record_ms;
  unlimited_post = (resolved_post_ms < 0);
  if (resolved_pre_ms > 0 && priv->max_cache_ms > 0 &&
      (guint) resolved_pre_ms > priv->max_cache_ms) {
    GST_WARNING_OBJECT (self,
        "pre-record-ms=%d exceeds max-cache-ms=%u; actual pre-roll is limited by cache",
        resolved_pre_ms, priv->max_cache_ms);
  }

  if (resolved_pre_ms > 0) {
    pre_ns = (GstClockTime) resolved_pre_ms * GST_MSECOND;
  }
  if (!unlimited_post && resolved_post_ms > 0) {
    post_ns = (GstClockTime) resolved_post_ms * GST_MSECOND;
  }

  if (!GST_CLOCK_TIME_IS_VALID (event_ts)) {
    event_ts = amba_event_recorder_get_running_time (self);
    if (!GST_CLOCK_TIME_IS_VALID (event_ts)) {
      event_ts = ctx ? ctx->last_running_time : GST_CLOCK_TIME_NONE;
    }
  }
  if (!GST_CLOCK_TIME_IS_VALID (event_ts)) {
    event_ts = 0;
  }

  start = (event_ts > pre_ns) ? (event_ts - pre_ns) : 0;
  end = unlimited_post ? GST_CLOCK_TIME_NONE : (event_ts + post_ns);
  GST_INFO_OBJECT (self,
      "trigger-recv event-id=%s event-ts=%" GST_TIME_FORMAT
      " pre-ms=%d post-ms=%d active=%d",
      event_id ? event_id : "(null)", GST_TIME_ARGS (event_ts),
      resolved_pre_ms, resolved_post_ms, priv->window_active);

  if (!priv->window_active) {
    if (resolved_pre_ms < 0 && ctx && !g_queue_is_empty (&ctx->cached_buffers)) {
      head_cb = (CachedBuffer *) g_queue_peek_head (&ctx->cached_buffers);
      start = head_cb ? head_cb->running_time : 0;
    } else if (resolved_pre_ms > 0) {
      if (!ctx || g_queue_is_empty (&ctx->cached_buffers)) {
        GST_WARNING_OBJECT (self,
            "insufficient cache for pre-roll=%dms at trigger time; cache currently empty",
            resolved_pre_ms);
      } else {
        head_cb = (CachedBuffer *) g_queue_peek_head (&ctx->cached_buffers);
        if (head_cb && head_cb->running_time > start) {
          GST_WARNING_OBJECT (self,
              "insufficient cache for pre-roll=%dms; requested-start=%" GST_TIME_FORMAT
              " earliest-cached=%" GST_TIME_FORMAT,
              resolved_pre_ms, GST_TIME_ARGS (start),
              GST_TIME_ARGS (head_cb->running_time));
        }
      }
    }
    open_new = TRUE;
    priv->window_active = TRUE;
    priv->active_retain_pre_ms = resolved_pre_ms;
    priv->window_id++;
    priv->window_start = start;
    priv->window_end = end;
    amba_event_recorder_post_window_message (self, "window-opened",
        priv->window_id, priv->window_start, priv->window_end);
    GST_INFO_OBJECT (self,
        "window-opened window=%" G_GUINT64_FORMAT " event-id=%s"
        " event-ts=%" GST_TIME_FORMAT " start=%" GST_TIME_FORMAT
        " end=%" GST_TIME_FORMAT,
        priv->window_id, event_id ? event_id : "(null)", GST_TIME_ARGS (event_ts),
        GST_TIME_ARGS (priv->window_start), GST_TIME_ARGS (priv->window_end));
  } else {
    if (resolved_pre_ms < 0 || priv->active_retain_pre_ms < 0) {
      priv->active_retain_pre_ms = -1;
    } else {
      priv->active_retain_pre_ms = MAX (priv->active_retain_pre_ms, resolved_pre_ms);
    }
    /* During ACTIVE writing we only extend end, never move start backward. */
    if (unlimited_post || !GST_CLOCK_TIME_IS_VALID (priv->window_end)) {
      priv->window_end = GST_CLOCK_TIME_NONE;
    } else {
      priv->window_end = MAX (priv->window_end, end);
    }
    amba_event_recorder_post_window_message (self, "window-extended",
        priv->window_id, priv->window_start, priv->window_end);
    GST_INFO_OBJECT (self,
        "window-extended window=%" G_GUINT64_FORMAT " event-id=%s"
        " start=%" GST_TIME_FORMAT " end=%" GST_TIME_FORMAT,
        priv->window_id, event_id ? event_id : "(null)",
        GST_TIME_ARGS (priv->window_start), GST_TIME_ARGS (priv->window_end));
  }

  if (open_new) {
    if (amba_event_recorder_open_writer_unlocked (self)) {
      (void) amba_event_recorder_push_pending_unlocked (self,
          MIN (priv->window_end, ctx ? ctx->last_running_time : GST_CLOCK_TIME_NONE));
    }
  }

  g_mutex_unlock (&priv->lock);
}

/* GObject set: recorder options under lock; warn when changing pre/post in running state. */
static void
amba_event_recorder_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAmbaEventRecorder *self = GST_AMBA_EVENT_RECORDER (object);
  GstAmbaEventRecorderPrivate *priv = amba_event_recorder_get_priv (self);

  g_mutex_lock (&priv->lock);
  switch (prop_id) {
    case PROP_LOCATION:
      g_free (priv->location);
      priv->location = g_value_dup_string (value);
      if (!priv->location) {
        priv->location = g_strdup (DEFAULT_LOCATION);
      }
      break;
    case PROP_MUX_FACTORY:
      g_free (priv->mux_factory);
      priv->mux_factory = g_value_dup_string (value);
      if (!priv->mux_factory || !priv->mux_factory[0]) {
        g_clear_pointer (&priv->mux_factory, g_free);
        priv->mux_factory = g_strdup (DEFAULT_MUX_FACTORY);
      }
      break;
    case PROP_MUX_PROPERTIES:
      g_free (priv->mux_properties);
      priv->mux_properties = g_value_dup_string (value);
      break;
    case PROP_PRE_RECORD_MS:
      if (amba_event_recorder_is_runtime_locked (self)) {
        GST_WARNING_OBJECT (self, "pre-record-ms update ignored in PLAYING/PAUSED");
      } else {
        priv->pre_record_ms = g_value_get_int (value);
        if (priv->pre_record_ms > 0 && priv->max_cache_ms > 0 &&
            (guint) priv->pre_record_ms > priv->max_cache_ms) {
          GST_WARNING_OBJECT (self,
              "pre-record-ms=%d exceeds max-cache-ms=%u; actual pre-roll is limited by cache",
              priv->pre_record_ms, priv->max_cache_ms);
        }
      }
      break;
    case PROP_POST_RECORD_MS:
      if (amba_event_recorder_is_runtime_locked (self)) {
        GST_WARNING_OBJECT (self, "post-record-ms update ignored in PLAYING/PAUSED");
      } else {
        priv->post_record_ms = g_value_get_int (value);
      }
      break;
    case PROP_MAX_CACHE_MS:
      priv->max_cache_ms = g_value_get_uint (value);
      if (priv->pre_record_ms > 0 && priv->max_cache_ms > 0 &&
          (guint) priv->pre_record_ms > priv->max_cache_ms) {
        GST_WARNING_OBJECT (self,
            "pre-record-ms=%d exceeds max-cache-ms=%u; actual pre-roll is limited by cache",
            priv->pre_record_ms, priv->max_cache_ms);
      }
      break;
    case PROP_MAX_BUFFERS:
      priv->max_buffers = g_value_get_uint (value);
      break;
    case PROP_MAX_BYTES:
      priv->max_bytes = g_value_get_uint64 (value);
      break;
    case PROP_ALIGN_START_KEYFRAME:
      priv->align_start_keyframe = g_value_get_boolean (value);
      break;
    case PROP_PRIMARY_PAD:
    {
      const gchar *requested = g_value_get_string (value);
      if (!requested || !requested[0]) {
        requested = DEFAULT_PRIMARY_PAD_NAME;
      }
      g_clear_pointer (&priv->primary_pad_name, g_free);
      priv->primary_pad_name = g_strdup (requested);
      GST_INFO_OBJECT (self, "primary-pad set to %s", priv->primary_pad_name);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  g_mutex_unlock (&priv->lock);
}

/* GObject get: copy current string/int/bool cache limits under lock. */
static void
amba_event_recorder_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAmbaEventRecorder *self = GST_AMBA_EVENT_RECORDER (object);
  GstAmbaEventRecorderPrivate *priv = amba_event_recorder_get_priv (self);

  g_mutex_lock (&priv->lock);
  switch (prop_id) {
    case PROP_LOCATION:
      g_value_set_string (value, priv->location);
      break;
    case PROP_MUX_FACTORY:
      g_value_set_string (value, priv->mux_factory);
      break;
    case PROP_MUX_PROPERTIES:
      g_value_set_string (value, priv->mux_properties);
      break;
    case PROP_PRE_RECORD_MS:
      g_value_set_int (value, priv->pre_record_ms);
      break;
    case PROP_POST_RECORD_MS:
      g_value_set_int (value, priv->post_record_ms);
      break;
    case PROP_MAX_CACHE_MS:
      g_value_set_uint (value, priv->max_cache_ms);
      break;
    case PROP_MAX_BUFFERS:
      g_value_set_uint (value, priv->max_buffers);
      break;
    case PROP_MAX_BYTES:
      g_value_set_uint64 (value, priv->max_bytes);
      break;
    case PROP_ALIGN_START_KEYFRAME:
      g_value_set_boolean (value, priv->align_start_keyframe);
      break;
    case PROP_PRIMARY_PAD:
      g_value_set_string (value, priv->primary_pad_name);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  g_mutex_unlock (&priv->lock);
}

/* Allocate sink_video_%u / sink_audio_%u name, add_track if new, return ref to ghost pad. */
static GstPad *
amba_event_recorder_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * req_name, const GstCaps * caps)
{
  GstAmbaEventRecorder *self = GST_AMBA_EVENT_RECORDER (element);
  GstAmbaEventRecorderPrivate *priv = amba_event_recorder_get_priv (self);
  const gchar *templ_name = NULL;
  gboolean is_video = FALSE;
  gboolean is_audio = FALSE;
  gchar *chosen_name = NULL;
  TrackPadContext *ctx = NULL;
  GstPad *ret_pad = NULL;

  DUNUSED (caps);
  if (!templ) {
    return NULL;
  }
  templ_name = GST_PAD_TEMPLATE_NAME_TEMPLATE (templ);
  is_video = templ_name && g_str_has_prefix (templ_name, "sink_video_");
  is_audio = templ_name && g_str_has_prefix (templ_name, "sink_audio_");
  if (!is_video && !is_audio) {
    GST_WARNING_OBJECT (self, "unsupported request pad template %s",
        templ_name ? templ_name : "unknown");
    return NULL;
  }

  g_mutex_lock (&priv->lock);
  if (req_name && req_name[0]) {
    if ((is_video && !amba_event_recorder_is_video_pad_name (req_name)) ||
        (is_audio && !amba_event_recorder_is_audio_pad_name (req_name))) {
      GST_WARNING_OBJECT (self,
          "invalid request pad name '%s' for template %s",
          req_name, templ_name);
      goto done;
    }
    chosen_name = g_strdup (req_name);
  } else {
    chosen_name = amba_event_recorder_pick_request_pad_name_unlocked (priv, is_video);
  }
  if (!chosen_name) {
    goto done;
  }

  ctx = amba_event_recorder_lookup_track_unlocked (priv, chosen_name);
  if (!ctx) {
    ctx = amba_event_recorder_add_track_unlocked (self, chosen_name, is_video);
  }
  if (!ctx || !ctx->sink_ghost_pad) {
    g_free (chosen_name);
    chosen_name = NULL;
    goto done;
  }
  ret_pad = gst_object_ref (ctx->sink_ghost_pad);
  GST_DEBUG_OBJECT (self, "pad-request name=%s template=%s",
      chosen_name, templ_name ? templ_name : "unknown");
  g_free (chosen_name);

done:
  g_mutex_unlock (&priv->lock);
  return ret_pad;
}

/* Remove non-primary track: refuse if primary or active window; NULL-state queue/appsink and remove from bin. */
static void
amba_event_recorder_release_pad (GstElement * element, GstPad * pad)
{
  GstAmbaEventRecorder *self = GST_AMBA_EVENT_RECORDER (element);
  GstAmbaEventRecorderPrivate *priv = amba_event_recorder_get_priv (self);
  const gchar *pad_name = NULL;
  gchar *pad_name_copy = NULL;
  TrackPadContext *ctx = NULL;

  if (!pad) {
    return;
  }
  pad_name = GST_PAD_NAME (pad);
  pad_name_copy = g_strdup (pad_name ? pad_name : "");

  g_mutex_lock (&priv->lock);
  if (priv->disposing || !priv->track_contexts) {
    GST_DEBUG_OBJECT (self, "pad-release during dispose ignored pad=%s",
        pad_name_copy ? pad_name_copy : "none");
    goto done;
  }
  ctx = amba_event_recorder_lookup_track_unlocked (priv, pad_name_copy);
  if (!ctx) {
    GST_DEBUG_OBJECT (self, "pad-release unknown pad=%s",
        pad_name_copy ? pad_name_copy : "none");
    goto done;
  }
  if (g_strcmp0 (pad_name_copy, priv->primary_pad_name) == 0) {
    GST_WARNING_OBJECT (self,
        "refuse release primary-pad=%s; switch primary-pad first",
        pad_name_copy);
    goto done;
  }
  if (priv->window_active) {
    GST_WARNING_OBJECT (self, "refuse release while window is active");
    goto done;
  }
  if (ctx->queue) {
    gst_element_set_state (ctx->queue, GST_STATE_NULL);
  }
  if (ctx->appsink) {
    gst_element_set_state (ctx->appsink, GST_STATE_NULL);
  }
  (void) gst_element_remove_pad (element, pad);
  if (ctx->queue) {
    (void) gst_bin_remove (GST_BIN (self), ctx->queue);
  }
  if (ctx->appsink) {
    (void) gst_bin_remove (GST_BIN (self), ctx->appsink);
  }
  g_hash_table_remove (priv->track_contexts, pad_name_copy);
  GST_INFO_OBJECT (self, "pad-released name=%s", pad_name_copy ? pad_name_copy : "none");

done:
  g_mutex_unlock (&priv->lock);
  g_free (pad_name_copy);
}

/* Stop close timer, close active window if any, clear string fields and track table before chain-up. */
static void
amba_event_recorder_dispose (GObject * object)
{
  GstAmbaEventRecorder *self = GST_AMBA_EVENT_RECORDER (object);
  GstAmbaEventRecorderPrivate *priv = amba_event_recorder_get_priv (self);

  if (priv->close_timer_id) {
    g_source_remove (priv->close_timer_id);
    priv->close_timer_id = 0;
  }

  g_mutex_lock (&priv->lock);
  priv->disposing = TRUE;
  if (priv->window_active && priv->writer_pipeline) {
    amba_event_recorder_close_active_window_unlocked (self, GST_CLOCK_TIME_NONE, "dispose");
  }
  g_clear_pointer (&priv->location, g_free);
  g_clear_pointer (&priv->mux_factory, g_free);
  g_clear_pointer (&priv->mux_properties, g_free);
  g_clear_pointer (&priv->primary_pad_name, g_free);
  g_clear_pointer (&priv->writer_location, g_free);
  g_mutex_unlock (&priv->lock);

  g_clear_pointer (&priv->track_contexts, g_hash_table_unref);

  G_OBJECT_CLASS (gst_amba_event_recorder_parent_class)->dispose (object);
}

/* Clear instance mutex after GObject dispose chain. */
static void
amba_event_recorder_finalize (GObject * object)
{
  GstAmbaEventRecorder *self = GST_AMBA_EVENT_RECORDER (object);
  GstAmbaEventRecorderPrivate *priv = amba_event_recorder_get_priv (self);
  g_mutex_clear (&priv->lock);
  G_OBJECT_CLASS (gst_amba_event_recorder_parent_class)->finalize (object);
}

/* Install properties, trigger signal, pad templates, metadata, and debug category. */
static void
gst_amba_event_recorder_class_init (GstAmbaEventRecorderClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = amba_event_recorder_set_property;
  gobject_class->get_property = amba_event_recorder_get_property;
  gobject_class->dispose = amba_event_recorder_dispose;
  gobject_class->finalize = amba_event_recorder_finalize;
  gstelement_class->request_new_pad = amba_event_recorder_request_new_pad;
  gstelement_class->release_pad = amba_event_recorder_release_pad;

  properties[PROP_LOCATION] = g_param_spec_string ("location", "Location",
      "Output file template used by active window writer",
      DEFAULT_LOCATION, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  properties[PROP_MUX_FACTORY] = g_param_spec_string ("mux-factory", "Mux Factory",
      "Muxer element factory name used for writer (e.g. mp4mux, matroskamux)",
      DEFAULT_MUX_FACTORY, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  properties[PROP_MUX_PROPERTIES] = g_param_spec_string ("mux-properties", "Mux Properties",
      "Optional mux properties, comma separated key=value pairs",
      NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  properties[PROP_PRE_RECORD_MS] = g_param_spec_int ("pre-record-ms",
      "Pre Record (ms)", "Window duration before trigger; -1 means use maximum available cache",
      -1, 120000, DEFAULT_PRE_RECORD_MS,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  properties[PROP_POST_RECORD_MS] = g_param_spec_int ("post-record-ms",
      "Post Record (ms)", "Window duration after trigger; -1 means keep recording until EOS",
      -1, 120000, DEFAULT_POST_RECORD_MS,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  properties[PROP_MAX_CACHE_MS] = g_param_spec_uint ("max-cache-ms",
      "Max Cache (ms)", "Ring buffer duration limit",
      100, 600000, DEFAULT_MAX_CACHE_MS,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  properties[PROP_MAX_BUFFERS] = g_param_spec_uint ("max-buffers",
      "Max Buffers", "Ring buffer count limit, 0 means unlimited",
      0, G_MAXUINT, DEFAULT_MAX_BUFFERS,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  properties[PROP_MAX_BYTES] = g_param_spec_uint64 ("max-bytes",
      "Max Bytes", "Ring buffer bytes limit, 0 means unlimited",
      0, G_MAXUINT64, DEFAULT_MAX_BYTES,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  properties[PROP_ALIGN_START_KEYFRAME] = g_param_spec_boolean (
      "align-start-keyframe", "Align Start Keyframe",
      "Start from latest keyframe before window start",
      DEFAULT_ALIGN_START_KEYFRAME, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  properties[PROP_PRIMARY_PAD] = g_param_spec_string ("primary-pad",
      "Primary Pad", "Primary track pad name for timing and writer sync anchor",
      DEFAULT_PRIMARY_PAD_NAME, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (gobject_class, N_PROPERTIES, properties);

  /**
   * GstAmbaEventRecorder::trigger:
   * @self: the recorder instance.
   * @event_ts_ns: Event time as #GstClockTime (nanoseconds). Use %GST_CLOCK_TIME_NONE to use the
   *     current running time on the primary pad (or cache-based fallback).
   * @pre_ms: Pre-roll duration in milliseconds. `0` selects the `pre-record-ms` property; negative
   *     values request maximum-cache pre-roll where implemented.
   * @post_ms: Post-roll in milliseconds. `0` selects `post-record-ms`; `-1` means unlimited post-roll
   *     until upstream EOS.
   * @event_id: Optional NUL-terminated event id for logging (may be %NULL).
   *
   * Action signal: the application emits this (e.g. g_signal_emit_by_name) to open a new recording
   * window or extend the active window. The element does not generate triggers by itself. See the
   * element SECTION for behaviour. Listed under Signals in gst-inspect (GObject signature).
   */
  signals[SIGNAL_TRIGGER] =
      g_signal_new ("trigger", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      0, NULL, NULL, NULL, G_TYPE_NONE, 4,
      G_TYPE_UINT64, G_TYPE_INT, G_TYPE_INT, G_TYPE_STRING);

  gst_element_class_set_static_metadata (gstelement_class,
      "Ambarella Event Recorder",
      "Generic/Bin/Recorder",
      "Request-pad bin for multi-track event recording: per-pad ring buffers, one muxed output file "
      "per window using mux-factory and location. The application must invoke the \"trigger\" action "
      "signal (event_ts_ns, pre_ms, post_ms, event_id) to start or extend a clip; primary-pad sets "
      "timing and window end. Not usable from gst-launch without external code to emit triggers.",
      "Yang Yu <yyua@ambarella.com>");
  gst_element_class_add_static_pad_template (gstelement_class, &sink_video_request_template);
  gst_element_class_add_static_pad_template (gstelement_class, &sink_audio_request_template);

  GST_DEBUG_CATEGORY_INIT (amba_event_recorder_debug, "amba_event_recorder", 0,
      "Ambarella Event Recorder");
}

/* Instance init: defaults, track hash, connect trigger, start window-end poll timer. */
static void
gst_amba_event_recorder_init (GstAmbaEventRecorder * self)
{
  GstAmbaEventRecorderPrivate *priv = amba_event_recorder_get_priv (self);

  g_mutex_init (&priv->lock);
  priv->track_contexts = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, (GDestroyNotify) track_pad_context_free);

  priv->location = g_strdup (DEFAULT_LOCATION);
  priv->mux_factory = g_strdup (DEFAULT_MUX_FACTORY);
  priv->primary_pad_name = g_strdup (DEFAULT_PRIMARY_PAD_NAME);
  priv->pre_record_ms = DEFAULT_PRE_RECORD_MS;
  priv->post_record_ms = DEFAULT_POST_RECORD_MS;
  priv->max_cache_ms = DEFAULT_MAX_CACHE_MS;
  priv->max_buffers = DEFAULT_MAX_BUFFERS;
  priv->max_bytes = DEFAULT_MAX_BYTES;
  priv->align_start_keyframe = DEFAULT_ALIGN_START_KEYFRAME;
  priv->fatal_error = FALSE;
  priv->writer_start_rt = GST_CLOCK_TIME_NONE;
  priv->writer_start_serial = INVALID_BUFFER_SERIAL;
  priv->writer_last_written_rt = GST_CLOCK_TIME_NONE;
  priv->writer_written_count = 0;
  priv->active_retain_pre_ms = 0;
  priv->next_buffer_serial = 0;

  /* Ingest tracks: queue+appsink per pad, created only via request_new_pad. */

  g_signal_connect (self, "trigger", G_CALLBACK (amba_event_recorder_trigger), NULL);
  priv->close_timer_id = g_timeout_add (100, amba_event_recorder_close_timer_cb, self);
}
