/*
 * gst_amba_cavalry_bufferpool.c
 *
 * History:
 *    2025/06/24 - [Yang Yu] created file
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

#include "gst_amba_cavalry_bufferpool.h"
#include "gst_amba_cavalry_allocator.h"

#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>

#include "cavalry_ioctl.h"
#include "cavalry_mem.h"

GST_DEBUG_CATEGORY_STATIC (amba_cavalry_bufferpool_debug);
#define GST_CAT_DEFAULT amba_cavalry_bufferpool_debug

#define _do_init                                        \
    GST_DEBUG_CATEGORY_INIT (amba_cavalry_bufferpool_debug,      \
    "amba_cavalry_bufferpool", 0, "amba cavalry buffer pool");

/**
 * Structure to track individual buffer blocks within the contiguous memory
 */
typedef struct _GstAmbaCavalryBufferBlock {
  gsize offset;     /* Offset from the start of the contiguous memory */
  gsize size;       /* Size of this block */
  gboolean in_use;  /* TRUE if this block is currently allocated */
  gint index;       /* Block index for easy retrieval */
} GstAmbaCavalryBufferBlock;

/**
 * GstAmbaCavalryBufferPool:
 *
 * Buffer pool that allocates from a single contiguous cavalry memory block
 */
struct _GstAmbaCavalryBufferPool
{
  GstBufferPool parent;

  /*< private >*/
  gboolean use_contiguous_memory;

  /* Cavalry allocator - cached for efficiency */
  GstAllocator *cavalry_allocator;         /* Cached cavalry allocator */

  /* Contiguous memory management */
  GstMemory *contiguous_mem;        /* The large contiguous memory block */
  gsize contiguous_size;            /* Total size of contiguous memory */
  gsize buffer_size;                /* Size of each individual buffer (requested) */
  gsize aligned_buffer_size;        /* Size of each buffer after alignment, prefix and padding*/
  gint max_buffers;                 /* Maximum number of buffers */

  /* Mapped memory information */
  GstMapInfo contiguous_map_info;   /* Map info for the contiguous memory */
  gboolean contiguous_mapped;       /* TRUE if contiguous memory is mapped */

  /* Allocation parameters */
  GstAllocationParams alloc_params; /* Parameters for individual buffers */

  /* Buffer block management */
  GstAmbaCavalryBufferBlock *blocks; /* Array of buffer blocks */
  GMutex blocks_lock;               /* Lock for thread-safe block management */

  /* Memory to block mapping for this pool instance */
  GHashTable *memory_to_block_map;  /* Map GstMemory* to GstAmbaCavalryBufferBlock* */
  GMutex memory_map_lock;           /* Lock for memory mapping operations */

  /* Allocator */
  GstAllocator *allocator;

  gpointer _gst_reserved[GST_PADDING];
};


/* Add memory to block mapping for a specific pool */
static void
add_memory_block_mapping (GstAmbaCavalryBufferPool *pool, GstMemory *mem, GstAmbaCavalryBufferBlock *block)
{
  g_mutex_lock (&pool->memory_map_lock);
  g_hash_table_insert (pool->memory_to_block_map, mem, block);
  g_mutex_unlock (&pool->memory_map_lock);
}

/* Remove memory to block mapping for a specific pool */
static void
remove_memory_block_mapping (GstAmbaCavalryBufferPool *pool, GstMemory *mem)
{
  g_mutex_lock (&pool->memory_map_lock);
  g_hash_table_remove (pool->memory_to_block_map, mem);
  g_mutex_unlock (&pool->memory_map_lock);
}

/* Get block information from memory for a specific pool */
static GstAmbaCavalryBufferBlock *
get_memory_block_info (GstAmbaCavalryBufferPool *pool, GstMemory *mem)
{
  GstAmbaCavalryBufferBlock *block = NULL;

  if (!pool || !mem) {
    return NULL;
  }

  g_mutex_lock (&pool->memory_map_lock);
  block = g_hash_table_lookup (pool->memory_to_block_map, mem);
  g_mutex_unlock (&pool->memory_map_lock);

  return block;
}

