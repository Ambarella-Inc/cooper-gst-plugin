/*
 * rtsp_config.h
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

#ifndef _rtsp_config_h_
#define _rtsp_config_h_

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <glib.h>

#define DEFAULT_SERVER_CONF_NAME "/usr/share/ambarella/gst_rtsp_ssl/config/rtsp_server.conf"
#define DEFAULT_CLIENT_CONF_NAME "/usr/share/ambarella/gst_rtsp_ssl/config/rtsp_client.conf"
#define MAXBUF 128
#define DELIM "="

typedef struct
{
    char rtsp_server_port[MAXBUF];
    char rtsp_server_mount_point[MAXBUF];
    char rtsp_server_username[MAXBUF];
    char rtsp_server_password[MAXBUF];
    char rtsp_ca_cert[MAXBUF];
    char rtsp_cert_pem[MAXBUF];
    char rtsp_cert_key[MAXBUF];

} rtsp_server_config;

typedef struct
{
    char rtsp_stream_url[MAXBUF];
    char rtsp_server_username[MAXBUF];
    char rtsp_server_password[MAXBUF];
    char rtsp_ca_cert[MAXBUF];
    char rtsp_cert_pem[MAXBUF];
    char rtsp_cert_key[MAXBUF];

} rtsp_client_config;

bool get_rtsp_server_config(rtsp_server_config *config, char *filename);

bool get_rtsp_client_config(rtsp_client_config *config, char *filename);

#endif
