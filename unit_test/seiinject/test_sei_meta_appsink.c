/*
 * test_sei_meta_appsink.c
 *
 * History:
 *    04/10/2026 - [Yang Yu] created file
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

#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <gst/gst.h>
#include <glib-unix.h>
#include "gstambaseimeta.h"

/**
 * SECTION:unit_test-test-sei-meta-appsink
 * @title: test-sei-meta-appsink
 *
 * Reference application for reading `GstAmbaSeiMeta` from `appsink` output.
 * The input prefix is configurable (`--pipeline`, up to parser), while the
 * downstream chain is fixed as:
 * `amba_seiinject -> amba_sei_decoder -> appsink(name=sink)`.
 *
 * Optional dump mode writes raw frame buffers and an aligned `sei_meta.csv`
 * index. Dump stops automatically when `--dump-max-frames` is reached.
 *
 * ## Examples
 * |[
 * test-sei-meta-appsink
 * test-sei-meta-appsink --pipeline "amba_venccap2 stream-id=0 ! queue ! h265parse" --decoder-factory avdec_h265
 * test-sei-meta-appsink --dump-dir /tmp/sei_dump --dump-max-frames 300
 * ]|
 */

#define DUNUSED(x) ((void) (x))
#define DEFAULT_STARTUP_TIMEOUT_S 5U
#define MAX_DUMP_FRAMES 300U

typedef struct
{
  /* Main-loop handle used by bus/signals/timeouts for coordinated stop. */
  GMainLoop *loop;
  /* Runtime counters for summary and stop conditions. */
  gboolean got_sample;
  gboolean got_meta;
  guint64 sample_count;
  guint64 meta_count;
  /* Source IDs so cleanup can safely remove active watchers/timers. */
  guint timeout_id;
  guint run_seconds_id;
  guint sigint_id;
  /* Dump controls and outputs (frame dump + aligned CSV index). */
  guint64 dump_count;
  guint64 dump_max_frames;
  gchar *dump_dir;
  FILE *meta_fp;
} MetaAppCtx;

static gboolean
bus_message_cb (GstBus *bus, GstMessage *msg, gpointer user_data)
{
  MetaAppCtx *ctx = (MetaAppCtx *) user_data;
  GMainLoop *loop = ctx->loop;
  DUNUSED (bus);

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ERROR:
    {
      GError *err = NULL;
      gchar *dbg = NULL;
      gst_message_parse_error (msg, &err, &dbg);
      g_printerr ("ERROR: %s\n", err ? err->message : "unknown");
      if (dbg)
        g_printerr ("Debug: %s\n", dbg);
      g_clear_error (&err);
      g_free (dbg);
      g_main_loop_quit (loop);
      break;
    }
    case GST_MESSAGE_WARNING:
    {
      GError *err = NULL;
      gchar *dbg = NULL;
      gst_message_parse_warning (msg, &err, &dbg);
      g_printerr ("WARNING: %s\n", err ? err->message : "unknown");
      g_clear_error (&err);
      g_free (dbg);
      break;
    }
    case GST_MESSAGE_EOS:
      g_main_loop_quit (loop);
      break;
    default:
      break;
  }
  return TRUE;
}

static GstAmbaSeiMeta *
find_amba_sei_meta (GstBuffer *buffer)
{
  gpointer state = NULL;
  GstMeta *meta = NULL;

  /* Look up by API type name so this sample tool does not require extra helper API. */
  while ((meta = gst_buffer_iterate_meta (buffer, &state)) != NULL) {
    const gchar *api_name = NULL;
    if (!meta->info)
      continue;
    api_name = g_type_name (meta->info->api);
    if (api_name && g_strcmp0 (api_name, "GstAmbaSeiMetaAPI") == 0)
      return (GstAmbaSeiMeta *) meta;
  }
  return NULL;
}

static void
close_dump_files (MetaAppCtx *ctx)
{
  if (ctx->meta_fp) {
    fclose (ctx->meta_fp);
    ctx->meta_fp = NULL;
  }
}