/* GstAmbaCavalryBufferMeta implementation */
static gboolean
gst_amba_cavalry_buffer_meta_init (GstMeta * meta, gpointer params, GstBuffer * buffer)
{
  GstAmbaCavalryBufferMeta *cavalry_meta = (GstAmbaCavalryBufferMeta *) meta;

  /* Suppress unused parameter warnings */
  (void) params;
  (void) buffer;

  cavalry_meta->block_index = -1;
  cavalry_meta->block_offset = 0;
  cavalry_meta->block_size = 0;
  cavalry_meta->is_contiguous = FALSE;
  cavalry_meta->fd = -1;
  cavalry_meta->slab_phys_base = 0;

  return TRUE;
}

static gboolean
gst_amba_cavalry_buffer_meta_transform (GstBuffer * transbuf, GstMeta * meta,
    GstBuffer * buffer, GQuark type, gpointer data)
{
  GstAmbaCavalryBufferMeta *cavalry_meta = (GstAmbaCavalryBufferMeta *) meta;

  /* Suppress unused parameter warnings */
  (void) buffer;
  (void) data;

  /* We only transform the meta if it's a copy operation */
  if (GST_META_TRANSFORM_IS_COPY (type)) {
    gst_buffer_add_amba_cavalry_meta (transbuf, cavalry_meta->block_index,
        cavalry_meta->block_offset, cavalry_meta->block_size, cavalry_meta->is_contiguous,
        cavalry_meta->fd, cavalry_meta->slab_phys_base);
  }

  return TRUE;
}

GType
gst_amba_cavalry_buffer_meta_api_get_type (void)
{
  static gsize type = 0;
  static const gchar *tags[] = { "memory", NULL };

  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("GstAmbaCavalryBufferMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }
  return (GType) type;
}

const GstMetaInfo *
gst_amba_cavalry_buffer_meta_get_info (void)
{
  static const GstMetaInfo *meta_info = NULL;

  if (g_once_init_enter (&meta_info)) {
    const GstMetaInfo *info = gst_meta_register (GST_AMBA_CAVALRY_BUFFER_META_API_TYPE,
        "GstAmbaCavalryBufferMeta",
        sizeof (GstAmbaCavalryBufferMeta),
        gst_amba_cavalry_buffer_meta_init,
        NULL,
        gst_amba_cavalry_buffer_meta_transform);
    g_once_init_leave (&meta_info, info);
  }
  return meta_info;
}

GstAmbaCavalryBufferMeta *
gst_buffer_add_amba_cavalry_meta (GstBuffer * buffer, gint block_index,
    gsize block_offset, gsize block_size, gboolean is_contiguous, gint fd,
    guint64 slab_phys_base)
{
  GstAmbaCavalryBufferMeta *meta;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), NULL);

  meta = (GstAmbaCavalryBufferMeta *) gst_buffer_add_meta (buffer,
      GST_AMBA_CAVALRY_BUFFER_META_INFO, NULL);

  if (meta) {
    meta->block_index = block_index;
    meta->block_offset = block_offset;
    meta->block_size = block_size;
    meta->is_contiguous = is_contiguous;
    meta->fd = fd;
    meta->slab_phys_base = slab_phys_base;
  }

  return meta;
}

/* cppcheck-suppress unknownMacro */
G_DEFINE_TYPE_WITH_CODE (GstAmbaCavalryBufferPool, gst_amba_cavalry_buffer_pool,
    GST_TYPE_BUFFER_POOL, _do_init);

static gboolean gst_amba_cavalry_buffer_pool_start (GstBufferPool * pool);
static gboolean gst_amba_cavalry_buffer_pool_stop (GstBufferPool * pool);
static gboolean gst_amba_cavalry_buffer_pool_set_config (GstBufferPool * pool, GstStructure * config);
static GstFlowReturn gst_amba_cavalry_buffer_pool_alloc_buffer (GstBufferPool * pool, GstBuffer ** buffer, GstBufferPoolAcquireParams * params);
static void gst_amba_cavalry_buffer_pool_free_buffer (GstBufferPool * pool, GstBuffer * buffer);
/* acquire_buffer and release_buffer use parent implementation */

