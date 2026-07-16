/*
 * test_launch_v3.c
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

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>
#include <gst/rtsp-server/rtsp-server-object.h>
#include <gst/rtsp-server/rtsp-media.h>

#include "rtsp_config.h"

#define DUNUSED(x) ((void)(x))

#define DEFAULT_VIDEO_COUNT 1
#define DEFAULT_RTSP_PORT "8554"
#define ATTACH_URL "/test"
#define DEFAULT_DISABLE_RTCP FALSE
#define DEFAULT_ENABLE_SECURE FALSE
#define DEFAULT_VERIFY_CHAIN FALSE

static char *config_file = (char *)DEFAULT_SERVER_CONF_NAME;
static char *port = (char *)DEFAULT_RTSP_PORT;
static int count = DEFAULT_VIDEO_COUNT;
static gboolean disable_rtcp = DEFAULT_DISABLE_RTCP;
static gboolean enable_secure = DEFAULT_ENABLE_SECURE;
static gboolean cert_chain_verify = DEFAULT_VERIFY_CHAIN;

static rtsp_server_config config_param;

static GOptionEntry entries[] = {
    {"port", 'p', 0, G_OPTION_ARG_STRING, &port,
     "Port to listen on (default: " DEFAULT_RTSP_PORT ")", "PORT"},
    {"count", 'c', 0, G_OPTION_ARG_INT, &count,
     "Video count to send (default: 1)", "COUNT"},
    {"conf", 'f', 0, G_OPTION_ARG_STRING, &config_file,
     "Config file name, settings in file have higher priority (default: " DEFAULT_SERVER_CONF_NAME ")", "FILENAME"},
    {"disable-rtcp", '\0', 0, G_OPTION_ARG_NONE, &disable_rtcp,
     "Whether RTCP should be disabled (default false)", NULL},
    {"enable-secure", '\0', 0, G_OPTION_ARG_NONE, &enable_secure,
     "Whether security feature (include basic authentication, rtsp over tls and srtp) should be enabled (default false)", NULL},
    {"cert-chain-verify", '\0', 0, G_OPTION_ARG_NONE, &cert_chain_verify,
     "Whether verify the full certificate chain during TLS/SSL (default false)", NULL},
    {NULL}};

/* this timeout is periodically run to clean up the expired sessions from the
 * pool. This needs to be run explicitly currently but might be done
 * automatically as part of the mainloop. */
static gboolean
timeout(GstRTSPServer *server)
{
  GstRTSPSessionPool *pool;
  pool = gst_rtsp_server_get_session_pool(server);
  gst_rtsp_session_pool_cleanup(pool);
  g_object_unref(pool);
  return TRUE;
}

static gboolean
accept_tls_certificate_simple(GstRTSPAuth *auth,
                              GTlsConnection *conn,
                              GTlsCertificate *peer_cert,
                              GTlsCertificateFlags errors,
                              gpointer user_data)
{
  GTlsCertificate *ca_tls_cert = (GTlsCertificate *)user_data;

  DUNUSED(auth);
  DUNUSED(conn);

  g_print("Entered accept_tls_certificate_simple, GTlsCertificateFlags: %d\n", errors);
  guint verification = g_tls_certificate_verify(peer_cert, NULL, ca_tls_cert);
  g_print("Accept Certificate: %d\n", verification);
  // Return TRUE or FALSE here depending on the value of verification
  if ((errors & verification) == 0)
  {
    return TRUE;
  }
  return FALSE;
}

static gboolean
accept_tls_certificate_chain(GstRTSPAuth *auth,
                             GTlsConnection *conn,
                             GTlsCertificate *peer_cert,
                             GTlsCertificateFlags errors,
                             gpointer user_data)
{
  GError *error = NULL;
  GTlsCertificate *ca_tls_cert = (GTlsCertificate *)user_data;

  DUNUSED(auth);
  DUNUSED(ca_tls_cert);

  g_print("Entered accept_tls_certificate_chain, GTlsCertificateFlags: %d\n", errors);
  GTlsDatabase *database = g_tls_connection_get_database(G_TLS_CONNECTION(conn));
  if (database)
  {
    GSocketConnectable *peer_identity = NULL;
    guint validation_flags;
    g_debug("TLS peer certificate not accepted, checking user database...\n");
    // peer_identity = g_tls_client_connection_get_server_identity(G_TLS_CLIENT_CONNECTION(conn));
    validation_flags =
        g_tls_database_verify_chain(database, peer_cert,
                                    G_TLS_DATABASE_PURPOSE_AUTHENTICATE_CLIENT, peer_identity,
                                    g_tls_connection_get_interaction(conn), G_TLS_DATABASE_VERIFY_NONE,
                                    NULL, &error);
    g_print("Accept Certificate: %d\n", validation_flags);
    g_object_unref(database);
    if (error)
    {
      g_print("failure verifying certificate chain: %s", error->message);
      g_assert(validation_flags != 0);
      g_clear_error(&error);
      return FALSE;
    }
    if ((errors & validation_flags) == 0)
    {
      return TRUE;
    }
  }
  else
  {
    g_warning("g_tls_connection_get_database return null!\n");
  }

  g_warning("FIXME: accept_tls_certificate_chain should never go here! Ignore unknown errors and continue ...\n");
  return TRUE;
}

