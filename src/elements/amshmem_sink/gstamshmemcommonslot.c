/*
 * gstamshmemcommonslot.c
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

#include "gstamshmemcommonslot.h"

static GQuark slot_quark;

static void
ensure_quark (void)
{
  if (slot_quark == 0)
    slot_quark = g_quark_from_static_string ("amshmem.pool-slot");
}

guint
gst_amshmem_buffer_get_or_assign_slot (GstBuffer *buf, guint *counter)
{
  gpointer q;

  g_return_val_if_fail (GST_IS_BUFFER (buf), 0);
  g_return_val_if_fail (counter != NULL, 0);

  ensure_quark ();
  q = gst_mini_object_get_qdata (GST_MINI_OBJECT (buf), slot_quark);
  if (q != NULL)
    return GPOINTER_TO_UINT (q) - 1;

  /* HW pool: same GstBuffer handles repeat; counter stops at N. Soft-staging: new buffer
   * every frame — wrap slot index so buffer_index stays in range and FreeFrame matches. */
  {
    guint s = (*counter)++ % GST_AMSHMEM_POOL_MAX_BUFFERS;

    gst_mini_object_set_qdata (GST_MINI_OBJECT (buf), slot_quark,
        GUINT_TO_POINTER (s + 1), NULL);
    return s;
  }
}
