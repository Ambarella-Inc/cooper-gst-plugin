/*
 * buffer_utils.c
 *
 * History:
 *    6/13/2022 - [Zhi He] created file
 *
 * Copyright (C) 2022 Ambarella International LP
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

#include "stdlib.h"
#include "stdio.h"
#include "string.h"

#include <gst/gst.h>

#include <gst/video/video-format.h>
#include <gst/video/video-frame.h>

#include "debug_log.h"

#include "internal.h"

#include "amba_direct_mem.h"

#include "element_common.h"

GstBuffer * alloc_gst_buffer_amba_direct_mem(
  unsigned char * data, unsigned int size,
  guint alloc_and_copy,
  void * shared_stream_info)
{
  GstBuffer *buf;
  GstMemory *mem;

  buf = gst_buffer_new ();
  if (!buf) {
    DPRINT_ERROR ("no memory");
    return NULL;
  }

  mem = amba_direct_mem_alloc_user_data_0 (data, size,
    alloc_and_copy, shared_stream_info);
  if (!mem) {
    if (buf) {
      gst_buffer_unref (buf);
    }
    DPRINT_ERROR ("no memory");
    return NULL;
  }

  gst_buffer_append_memory (buf, mem);

  return buf;
}


GstBuffer * alloc_gst_buffer_amba_direct_mem_nv12(
  unsigned int width, unsigned int height,
  unsigned int stride_y, unsigned int stride_uv,
  unsigned char * p_y, unsigned char * p_uv,
  guint alloc_and_copy,
  void * user_data)
{
  gsize offset[GST_VIDEO_MAX_PLANES] = {0};
  gint stride[GST_VIDEO_MAX_PLANES] = {0};

  GstBuffer * buf;
  GstMemory * mem_y;
  GstMemory * mem_uv;

  buf = gst_buffer_new ();
  if (!buf) {
    DPRINT_ERROR ("no memory");
    return NULL;
  }

  mem_y = amba_direct_mem_alloc_user_data_0 (p_y, stride_y * height,
    alloc_and_copy, user_data);
  mem_uv = amba_direct_mem_alloc (p_uv, stride_uv * (height + 1) / 2,
    alloc_and_copy);
  if (!mem_y) {
    if (buf) {
      gst_buffer_unref (buf);
    }
    DPRINT_ERROR ("no memory");
    return NULL;
  }

  gst_buffer_append_memory (buf, mem_y);
  gst_buffer_append_memory (buf, mem_uv);

  stride[0] = stride_y;
  stride[1] = stride_uv;

  offset[0] = 0;
  offset[1] = stride_y * height;

  gst_buffer_add_video_meta_full (buf, GST_VIDEO_FRAME_FLAG_NONE,
      GST_VIDEO_FORMAT_NV12, width, height, 2, offset, stride);

  return buf;
}

