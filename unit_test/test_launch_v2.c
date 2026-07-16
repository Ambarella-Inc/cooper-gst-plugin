/*
 * test_launch_v2.c
 *
 * History:
 *    11/20/2023 - [pxduan] created file
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
 * SECTION:unit_test-test-launch-v2
 * @title: test-launch-v2
 *
 * This unit test was modified from test-launch for multi-streams in RTSP server.
 *
 * ## Example pipelines, two streams for h265+h264.
 * |[
 * ./test-launch-v2 -c 2 "( amba_venccap stream-id= 0 ! amba_vencdemux name=d d.stream0 ! queue ! h265parse ! rtph265pay name=pay0 pt=96 )" \
 * "( amba_venccap stream-id= 1 ! amba_vencdemux name=d d.stream1 ! queue ! h264parse ! rtph264pay name=pay0 pt=96 )"
 * ]|
 *  Read h265+h264 encoded bit-streams and send to same port with different mounts.
 *
 */


#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>
#include <gst/rtsp-server/rtsp-server-object.h>
#include <gst/rtsp-server/rtsp-media.h>

#define DUNUSED(x) ((void)(x))

#define DEFAULT_VIDEO_COUNT        1
#define DEFAULT_RTSP_PORT      "8554"
#define ATTACH_URL             "/test"
#define DEFAULT_DISABLE_RTCP   FALSE

static char *port = (char *) DEFAULT_RTSP_PORT;
static gboolean disable_rtcp = DEFAULT_DISABLE_RTCP;
static int count = DEFAULT_VIDEO_COUNT;
static GMainLoop *global_loop = NULL;
static GstRTSPServer *global_server = NULL;
static GList *media_list = NULL;  /* List of active GstRTSPMedia */
static GMutex media_list_mutex;   /* Mutex to protect media_list */

static GOptionEntry entries[] = {
  {"port", 'p', 0, G_OPTION_ARG_STRING, &port,
      "Port to listen on (default: " DEFAULT_RTSP_PORT ")", "PORT"},
  {"disable-rtcp", '\0', 0, G_OPTION_ARG_NONE, &disable_rtcp,
      "Whether RTCP should be disabled (default false)", NULL},
  {"count", 'c', 0, G_OPTION_ARG_INT, &count,
      "Video count to send (default: 1)", "COUNT"},
  {NULL}
};

/* called when a stream has received an RTCP packet from the client */
static void on_ssrc_active (GObject * session, GObject * source, GstRTSPMedia * media)
{
  GstStructure *stats;

  DUNUSED(media);

  GST_INFO ("source %p in session %p is active", source, session);
  g_object_get (source, "stats", &stats, NULL);
  if (stats) {
    gchar *sstr;
    sstr = gst_structure_to_string (stats);
    GST_INFO ("structure: %s\n", sstr);
    g_free (sstr);
    gst_structure_free (stats);
  }
}
static void on_sender_ssrc_active (GObject * session, GObject * source, GstRTSPMedia * media)
{
  on_ssrc_active(session, source, media);
}

static void send_eos_to_pipeline (GstElement * element)
{
  if (element == NULL)
    return;

  GST_INFO ("Sending EOS to pipeline: %s", GST_OBJECT_NAME (element));
  gst_element_send_event (element, gst_event_new_eos ());
}

/* Send EOS to all active media pipelines */
static void send_eos_to_all_media (void)
{
  GList *l;
  GstElement *element;

  g_mutex_lock (&media_list_mutex);

  for (l = media_list; l != NULL; l = l->next) {
    GstRTSPMedia *media = (GstRTSPMedia *) l->data;

    if (media == NULL)
      continue;

    element = gst_rtsp_media_get_element (media);
    if (element != NULL) {
      /* Send EOS to the pipeline, which will propagate to all sinks */
      send_eos_to_pipeline (element);
      gst_object_unref (element);
    }
  }

  g_mutex_unlock (&media_list_mutex);

  /* Give some time for EOS to propagate and files to be finalized
   * This is critical: without this wait, the program exits too quickly
   * and files may not be properly closed, making them unplayable.
   * The wait allows:
   * 1. EOS event to propagate through the pipeline
   * 2. mux elements to finalize file headers and metadata
   * 3. filesink to flush all buffered data to disk */
  g_usleep (1000000);  /* 1 second */
}

/* Signal handler for graceful shutdown */
static void signal_handler (int signum)
{
  GST_INFO ("Received signal %d, sending EOS to all pipelines...", signum);

  /* Send EOS to all active media pipelines */
  send_eos_to_all_media ();

  /* Quit the main loop */
  if (global_loop != NULL) {
    g_main_loop_quit (global_loop);
  }
}

/* Callback when media is removed/unprepared */
static void media_unprepared_cb (GstRTSPMedia * media, gpointer user_data)
{
  DUNUSED(user_data);

  g_mutex_lock (&media_list_mutex);
  /* Remove media from list (using pointer comparison) */
  GList *found = g_list_find (media_list, media);
  if (found != NULL) {
    media_list = g_list_delete_link (media_list, found);
    GST_INFO ("Media %p removed from list, %d media remaining",
        media, g_list_length (media_list));
  }
  g_mutex_unlock (&media_list_mutex);
}