static void
gst_amba_cavalry_buffer_pool_finalize (GObject * object)
{
  GstAmbaCavalryBufferPool *self = GST_AMBA_CAVALRY_BUFFER_POOL (object);

  GST_DEBUG_OBJECT (self, "finalize");

  if (self->contiguous_mem) {
    if (self->contiguous_mapped) {
      gst_memory_unmap (self->contiguous_mem, &self->contiguous_map_info);
      self->contiguous_mapped = FALSE;
    }
    gst_memory_unref (self->contiguous_mem);
    self->contiguous_mem = NULL;
  }

  if (self->blocks) {
    /* Clean up any remaining memory mappings */
    gint i;
    for (i = 0; i < self->max_buffers; i++) {
      if (self->blocks[i].in_use) {
        GST_DEBUG_OBJECT (self, "cleaning up block %d mapping on pool destruction", i);
        /* Note: We don't need to explicitly remove mappings here as the hash table
         * will be cleaned up when the process exits, and individual mappings are
         * removed when buffers are freed normally */
      }
    }
    g_free (self->blocks);
    self->blocks = NULL;
  }

  if (self->allocator) {
    gst_object_unref (self->allocator);
    self->allocator = NULL;
  }

  if (self->cavalry_allocator) {
    gst_object_unref (self->cavalry_allocator);
    self->cavalry_allocator = NULL;
  }

  /* Clean up memory mapping hash table */
  if (self->memory_to_block_map) {
    g_hash_table_destroy (self->memory_to_block_map);
    self->memory_to_block_map = NULL;
  }

  g_mutex_clear (&self->blocks_lock);
  g_mutex_clear (&self->memory_map_lock);

  G_OBJECT_CLASS (gst_amba_cavalry_buffer_pool_parent_class)->finalize (object);
}

static void
gst_amba_cavalry_buffer_pool_class_init (GstAmbaCavalryBufferPoolClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBufferPoolClass *bufferpool_class = GST_BUFFER_POOL_CLASS (klass);

  gobject_class->finalize = gst_amba_cavalry_buffer_pool_finalize;

  bufferpool_class->start = gst_amba_cavalry_buffer_pool_start;
  bufferpool_class->stop = gst_amba_cavalry_buffer_pool_stop;
  bufferpool_class->set_config = gst_amba_cavalry_buffer_pool_set_config;
  bufferpool_class->alloc_buffer = gst_amba_cavalry_buffer_pool_alloc_buffer;
  bufferpool_class->free_buffer = gst_amba_cavalry_buffer_pool_free_buffer;
  /* acquire_buffer and release_buffer use parent implementation */
}

static void
gst_amba_cavalry_buffer_pool_init (GstAmbaCavalryBufferPool * self)
{
  GST_DEBUG_OBJECT (self, "init");

  self->use_contiguous_memory = FALSE;
  self->cavalry_allocator = NULL;
  self->contiguous_mem = NULL;
  self->contiguous_size = 0;
  self->buffer_size = 0;
  self->aligned_buffer_size = 0;
  self->max_buffers = 0;
  self->contiguous_mapped = FALSE;
  self->blocks = NULL;
  self->allocator = NULL;

  memset (&self->alloc_params, 0, sizeof (self->alloc_params));

  self->memory_to_block_map = g_hash_table_new (g_direct_hash, g_direct_equal);

  g_mutex_init (&self->blocks_lock);
  g_mutex_init (&self->memory_map_lock);

    /* Try to initialize cavalry allocator and detect availability */
  gst_amba_cavalry_allocator_init_once ();

  self->cavalry_allocator = gst_amba_cavalry_allocator_get ();
  if (self->cavalry_allocator) {
    GST_DEBUG_OBJECT (self, "cavalry allocator is available and cached");
  } else {
    GST_DEBUG_OBJECT (self, "cavalry allocator is not available");
  }
}

/* Get actual buffer spacing by using allocator's internal maxsize calculation */
static gsize
get_actual_aligned_buffer_size (GstAllocator *allocator, gsize size, GstAllocationParams *params)
{
  GstMemory *sample_mem;
  gsize maxsize, offset, current_size;

  /* Allocate a sample buffer to examine the allocator's behavior */
  sample_mem = gst_allocator_alloc (allocator, size, params);
  if (!sample_mem) {
    GST_WARNING ("Failed to allocate sample buffer for size calculation");
    return 0;
  }

  /* Get the sizes - the return value is current size, maxsize is returned via parameter */
  current_size = gst_memory_get_sizes (sample_mem, &offset, &maxsize);

  /* The actual aligned buffer size should be the maxsize */
  /* For cavalry allocator, this includes the fd memory overhead */
  gsize aligned_size = maxsize;

  GST_DEBUG ("Sample buffer: requested_size=%zu, current_size=%zu, maxsize=%zu, offset=%zu, aligned_size=%zu",
             size, current_size, maxsize, offset, aligned_size);

  /* Free the sample buffer */
  gst_memory_unref (sample_mem);

  return aligned_size;
}

