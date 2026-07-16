/*
 * gstmlinference2.c
 *
 * History:
 *    1/6/2026 - [pxduan] created file
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

/**
 * SECTION:element-mlinference2
 * @title: mlinference2
 * @see_also: mlpostprocess
 *
 * Runs ML inference with cvflow (no built-in preprocess/postprocess).
 * Preprocess: videoscale/videoconvert or amba_videoscale/amba_img_cvt.
 * Postprocess: mlpostprocess (type=xxx, see mlpostprocess registry).
 *
 * Output: application/x-amba-ml-tensors (single GstMemory, float16/float32).
 * Downstream: mlpostprocess -> amba_draw_data_gen (video overlay).
 *
 * <refsect2>
 * <title>Example launch line</title>
 * Live mode: camsrc/camsrc2 -> videoconvertscale -> mlinference2 -> mlpostprocess -> amba_draw_data_gen -> amba_overlay_draw
 * |[
 * gst-launch-1.0 amba_camsrc buf-id=0 ! queue ! videoconvert ! videoscale ! video/x-raw,format=RGBP,width=416,height=416 ! \
 * mlinference2 in_name=images out_name=1037+1017+997 model=/tmp/nn/model/cv75/onnx_yolov5s_cavalry.bin ! queue ! \
 * mlpostprocess type=yolov5 label=/tmp/nn/in/coco_class_names.txt coord_res=1920x1080 conf_threshold=0.25 nms=0.45 top_k=100 ! \
 * amba_draw_data_gen ! amba_overlay_draw stream_id=0 sync_pts=1 osd_offset=0 osd_size=4163584
 * ]|
 * Live mode: camsrc2 -> amba_videoscale -> amba_img_cvt -> mlinference2 -> mlpostprocess -> amba_draw_data_gen -> amba_overlay_draw
 * |[
 * gst-launch-1.0 amba_camsrc2 buf-id=0 ! amba_videoscale ! video/x-raw,width=416,height=416 ! queue ! amba_img_cvt ! video/x-raw,format=RGBP ! queue ! \
 * mlinference2 in_name=images out_name=1037+1017+997 model=/tmp/nn/model/cv75/onnx_yolov5s_cavalry.bin ! queue ! \
 * mlpostprocess type=yolov5 label=/tmp/nn/in/coco_class_names.txt coord_res=1920x1080 conf_threshold=0.25 nms=0.45 top_k=100 ! queue ! \
 * amba_draw_data_gen ! amba_overlay_draw stream_id=0 sync_pts=1 osd_offset=0 osd_size=4163584
 * ]|
 * File mode: filesrc -> videoconvertscale-> mlinference2 -> mlpostprocess -> amba_draw_data_gen (output-mode=video) ->compositor -> filesink
 * |[
 * gst-launch-1.0 filesrc location=/tmp/bitstream-files/IMX490_road_8_1080p.mp4 num-buffers=300 ! qtdemux name=demux demux. ! queue ! \
 * h264parse ! avdec_h264 ! queue ! videoconvert ! videoscale ! video/x-raw,format=ARGB,width=1920,height=1080,pixel-aspect-ratio=1/1 ! \
 * tee name=t t. ! queue ! comp.sink_0 t. ! queue ! videoconvert ! videoscale ! video/x-raw,format=RGBP,width=416,height=416 ! \
 * mlinference2 in_name=images out_name=997+1017+1037 model=/tmp/nn/model/cv75/onnx_yolov5s_cavalry.bin ! queue ! \
 * mlpostprocess type=yolov5 label=/tmp/nn/in/coco_class_names.txt coord_res=1920x1080 conf_threshold=0.25 nms=0.45 top_k=100 ! \
 * amba_draw_data_gen output-mode=video ! comp.sink_1 compositor name=comp sink_0::zorder=1 sink_0::xpos=0 sink_0::ypos=0 sink_0::width=1920 sink_0::height=1080 \
 * sink_1::zorder=2 sink_1::xpos=0 sink_1::ypos=0 sink_1::width=1920 sink_1::height=1080 ! videoconvert ! openh264enc ! h264parse ! mp4mux ! filesink location=/tmp/output_yolov5.mp4
 * ]|
 * </refsect2>
 */

#include <gst/video/gstvideometa.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <stdint.h>
#include <stdio.h>
#include <ctype.h>
#include <wchar.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <linux/fb.h>
#include <fcntl.h>
#include <dirent.h>
#include <pthread.h>
#include <math.h>
#include <float.h>

#include "cavalry_mem.h"
#include "nnctrl.h"
#include "vproc.h"

#include "internal.h"
#include "debug_log.h"
#include "gstmlinference2.h"
#include "gst_amba_pitch_align.h"
#include "gst_amba_cavalry_allocator.h"
#include "gst_amba_cavalry_bufferpool.h"
#include "ml_tensors_caps.h"
#include "amba_private_data.h"

GST_DEBUG_CATEGORY_STATIC (gst_ml_inference2_debug);
#define GST_CAT_DEFAULT gst_ml_inference2_debug

/* Allocate cavalry memory with share_to_dsp for CV7/V6 (required for CAVALRY_RUN_DAGS_MFD) */
static int cavalry_mem_alloc_mfd_cv(unsigned long size, int *fd, void **virt, int cache_en)
{
  struct cavalry_mem_attr attr;
  memset(&attr, 0, sizeof(attr));
  attr.cache_en = cache_en ? 1 : 0;
#ifdef BUILD_DSP_AMBA_V6
  attr.share_to_dsp = 1;  /* CV7/V6: CV+DSP+ARM shared memory for dmabuf */
#endif
  return cavalry_mem_alloc_with_attr_mfd(size, fd, virt, &attr);
}

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_IN_NAME,
  PROP_OUT_NAME,
  PROP_MODEL,
  PROP_IN_DATA_FMT,
  PROP_USE_MFD
};

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, "
                     "format=(string)RGBP, "
                     "width=(int)[1,4096], "
                     "height=(int)[1,4096]")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    /* cppcheck-suppress unknownMacro */
    GST_STATIC_CAPS (GST_AMBA_ML_TENSORS_CAPS ", "
                     "num_tensors=(int)[1," G_STRINGIFY(AMBA_ML_MAX_TENSORS) "]")
    );

#define gst_ml_inference2_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstMlInference2, gst_ml_inference2, GST_TYPE_BASE_TRANSFORM,
  GST_DEBUG_CATEGORY_INIT(gst_ml_inference2_debug, "mlinference2", 0,
  "mlinference2"));

static void gst_ml_inference2_finalize (GObject * object);
static void gst_ml_inference2_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_ml_inference2_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static gboolean gst_ml_inference2_start (GstBaseTransform *trans);
static gboolean gst_ml_inference2_stop (GstBaseTransform *trans);
static gboolean gst_ml_inference2_propose_allocation (GstBaseTransform *trans, GstQuery *decide_query, GstQuery *query);
static gboolean gst_ml_inference2_decide_allocation (GstBaseTransform *trans, GstQuery *query);
static GstFlowReturn gst_ml_inference2_prepare_output_buffer (GstBaseTransform *trans, GstBuffer *inbuf, GstBuffer **outbuf);
static GstCaps * gst_ml_inference2_transform_caps (GstBaseTransform *trans, GstPadDirection direction, GstCaps *caps, GstCaps *filter);
static GstFlowReturn gst_ml_inference2_transform (GstBaseTransform *
    trans, GstBuffer * inbuf, GstBuffer * outbuf);
static gboolean gst_ml_inference2_create_session (GstBaseTransform * trans);
static gboolean gst_ml_inference2_process (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf);

static inline guint
ml2_input_num (priv_ml_infer_ctx2_t *ctx)
{
  return ctx->use_mfd ? ctx->input_cfg.in_num : ctx->io_input_cfg.in_num;
}

static inline guint
ml2_output_num (priv_ml_infer_ctx2_t *ctx)
{
  return ctx->use_mfd ? ctx->output_cfg.out_num : ctx->io_output_cfg.out_num;
}

static inline struct io_dim *
ml2_in_dim (priv_ml_infer_ctx2_t *ctx, guint i)
{
  return ctx->use_mfd
      ? &ctx->input_cfg.in_desc[i].dim
      : &ctx->io_input_cfg.in_desc[i].dim;
}

static inline struct io_dim *
ml2_out_dim (priv_ml_infer_ctx2_t *ctx, guint i)
{
  return ctx->use_mfd
      ? &ctx->output_cfg.out_desc[i].dim
      : &ctx->io_output_cfg.out_desc[i].dim;
}

static inline struct io_data_fmt *
ml2_out_data_fmt (priv_ml_infer_ctx2_t *ctx, guint i)
{
  return ctx->use_mfd
      ? &ctx->output_cfg.out_desc[i].data_fmt
      : &ctx->io_output_cfg.out_desc[i].data_fmt;
}

static inline unsigned long
ml2_out_size (priv_ml_infer_ctx2_t *ctx, guint i)
{
  return ctx->use_mfd
      ? ctx->output_cfg.out_desc[i].size
      : ctx->io_output_cfg.out_desc[i].size;
}

static inline unsigned long
ml2_in_size (priv_ml_infer_ctx2_t *ctx, guint i)
{
  return ctx->use_mfd
      ? ctx->input_cfg.in_desc[i].size
      : ctx->io_input_cfg.in_desc[i].size;
}

static inline gsize
ml2_output_alloc_size (priv_ml_infer_ctx2_t *ctx)
{
  gsize total = 0;
  guint n;

  if (!ctx)
    return 0;

  for (n = 0; n < ml2_output_num (ctx); n++)
    total += ml2_out_size (ctx, n);

  return total;
}

#define ML2_OUTPUT_POOL_MIN_BUFFERS  2

static GstAllocator *
ml2_output_allocator (priv_ml_infer_ctx2_t *ctx)
{
  if (!ctx)
    return NULL;

  if (!ctx->use_mfd)
    return gst_amba_cavalry_phys_allocator_get ();

  return gst_amba_cavalry_allocator_get ();
}

static void
ml2_output_pool_destroy (GstMlInference2 *self)
{
  if (!self)
    return;

  GST_OBJECT_LOCK (self);
  if (self->out_pool) {
    gst_buffer_pool_set_active (self->out_pool, FALSE);
    gst_object_unref (self->out_pool);
    self->out_pool = NULL;
  }
  self->out_pool_size = 0;
  GST_OBJECT_UNLOCK (self);
}

static void
ml2_output_buffer_strip_meta (GstBuffer *buf)
{
  GstMeta *meta;
  gpointer state = NULL;

  if (!buf)
    return;

  while ((meta = gst_buffer_iterate_meta (buf, &state)) != NULL) {
    gst_buffer_remove_meta (buf, meta);
    state = NULL;
  }
}

static void
ml2_copy_buffer_timestamps (GstBuffer *outbuf, GstBuffer *inbuf)
{
  if (!outbuf || !inbuf)
    return;

  if (GST_BUFFER_PTS_IS_VALID (inbuf))
    GST_BUFFER_PTS (outbuf) = GST_BUFFER_PTS (inbuf);
  if (GST_BUFFER_DTS_IS_VALID (inbuf))
    GST_BUFFER_DTS (outbuf) = GST_BUFFER_DTS (inbuf);
  if (GST_BUFFER_DURATION_IS_VALID (inbuf))
    GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (inbuf);
  GST_BUFFER_FLAGS (outbuf) = GST_BUFFER_FLAGS (inbuf);
}

/* (Re)create output pool when NN output size or allocator path changes.
 * @force_size: if >0 and NN not loaded yet, use for pool buffer size (caps estimate). */
static gboolean
ml2_output_pool_ensure (GstMlInference2 *self, GstCaps *caps, gsize force_size)
{
  priv_ml_infer_ctx2_t *filter;
  GstAllocator *alloc;
  GstBufferPool *pool;
  GstStructure *config;
  GstCaps *pool_caps = NULL;
  gsize size;

  g_return_val_if_fail (self != NULL, FALSE);

  filter = self->priv_ctx;
  if (!filter)
    return FALSE;

  if (ml2_output_num (filter) > 0) {
    size = ml2_output_alloc_size (filter);
  } else if (force_size > 0) {
    size = force_size;
  } else {
    return FALSE;
  }

  if (size == 0)
    return FALSE;

  GST_OBJECT_LOCK (self);

  if (self->out_pool && self->out_pool_size == size
      && gst_buffer_pool_is_active (self->out_pool)) {
    GST_OBJECT_UNLOCK (self);
    return TRUE;
  }

  if (self->out_pool) {
    gst_buffer_pool_set_active (self->out_pool, FALSE);
    gst_object_unref (self->out_pool);
    self->out_pool = NULL;
    self->out_pool_size = 0;
  }

  alloc = ml2_output_allocator (filter);
  if (!alloc) {
    if (!filter->use_mfd) {
      GST_ERROR_OBJECT (self,
          "amba_cavalry_phys allocator unavailable (legacy does not use mfd)");
    } else {
      GST_ERROR_OBJECT (self, "amba_cavalry allocator unavailable");
    }
    GST_OBJECT_UNLOCK (self);
    return FALSE;
  }

  pool = gst_buffer_pool_new ();
  config = gst_buffer_pool_get_config (pool);

  if (caps && gst_caps_get_size (caps) > 0) {
    pool_caps = gst_caps_ref (caps);
  } else {
    pool_caps = gst_caps_from_string ("application/x-amba-ml-tensors");
  }

  gst_buffer_pool_config_set_params (config, pool_caps, (guint) size,
      ML2_OUTPUT_POOL_MIN_BUFFERS, 0);
  gst_buffer_pool_config_set_allocator (config, alloc, NULL);
  gst_object_unref (alloc);
  gst_caps_unref (pool_caps);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_WARNING_OBJECT (self, "output pool set_config failed");
    gst_object_unref (pool);
    GST_OBJECT_UNLOCK (self);
    return FALSE;
  }

  if (!gst_buffer_pool_set_active (pool, TRUE)) {
    GST_WARNING_OBJECT (self, "output pool set_active failed");
    gst_object_unref (pool);
    GST_OBJECT_UNLOCK (self);
    return FALSE;
  }

  self->out_pool = pool;
  self->out_pool_size = size;
  GST_OBJECT_UNLOCK (self);

  GST_DEBUG_OBJECT (self, "output pool ready size=%" G_GSIZE_FORMAT, size);
  return TRUE;
}

