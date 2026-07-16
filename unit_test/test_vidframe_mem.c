/*
 * test_vidframe_mem.c
 *
 * History:
 *    5/28/2022 - [Zhi He] created file
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

#include "amba_vidframe_mem.h"

int
main (int argc, char **argv)
{
  GstAllocator *alloc;
  GstMemory *mem;
  GstAllocationParams params;
  GstMapInfo info;
  guint f, w, h;

  gst_init (&argc, &argv);

  /* allocator with custom alloc API */
  amba_vidframe_mem_init();

  /* we can get the allocator but we can only make objects from it when we know
   * the API */
  alloc = gst_allocator_find ("AmbaVidfmem");

  /* use custom api to alloc */
  mem = my_vidmem_alloc (0, 640, 480);
  g_assert (my_is_vidmem (mem) );

  amba_vidframe_mem_get_format (mem, &f, &w, &h);
  g_assert (f == 0);
  g_assert (w == 640);
  g_assert (h == 480);

  gst_memory_map (mem, &info, GST_MAP_READ);
  gst_memory_unmap (mem, &info);

  gst_memory_unref (mem);
  gst_object_unref (alloc);

  return 0;
}


