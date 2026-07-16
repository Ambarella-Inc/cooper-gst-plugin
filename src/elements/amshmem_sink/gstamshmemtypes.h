/*
 * gstamshmemtypes.h
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
 * SECTION: element-amshmem_types
 * @title: amshmem_types
 *
 * Shared types for amshmem_sink and amshmem_src.
 */


#ifndef __GST_AMSHMEM_TYPES_H__
#define __GST_AMSHMEM_TYPES_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef enum {
  GST_AMSHMEM_IMPLEM_SOCKET = 0,
  GST_AMSHMEM_IMPLEM_CYCLONEDDS,
  GST_AMSHMEM_IMPLEM_FASTDDS,
} GstAmShMemImplemMethod;

GType gst_amshmem_implem_method_get_type (void);

#define GST_TYPE_AMSHMEM_IMPLEM_METHOD (gst_amshmem_implem_method_get_type ())

/** Caps for buffers carrying a serialized AmShMem_Msg (amshmem_src, element A/B, compositor). */
#define GST_AMSHMEM_MSG_CAPS "application/x-amshmem-msg"

/**
 * AmShMem_Msg.data_format when publishing over DDS:
 * @AM_SHMEM_DATA_FORMAT_PHYS_NV12: phys_y_addr / phys_uv_addr are CPU physical addresses
 *   in a shared PA memory view; receivers map with /dev/mem (Linux) or equivalent.
 *   Default when amshmem_sink shm-handshake-path is empty.
 *   Those PA values must refer to memory obtained from the Cavalry allocator
 *   (e.g. cavalry_mem_alloc_with_attr); plain GStreamer heap buffers cannot be
 *   turned into PHY with cavalry_mem_virt_to_phys. For software decode (NV12
 *   without GstAmbaCavalryBufferMeta), amshmem_sink stages each frame into
 *   cavalry_mem_alloc_with_attr(..., share_to_dsp=0, cache_en=1) before publish.
 * @AM_SHMEM_DATA_FORMAT_POOL_OFFSET_NV12: same fields are byte offsets from mmap base of the pool fd
 *   (pool fd passed via SCM_RIGHTS on the Unix socket named by amshmem_sink/src shm-handshake-path).
 *   Used when shm-handshake-path is set on both sink and src (same host).
 */
#define AM_SHMEM_DATA_FORMAT_PHYS_NV12         0u
#define AM_SHMEM_DATA_FORMAT_POOL_OFFSET_NV12  1u

G_END_DECLS

#endif /* __GST_AMSHMEM_TYPES_H__ */