/* Bind legacy NN outputs to Gst cavalry buffer (virt + phys). Writes run_out only. */
static gboolean
ml2_legacy_bind_output_zero_copy (GstMlInference2 *self, priv_ml_infer_ctx2_t *filter,
    GstBuffer *outbuf, GstMemory *output_mem, GstMapInfo *output_map,
    struct net_output_cfg *run_out)
{
  guint64 slab_phys = 0;
  gsize mem_off = 0, mem_sz = 0;
  guint n;
  gsize out_offset = 0;
  gsize total_out = 0;
  gsize map_max = output_map->size;
  unsigned char *virt_base = output_map->data;

  if (!virt_base || map_max == 0)
    return FALSE;

  slab_phys = gst_amba_cavalry_buffer_get_slab_phys (outbuf);
  if (!slab_phys) {
    guint64 pb = gst_amba_cavalry_memory_get_phys_base (output_mem);

    if (pb) {
      gst_memory_get_sizes (output_mem, &mem_off, &mem_sz);
      slab_phys = pb;
    }
  }

  if (!slab_phys) {
    unsigned long pa = cavalry_mem_virt_to_phys (virt_base);

    if (pa == 0)
      return FALSE;
    slab_phys = (guint64) pa;
    mem_off = 0;
    gst_memory_get_sizes (output_mem, NULL, &mem_sz);
  } else if (!mem_sz) {
    gst_memory_get_sizes (output_mem, &mem_off, &mem_sz);
  }

  if (!run_out || run_out->out_num == 0)
    return FALSE;

  for (n = 0; n < run_out->out_num; n++)
    total_out += run_out->out_desc[n].size;

  if (total_out > map_max)
    return FALSE;

  if (mem_sz > 0 && total_out > mem_sz - mem_off)
    return FALSE;

  for (n = 0; n < run_out->out_num; n++) {
    run_out->out_desc[n].virt = virt_base + out_offset;
    run_out->out_desc[n].addr =
        (cv_daddr_t) (slab_phys + mem_off + out_offset);
    out_offset += run_out->out_desc[n].size;
  }

  if (!self->logged_output_path_once) {
    g_printerr ("[mlinference2]: NN output zero-copy (legacy phys), "
        "slab_phys=0x%llx, tensors=%u\n",
        (unsigned long long) (slab_phys + mem_off),
        ml2_output_num(filter));
    self->logged_output_path_once = TRUE;
  }

  return TRUE;
}

/* Parse in_data_fmt "sign.datasize.exp_offset.exp_bits" */
static int __parse_in_data_fmt(const char *s, unsigned int *sign, unsigned int *datasize,
    unsigned int *exp_offset, unsigned int *exp_bits)
{
  if (!s || !s[0])
    return 0;
  return (sscanf(s, "%u.%u.%u.%u", sign, datasize, exp_offset, exp_bits) >= 4);
}

static gboolean
ml2_virt_in_io_mem (priv_ml_infer_ctx2_t *params, const void *virt)
{
  const guint8 *v = (const guint8 *) virt;
  const guint8 *base;
  gsize len;

  if (!params || !virt || !params->io_mem.virt_addr || params->io_mem.mem_size == 0)
    return FALSE;

  base = (const guint8 *) params->io_mem.virt_addr;
  len = (gsize) params->io_mem.mem_size;
  return (v >= base && v < base + len);
}

static gboolean
ml2_phys_in_io_mem (priv_ml_infer_ctx2_t *params, unsigned long phys, unsigned long size)
{
  unsigned long base, end;

  if (!params || !phys || !size || !params->io_mem.phy_addr || params->io_mem.mem_size == 0)
    return FALSE;

  base = params->io_mem.phy_addr;
  end = base + params->io_mem.mem_size;
  return (phys >= base && phys + size <= end);
}

/* Copy only active outputs into legacy_run_out (not a full 128-entry struct copy). */
static void
ml2_legacy_run_out_reset (priv_ml_infer_ctx2_t *filter)
{
  guint i, n;

  if (!filter)
    return;

  n = filter->io_output_cfg.out_num;
  if (n > MAX_IO_NUM)
    n = MAX_IO_NUM;

  filter->legacy_run_out.out_num = n;
  for (i = 0; i < n; i++)
    filter->legacy_run_out.out_desc[i] = filter->io_output_cfg.out_desc[i];
}

static gsize
ml2_legacy_rgbp_input_bytes (img_data_info2_t *input_info, struct input_desc *dst)
{
  gsize total = 0;
  gint p;

  for (p = 0; p < input_info->n_planes; p++) {
    if ((gsize) input_info->stride[p] != (gsize) dst->dim.pitch)
      total += (gsize) input_info->height * dst->dim.pitch;
    else
      total += (gsize) input_info->height * input_info->stride[p];
  }
  return total;
}

/* Free load-time legacy output slabs; keep size/dim for run + caps. */
static void
ml2_legacy_release_output_bufs (priv_ml_infer_ctx2_t *params)
{
  unsigned int i;

  if (!params)
    return;

  for (i = 0; i < params->io_output_cfg.out_num; i++) {
    struct output_desc *out = &params->io_output_cfg.out_desc[i];

    if (!out->virt || out->addr == 0)
      continue;
    if (ml2_virt_in_io_mem (params, out->virt)
        || ml2_phys_in_io_mem (params, out->addr, out->size))
      continue;

    cavalry_mem_free (out->size, out->addr, out->virt);
    out->virt = NULL;
    out->addr = 0;
  }
}

/* (Re)allocate legacy output slabs for the memcpy fallback path. */
static int
ml2_legacy_ensure_output_bufs (priv_ml_infer_ctx2_t *params, int max_batch)
{
  unsigned long size = 0;
  unsigned long phys_addr = 0;
  size_t shape[4];
  size_t pitch = 0;
  unsigned int i;

  if (!params)
    return -1;

  for (i = 0; i < params->io_output_cfg.out_num; i++) {
    struct output_desc *out = &params->io_output_cfg.out_desc[i];

    if (out->virt && out->addr != 0)
      continue;

    shape[0] = out->dim.plane * max_batch;
    shape[1] = out->dim.depth;
    shape[2] = out->dim.height;
    shape[3] = out->dim.width;
    pitch = out->dim.pitch;
    if (pitch == 0) {
      if (out->data_fmt.size == 1) {
        pitch = DROUND_UP (shape[3] * sizeof (unsigned short), CAVALRY_PORT_PITCH_ALIGN);
      } else if (out->data_fmt.size == 2) {
        pitch = DROUND_UP (shape[3] * sizeof (float), CAVALRY_PORT_PITCH_ALIGN);
      } else {
        DPRINT_ERROR ("net output only supports 16-bits and 32-bits data by now\n");
        return -1;
      }
      out->dim.pitch = pitch;
    }
    size = shape[0] * shape[1] * shape[2] * pitch;
    if (cavalry_mem_alloc (&size, &phys_addr, (void **) &out->virt, params->cache_en) < 0) {
      DPRINT_ERROR ("output memory error (legacy copy path)\n");
      return -1;
    }
    out->size = size;
    out->addr = phys_addr;
  }

  return 0;
}

static void __net_io_config(priv_ml_infer_ctx2_t *params)
{
  unsigned int i;

  if (params->use_mfd) {
    for (i = 0; i < params->input_num; i++) {
      params->input_cfg.in_desc[i].name = params->input_name[i];
      params->input_cfg.in_desc[i].no_mem = 1;
    }
    params->input_cfg.in_num = params->input_num;

    for (i = 0; i < params->output_num; i++) {
      params->output_cfg.out_desc[i].name = params->output_name[i];
      params->output_cfg.out_desc[i].no_mem = 1;
    }
    params->output_cfg.out_num = params->output_num;
  } else {
    for (i = 0; i < params->input_num; i++) {
      params->io_input_cfg.in_desc[i].name = params->input_name[i];
      params->io_input_cfg.in_desc[i].no_mem = 1;
    }
    params->io_input_cfg.in_num = params->input_num;

    for (i = 0; i < params->output_num; i++) {
      params->io_output_cfg.out_desc[i].name = params->output_name[i];
      params->io_output_cfg.out_desc[i].no_mem = 1;
    }
    params->io_output_cfg.out_num = params->output_num;
  }
}

static void __net_free_mfd(priv_ml_infer_ctx2_t *params)
{
  unsigned int i;

  if (params->input_mem_virt && params->input_mem_fd > 0) {
    cavalry_mem_free_mfd(params->input_mem_size, params->input_mem_fd, params->input_mem_virt);
    params->input_mem_virt = NULL;
    params->input_mem_size = 0;
    params->input_mem_fd = -1;
  }
  for (i = 0; i < params->output_cfg.out_num; i++) {
    params->output_cfg.out_desc[i].virt = NULL;
    params->output_cfg.out_desc[i].mem_fd = -1;
  }

  if (params->mem.mem_size > 0 && params->mem.fd > 0) {
    cavalry_mem_free_mfd(params->mem.mem_size, params->mem.fd, params->mem.virt_addr);
    params->mem.virt_addr = NULL;
    params->mem.mem_size = 0;
    params->mem.fd = -1;
  }

#if defined (DBUILD_AMBA_CAVALRY_V2)
  if (params->conv_buf_size > 0 && params->conv_buf_fd > 0) {
    cavalry_mem_free_mfd(params->conv_buf_size, params->conv_buf_fd, params->conv_buf_virt);
    params->conv_buf_virt = NULL;
    params->conv_buf_size = 0;
    params->conv_buf_fd = -1;
  }
#endif
}

static void __net_free_legacy(priv_ml_infer_ctx2_t *params)
{
  unsigned int i;

  if (params->input_mem_virt && params->input_mem_phys > 0) {
    gboolean same_as_in = FALSE;

    if (params->io_input_cfg.in_num > 0
        && params->io_input_cfg.in_desc[0].virt == params->input_mem_virt)
      same_as_in = TRUE;

    if (!same_as_in) {
      cavalry_mem_free (params->input_mem_size, params->input_mem_phys,
          params->input_mem_virt);
    }
    params->input_mem_virt = NULL;
    params->input_mem_size = 0;
    params->input_mem_phys = 0;
  }

  for (i = 0; i < params->io_input_cfg.in_num; i++) {
    struct input_desc *in = &params->io_input_cfg.in_desc[i];

    if (!in->virt || in->addr == 0 || ml2_virt_in_io_mem (params, in->virt)
        || ml2_phys_in_io_mem (params, in->addr, in->size))
      continue;

    cavalry_mem_free (in->size, in->addr, in->virt);
    in->virt = NULL;
    in->addr = 0;
  }

  ml2_legacy_release_output_bufs (params);

  if (params->io_mem.mem_size > 0 && params->io_mem.virt_addr) {
    cavalry_mem_free (params->io_mem.mem_size, params->io_mem.phy_addr,
        params->io_mem.virt_addr);
    params->io_mem.virt_addr = NULL;
    params->io_mem.mem_size = 0;
    params->io_mem.phy_addr = 0;
  }

#if defined (DBUILD_AMBA_CAVALRY_V2)
  if (params->conv_buf_size > 0 && params->conv_buf_phys > 0) {
    cavalry_mem_free(params->conv_buf_size, params->conv_buf_phys, params->conv_buf_virt);
    params->conv_buf_virt = NULL;
    params->conv_buf_size = 0;
    params->conv_buf_phys = 0;
  }
#endif
}

static void __net_free(priv_ml_infer_ctx2_t *params)
{
  int net_id;

  if (!params || params->nn_freed)
    return;

  params->nn_freed = TRUE;
  net_id = params->id;
  params->id = -1;

  if (net_id >= 0)
    nnctrl_exit_net (net_id);

  if (params->use_mfd)
    __net_free_mfd (params);
  else
    __net_free_legacy (params);
}


