/*
 * gst_amshmem_phys_mmap.c
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
 *
 * ## Example pipelines
 * |[
 * gst-launch-1.0 -e amshmem_element_a num-buffers=20 ! queue ! amshmem_sink implem-method=cyclonedds
 * ]|
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gst_amshmem_phys_mmap.h"

#include <gst/video/video.h>
#include <string.h>

#if defined(__linux__)
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

typedef struct
{
  guint8 *map_base;
  gsize map_len;
} GstAmShMemPhysMmapUser;

static void
gst_amshmem_phys_munmap_notify (gpointer data)
{
  GstAmShMemPhysMmapUser *u = data;

  if (u->map_base && (gpointer) u->map_base != (gpointer) MAP_FAILED
      && u->map_len > 0)
    munmap (u->map_base, u->map_len);
  g_free (u);
}

gboolean
gst_amshmem_phys_mmap_wrap_nv12 (guint64 phys_y, guint32 width, guint32 height,
    guint32 pitch, GstBuffer ** out_buf)
{
#if !defined(__linux__)
  (void) phys_y;
  (void) width;
  (void) height;
  (void) pitch;
  *out_buf = NULL;
  return FALSE;
#else
  const gsize page_size = 4096;
  guint32 al_h;
  gsize nv12_sz;
  gsize y_off_in_map;
  gsize map_len;
  guint64 map_phys;
  gint mem_fd;
  guint8 *map_base;
  GstBuffer *buf;
  GstMemory *mem;
  GstAmShMemPhysMmapUser *u;
  gsize off[GST_VIDEO_MAX_PLANES];
  gint str[GST_VIDEO_MAX_PLANES];

  g_return_val_if_fail (out_buf != NULL, FALSE);
  *out_buf = NULL;

  if (phys_y == 0 || width == 0 || height == 0 || pitch == 0)
    return FALSE;

  al_h = (height + 15u) & ~15u;
  nv12_sz = (gsize) pitch * (gsize) al_h * 3 / 2;

  map_phys = phys_y & ~(guint64) (page_size - 1);
  y_off_in_map = (gsize) (phys_y - map_phys);
  map_len = y_off_in_map + nv12_sz;
  map_len = (map_len + page_size - 1) & ~(page_size - 1);

  mem_fd = open ("/dev/mem", O_RDWR | O_SYNC);
  if (mem_fd < 0)
    return FALSE;

  map_base = (guint8 *) mmap (NULL, map_len, PROT_READ | PROT_WRITE, MAP_SHARED,
      mem_fd, (off_t) map_phys);
  close (mem_fd);

  if (map_base == NULL || (gpointer) map_base == (gpointer) MAP_FAILED)
    return FALSE;

  buf = gst_buffer_new ();
  if (!buf) {
    munmap (map_base, map_len);
    return FALSE;
  }

  u = g_new0 (GstAmShMemPhysMmapUser, 1);
  u->map_base = map_base;
  u->map_len = map_len;

  mem = gst_memory_new_wrapped ((GstMemoryFlags) 0,
      map_base + y_off_in_map, nv12_sz, 0, nv12_sz, u, gst_amshmem_phys_munmap_notify);
  gst_buffer_append_memory (buf, mem);

  memset (off, 0, sizeof (off));
  memset (str, 0, sizeof (str));
  off[0] = 0;
  off[1] = (gsize) pitch * (gsize) al_h;
  str[0] = (gint) pitch;
  str[1] = (gint) pitch;
  gst_buffer_add_video_meta_full (buf, GST_VIDEO_FRAME_FLAG_NONE,
      GST_VIDEO_FORMAT_NV12, width, height, 2, off, str);

  *out_buf = buf;
  return TRUE;
#endif
}
