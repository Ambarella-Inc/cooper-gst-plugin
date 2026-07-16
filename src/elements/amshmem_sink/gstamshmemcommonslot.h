  /*
 * gstamshmemcommonslot.h
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
 * SECTION: element-amshmem_commonslot
 * @title: amshmem_commonslot
 *
 * A common slot for amshmem_sink and amshmem_src.
 */

#ifndef __GST_AMSHMEM_COMMON_SLOT_H__
#define __GST_AMSHMEM_COMMON_SLOT_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_AMSHMEM_POOL_MAX_BUFFERS 16

/** Assign first-seen slot on buffer (qdata); reset *counter in element start.
 *  New buffers get slot (*counter)++ % GST_AMSHMEM_POOL_MAX_BUFFERS (soft path wraps). */
guint gst_amshmem_buffer_get_or_assign_slot (GstBuffer *buf, guint *counter);

G_END_DECLS

#endif /* __GST_AMSHMEM_COMMON_SLOT_H__ */
