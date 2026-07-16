/*
 * rtsp_config.c
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

#include "rtsp_config.h"

bool get_rtsp_server_config(rtsp_server_config *config, char *filename)
{
    g_assert(config != NULL);
    FILE *file = NULL;
    if (!filename)
    {
        g_debug("No config filename provided, use default ...\n");
        file = fopen(DEFAULT_SERVER_CONF_NAME, "r");
    }
    else
    {
        file = fopen(filename, "r");
    }
    if (!file)
    {
        g_printerr("Open config file failed, please check !\n");
        return false;
    }
    else
    {
        char line[2 * MAXBUF] = {0};
        char *key, *value;
        while (fgets(line, sizeof(line), file))
        {
            if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
            {
                continue;
            }
            key = strtok(line, DELIM);
            value = strtok(NULL, "\n");
            if (key && value)
            {
                g_debug("%s: %s\n", key, value);
                if (!strcmp("RTSP_SERVER_PORT", key))
                {
                    strncpy(config->rtsp_server_port, value, MAXBUF - 1);
                }
                else if (!strcmp("RTSP_SERVER_MOUNT_POINT", key))
                {
                    strncpy(config->rtsp_server_mount_point, value, MAXBUF - 1);
                }
                else if (!strcmp("RTSP_USERNAME", key))
                {
                    strncpy(config->rtsp_server_username, value, MAXBUF - 1);
                }
                else if (!strcmp("RTSP_PASSWORD", key))
                {
                    strncpy(config->rtsp_server_password, value, MAXBUF - 1);
                }
                else if (!strcmp("RTSP_CA_CERT_PEM", key))
                {
                    strncpy(config->rtsp_ca_cert, value, MAXBUF - 1);
                }
                else if (!strcmp("RTSP_CERT_PEM", key))
                {
                    strncpy(config->rtsp_cert_pem, value, MAXBUF - 1);
                }
                else if (!strcmp("RTSP_CERT_KEY", key))
                {
                    strncpy(config->rtsp_cert_key, value, MAXBUF - 1);
                }
                else
                {
                    g_warning("unknown config option: %s\n", key);
                }
            }
            // memset(line,0,sizeof(line));
        }
    }
    fclose(file);
    return true;
}

bool get_rtsp_client_config(rtsp_client_config *config, char *filename)
{
    g_assert(config != NULL);
    FILE *file = NULL;
    if (!filename)
    {
        g_debug("No config filename provided, use default ...\n");
        file = fopen(DEFAULT_CLIENT_CONF_NAME, "r");
    }
    else
    {
        file = fopen(filename, "r");
    }
    if (!file)
    {
        g_printerr("Open config file failed, please check !\n");
        return false;
    }
    else
    {
        char line[2 * MAXBUF] = {0};
        char *key, *value;
        while (fgets(line, sizeof(line), file))
        {
            if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
            {
                continue;
            }
            key = strtok(line, DELIM);
            value = strtok(NULL, "\n");
            if (key && value)
            {
                g_debug("%s: %s\n", key, value);
                if (!strcmp("RTSP_STREAM_URL", key))
                {
                    strncpy(config->rtsp_stream_url, value, MAXBUF - 1);
                }
                else if (!strcmp("RTSP_USERNAME", key))
                {
                    strncpy(config->rtsp_server_username, value, MAXBUF - 1);
                }
                else if (!strcmp("RTSP_PASSWORD", key))
                {
                    strncpy(config->rtsp_server_password, value, MAXBUF - 1);
                }
                else if (!strcmp("RTSP_CA_CERT_PEM", key))
                {
                    strncpy(config->rtsp_ca_cert, value, MAXBUF - 1);
                }
                else if (!strcmp("RTSP_CERT_PEM", key))
                {
                    strncpy(config->rtsp_cert_pem, value, MAXBUF - 1);
                }
                else if (!strcmp("RTSP_CERT_KEY", key))
                {
                    strncpy(config->rtsp_cert_key, value, MAXBUF - 1);
                }
                else
                {
                    g_warning("unknown config option: %s\n", key);
                }
            }
            // memset(line,0,sizeof(line));
        }
    }
    fclose(file);
    return true;
}