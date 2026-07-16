/*
 * chain_verify_tls_interaction.c
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

// Modified from https://github.com/enthusiasticgeek/gstreamer-rtsp-ssl-example
// Copyright (c) 2017 Mandar Joshi
// Copyright (c) 2017 Pratik M Tambe <enthusiasticgeek@gmail.com>

#include "chain_verify_tls_interaction.h"

#define DUNUSED(x) ((void)(x))

// This struct should contain instance variable for the subclass
struct _ChainVerifyTlsInteraction
{
    GTlsInteraction parent_instance;
    GTlsCertificate *ca_cert, *cert_key;
    GTlsDatabase *database;
};

G_DEFINE_TYPE(ChainVerifyTlsInteraction, chain_verify_tls_interaction, G_TYPE_TLS_INTERACTION)

// This function handles all initialisation
static void chain_verify_tls_interaction_init(ChainVerifyTlsInteraction *tls_interaction)
{
    DUNUSED(tls_interaction);
}

gboolean accept_tls_certificate_chain(GTlsConnection *conn, GTlsCertificate *peer_cert,
                                      GTlsCertificateFlags errors, ChainVerifyTlsInteraction *user_data)
{
    GError *error = NULL;
    gboolean accept = FALSE;
    GTlsCertificate *ca_tls_cert = (GTlsCertificate *)user_data->ca_cert;

    DUNUSED(accept);
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
                                        G_TLS_DATABASE_PURPOSE_AUTHENTICATE_SERVER, peer_identity,
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
    g_warning("FIXME: accept_tls_certificate_chain should never go here! Ignore unknown errors and continue ...\n");
    return TRUE;
#if 0
    //Trying to verify peer
        if (error)
          goto verify_error;

        validation_flags = g_tls_client_connection_get_validation_flags(G_TLS_CLIENT_CONNECTION
            (conn));
        g_print("validation_flags value %d\n",validation_flags);

        accept = ((errors & validation_flags) == 0);
        if (accept)
          g_print ("Peer certificate accepted\n");
        else
          g_print ("Peer certificate not accepted (errors: 0x%08X)\n", errors);
      }

      return accept;

    verify_error:
      {
        g_print ("An error occurred while verifying the peer certificate: %s\n",
            error->message);
        g_clear_error (&error);
        return FALSE;
      }
#endif
}

GTlsInteractionResult chain_verify_request_certificate(GTlsInteraction *interaction,
                                                       GTlsConnection *connection,
                                                       GTlsCertificateRequestFlags flags,
                                                       GCancellable *cancellable,
                                                       GError **error)
{
    ChainVerifyTlsInteraction *stls = (ChainVerifyTlsInteraction *)interaction;

    DUNUSED(flags);
    DUNUSED(cancellable);
    DUNUSED(error);

    g_debug("ChainVerify Request Certificate");
    g_signal_connect(connection, "accept-certificate", G_CALLBACK(accept_tls_certificate_chain), stls);
    g_tls_connection_set_certificate(connection, stls->cert_key);
    return G_TLS_INTERACTION_HANDLED;
}

// Virt funcs overrides, properties, signal defs here
static void chain_verify_tls_interaction_class_init(ChainVerifyTlsInteractionClass *class)
{
    GTlsInteractionClass *object_class = G_TLS_INTERACTION_CLASS(class);
    object_class->request_certificate = chain_verify_request_certificate;
}

ChainVerifyTlsInteraction *chain_verify_tls_interaction_new(GTlsCertificate *cert_key, GTlsCertificate *ca_cert, GTlsDatabase *database)
{
    ChainVerifyTlsInteraction *interaction = g_object_new(CHAIN_VERIFY_TLS_INTERACTION_TYPE, NULL);
    interaction->cert_key = cert_key;
    interaction->ca_cert = ca_cert;
    interaction->database = database;
    return interaction;
}
