/*
 * test_client.c
 *
 * History:
 *    10/21/2024 - [Yang Yu] created file
 *
 * Copyright (C) 2024 Ambarella International LP
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

#include <gst/gst.h>
#include <glib.h>
#include <gio/gio.h>
#include <gst/rtsp-server/rtsp-server.h>
#include <gst/rtsp-server/rtsp-server-object.h>
#include <gst/rtsp-server/rtsp-media.h>

#include "rtsp_config.h"
#include "simple_verify_tls_interaction.h"
#include "chain_verify_tls_interaction.h"

#define DUNUSED(x) ((void)(x))

#define DEFAULT_RTSP_VERSION 1
#define DEFAULT_USE_FAKESINK FALSE
#define DEFAULT_ENABLE_SECURE FALSE
#define DEFAULT_VERIFY_CHAIN FALSE

static char *config_file = (char *)DEFAULT_CLIENT_CONF_NAME;
static char *rtsp_url = NULL;
static gboolean use_fakesink = DEFAULT_USE_FAKESINK;
static gboolean enable_secure = DEFAULT_ENABLE_SECURE;
static gboolean cert_chain_verify = DEFAULT_VERIFY_CHAIN;
static int rtsp_ver = DEFAULT_RTSP_VERSION;

static rtsp_client_config config_param;

static GOptionEntry entries[] = {
    {"url", 'u', 0, G_OPTION_ARG_STRING, &rtsp_url,
     "URL of the RTSP stream, e.g. rtsp://127.0.0.1:8554/test", "URL"},
    {"ver", 'v', 0, G_OPTION_ARG_INT, &rtsp_ver,
     "RTSP version to use (default: 1)", "VERSION"},
    {"conf", 'f', 0, G_OPTION_ARG_STRING, &config_file,
     "Config file name, settings in file have higher priority (default: " DEFAULT_CLIENT_CONF_NAME ").", "FILENAME"},
    {"use-fakesink", '\0', 0, G_OPTION_ARG_NONE, &use_fakesink,
     "Whether use fakesink for test (default false)", NULL},
    {"enable-secure", '\0', 0, G_OPTION_ARG_NONE, &enable_secure,
     "Whether security feature (include rtsp over tls, srtp and basic authentication) should be enabled (default false)", NULL},
    {"cert-chain-verify", '\0', 0, G_OPTION_ARG_NONE, &cert_chain_verify,
     "Whether verify the full certificate chain during TLS/SSL (default false)", NULL},
    {NULL}};

static gboolean
bus_call(GstBus *bus, GstMessage *msg, gpointer data)
{
    GMainLoop *loop = (GMainLoop *)data;

    DUNUSED(bus);

    switch (GST_MESSAGE_TYPE(msg))
    {
    case GST_MESSAGE_EOS:
        g_print("Stream Ends\n");
        g_main_loop_quit(loop);
        break;
    case GST_MESSAGE_ERROR:
    {
        gchar *debug;
        GError *error;
        gst_message_parse_error(msg, &error, &debug);
        g_free(debug);
        g_printerr("Error: %s\n", error->message);
        g_error_free(error);
        g_main_loop_quit(loop);
        break;
    }
    default:
        break;
    }
    return TRUE;
}

/* Dynamically link */
static void
on_pad_added(GstElement *element, GstPad *pad, gpointer data)
{
    GstPad *sinkpad;
    GstPadLinkReturn ret;
    GstElement *decoder = (GstElement *)data;

    DUNUSED(element);

    /* We can now link this pad with the rtsp-decoder sink pad */
    g_debug("Dynamic pad created, linking source/demuxer\n");
    sinkpad = gst_element_get_static_pad(decoder, "sink");
    /* If our converter is already linked, we have nothing to do here */
    if (gst_pad_is_linked(sinkpad))
    {
        g_debug("*** We are already linked ***\n");
        gst_object_unref(sinkpad);
        return;
    }
    else
    {
        g_debug("proceeding to linking ...\n");
    }
    ret = gst_pad_link(pad, sinkpad);
    if (GST_PAD_LINK_FAILED(ret))
    {
        g_print("failed to link dynamically\n");
    }
    else
    {
        g_debug("dynamically link successful\n");
    }
    gst_object_unref(sinkpad);
}

