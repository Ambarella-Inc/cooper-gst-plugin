/*
 * gstamshmemsrc.h
 *
 * History:
 *    3/30/2026 - [Da-Shun Pei] created file
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
 * SECTION: element-amshmem_src_buffer_pool
 * @title: amshmem_src_buffer_pool
 *
 * A negotiated GstBufferPool for amshmem_src.
 * When downstream returns buffers to the pool, release_buffer publishes FreeFrame_Msg on DDS.
 */


#ifndef __GST_AMSHMEM_SRC_BUFFER_POOL_H__
#define __GST_AMSHMEM_SRC_BUFFER_POOL_H__

#include <gst/gst.h>

#include "dds_msgs/AmShMem_Msg.h"

G_BEGIN_DECLS

typedef struct _GstAmShMemSrc GstAmShMemSrc;

#define GST_TYPE_AMSHMEM_SRC_BUFFER_POOL (gst_amshmem_src_buffer_pool_get_type ())
#define GST_AMSHMEM_SRC_BUFFER_POOL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_AMSHMEM_SRC_BUFFER_POOL, GstAmShMemSrcBufferPool))
#define GST_IS_AMSHMEM_SRC_BUFFER_POOL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_AMSHMEM_SRC_BUFFER_POOL))

typedef struct _GstAmShMemSrcBufferPool GstAmShMemSrcBufferPool;
typedef struct _GstAmShMemSrcBufferPoolClass GstAmShMemSrcBufferPoolClass;

GType gst_amshmem_src_buffer_pool_get_type (void);

GstBufferPool *gst_amshmem_src_buffer_pool_new (GstAmShMemSrc *src);

void gst_amshmem_src_buffer_mark_dds_filled (GstBuffer *buf);

void gst_amshmem_src_buffer_attach_nv12_side (GstBuffer *buf,
    const AmShMem_Msg *msg);

gboolean gst_amshmem_src_buffer_peek_nv12_frame_id (GstBuffer *buf,
    guint32 *frame_id_out);

const AmShMem_Msg *gst_amshmem_src_buffer_peek_nv12_msg (GstBuffer *buf);

G_END_DECLS

#endif /* __GST_AMSHMEM_SRC_BUFFER_POOL_H__ */