/* signal callback when the media is prepared for streaming. We can get the
 * session manager for each of the streams and connect to some signals. */
static void media_prepared_cb (GstRTSPMedia * media)
{
  guint i, n_streams;
  n_streams = gst_rtsp_media_n_streams (media);
  GST_INFO ("media %p is prepared and has %u streams", media, n_streams);

  /* Add media to the list for EOS handling on exit. */
  g_mutex_lock (&media_list_mutex);
  if (g_list_find (media_list, media) == NULL) {
    media_list = g_list_append (media_list, media);
    GST_INFO ("Media %p added to list, total %d media",
        media, g_list_length (media_list));
  }
  g_mutex_unlock (&media_list_mutex);

  /* Connect to unprepared signal to remove from list when media is removed */
  g_signal_connect (media, "unprepared", (GCallback) media_unprepared_cb, NULL);

  for (i = 0; i < n_streams; i++) {
    GstRTSPStream *stream;
    GObject *session;
    stream = gst_rtsp_media_get_stream (media, i);
    if (stream == NULL)
    {
      continue;
    }
    session = gst_rtsp_stream_get_rtpsession (stream);
    GST_INFO ("watching session %p on stream %u", session, i);
    g_signal_connect (session, "on-ssrc-active",        (GCallback) on_ssrc_active,        media);
    g_signal_connect (session, "on-sender-ssrc-active", (GCallback) on_sender_ssrc_active, media);
  }
}
static void media_configure_cb (GstRTSPMediaFactory * factory, GstRTSPMedia * media)
{
  /* connect our prepared signal so that we can see when this media is
   * prepared for streaming */
  g_signal_connect (media, "prepared", (GCallback) media_prepared_cb, factory);
}
int main (int argc, char *argv[])
{
  GMainLoop           *loop    = NULL;
  GstRTSPServer       *server  = NULL;
  GstRTSPMountPoints  *mounts  = NULL;
  GstRTSPMediaFactory *factory = NULL;
  GOptionContext      *optctx  = NULL;
  GError              *error   = NULL;
  char temp_buffer[256]        = {0};
  int index = 0;

  optctx = g_option_context_new ("<launch line> - Test RTSP Server, Launch");
  g_option_context_add_main_entries (optctx, entries, NULL);
  g_option_context_add_group (optctx, gst_init_get_option_group ());
  if (!g_option_context_parse (optctx, &argc, &argv, &error)) {
    g_printerr ("Error parsing options: %s\n", error->message);
    g_option_context_free (optctx);
    g_clear_error (&error);
    return -1;
  }
  g_option_context_free (optctx);

  /* Initialize mutex for media list */
  g_mutex_init (&media_list_mutex);

  /* Set up signal handlers for graceful shutdown (like gst-launch -e) */
  signal (SIGINT, signal_handler);
  signal (SIGTERM, signal_handler);

  loop = g_main_loop_new (NULL, FALSE);
  global_loop = loop;  /* Save for signal handler */
  server = gst_rtsp_server_new ();
  global_server = server;  /* Save for potential cleanup */
  g_object_set (server, "service", port, NULL);
  /* get the mount points for this server, every server has a default object
   * that be used to map uri mount points to media factories */
  mounts = gst_rtsp_server_get_mount_points (server);
  for (index = 0; index < count; index++) {
    /* make a media factory for a test stream. The default media factory can use
     * gst-launch syntax to create pipelines.
     * any launch line works as long as it contains elements named pay%d. Each
     * element with pay%d names will be a stream */
    factory = gst_rtsp_media_factory_new ();
    if (argv[index+1]) {
      gst_rtsp_media_factory_set_launch (factory, argv[index+1]);
    } else {
      g_printerr ("Error parsing options: %d\n", index+1);
      return -1;
    }
    g_signal_connect (factory, "media-configure", (GCallback) media_configure_cb, factory);
    gst_rtsp_media_factory_set_shared (factory, TRUE);
    //gst_rtsp_media_factory_set_enable_rtcp (factory, !disable_rtcp);
    snprintf(temp_buffer, sizeof(temp_buffer) - 1, "%s%d", ATTACH_URL, index);
    /* attach the test factory to the /test url */
    gst_rtsp_mount_points_add_factory (mounts, temp_buffer, factory);

    g_print ("stream ready at rtsp://127.0.0.1:%s%s\n", port, temp_buffer);
  }
  /* don't need the ref to the mapper anymore */
  g_object_unref (mounts);
  /* attach the server to the default maincontext */
  gst_rtsp_server_attach (server, NULL);
  g_main_loop_run (loop);

  /* Note: EOS should have been sent by signal_handler already
   * Only send again if main loop exited for other reasons */
  if (media_list != NULL && g_list_length (media_list) > 0) {
    g_print ("Shutting down, sending EOS to all pipelines...\n");
    send_eos_to_all_media ();
  }

  /* Clean up media list. */
  g_mutex_lock (&media_list_mutex);
  if (media_list != NULL) {
    g_list_free (media_list);
    media_list = NULL;
  }
  g_mutex_unlock (&media_list_mutex);
  g_mutex_clear (&media_list_mutex);

  if (server) {
    g_object_unref (server);
  }
  if (loop) {
    g_main_loop_unref (loop);
  }

  return 0;
}

