/*
 * test_event_recording.c
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

/**
 * SECTION:unit_test-test-event-recording
 * @title: test-event-recording
 *
 * Reference application template for integrating amba_event_recorder into a host
 * pipeline. Configure the element (location, mux, pre/post, primary-pad, ...) in the
 * gst-parse string; this program only handles bus, signals, stdin, and trigger/EOS.
 *
 * ## Examples
 * |[
 * test-event-recording --auto --trigger-period=4 --trigger-count=10
 * test-event-recording -p '<gst-parse graph; link parsers to amba_event_recorder request pads>'
 * ]|
 */

#include <gst/gst.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define APP_LOG_PREFIX "[AER-APP]"

/* Cap --auto triggers to avoid unbounded output files. */
#define AER_AUTO_TRIGGER_COUNT_MAX 1000

/* Default graph: tune location/mux/pre/post in this string if you do not pass -p. */
#define DEFAULT_PIPELINE_DESC \
  "videotestsrc is-live=true pattern=18 ! queue ! openh264enc gop-size=30 ! h264parse ! " \
  "amba_event_recorder name=aer location=/tmp/amba-event-recorder-example-%05d.mp4 "

typedef enum {
  AER_EXIT_OK = 0,               /* success */
  AER_EXIT_BAD_ARGS = 1,         /* invalid options or gst_init_check() failed */
  AER_EXIT_BAD_PIPELINE = 2,     /* pipeline string invalid or no amba_event_recorder */
  AER_EXIT_BAD_STATE = 3,        /* pipeline could not be set to PLAYING */
  AER_EXIT_RUNTIME_ERROR = 4     /* GST_MESSAGE_ERROR on bus */
} AerExitCode;


typedef struct {
  GMainLoop *loop;
  GstElement *pipeline;
  GstElement *aer;
  gboolean eos_sent;
  gboolean got_error;
} AppCtx;

typedef struct {
  AppCtx *ctx;
  guint interval_s;
  guint remaining_triggers;
  guint seq;
} ScheduledState;

/* Set while the main loop is running so SIGINT/SIGTERM can call send_pipeline_eos(). */
static AppCtx *g_app_ctx = NULL;

static gboolean scheduled_eos_after_last_cb (gpointer user_data);

/* --- Pipeline: locate amba_event_recorder in the graph --- */

static GstElement *
find_first_amba_event_recorder (GstElement *root)
{
  GstIterator *it = NULL;
  GValue v = G_VALUE_INIT;
  GstElement *found = NULL;

  if (!root) {
    return NULL;
  }
  if (!GST_IS_BIN (root)) {
    GstElementFactory *fac = gst_element_get_factory (root);
    if (fac && g_strcmp0 (gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (fac)),
            "amba_event_recorder") == 0) {
      return root;
    }
    return NULL;
  }

  it = gst_bin_iterate_recurse (GST_BIN (root));
  if (!it) {
    return NULL;
  }

  while (TRUE) {
    switch (gst_iterator_next (it, &v)) {
      case GST_ITERATOR_OK: {
        GstElement *e = GST_ELEMENT (g_value_get_object (&v));
        GstElementFactory *fac = gst_element_get_factory (e);
        if (fac && g_strcmp0 (gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (fac)),
                "amba_event_recorder") == 0) {
          found = e;
          g_value_unset (&v);
          goto done;
        }
        g_value_unset (&v);
        break;
      }
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (it);
        break;
      default:
        goto done;
    }
  }

done:
  gst_iterator_free (it);
  return found;
}

/* --- Help / console output --- */

static void
print_pipeline_syntax_help (void)
{
  g_print (APP_LOG_PREFIX " Pipeline (all amba_event_recorder props go in the gst-parse string):\n");
  g_print (APP_LOG_PREFIX "   ... ! amba_event_recorder name=<name> location=<pattern> mux-factory=<mux> \\\n");
  g_print (APP_LOG_PREFIX "       primary-pad=sink_video_0 pre-record-ms=<ms> post-record-ms=<ms> \\\n");
  g_print (APP_LOG_PREFIX "       align-start-keyframe=true|false [mux-properties=k=v,...] \\\n");
  g_print (APP_LOG_PREFIX "       ! <name>.sink_video_0   (and optional sink_audio_0, sink_audio_1, ...)\n");
  g_print (APP_LOG_PREFIX " Default pipeline if -p is omitted:\n");
  g_print (APP_LOG_PREFIX "   %s\n", DEFAULT_PIPELINE_DESC);
}