static gboolean
open_dump_files (MetaAppCtx *ctx)
{
  gchar *meta_path = NULL;

  if (!ctx->dump_dir)
    return TRUE;
  if (g_mkdir_with_parents (ctx->dump_dir, 0755) != 0) {
    g_printerr ("Failed to create dump dir: %s\n", ctx->dump_dir);
    return FALSE;
  }

  meta_path = g_build_filename (ctx->dump_dir, "sei_meta.csv", NULL);
  if (!meta_path)
    return FALSE;

  ctx->meta_fp = fopen (meta_path, "w");
  if (!ctx->meta_fp) {
    g_printerr ("Failed to open meta index file: %s\n", meta_path);
    g_free (meta_path);
    return FALSE;
  }
  /* One CSV row per dumped frame; row index matches frame file numbering. */
  fprintf (ctx->meta_fp,
      "frame_idx,pts_ns,dump_file,has_meta,present_mask,timestamp_ns,gps_valid,gps_lat_e7,gps_lon_e7,gps_alt_cm,payload_version,payload_flags\n");
  fflush (ctx->meta_fp);
  g_free (meta_path);
  return TRUE;
}

static void
dump_frame_and_meta (MetaAppCtx *ctx, GstBuffer *buffer, const GstAmbaSeiMeta *meta)
{
  GstMapInfo map;
  gchar *frame_name = NULL;
  gchar *frame_path = NULL;
  gboolean mapped = FALSE;
  guint64 pts_ns = GST_CLOCK_TIME_IS_VALID (GST_BUFFER_PTS (buffer)) ?
      (guint64) GST_BUFFER_PTS (buffer) : G_MAXUINT64;

  /* Dump is optional: if not enabled, keep runtime overhead minimal. */
  if (!ctx->dump_dir || !ctx->meta_fp)
    return;
  if (ctx->dump_max_frames > 0 && ctx->dump_count >= ctx->dump_max_frames)
    return;
  if (!gst_buffer_map (buffer, &map, GST_MAP_READ))
    return;
  mapped = TRUE;

  /* Store raw decoded buffer payload exactly as produced by appsink sample buffer. */
  frame_name = g_strdup_printf ("frame_%06" G_GUINT64_FORMAT "_pts_%" G_GUINT64_FORMAT ".yuv",
      ctx->dump_count, pts_ns);
  frame_path = g_build_filename (ctx->dump_dir, frame_name, NULL);
  if (frame_path) {
    FILE *fp = fopen (frame_path, "wb");
    if (fp) {
      (void) fwrite (map.data, 1, map.size, fp);
      fclose (fp);
    } else {
      g_printerr ("Failed to open frame dump file: %s\n", frame_path);
    }
  }

  /* Keep CSV and frame naming on the same dump_count for easy post-analysis join. */
  fprintf (ctx->meta_fp,
      "%" G_GUINT64_FORMAT ",%" G_GUINT64_FORMAT ",%s,%d,0x%llx,%llu,%d,%d,%d,%d,%u,0x%04x\n",
      ctx->dump_count,
      pts_ns,
      frame_name ? frame_name : "",
      meta ? 1 : 0,
      (unsigned long long) (meta ? meta->present_mask : 0ULL),
      (unsigned long long) (meta ? meta->timestamp_ns : 0ULL),
      meta ? meta->gps_valid : 0,
      meta ? meta->gps_lat_e7 : 0,
      meta ? meta->gps_lon_e7 : 0,
      meta ? meta->gps_alt_cm : 0,
      meta ? meta->payload_version : 0U,
      meta ? meta->payload_flags : 0U);
  fflush (ctx->meta_fp);
  ctx->dump_count++;

  if (mapped)
    gst_buffer_unmap (buffer, &map);
  g_free (frame_path);
  g_free (frame_name);
}