/* called when a stream has received an RTCP packet from the client */
static void on_ssrc_active(GObject *session, GObject *source, GstRTSPMedia *media)
{
  GstStructure *stats;

  DUNUSED(media);

  GST_INFO("source %p in session %p is active", source, session);
  g_object_get(source, "stats", &stats, NULL);
  if (stats)
  {
    gchar *sstr;
    sstr = gst_structure_to_string(stats);
    GST_INFO("structure: %s\n", sstr);
    g_free(sstr);
    gst_structure_free(stats);
  }
}

static void on_sender_ssrc_active(GObject *session, GObject *source, GstRTSPMedia *media)
{
  on_ssrc_active(session, source, media);
}

/* signal callback when the media is prepared for streaming. We can get the
 * session manager for each of the streams and connect to some signals. */
static void media_prepared_cb(GstRTSPMedia *media)
{
  guint i, n_streams;
  n_streams = gst_rtsp_media_n_streams(media);
  GST_INFO("media %p is prepared and has %u streams", media, n_streams);
  for (i = 0; i < n_streams; i++)
  {
    GstRTSPStream *stream;
    GObject *session;
    stream = gst_rtsp_media_get_stream(media, i);
    if (stream == NULL)
    {
      continue;
    }
    session = gst_rtsp_stream_get_rtpsession(stream);
    GST_INFO("watching session %p on stream %u", session, i);
    g_signal_connect(session, "on-ssrc-active", (GCallback)on_ssrc_active, media);
    g_signal_connect(session, "on-sender-ssrc-active", (GCallback)on_sender_ssrc_active, media);
  }
}

static void media_configure_cb(GstRTSPMediaFactory *factory, GstRTSPMedia *media)
{
  /* connect our prepared signal so that we can see when this media is
   * prepared for streaming */
  g_signal_connect(media, "prepared", (GCallback)media_prepared_cb, factory);
}

