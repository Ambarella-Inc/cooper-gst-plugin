/*
 * amba_direct_mem.h
 *
 * History:
 *    5/30/2022 - [Zhi He] created file
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

#ifndef __AMBA_DIRECT_MEM_H__
#define __AMBA_DIRECT_MEM_H__

void amba_direct_mem_init (void);

GstMemory *
amba_direct_mem_alloc (gpointer base, gsize maxsize, guint alloc_and_copy);

GstMemory *
amba_direct_mem_alloc_user_data_0 (gpointer base, gsize maxsize,
  guint alloc_and_copy, void * user_data_0);

GstMemory *
amba_direct_mem_alloc_user_data_1 (gpointer base, gsize maxsize,
  guint alloc_and_copy, void * user_data_0, void * user_data_1);

#endif