static void
print_recording_config (GstElement *aer, gboolean custom_pipeline)
{
  gchar *location = NULL;
  gchar *mux_factory = NULL;
  gchar *mux_properties = NULL;
  gchar *primary_pad = NULL;
  gint pre_ms = 0;
  gint post_ms = 0;
  gboolean align_kf = TRUE;

  if (!aer) {
    return;
  }
  g_object_get (aer,
      "location", &location,
      "mux-factory", &mux_factory,
      "mux-properties", &mux_properties,
      "primary-pad", &primary_pad,
      "pre-record-ms", &pre_ms,
      "post-record-ms", &post_ms,
      "align-start-keyframe", &align_kf,
      NULL);

  g_print (APP_LOG_PREFIX " Recording config (%s pipeline, element %s):\n",
      custom_pipeline ? "custom -p" : "default built-in",
      GST_OBJECT_NAME (aer));
  g_print (APP_LOG_PREFIX "   location             %s\n", location ? location : "(null)");
  g_print (APP_LOG_PREFIX "   mux-factory          %s\n", mux_factory ? mux_factory : "(null)");
  g_print (APP_LOG_PREFIX "   mux-properties       %s\n",
      (mux_properties && mux_properties[0]) ? mux_properties : "(none)");
  g_print (APP_LOG_PREFIX "   primary-pad          %s\n", primary_pad ? primary_pad : "(null)");
  g_print (APP_LOG_PREFIX "   pre-record-ms        %d\n", pre_ms);
  g_print (APP_LOG_PREFIX "   post-record-ms       %d\n", post_ms);
  g_print (APP_LOG_PREFIX "   align-start-keyframe %s\n", align_kf ? "true" : "false");

  g_free (location);
  g_free (mux_factory);
  g_free (mux_properties);
  g_free (primary_pad);
}

static void
print_usage_interactive (void)
{
  print_pipeline_syntax_help ();
  g_print (APP_LOG_PREFIX " stdin:  t = trigger (uses element pre-record-ms / post-record-ms)\n");
  g_print (APP_LOG_PREFIX "         eos|quit|q|exit = graceful shutdown (same as Ctrl+C)\n");
  g_print (APP_LOG_PREFIX "         help = show this text again\n");
}

static void
print_startup_stdin_hint (void)
{
  g_print (APP_LOG_PREFIX " stdin: t | eos|quit|q|exit | help\n");
}

/* --- Trigger action & EOS --- */

/* Reads pre/post from element properties (set in the pipeline string) and emits the action signal. */
static void
emit_trigger (AppCtx *ctx, const gchar *event_id)
{
  gint pre_ms = 0;
  gint post_ms = 0;

  if (!ctx || !ctx->aer) {
    return;
  }
  g_object_get (ctx->aer, "pre-record-ms", &pre_ms, "post-record-ms", &post_ms, NULL);
  g_signal_emit_by_name (ctx->aer, "trigger",
      (guint64) GST_CLOCK_TIME_NONE, pre_ms, post_ms, event_id);
  g_print (APP_LOG_PREFIX " trigger id=%s pre_ms=%d post_ms=%d\n", event_id, pre_ms, post_ms);
}

/* Downstream EOS so muxers finalize; duplicate calls are harmless (eos_sent guard). */
static void
send_pipeline_eos (AppCtx *ctx)
{
  if (!ctx || !ctx->pipeline) {
    return;
  }
  if (ctx->eos_sent) {
    g_print (APP_LOG_PREFIX " EOS already sent; waiting for pipeline to drain...\n");
    return;
  }
  g_print (APP_LOG_PREFIX " sending EOS to pipeline...\n");
  gst_element_send_event (ctx->pipeline, gst_event_new_eos ());
  ctx->eos_sent = TRUE;
}

static void
signal_handler (int signum)
{
  g_print (APP_LOG_PREFIX " signal %d: graceful shutdown (EOS)\n", signum);
  if (g_app_ctx) {
    send_pipeline_eos (g_app_ctx);
  }
}

/* --- Bus --- */

