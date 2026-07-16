/*
 * gst_amshmem_phys_mmap.h
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
 * mmap NV12 via CPU physical (/dev/mem), Linux only
 */

#ifndef __GST_AMSHMEM_PHYS_MMAP_H__
#define __GST_AMSHMEM_PHYS_MMAP_H__

#include <glib.h>
#include <gst/gst.h>

G_BEGIN_DECLS

/**
 * gst_amshmem_phys_mmap_wrap_nv12:
 * @phys_y: CPU physical address of NV12 Y plane start (same PA space on receiver).
 * @width: picture width
 * @height: picture height
 * @pitch: Y plane row bytes (stride)
 * @out_buf: (out) new #GstBuffer with one #GstMemory and #GstVideoMeta NV12
 *
 * Maps a page-aligned span covering the full NV12 frame via /dev/mem,
 * wraps it in a #GstBuffer; unref runs munmap.
 *
 * Returns: %TRUE on success
 */
gboolean gst_amshmem_phys_mmap_wrap_nv12 (guint64 phys_y, guint32 width,
    guint32 height, guint32 pitch, GstBuffer ** out_buf);

G_END_DECLS

#endif /* __GST_AMSHMEM_PHYS_MMAP_H__ */
