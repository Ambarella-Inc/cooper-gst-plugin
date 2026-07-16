/*
 * direct_ring_mem.h
 *
 * History:
 *    5/27/2022 - [Zhi He] created file
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

#include <gst/gst.h>

#ifndef __DIRECT_RING_MEM_H__
#define __DIRECT_RING_MEM_H__

GstAllocator * create_direct_ring_mem (void);
void destroy_direct_ring_mem (GstAllocator * thiz);

GstMemory * direct_ring_mem_alloc (GstAllocator * thiz, gpointer base, gsize maxsize);

#endif

