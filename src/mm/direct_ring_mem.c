/*
 * direct_ring_mem.c
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

#include "direct_ring_mem.h"

typedef struct {
  GstMemory mem;

  gpointer base;
} DirectRingMem;

static GstMemory *
_alloc (GstAllocator *allocator, gsize size, GstAllocationParams *params)
{
  g_warning ("Use direct_ring_mem_alloc() to allocate from this allocator");

  return NULL;
}

static void
_free (GstAllocator *allocator, GstMemory *mem)
{
  DirectRingMem *vmem = (DirectRingMem *) mem;

  g_slice_free (DirectRingMem, vmem);

  GST_DEBUG ("%p: freed", vmem);
}

static gpointer
_direct_ring_mem_map (DirectRingMem *mem,
  gsize maxsize, GstMapFlags flags)
{
  gpointer res = mem->base;

  GST_DEBUG ("%p: mapped %p", mem, res);

  return res;
}

static gboolean
_direct_ring_mem_unmap (DirectRingMem *mem)
{
  GST_DEBUG ("%p: unmapped %p", mem, mem->base);
  return TRUE;
}

static DirectRingMem *
_direct_ring_mem_share (DirectRingMem *mem, gssize offset, gsize size)
{
  DirectRingMem *sub;
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

  sub = g_slice_new (DirectRingMem);
  /* the shared memory is always readonly */
  gst_memory_init (GST_MEMORY_CAST (sub), GST_MINI_OBJECT_FLAGS (parent) |
    GST_MINI_OBJECT_FLAG_LOCK_READONLY, mem->mem.allocator, parent,
    mem->mem.maxsize, mem->mem.align, mem->mem.offset + offset, size);

  /* install pointer */
  sub->base = mem->base + offset;

  return sub;
}

typedef struct {
  GstAllocator parent;
} DirectRingMemAllocator;

typedef struct {
  GstAllocatorClass parent_class;
} DirectRingMemAllocatorClass;

GType direct_ring_mem_allocator_get_type (void);
G_DEFINE_TYPE (DirectRingMemAllocator, direct_ring_mem_allocator, GST_TYPE_ALLOCATOR);

static void
direct_ring_mem_allocator_class_init (
  DirectRingMemAllocatorClass *klass)
{
  GstAllocatorClass *allocator_class;

  allocator_class = (GstAllocatorClass *) klass;

  allocator_class->alloc = _alloc;
  allocator_class->free = _free;
}

static void
direct_ring_mem_allocator_init (DirectRingMemAllocator * allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

  alloc->mem_type = "DirectRingMem";
  alloc->mem_map = (GstMemoryMapFunction) _direct_ring_mem_map;
  alloc->mem_unmap = (GstMemoryUnmapFunction) _direct_ring_mem_unmap;
  alloc->mem_share = (GstMemoryShareFunction) _direct_ring_mem_share;
}

GstAllocator * create_direct_ring_mem (void)
{
  GstAllocator * thiz = NULL;
  thiz = g_object_new (direct_ring_mem_allocator_get_type(), NULL);
  if (!thiz) {
    GST_ERROR("no memory\n");
    return NULL;
  }
  gst_allocator_register ("DirectRingMem", gst_object_ref (thiz) );

  return thiz;
}

void destroy_direct_ring_mem (GstAllocator * thiz)
{
  g_object_unref(thiz);
}


GstMemory *
direct_ring_mem_alloc (GstAllocator * thiz, gpointer base, gsize maxsize)
{
  DirectRingMem * mem;

  GST_DEBUG ("alloc mem: base %p, maxsize %ld", base, maxsize);

  mem = g_slice_new (DirectRingMem);

  gst_memory_init (GST_MEMORY_CAST (mem), 0, thiz, NULL,
    maxsize, 0, 0, maxsize);

  mem->base = base;

  return (GstMemory *) mem;
}