/* ERROR: set got_error and stop. EOS: normal shutdown after send_pipeline_eos(). */
static gboolean
bus_watch (GstBus *bus, GstMessage *msg, gpointer user_data)
{
  AppCtx *ctx = (AppCtx *) user_data;
  (void) bus;

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ERROR: {
      GError *err = NULL;
      gchar *dbg = NULL;
      gst_message_parse_error (msg, &err, &dbg);
      g_printerr (APP_LOG_PREFIX " ERROR: %s\n", err ? err->message : "?");
      if (dbg) {
        g_printerr (APP_LOG_PREFIX " DEBUG: %s\n", dbg);
      }
      g_clear_error (&err);
      g_free (dbg);
      ctx->got_error = TRUE;
      if (ctx->loop) {
        g_main_loop_quit (ctx->loop);
      }
      break;
    }
    case GST_MESSAGE_EOS:
      g_print (APP_LOG_PREFIX " GST_MESSAGE_EOS - quitting main loop\n");
      if (ctx->loop) {
        g_main_loop_quit (ctx->loop);
      }
      break;
    case GST_MESSAGE_ELEMENT: {
      const GstStructure *s = gst_message_get_structure (msg);
      const gchar *name = NULL;
      if (!s) {
        break;
      }
      name = gst_structure_get_name (s);
      if (g_strcmp0 (name, "window-opened") == 0 ||
          g_strcmp0 (name, "window-extended") == 0 ||
          g_strcmp0 (name, "window-closed") == 0) {
        gchar *sstr = gst_structure_to_string (s);
        g_print (APP_LOG_PREFIX " element-msg %s: %s\n", name, sstr ? sstr : "");
        g_free (sstr);
      }
      break;
    }
    default:
      break;
  }
  return TRUE;
}

/* --- Interactive stdin --- */

static gboolean
stdin_cmd_cb (GIOChannel *io, GIOCondition cond, gpointer user_data)
{
  AppCtx *ctx = (AppCtx *) user_data;
  gchar *line = NULL;
  gsize len = 0;
  GError *err = NULL;
  GIOStatus st;
  gchar **tok = NULL;

  if (cond & (G_IO_ERR | G_IO_HUP)) {
    return FALSE;
  }

  st = g_io_channel_read_line (io, &line, &len, NULL, &err);
  if (st != G_IO_STATUS_NORMAL || !line) {
    if (err) {
      g_printerr (APP_LOG_PREFIX " stdin error: %s\n", err->message);
      g_error_free (err);
    }
    g_free (line);
    return TRUE;
  }
  g_strstrip (line);
  if (line[0] == '\0') {
    g_free (line);
    return TRUE;
  }

  tok = g_strsplit_set (line, " \t", 0);
  if (tok && tok[0] && g_strcmp0 (tok[0], "t") == 0) {
    static guint seq = 0;
    gchar event_id[64];
    seq++;
    g_snprintf (event_id, sizeof (event_id), "app-ev-%u", seq);
    emit_trigger (ctx, event_id);
  } else if (tok && tok[0] && (g_strcmp0 (tok[0], "eos") == 0 ||
          g_strcmp0 (tok[0], "quit") == 0 ||
          g_strcmp0 (tok[0], "q") == 0 ||
          g_strcmp0 (tok[0], "exit") == 0)) {
    send_pipeline_eos (ctx);
  } else if (tok && tok[0] && g_strcmp0 (tok[0], "help") == 0) {
    print_usage_interactive ();
  } else {
    g_print (APP_LOG_PREFIX " unknown command; type 'help'\n");
  }

  g_strfreev (tok);
  g_free (line);
  return TRUE;
}

/* --- Auto (--auto): timed triggers then EOS --- */

/* One shot: emit trigger, then either schedule the next trigger or EOS after trigger-period. */
static gboolean
scheduled_trigger_tick_cb (gpointer user_data)
{
  ScheduledState *st = (ScheduledState *) user_data;
  AppCtx *ctx = st->ctx;
  gchar event_id[64];

  if (!ctx || ctx->got_error) {
    g_free (st);
    return G_SOURCE_REMOVE;
  }

  st->seq++;
  g_snprintf (event_id, sizeof (event_id), "sched-%u", st->seq);
  g_print (APP_LOG_PREFIX " auto: trigger #%u\n", st->seq);
  emit_trigger (ctx, event_id);

  st->remaining_triggers--;
  if (st->remaining_triggers > 0) {
    g_timeout_add_seconds (st->interval_s, scheduled_trigger_tick_cb, st);
    return G_SOURCE_REMOVE;
  }

  g_timeout_add_seconds (st->interval_s, scheduled_eos_after_last_cb, st);
  return G_SOURCE_REMOVE;
}