int main(int argc, char *argv[])
{

    GMainLoop *loop;
    GstBus *bus;
    GOptionContext *optctx = NULL;
    GError *error = NULL;
    GstElement *pipeline;
    GstElement *rtspsrc;
    GstElement *rtph264depay;
    GstElement *h264parse;
    GstElement *videodecoder;
    GstElement *videoqueue0;
    GstElement *videoconvert;
    GstElement *videosink;

    memset(&config_param, 0, sizeof(config_param));

    optctx = g_option_context_new("<launch line> - Test RTSP Client, Launch\n\n"
                                  "Example: test-client -u rtsps://127.0.0.1:8554/test0 -v 2 --enable-secure");
    g_option_context_add_main_entries(optctx, entries, NULL);
    g_option_context_add_group(optctx, gst_init_get_option_group());
    if (!g_option_context_parse(optctx, &argc, &argv, &error))
    {
        g_printerr("Error parsing options: %s\n", error->message);
        g_option_context_free(optctx);
        g_clear_error(&error);
        return -1;
    }
    g_option_context_free(optctx);
    if (!get_rtsp_client_config(&config_param, config_file))
    {
        g_printerr("Set config failed  ... Exiting\n");
        exit(-1);
    }
    if (!strlen(config_param.rtsp_stream_url))
    {
        if (rtsp_url)
        {
            strncpy(config_param.rtsp_stream_url, rtsp_url, MAXBUF - 1);
        }
        else
        {
            g_printerr("RTSP URL not given ... Exiting\n");
            exit(-2);
        }
    }
    /* Initializing GStreamer */
    gst_init(&argc, &argv);
    loop = g_main_loop_new(NULL, FALSE);

    /* Create Pipe's Elements */
    pipeline = gst_pipeline_new("video player");

    rtspsrc = gst_element_factory_make("rtspsrc", "rtspsrc0");
    rtph264depay = gst_element_factory_make("rtph264depay", "rtph264depay0");
    h264parse = gst_element_factory_make("h264parse", "h264parse0");
    videodecoder = gst_element_factory_make("decodebin", "h264_decoder0");
    videoqueue0 = gst_element_factory_make("queue", "videoqueue0");
    videoconvert = gst_element_factory_make("videoconvert", "videoconvert0");
    if (!use_fakesink)
    {
        videosink = gst_element_factory_make("autovideosink", "autovideosink0");
    }
    else
    {
        videosink = gst_element_factory_make("fakesink", "fakesink0");
    }

    // Make sure: Every elements was created ok
    if (!pipeline || !rtspsrc || !rtph264depay || !h264parse || !videodecoder || !videoqueue0 || !videoconvert || !videosink)
    {
        g_printerr("One of the elements wasn't created... Exiting\n");
        return -1;
    }

    // video
    g_object_set(G_OBJECT(videosink), "sync", FALSE, NULL);
    // g_object_set (G_OBJECT (videosink), "async-handling", TRUE, NULL);

    /* Set video Source */
    g_object_set(G_OBJECT(rtspsrc), "location", config_param.rtsp_stream_url, NULL);
    g_object_set(G_OBJECT(rtspsrc), "do-rtcp", TRUE, NULL);
    g_object_set(G_OBJECT(rtspsrc), "latency", 0, NULL);
    // choose rtsp version
    if (rtsp_ver == 2)
    {
        g_object_set(G_OBJECT(rtspsrc), "default-rtsp-version", GST_RTSP_VERSION_2_0, NULL);
    }
    // tls and auth related
    if (enable_secure)
    {

        g_object_set(G_OBJECT(rtspsrc), "user-id", config_param.rtsp_server_username, NULL);
        g_object_set(G_OBJECT(rtspsrc), "user-pw", config_param.rtsp_server_password, NULL);
        g_object_set(G_OBJECT(rtspsrc), "tls-validation-flags", G_TLS_CERTIFICATE_VALIDATE_ALL, NULL);
        // g_object_set (G_OBJECT (rtspsrc), "tls-validation-flags", G_TLS_CERTIFICATE_INSECURE, NULL);

        GTlsCertificate *cert = g_tls_certificate_new_from_files(config_param.rtsp_cert_pem, config_param.rtsp_cert_key, &error);
        if (cert == NULL)
        {
            g_printerr("failed to parse PEM: %s\n", error->message);
            return -2;
        }
        GTlsCertificate *ca_cert = g_tls_certificate_new_from_file(config_param.rtsp_ca_cert, &error);
        if (ca_cert == NULL)
        {
            g_printerr("failed to parse CA PEM: %s\n", error->message);
            return -3;
        }
        GTlsDatabase *database = g_tls_file_database_new(config_param.rtsp_ca_cert, &error);
        if (database == NULL)
        {
            g_printerr("failed to creat TlsDatabase: %s\n", error->message);
            return -4;
        }
        g_object_set(G_OBJECT(rtspsrc), "tls-database", database, NULL);

        if (cert_chain_verify)
        {
            ChainVerifyTlsInteraction *interaction = chain_verify_tls_interaction_new(cert, ca_cert, database);
            g_object_set(G_OBJECT(rtspsrc), "tls-interaction", interaction, NULL);
        }
        else
        {
            SimpleVerifyTlsInteraction *interaction = simple_verify_tls_interaction_new(cert, ca_cert);
            g_object_set(G_OBJECT(rtspsrc), "tls-interaction", interaction, NULL);
        }
    }
    /* Putting a Message handler */
    bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
    gst_bus_add_watch(bus, bus_call, loop);
    gst_object_unref(bus);

    // Add Elements to the Bin
    gst_bin_add_many(GST_BIN(pipeline), rtspsrc, rtph264depay, h264parse, videodecoder, videoqueue0, videoconvert, videosink, NULL);

    // Link confirmation
    if (!gst_element_link_many(rtph264depay, h264parse, videodecoder, NULL))
    {
        g_warning("Linking to decoder Fail...");
        return -5;
    }
    // Link confirmation
    if (!gst_element_link_many(videoqueue0, videoconvert, videosink, NULL))
    {
        g_warning("Linking to videosink Fail...");
        return -6;
    }

    // Dynamic Pad Creation
    if (!g_signal_connect(rtspsrc, "pad-added", G_CALLBACK(on_pad_added), rtph264depay))
    {
        g_warning("Linking rtspsrc with rtph264depay Fail...");
    }
    // Dynamic Pad Creation
    if (!g_signal_connect(videodecoder, "pad-added", G_CALLBACK(on_pad_added), videoqueue0))
    {
        g_warning("Linking h264 decoder with videoqueue Fail...");
    }

    /* Run the pipeline */
    g_print("Playing: %s\n", config_param.rtsp_stream_url);
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    g_main_loop_run(loop);

    /* Ending Playback */
    g_print("End of the Streaming... ending the playback\n");
    gst_element_set_state(pipeline, GST_STATE_NULL);

    /* Eliminating Pipeline */
    g_print("Eliminating Pipeline\n");
    gst_object_unref(GST_OBJECT(pipeline));

    return 0;
}
