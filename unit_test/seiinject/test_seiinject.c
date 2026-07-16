/*
 * test_seiinject.c
 *
 * Unit test for amba_seiinject element.
 * Injects timestamp (and optionally GPS) SEI into H.264/H.265 bitstream.
 *
 * Usage:
 *   test-seiinject [input.h264] [output.h264]
 *   If no args: use built-in minimal H.264 NAL and run pipeline to fakesink.
 *   If one arg: input file -> amba_seiinject -> /tmp/seiinject_out.h264
 *   If two args: input file -> amba_seiinject -> output file.
 *
 * Ensure libgstamba.so is in GST_PLUGIN_PATH when running.
 *
 * Copyright (C) 2025 Ambarella International LP
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <gst/gst.h>

#define DUNUSED(x) ((void)(x))

/* Minimal H.264 Annex B: start code + IDR slice NAL (type 5) with minimal payload.
 * Enough for amba_seiinject to find VCL and inject SEI. */
static const guint8 minimal_h264_nal[] = {
  0x00, 0x00, 0x00, 0x01,  /* start code */
  0x65, 0x88, 0x84, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x5d, 0x90, 0x80  /* IDR slice */
};
static const guint minimal_h264_nal_size = sizeof (minimal_h264_nal);

static gboolean bus_message_cb (GstBus *bus, GstMessage *msg, gpointer user_data)
{
  GMainLoop *loop = (GMainLoop *) user_data;
  DUNUSED(bus);

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ERROR: {
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
    case GST_MESSAGE_EOS:
      g_main_loop_quit (loop);
      break;
    case GST_MESSAGE_WARNING: {
      GError *err = NULL;
      gchar *dbg = NULL;
      gst_message_parse_warning (msg, &err, &dbg);
      g_printerr ("WARNING: %s\n", err ? err->message : "unknown");
      g_clear_error (&err);
      g_free (dbg);
      break;
    }
    default:
      break;
  }
  return TRUE;
}

/* Run pipeline with appsrc pushing minimal H.264 -> amba_seiinject -> fakesink */
static int run_appsrc_test (void)
{
  GstElement *pipeline = NULL;
  GstElement *appsrc = NULL, *seiinject = NULL;
  GstBus *bus = NULL;
  GMainLoop *loop = NULL;
  GstBuffer *buf = NULL;
  GstFlowReturn ret;
  GError *err = NULL;
  int retcode = 0;

  pipeline = gst_parse_launch (
      "appsrc name=src is-live=false format=time ! "
      "video/x-h264, stream-format=byte-stream, parsed=false ! "
      "amba_seiinject name=inj add-timestamp=true add-gps=false ! "
      "video/x-h264, stream-format=byte-stream, parsed=false ! fakesink sync=false",
      &err);
  if (err) {
    g_printerr ("Failed to create pipeline (is libgstamba in GST_PLUGIN_PATH?): %s\n", err->message);
    g_clear_error (&err);
    return 1;
  }

  appsrc = gst_bin_get_by_name (GST_BIN (pipeline), "src");
  seiinject = gst_bin_get_by_name (GST_BIN (pipeline), "inj");
  if (!appsrc || !seiinject) {
    g_printerr ("Failed to get appsrc or amba_seiinject from pipeline\n");
    if (appsrc) gst_object_unref (appsrc);
    if (seiinject) gst_object_unref (seiinject);
    gst_object_unref (pipeline);
    return 1;
  }

  g_object_set (G_OBJECT (appsrc),
      "caps", gst_caps_new_simple ("video/x-h264",
          "stream-format", G_TYPE_STRING, "byte-stream",
          "parsed", G_TYPE_BOOLEAN, FALSE,
          NULL),
      "do-timestamp", TRUE,
      NULL);

  bus = gst_element_get_bus (pipeline);
  loop = g_main_loop_new (NULL, FALSE);
  gst_bus_add_watch (bus, bus_message_cb, loop);

  if (gst_element_set_state (pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Failed to set pipeline to PLAYING\n");
    retcode = 1;
    goto done;
  }

  buf = gst_buffer_new_allocate (NULL, minimal_h264_nal_size, NULL);
  gst_buffer_fill (buf, 0, minimal_h264_nal, minimal_h264_nal_size);
  GST_BUFFER_PTS (buf) = 0;
  GST_BUFFER_DURATION (buf) = GST_SECOND / 30;

  g_signal_emit_by_name (appsrc, "push-buffer", buf, &ret);
  gst_buffer_unref (buf);
  if (ret != GST_FLOW_OK) {
    g_printerr ("push-buffer returned %d\n", ret);
    retcode = 1;
    goto done;
  }

  g_signal_emit_by_name (appsrc, "end-of-stream", &ret);
  g_main_loop_run (loop);

done:
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (bus);
  gst_object_unref (appsrc);
  gst_object_unref (seiinject);
  gst_object_unref (pipeline);
  g_main_loop_unref (loop);
  return retcode;
}

/* Run pipeline: filesrc ! h264parse ! amba_seiinject ! filesink */
static int run_file_test (const char *input_path, const char *output_path)
{
  GstElement *pipeline = NULL;
  GstBus *bus = NULL;
  GMainLoop *loop = NULL;
  GError *err = NULL;
  gchar *launch = NULL;
  int retcode = 0;

  launch = g_strdup_printf (
      "filesrc location=\"%s\" ! video/x-h264, stream-format=byte-stream, parsed=false ! "
      "amba_seiinject add-timestamp=true add-gps=false ! "
      "filesink location=\"%s\"",
      input_path, output_path);

  pipeline = gst_parse_launch (launch, &err);
  g_free (launch);
  if (err) {
    g_printerr ("Failed to create pipeline: %s\n", err->message);
    g_clear_error (&err);
    return 1;
  }

  bus = gst_element_get_bus (pipeline);
  loop = g_main_loop_new (NULL, FALSE);
  gst_bus_add_watch (bus, bus_message_cb, loop);

  if (gst_element_set_state (pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Failed to set pipeline to PLAYING\n");
    retcode = 1;
    goto done;
  }

  g_main_loop_run (loop);

done:
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (bus);
  gst_object_unref (pipeline);
  g_main_loop_unref (loop);
  return retcode;
}

int main (int argc, char *argv[])
{
  int ret;

  gst_init (&argc, &argv);

  if (argc >= 3) {
    ret = run_file_test (argv[1], argv[2]);
  } else if (argc == 2) {
    ret = run_file_test (argv[1], "/tmp/seiinject_out.h264");
    if (ret == 0)
      g_print ("Output written to /tmp/seiinject_out.h264\n");
  } else {
    g_print ("Running built-in test (appsrc -> amba_seiinject -> fakesink)\n");
    ret = run_appsrc_test ();
  }

  return ret;
}