static gboolean
gst_amba_cavalry_buffer_pool_set_config (GstBufferPool * pool, GstStructure * config)
{
  GstAmbaCavalryBufferPool *self = GST_AMBA_CAVALRY_BUFFER_POOL (pool);
  GstCaps *caps;
  guint size;
  guint min_buffers, max_buffers;
  GstAllocator *allocator;
  GstAllocationParams params;

  GST_DEBUG_OBJECT (self, "set config");

  if (!gst_buffer_pool_config_get_params (config, &caps, &size, &min_buffers, &max_buffers)) {
    GST_WARNING_OBJECT (self, "invalid config");
    return FALSE;
  }

  if (!gst_buffer_pool_config_get_allocator (config, &allocator, &params)) {
    GST_DEBUG_OBJECT (self, "no allocator in config, using default");
    allocator = NULL;
    memset (&params, 0, sizeof (params));
  }

  /* Configuration validation and allocator selection */

  /* Step 1: Priority-based allocator selection - contiguous mode has highest priority */
  GstAllocator *final_allocator = NULL;

  if (self->use_contiguous_memory) {
    /* Prefer allocator from pool config (e.g. phys vs mfd); else cached default */
    if (allocator && gst_is_amba_cavalry_allocator_family (allocator)) {
      final_allocator = gst_object_ref (allocator);
      GST_DEBUG_OBJECT (self, "contiguous mode: using allocator from pool config");
    } else if (self->cavalry_allocator) {
      final_allocator = gst_object_ref (self->cavalry_allocator);
      GST_DEBUG_OBJECT (self, "contiguous mode: using cached cavalry allocator");
    } else {
      GST_WARNING_OBJECT (self, "contiguous memory mode requires cavalry allocator, "
          "but cavalry allocator is not available. Falling back to normal mode with provided/default allocator.");
      self->use_contiguous_memory = FALSE;
    }
  }

  /* Step 2: If not using contiguous mode or cavalry allocator not available, use provided or default */
  if (!final_allocator) {
    if (allocator) {
      final_allocator = gst_object_ref (allocator);
      GST_DEBUG_OBJECT (self, "using provided allocator: %s", G_OBJECT_TYPE_NAME (allocator));
    } else if (self->cavalry_allocator) {
      final_allocator = gst_object_ref (self->cavalry_allocator);
      GST_DEBUG_OBJECT (self, "using cached cavalry allocator as default");
    } else {
      GST_DEBUG_OBJECT (self, "no allocator specified and cavalry allocator not available, will use system default");
    }
  }

  /* Step 3: Validate and adjust buffer pool configuration for contiguous mode */
  if (self->use_contiguous_memory) {
    gboolean config_updated = FALSE;

    /* Ensure min_buffers is at least 1 for contiguous mode */
    if (min_buffers == 0) {
      GST_WARNING_OBJECT (self, "contiguous memory mode requires min_buffers > 0 "
          "for proper buffer management. Setting min_buffers to 1.");
      min_buffers = 1;
      config_updated = TRUE;
    }

    /* Ensure max_buffers is at least 2 for contiguous mode buffer reuse */
    if (max_buffers != 0 && max_buffers < 2) {
      GST_WARNING_OBJECT (self, "contiguous memory mode with max_buffers (%u) < 2 "
          "prevents proper buffer reuse. Setting max_buffers to 2.", max_buffers);
      max_buffers = 2;
      config_updated = TRUE;
    }

    /* For contiguous memory, we need a reasonable max_buffers limit */
    if (max_buffers == 0) {
      GST_WARNING_OBJECT (self, "contiguous memory mode with unlimited max_buffers (0) "
          "is not recommended. Setting max_buffers to 8 for contiguous mode.");
      max_buffers = 8;
      config_updated = TRUE;
    }

    /* Update the config if any changes were made */
    if (config_updated) {
      gst_buffer_pool_config_set_params (config, caps, size, min_buffers, max_buffers);
    }
  }

  /* Step 4: Set the final allocator */
  if (self->allocator != final_allocator) {
    gst_clear_object (&self->allocator);
    self->allocator = final_allocator; /* Transfer ownership */
  } else {
    /* Same allocator, just release our temporary reference */
    if (final_allocator) {
      gst_object_unref (final_allocator);
    }
  }

  /* Store the allocation parameters from the caller */
  self->alloc_params = params;
  self->buffer_size = (gsize) size;
  self->max_buffers = (gint) max_buffers;

  /* Get actual aligned buffer size only for contiguous mode */
  if (self->use_contiguous_memory) {
    /* For contiguous mode, we need to calculate aligned size */
    self->aligned_buffer_size = get_actual_aligned_buffer_size (self->allocator, (gsize) size, &params);
    if (self->aligned_buffer_size == 0) {
      GST_ERROR_OBJECT (self, "Failed to determine aligned buffer size for contiguous mode");
      return FALSE;
    }
  } else {
    /* For normal mode, use the original size */
    self->aligned_buffer_size = (gsize) size;
  }

  GST_DEBUG_OBJECT (self, "config validated: size=%zu, aligned_size=%zu, min=%u, max=%u, contiguous=%d, align=%zu",
      (gsize) size, self->aligned_buffer_size, min_buffers, max_buffers, self->use_contiguous_memory, params.align);

  return GST_BUFFER_POOL_CLASS (gst_amba_cavalry_buffer_pool_parent_class)->set_config (pool, config);
}