static int __net_load_mfd(priv_ml_infer_ctx2_t *params, int max_batch)
{
  int rval = 0;
  unsigned long size = 0;
  size_t shape[4];
  size_t pitch = 0;
  unsigned int i = 0;

  do {
    params->cfg.verbose = params->verbose_print;
    params->cfg.print_time = params->print_time;
    params->cfg.net_file = params->model_file;

    params->cfg.reuse_mem = 1;

    params->cfg.net_loop_cnt = max_batch;
    params->id = -1;
    params->id = nnctrl_init_net_by_mfd(&params->cfg, &params->input_cfg, &params->output_cfg);
    if (params->id < 0) {
        DPRINT_ERROR("nnctrl_init_net_by_mfd failed on %s\n", params->cfg.net_file);
        rval = -1;
        break;
    }

    // allocate memory for network
    size = params->cfg.net_mem_total;
#if ENABLE_CACHE_ON_NET_MEM
    rval = cavalry_mem_alloc_mfd_cv(size, &params->mem.fd,
        (void **) & (params->mem.virt_addr), params->cache_en);
#else
    rval = cavalry_mem_alloc_mfd_cv(size, &params->mem.fd,
        (void **) & (params->mem.virt_addr), 0);
#endif
    if (rval < 0) {
      DPRINT_ERROR("cavalry_mem_alloc_mfd error\n");
      rval = -1;
      break;
    } else {
      if (params->mem.virt_addr == NULL) {
        DPRINT_ERROR("alloc cv mem is NULL\n");
        rval = -1;
        break;
      }
    }

    params->mem.mem_size = size;

    if (params->mem.fd <= 0) {
      DPRINT_ERROR("net mem fd=%d invalid (cavalry_mem_alloc_mfd_cv may have failed)\n", params->mem.fd);
      rval = -1;
      break;
    }
    DPRINT_INFO("net use cavalry memory total 0x%lX bytes, mfd=%d\n",
        (unsigned long)params->mem.mem_size, params->mem.fd);

    // Calculate input size and pitch, but delay memory allocation to runtime
    // This allows zero-copy mode to use upstream buffer directly
    for (i = 0; i < params->input_cfg.in_num; i++) {

      shape[0] = params->input_cfg.in_desc[i].dim.plane * max_batch;
      if (params->input_cfg.in_desc[i].dim.dram_fmt == 1) {
        shape[1] = 1;
        shape[2] = params->input_cfg.in_desc[i].dim.height;
        shape[3] = params->input_cfg.in_desc[i].dim.width * params->input_cfg.in_desc[i].dim.depth;
      } else {
        shape[1] = params->input_cfg.in_desc[i].dim.depth;
        shape[2] = params->input_cfg.in_desc[i].dim.height;
        shape[3] = params->input_cfg.in_desc[i].dim.width;
      }
      pitch = params->input_cfg.in_desc[i].dim.pitch;
      if (pitch == 0) {
        if (params->input_cfg.in_desc[i].data_fmt.size == 0) {
          pitch = DROUND_UP(shape[3] * sizeof(unsigned char), CAVALRY_PORT_PITCH_ALIGN);
        } else if (params->input_cfg.in_desc[i].data_fmt.size == 2) {
          pitch = DROUND_UP(shape[3] * sizeof(float), CAVALRY_PORT_PITCH_ALIGN);
        } else {
          DPRINT_ERROR("net input only supports 8-bits and 32-bits data by now\n");
          rval = -1;
          break;
        }
        params->input_cfg.in_desc[i].dim.pitch = pitch;
      }
      size = shape[0] * shape[1] * shape[2] * pitch;
      // Don't allocate here
      params->input_cfg.in_desc[i].size = size;
      params->input_cfg.in_desc[i].virt = NULL;
      params->input_cfg.in_desc[i].mem_fd = -1;
      params->input_cfg.in_desc[i].fd_offset = 0;
      DPRINT_INFO("net input %u requires 0x%lX bytes (delayed allocation)\n", i, size);
    }
    // Initialize input_mem fields
    params->input_mem_virt = NULL;
    params->input_mem_fd = -1;
    params->input_mem_size = 0;

    for (i = 0; i < params->output_cfg.out_num; i++) {
      shape[0] = params->output_cfg.out_desc[i].dim.plane * max_batch;
      shape[1] = params->output_cfg.out_desc[i].dim.depth;
      shape[2] = params->output_cfg.out_desc[i].dim.height;
      shape[3] = params->output_cfg.out_desc[i].dim.width;
      pitch = params->output_cfg.out_desc[i].dim.pitch;
      if (pitch == 0) {
        if (params->output_cfg.out_desc[i].data_fmt.size == 1) {
          pitch = DROUND_UP(shape[3] * sizeof(unsigned short), CAVALRY_PORT_PITCH_ALIGN);
        } else if (params->output_cfg.out_desc[i].data_fmt.size == 2) {
          pitch = DROUND_UP(shape[3] * sizeof(float), CAVALRY_PORT_PITCH_ALIGN);
        } else {
          DPRINT_ERROR("net output only supports 16-bits and 32-bits data by now\n");
          rval = -1;
          break;
        }
        params->output_cfg.out_desc[i].dim.pitch = pitch;
      }

      size = shape[0] * shape[1] * shape[2] * pitch;
      // Don't allocate output at init - per-frame buffers from out_pool (prepare_output_buffer)
      params->output_cfg.out_desc[i].size = size;
      params->output_cfg.out_desc[i].virt = NULL;
      params->output_cfg.out_desc[i].mem_fd = -1;
      params->output_cfg.out_desc[i].fd_offset = 0;
      DPRINT_INFO("net output %u requires 0x%lX bytes (per-frame allocation)\n", i, size);

    }

    // load network to memory
    if (nnctrl_load_net_by_mfd(params->id, &params->mem, &params->input_cfg, &params->output_cfg) < 0) {
      printf("nnctrl_load_net_by_mfd failed\n");
      rval = -1;
      break;
    }
#if ENABLE_CACHE_ON_NET_MEM
    if (params->cache_en) {
      cavalry_mem_sync_cache_mfd(params->mem.mem_size, 0, params->mem.fd, 1, 0); // clean data in CPU cache after load
    }
#endif
  } while (0);

  if (rval < 0) {
    __net_free(params);
  }

  return rval;
}

static int __net_load_legacy(priv_ml_infer_ctx2_t *params, int max_batch)
{
  int rval = 0;
  unsigned long size = 0;
  unsigned long phys_addr = 0;
  size_t shape[4];
  size_t pitch = 0;
  unsigned int i = 0;

  do {
    params->cfg.verbose = params->verbose_print;
    params->cfg.print_time = params->print_time;
    params->cfg.net_file = params->model_file;
    params->cfg.reuse_mem = 1;
    params->cfg.net_loop_cnt = max_batch;
    params->id = -1;
    params->id = nnctrl_init_net(&params->cfg, &params->io_input_cfg, &params->io_output_cfg);
    if (params->id < 0) {
      DPRINT_ERROR("nnctrl_init_net failed on %s\n", params->cfg.net_file);
      rval = -1;
      break;
    }

    size = params->cfg.net_mem_total;
#if ENABLE_CACHE_ON_NET_MEM
    rval = cavalry_mem_alloc(&size, &phys_addr,
        (void **) & (params->io_mem.virt_addr), params->cache_en);
#else
    rval = cavalry_mem_alloc(&size, &phys_addr,
        (void **) & (params->io_mem.virt_addr), 0);
#endif
    if (rval < 0 || params->io_mem.virt_addr == NULL) {
      DPRINT_ERROR("cavalry_mem_alloc error for net memory\n");
      rval = -1;
      break;
    }
    params->io_mem.mem_size = size;
    params->io_mem.phy_addr = phys_addr;
    DPRINT_INFO("net use cavalry memory total 0x%lX bytes (legacy)\n",
        (unsigned long)params->io_mem.mem_size);

    for (i = 0; i < params->io_input_cfg.in_num; i++) {
      shape[0] = params->io_input_cfg.in_desc[i].dim.plane * max_batch;
      if (params->io_input_cfg.in_desc[i].dim.dram_fmt == 1) {
        shape[1] = 1;
        shape[2] = params->io_input_cfg.in_desc[i].dim.height;
        shape[3] = params->io_input_cfg.in_desc[i].dim.width * params->io_input_cfg.in_desc[i].dim.depth;
      } else {
        shape[1] = params->io_input_cfg.in_desc[i].dim.depth;
        shape[2] = params->io_input_cfg.in_desc[i].dim.height;
        shape[3] = params->io_input_cfg.in_desc[i].dim.width;
      }
      pitch = params->io_input_cfg.in_desc[i].dim.pitch;
      if (pitch == 0) {
        if (params->io_input_cfg.in_desc[i].data_fmt.size == 0) {
          pitch = DROUND_UP(shape[3] * sizeof(unsigned char), CAVALRY_PORT_PITCH_ALIGN);
        } else if (params->io_input_cfg.in_desc[i].data_fmt.size == 2) {
          pitch = DROUND_UP(shape[3] * sizeof(float), CAVALRY_PORT_PITCH_ALIGN);
        } else {
          DPRINT_ERROR("net input only supports 8-bits and 32-bits data by now\n");
          rval = -1;
          break;
        }
        params->io_input_cfg.in_desc[i].dim.pitch = pitch;
      }
      size = shape[0] * shape[1] * shape[2] * pitch;
      if (cavalry_mem_alloc(&size, &phys_addr,
            (void **) & (params->io_input_cfg.in_desc[i].virt), params->cache_en) < 0) {
        DPRINT_ERROR("input memory error (legacy)\n");
        rval = -1;
        break;
      }
      params->io_input_cfg.in_desc[i].size = size;
      params->io_input_cfg.in_desc[i].addr = phys_addr;
      DPRINT_INFO("net input %u use cavalry mem total 0x%lX bytes (legacy)\n", i, size);
    }

    for (i = 0; i < params->io_output_cfg.out_num; i++) {
      shape[0] = params->io_output_cfg.out_desc[i].dim.plane * max_batch;
      shape[1] = params->io_output_cfg.out_desc[i].dim.depth;
      shape[2] = params->io_output_cfg.out_desc[i].dim.height;
      shape[3] = params->io_output_cfg.out_desc[i].dim.width;
      pitch = params->io_output_cfg.out_desc[i].dim.pitch;
      if (pitch == 0) {
        if (params->io_output_cfg.out_desc[i].data_fmt.size == 1) {
          pitch = DROUND_UP(shape[3] * sizeof(unsigned short), CAVALRY_PORT_PITCH_ALIGN);
        } else if (params->io_output_cfg.out_desc[i].data_fmt.size == 2) {
          pitch = DROUND_UP(shape[3] * sizeof(float), CAVALRY_PORT_PITCH_ALIGN);
        } else {
          DPRINT_ERROR("net output only supports 16-bits and 32-bits data by now\n");
          rval = -1;
          break;
        }
        params->io_output_cfg.out_desc[i].dim.pitch = pitch;
      }
      size = shape[0] * shape[1] * shape[2] * pitch;
      if (cavalry_mem_alloc(&size, &phys_addr,
            (void **) & (params->io_output_cfg.out_desc[i].virt), params->cache_en) < 0) {
        DPRINT_ERROR("output memory error (legacy)\n");
        rval = -1;
        break;
      }
      params->io_output_cfg.out_desc[i].size = size;
      params->io_output_cfg.out_desc[i].addr = phys_addr;
      DPRINT_INFO("net output %u use cavalry mem total 0x%lX bytes (legacy)\n", i, size);
    }

    if (nnctrl_load_net(params->id, &params->io_mem,
          &params->io_input_cfg, &params->io_output_cfg) < 0) {
      DPRINT_ERROR("nnctrl_load_net failed\n");
      rval = -1;
      break;
    }
#if ENABLE_CACHE_ON_NET_MEM
    if (params->cache_en) {
      cavalry_mem_sync_cache(params->io_mem.mem_size, params->io_mem.phy_addr, 1, 0);
    }
#endif

  } while (0);

  if (rval < 0)
    __net_free(params);

  return rval;
}

static int __net_load(priv_ml_infer_ctx2_t *params, int max_batch)
{
  if (params->use_mfd)
    return __net_load_mfd(params, max_batch);
  return __net_load_legacy(params, max_batch);
}


static int __net_init(priv_ml_infer_ctx2_t *params)
{
  int rval = 0;

  if (params) {
    do {

      if (params->id >= 0)
        __net_free (params);

      params->verbose_print = 0;
      params->split_num = 0;
      params->abort_if_preempted = 0;
      params->priority = 0;
      params->cache_en = 1;
      params->nn_freed = FALSE;

      __net_io_config(params);
      if (__net_load(params, 1/*max_batch*/) < 0) {
        DPRINT_ERROR("__net_load failed\n");
        rval = -1;
        break;
      }

    } while (0);
  } else {
    DPRINT_ERROR("params error\n");
    rval = -1;
  }


  if (rval < 0) {
    if (params) {
      __net_free(params);
    }
  }

  return rval;
}

