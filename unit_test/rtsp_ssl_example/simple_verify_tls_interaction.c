/*
 * simple_verify_tls_interaction.c
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

// Modified from https://github.com/mndar/tlsinteraction

#include "simple_verify_tls_interaction.h"

#define DUNUSED(x) ((void)(x))

// This struct should contain instance variable for the subclass
struct _SimpleVerifyTlsInteraction
{
  GTlsInteraction parent_instance;
  GTlsCertificate *ca_cert, *client_cert_key;
};

G_DEFINE_TYPE(SimpleVerifyTlsInteraction, simple_verify_tls_interaction, G_TYPE_TLS_INTERACTION)

// This function handles all initialisation
static void simple_verify_tls_interaction_init(SimpleVerifyTlsInteraction *tls_interaction)
{
  DUNUSED(tls_interaction);
}

gboolean accept_tls_certificate_simple(GTlsConnection *conn, GTlsCertificate *peer_cert,
                                       GTlsCertificateFlags errors, gpointer user_data)
{
  GTlsCertificate *ca_tls_cert = (GTlsCertificate *)user_data;

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

GTlsInteractionResult simple_verify_request_certificate(GTlsInteraction *interaction,
                                                        GTlsConnection *connection,
                                                        GTlsCertificateRequestFlags flags,
                                                        GCancellable *cancellable,
                                                        GError **error)
{
  SimpleVerifyTlsInteraction *stls = (SimpleVerifyTlsInteraction *)interaction;

  DUNUSED(flags);
  DUNUSED(cancellable);
  DUNUSED(error);

  g_debug("SimpleVerify Request Certificate");
  g_signal_connect(connection, "accept-certificate", G_CALLBACK(accept_tls_certificate_simple), stls->ca_cert);
  g_tls_connection_set_certificate(connection, stls->client_cert_key);
  return G_TLS_INTERACTION_HANDLED;
}

// Virt funcs overrides, properties, signal defs here
static void simple_verify_tls_interaction_class_init(SimpleVerifyTlsInteractionClass *class)
{
  GTlsInteractionClass *object_class = G_TLS_INTERACTION_CLASS(class);
  object_class->request_certificate = simple_verify_request_certificate;
}

SimpleVerifyTlsInteraction *simple_verify_tls_interaction_new(GTlsCertificate *client_cert_key, GTlsCertificate *ca_cert)
{
  SimpleVerifyTlsInteraction *interaction = g_object_new(SIMPLE_VERIFY_TLS_INTERACTION, NULL);
  interaction->client_cert_key = client_cert_key;
  interaction->ca_cert = ca_cert;
  return interaction;
}
