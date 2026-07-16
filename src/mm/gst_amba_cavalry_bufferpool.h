/*
 * gst_amba_cavalry_bufferpool.h
 *
 * History:
 *    2025/06/24 - [Yang Yu] created file
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

#ifndef __GST_AMBA_CAVALRY_BUFFERPOOL_H__
#define __GST_AMBA_CAVALRY_BUFFERPOOL_H__

#include <gst/gst.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

/**
 * GstAmbaCavalryBufferMeta:
 * @meta: parent #GstMeta
 * @block_index: index of the memory block in the contiguous memory
 * @block_offset: offset of this block within the contiguous memory
 * @block_size: size of this memory block
 * @is_contiguous: whether this buffer uses contiguous memory
 * @fd: file descriptor for cavalry memory (shared for contiguous mode)
 * @slab_phys_base: physical address of buffer NV12 origin (map.data); 0 if mfd-only
 *
 * Meta containing information about cavalry buffer allocation
 */
typedef struct _GstAmbaCavalryBufferMeta {
  GstMeta meta;

  gint block_index;       /* Index in the blocks array */
  gsize block_offset;     /* Offset within contiguous memory */
  gsize block_size;       /* Size of this block */
  gboolean is_contiguous; /* TRUE if from contiguous allocation */
  gint fd;                /* File descriptor for cavalry memory */
  guint64 slab_phys_base; /* PA for first byte of NV12 view (cavalry_mem_alloc_with_attr path) */
} GstAmbaCavalryBufferMeta;

GST_EXPORT
GType gst_amba_cavalry_buffer_meta_api_get_type (void);
#define GST_AMBA_CAVALRY_BUFFER_META_API_TYPE (gst_amba_cavalry_buffer_meta_api_get_type())

GST_EXPORT
const GstMetaInfo * gst_amba_cavalry_buffer_meta_get_info (void);
#define GST_AMBA_CAVALRY_BUFFER_META_INFO (gst_amba_cavalry_buffer_meta_get_info())

#define gst_buffer_get_amba_cavalry_meta(b) \
  ((GstAmbaCavalryBufferMeta*)gst_buffer_get_meta((b), GST_AMBA_CAVALRY_BUFFER_META_API_TYPE))

GST_EXPORT
GstAmbaCavalryBufferMeta * gst_buffer_add_amba_cavalry_meta (GstBuffer * buffer,
    gint block_index, gsize block_offset, gsize block_size, gboolean is_contiguous, gint fd,
    guint64 slab_phys_base);

#define GST_TYPE_AMBA_CAVALRY_BUFFER_POOL              (gst_amba_cavalry_buffer_pool_get_type())

GST_EXPORT
G_DECLARE_FINAL_TYPE (GstAmbaCavalryBufferPool, gst_amba_cavalry_buffer_pool, GST, AMBA_CAVALRY_BUFFER_POOL, GstBufferPool)

/**
 * GstAmbaCavalryBufferPool:
 *
 * A GstBufferPool subclass for allocating buffers backed by a single large
 * contiguous cavalry memory block. This ensures physical address continuity
 * between buffers.
 */

/**
 * gst_amba_cavalry_buffer_pool_new:
 *
 * Create a new #GstAmbaCavalryBufferPool
 *
 * Returns: (transfer full): A new #GstAmbaCavalryBufferPool
 */
GST_EXPORT
GstBufferPool * gst_amba_cavalry_buffer_pool_new (void);

/**
 * gst_amba_cavalry_buffer_pool_set_contiguous_memory:
 * @pool: a #GstAmbaCavalryBufferPool
 * @enable: %TRUE to enable contiguous memory allocation
 *
 * Enable or disable contiguous memory allocation mode. When enabled,
 * the pool will allocate one large memory block and divide it into
 * smaller buffers to ensure physical address continuity.
 */
GST_EXPORT
void gst_amba_cavalry_buffer_pool_set_contiguous_memory (GstAmbaCavalryBufferPool * pool, gboolean enable);

/**
 * gst_amba_cavalry_buffer_pool_get_contiguous_memory:
 * @pool: a #GstAmbaCavalryBufferPool
 *
 * Check if contiguous memory allocation is enabled.
 *
 * Returns: %TRUE if contiguous memory allocation is enabled
 */
GST_EXPORT
gboolean gst_amba_cavalry_buffer_pool_get_contiguous_memory (GstAmbaCavalryBufferPool * pool);

/**
 * gst_amba_cavalry_buffer_get_fd:
 * @buffer: a #GstBuffer
 *
 * Get the cavalry file descriptor from the buffer metadata.
 * This works for both contiguous and individual cavalry buffers.
 *
 * Returns: the file descriptor, or -1 if not available
 */
GST_EXPORT
gint gst_amba_cavalry_buffer_get_fd (GstBuffer * buffer);

GST_EXPORT
gint gst_amba_cavalry_buffer_get_block_index (GstBuffer * buffer);

GST_EXPORT
gsize gst_amba_cavalry_buffer_get_block_offset (GstBuffer * buffer);

GST_EXPORT
gsize gst_amba_cavalry_buffer_get_block_size (GstBuffer * buffer);

GST_EXPORT
gboolean gst_amba_cavalry_buffer_is_contiguous (GstBuffer * buffer);

GST_EXPORT
gboolean gst_amba_cavalry_buffer_has_meta (GstBuffer * buffer);

GST_EXPORT
guint64 gst_amba_cavalry_buffer_get_slab_phys (GstBuffer * buffer);

G_END_DECLS

#endif /* __GST_AMBA_CAVALRY_BUFFERPOOL_H__ */