static GstFlowReturn
meta_appsink_new_sample_cb (GstElement *appsink, gpointer user_data)
{
  MetaAppCtx *ctx = (MetaAppCtx *) user_data;
  GstSample *sample = NULL;
  GstBuffer *buffer = NULL;
  GstAmbaSeiMeta *meta = NULL;

  /* appsink is configured with emit-signals=true; pull one sample per callback. */
  g_signal_emit_by_name (appsink, "pull-sample", &sample);
  if (!sample)
    return GST_FLOW_ERROR;

  ctx->sample_count++;
  ctx->got_sample = TRUE;
  /* First sample means startup succeeded; startup watchdog can be cancelled. */
  if (ctx->timeout_id) {
    g_source_remove (ctx->timeout_id);
    ctx->timeout_id = 0;
  }
  buffer = gst_sample_get_buffer (sample);
  if (!buffer) {
    gst_sample_unref (sample);
    return GST_FLOW_OK;
  }

  meta = find_amba_sei_meta (buffer);
  if (meta) {
    ctx->got_meta = TRUE;
    ctx->meta_count++;
    g_print ("SEI meta: mask=0x%llx ts_ns=%llu gps(valid=%d lat=%d lon=%d alt=%d) "
             "payload(ver=%u flags=0x%04x)\n",
        (unsigned long long) meta->present_mask,
        (unsigned long long) meta->timestamp_ns,
        meta->gps_valid, meta->gps_lat_e7, meta->gps_lon_e7, meta->gps_alt_cm,
        meta->payload_version, meta->payload_flags);
  } else {
    g_print ("SEI meta: not found on this sample\n");
  }

  /* Optional: dump frame payload and aligned SEI index row. */
  dump_frame_and_meta (ctx, buffer, meta);
  if (ctx->dump_dir && ctx->dump_max_frames > 0 &&
      ctx->dump_count >= ctx->dump_max_frames && ctx->loop) {
    g_print ("Reached dump-max-frames=%" G_GUINT64_FORMAT ", stopping.\n",
        ctx->dump_max_frames);
    gst_sample_unref (sample);
    g_main_loop_quit (ctx->loop);
    return GST_FLOW_OK;
  }
  gst_sample_unref (sample);
  return GST_FLOW_OK;
}

static gboolean
meta_timeout_cb (gpointer user_data)
{
  MetaAppCtx *ctx = (MetaAppCtx *) user_data;
  ctx->timeout_id = 0;
  g_printerr ("meta timeout: no sample received within timeout\n");
  if (ctx->loop)
    g_main_loop_quit (ctx->loop);
  return G_SOURCE_REMOVE;
}

static gboolean
run_seconds_cb (gpointer user_data)
{
  MetaAppCtx *ctx = (MetaAppCtx *) user_data;
  ctx->run_seconds_id = 0;
  g_print ("Run duration reached, stopping.\n");
  if (ctx->loop)
    g_main_loop_quit (ctx->loop);
  return G_SOURCE_REMOVE;
}

static gboolean
sigint_cb (gpointer user_data)
{
  MetaAppCtx *ctx = (MetaAppCtx *) user_data;
  ctx->sigint_id = 0;
  g_print ("SIGINT received, stopping pipeline...\n");
  if (ctx->loop)
    g_main_loop_quit (ctx->loop);
  return G_SOURCE_REMOVE;
}

static gchar *
build_pipeline (const gchar *input_prefix, const gchar *decoder_factory)
{
  gchar *suffix = NULL;
  gchar *trimmed = NULL;
  gchar *launch = NULL;
  const gchar *factory = decoder_factory;

  if (!factory || !factory[0])
    factory = "avdec_h264";
  /* Keep downstream fixed so this tool validates a stable inject->decode->meta flow. */
  suffix = g_strdup_printf (
      "amba_seiinject add-timestamp=true add-gps=true ! "
      "amba_sei_decoder decoder-factory=%s ! "
      "appsink name=sink emit-signals=true max-buffers=4 drop=true sync=false",
      factory);
  if (!suffix)
    return NULL;

  trimmed = g_strdup (input_prefix ? input_prefix : "");
  if (!trimmed) {
    g_free (suffix);
    return NULL;
  }
  g_strstrip (trimmed);
  if (trimmed[0] == '\0') {
    g_free (trimmed);
    return suffix;
  }
  if (g_str_has_suffix (trimmed, "!")) {
    launch = g_strdup_printf ("%s %s", trimmed, suffix);
  } else {
    launch = g_strdup_printf ("%s ! %s", trimmed, suffix);
  }
  g_free (trimmed);
  g_free (suffix);
  return launch;
}

