/* amba_vidframe_mem.c
 *
 * History:
 *    5/30/2022 - [Zhi He] created file
 *
 * Copyright (C) 2022 Ambarella International LP
 *
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

#include "amba_vidframe_mem.h"

static GstAllocator *_amba_vidf_mem_allocator;

typedef struct {
  GstMemory mem;

  guint format;
  guint width;
  guint height;
  gpointer data;

} AmbaVidfmem;


static GstMemory *
_alloc (GstAllocator *allocator, gsize size, GstAllocationParams *params)
{
  g_warning ("Use amba_vidframe_mem_alloc() to allocate from this allocator");

  return NULL;
}

static void
_free (GstAllocator *allocator, GstMemory *mem)
{
  AmbaVidfmem *vmem = (AmbaVidfmem *) mem;

  g_free (vmem->data);
  g_slice_free (AmbaVidfmem, vmem);
  GST_DEBUG ("%p: freed", vmem);
}

static gpointer
_amba_vidframe_mem_map (AmbaVidfmem *mem, gsize maxsize, GstMapFlags flags)
{
  gpointer res;

  while (TRUE) {
    if ( (res = g_atomic_pointer_get (&mem->data) ) != NULL) {
      break;
    }

    res = g_malloc (maxsize);

    if (g_atomic_pointer_compare_and_exchange (&mem->data, NULL, res) ) {
      break;
    }

    g_free (res);
  }

  GST_DEBUG ("%p: mapped %p", mem, res);

  return res;
}

static gboolean
_amba_vidframe_mem_unmap (AmbaVidfmem *mem)
{
  GST_DEBUG ("%p: unmapped", mem);
  return TRUE;
}

static AmbaVidfmem *
_amba_vidframe_mem_share (AmbaVidfmem *mem, gssize offset, gsize size)
{
  AmbaVidfmem *sub;
  GstMemory *parent;

  GST_DEBUG ("%p: share %" G_GSSIZE_FORMAT " %" G_GSIZE_FORMAT, mem, offset,
             size);

  /* find the real parent */
  if ( (parent = mem->mem.parent) == NULL) {
    parent = (GstMemory *) mem;
  }

  if (size == -1) {
    size = mem->mem.size - offset;
  }

  sub = g_slice_new (AmbaVidfmem);
  /* the shared memory is always readonly */
  gst_memory_init (GST_MEMORY_CAST (sub), GST_MINI_OBJECT_FLAGS (parent) |
                   GST_MINI_OBJECT_FLAG_LOCK_READONLY, mem->mem.allocator, parent,
                   mem->mem.maxsize, mem->mem.align, mem->mem.offset + offset, size);

  /* install pointer */
  sub->data = _amba_vidframe_mem_map (mem, mem->mem.maxsize, GST_MAP_READ);

  return sub;
}

typedef struct {
  GstAllocator parent;
} AmbaVidfmemAllocator;

typedef struct {
  GstAllocatorClass parent_class;
} AmbaVidfmemAllocatorClass;

GType amba_vidframe_mem_allocator_get_type (void);
G_DEFINE_TYPE (AmbaVidfmemAllocator, amba_vidframe_mem_allocator, GST_TYPE_ALLOCATOR);

static void
amba_vidframe_mem_allocator_class_init (
  AmbaVidfmemAllocatorClass *klass)
{
  GstAllocatorClass *allocator_class;

  allocator_class = (GstAllocatorClass *) klass;

  allocator_class->alloc = _alloc;
  allocator_class->free = _free;
}

static void
amba_vidframe_mem_allocator_init (AmbaVidfmemAllocator *allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

  alloc->mem_type = "AmbaVidfmem";
  alloc->mem_map = (GstMemoryMapFunction) _amba_vidframe_mem_map;
  alloc->mem_unmap = (GstMemoryUnmapFunction) _amba_vidframe_mem_unmap;
  alloc->mem_share = (GstMemoryShareFunction) _amba_vidframe_mem_share;
}

void
amba_vidframe_mem_init (void)
{
  _amba_vidf_mem_allocator = g_object_new (my_vidmem_allocator_get_type(), NULL);

  gst_allocator_register ("AmbaVidfmem", gst_object_ref (_amba_vidf_mem_allocator) );
}

GstMemory *
amba_vidframe_mem_alloc (guint format, guint width, guint height)
{
  AmbaVidfmem *mem;
  gsize maxsize;

  GST_DEBUG ("alloc frame format %u %ux%u", format, width, height);

  maxsize = (GST_ROUND_UP_4 (width) * height);

  mem = g_slice_new (AmbaVidfmem);

  gst_memory_init (GST_MEMORY_CAST (mem), 0, _amba_vidf_mem_allocator, NULL,
                   maxsize, 31, 0, maxsize);

  mem->format = format;
  mem->width = width;
  mem->height = height;
  mem->data = NULL;

  return (GstMemory *) mem;
}

gboolean
is_amba_vidframe_mem (GstMemory *mem)
{
  return mem->allocator == _amba_vidf_mem_allocator;
}

void
amba_vidframe_mem_get_format (GstMemory *mem, guint *format,
                              guint *width, guint *height)
{
  AmbaVidfmem *vmem = (AmbaVidfmem *) mem;

  *format = vmem->format;
  *width = vmem->width;
  *height = vmem->height;
}