static gboolean
scheduled_eos_after_last_cb (gpointer user_data)
{
  ScheduledState *st = (ScheduledState *) user_data;
  AppCtx *ctx = st->ctx;

  if (!ctx || ctx->got_error) {
    g_free (st);
    return G_SOURCE_REMOVE;
  }
  g_print (APP_LOG_PREFIX " auto: sending EOS (after last trigger + period)\n");
  send_pipeline_eos (ctx);
  g_free (st);
  return G_SOURCE_REMOVE;
}

int
main (int argc, char *argv[])
{
  AppCtx ctx;
  GOptionContext *opt = NULL;
  GError *opt_err = NULL;
  gchar *pipeline_desc = NULL;
  gboolean auto_mode = FALSE;
  gboolean no_interactive = FALSE;
  gdouble trigger_period_s = 5.0;
  gint trigger_count = 1;

  GstBus *bus = NULL;
  GIOChannel *stdin_io = NULL;
  guint stdin_watch = 0;
  const gchar *launch_line = NULL;
  AerExitCode exit_code = AER_EXIT_OK;

  GOptionEntry entries[] = {
    { "pipeline", 'p', 0, G_OPTION_ARG_STRING, &pipeline_desc,
        "gst-parse pipeline; set amba_event_recorder props (location, mux, pre/post, primary-pad, ...). "
        "Omit to use built-in default graph.", NULL },
    { "auto", 0, 0, G_OPTION_ARG_NONE, &auto_mode,
        "Scheduled triggers then EOS (no stdin); use with --trigger-period / --trigger-count", NULL },
    { "no-interactive", 0, 0, G_OPTION_ARG_NONE, &no_interactive,
        "Same as --auto", NULL },
    { "trigger-period", 0, 0, G_OPTION_ARG_DOUBLE, &trigger_period_s,
        "Seconds: wait before first trigger, spacing between triggers, wait after last trigger before EOS (default: 5)", NULL },
    { "trigger-count", 0, 0, G_OPTION_ARG_INT, &trigger_count,
        "With --auto: number of triggers, 1..1000 (default: 1)", NULL },
    { NULL }
  };

  memset (&ctx, 0, sizeof (ctx));

  /* --- CLI: only -p and --auto scheduling knobs; element props live in the pipeline string. */
  opt = g_option_context_new ("- amba_event_recorder shell (configure element in -p)");
  g_option_context_set_description (opt,
      "Set all amba_event_recorder properties in the gst-parse string (-p). "
      "Omit -p to use the built-in default pipeline.\n\n"
      "Default pipeline (no -p):\n"
      DEFAULT_PIPELINE_DESC "\n\n"
      "For multiple audio tracks, add branches and link to sink_audio_0, sink_audio_1, ...\n");
  g_option_context_add_main_entries (opt, entries, NULL);
  g_option_context_add_group (opt, gst_init_get_option_group ());
  if (!g_option_context_parse (opt, &argc, &argv, &opt_err)) {
    g_printerr ("option error: %s\n", opt_err ? opt_err->message : "?");
    g_clear_error (&opt_err);
    g_option_context_free (opt);
    exit_code = AER_EXIT_BAD_ARGS;
    goto out;
  }
  g_option_context_free (opt);

  if (auto_mode || no_interactive) {
    auto_mode = TRUE;
  }
  if (trigger_period_s <= 0.0) {
    trigger_period_s = 5.0;
  }
  if (auto_mode && (trigger_count < 1 || trigger_count > AER_AUTO_TRIGGER_COUNT_MAX)) {
    g_printerr ("--auto: --trigger-count must be between 1 and %d (got %d)\n",
        AER_AUTO_TRIGGER_COUNT_MAX, trigger_count);
    exit_code = AER_EXIT_BAD_ARGS;
    goto out;
  }

  signal (SIGINT, signal_handler);
  signal (SIGTERM, signal_handler);

  if (!gst_init_check (&argc, &argv, &opt_err)) {
    g_printerr ("gst_init_check failed: %s\n", opt_err ? opt_err->message : "?");
    g_clear_error (&opt_err);
    exit_code = AER_EXIT_BAD_ARGS;
    goto out;
  }

  launch_line = (pipeline_desc && pipeline_desc[0]) ? pipeline_desc : DEFAULT_PIPELINE_DESC;

  {
    GError *parse_err = NULL;
    GstElement *parsed = NULL;

    /* --- Build graph from gst-parse string; wrapper pipeline if parse returns a single element. */
    parsed = gst_parse_launch (launch_line, &parse_err);
    if (!parsed) {
      g_printerr ("gst_parse_launch failed: %s\n", parse_err ? parse_err->message : "?");
      g_clear_error (&parse_err);
      exit_code = AER_EXIT_BAD_PIPELINE;
      goto out;
    }
    if (GST_IS_PIPELINE (parsed)) {
      ctx.pipeline = parsed;
    } else {
      ctx.pipeline = gst_pipeline_new ("amba-aer-example");
      if (!ctx.pipeline) {
        gst_object_unref (parsed);
        exit_code = AER_EXIT_BAD_PIPELINE;
        goto out;
      }
      gst_bin_add (GST_BIN (ctx.pipeline), parsed);
    }

    ctx.aer = find_first_amba_event_recorder (ctx.pipeline);
    if (!ctx.aer) {
      g_printerr ("pipeline has no element with factory name \"amba_event_recorder\"\n");
      exit_code = AER_EXIT_BAD_PIPELINE;
      goto pipeline_fail;
    }

    ctx.loop = g_main_loop_new (NULL, FALSE);
    g_app_ctx = &ctx;

    bus = gst_element_get_bus (ctx.pipeline);
    gst_bus_add_watch (bus, bus_watch, &ctx);
    gst_object_unref (bus);
    bus = NULL;

    if (gst_element_set_state (ctx.pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
      g_printerr ("GST_STATE_PLAYING failed\n");
      exit_code = AER_EXIT_BAD_STATE;
      goto loop_fail;
    }

    g_print (APP_LOG_PREFIX " state PLAYING\n");
    print_recording_config (ctx.aer, pipeline_desc && pipeline_desc[0]);

    if (auto_mode) {
      /* Timeline: wait trigger-period, then up to trigger-count triggers, then EOS after last + period. */
      ScheduledState *dst = g_new0 (ScheduledState, 1);
      guint interval_s = (guint) (trigger_period_s + 0.5);

      if (interval_s < 1) {
        interval_s = 1;
      }
      dst->ctx = &ctx;
      dst->interval_s = interval_s;
      dst->remaining_triggers = (guint) trigger_count;
      dst->seq = 0;
      g_print (APP_LOG_PREFIX " --auto: trigger-count=%d trigger-period=%us (EOS after last + %us)\n",
          trigger_count, interval_s, interval_s);
      /* First run after initial wait; later runs are scheduled from scheduled_trigger_tick_cb. */
      g_timeout_add_seconds (interval_s, scheduled_trigger_tick_cb, dst);
    } else {
      /* Interactive: stdin drives trigger/EOS; same pipeline lifecycle as --auto. */
      print_startup_stdin_hint ();
      stdin_io = g_io_channel_unix_new (0);
      if (stdin_io) {
        g_io_channel_set_encoding (stdin_io, NULL, NULL);
        g_io_channel_set_flags (stdin_io, G_IO_FLAG_NONBLOCK, NULL);
        stdin_watch = g_io_add_watch (stdin_io,
            (GIOCondition) (G_IO_IN | G_IO_HUP | G_IO_ERR), stdin_cmd_cb, &ctx);
      }
    }

    /* Blocks until EOS, bus ERROR, or g_main_loop_quit() from signal/stdin path. */
    g_main_loop_run (ctx.loop);

    g_app_ctx = NULL;

    if (stdin_watch > 0) {
      g_source_remove (stdin_watch);
    }
    if (stdin_io) {
      g_io_channel_unref (stdin_io);
    }

loop_fail:
    g_app_ctx = NULL;
    /* Tear down: NULL state first so pads and writers flush; then drop our refs. */
    if (ctx.pipeline) {
      gst_element_set_state (ctx.pipeline, GST_STATE_NULL);
      gst_object_unref (ctx.pipeline);
      ctx.pipeline = NULL;
    }
    /* ctx.aer pointed at a child of the pipeline; invalid after pipeline teardown. */
    ctx.aer = NULL;
    if (ctx.loop) {
      g_main_loop_unref (ctx.loop);
      ctx.loop = NULL;
    }
    if (ctx.got_error && exit_code == AER_EXIT_OK) {
      exit_code = AER_EXIT_RUNTIME_ERROR;
    }
    goto out;
  }

pipeline_fail:
  if (ctx.pipeline) {
    gst_object_unref (ctx.pipeline);
    ctx.pipeline = NULL;
  }

out:
  g_free (pipeline_desc);
  return (int) exit_code;
}
