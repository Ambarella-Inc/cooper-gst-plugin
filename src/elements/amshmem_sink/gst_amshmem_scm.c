/*
 * gst_amshmem_scm.c
 *
 * History:
 *    3/11/2026 - [Da-Shun Pei] created file
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
 * SECTION: element-amshmem_sink
 * @title: amshmem_sink
 *
 * Unix socket + SCM_RIGHTS (Linux)
 */


#include "gst_amshmem_scm.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#if defined(__linux__)
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#endif

struct _GstAmShMemScmServer {
  gchar *path;
  gint listen_fd;
  GThread *thread;
  volatile gboolean stop;
  GMutex lock;
  GCond cond;
  gint pool_fd;
  guint64 pool_bytes;
  gboolean pool_set;
};

#if defined(__linux__)

#define CMSG_BUF_LEN (CMSG_SPACE (sizeof (int)))

static gboolean
set_cloexec (gint fd)
{
  gint fl = fcntl (fd, F_GETFD);

  if (fl < 0)
    return FALSE;
  return fcntl (fd, F_SETFD, fl | FD_CLOEXEC) >= 0;
}

static gpointer
scm_server_thread (gpointer data)
{
  GstAmShMemScmServer *srv = data;

  while (!srv->stop) {
    struct pollfd pfd;
    gint cfd;
    gint pfd_send = -1;
    guint64 sz_send = 0;
    struct msghdr msg = { 0 };
    struct iovec iov;
    struct cmsghdr *cmsg;
    gchar cmsg_buf[CMSG_BUF_LEN];
    char dummy = 0;
    guint64 sz_le;
    ssize_t wr;

    pfd.fd = srv->listen_fd;
    pfd.events = POLLIN;
    if (poll (&pfd, 1, 500) <= 0)
      continue;

    cfd = accept (srv->listen_fd, NULL, NULL);
    if (cfd < 0) {
      if (errno == EINTR)
        continue;
      continue;
    }
    set_cloexec (cfd);

    g_mutex_lock (&srv->lock);
    while (!srv->stop && (!srv->pool_set || srv->pool_fd < 0))
      g_cond_wait (&srv->cond, &srv->lock);
    if (!srv->stop && srv->pool_set && srv->pool_fd >= 0) {
      pfd_send = srv->pool_fd;
      sz_send = srv->pool_bytes;
    }
    g_mutex_unlock (&srv->lock);

    if (srv->stop || pfd_send < 0) {
      close (cfd);
      continue;
    }

    memset (&msg, 0, sizeof (msg));
    iov.iov_base = &dummy;
    iov.iov_len = 1;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cmsg_buf;
    msg.msg_controllen = sizeof (cmsg_buf);
    cmsg = CMSG_FIRSTHDR (&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN (sizeof (int));
    memcpy (CMSG_DATA (cmsg), &pfd_send, sizeof (int));

    if (sendmsg (cfd, &msg, 0) < 0) {
      close (cfd);
      continue;
    }

    sz_le = GUINT64_TO_LE (sz_send);
    wr = write (cfd, &sz_le, sizeof (sz_le));
    (void) wr;
    close (cfd);
  }

  return NULL;
}

#endif /* __linux__ */

GstAmShMemScmServer *
gst_amshmem_scm_server_new (const gchar * path)
{
  GstAmShMemScmServer *srv;

  srv = g_new0 (GstAmShMemScmServer, 1);
  srv->path = g_strdup (path);
  srv->listen_fd = -1;
  srv->pool_fd = -1;
  g_mutex_init (&srv->lock);
  g_cond_init (&srv->cond);
  return srv;
}

void
gst_amshmem_scm_server_free (GstAmShMemScmServer * srv)
{
  if (!srv)
    return;
  gst_amshmem_scm_server_stop (srv);
  g_free (srv->path);
  g_mutex_clear (&srv->lock);
  g_cond_clear (&srv->cond);
  g_free (srv);
}

gboolean
gst_amshmem_scm_server_start (GstAmShMemScmServer * srv)
{
#if !defined(__linux__)
  (void) srv;
  return FALSE;
#else
  struct sockaddr_un addr;

  if (!srv || !srv->path || !srv->path[0])
    return FALSE;

  gst_amshmem_scm_server_stop (srv);

  srv->listen_fd = socket (AF_UNIX, SOCK_STREAM, 0);
  if (srv->listen_fd < 0)
    return FALSE;
  set_cloexec (srv->listen_fd);

  unlink (srv->path);
  memset (&addr, 0, sizeof (addr));
  addr.sun_family = AF_UNIX;
  g_strlcpy (addr.sun_path, srv->path, sizeof (addr.sun_path));

  if (bind (srv->listen_fd, (struct sockaddr *) &addr, sizeof (addr)) < 0) {
    close (srv->listen_fd);
    srv->listen_fd = -1;
    return FALSE;
  }
  if (listen (srv->listen_fd, 4) < 0) {
    close (srv->listen_fd);
    srv->listen_fd = -1;
    return FALSE;
  }

  srv->stop = FALSE;
  srv->thread = g_thread_new ("amshmem-scm-srv", scm_server_thread, srv);
  return srv->thread != NULL;
#endif
}

void
gst_amshmem_scm_server_stop (GstAmShMemScmServer * srv)
{
#if defined(__linux__)
  if (!srv)
    return;
  srv->stop = TRUE;
  if (srv->listen_fd >= 0) {
    shutdown (srv->listen_fd, SHUT_RDWR);
    close (srv->listen_fd);
    srv->listen_fd = -1;
  }
  if (srv->thread) {
    g_thread_join (srv->thread);
    srv->thread = NULL;
  }
  if (srv->path && srv->path[0])
    unlink (srv->path);
#endif
}

void
gst_amshmem_scm_server_set_pool (GstAmShMemScmServer * srv, gint pool_fd,
    guint64 pool_bytes)
{
#if defined(__linux__)
  if (!srv)
    return;
  g_mutex_lock (&srv->lock);
  srv->pool_fd = pool_fd;
  srv->pool_bytes = pool_bytes;
  srv->pool_set = TRUE;
  g_cond_broadcast (&srv->cond);
  g_mutex_unlock (&srv->lock);
#else
  (void) srv;
  (void) pool_fd;
  (void) pool_bytes;
#endif
}

gboolean
gst_amshmem_scm_client_recv_pool (const gchar * path, gint * pool_fd_out,
    guint64 * pool_size_out)
{
#if !defined(__linux__)
  (void) path;
  (void) pool_fd_out;
  (void) pool_size_out;
  return FALSE;
#else
  struct sockaddr_un addr;
  gint fd;
  struct msghdr msg = { 0 };
  struct iovec iov;
  gchar cmsg_buf[CMSG_BUF_LEN];
  gchar dummy;
  ssize_t nr;
  struct cmsghdr *cmsg;
  gint recv_fd = -1;
  guint64 sz_le;

  if (!path || !path[0] || !pool_fd_out || !pool_size_out)
    return FALSE;

  *pool_fd_out = -1;
  *pool_size_out = 0;

  fd = socket (AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0)
    return FALSE;
  set_cloexec (fd);

  memset (&addr, 0, sizeof (addr));
  addr.sun_family = AF_UNIX;
  g_strlcpy (addr.sun_path, path, sizeof (addr.sun_path));

  if (connect (fd, (struct sockaddr *) &addr, sizeof (addr)) < 0) {
    close (fd);
    return FALSE;
  }

  iov.iov_base = &dummy;
  iov.iov_len = 1;
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = cmsg_buf;
  msg.msg_controllen = sizeof (cmsg_buf);

  nr = recvmsg (fd, &msg, 0);
  if (nr < 0) {
    close (fd);
    return FALSE;
  }

  for (cmsg = CMSG_FIRSTHDR (&msg); cmsg != NULL;
      cmsg = CMSG_NXTHDR (&msg, cmsg)) {
    if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
      memcpy (&recv_fd, CMSG_DATA (cmsg), sizeof (int));
      break;
    }
  }

  if (recv_fd < 0) {
    close (fd);
    return FALSE;
  }
  set_cloexec (recv_fd);

  if (read (fd, &sz_le, sizeof (sz_le)) != (ssize_t) sizeof (sz_le)) {
    close (recv_fd);
    close (fd);
    return FALSE;
  }

  close (fd);
  *pool_fd_out = recv_fd;
  *pool_size_out = GUINT64_FROM_LE (sz_le);
  return TRUE;
#endif
}
