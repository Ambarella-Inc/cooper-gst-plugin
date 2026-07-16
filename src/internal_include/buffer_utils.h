/*
 * buffer_utils.h
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

#ifndef __BUFFER_UTILS_H__
#define __BUFFER_UTILS_H__

#include <gst/gstbuffer.h>

GstBuffer * alloc_gst_buffer_amba_direct_mem(
  unsigned char * data, unsigned int size,
  guint alloc_and_copy, void * shared_stream_info);

GstBuffer * alloc_gst_buffer_amba_direct_mem_nv12(
  unsigned int width, unsigned int height,
  unsigned int stride_y, unsigned int stride_uv,
  unsigned char * p_y, unsigned char * p_uv,
  guint alloc_and_copy,
  void * user_data);

#endif

