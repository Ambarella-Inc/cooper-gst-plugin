/*
 * gst_amba_cavalry_allocator.h
 *
 * History:
 *    2025/04/01 - [Yang Yu] created file
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

#ifndef __GST_AMBA_CAVALRY_ALLOCATOR_H__
#define __GST_AMBA_CAVALRY_ALLOCATOR_H__

#include <gst/gst.h>
#include <gst/allocators/gstfdmemory.h>

G_BEGIN_DECLS


#define GST_CAPS_FEATURE_MEMORY_AMBA_CAVALRY "memory:AMBA_CAVALRY"

#define GST_ALLOCATOR_AMBA_CAVALRY "amba_cavalry"
#define GST_ALLOCATOR_AMBA_CAVALRY_PHYS "amba_cavalry_phys"

#define GST_TYPE_AMBA_CAVALRY_ALLOCATOR              (gst_amba_cavalry_allocator_get_type())
#define GST_TYPE_AMBA_CAVALRY_PHYS_ALLOCATOR         (gst_amba_cavalry_phys_allocator_get_type())
#define GST_IS_AMBA_CAVALRY_PHYS_ALLOCATOR(obj) \
  ((obj) && G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_AMBA_CAVALRY_PHYS_ALLOCATOR))

#if 0
#define GST_IS_AMBA_CAVALRY_ALLOCATOR(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_AMBA_CAVALRY_ALLOCATOR))
#define GST_IS_AMBA_CAVALRY_ALLOCATOR_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_AMBA_CAVALRY_ALLOCATOR))
#define GST_AMBA_CAVALRY_ALLOCATOR_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_AMBA_CAVALRY_ALLOCATOR, GstAmbaCavalryAllocatorClass))
#define GST_AMBA_CAVALRY_ALLOCATOR(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_AMBA_CAVALRY_ALLOCATOR, GstAmbaCavalryAllocator))
#define GST_AMBA_CAVALRY_ALLOCATOR_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_AMBA_CAVALRY_ALLOCATOR, GstAmbaCavalryAllocatorClass))
#define GST_AMBA_CAVALRY_ALLOCATOR_CAST(obj)         ((GstAmbaCavalryAllocator *)(obj))

typedef struct _GstAmbaCavalryAllocator GstAmbaCavalryAllocator;
typedef struct _GstAmbaCavalryAllocatorClass GstAmbaCavalryAllocatorClass;

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstAmbaCavalryAllocator, gst_object_unref)
#endif

GST_ALLOCATORS_API
GType          gst_amba_cavalry_allocator_get_type (void);

GST_ALLOCATORS_API
GType          gst_amba_cavalry_phys_allocator_get_type (void);

GST_ALLOCATORS_API
GstAllocator * gst_amba_cavalry_allocator_new (void);

GST_ALLOCATORS_API
gint           gst_amba_cavalry_memory_get_fd (GstMemory * mem);

GST_ALLOCATORS_API
gboolean       gst_is_amba_cavalry_memory (GstMemory * mem);

GST_ALLOCATORS_API
gboolean       gst_is_amba_cavalry_memory_phy (GstMemory * mem);

GST_ALLOCATORS_API
gboolean       gst_is_amba_cavalry_allocator_family (GstAllocator * alloc);

GST_ALLOCATORS_API
guint64        gst_amba_cavalry_memory_get_phys_base (GstMemory * mem);

GST_ALLOCATORS_API
void gst_amba_cavalry_allocator_init_once (void);

GST_ALLOCATORS_API
void gst_amba_cavalry_phys_allocator_init_once (void);

GST_ALLOCATORS_API
GstAllocator* gst_amba_cavalry_allocator_get (void);

GST_ALLOCATORS_API
GstAllocator* gst_amba_cavalry_phys_allocator_get (void);

GST_ALLOCATORS_API
G_DECLARE_FINAL_TYPE (GstAmbaCavalryAllocator, gst_amba_cavalry_allocator, GST, AMBA_CAVALRY_ALLOCATOR, GstFdAllocator)


G_END_DECLS
#endif /* __GST_AMBA_CAVALRY_ALLOCATOR_H__ */
