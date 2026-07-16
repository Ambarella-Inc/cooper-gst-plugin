/*
 * amba_direct_mem.c
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

#include "debug_log.h"
#include "amba_direct_mem.h"
#include "internal.h"

static GstAllocator *_amba_direct_mem_allocator = NULL;

typedef struct {
  GstMemory mem;

  gpointer base;

  gpointer user_data[4];

  guint8 * p_allocated_base;
  guint  allocated_size;
} AmbaDirectMem;

static GstMemory *
_alloc (GstAllocator *allocator, gsize size, GstAllocationParams *params)
{
  DUNUSED(allocator);
  DUNUSED(params);
  AmbaDirectMem * mem;

  g_warning ("Use amba_direct_mem_alloc() to allocate from this allocator, size %ld", size);

  mem = g_slice_new (AmbaDirectMem);

  mem->allocated_size = size;
  mem->p_allocated_base = (guint8 *) malloc(size);
  mem->base = mem->p_allocated_base;

  gst_memory_init (GST_MEMORY_CAST (mem), 0,
    _amba_direct_mem_allocator, NULL,
    size, 0, 0, size);

  return (GstMemory *) mem;
}

static void
_free (GstAllocator *allocator, GstMemory *mem)
{
  DUNUSED(allocator);
  AmbaDirectMem *vmem = (AmbaDirectMem *) mem;

  if (vmem) {
    if (vmem->p_allocated_base) {
      free (vmem->p_allocated_base);
      vmem->p_allocated_base= (guint8 *) 0;
    }
    g_slice_free (AmbaDirectMem, vmem);
  }

}

static gpointer
_amba_direct_mem_map_full (GstMemory * mem, GstMapInfo * info, gsize maxsize)
{
  DUNUSED(maxsize);
  AmbaDirectMem * thiz = (AmbaDirectMem *) mem;
  gpointer res = thiz->base;

  info->user_data[0] = thiz->user_data[0];
  info->user_data[1] = thiz->user_data[1];
  info->user_data[2] = thiz->user_data[2];
  info->user_data[3] = thiz->user_data[3];

  GST_DEBUG ("%p: mapped %p", thiz, res);
  return res;
}

static void
_amba_direct_mem_unmap_full (GstMemory * mem, GstMapInfo * info)
{
  DUNUSED(info);
  AmbaDirectMem * thiz = (AmbaDirectMem *) mem;
  GST_DEBUG ("%p: unmapped %p", thiz, thiz->base);
}

static AmbaDirectMem *
_amba_direct_mem_share (AmbaDirectMem *mem, gssize offset, gsize size)
{
  AmbaDirectMem *sub;
  GstMemory *parent;

  GST_DEBUG ("%p: share %" G_GSSIZE_FORMAT " %" G_GSIZE_FORMAT, mem, offset,
    size);

  /* find the real parent */
  if ( (parent = mem->mem.parent) == NULL) {
    parent = (GstMemory *) mem;
  }

  if (size == (gsize) (-1)) {
    if (offset >= (gssize) mem->mem.size) {
      GST_ERROR("offset too large %" G_GSSIZE_FORMAT ", mem.size %" G_GSSIZE_FORMAT,
        offset, mem->mem.size);
      return NULL;
    } else {
      size = mem->mem.size - offset;
    }
  } else {
    if ((offset + size) >= mem->mem.size) {
      GST_ERROR("offset + size exceed: %" G_GSSIZE_FORMAT " + %"  G_GSSIZE_FORMAT " >= mem.size %" G_GSSIZE_FORMAT,
        offset, size, mem->mem.size);
      return NULL;
    }
  }

  sub = g_slice_new (AmbaDirectMem);
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
} AmbaDirectMemAllocator;

typedef struct {
  GstAllocatorClass parent_class;
} AmbaDirectMemAllocatorClass;

GType amba_direct_mem_allocator_get_type (void);
G_DEFINE_TYPE (AmbaDirectMemAllocator, amba_direct_mem_allocator, GST_TYPE_ALLOCATOR);

static void
amba_direct_mem_allocator_class_init (
  AmbaDirectMemAllocatorClass *klass)
{
  GstAllocatorClass *allocator_class;

  allocator_class = (GstAllocatorClass *) klass;

  allocator_class->alloc = _alloc;
  allocator_class->free = _free;
}

static void
amba_direct_mem_allocator_init (AmbaDirectMemAllocator * allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

  alloc->mem_type = "AmbaDirectMem";
  alloc->mem_map_full = (GstMemoryMapFullFunction) _amba_direct_mem_map_full;
  alloc->mem_unmap_full = (GstMemoryUnmapFullFunction) _amba_direct_mem_unmap_full;
  alloc->mem_share = (GstMemoryShareFunction) _amba_direct_mem_share;
}

void amba_direct_mem_init (void)
{
  /* cppcheck-suppress threadsafety */
  static gsize initialized = 0;

  if (g_once_init_enter (&initialized)) {
    _amba_direct_mem_allocator = g_object_new (amba_direct_mem_allocator_get_type(), NULL);
    if (!_amba_direct_mem_allocator) {
      GST_ERROR("g_object_new failed\n");
    } else {
      gst_allocator_register ("AmbaDirectMem", _amba_direct_mem_allocator);
    }
    g_once_init_leave (&initialized, 1);
  }

  return;
}

static AmbaDirectMem *
__amba_direct_mem_alloc (gpointer base, gsize maxsize, guint alloc_and_copy)
{
  AmbaDirectMem * mem;

  GST_DEBUG ("alloc mem: base %p, maxsize %ld", base, maxsize);

  mem = g_slice_new (AmbaDirectMem);

  gst_memory_init (GST_MEMORY_CAST (mem), 0,
    _amba_direct_mem_allocator, NULL,
    maxsize, 0, 0, maxsize);

  if (!alloc_and_copy) {
    mem->base = base;
    mem->allocated_size = 0;
    mem->p_allocated_base = (guint8 *) 0;
  } else {
    mem->allocated_size = maxsize;
    mem->p_allocated_base = (guint8 *) malloc(maxsize);
    mem->base = mem->p_allocated_base;
    memcpy(mem->base, base, maxsize);
  }

  return mem;
}

GstMemory *
amba_direct_mem_alloc (gpointer base, gsize maxsize, guint alloc_and_copy)
{
  return (GstMemory *) __amba_direct_mem_alloc (base, maxsize, alloc_and_copy);
}

GstMemory *
amba_direct_mem_alloc_user_data_0 (gpointer base, gsize maxsize,
  guint alloc_and_copy, void * user_data_0)
{
  AmbaDirectMem * mem = __amba_direct_mem_alloc (base, maxsize, alloc_and_copy);

  mem->user_data[0] = user_data_0;

  return (GstMemory *) mem;
}

GstMemory *
amba_direct_mem_alloc_user_data_1 (gpointer base, gsize maxsize,
  guint alloc_and_copy, void * user_data_0, void * user_data_1)
{
  AmbaDirectMem * mem = __amba_direct_mem_alloc (base, maxsize, alloc_and_copy);

  mem->user_data[0] = user_data_0;
  mem->user_data[1] = user_data_1;

  return (GstMemory *) mem;
}


gboolean
is_amba_direct_mem (GstMemory *mem)
{
  return mem->allocator == _amba_direct_mem_allocator;
}

