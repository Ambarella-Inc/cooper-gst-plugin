/*
 * gst_amshmem_scm.h
 *
 * History:
 *    4/11/2026 - [Da-Shun Pei] created file
 *
 * Copyright (C) 2025 Ambarella International LP
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
 * SECTION: element-amshmem_types
 * @title: amshmem_types
 *
 * Unix socket + SCM_RIGHTS pool fd handshake (Linux)
 */


#ifndef __GST_AMSHMEM_SCM_H__
#define __GST_AMSHMEM_SCM_H__

#include <glib.h>

G_BEGIN_DECLS

typedef struct _GstAmShMemScmServer GstAmShMemScmServer;

GstAmShMemScmServer *gst_amshmem_scm_server_new (const gchar * path);

void gst_amshmem_scm_server_free (GstAmShMemScmServer * srv);

gboolean gst_amshmem_scm_server_start (GstAmShMemScmServer * srv);

void gst_amshmem_scm_server_stop (GstAmShMemScmServer * srv);

/** Called when pool fd and total mmap size are known (e.g. first NV12 buffer). */
void gst_amshmem_scm_server_set_pool (GstAmShMemScmServer * srv, gint pool_fd,
    guint64 pool_bytes);

/**
 * Client: connect, receive duplicated pool fd + 8-byte little-endian pool size.
 * @pool_fd_out: set to received fd, or -1 on failure
 */
gboolean gst_amshmem_scm_client_recv_pool (const gchar * path, gint * pool_fd_out,
    guint64 * pool_size_out);

G_END_DECLS

#endif /* __GST_AMSHMEM_SCM_H__ */
