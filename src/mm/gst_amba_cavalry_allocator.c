/*
 * gst_amba_cavalry_allocator.c
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#include "cavalry_mem.h"
#include "gst_amba_cavalry_allocator.h"


/* Cavalry / dmabuf backing: round size up to page multiple (typical 4 KiB). */
#define GST_AMBA_CAVALRY_HW_ALIGN_SIZE  4096u

/**
 * GstAmbaCavalryAllocator:
 *
 * Final class of allocator with amba cavalry memory
 *
 */
struct _GstAmbaCavalryAllocator
{
  GstFdAllocator parent;

  /*< private >*/
  gint cavalry_fd;
  gpointer _gst_reserved[GST_PADDING];
};

struct _GstAmbaCavalryAllocatorClass
{
  GstFdAllocatorClass parent_class;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GST_DEBUG_CATEGORY_STATIC (amba_cavalry_debug);
#define GST_CAT_DEFAULT amba_cavalry_debug

#define _do_init                                        \
    GST_DEBUG_CATEGORY_INIT (amba_cavalry_debug,              \
    "amba_cavalry", 0, "amba cavalry memory");

/* cppcheck-suppress unknownMacro */
G_DEFINE_TYPE_WITH_CODE (GstAmbaCavalryAllocator, gst_amba_cavalry_allocator,
    GST_TYPE_FD_ALLOCATOR, _do_init);

static gpointer
gst_amba_cavalry_mem_map (GstMemory * gmem, GstMapInfo * info, gsize maxsize)
{
  GstAllocator *allocator = gmem->allocator;
  gpointer ret;

  ret = allocator->mem_map (gmem, maxsize, info->flags);

  if ((info->flags & GST_MAP_READ) || (info->flags & GST_MAP_WRITE))
  {
    // we need invalid cache after map if we need read or write
    if (cavalry_mem_sync_cache_mfd(maxsize, 0, gst_fd_memory_get_fd(gmem), 0, 1) < 0)
      GST_WARNING_OBJECT(allocator, "cavalry_mem_sync_cache_mfd: Failed to invalid cache: %s (%i)",
                         g_strerror(errno), errno);
  }

  return ret;
}

static void
gst_amba_cavalry_mem_unmap(GstMemory *gmem, GstMapInfo *info)
{
  GstAllocator *allocator = gmem->allocator;
  gsize maxsize = info->maxsize;

  if (info->flags & GST_MAP_WRITE)
  {
    // we only clean cache before unmap if we do write map
    if (cavalry_mem_sync_cache_mfd(maxsize, 0, gst_fd_memory_get_fd(gmem), 1, 0) < 0)
      GST_WARNING_OBJECT(allocator, "cavalry_mem_sync_cache_mfd: Failed to clean cache: %s (%i)",
                         g_strerror(errno), errno);
  }

  allocator->mem_unmap (gmem);
}

static GstMemory *
gst_amba_cavalry_allocator_alloc (GstAllocator * allocator, gsize size,
    GstAllocationParams * params)
{
  GstAmbaCavalryAllocator *self = GST_AMBA_CAVALRY_ALLOCATOR (allocator);
  int fd;
  GstMemory *mem;
  GstMapInfo info;

  /* Optimized memory size calculation - leverage hardware DMA alignment guarantee */
  gsize align = params->align;
  align |= gst_memory_alignment;

  // Ensure prefix is aligned to user's requirement to avoid breaking alignment
  gsize aligned_prefix = (params->prefix + align) & ~align;
  gsize requested_size = size + aligned_prefix + params->padding;

  // Hardware allocates in 4K multiples, align our request accordingly
  gsize maxsize = (requested_size + GST_AMBA_CAVALRY_HW_ALIGN_SIZE - 1) & ~(GST_AMBA_CAVALRY_HW_ALIGN_SIZE - 1);

  void* p_virt = NULL;
  struct cavalry_mem_attr attr;
  memset(&attr, 0, sizeof(attr));
  attr.cache_en = 1;
#ifdef BUILD_DSP_AMBA_V6
  attr.share_to_dsp = 1;  /* V6: CV+DSP+ARM shared memory */
#endif
  /* V5: share_to_dsp = 0 (default from memset) */
  if (cavalry_mem_alloc_with_attr_mfd (maxsize, &fd, &p_virt, &attr) < 0) {
#ifdef BUILD_DSP_AMBA_V6
    GST_ERROR_OBJECT (self, "cavalry_mem_alloc_with_attr_mfd (V6, share_to_dsp=1) failed: %s, require size:%zu", strerror (errno), maxsize);
#else
    GST_ERROR_OBJECT (self, "cavalry_mem_alloc_with_attr_mfd (V5) failed: %s, require size:%zu", strerror (errno), maxsize);
#endif
    return NULL;
  }

  // FIXME: GST will handle the mem fd, so we unmap the first time mmap
  if (!p_virt || munmap(p_virt, maxsize) < 0) {
    GST_WARNING_OBJECT (self, "unmap cavalry mem after first alloc failed");
  }
  mem = gst_fd_allocator_alloc(allocator, fd, maxsize,
                               GST_FD_MEMORY_FLAG_KEEP_MAPPED);
  if (G_UNLIKELY (!mem)) {
    GST_ERROR_OBJECT (self, "GstFdMemory allocation failed");
    close (fd);
    return NULL;
  }

  /* We use GST_FD_MEMORY_FLAG_KEEP_MAPPED, so make sure the first map is RW. */
  if (!gst_memory_map (mem, &info, GST_MAP_READWRITE)) {
    GST_ERROR_OBJECT (self, "GstFdMemory map failed");
    gst_memory_unref (mem);
    return NULL;
  }

  /* Verify hardware alignment guarantee - safety check */
  guint8 *data = info.data;
  if (G_UNLIKELY(((guintptr) data) & align)) {
    GST_ERROR_OBJECT (self, "Hardware memory address %p not aligned to user requirement %lu bytes",
                      data, align + 1);
    gst_memory_unmap (mem, &info);
    gst_memory_unref (mem);
    return NULL;
  }

  /* Hardware alignment verified - proceed with simplified logic */
  gsize effective_prefix = aligned_prefix;

  if (params->prefix && (params->flags & GST_MEMORY_FLAG_ZERO_PREFIXED))
    memset (data, 0, effective_prefix);

  gsize padding = maxsize - (effective_prefix + size);
  if (padding && (params->flags & GST_MEMORY_FLAG_ZERO_PADDED))
    memset (data + effective_prefix + size, 0, padding);

  mem->align = align;
  mem->size = size;
  mem->maxsize = maxsize;
  mem->offset = effective_prefix;

  gst_memory_unmap (mem, &info);
#ifdef BUILD_DSP_AMBA_V6
  GST_DEBUG_OBJECT (self,
      "mfd_alloc: requested=%" G_GSIZE_FORMAT " sw_aligned=%" G_GSIZE_FORMAT
      " hw_aligned=%" G_GSIZE_FORMAT " fd=%d (virt after map was %p)",
      size, requested_size, maxsize, fd, (gpointer) data);
#else
  GST_DEBUG_OBJECT (self,
      "mfd_alloc: requested=%" G_GSIZE_FORMAT " sw_aligned=%" G_GSIZE_FORMAT
      " hw_aligned=%" G_GSIZE_FORMAT " fd=%d (virt after map was %p)",
      size, requested_size, maxsize, fd, (gpointer) data);
#endif
  return mem;
}

// we need this to close cavalry fd
static void
gst_amba_cavalry_allocator_finalize(GObject * obj)
{
  GstAmbaCavalryAllocator *alloc = GST_AMBA_CAVALRY_ALLOCATOR (obj);
  if (alloc->cavalry_fd >= 0) {
    GST_DEBUG ("gst_amba_cavalry_allocator close cavalry_fd.\n");
    close (alloc->cavalry_fd);
    alloc->cavalry_fd = -1;
  }
  G_OBJECT_CLASS (gst_amba_cavalry_allocator_parent_class)->finalize (obj);
}

static void
gst_amba_cavalry_allocator_class_init (GstAmbaCavalryAllocatorClass * klass)
{
  GstAllocatorClass *allocator_class = GST_ALLOCATOR_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  allocator_class->alloc = GST_DEBUG_FUNCPTR (gst_amba_cavalry_allocator_alloc);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_amba_cavalry_allocator_finalize);
}