/* initialize the ml_inference2's class */
static void
gst_ml_inference2_class_init (GstMlInference2Class * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstBaseTransformClass *basetransform_class = (GstBaseTransformClass *) klass;

  gobject_class->finalize = gst_ml_inference2_finalize;
  gobject_class->set_property = gst_ml_inference2_set_property;
  gobject_class->get_property = gst_ml_inference2_get_property;

  basetransform_class->start = gst_ml_inference2_start;
  basetransform_class->stop = gst_ml_inference2_stop;
  basetransform_class->propose_allocation = GST_DEBUG_FUNCPTR (gst_ml_inference2_propose_allocation);
  basetransform_class->decide_allocation = GST_DEBUG_FUNCPTR (gst_ml_inference2_decide_allocation);
  basetransform_class->prepare_output_buffer = GST_DEBUG_FUNCPTR (gst_ml_inference2_prepare_output_buffer);
  basetransform_class->transform_caps = GST_DEBUG_FUNCPTR (gst_ml_inference2_transform_caps);
  basetransform_class->transform =
      GST_DEBUG_FUNCPTR (gst_ml_inference2_transform);

  g_object_class_install_property (gobject_class, PROP_IN_NAME,
      g_param_spec_string ("in_name", "InName", "input feature name ?",
          NULL, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_OUT_NAME,
      g_param_spec_string ("out_name", "OutName", "output feature name ?",
          NULL, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_MODEL,
      g_param_spec_string ("model", "ModelFile", "model file name ?",
          NULL, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_IN_DATA_FMT,
      g_param_spec_string ("in_data_fmt", "InDataFmt",
          "Source data format (sign.datasize.exp_offset.exp_bits). If differs from model, conversion applied. avdec: 0.0.0.0.",
          "", G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_USE_MFD,
      g_param_spec_boolean ("use_mfd", "Use MFD APIs",
          "Use nnctrl memory-fd APIs (nnctrl_init_net_by_mfd etc.) when TRUE; "
          "use legacy phys-addr APIs (nnctrl_init_net etc.) when FALSE; "
          "output requires amba_cavalry_phys (no mfd fallback).",
          TRUE, G_PARAM_READWRITE));

  gst_element_class_set_static_metadata (gstelement_class,
      "Amba Machine Learning Inference",
      "mlinference2",
      "Inference v2 of cvflow for machine learning",
      "PengXue Duan <<pxduan@ambarella.com>>");

  gst_element_class_add_static_pad_template (gstelement_class, &src_factory);
  gst_element_class_add_static_pad_template (gstelement_class, &sink_factory);
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad callback functions
 * initialize instance structure
 */
static void
gst_ml_inference2_init (GstMlInference2 * in_filter)
{
  priv_ml_infer_ctx2_t * filter;
  filter = (priv_ml_infer_ctx2_t *) malloc(sizeof(priv_ml_infer_ctx2_t));
  memset(filter, 0x0, sizeof(priv_ml_infer_ctx2_t));
  in_filter->priv_ctx = filter;
  in_filter->out_pool = NULL;
  in_filter->out_pool_size = 0;
  in_filter->logged_input_path_once = FALSE;
  in_filter->logged_output_path_once = FALSE;

  filter->input_num = 0;
  filter->output_num = 0;
  filter->in_data_fmt[0] = '\0';
  filter->use_mfd = TRUE;
  filter->nn_freed = TRUE;
  filter->input_mem_fd = -1;

  filter->cv_ctx = acquire_cv_vproc_ctx(1, 1);

  gst_amba_cavalry_allocator_init_once ();
  gst_amba_cavalry_phys_allocator_init_once ();

}

static gint __setprop_NAME (priv_ml_infer_ctx2_t * pdata,
    const gchar * value, const gboolean is_input)
{
  guint num_names = 0;

  if (value) {
    guint i = 0;
    gchar **str_names = NULL;

    str_names = g_strsplit (value, "+", -1);
    num_names = g_strv_length (str_names);

    if (num_names > (guint) MIN (MAX_IO_NUM, AMBA_ML_MAX_TENSORS)) {
      GST_WARNING ("Invalid param, names (%d) exceeds max (%d)\n",
          num_names, (int) MIN (MAX_IO_NUM, AMBA_ML_MAX_TENSORS));
      num_names = (guint) MIN (MAX_IO_NUM, AMBA_ML_MAX_TENSORS);
    }

    if (is_input) {
      for (i = 0; i < num_names; i++) {
        if (str_names[i] && strlen (g_strstrip (str_names[i]))) {
          strncpy(pdata->input_name[i], str_names[i], DMAX_FILE_NAME_LENGTH);
          pdata->input_name[i][DMAX_FILE_NAME_LENGTH] = '\0';
        } else {
          pdata->input_name[i][0] = '\0';
        }
      }
      pdata->input_num = num_names;
    } else {
      for (i = 0; i < num_names; i++) {
        if (str_names[i] && strlen (g_strstrip (str_names[i]))) {
          strncpy(pdata->output_name[i], str_names[i], DMAX_FILE_NAME_LENGTH);
          pdata->output_name[i][DMAX_FILE_NAME_LENGTH] = '\0';
        } else {
          pdata->output_name[i][0] = '\0';
        }
      }
      pdata->output_num = num_names;
    }

    g_strfreev (str_names);
  } else {
    GST_WARNING ("params error: value is NULL\n");
  }
  return num_names;
}

static void
gst_ml_inference2_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMlInference2 *in_filter = GST_MLINFERENCE2 (object);
  priv_ml_infer_ctx2_t * filter = in_filter->priv_ctx;

  switch (prop_id) {
    case PROP_IN_NAME:
      __setprop_NAME(filter, g_value_get_string (value), TRUE);
      break;
    case PROP_OUT_NAME:
      __setprop_NAME(filter, g_value_get_string (value), FALSE);
      break;
    case PROP_MODEL:
      strncpy(filter->model_file, g_value_get_string (value), DMAX_FILE_NAME_LENGTH);
      break;
    case PROP_IN_DATA_FMT: {
      const gchar *s = g_value_get_string (value);
      strncpy(filter->in_data_fmt, s ? s : "", sizeof(filter->in_data_fmt) - 1);
      filter->in_data_fmt[sizeof(filter->in_data_fmt) - 1] = '\0';
      break;
    }
    case PROP_USE_MFD:
      filter->use_mfd = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ml_inference2_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMlInference2 *in_filter = GST_MLINFERENCE2 (object);
  priv_ml_infer_ctx2_t * filter = in_filter->priv_ctx;

  switch (prop_id) {
    case PROP_IN_NAME:
      g_value_set_string (value, filter->input_name[0]);
      break;
    case PROP_OUT_NAME:
      g_value_set_string (value, filter->output_name[0]);
      break;
    case PROP_MODEL:
      g_value_set_string (value, filter->model_file);
      break;
    case PROP_IN_DATA_FMT:
      g_value_set_string (value, filter->in_data_fmt);
      break;
    case PROP_USE_MFD:
      g_value_set_boolean (value, filter->use_mfd);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}



static void
gst_ml_inference2_finalize (GObject * object)
{
  GstMlInference2 *in_filter = GST_MLINFERENCE2 (object);
  //priv_ml_infer_ctx2_t * filter = in_filter->priv_ctx;

  /* NN cavalry memory is released in stop(); avoid double-free here. */
  ml2_output_pool_destroy (in_filter);
  release_cv_vproc_ctx(1);

  if (in_filter->priv_ctx) {
    free(in_filter->priv_ctx);
    in_filter->priv_ctx = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
ml2_legacy_log_input_copy_path (GstMlInference2 *self, priv_ml_infer_ctx2_t *ctx,
    img_data_info2_t *input_info, GstMemory *memory, int need_convert)
{
  const char *reason = "legacy uses dedicated NN input slab (memcpy)";
  int pi;

  if (!self || self->logged_input_path_once)
    return;

  if (need_convert) {
    reason = "source/model exponent mismatch";
  } else if (!memory || !(gst_is_amba_cavalry_memory (memory)
          || gst_is_amba_cavalry_memory_phy (memory))) {
    reason = "input is not Cavalry memory";
  } else if (input_info->n_planes == 3 &&
      (input_info->stride[0] != input_info->stride[1] ||
          input_info->stride[1] != input_info->stride[2])) {
    reason = "RGBP plane strides differ (zero-copy requires R/G/B same pitch)";
  } else {
    for (pi = 0; pi < input_info->n_planes; pi++) {
      if ((gint) input_info->stride[pi] !=
          (gint) ctx->io_input_cfg.in_desc[0].dim.pitch) {
        reason = "RGBP stride != NN pitch";
        break;
      }
    }
  }

  g_printerr ("[mlinference2]: NN input copy path (legacy): %s\n", reason);
  self->logged_input_path_once = TRUE;
}

static int __simple_data_type_convert_legacy (GstMlInference2 *self,
    priv_ml_infer_ctx2_t *ctx, img_data_info2_t *input_info, GstMemory *memory)
{
  int rval = 0;
  struct input_desc *dst = &ctx->io_input_cfg.in_desc[0];
  int need_convert = 0;

  do {
    g_assert(input_info->format == GST_VIDEO_FORMAT_RGBP);

    if (input_info->height != dst->dim.height || input_info->width != dst->dim.width) {
      DPRINT_ERROR("Input dimensions (%dx%d) don't match NN requirements (%lux%lu)\n",
          input_info->width, input_info->height,
          (unsigned long)dst->dim.width, (unsigned long)dst->dim.height);
      rval = -1;
      break;
    }

    if (!dst->virt || dst->addr == 0) {
      DPRINT_ERROR("legacy input buffer not allocated (addr=0x%lx)\n",
          (unsigned long)dst->addr);
      rval = -1;
      break;
    }

    if (input_info->format == GST_VIDEO_FORMAT_RGBP && input_info->n_planes > 0) {
      gsize need = ml2_legacy_rgbp_input_bytes (input_info, dst);

      if (need > (gsize) dst->size) {
        DPRINT_ERROR ("RGBP input needs %" G_GSIZE_FORMAT " bytes but NN buffer is %lu bytes "
            "(plane=%lu depth=%lu pitch=%lu)\n",
            need, dst->size,
            dst->dim.plane, dst->dim.depth, dst->dim.pitch);
        rval = -1;
        break;
      }
    }

    if (dst->data_fmt.size == 0) {
      unsigned int src_exp = 0, model_exp = (unsigned int)dst->data_fmt.expoffset;
      unsigned int d0 = 0, d1 = 0, d2 = 0;
      unsigned char *nn_input = dst->virt;

      if (__parse_in_data_fmt(ctx->in_data_fmt, &d0, &d1, &src_exp, &d2)) {
        if (src_exp != model_exp)
          need_convert = 1;
      }

      ml2_legacy_log_input_copy_path (self, ctx, input_info, memory, need_convert);

      if (!need_convert) {
        for (int i = 0; i < input_info->n_planes; i++) {
          if (input_info->stride[i] != dst->dim.pitch) {
            unsigned char *s0 = input_info->data[i];
            for (unsigned int h = 0; h < input_info->height; h++) {
              memcpy(nn_input, s0, input_info->width);
              nn_input += dst->dim.pitch;
              s0 += input_info->stride[i];
            }
          } else {
            gsize ss = input_info->height * input_info->stride[i];
            memcpy(nn_input, input_info->data[i], ss);
            nn_input += ss;
          }
        }
      } else {
        for (int i = 0; i < input_info->n_planes; i++) {
          unsigned char *s0 = input_info->data[i];
          for (unsigned int h = 0; h < input_info->height; h++) {
            for (unsigned int w = 0; w < input_info->width; w++) {
              unsigned int v = (unsigned int)s0[w];
              if (src_exp == 8 && model_exp == 0)
                nn_input[w] = (unsigned char)((v * 255 + 128) / 256);
              else if (src_exp == 0 && model_exp == 8)
                nn_input[w] = (unsigned char)((v * 256 + 127) / 255);
              else
                nn_input[w] = (unsigned char)v;
            }
            nn_input += dst->dim.pitch;
            s0 += input_info->stride[i];
          }
        }
      }
    } else if (dst->data_fmt.size == 2) {
      ml2_legacy_log_input_copy_path (self, ctx, input_info, memory, 0);
      {
        unsigned char *f32_row = (unsigned char *)dst->virt;

        for (int i = 0; i < input_info->n_planes; i++) {
          unsigned char *s0 = input_info->data[i];
          for (unsigned int h = 0; h < input_info->height; h++) {
            float *f32_data = (float *)f32_row;
            for (unsigned int w = 0; w < input_info->width; w++)
              f32_data[w] = (float)s0[w];
            s0 += input_info->stride[i];
            f32_row += dst->dim.pitch;
          }
        }
      }
    } else {
      DPRINT_ERROR("Unsupported NN input data format size: %d\n", dst->data_fmt.size);
      rval = -1;
      break;
    }

#if ENABLE_CACHE_ON_NET_MEM
    if (ctx->cache_en)
      cavalry_mem_sync_cache(dst->size, dst->addr, 1, 0);
#endif

    if (nnctrl_set_net_io_cfg(ctx->id, &ctx->io_input_cfg, NULL) < 0) {
      DPRINT_ERROR("nnctrl_set_net_io_cfg failed\n");
      rval = -1;
      break;
    }
  } while (0);

  return rval;
}

static int __simple_data_type_convert(GstBaseTransform *trans,
    priv_ml_infer_ctx2_t *ctx,
    img_data_info2_t *input_info, GstMemory *memory)
{
  if (!ctx->use_mfd)
    return __simple_data_type_convert_legacy (GST_MLINFERENCE2 (trans), ctx,
        input_info, memory);

  int rval = 0;
  struct input_mfd_desc *dst = &ctx->input_cfg.in_desc[0];

  do {
    int zero_copy_dmabuf_frame = 0;

    // Assert RGBP format (guaranteed by sink pad caps)
    g_assert(input_info->format == GST_VIDEO_FORMAT_RGBP);

    // Check if input dimensions match neural network requirements
    if (input_info->height != dst->dim.height || input_info->width != dst->dim.width) {
      DPRINT_ERROR("Input dimensions (%dx%d) don't match NN requirements (%lux%lu)\n",
          input_info->width, input_info->height, (unsigned long)dst->dim.width, (unsigned long)dst->dim.height);
      rval = -1;
      break;
    }

    if (dst->data_fmt.size == 0) {
      // NN expects uint8 input. in_data_fmt describes SOURCE format; convert if differs from model.
      unsigned int src_exp = 0, model_exp = (unsigned int)dst->data_fmt.expoffset;
      unsigned int d0 = 0, d1 = 0, d2 = 0;
      int need_convert = 0;
      if (__parse_in_data_fmt(ctx->in_data_fmt, &d0, &d1, &src_exp, &d2)) {
        if (src_exp != model_exp)
          need_convert = 1;
      }

      /* Zero-copy when Cavalry-backed, no exp conversion, strides CAVALRY-aligned and uniform
       * (pitch may differ from model default; we then refresh dst->dim.pitch / dst->size). */
      gboolean strides_ok = TRUE;
      for (int pi = 0; pi < input_info->n_planes; pi++) {
        if (CHECK_PITCH_ALIGN (input_info->stride[pi])) {
          strides_ok = FALSE;
          break;
        }
      }
      gboolean can_input_zero_copy = gst_is_amba_cavalry_memory (memory) && (!need_convert) && strides_ok;

      if (!can_input_zero_copy) {
        {
          GstMlInference2 *inf = GST_MLINFERENCE2 (trans);

          if (!inf->logged_input_path_once) {
            const char *reason;
            gint in_mfd = -1;

            if (!gst_is_amba_cavalry_memory(memory)) {
              reason = "input is not Cavalry/DMA memory";
            } else if (need_convert) {
              reason = "source/model exponent mismatch (format conversion)";
            } else if (input_info->n_planes == 3 &&
                (input_info->stride[0] != input_info->stride[1] ||
                    input_info->stride[1] != input_info->stride[2])) {
              reason = "RGBP plane strides differ (zero-copy requires R/G/B same pitch)";
            } else {
              reason = "RGBP stride not CAVALRY-aligned";
            }
            if (gst_is_amba_cavalry_memory (memory))
              in_mfd = gst_amba_cavalry_memory_get_fd (memory);
            else if (gst_is_fd_memory (memory))
              in_mfd = gst_fd_memory_get_fd (memory);
            g_printerr ("[mlinference2]: NN input copy path: %s mem_fd=%d\n", reason, in_mfd);
            inf->logged_input_path_once = TRUE;
          }
        }
        // Copy mode: allocate input buffer if needed
        if (ctx->input_mem_virt == NULL) {
          if (cavalry_mem_alloc_mfd_cv(dst->size, &ctx->input_mem_fd,
              (void **) &ctx->input_mem_virt, ctx->cache_en) < 0) {
            DPRINT_ERROR("cavalry_mem_alloc_mfd_cv failed for input buffer\n");
            rval = -1;
            break;
          }
          ctx->input_mem_size = dst->size;
          DPRINT_INFO("Allocated input buffer: size=0x%lX, mfd=%d\n",
              ctx->input_mem_size, ctx->input_mem_fd);
        }

        dst->virt = ctx->input_mem_virt;
        dst->mem_fd = ctx->input_mem_fd;
        dst->fd_offset = 0;

        unsigned char *nn_input = ctx->input_mem_virt;

        if (!need_convert) {
          for (int i = 0; i < input_info->n_planes; i++) {
            if (input_info->stride[i] != dst->dim.pitch) {
              unsigned char *s0 = input_info->data[i];
              for (unsigned int h = 0; h < input_info->height; h++) {
                memcpy(nn_input, s0, input_info->width);
                nn_input += dst->dim.pitch;
                s0 += input_info->stride[i];
              }
            } else {
              gsize ss = input_info->height * input_info->stride[i];
              memcpy(nn_input, input_info->data[i], ss);
              nn_input += ss;
            }
          }
        } else {
          /* 0.0.8.0->0.0.0.0: out=(in*255+128)/256; 0.0.0.0->0.0.8.0: out=(in*256+127)/255 */
          for (int i = 0; i < input_info->n_planes; i++) {
            unsigned char *s0 = input_info->data[i];
            for (unsigned int h = 0; h < input_info->height; h++) {
              for (unsigned int w = 0; w < input_info->width; w++) {
                unsigned int v = (unsigned int)s0[w];
                if (src_exp == 8 && model_exp == 0)
                  nn_input[w] = (unsigned char)((v * 255 + 128) / 256);
                else if (src_exp == 0 && model_exp == 8)
                  nn_input[w] = (unsigned char)((v * 256 + 127) / 255);
                else
                  nn_input[w] = (unsigned char)v;
              }
              nn_input += dst->dim.pitch;
              s0 += input_info->stride[i];
            }
          }
        }
      } else {
        GstMlInference2 *inf = GST_MLINFERENCE2 (trans);

        /* Zero-copy: same dmabuf; dst->dim.pitch = row bytes; dst->size = real GstMemory (incl. padding). */
        int input_fd = gst_fd_memory_get_fd (memory);
        if (input_fd > 0) {
          dst->dim.pitch = (unsigned int) (gsize) input_info->stride[0];
          dst->size = (unsigned long) gst_memory_get_sizes (memory, NULL, NULL);
          dst->virt = input_info->data[0];
          dst->mem_fd = input_fd;
          dst->fd_offset = 0;
          dst->update_pitch = dst->dim.pitch;
          zero_copy_dmabuf_frame = 1;

          if (!inf->logged_input_path_once) {
            g_printerr ("[mlinference2]: NN input zero-copy (Cavalry dmabuf), "
                "stride_0=%d, mem_fd=%d\n",
                input_info->stride[0], input_fd);
            inf->logged_input_path_once = TRUE;
          }
        } else {
          if (!inf->logged_input_path_once) {
            g_printerr ("[mlinference2]: NN input copy path: invalid dmabuf for NN "
                "(mem_fd=%d), using Cavalry temp buffer\n", input_fd);
            inf->logged_input_path_once = TRUE;
          }

          if (ctx->input_mem_virt == NULL) {
            if (cavalry_mem_alloc_mfd_cv(dst->size, &ctx->input_mem_fd,
                (void **) &ctx->input_mem_virt, ctx->cache_en) < 0) {
              DPRINT_ERROR("cavalry_mem_alloc_mfd_cv failed for input buffer\n");
              rval = -1;
              break;
            }
            ctx->input_mem_size = dst->size;
            DPRINT_INFO("Allocated input buffer: size=0x%lX, mfd=%d\n",
                ctx->input_mem_size, ctx->input_mem_fd);
          }
          dst->virt = ctx->input_mem_virt;
          dst->mem_fd = ctx->input_mem_fd;
          dst->fd_offset = 0;
          unsigned char *nn_input = ctx->input_mem_virt;
          for (int i = 0; i < input_info->n_planes; i++) {
            if (input_info->stride[i] != dst->dim.pitch) {
              unsigned char *s0 = input_info->data[i];
              for (unsigned int h = 0; h < input_info->height; h++) {
                memcpy(nn_input, s0, input_info->width);
                nn_input += dst->dim.pitch;
                s0 += input_info->stride[i];
              }
            } else {
              gsize ss = input_info->height * input_info->stride[i];
              memcpy(nn_input, input_info->data[i], ss);
              nn_input += ss;
            }
          }
        }
      }

    } else if (dst->data_fmt.size == 2) {
      // Allocate float32 input buffer if needed
      if (ctx->input_mem_virt == NULL) {
        if (cavalry_mem_alloc_mfd_cv(dst->size, &ctx->input_mem_fd,
            (void **) &ctx->input_mem_virt, ctx->cache_en) < 0) {
          DPRINT_ERROR("cavalry_mem_alloc_mfd_cv failed for float32 input buffer\n");
          rval = -1;
          break;
        }
        ctx->input_mem_size = dst->size;
        DPRINT_INFO("Allocated float32 input buffer: size=0x%lX, mfd=%d\n",
            ctx->input_mem_size, ctx->input_mem_fd);
      }

      // Point in_desc to our allocated buffer
      dst->virt = ctx->input_mem_virt;
      dst->mem_fd = ctx->input_mem_fd;
      dst->fd_offset = 0;

      // Convert uint8 -> float32
#if defined (DBUILD_AMBA_CAVALRY_V2)
      // Calculate U8 pitch for intermediate buffer (dst->dim.pitch is for float32)
      gsize u8_pitch = DROUND_UP(dst->dim.width * sizeof(unsigned char), CAVALRY_PORT_PITCH_ALIGN);

      // Allocate memory for intermediate uint8 data if needed
      gsize size = dst->dim.plane * dst->dim.depth * dst->dim.height * u8_pitch;
      if (ctx->conv_buf_size < size) {
        if (ctx->conv_buf_virt) {
          cavalry_mem_free_mfd(ctx->conv_buf_size, ctx->conv_buf_fd,
              ctx->conv_buf_virt);
        }
        ctx->conv_buf_size = size;
        if (cavalry_mem_alloc_mfd_cv(size, &ctx->conv_buf_fd,
            (void **) & (ctx->conv_buf_virt), ctx->cache_en) < 0) {
          DPRINT_ERROR("cavalry_mem_alloc_mfd_cv failed for intermediate buffer\n");
          return -2;
        }
      }

      // Copy RGBP data to intermediate buffer
      unsigned char *input_buf = ctx->conv_buf_virt;
      for (int i = 0; i < input_info->n_planes; i++) {
        if (CHECK_PITCH_ALIGN(input_info->stride[i])) {
          unsigned char *s0 = input_info->data[i];
          for (unsigned int h = 0; h < input_info->height; h++) {
            memcpy(input_buf, s0, input_info->width);
            input_buf += u8_pitch;
            s0 += input_info->stride[i];
          }
        } else {
          gsize ss = input_info->height * input_info->stride[i];
          memcpy(input_buf, input_info->data[i], ss);
          input_buf += ss;
        }
      }

#if ENABLE_CACHE_ON_NET_MEM
      if (ctx->cache_en) {
        cavalry_mem_sync_cache_mfd(ctx->conv_buf_size, 0, ctx->conv_buf_fd, 1, 0);
      }
#endif
      vect_desc_mfd_t src_desc = {0};
      vect_desc_mfd_t dst_desc = {0};

      src_desc.shape.p = dst->dim.plane;
      src_desc.shape.d = dst->dim.depth;
      src_desc.shape.h = dst->dim.height;
      src_desc.shape.w = dst->dim.width;
      src_desc.pitch = u8_pitch;
      src_desc.data_format.sign = 0;
      src_desc.data_format.datasize = 0;  // uint8
      src_desc.data_format.exp_offset = 0;
      src_desc.data_format.exp_bits = 0;
      src_desc.data_addr_fd = ctx->conv_buf_fd;
      src_desc.data_addr_offset = 0;

      dst_desc.shape.p = dst->dim.plane;
      dst_desc.shape.d = dst->dim.depth;
      dst_desc.shape.h = dst->dim.height;
      dst_desc.shape.w = dst->dim.width;
      dst_desc.pitch = dst->dim.pitch;
      dst_desc.data_format.sign = 1;
      dst_desc.data_format.datasize = 2;  // float32
      dst_desc.data_format.exp_offset = 0;
      dst_desc.data_format.exp_bits = 7;
      dst_desc.data_addr_fd = dst->mem_fd;
      dst_desc.data_addr_offset = dst->fd_offset;

      if (vproc_scale_ext_mfd(&src_desc, &dst_desc, 1) < 0) {
        DPRINT_ERROR("vproc_scale_ext_mfd failed for uint8->float32 conversion\n");
        rval = -1;
        break;
      }

#else
      // Software conversion: uint8 -> float32 directly from input
      unsigned char *f32_row = (unsigned char *)dst->virt;
      for (int i = 0; i < input_info->n_planes; i++) {
        unsigned char *s0 = input_info->data[i];
        for (unsigned int h = 0; h < input_info->height; h++) {
          float *f32_data = (float *)f32_row;
          for (unsigned int w = 0; w < input_info->width; w++) {
            f32_data[w] = (float)s0[w];
          }
          s0 += input_info->stride[i];
          f32_row += dst->dim.pitch;
        }
      }
#endif

    } else {
      DPRINT_ERROR("Unsupported NN input data format size: %d\n", dst->data_fmt.size);
      rval = -1;
      break;
    }

    // Sync cache for nn input buffer
#if ENABLE_CACHE_ON_NET_MEM
    if (ctx->cache_en) {
      cavalry_mem_sync_cache_mfd(dst->size, dst->fd_offset, dst->mem_fd, 1, 0);
    }
#endif

    if (!zero_copy_dmabuf_frame && ctx->nn_prev_was_zero_copy_dmabuf) {
      dst->update_pitch = dst->dim.pitch;
    }
    ctx->nn_prev_was_zero_copy_dmabuf = zero_copy_dmabuf_frame ? TRUE : FALSE;

    if (nnctrl_set_net_io_cfg_by_mfd(ctx->id, &ctx->input_cfg, NULL) < 0) {
      DPRINT_ERROR("nnctrl_set_net_io_cfg_by_mfd failed\n");
      rval = -1;
      break;
    }

  } while (0);

  return rval;
}

static int __nn_vp_forward_legacy(priv_ml_infer_ctx2_t *net,
    struct net_output_cfg *out_cfg)
{
  int rval = 0;

  if (net && out_cfg) {
    do {
#if 0 //ENABLE_CACHE_ON_NET_MEM
      if (net->cache_en) {
        for (unsigned int i = 0; i < out_cfg->out_num; i++) {
          memset (out_cfg->out_desc[i].virt, 0x0, out_cfg->out_desc[i].size);
          cavalry_mem_sync_cache (out_cfg->out_desc[i].size,
              out_cfg->out_desc[i].addr, 1, 0);

        }
      }
#endif
      net->run_cfg.net_loop_cnt = 1;
      net->run_cfg.no_auto_resume = net->abort_if_preempted;
      net->run_cfg.priority = net->priority;
      net->run_cfg.split_num_run = net->split_num;
      if (nnctrl_run_net(net->id, &net->result, &net->run_cfg,
            &net->io_input_cfg, out_cfg) < 0) {
        DPRINT_ERROR("nnctrl_run_net error\n");
        rval = -1;
      }
      net->vp_time_us = net->result.vp_time_us;
    } while (0);
  } else {
    DPRINT_ERROR("params error\n");
    rval = -1;
  }

  return rval;
}

static int __nn_vp_forward_mfd(priv_ml_infer_ctx2_t *net)
{
  int rval = 0;

  if (net) {
    do {
      /* Cavalry/nnctrl rejects fd<=0 (fd 0 is stdin, not dmabuf). Validate before run. */
      for (unsigned int i = 0; i < net->input_cfg.in_num; i++) {
        if (net->input_cfg.in_desc[i].mem_fd <= 0) {
          DPRINT_ERROR("Input %u mem_fd=%d invalid for CAVALRY_RUN_DAGS_MFD (dag-port fd:0?)\n",
              i, net->input_cfg.in_desc[i].mem_fd);
          rval = -1;
          break;
        }
      }
      if (rval < 0)
        break;
      for (unsigned int i = 0; i < net->output_cfg.out_num; i++) {
        if (net->output_cfg.out_desc[i].mem_fd <= 0) {
          DPRINT_ERROR("Output %u mem_fd=%d invalid for CAVALRY_RUN_DAGS_MFD (dag-port fd:0?)\n",
              i, net->output_cfg.out_desc[i].mem_fd);
          rval = -1;
          break;
        }
      }
      if (rval < 0)
        break;
#if 0 //ENABLE_CACHE_ON_NET_MEM
      if (net->cache_en) {
        /*for (unsigned int i = 0; i < net->input_cfg.in_num; i++) {
          // data will be read by VP
          cavalry_mem_sync_cache_mfd(net->input_cfg.in_desc[i].size,
              0, net->input_cfg.in_desc[i].mem_fd, 0, 1);
        }*/

        for (unsigned int i = 0; i < net->output_cfg.out_num; i++) {
          memset(net->output_cfg.out_desc[i].virt,
              0x0, net->output_cfg.out_desc[i].size);
          cavalry_mem_sync_cache_mfd(net->output_cfg.out_desc[i].size,
              0, net->output_cfg.out_desc[i].mem_fd, 1, 0);
        }
      }
#endif
      net->run_cfg.net_loop_cnt = 1;//batch
      net->run_cfg.no_auto_resume = net->abort_if_preempted;
      net->run_cfg.priority = net->priority;
      net->run_cfg.split_num_run = net->split_num;
      if (nnctrl_run_net_by_mfd(net->id, &net->result,
          &net->run_cfg, &net->input_cfg, &net->output_cfg) < 0) {
        DPRINT_ERROR("nnctrl_run_net error\n");
        rval = -1;
      }
      net->vp_time_us = net->result.vp_time_us;
    } while (0);
  } else {
    DPRINT_ERROR("params error\n");
    rval = -1;
  }

  return rval;
}

static int __nn_vp_forward(priv_ml_infer_ctx2_t *net)
{
  if (net->use_mfd)
    return __nn_vp_forward_mfd(net);
  return __nn_vp_forward_legacy(net, &net->io_output_cfg);
}


static gboolean
gst_ml_inference2_process_legacy (GstBaseTransform * trans,
    priv_ml_infer_ctx2_t * filter, img_data_info2_t * input_info, GstBuffer * outbuf,
    GstMemory *input_mem)
{
  gboolean ret = TRUE;
  GstMlInference2 *self = GST_MLINFERENCE2 (trans);
  GstMemory *output_mem = NULL;
  GstMapInfo output_map;
  gboolean output_mapped = FALSE;
  gboolean output_zero_copy = FALSE;
  guint n = 0;
  gsize out_offset = 0;
  struct net_output_cfg *run_out = NULL;
  guint n_out = ml2_output_num(filter);

  output_mem = gst_buffer_peek_memory(outbuf, 0);
  if (!output_mem) {
    GST_ERROR_OBJECT (trans, "No memory in output buffer");
    return FALSE;
  }

  if (!gst_memory_map(output_mem, &output_map, GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (trans, "Failed to map output buffer");
    return FALSE;
  }
  output_mapped = TRUE;

  ml2_legacy_run_out_reset (filter);
  run_out = &filter->legacy_run_out;

  if (gst_is_amba_cavalry_allocator_family (output_mem->allocator)
      || gst_amba_cavalry_buffer_get_slab_phys (outbuf) != 0) {
    output_zero_copy = ml2_legacy_bind_output_zero_copy (self, filter, outbuf,
        output_mem, &output_map, run_out);
  }

  if (!output_zero_copy) {
    if (!self->logged_output_path_once) {
      g_printerr ("[mlinference2]: NN output copy path (legacy): output not Cavalry "
          "or no phys (%s), using persistent buffers + memcpy\n",
          output_mem->allocator ? G_OBJECT_TYPE_NAME(output_mem->allocator) : "null");
      self->logged_output_path_once = TRUE;
    }
    if (ml2_legacy_ensure_output_bufs (filter, 1) < 0) {
      DPRINT_ERROR ("legacy output buffer alloc failed (copy path)\n");
      ret = FALSE;
      goto ML_END;
    }
  }

  if (__simple_data_type_convert_legacy (self, filter, input_info, input_mem) < 0) {
    DPRINT_ERROR("__simple_data_type_convert_legacy error\n");
    ret = FALSE;
    goto ML_END;
  }

  if (nnctrl_set_net_io_cfg (filter->id, NULL,
          output_zero_copy ? run_out : &filter->io_output_cfg) < 0) {
    DPRINT_ERROR ("nnctrl_set_net_io_cfg (legacy output) failed\n");
    ret = FALSE;
    goto ML_END;
  }

  if (__nn_vp_forward_legacy (filter,
          output_zero_copy ? run_out : &filter->io_output_cfg) < 0) {
    DPRINT_ERROR("nn_vp_forward error (legacy)\n");
    ret = FALSE;
    goto ML_END;
  }

#if ENABLE_CACHE_ON_NET_MEM
  if (filter->cache_en) {
    struct net_output_cfg *sync_out =
        output_zero_copy ? run_out : &filter->io_output_cfg;

    for (n = 0; n < n_out; n++) {
      cavalry_mem_sync_cache (sync_out->out_desc[n].size,
          sync_out->out_desc[n].addr, 0, 1);
    }
  }
#endif

  if (!output_zero_copy) {
    for (n = 0; n < n_out; n++) {
      gsize sz = filter->io_output_cfg.out_desc[n].size;

      if (!filter->io_output_cfg.out_desc[n].virt) {
        GST_ERROR_OBJECT (trans, "legacy output %u buffer not allocated", n);
        ret = FALSE;
        goto ML_END;
      }
      if (out_offset + sz > output_map.size) {
        GST_ERROR_OBJECT (trans,
            "legacy output %u size %zu exceeds Gst map %" G_GSIZE_FORMAT,
            n, (size_t) sz, output_map.size);
        ret = FALSE;
        goto ML_END;
      }
      memcpy (output_map.data + out_offset,
          filter->io_output_cfg.out_desc[n].virt, sz);
      out_offset += sz;
    }
  }

ML_END:
  if (output_mapped)
    gst_memory_unmap(output_mem, &output_map);

  return ret;
}


static gboolean
gst_ml_inference2_create_session (GstBaseTransform * trans)
{
  GstMlInference2 *self = GST_MLINFERENCE2 (trans);
  priv_ml_infer_ctx2_t * filter = self->priv_ctx;
  gboolean nn_disabled = FALSE;
  int ret = 0;

  GST_OBJECT_LOCK (self);

  if (filter->model_file[0] != '\0') {
    ret = __net_init(filter);
    if (ret < 0) {
      DPRINT_ERROR("net init error\n");
      nn_disabled = TRUE;
    }

  } else {
    nn_disabled = TRUE;
  }
  GST_OBJECT_UNLOCK (self);

  /* Do not passthrough: downstream (mlpostprocess) expects tensors, not video.
   * Fail start so pipeline fails early with clear cause. */
  if (nn_disabled) {
    GST_ELEMENT_ERROR (self, LIBRARY, INIT,
        ("NN init failed or no model file"), ("Set model= and ensure __net_init succeeds"));
    return FALSE;
  }

  {
    GstPad *srcpad = gst_element_get_static_pad (GST_ELEMENT (self), "src");
    GstCaps *srccaps = srcpad ? gst_pad_get_current_caps (srcpad) : NULL;

    if (!ml2_output_pool_ensure (self, srccaps, 0)) {
      if (!filter->use_mfd) {
        if (srccaps)
          gst_caps_unref (srccaps);
        if (srcpad)
          gst_object_unref (srcpad);
        GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND, NULL,
            ("amba_cavalry_phys allocator or output pool failed (legacy)"));
        return FALSE;
      }
      GST_WARNING_OBJECT (self,
          "output pool not ready at start (will retry in decide_allocation)");
    }
    if (srccaps)
      gst_caps_unref (srccaps);
    if (srcpad)
      gst_object_unref (srcpad);
  }

  return TRUE;
}

static gboolean
gst_ml_inference2_start (GstBaseTransform * trans)
{
  GstMlInference2 *self = GST_MLINFERENCE2 (trans);

  if ( !gst_ml_inference2_create_session (trans) ) {
    DPRINT_ERROR("ml_inference2_create error\n");
    return FALSE;
  }

  self->logged_input_path_once = FALSE;
  self->logged_output_path_once = FALSE;

  return TRUE;
}

static gboolean
gst_ml_inference2_stop (GstBaseTransform * trans)
{
  GstMlInference2 *self = GST_MLINFERENCE2 (trans);

  ml2_output_pool_destroy (self);
  __net_free (self->priv_ctx);

  return TRUE;
}

static gboolean
gst_ml_inference2_process (GstBaseTransform * trans, GstBuffer * inbuf, GstBuffer * outbuf)
{
  gboolean ret = TRUE;
  GstMlInference2 *self = GST_MLINFERENCE2 (trans);
  gint i = 0;
  guint n = 0;
  img_data_info2_t input_info;
  GstVideoMeta *vmeta = gst_buffer_get_video_meta (inbuf);
  GstMemory *memory = NULL;
  GstMapInfo map_info;
  GstMemory *output_mem = NULL;
  GstMapInfo output_map;
  gboolean output_mapped = FALSE;
  priv_ml_infer_ctx2_t * filter = self->priv_ctx;

  memory = gst_buffer_peek_memory(inbuf, 0);
  if (!gst_memory_map(memory, &map_info, GST_MAP_READ)) {
    GST_ERROR_OBJECT (trans, "failed to map memory");
    return FALSE;
  }

  if (vmeta) {
    // Use video meta if available
    if (vmeta->n_planes > GST_VIDEO_MAX_PLANES) {
      GST_ERROR_OBJECT (trans, "too many planes in video meta");
      gst_memory_unmap(memory, &map_info);
      return FALSE;
    }

    input_info.n_planes = vmeta->n_planes;
    input_info.format = vmeta->format;
    input_info.height = vmeta->height;
    input_info.width = vmeta->width;

    for (i = 0; i < (gint) vmeta->n_planes; i++) {
      input_info.data[i] = map_info.data + vmeta->offset[i];
      input_info.stride[i] = vmeta->stride[i];
    }
  } else {
    // Fall back to using GstVideoInfo from caps
    GstCaps *caps = gst_pad_get_current_caps(GST_BASE_TRANSFORM_SINK_PAD(trans));
    if (!caps) {
      GST_ERROR_OBJECT (trans, "no caps on sink pad");
      gst_memory_unmap(memory, &map_info);
      return FALSE;
    }

    GstVideoInfo vinfo;
    if (!gst_video_info_from_caps(&vinfo, caps)) {
      GST_ERROR_OBJECT (trans, "failed to parse video info from caps");
      gst_caps_unref(caps);
      gst_memory_unmap(memory, &map_info);
      return FALSE;
    }
    gst_caps_unref(caps);

    input_info.n_planes = GST_VIDEO_INFO_N_PLANES(&vinfo);
    input_info.format = GST_VIDEO_INFO_FORMAT(&vinfo);
    input_info.height = GST_VIDEO_INFO_HEIGHT(&vinfo);
    input_info.width = GST_VIDEO_INFO_WIDTH(&vinfo);

    for (i = 0; i < input_info.n_planes; i++) {
      input_info.data[i] = map_info.data + GST_VIDEO_INFO_PLANE_OFFSET(&vinfo, i);
      input_info.stride[i] = GST_VIDEO_INFO_PLANE_STRIDE(&vinfo, i);
    }

    /* Single-memory RGBP: buffer size may exceed GST_VIDEO_INFO_SIZE for two reasons:
     * (A) Row padding: each plane uses stride > width, buffer = 3 * stride * height.
     * (B) Tight pixels + tail slack: GstVideoInfo stride == width, image = cap_size bytes,
     *     pool/allocator rounds buffer up (videoconvert/videoscale common).*/
    if (input_info.format == GST_VIDEO_FORMAT_RGBP && input_info.n_planes == 3
        && gst_buffer_n_memory (inbuf) == 1) {
      gsize total = gst_buffer_get_size (inbuf);
      gsize cap_size = GST_VIDEO_INFO_SIZE (&vinfo);

      if (cap_size != total) {
        gboolean caps_tight_rgbp = TRUE;

        for (i = 0; i < 3; i++) {
          if ((guint) GST_VIDEO_INFO_PLANE_STRIDE (&vinfo, i) != (guint) input_info.width) {
            caps_tight_rgbp = FALSE;
            break;
          }
        }
        if (!caps_tight_rgbp || total <= cap_size) {
          if (total % 3 == 0) {
            gsize plane_sz = total / 3;

            if (plane_sz % (gsize) input_info.height == 0) {
              guint inf_stride = (guint) (plane_sz / (gsize) input_info.height);

              if (inf_stride >= input_info.width) {
                for (i = 0; i < 3; i++) {
                  input_info.stride[i] = inf_stride;
                  input_info.data[i] = map_info.data + (gsize) i * plane_sz;
                }
              }
            }
          }
        }
      }
    }
  }

  if (!filter->use_mfd) {
    ret = gst_ml_inference2_process_legacy (trans, filter, &input_info, outbuf, memory);
    gst_memory_unmap(memory, &map_info);
    return ret;
  }

  // Zero-copy output: point NN output to Gst buffer (from out_pool or direct alloc)
  output_mem = gst_buffer_peek_memory(outbuf, 0);
  if (!output_mem) {
    GST_ERROR_OBJECT (trans, "No memory in output buffer");
    ret = FALSE;
    goto ML_END;
  }

  gsize total_out_size = 0;
  for (n = 0; n < filter->output_cfg.out_num; n++)
    total_out_size += filter->output_cfg.out_desc[n].size;

  if (!gst_memory_map(output_mem, &output_map, GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (trans, "Failed to map output buffer (allocator=%s). "
        "prepare_output_buffer should provide cavalry buffers.",
        output_mem->allocator ? G_OBJECT_TYPE_NAME(output_mem->allocator) : "null");
    ret = FALSE;
    goto ML_END;
  }
  output_mapped = TRUE;

  /* NN requires Cavalry/DMA memory. If output buffer is not cavalry-backed
   * (e.g. cavalry allocator unavailable or downstream rejected our pool),
   * allocate a temp cavalry buffer, run NN into it, then copy to output. */
  {
    gboolean use_zero_copy = FALSE;
    if (gst_is_amba_cavalry_memory(output_mem)) {
      int output_fd = gst_amba_cavalry_memory_get_fd(output_mem);
      if (output_fd > 0) {
        /* Zero-copy path: output buffer is already DMA-backed */
        unsigned char *output_base = output_map.data;
        gsize output_offset = 0;

        for (n = 0; n < filter->output_cfg.out_num; n++) {
          filter->output_cfg.out_desc[n].virt = output_base + output_offset;
          filter->output_cfg.out_desc[n].mem_fd = output_fd;
          filter->output_cfg.out_desc[n].fd_offset = output_offset;
          output_offset += filter->output_cfg.out_desc[n].size;
        }
        use_zero_copy = TRUE;
        if (!self->logged_output_path_once) {
          g_printerr ("[mlinference2]: NN output zero-copy (Cavalry dmabuf), stride_0=%d, mem_fd=%d\n",
            (unsigned int) filter->output_cfg.out_desc[0].dim.pitch, output_fd);
          self->logged_output_path_once = TRUE;
        }
      } else {
        /* fd 0 or -1 invalid for dmabuf; cavalry CAVALRY_RUN_DAGS_MFD rejects it */
        if (!self->logged_output_path_once) {
          g_printerr ("[mlinference2]: NN output: output mem_fd=%d invalid for dmabuf, using copy path\n",
              output_fd);
          self->logged_output_path_once = TRUE;
        }
      }
    } else {
      if (!self->logged_output_path_once) {
        gint out_mfd = -1;

        if (gst_is_amba_cavalry_memory (output_mem))
          out_mfd = gst_amba_cavalry_memory_get_fd (output_mem);
        else if (gst_is_fd_memory (output_mem))
          out_mfd = gst_fd_memory_get_fd (output_mem);
        g_printerr ("[mlinference2]: NN output copy path: output not Cavalry/DMA (%s), "
            "out_mem_fd=%d, temp Cavalry buffer + memcpy\n",
            output_mem->allocator ? G_OBJECT_TYPE_NAME(output_mem->allocator) : "null",
            out_mfd);
        self->logged_output_path_once = TRUE;
      }
    }
    if (!use_zero_copy) {
      /* Copy path: allocate temp cavalry buffer for NN, then copy to output */
      GstAllocator *cav_alloc = gst_amba_cavalry_allocator_get();
      if (!cav_alloc) {
        GST_ERROR_OBJECT (trans, "Output buffer is not Cavalry memory and cavalry allocator unavailable");
        ret = FALSE;
        goto ML_END;
      }
      GstAllocationParams alloc_params;
      gst_allocation_params_init(&alloc_params);
      GstMemory *cav_mem = gst_allocator_alloc(cav_alloc, total_out_size, &alloc_params);
      gst_object_unref(cav_alloc);
      if (!cav_mem) {
        GST_ERROR_OBJECT (trans, "Failed to allocate Cavalry buffer for NN output");
        ret = FALSE;
        goto ML_END;
      }
      GstMapInfo cav_map;
      if (!gst_memory_map(cav_mem, &cav_map, GST_MAP_READWRITE)) {
        gst_memory_unref(cav_mem);
        ret = FALSE;
        goto ML_END;
      }
      {
        unsigned char *cav_base = cav_map.data;
        gsize cav_offset = 0;
        int cav_fd = gst_amba_cavalry_memory_get_fd(cav_mem);

        if (cav_fd <= 0) {
          GST_ERROR_OBJECT (trans, "Cavalry temp buffer fd=%d invalid for dmabuf", cav_fd);
          gst_memory_unmap(cav_mem, &cav_map);
          gst_memory_unref(cav_mem);
          ret = FALSE;
          goto ML_END;
        }
        for (n = 0; n < filter->output_cfg.out_num; n++) {
          filter->output_cfg.out_desc[n].virt = cav_base + cav_offset;
          filter->output_cfg.out_desc[n].mem_fd = cav_fd;
          filter->output_cfg.out_desc[n].fd_offset = cav_offset;
          cav_offset += filter->output_cfg.out_desc[n].size;
        }
      }

      if (__simple_data_type_convert(trans, filter, &input_info, memory) < 0) {
        gst_memory_unmap(cav_mem, &cav_map);
        gst_memory_unref(cav_mem);
        ret = FALSE;
        goto ML_END;
      }

      if (__nn_vp_forward(filter) < 0) {
        gst_memory_unmap(cav_mem, &cav_map);
        gst_memory_unref(cav_mem);
        ret = FALSE;
        goto ML_END;
      }

#if ENABLE_CACHE_ON_NET_MEM
      if (filter->cache_en) {
        for (n = 0; n < filter->output_cfg.out_num; n++) {
          cavalry_mem_sync_cache_mfd(filter->output_cfg.out_desc[n].size,
              filter->output_cfg.out_desc[n].fd_offset,
              filter->output_cfg.out_desc[n].mem_fd, 0, 1);
        }
      }
#endif
      memcpy(output_map.data, cav_map.data, total_out_size);
      gst_memory_unmap(cav_mem, &cav_map);
      gst_memory_unref(cav_mem);
      goto ML_END;  /* Skip normal convert + forward + sync below */
    }
  }

  if (__simple_data_type_convert(trans, filter, &input_info, memory) < 0) {
    DPRINT_ERROR("__simple_data_type_convert error\n");
    ret = FALSE;
    goto ML_END;
  }

  if (__nn_vp_forward(filter) < 0) {
    DPRINT_ERROR("nn_vp_forward error\n");
    ret = FALSE;
    goto ML_END;
  }

#if ENABLE_CACHE_ON_NET_MEM
  /* Sync output cache for CPU read (e.g. by mlpostprocess) */
  if (filter->cache_en) {
    for (n = 0; n < filter->output_cfg.out_num; n++) {
      cavalry_mem_sync_cache_mfd(filter->output_cfg.out_desc[n].size,
          filter->output_cfg.out_desc[n].fd_offset,
          filter->output_cfg.out_desc[n].mem_fd, 0, 1);
    }
  }
#endif

ML_END:
  if (output_mapped)
    gst_memory_unmap(output_mem, &output_map);
  gst_memory_unmap(memory, &map_info);

  return ret;
}

static const char* get_ml_tensor_data_type_string(int data_size)
{
  /* Return actual NN output format - f16 conversion is done in mlpostprocess */
  if (data_size == 1)
    return "float16";
  if (data_size == 2)
    return "float32";
  return "float32";
}

static GstCaps *
gst_ml_inference2_transform_caps (GstBaseTransform * trans, GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstMlInference2 *self = GST_MLINFERENCE2 (trans);
  priv_ml_infer_ctx2_t *filter_ctx = self->priv_ctx;
  GstCaps *result = NULL;

  // Get framerate from input caps if available
  gint fps_n = 0, fps_d = 1;
  gboolean has_framerate = FALSE;
  if (gst_caps_get_size(caps) > 0) {
    GstStructure *in_struct = gst_caps_get_structure(caps, 0);
    has_framerate = gst_structure_get_fraction(in_struct, "framerate", &fps_n, &fps_d);
  }

  if (direction == GST_PAD_SINK) {
    // Given sink caps (video/x-raw), return what src pad can produce
    GstStructure *structure;

    if (filter_ctx && ml2_output_num(filter_ctx) > 0) {
      /* Clamp to MIN(MAX_IO_NUM, AMBA_ML_MAX_TENSORS); keep caps strings and num_tensors aligned. */
      unsigned int n_out = ml2_output_num(filter_ctx);
      unsigned int n_max = (unsigned int) MIN (MAX_IO_NUM, AMBA_ML_MAX_TENSORS);
      if (n_out > n_max) {
        GST_WARNING_OBJECT (trans, "Network has %u output tensors; caps support up to %u, clamping",
            ml2_output_num(filter_ctx), n_max);
        n_out = n_max;
      }
      // Build types, dimensions, pitches and names strings
      GString *types_str = g_string_new("");
      GString *dimensions_str = g_string_new("");
      GString *pitches_str = g_string_new("");
      GString *names_str = g_string_new("");

      for (unsigned int i = 0; i < n_out; i++) {
        struct io_dim *dim = ml2_out_dim(filter_ctx, i);
        struct io_data_fmt *fmt = ml2_out_data_fmt(filter_ctx, i);
        int width = dim->width;
        int height = dim->height;
        int channels = dim->depth;
        int pitch = dim->pitch;
        int data_size = fmt->size;
        const char* data_type = get_ml_tensor_data_type_string(data_size);

        // Add comma separator for multiple tensors
        if (i > 0) {
          g_string_append(types_str, ",");
          g_string_append(dimensions_str, ",");
          g_string_append(pitches_str, ",");
          g_string_append(names_str, ",");
        }

        // Add type
        g_string_append(types_str, data_type);

        g_string_append_printf(dimensions_str, "%d:%d:%d:1", width, height, channels);

        g_string_append_printf(pitches_str, "%d", pitch);

        // Add tensor name for mlpostprocess lookup
        const char *nm = filter_ctx->output_name[i];
        g_string_append(names_str, nm && nm[0] ? nm : "");
      }

      structure = gst_structure_new (GST_AMBA_ML_TENSORS_CAPS,
          "num_tensors", G_TYPE_INT, (gint) n_out,
          "types", G_TYPE_STRING, types_str->str,
          "dimensions", G_TYPE_STRING, dimensions_str->str,
          "pitches", G_TYPE_STRING, pitches_str->str,
          "names", G_TYPE_STRING, names_str->str,
          "format", G_TYPE_STRING, "static",
          NULL);

      if (ml2_input_num(filter_ctx) > 0) {
        struct io_dim *in_dim = ml2_in_dim(filter_ctx, 0);
        gchar nn_input_res[32];
        g_snprintf(nn_input_res, sizeof(nn_input_res), "%lux%lu",
            (unsigned long)in_dim->width, (unsigned long)in_dim->height);
        gst_structure_set(structure, "nn_input_res", G_TYPE_STRING, nn_input_res, NULL);
      }

      if (has_framerate) {
        gst_structure_set(structure, "framerate", GST_TYPE_FRACTION, fps_n, fps_d, NULL);
      }

      g_string_free(types_str, TRUE);
      g_string_free(dimensions_str, TRUE);
      g_string_free(pitches_str, TRUE);
      g_string_free(names_str, TRUE);

      result = gst_caps_new_full (structure, NULL);
    } else {
      // Fallback caps if neural network not initialized
      structure = gst_structure_new (GST_AMBA_ML_TENSORS_CAPS,
          "num_tensors", G_TYPE_INT, 1,
          "types", G_TYPE_STRING, "float32",
          "dimensions", G_TYPE_STRING, "1:1:1:1",
          "pitches", G_TYPE_STRING, "4",
          "nn_input_res", G_TYPE_STRING, "416x416",
          "format", G_TYPE_STRING, "static",
          NULL);

      if (has_framerate) {
        gst_structure_set(structure, "framerate", GST_TYPE_FRACTION, fps_n, fps_d, NULL);
      }

      result = gst_caps_new_full (structure, NULL);
    }
  } else {
    // Given src caps, return what sink pad can accept (video/x-raw)
    if (filter_ctx && ml2_input_num(filter_ctx) > 0) {
      struct io_dim *in_dim = ml2_in_dim(filter_ctx, 0);
      int nn_width = in_dim->width;
      int nn_height = in_dim->height;

      // Create caps with RGBP format requirement
      // Use string format to match capsfilter format type
      GstStructure *structure = gst_structure_new ("video/x-raw",
          "format", G_TYPE_STRING, "RGBP",
          "width", G_TYPE_INT, nn_width,
          "height", G_TYPE_INT, nn_height,
          NULL);

      if (has_framerate) {
        gst_structure_set(structure, "framerate", GST_TYPE_FRACTION, fps_n, fps_d, NULL);
      }

      result = gst_caps_new_full (structure, NULL);

      GST_INFO_OBJECT (trans, "NN requires input: %dx%d, format=RGBP",
                       nn_width, nn_height);
    } else {
      // Fallback to template caps if NN not initialized
      result = gst_pad_get_pad_template_caps (GST_BASE_TRANSFORM_SINK_PAD (trans));
    }
  }

  //GST_DEBUG_OBJECT (trans, "transform_caps: direction=%s, caps=%" GST_PTR_FORMAT ", filter=%" GST_PTR_FORMAT ", result_before_intersect=%" GST_PTR_FORMAT,
      //direction == GST_PAD_SINK ? "SINK" : "SRC", caps, filter, result);

  if (filter && result && gst_caps_get_size(filter) > 0 && gst_caps_get_size(result) > 0) {
    // Only intersect if filter and result have compatible media types
    GstStructure *result_struct = gst_caps_get_structure(result, 0);
    GstStructure *filter_struct = gst_caps_get_structure(filter, 0);
    const gchar *result_name = gst_structure_get_name(result_struct);
    const gchar *filter_name = gst_structure_get_name(filter_struct);

    //GST_DEBUG_OBJECT (trans, "result_name=%s, filter_name=%s", result_name, filter_name);

    if (g_strcmp0(result_name, filter_name) == 0) {
      GstCaps *tmp = gst_caps_intersect (result, filter);
      gst_caps_unref (result);
      result = tmp;
      //GST_DEBUG_OBJECT (trans, "after intersect: result=%" GST_PTR_FORMAT, result);
    }
    // If media types don't match, skip intersection (cross-type transform)
  }

  //GST_DEBUG_OBJECT (trans, "transform_caps: direction=%s, final result=%" GST_PTR_FORMAT,
      //direction == GST_PAD_SINK ? "SINK" : "SRC", result);

  return result;
}

static GstFlowReturn
gst_ml_inference2_prepare_output_buffer (GstBaseTransform * trans, GstBuffer * inbuf, GstBuffer ** outbuf)
{
  GstMlInference2 *self = GST_MLINFERENCE2 (trans);
  priv_ml_infer_ctx2_t *filter = self->priv_ctx;
  GstAllocator *allocator;
  GstBufferPool *pool = NULL;
  GstCaps *srccaps = NULL;
  GstPad *srcpad;
  gsize total_size = 0;
  GstFlowReturn flow = GST_FLOW_OK;

  if (!filter || ml2_output_num (filter) == 0) {
    return GST_BASE_TRANSFORM_CLASS (parent_class)->prepare_output_buffer (trans,
        inbuf, outbuf);
  }

  srcpad = gst_element_get_static_pad (GST_ELEMENT (self), "src");
  if (srcpad) {
    srccaps = gst_pad_get_current_caps (srcpad);
    gst_object_unref (srcpad);
  }

  if (ml2_output_pool_ensure (self, srccaps, 0)) {
    GST_OBJECT_LOCK (self);
    if (self->out_pool)
      pool = gst_object_ref (self->out_pool);
    GST_OBJECT_UNLOCK (self);

    if (pool) {
      flow = gst_buffer_pool_acquire_buffer (pool, outbuf, NULL);
      gst_object_unref (pool);
      if (flow == GST_FLOW_OK && *outbuf) {
        ml2_output_buffer_strip_meta (*outbuf);
        ml2_copy_buffer_timestamps (*outbuf, inbuf);
        if (srccaps)
          gst_caps_unref (srccaps);
        return GST_FLOW_OK;
      }
      GST_WARNING_OBJECT (trans,
          "output pool acquire failed (%s), fallback to direct alloc",
          gst_flow_get_name (flow));
    }
  }

  if (srccaps)
    gst_caps_unref (srccaps);

  total_size = ml2_output_alloc_size (filter);
  allocator = ml2_output_allocator (filter);
  if (!allocator) {
    if (!filter->use_mfd) {
      GST_ERROR_OBJECT (trans,
          "amba_cavalry_phys allocator unavailable (legacy does not use mfd)");
      return GST_FLOW_ERROR;
    }
    GST_WARNING_OBJECT (trans, "Cavalry allocator unavailable, using default");
    return GST_BASE_TRANSFORM_CLASS (parent_class)->prepare_output_buffer (trans,
        inbuf, outbuf);
  }

  *outbuf = gst_buffer_new_allocate (allocator, total_size, NULL);
  gst_object_unref (allocator);
  if (!*outbuf) {
    GST_ERROR_OBJECT (trans, "Failed to allocate output buffer");
    return GST_FLOW_ERROR;
  }

  ml2_copy_buffer_timestamps (*outbuf, inbuf);
  return GST_FLOW_OK;
}

static gboolean
gst_ml_inference2_decide_allocation (GstBaseTransform * trans, GstQuery * query)
{
  GstBufferPool *pool = NULL;
  guint size, min, max;
  GstCaps *caps = NULL;

  // Parse allocation query to get caps
  gst_query_parse_allocation (query, &caps, NULL);
  if (!caps) {
    GST_ERROR_OBJECT (trans, "No caps in allocation query");
    return FALSE;
  }

  guint total_size = 0;
  GstMlInference2 *self = GST_MLINFERENCE2 (trans);
  priv_ml_infer_ctx2_t *filter = self->priv_ctx;

  /* Use actual pitch/size from NN load (like input), fallback to caps when NN not ready */
  if (filter && ml2_output_num(filter) > 0) {
    total_size = ml2_output_alloc_size (filter);
    for (unsigned int i = 0; i < ml2_output_num(filter); i++) {
      GST_INFO_OBJECT (trans, "Tensor %u size=0x%lX (from NN)",
          i, (unsigned long)ml2_out_size(filter, i));
    }
  } else {
    GstStructure *structure = gst_caps_get_structure(caps, 0);
    gint num_tensors;
    if (!gst_structure_get_int(structure, "num_tensors", &num_tensors)) {
      GST_ERROR_OBJECT (trans, "No num_tensors in caps");
      return FALSE;
    }
    const gchar *types_str = gst_structure_get_string(structure, "types");
    const gchar *dimensions_str = gst_structure_get_string(structure, "dimensions");
    const gchar *pitches_str = gst_structure_get_string(structure, "pitches");
    if (!types_str || !dimensions_str) {
      GST_ERROR_OBJECT (trans, "No types or dimensions in caps");
      return FALSE;
    }
    gchar **types = g_strsplit(types_str, ",", -1);
    gchar **dimensions = g_strsplit(dimensions_str, ",", -1);
    gchar **pitches = pitches_str ? g_strsplit(pitches_str, ",", -1) : NULL;
    for (int i = 0; i < num_tensors && types[i] && dimensions[i]; i++) {
      gchar **dims = g_strsplit(dimensions[i], ":", -1);
      if (g_strv_length(dims) >= 3) {
        gint width = atoi(dims[0]);
        gint height = atoi(dims[1]);
        gint channels = atoi(dims[2]);
        guint elem_size = (g_strcmp0(types[i], "float16") == 0) ? 2 : 4;
        guint pitch;
        if (pitches && pitches[i]) {
          pitch = (guint)atoi(pitches[i]);
          if (pitch == 0)
            pitch = ALIGN_PITCH((size_t)width * elem_size);
        } else {
          pitch = ALIGN_PITCH((size_t)width * elem_size);
        }
        total_size += height * channels * pitch;
      }
      g_strfreev(dims);
    }
    g_strfreev(types);
    g_strfreev(dimensions);
    if (pitches)
      g_strfreev(pitches);
  }

  /* Cavalry output pool: same pool as prepare_output_buffer acquire path. */
  size = total_size;
  min = max = 0;

  if (total_size > 0
      && ml2_output_pool_ensure (self, caps, (gsize) total_size)) {
    GST_OBJECT_LOCK (self);
    pool = self->out_pool ? gst_object_ref (self->out_pool) : NULL;
    GST_OBJECT_UNLOCK (self);
  }

  if (!pool) {
    if (filter && !filter->use_mfd) {
      GST_ERROR_OBJECT (trans,
          "output pool failed: amba_cavalry_phys required for legacy (use_mfd=FALSE)");
      return FALSE;
    }
    GST_WARNING_OBJECT (trans, "output pool ensure failed in decide_allocation");
    return GST_BASE_TRANSFORM_CLASS (parent_class)->decide_allocation (trans, query);
  }

  if (gst_query_get_n_allocation_pools (query) > 0) {
    GstBufferPool *downstream_pool = NULL;
    guint dmin, dmax, qsize = 0;

    gst_query_parse_nth_allocation_pool (query, 0, &downstream_pool, &qsize, &dmin, &dmax);
    size = MAX (size, (guint) total_size);
    size = MAX (size, qsize);
    if (downstream_pool)
      gst_object_unref (downstream_pool);
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  } else {
    gst_query_add_allocation_pool (query, pool, size, min, max);
  }

  gst_object_unref (pool);

  return GST_BASE_TRANSFORM_CLASS (parent_class)->decide_allocation (trans, query);
}

static gboolean
gst_ml_inference2_propose_allocation (GstBaseTransform * trans, GstQuery * decide_query, GstQuery * query)
{
  GstMlInference2 *self = GST_MLINFERENCE2 (trans);
  priv_ml_infer_ctx2_t *filter = self->priv_ctx;
  GstAllocator *allocator;
  GstBufferPool *pool;
  guint size, min, max;
  GstStructure *config;
  GstCaps *caps = NULL;

  // Parse allocation query to get caps
  gst_query_parse_allocation (query, &caps, NULL);
  if (!caps) {
    GST_ERROR_OBJECT (trans, "No caps in allocation query");
    return FALSE;
  }

  // Use NN input buffer size if available (for zero-copy compatibility),
  // otherwise fall back to video info size
  if (filter && ml2_input_num(filter) > 0) {
    size = 0;
    for (unsigned int i = 0; i < ml2_input_num(filter); i++)
      size += ml2_in_size(filter, i);
  } else {
    GstVideoInfo info;
    if (!gst_video_info_from_caps (&info, caps)) {
      GST_ERROR_OBJECT (trans, "Failed to parse video info from caps");
      return FALSE;
    }
    size = GST_VIDEO_INFO_SIZE (&info);
  }

  min = max = 0;

  // Get cavalry allocator
  allocator = gst_amba_cavalry_allocator_get();
  if (!allocator) {
    GST_WARNING_OBJECT (trans, "gst_amba_cavalry_allocator_get return NULL, use default!");
  }

  // Create buffer pool for input
  pool = gst_buffer_pool_new ();

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_allocator (config, allocator, NULL);

  if (caps)
    gst_buffer_pool_config_set_params (config, caps, size, min, max);

  if (gst_query_find_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL)) {
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);
  }

  gst_buffer_pool_set_config (pool, config);

  gst_query_add_allocation_pool (query, pool, size, min, max);

  if (pool)
    gst_object_unref (pool);
  if (allocator) {
    gst_object_unref(allocator);
    return TRUE;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->propose_allocation (trans, decide_query, query);
}

static GstFlowReturn
gst_ml_inference2_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  //GstClockTime start = GST_CLOCK_TIME_NONE, end = GST_CLOCK_TIME_NONE;

  /* Copy PTS/DTS from input (camsrc) to output (tensor) for mlpostprocess sync */
  if (GST_BUFFER_PTS_IS_VALID (inbuf))
    GST_BUFFER_PTS (outbuf) = GST_BUFFER_PTS (inbuf);
  if (GST_BUFFER_DTS_IS_VALID (inbuf))
    GST_BUFFER_DTS (outbuf) = GST_BUFFER_DTS (inbuf);
  if (GST_BUFFER_DURATION_IS_VALID (inbuf))
    GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (inbuf);

  //start = gst_util_get_timestamp ();

  if (!gst_base_transform_is_passthrough (trans)
    && !gst_ml_inference2_process (trans, inbuf, outbuf)){
    GST_ELEMENT_WARNING (trans, STREAM, FAILED,
        ("ML Inference failed"), (NULL));
    return GST_FLOW_ERROR;
  }

  //end = gst_util_get_timestamp ();

  //GST_FIXME_OBJECT (trans, "ML running time: %" GST_TIME_FORMAT "\n", GST_TIME_ARGS (end - start));

  /* Pool/acquire buffer has no upstream meta; NN write does not preserve custom meta. */
  if (!amba_buffer_get_private_data_meta (outbuf) &&
      amba_buffer_get_private_data_meta (inbuf)) {
    amba_buffer_copy_private_data_meta (outbuf, inbuf);
  }

  return GST_FLOW_OK;
}