int main(int argc, char *argv[])
{
  GMainLoop *loop = NULL;
  GstRTSPServer *server = NULL;
  GstRTSPMountPoints *mounts = NULL;
  GstRTSPMediaFactory *factory = NULL;
  GOptionContext *optctx = NULL;
  GError *error = NULL;
  char temp_buffer[256] = {0};
  int index = 0;
  // for basic authentication
  GstRTSPAuth *auth;
  GstRTSPToken *token;
  gchar *basic;
  GstRTSPPermissions *permissions;
  // for tls
  GTlsCertificate *cert;
  GTlsCertificate *ca_cert;

  memset(&config_param, 0, sizeof(config_param));
  optctx = g_option_context_new("<launch line> - Test RTSP Server, Launch\n\n"
                                "Example: \"( videotestsrc ! openh264enc ! rtph264pay name=pay0 pt=96 )\"");
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

  if (!get_rtsp_server_config(&config_param, config_file))
  {
    g_printerr("Set config failed  ... Exiting\n");
    exit(-1);
  }
  if (!strlen(config_param.rtsp_server_port))
  {
    strncpy(config_param.rtsp_server_port, port, MAXBUF - 1);
  }
  if (!strlen(config_param.rtsp_server_mount_point))
  {
    strncpy(config_param.rtsp_server_mount_point, ATTACH_URL, MAXBUF - 1);
  }
  gst_init(&argc, &argv);
  loop = g_main_loop_new(NULL, FALSE);
  server = gst_rtsp_server_new();
  g_object_set(server, "service", config_param.rtsp_server_port, NULL);

  if (enable_secure)
  {
    /* make a new authentication manager. it can be added to control access to all
     * the factories on the server or on individual factories. */
    auth = gst_rtsp_auth_new();
    g_print("Inside RTSP server's TLS portion\n");

    cert = g_tls_certificate_new_from_files(config_param.rtsp_cert_pem, config_param.rtsp_cert_key, &error);
    if (cert == NULL)
    {
      g_printerr("failed to parse PEM: %s\n", error->message);
      return -2;
    }

    ca_cert = g_tls_certificate_new_from_file(config_param.rtsp_ca_cert, &error);
    if (ca_cert == NULL)
    {
      g_printerr("failed to parse CA PEM: %s\n", error->message);
      return -3;
    }

    gst_rtsp_auth_set_tls_authentication_mode(auth, G_TLS_AUTHENTICATION_REQUIRED);
    // gst_rtsp_auth_set_tls_authentication_mode(auth, G_TLS_AUTHENTICATION_REQUESTED);

    gst_rtsp_auth_set_tls_certificate(auth, cert);
    if (cert_chain_verify)
    {
      GTlsDatabase *database = g_tls_file_database_new(config_param.rtsp_ca_cert, &error);
      if (ca_cert == NULL)
      {
        g_printerr("failed to creat TlsDatabase: %s\n", error->message);
        return -4;
      }
      gst_rtsp_auth_set_tls_database(auth, database);
      g_signal_connect(auth, "accept-certificate", G_CALLBACK(accept_tls_certificate_chain), ca_cert);
    }
    else
    {
      g_signal_connect(auth, "accept-certificate", G_CALLBACK(accept_tls_certificate_simple), ca_cert);
    }
    /* make user token */
    token =
        gst_rtsp_token_new(GST_RTSP_TOKEN_MEDIA_FACTORY_ROLE, G_TYPE_STRING,
                           config_param.rtsp_server_username, NULL);
    basic = gst_rtsp_auth_make_basic(config_param.rtsp_server_username, config_param.rtsp_server_password);

    gst_rtsp_auth_add_basic(auth, basic, token);
    g_free(basic);
    gst_rtsp_token_unref(token);
    /* configure in the server */
    gst_rtsp_server_set_auth(server, auth);
  }

  /* get the mount points for this server, every server has a default object
   * that be used to map uri mount points to media factories */
  mounts = gst_rtsp_server_get_mount_points(server);
  for (index = 0; index < count; index++)
  {
    /* make a media factory for a test stream. The default media factory can use
     * gst-launch syntax to create pipelines.
     * any launch line works as long as it contains elements named pay%d. Each
     * element with pay%d names will be a stream */
    factory = gst_rtsp_media_factory_new();
    if (argv[index + 1])
    {
      gst_rtsp_media_factory_set_launch(factory, argv[index + 1]);
    }
    else
    {
      g_printerr("Error parsing options: %d\n", index + 1);
      return -5;
    }
    g_signal_connect(factory, "media-configure", (GCallback)media_configure_cb, factory);
    gst_rtsp_media_factory_set_shared (factory, TRUE);
    gst_rtsp_media_factory_set_enable_rtcp(factory, !disable_rtcp);

    if (enable_secure)
    {
      /* add permissions for the user media role */
      permissions = gst_rtsp_permissions_new();
      gst_rtsp_permissions_add_role(permissions, config_param.rtsp_server_username,
                                    GST_RTSP_PERM_MEDIA_FACTORY_ACCESS, G_TYPE_BOOLEAN, TRUE,
                                    GST_RTSP_PERM_MEDIA_FACTORY_CONSTRUCT, G_TYPE_BOOLEAN, TRUE, NULL);
      gst_rtsp_media_factory_set_permissions(factory, permissions);
      gst_rtsp_permissions_unref(permissions);
      gst_rtsp_media_factory_set_profiles(factory, GST_RTSP_PROFILE_SAVP | GST_RTSP_PROFILE_SAVPF);
    }

    snprintf(temp_buffer, sizeof(temp_buffer) - 1, "%s%d", config_param.rtsp_server_mount_point, index);
    /* attach the test factory to the /test url */
    gst_rtsp_mount_points_add_factory(mounts, temp_buffer, factory);
    if (!enable_secure)
    {
      g_print("stream ready at rtsp://127.0.0.1:%s%s\n", config_param.rtsp_server_port, temp_buffer);
    }
    else
    {
      g_print("stream ready at rtsps://127.0.0.1:%s%s\n", config_param.rtsp_server_port, temp_buffer);
    }
  }
  /* don't need the ref to the mapper anymore */
  g_object_unref(mounts);
  /* attach the server to the default maincontext */
  gst_rtsp_server_attach(server, NULL);
  /* add a timeout for the session cleanup */
  g_timeout_add_seconds(2, (GSourceFunc)timeout, server);
  g_main_loop_run(loop);

  return 0;
}