static void
gst_amba_cavalry_allocator_init (GstAmbaCavalryAllocator * allocator)
{
  GstAllocator *base_alloc = GST_ALLOCATOR_CAST (allocator);
  // init cavalry mem lib when create allocator
  allocator->cavalry_fd = open(CAVALRY_DEV_NODE, O_RDWR, 0);
  int verbose_flag = 0;

  if (allocator->cavalry_fd < 0)
  {
    GST_ERROR_OBJECT(allocator, "open %s failed, %d.\n", CAVALRY_DEV_NODE,
                     allocator->cavalry_fd);
    return;
  }
  if (cavalry_mem_init(allocator->cavalry_fd, verbose_flag) < 0)
  {
    GST_ERROR_OBJECT(allocator, "cavalry_mem_init failed.\n");
    close(allocator->cavalry_fd);
    allocator->cavalry_fd = -1;
    return;
  }

  base_alloc->mem_type = GST_ALLOCATOR_AMBA_CAVALRY;
  base_alloc->mem_map_full = GST_DEBUG_FUNCPTR (gst_amba_cavalry_mem_map);
  base_alloc->mem_unmap_full = GST_DEBUG_FUNCPTR (gst_amba_cavalry_mem_unmap);
  GST_OBJECT_FLAG_SET (allocator, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
  GST_INFO ("gst_amba_cavalry_allocator inited.\n");
}

GstAllocator *
gst_amba_cavalry_allocator_new (void)
{
  GstAllocator *alloc;

  alloc = g_object_new (GST_TYPE_AMBA_CAVALRY_ALLOCATOR, NULL);
  gst_object_ref_sink (alloc);

  return alloc;
}

/* --- Phys allocator (cavalry_mem_alloc_with_attr, PA from driver) --- */

typedef struct {
  unsigned long phys;
  void *virt;
  gsize maxsize;
} GstAmbaCavalryPhysMemCtx;

static GQuark gst_amba_cavalry_phys_base_quark;

struct _GstAmbaCavalryPhysAllocator {
  GstAllocator parent;
};

struct _GstAmbaCavalryPhysAllocatorClass {
  GstAllocatorClass parent_class;
};

typedef struct _GstAmbaCavalryPhysAllocator GstAmbaCavalryPhysAllocator;
typedef struct _GstAmbaCavalryPhysAllocatorClass GstAmbaCavalryPhysAllocatorClass;

/* GstMemory subclass: first field must be GstMemory (same-address cast). */
typedef struct {
  GstMemory mem;
  GstAmbaCavalryPhysMemCtx *ctx;
} GstAmbaCavalryPhysMemory;

static void gst_amba_cavalry_phys_mem_ctx_destroy (gpointer data);
static void gst_amba_cavalry_phys_allocator_free (GstAllocator * allocator,
    GstMemory * mem);
static inline GstAmbaCavalryPhysMemory *
gst_amba_cavalry_phys_memory_cast (GstMemory * mem);

G_DEFINE_TYPE_WITH_CODE (GstAmbaCavalryPhysAllocator, gst_amba_cavalry_phys_allocator,
    GST_TYPE_ALLOCATOR, _do_init);

static void
gst_amba_cavalry_phys_mem_ctx_destroy (gpointer data)
{
  GstAmbaCavalryPhysMemCtx *ctx = data;

  if (!ctx)
    return;
  cavalry_mem_free ((unsigned long) ctx->maxsize, ctx->phys, ctx->virt);
  g_slice_free (GstAmbaCavalryPhysMemCtx, ctx);
}

static void
gst_amba_cavalry_phys_mem_unmap_full (GstMemory * mem, GstMapInfo * info)
{
  GstAmbaCavalryPhysMemory *pm;
  GstAmbaCavalryPhysMemCtx *ctx;
  gsize offset = 0, msize = 0;

  pm = gst_amba_cavalry_phys_memory_cast (mem);
  if (!pm)
    return;
  ctx = pm->ctx;
  gst_memory_get_sizes (mem, &offset, &msize);

  if (ctx && (info->flags & GST_MAP_WRITE)) {
    if (cavalry_mem_sync_cache ((unsigned long) msize,
            ctx->phys + (unsigned long) offset, 1, 0) < 0)
      GST_WARNING_OBJECT (mem->allocator,
          "cavalry_mem_sync_cache clean failed (phys path)");
  }
}

static gpointer
gst_amba_cavalry_phys_mem_map_full (GstMemory * mem, GstMapInfo * info,
    gsize maxsize)
{
  GstAmbaCavalryPhysMemory *pm;
  GstAmbaCavalryPhysMemCtx *ctx;
  gsize offset = 0, msize = 0;

  (void) maxsize;

  pm = gst_amba_cavalry_phys_memory_cast (mem);
  if (!pm)
    return NULL;
  ctx = pm->ctx;
  gst_memory_get_sizes (mem, &offset, &msize);

  if (ctx && ((info->flags & GST_MAP_READ) || (info->flags & GST_MAP_WRITE))) {
    if (cavalry_mem_sync_cache ((unsigned long) msize,
            ctx->phys + (unsigned long) offset, 0, 1) < 0)
      GST_WARNING_OBJECT (mem->allocator,
          "cavalry_mem_sync_cache invalidate failed (phys path)");
  }

  if (!ctx)
    return NULL;

  return (gpointer) ((guint8 *) ctx->virt + offset);
}

static inline GstAmbaCavalryPhysMemory *
gst_amba_cavalry_phys_memory_cast (GstMemory * mem)
{
  if (!mem || !mem->allocator
      || !GST_IS_AMBA_CAVALRY_PHYS_ALLOCATOR (mem->allocator))
    return NULL;
  return (GstAmbaCavalryPhysMemory *) mem;
}

static void
gst_amba_cavalry_phys_allocator_free (GstAllocator * allocator, GstMemory * mem)
{
  GstAmbaCavalryPhysMemory *pm;

  (void) allocator;

  gst_amba_cavalry_phys_allocator_init_once ();

  pm = gst_amba_cavalry_phys_memory_cast (mem);
  if (!pm) {
    GST_WARNING ("phys_allocator_free: invalid mem %p", (void *) mem);
    return;
  }

  if (pm->ctx) {
    gst_amba_cavalry_phys_mem_ctx_destroy (pm->ctx);
    pm->ctx = NULL;
  }

  g_slice_free (GstAmbaCavalryPhysMemory, pm);
}

static GstMemory *
gst_amba_cavalry_phys_allocator_alloc (GstAllocator * allocator, gsize size,
    GstAllocationParams * params)
{
  struct cavalry_mem_attr attr;
  unsigned long phys = 0;
  void *virt = NULL;
  GstAmbaCavalryPhysMemory *pm;
  gsize align, aligned_prefix, requested_size, maxsize, effective_prefix, padding;
  GstAmbaCavalryPhysMemCtx *ctx;

  align = params->align;
  align |= gst_memory_alignment;
  aligned_prefix = (params->prefix + align) & ~align;
  requested_size = size + aligned_prefix + params->padding;
  maxsize = (requested_size + GST_AMBA_CAVALRY_HW_ALIGN_SIZE - 1)
      & ~(GST_AMBA_CAVALRY_HW_ALIGN_SIZE - 1);

  memset (&attr, 0, sizeof (attr));
  attr.cache_en = 1;
#ifdef BUILD_DSP_AMBA_V6
  attr.share_to_dsp = 1;
#endif

  if (cavalry_mem_alloc_with_attr ((unsigned long) maxsize, &phys, &virt, &attr) < 0) {
    GST_ERROR_OBJECT (allocator, "cavalry_mem_alloc_with_attr failed size=%zu", maxsize);
    return NULL;
  }

  ctx = g_slice_new (GstAmbaCavalryPhysMemCtx);
  ctx->phys = phys;
  ctx->virt = virt;
  ctx->maxsize = maxsize;

  effective_prefix = aligned_prefix;
  padding = maxsize - (effective_prefix + size);

  if (params->prefix && (params->flags & GST_MEMORY_FLAG_ZERO_PREFIXED))
    memset (virt, 0, effective_prefix);
  if (padding && (params->flags & GST_MEMORY_FLAG_ZERO_PADDED))
    memset ((guint8 *) virt + effective_prefix + size, 0, padding);

  pm = g_slice_new0 (GstAmbaCavalryPhysMemory);
  pm->ctx = ctx;

  gst_memory_init (GST_MEMORY_CAST (&pm->mem), (GstMemoryFlags) 0,
      GST_ALLOCATOR_CAST (allocator), NULL, maxsize, align, effective_prefix, size);

  {
    guint64 *phys_copy = g_new (guint64, 1);

    *phys_copy = (guint64) phys;
    gst_mini_object_set_qdata (GST_MINI_OBJECT (&pm->mem), gst_amba_cavalry_phys_base_quark,
        phys_copy, (GDestroyNotify) g_free);
  }

  GST_DEBUG_OBJECT (allocator,
      "phys_alloc: PA=0x%lx virt=%p maxsize=%" G_GSIZE_FORMAT " visible=%" G_GSIZE_FORMAT
      " offset=%" G_GSIZE_FORMAT " align=%" G_GSIZE_FORMAT,
      (unsigned long) phys, virt, maxsize, size, effective_prefix, align);

  return GST_MEMORY_CAST (&pm->mem);
}

static void
gst_amba_cavalry_phys_allocator_class_init (GstAmbaCavalryPhysAllocatorClass * klass)
{
  GstAllocatorClass *allocator_class = GST_ALLOCATOR_CLASS (klass);

  allocator_class->alloc = GST_DEBUG_FUNCPTR (gst_amba_cavalry_phys_allocator_alloc);
  allocator_class->free = GST_DEBUG_FUNCPTR (gst_amba_cavalry_phys_allocator_free);
}

static void
gst_amba_cavalry_phys_allocator_init (GstAmbaCavalryPhysAllocator * allocator)
{
  GstAllocator *base_alloc = GST_ALLOCATOR_CAST (allocator);

  gst_amba_cavalry_allocator_init_once ();

  if (cavalry_mem_get_fd () < 0) {
    GST_ERROR_OBJECT (allocator,
        "cavalry_mem not initialized; default amba_cavalry allocator must load first");
    return;
  }

  gst_object_set_name (GST_OBJECT_CAST (base_alloc), GST_ALLOCATOR_AMBA_CAVALRY_PHYS);
  base_alloc->mem_type = GST_ALLOCATOR_AMBA_CAVALRY_PHYS;
  base_alloc->mem_map_full = GST_DEBUG_FUNCPTR (gst_amba_cavalry_phys_mem_map_full);
  base_alloc->mem_unmap_full = GST_DEBUG_FUNCPTR (gst_amba_cavalry_phys_mem_unmap_full);
  GST_OBJECT_FLAG_SET (allocator, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
  GST_INFO ("gst_amba_cavalry_phys_allocator inited.\n");
}

void
gst_amba_cavalry_phys_allocator_init_once (void)
{
  static gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    GstAllocator *alloc;

    if (!gst_amba_cavalry_phys_base_quark) {
      gst_amba_cavalry_phys_base_quark =
          g_quark_from_static_string ("gst-amba-cavalry-phys-base");
    }

    gst_amba_cavalry_allocator_init_once ();

    alloc = (GstAllocator *) g_object_new (GST_TYPE_AMBA_CAVALRY_PHYS_ALLOCATOR, NULL);
    gst_object_ref_sink (alloc);
    gst_allocator_register (GST_ALLOCATOR_AMBA_CAVALRY_PHYS, alloc);

    g_once_init_leave (&_init, 1);
  }
}

GstAllocator *
gst_amba_cavalry_phys_allocator_get (void)
{
  gst_amba_cavalry_phys_allocator_init_once ();
  return gst_allocator_find (GST_ALLOCATOR_AMBA_CAVALRY_PHYS);
}

gboolean
gst_is_amba_cavalry_allocator_family (GstAllocator * alloc)
{
  if (!alloc)
    return FALSE;
  return GST_IS_AMBA_CAVALRY_ALLOCATOR (alloc)
      || GST_IS_AMBA_CAVALRY_PHYS_ALLOCATOR (alloc);
}

guint64
gst_amba_cavalry_memory_get_phys_base (GstMemory * mem)
{
  GstAmbaCavalryPhysMemory *pm;

  pm = gst_amba_cavalry_phys_memory_cast (mem);
  if (pm && pm->ctx)
    return (guint64) pm->ctx->phys;

  return 0;
}

gint
gst_amba_cavalry_memory_get_fd (GstMemory * mem)
{
  g_return_val_if_fail (gst_is_amba_cavalry_memory (mem), -1);

  if (GST_IS_AMBA_CAVALRY_PHYS_ALLOCATOR (mem->allocator)) {
    GST_ERROR ("cavalry phy allocator can't get fd\n");
    return -1;
  }

  return gst_fd_memory_get_fd (mem);
}

gboolean
gst_is_amba_cavalry_memory (GstMemory * mem)
{
  if (!mem || !mem->allocator)
    return FALSE;

  return GST_IS_AMBA_CAVALRY_ALLOCATOR (mem->allocator);
}

gboolean
gst_is_amba_cavalry_memory_phy (GstMemory * mem)
{
  if (!mem || !mem->allocator)
    return FALSE;

  return GST_IS_AMBA_CAVALRY_PHYS_ALLOCATOR (mem->allocator);
}

void
gst_amba_cavalry_allocator_init_once (void)
{
  static gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    GstAllocator *alloc;

    alloc = (GstAllocator *) g_object_new (GST_TYPE_AMBA_CAVALRY_ALLOCATOR, NULL);
    gst_object_ref_sink (alloc);
    gst_allocator_register (GST_ALLOCATOR_AMBA_CAVALRY, alloc);

    g_once_init_leave (&_init, 1);
  }
}

GstAllocator *
gst_amba_cavalry_allocator_get (void)
{
  return gst_allocator_find (GST_ALLOCATOR_AMBA_CAVALRY);
}
