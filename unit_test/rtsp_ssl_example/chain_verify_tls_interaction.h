/*
 * chain_verify_tls_interaction.h
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

#ifndef _chain_verify_tls_interaction_h_
#define _chain_verify_tls_interaction_h_
#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

G_DECLARE_FINAL_TYPE(ChainVerifyTlsInteraction, chain_verify_tls_interaction, CHAIN_VERIFY_TLS, INTERACTION, GTlsInteraction)

#define CHAIN_VERIFY_TLS_INTERACTION_TYPE (chain_verify_tls_interaction_get_type())

ChainVerifyTlsInteraction *chain_verify_tls_interaction_new(GTlsCertificate *, GTlsCertificate *, GTlsDatabase *);

G_END_DECLS

#endif