static gboolean
gst_amba_cavalry_buffer_pool_start (GstBufferPool * pool)
{
  GstAmbaCavalryBufferPool *self = GST_AMBA_CAVALRY_BUFFER_POOL (pool);
  gboolean ret;

  GST_DEBUG_OBJECT (self, "start");

  if (self->use_contiguous_memory && self->max_buffers > 0 && self->aligned_buffer_size > 0) {
    /* Double-check: contiguous mode requires cavalry allocator */
    if (!self->allocator || !gst_is_amba_cavalry_allocator_family (self->allocator)) {
      GST_ERROR_OBJECT (self, "contiguous memory mode enabled but no cavalry allocator available. "
          "This should have been caught in set_config. Disabling contiguous mode.");
      self->use_contiguous_memory = FALSE;
    } else {
      /* For contiguous memory mode, allocate the large memory block first */
      GstAllocationParams params = { 0, };
      gsize total_size;
      gint i;

      /* Calculate total size */
      total_size = self->max_buffers * self->aligned_buffer_size;

      GST_DEBUG_OBJECT (self, "allocating contiguous memory: total_size=%zu, buffer_size=%zu, aligned_buffer_size=%zu, max_buffers=%d",
          total_size, self->buffer_size, self->aligned_buffer_size, self->max_buffers);

      /* Allocate the contiguous memory */
      self->contiguous_mem = gst_allocator_alloc (self->allocator, total_size, &params);
    if (!self->contiguous_mem) {
      GST_ERROR_OBJECT (self, "failed to allocate contiguous memory");
      return FALSE;
    }

    /* Map the contiguous memory once and keep it mapped */
    if (!gst_memory_map (self->contiguous_mem, &self->contiguous_map_info, GST_MAP_READWRITE)) {
      GST_ERROR_OBJECT (self, "failed to map contiguous memory");
      gst_memory_unref (self->contiguous_mem);
      self->contiguous_mem = NULL;
      return FALSE;
    }
    self->contiguous_mapped = TRUE;
    self->contiguous_size = total_size;

    /* Initialize buffer blocks */
    self->blocks = g_new0 (GstAmbaCavalryBufferBlock, self->max_buffers);
    for (i = 0; i < self->max_buffers; i++) {
      self->blocks[i].offset = i * self->aligned_buffer_size;
      self->blocks[i].size = self->buffer_size;
      self->blocks[i].in_use = FALSE;
      self->blocks[i].index = i;
    }

    GST_INFO_OBJECT (self, "contiguous memory allocated and mapped successfully");
    }
  }

  /* Call parent start which will pre-allocate buffers */
  ret = GST_BUFFER_POOL_CLASS (gst_amba_cavalry_buffer_pool_parent_class)->start (pool);
  if (!ret) {
    GST_ERROR_OBJECT (self, "parent start failed");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_amba_cavalry_buffer_pool_stop (GstBufferPool * pool)
{
  GstAmbaCavalryBufferPool *self = GST_AMBA_CAVALRY_BUFFER_POOL (pool);

  GST_DEBUG_OBJECT (self, "stop");

  if (self->contiguous_mem) {
    if (self->contiguous_mapped) {
      gst_memory_unmap (self->contiguous_mem, &self->contiguous_map_info);
      self->contiguous_mapped = FALSE;
    }
    gst_memory_unref (self->contiguous_mem);
    self->contiguous_mem = NULL;
  }

  if (self->blocks) {
    g_free (self->blocks);
    self->blocks = NULL;
  }

  return GST_BUFFER_POOL_CLASS (gst_amba_cavalry_buffer_pool_parent_class)->stop (pool);
}

/* Handle memory cleanup when buffer is freed */
static void
handle_contiguous_memory_cleanup (GstAmbaCavalryBufferPool *pool, GstMemory *mem)
{
  GstAmbaCavalryBufferBlock *block;

  if (!pool || !mem) {
    return;
  }

  /* Get block info from our mapping */
  block = get_memory_block_info (pool, mem);
  if (block) {
    /* Use lock to ensure thread safety when modifying block state */
    g_mutex_lock (&pool->blocks_lock);
    block->in_use = FALSE;
    g_mutex_unlock (&pool->blocks_lock);

    GST_DEBUG_OBJECT (pool, "freed contiguous memory block %d at offset %zu", block->index, block->offset);
  }

  /* Remove the mapping */
  remove_memory_block_mapping (pool, mem);
}

static GstMemory *
gst_amba_cavalry_buffer_pool_alloc_contiguous_memory (GstAmbaCavalryBufferPool * self)
{
  GstMemory *mem = NULL;
  gint i;
  gpointer data;

  g_mutex_lock (&self->blocks_lock);

  /* Check if contiguous memory is mapped */
  if (!self->contiguous_mapped) {
    GST_ERROR_OBJECT (self, "contiguous memory is not mapped");
    g_mutex_unlock (&self->blocks_lock);
    return NULL;
  }

  /* Find an available block - always start from 0 for consistent reuse */
  for (i = 0; i < self->max_buffers; i++) {
    if (!self->blocks[i].in_use) {
      self->blocks[i].in_use = TRUE;

      /* Calculate the address for this block */
      data = (gpointer)((guint8*)self->contiguous_map_info.data + self->blocks[i].offset);

      /* Create a new wrapped memory object for this block */
      mem = gst_memory_new_wrapped (GST_MEMORY_FLAG_NO_SHARE,
                                   data,                                    /* data at pre-calculated offset */
                                   self->aligned_buffer_size,               /* maxsize (full aligned size) */
                                   0,                                       /* offset within this memory */
                                   self->buffer_size,                       /* size (usable size) */
                                   gst_object_ref (self),                   /* user_data: pool reference */
                                   (GDestroyNotify) gst_object_unref);      /* notify: unref pool */

      if (mem) {
        /* Add mapping from memory to block info */
        add_memory_block_mapping (self, mem, &self->blocks[i]);
        GST_DEBUG_OBJECT (self, "allocated contiguous memory block %d, offset=%zu, usable_size=%zu, aligned_size=%zu, data=%p",
            i, self->blocks[i].offset, self->buffer_size, self->aligned_buffer_size, data);
      } else {
        self->blocks[i].in_use = FALSE;
        GST_ERROR_OBJECT (self, "failed to create wrapped memory for block %d", i);
      }
      break;
    }
  }

  g_mutex_unlock (&self->blocks_lock);

  if (!mem) {
    GST_WARNING_OBJECT (self, "no available contiguous memory blocks");
  }

  return mem;
}



static GstFlowReturn
gst_amba_cavalry_buffer_pool_alloc_buffer (GstBufferPool * pool, GstBuffer ** buffer, GstBufferPoolAcquireParams * params)
{
  GstAmbaCavalryBufferPool *self = GST_AMBA_CAVALRY_BUFFER_POOL (pool);
  GstBuffer *buf;
  GstMemory *mem;
  GstAmbaCavalryBufferMeta *meta;
  gint block_index = -1;
  gsize block_offset = 0;
  gint fd = -1;
  guint64 slab_phys = 0;
  /* Suppress unused parameter warning */
  (void) params;

  GST_DEBUG_OBJECT (self, "alloc buffer");

  buf = gst_buffer_new ();
  if (!buf) {
    GST_ERROR_OBJECT (self, "failed to create buffer");
    return GST_FLOW_ERROR;
  }

  if (self->use_contiguous_memory && self->contiguous_mem) {
    /* Allocate from contiguous memory */
    mem = gst_amba_cavalry_buffer_pool_alloc_contiguous_memory (self);
    if (!mem) {
      gst_buffer_unref (buf);
      return GST_FLOW_ERROR;
    }

    /* For contiguous memory, find the block index from our mapping */
    GstAmbaCavalryBufferBlock *block_ptr = get_memory_block_info (self, mem);
    if (block_ptr) {
      block_index = block_ptr->index;
      block_offset = block_ptr->offset;
    }

    /* Get the FD / slab PA from the contiguous memory for metadata.
     * mfd (amba_cavalry): gst_is_amba_cavalry_memory -> fd + optional phys.
     * phys (amba_cavalry_phys): no dmabuf fd; slab_phys for IAV GDMA dst_use_phys. */
    if (gst_is_amba_cavalry_memory (self->contiguous_mem)) {
      fd = gst_amba_cavalry_memory_get_fd (self->contiguous_mem);
      if (gst_amba_cavalry_memory_get_phys_base (self->contiguous_mem) != 0)
        slab_phys = gst_amba_cavalry_memory_get_phys_base (self->contiguous_mem)
            + (guint64) block_offset;
    } else if (gst_amba_cavalry_memory_get_phys_base (self->contiguous_mem) != 0) {
      slab_phys = gst_amba_cavalry_memory_get_phys_base (self->contiguous_mem)
          + (guint64) block_offset;
    }

    /* Add meta with contiguous memory information including FD */
    meta = gst_buffer_add_amba_cavalry_meta (buf, block_index, block_offset,
        self->aligned_buffer_size, TRUE, fd, slab_phys);
  } else {
    /* Allocate individual memory using the stored allocation parameters */
    mem = gst_allocator_alloc (self->allocator, self->buffer_size, &self->alloc_params);
    if (!mem) {
      GST_ERROR_OBJECT (self, "failed to allocate memory");
      gst_buffer_unref (buf);
      return GST_FLOW_ERROR;
    }

    /* Get FD / slab PA for mfd cavalry or phys-only pool buffers */
    if (gst_is_amba_cavalry_memory (mem)) {
      fd = gst_amba_cavalry_memory_get_fd (mem);
      if (gst_amba_cavalry_memory_get_phys_base (mem) != 0) {
        gsize off = 0, sz = 0;

        gst_memory_get_sizes (mem, &off, &sz);
        (void) sz;
        slab_phys = gst_amba_cavalry_memory_get_phys_base (mem) + (guint64) off;
      }
    } else if (gst_amba_cavalry_memory_get_phys_base (mem) != 0) {
      gsize off = 0, sz = 0;

      gst_memory_get_sizes (mem, &off, &sz);
      (void) sz;
      slab_phys = gst_amba_cavalry_memory_get_phys_base (mem) + (guint64) off;
    }

    /* Add meta with individual memory information including FD */
    meta = gst_buffer_add_amba_cavalry_meta (buf, -1, 0, self->buffer_size, FALSE, fd,
        slab_phys);
  }

  if (!meta) {
    GST_WARNING_OBJECT (self, "failed to add cavalry buffer meta");
  }

  gst_buffer_append_memory (buf, mem);
  *buffer = buf;

  GST_DEBUG_OBJECT (self, "allocated buffer %p with memory %p (contiguous:%s, fd:%d, block_index:%d, offset:%zu)",
      buf, mem, self->use_contiguous_memory ? "yes" : "no", fd, block_index, block_offset);

  return GST_FLOW_OK;
}

static void
gst_amba_cavalry_buffer_pool_free_buffer (GstBufferPool * pool, GstBuffer * buffer)
{
  GstAmbaCavalryBufferPool *self = GST_AMBA_CAVALRY_BUFFER_POOL (pool);

  GST_DEBUG_OBJECT (self, "free buffer %p", buffer);

  if (self->use_contiguous_memory && self->contiguous_mem) {
    /* Handle contiguous memory cleanup */
    GstMemory *mem = gst_buffer_peek_memory (buffer, 0);
    if (mem) {
      handle_contiguous_memory_cleanup (self, mem);
    }
  }

  gst_buffer_unref (buffer);
}


/*
 * Public API
 */

GstBufferPool *
gst_amba_cavalry_buffer_pool_new (void)
{
  GstAmbaCavalryBufferPool *self;

  self = g_object_new (GST_TYPE_AMBA_CAVALRY_BUFFER_POOL, NULL);
  gst_object_ref_sink (self);

  return GST_BUFFER_POOL_CAST (self);
}

void
gst_amba_cavalry_buffer_pool_set_contiguous_memory (GstAmbaCavalryBufferPool * pool, gboolean enable)
{
  g_return_if_fail (GST_IS_AMBA_CAVALRY_BUFFER_POOL (pool));

  pool->use_contiguous_memory = enable;
  GST_DEBUG_OBJECT (pool, "contiguous memory: %s", enable ? "enabled" : "disabled");
}

gboolean
gst_amba_cavalry_buffer_pool_get_contiguous_memory (GstAmbaCavalryBufferPool * pool)
{
  g_return_val_if_fail (GST_IS_AMBA_CAVALRY_BUFFER_POOL (pool), FALSE);

  return pool->use_contiguous_memory;
}

gint
gst_amba_cavalry_buffer_get_fd (GstBuffer * buffer)
{
  GstAmbaCavalryBufferMeta *meta;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), -1);

  meta = gst_buffer_get_amba_cavalry_meta (buffer);
  if (meta) {
    return meta->fd;
  }

  /* Fallback: try to get FD directly from the first memory if it's cavalry memory */
  if (gst_buffer_n_memory (buffer) > 0) {
    GstMemory *mem = gst_buffer_peek_memory (buffer, 0);
    if (gst_is_amba_cavalry_memory (mem)) {
      return gst_amba_cavalry_memory_get_fd (mem);
    }
  }

  return -1;
}