int
main (int argc, char *argv[])
{
  static const char *default_input_prefix =
      "amba_venccap2 stream-id=0 ! queue ! h264parse";
  gchar *opt_pipeline = NULL;
  gchar *opt_decoder_factory = NULL;
  gchar *opt_dump_dir = NULL;
  gint opt_run_seconds = 0;
  gint opt_dump_max_frames = 0;
  guint startup_timeout_s = DEFAULT_STARTUP_TIMEOUT_S;
  GOptionContext *opt_ctx = NULL;
  GError *opt_err = NULL;
  GOptionEntry entries[] = {
    {"pipeline", 'p', 0, G_OPTION_ARG_STRING, &opt_pipeline,
        "Input chain up to parser (h264parse/h265parse); downstream chain is fixed", NULL},
    {"decoder-factory", 'f', 0, G_OPTION_ARG_STRING, &opt_decoder_factory,
        "Internal decoder factory for amba_sei_decoder (default: avdec_h264)", NULL},
    {"dump-dir", 'd', 0, G_OPTION_ARG_STRING, &opt_dump_dir,
        "Directory to dump YUV frames and sei_meta.csv", NULL},
    {"run-seconds", 't', 0, G_OPTION_ARG_INT, &opt_run_seconds,
        "Stop after N seconds (0 means run until EOS/SIGINT)", NULL},
    {"dump-max-frames", 0, 0, G_OPTION_ARG_INT, &opt_dump_max_frames,
        "Maximum dumped frames when --dump-dir is used (capped at 300; auto-stop on reach)", NULL},
    {NULL}
  };
  const char *input_prefix = NULL;
  gchar *launch = NULL;
  GstElement *pipeline = NULL;
  GstElement *appsink = NULL;
  GstBus *bus = NULL;
  GMainLoop *loop = NULL;
  MetaAppCtx ctx;
  GError *err = NULL;
  int retcode = 0;

  /* 1) Parse CLI options and let GStreamer option group consume gst args. */
  opt_ctx = g_option_context_new ("- appsink test for amba_sei_decoder meta");
  g_option_context_add_main_entries (opt_ctx, entries, NULL);
  g_option_context_add_group (opt_ctx, gst_init_get_option_group ());
  if (!g_option_context_parse (opt_ctx, &argc, &argv, &opt_err)) {
    g_printerr ("Option parse error: %s\n", opt_err ? opt_err->message : "unknown");
    g_clear_error (&opt_err);
    g_option_context_free (opt_ctx);
    return 1;
  }
  g_option_context_free (opt_ctx);

  /* 2) Build full pipeline from caller-provided input prefix + fixed downstream. */
  input_prefix = opt_pipeline ? opt_pipeline : default_input_prefix;
  launch = build_pipeline (input_prefix, opt_decoder_factory);
  if (!launch) {
    g_printerr ("Failed to build pipeline string\n");
    retcode = 1;
    goto done;
  }
  memset (&ctx, 0, sizeof (ctx));
  if (opt_dump_max_frames <= 0) {
    ctx.dump_max_frames = MAX_DUMP_FRAMES;
  } else if (opt_dump_max_frames > (gint) MAX_DUMP_FRAMES) {
    ctx.dump_max_frames = MAX_DUMP_FRAMES;
  } else {
    ctx.dump_max_frames = (guint64) opt_dump_max_frames;
  }
  ctx.dump_dir = opt_dump_dir ? g_strdup (opt_dump_dir) : NULL;

  /* 3) Prepare optional dump outputs before starting streaming. */
  if (!open_dump_files (&ctx)) {
    retcode = 1;
    goto done;
  }

  /* 4) Construct runtime objects: pipeline, appsink handle, bus watch, signal handlers. */
  pipeline = gst_parse_launch (launch, &err);
  if (err) {
    g_printerr ("Failed to create pipeline: %s\n", err->message);
    g_printerr ("Pipeline: %s\n", launch);
    g_clear_error (&err);
    retcode = 1;
    goto done;
  }

  appsink = gst_bin_get_by_name (GST_BIN (pipeline), "sink");
  if (!appsink) {
    g_printerr ("Pipeline must contain appsink named \"sink\"\n");
    retcode = 1;
    goto done;
  }

  loop = g_main_loop_new (NULL, FALSE);
  ctx.loop = loop;

  bus = gst_element_get_bus (pipeline);
  gst_bus_add_watch (bus, bus_message_cb, &ctx);
  g_signal_connect (appsink, "new-sample",
      G_CALLBACK (meta_appsink_new_sample_cb), &ctx);
  ctx.sigint_id = g_unix_signal_add (SIGINT, sigint_cb, &ctx);

  /* 5) Transition to PLAYING and arm stop conditions (startup timeout / run-seconds). */
  if (gst_element_set_state (pipeline, GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Failed to set pipeline to PLAYING\n");
    retcode = 1;
    goto done;
  }

  ctx.timeout_id = g_timeout_add_seconds (startup_timeout_s, meta_timeout_cb, &ctx);
  if (opt_run_seconds > 0)
    ctx.run_seconds_id = g_timeout_add_seconds ((guint) opt_run_seconds,
        run_seconds_cb, &ctx);

  g_print ("Running meta test. Press Ctrl+C to stop.\n");
  g_print ("Input prefix: %s\n", input_prefix);
  g_print ("Decoder factory: %s\n",
      (opt_decoder_factory && opt_decoder_factory[0]) ? opt_decoder_factory : "avdec_h264");
  g_print ("Pipeline: %s\n", launch);
  if (ctx.dump_dir)
    g_print ("Dump enabled: %s (max frames=%" G_GUINT64_FORMAT ")\n",
        ctx.dump_dir, ctx.dump_max_frames);
  /* 6) Run until one of stop paths triggers: SIGINT/EOS/ERROR/timeout/dump-limit. */
  g_main_loop_run (loop);

  if (!ctx.got_sample) {
    retcode = 1;
  }
  g_print ("meta summary: samples=%" G_GUINT64_FORMAT
           " samples_with_meta=%" G_GUINT64_FORMAT
           " dumped_frames=%" G_GUINT64_FORMAT "\n",
      ctx.sample_count, ctx.meta_count, ctx.dump_count);
  if (ctx.got_sample && ctx.got_meta)
    g_print ("meta parsing success\n");

done:
  /* 7) Unified cleanup: remove active sources, stop pipeline, release resources. */
  if (ctx.timeout_id)
    g_source_remove (ctx.timeout_id);
  if (ctx.run_seconds_id)
    g_source_remove (ctx.run_seconds_id);
  if (ctx.sigint_id)
    g_source_remove (ctx.sigint_id);
  if (pipeline)
    gst_element_set_state (pipeline, GST_STATE_NULL);
  if (bus)
    gst_object_unref (bus);
  if (appsink)
    gst_object_unref (appsink);
  if (pipeline)
    gst_object_unref (pipeline);
  if (loop)
    g_main_loop_unref (loop);
  close_dump_files (&ctx);
  g_free (ctx.dump_dir);
  g_free (launch);
  g_free (opt_pipeline);
  g_free (opt_decoder_factory);
  g_free (opt_dump_dir);
  return retcode;
}