gint
gst_amba_cavalry_buffer_get_block_index (GstBuffer * buffer)
{
  GstAmbaCavalryBufferMeta *meta;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), -1);

  meta = gst_buffer_get_amba_cavalry_meta (buffer);
  if (meta) {
    return meta->block_index;
  }

  return -1;
}

gsize
gst_amba_cavalry_buffer_get_block_offset (GstBuffer * buffer)
{
  GstAmbaCavalryBufferMeta *meta;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), 0);

  meta = gst_buffer_get_amba_cavalry_meta (buffer);
  if (meta) {
    return meta->block_offset;
  }

  return 0;
}

gsize
gst_amba_cavalry_buffer_get_block_size (GstBuffer * buffer)
{
  GstAmbaCavalryBufferMeta *meta;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), 0);

  meta = gst_buffer_get_amba_cavalry_meta (buffer);
  if (meta) {
    return meta->block_size;
  }

  return 0;
}

gboolean
gst_amba_cavalry_buffer_is_contiguous (GstBuffer * buffer)
{
  GstAmbaCavalryBufferMeta *meta;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), FALSE);

  meta = gst_buffer_get_amba_cavalry_meta (buffer);
  if (meta) {
    return meta->is_contiguous;
  }

  return FALSE;
}

gboolean
gst_amba_cavalry_buffer_has_meta (GstBuffer * buffer)
{
  g_return_val_if_fail (GST_IS_BUFFER (buffer), FALSE);

  return gst_buffer_get_amba_cavalry_meta (buffer) != NULL;
}

guint64
gst_amba_cavalry_buffer_get_slab_phys (GstBuffer * buffer)
{
  GstAmbaCavalryBufferMeta *meta;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), 0);

  meta = gst_buffer_get_amba_cavalry_meta (buffer);
  if (meta)
    return meta->slab_phys_base;

  return 0;
}
