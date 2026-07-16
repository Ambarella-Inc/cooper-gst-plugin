/*
 * gstambaoverlaydraw.c
 *
 * History:
 *    3/3/2026 - [pxduan] created file
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
 * SECTION:element-amba_overlay_draw
 * @title: amba_overlay_draw
 * @see_also: amba_draw_data_gen, mlinference2, mlpostprocess
 *
 * Sink element: receives draw data from amba_draw_data_gen and draws onto overlay hardware.
 * Used for live mode (camsrc) with sync_pts. Supports multi-pad (sink_1, sink_2, ...).
 * On BUILD_DSP_AMBA_V6, draw_format in each block may be 8/16/32bit like amba_venc_overlay;
 * it must match the stream overlay pixel format from IAV (see draw-format on amba_draw_data_gen).
 *
 * Input: application/x-amba-draw-data.
 * Downstream: none (sink).
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * amba_draw_data_gen ! amba_overlay_draw stream_id=0 sync_pts=1 osd_offset=0 osd_size=4163584 refresh-interval=3
 * ]|
 * </refsect2>
 */

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include "iav_ctx.h"
#include "common_err_code_c.h"
#include "debug_log.h"
#include "platform_al.h"

#include "internal.h"
#include "overlay_common.h"
#include "draw_data_caps.h"
#include "gstambaoverlaydraw.h"
#include "amba_private_data.h"

GST_DEBUG_CATEGORY_STATIC(gst_amba_overlay_draw_debug);
#define GST_CAT_DEFAULT gst_amba_overlay_draw_debug

enum {
  PROP_0,
  PROP_STREAM_ID,
  PROP_OSD_OFFSET,
  PROP_OSD_SIZE,
  PROP_COORD_RES,
  PROP_SYNC_WITH_PTS,
  PROP_OSD_INSERT_ALWAYS,
  PROP_BUF_NUM,
  PROP_SLEEP_TIME,
  PROP_REFRESH_INTERVAL,
};

/* Use ANY for flexible caps negotiation; set_caps validates application/x-amba-draw-data.
 * Strict draw-data caps in template caused link failure with amba_draw_data_gen when
 * GstBaseTransform passes upstream caps (image/bmp) as filter during negotiation. */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY
    );

static GstStaticPadTemplate sink_request_factory = GST_STATIC_PAD_TEMPLATE("sink_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY
    );

#define gst_amba_overlay_draw_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE(GstAmbaOverlayDraw, gst_amba_overlay_draw, GST_TYPE_BIN,
    GST_DEBUG_CATEGORY_INIT(gst_amba_overlay_draw_debug, "amba_overlay_draw", 0, "overlay draw sink"));

/* Internal GstBaseSink: does the actual overlay drawing. Reads aux cache from parent bin. */
typedef struct _GstAmbaOverlayDrawSink GstAmbaOverlayDrawSink;
typedef struct _GstAmbaOverlayDrawSinkClass GstAmbaOverlayDrawSinkClass;
#define GST_TYPE_AMBA_OVERLAY_DRAW_SINK (gst_amba_overlay_draw_sink_get_type())
#define GST_AMBA_OVERLAY_DRAW_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_AMBA_OVERLAY_DRAW_SINK, GstAmbaOverlayDrawSink))
struct _GstAmbaOverlayDrawSink { GstBaseSink parent; };
struct _GstAmbaOverlayDrawSinkClass { GstBaseSinkClass parent_class; };
G_DEFINE_TYPE(GstAmbaOverlayDrawSink, gst_amba_overlay_draw_sink, GST_TYPE_BASE_SINK);

/* Internal GstElement: chain-only, stores buffers to parent bin's aux cache. */
typedef struct _GstAmbaOverlayDrawAux GstAmbaOverlayDrawAux;
typedef struct _GstAmbaOverlayDrawAuxClass GstAmbaOverlayDrawAuxClass;
typedef struct _GstAmbaOverlayDrawAuxPrivate {
  gchar *pad_name;
} GstAmbaOverlayDrawAuxPrivate;
#define GST_TYPE_AMBA_OVERLAY_DRAW_AUX (gst_amba_overlay_draw_aux_get_type())
#define GST_AMBA_OVERLAY_DRAW_AUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_AMBA_OVERLAY_DRAW_AUX, GstAmbaOverlayDrawAux))
struct _GstAmbaOverlayDrawAux { GstElement parent; };
struct _GstAmbaOverlayDrawAuxClass { GstElementClass parent_class; };
G_DEFINE_TYPE_WITH_PRIVATE(GstAmbaOverlayDrawAux, gst_amba_overlay_draw_aux, GST_TYPE_ELEMENT);

static void gst_amba_overlay_draw_finalize(GObject *object);
static void gst_amba_overlay_draw_set_property(GObject *object, guint prop_id,
    const GValue *value, GParamSpec *pspec);
static void gst_amba_overlay_draw_get_property(GObject *object, guint prop_id,
    GValue *value, GParamSpec *pspec);
static GstPad *gst_amba_overlay_draw_request_new_pad(GstElement *element,
    GstPadTemplate *templ, const gchar *req_name, const GstCaps *caps);
static void gst_amba_overlay_draw_release_pad(GstElement *element, GstPad *pad);
static void gst_amba_overlay_draw_disable_overlay(overlay_draw_priv_t *priv);

#ifndef OVERLAY_WIDTH_ALIGN
#define OVERLAY_WIDTH_ALIGN 4
#endif
#ifndef OVERLAY_HEIGHT_ALIGN
#define OVERLAY_HEIGHT_ALIGN 4
#endif
#ifndef OVERLAY_X_OFFSET_ALIGN
#define OVERLAY_X_OFFSET_ALIGN 2
#endif
#ifndef OVERLAY_Y_OFFSET_ALIGN
#define OVERLAY_Y_OFFSET_ALIGN 2
#endif

#if defined (BUILD_DSP_AMBA_V6)

static int overlay_draw_get_stream_overlay_pixel_format(iav_ctx_t *ctx, int stream_id)
{
  if (!ctx || !ctx->iav_al.f_get_stream_overlay_pixel_format)
    return -1;
  return ctx->iav_al.f_get_stream_overlay_pixel_format(ctx->iav_fd, stream_id);
}

static unsigned int overlay_draw_iav_bytes_per_pixel(unsigned int overlay_format)
{
  if (overlay_format >= IAV_OVERLAY_FORMAT_32BIT_FIRST &&
      overlay_format < IAV_OVERLAY_FORMAT_32BIT_LAST)
    return 4;
  if (overlay_format >= IAV_OVERLAY_FORMAT_16BIT_FIRST &&
      overlay_format < IAV_OVERLAY_FORMAT_16BIT_LAST)
    return 2;
  return 1;
}

/* Match amba_venc_overlay / hardware: draw_data block draw_format vs stream overlay pixel format. */
static int overlay_draw_fmt_matches_iav(int draw_fmt, int pixel_fmt)
{
  switch (draw_fmt) {
    case AMBA_DRAW_FORMAT_8BIT_CLUT:
      return pixel_fmt == IAV_OVERLAY_FORMAT_8BIT_CLUT8;
    case AMBA_DRAW_FORMAT_RGB565:
      return pixel_fmt == IAV_OVERLAY_FORMAT_16BIT_RGB565;
    case AMBA_DRAW_FORMAT_UYV565:
      return pixel_fmt == IAV_OVERLAY_FORMAT_16BIT_UYV565;
    case AMBA_DRAW_FORMAT_BGR565:
      return pixel_fmt == IAV_OVERLAY_FORMAT_16BIT_BGR565;
    case AMBA_DRAW_FORMAT_AYUV4444:
      return pixel_fmt == IAV_OVERLAY_FORMAT_16BIT_AYUV4444;
    case AMBA_DRAW_FORMAT_RGBA4444:
      return pixel_fmt == IAV_OVERLAY_FORMAT_16BIT_RGBA4444;
    case AMBA_DRAW_FORMAT_BGRA4444:
      return pixel_fmt == IAV_OVERLAY_FORMAT_16BIT_BGRA4444;
    case AMBA_DRAW_FORMAT_ABGR4444:
      return pixel_fmt == IAV_OVERLAY_FORMAT_16BIT_ABGR4444;
    case AMBA_DRAW_FORMAT_ARGB4444:
      return pixel_fmt == IAV_OVERLAY_FORMAT_16BIT_ARGB4444;
    case AMBA_DRAW_FORMAT_AYUV1555:
      return pixel_fmt == IAV_OVERLAY_FORMAT_16BIT_AYUV1555;
    case AMBA_DRAW_FORMAT_YUV1555:
      return pixel_fmt == IAV_OVERLAY_FORMAT_16BIT_YUV1555;
    case AMBA_DRAW_FORMAT_RGBA5551:
      return pixel_fmt == IAV_OVERLAY_FORMAT_16BIT_RGBA5551;
    case AMBA_DRAW_FORMAT_BGRA5551:
      return pixel_fmt == IAV_OVERLAY_FORMAT_16BIT_BGRA5551;
    case AMBA_DRAW_FORMAT_ABGR1555:
      return pixel_fmt == IAV_OVERLAY_FORMAT_16BIT_ABGR1555;
    case AMBA_DRAW_FORMAT_ARGB1555:
      return pixel_fmt == IAV_OVERLAY_FORMAT_16BIT_ARGB1555;
    case AMBA_DRAW_FORMAT_AYUV8888:
      return pixel_fmt == IAV_OVERLAY_FORMAT_32BIT_AYUV8888;
    case AMBA_DRAW_FORMAT_RGBA8888:
      return pixel_fmt == IAV_OVERLAY_FORMAT_32BIT_RGBA8888;
    case AMBA_DRAW_FORMAT_BGRA8888:
      return pixel_fmt == IAV_OVERLAY_FORMAT_32BIT_BGRA8888;
    case AMBA_DRAW_FORMAT_ABGR8888:
      return pixel_fmt == IAV_OVERLAY_FORMAT_32BIT_ABGR8888;
    case AMBA_DRAW_FORMAT_ARGB8888:
      return pixel_fmt == IAV_OVERLAY_FORMAT_32BIT_ARGB8888;
    default:
      break;
  }
  return 0;
}

#endif /* BUILD_DSP_AMBA_V6 */

static int fill_overlay_data(overlay_draw_priv_t *thiz,
    unsigned int area_id, amba_overlay_area_attr_t *attr, guchar *content)
{
  int row, col, area_pitch, area_height, area_width, buf_id;
  guchar *dst, *tmp_dst, *src;
  guint pixel_size = thiz->pixel_size;
  guint pix = 0;

  iav_set_overlay_t *overlay_set = &thiz->overlay_set;

  buf_id = overlay_set->osd[area_id].buf_id + 1;
  buf_id = (buf_id >= attr->buf_num ? 0 : buf_id);
  overlay_set->overlay_insert.area[area_id].data_addr_offset = overlay_set->osd[area_id].buf_data[buf_id];
  {
    gulong offset = overlay_set->osd[area_id].buf_data[buf_id];
    gulong total_size = overlay_set->overlay_insert.area[area_id].total_size;
    if (offset + total_size > (gulong)thiz->iav_ctx->map_overlay.size) {
      DPRINT_ERROR("area %u: offset %lu + size %lu exceeds overlay buffer size %zu\n",
          area_id, (unsigned long)offset, (unsigned long)total_size,
          (size_t)thiz->iav_ctx->map_overlay.size);
      return -1;
    }
  }
  dst = thiz->iav_ctx->map_overlay.base + overlay_set->osd[area_id].buf_data[buf_id];
  area_pitch = overlay_set->overlay_insert.area[area_id].pitch;
  area_height = overlay_set->overlay_insert.area[area_id].height;
  area_width = overlay_set->overlay_insert.area[area_id].width;

  /* 8bit: CLUT background index. 16/32bit: clear to pixel 0 (typically transparent); venc_overlay uses
   * memset(BACKGROUND) for all bpp — keep this split unless padding must match venc exactly. */
  if (pixel_size == 1) {
    memset(dst, AMBA_DRAW_CLUT_ENTRY_BACKGROUND, overlay_set->overlay_insert.area[area_id].total_size);
  } else {
    memset(dst, 0, overlay_set->overlay_insert.area[area_id].total_size);
  }

  /* No rotate for overlay draw - direct copy */
  src = content;
  if (area_pitch == attr->rect.pitch) {
    memcpy(dst, src, area_pitch * area_height);
  } else {
    for (row = 0; row < area_height; row++) {
      src = content + row * attr->rect.pitch;
      memcpy(dst, src, area_width * pixel_size);
      dst = dst + area_pitch;
    }
  }
  overlay_set->osd[area_id].buf_id = buf_id;

  (void)tmp_dst;
  (void)col;
  (void)pix;

  return 0;
}

/* Effective attr/data change flags: GstAmbaDrawDataAreaFlagsMeta overrides header (per memory index). */
static void
overlay_effective_change_flags(GstBuffer *buf, unsigned int mem_idx,
    const osd_area_block_header_t *h, unsigned int *attr_out, unsigned int *data_out)
{
  GstAmbaDrawDataAreaFlagsMeta *m = gst_amba_draw_data_area_flags_meta_get(buf);
  unsigned int slot = osd_area_block_resolve_slot(h, mem_idx);
  if (m && (m->valid_mask & (1u << slot))) {
    guint8 f = m->flags[slot];
    *attr_out = (f & AMBA_DRAW_AREA_FLAG_ATTR_CHANGED) ? 1 : 0;
    *data_out = (f & AMBA_DRAW_AREA_FLAG_DATA_CHANGED) ? 1 : 0;
  } else {
    /* No meta: unknown producer; conservatively treat as full refresh */
    *attr_out = 1;
    *data_out = 1;
  }
}

/* TRUE if every area has valid hdr and both change flags clear (e.g. drawdatagen static/update cache). */
static gboolean overlay_draw_buffer_all_areas_no_change_flags(GstBuffer *buf, unsigned int area_count)
{
  unsigned int j;
  for (j = 0; j < area_count; j++) {
    GstMemory *mem = gst_buffer_peek_memory(buf, j);
    GstMapInfo map;
    if (!mem || !gst_memory_map(mem, &map, GST_MAP_READ))
      return FALSE;
    if (map.size < OSD_AREA_BLOCK_HEADER_SIZE) {
      gst_memory_unmap(mem, &map);
      return FALSE;
    }
    {
      osd_area_block_header_t *h = (osd_area_block_header_t *)map.data;
      unsigned int eff_a, eff_d;
      if (h->magic != OSD_AREA_BLOCK_MAGIC) {
        gst_memory_unmap(mem, &map);
        return FALSE;
      }
      overlay_effective_change_flags(buf, j, h, &eff_a, &eff_d);
      if (eff_a || eff_d) {
        gst_memory_unmap(mem, &map);
        return FALSE;
      }
    }
    gst_memory_unmap(mem, &map);
  }
  return TRUE;
}

/* Compute djb2 hash per area (per memory). One area = one memory; skip when all areas unchanged. */
static void overlay_draw_buffer_hash_per_area(GstBuffer *buf, guint32 *out_hash, gsize *out_size,
    unsigned int max_areas)
{
  unsigned int i, n = gst_buffer_n_memory(buf);
  if (n > max_areas) {
    n = max_areas;
  }
  for (i = 0; i < max_areas; i++) {
    out_hash[i] = 0;
    out_size[i] = 0;
  }
  for (i = 0; i < n; i++) {
    GstMemory *mem = gst_buffer_peek_memory(buf, i);
    GstMapInfo map;
    guint32 h = 5381;
    if (mem && gst_memory_map(mem, &map, GST_MAP_READ)) {
      gsize j;
      for (j = 0; j < map.size; j++) {
        h = ((h << 5) + h) + (guint32)map.data[j];
      }
      out_hash[i] = h;
      out_size[i] = map.size;
      gst_memory_unmap(mem, &map);
    }
  }
}

/* Merge per-area buffers: no global header, just append all area memories. */
static GstBuffer *merge_overlay_buffers(GstAmbaOverlayDraw *self,
    GstBuffer *primary, GList *aux_buffers)
{
  GstBuffer *merged = NULL;
  GstMemory *mem = NULL;
  unsigned int j, n;
  GList *l;
  GstBuffer *buf;

  (void)self;
  merged = gst_buffer_new();
  gst_amba_draw_data_area_flags_meta_merge_from_buffer(merged, primary);
  n = gst_buffer_n_memory(primary);
  /* gst_buffer_append_memory takes ownership of mem; caller must not unref */
  for (j = 0; j < n; j++) {
    mem = gst_buffer_get_memory(primary, j);
    if (mem) {
      gst_buffer_append_memory(merged, mem);
      if (gst_buffer_n_memory(merged) >= MAX_OVERLAY_AREA_NUM) {
        break;
      }
    }
  }
  /* Aux buffers are kept in parent bin hash; do not gst_buffer_get_memory (would steal and empty cache). */
  for (l = aux_buffers; l && gst_buffer_n_memory(merged) < MAX_OVERLAY_AREA_NUM; l = l->next) {
    buf = (GstBuffer *)l->data;
    gst_amba_draw_data_area_flags_meta_merge_from_buffer(merged, buf);
    n = gst_buffer_n_memory(buf);
    for (j = 0; j < n && gst_buffer_n_memory(merged) < MAX_OVERLAY_AREA_NUM; j++) {
      mem = gst_buffer_peek_memory(buf, j);
      if (mem) {
        gst_buffer_append_memory(merged, gst_memory_ref(mem));
      }
    }
  }
  return merged;
}

/* --- GstAmbaOverlayDrawSink (internal) --- */
static gboolean overlay_draw_sink_set_caps(GstBaseSink *sink, GstCaps *caps);
static void overlay_draw_sink_get_times(GstBaseSink *sink, GstBuffer *buffer,
    GstClockTime *start, GstClockTime *end);
static gboolean overlay_draw_sink_start(GstBaseSink *sink);
static gboolean overlay_draw_sink_stop(GstBaseSink *sink);
static GstFlowReturn overlay_draw_sink_render(GstBaseSink *sink, GstBuffer *buffer);

static GstStaticPadTemplate overlay_sink_sink_factory =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);

static void gst_amba_overlay_draw_sink_class_init(GstAmbaOverlayDrawSinkClass *klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS(klass);
  GstBaseSinkClass *base_sink_class = GST_BASE_SINK_CLASS(klass);

  gst_element_class_add_pad_template(gstelement_class,
      gst_static_pad_template_get(&overlay_sink_sink_factory));
  gst_element_class_set_static_metadata(gstelement_class,
      "Amba Overlay Draw Sink (internal)", "Sink", "Internal overlay sink", "Ambarella");

  base_sink_class->set_caps = GST_DEBUG_FUNCPTR(overlay_draw_sink_set_caps);
  base_sink_class->get_times = GST_DEBUG_FUNCPTR(overlay_draw_sink_get_times);
  base_sink_class->start = GST_DEBUG_FUNCPTR(overlay_draw_sink_start);
  base_sink_class->stop = GST_DEBUG_FUNCPTR(overlay_draw_sink_stop);
  base_sink_class->render = GST_DEBUG_FUNCPTR(overlay_draw_sink_render);
}

static void gst_amba_overlay_draw_sink_init(GstAmbaOverlayDrawSink *self)
{
  (void)self;
}

static overlay_draw_priv_t *overlay_draw_sink_get_priv(GstAmbaOverlayDrawSink *sink)
{
  GstElement *parent = GST_ELEMENT_PARENT(sink);
  GstAmbaOverlayDraw *bin = parent ? GST_AMBA_OVERLAY_DRAW(parent) : NULL;
  return bin ? bin->priv : NULL;
}

static gboolean overlay_draw_sink_set_caps(GstBaseSink *sink, GstCaps *caps)
{
  GstStructure *st;
  const gchar *name;
  overlay_draw_priv_t *priv = overlay_draw_sink_get_priv(GST_AMBA_OVERLAY_DRAW_SINK(sink));
  if (!caps || gst_caps_is_empty(caps)) {
    return FALSE;
  }
  st = gst_caps_get_structure(caps, 0);
  if (!st) {
    return FALSE;
  }
  name = gst_structure_get_name(st);
  if (!name || g_strcmp0(name, GST_AMBA_DRAW_DATA_CAPS) != 0) {
    return FALSE;
  }
  if (priv) {
    const gchar *cr = gst_structure_get_string(st, "coord_res");
    if (cr && cr[0]) {
      int w = 0, h = 0;
      if (sscanf(cr, "%dx%d", &w, &h) >= 2 && w > 0 && h > 0) {
        priv->coord_res_w = w;
        priv->coord_res_h = h;
        GST_INFO_OBJECT(sink, "coord_res from draw-data caps: %dx%d", w, h);
      }
    }
  }
  return TRUE;
}

static void overlay_draw_sink_get_times(GstBaseSink *sink, GstBuffer *buffer,
    GstClockTime *start, GstClockTime *end)
{
  overlay_draw_priv_t *priv = overlay_draw_sink_get_priv(GST_AMBA_OVERLAY_DRAW_SINK(sink));
  if (!priv) {
    *start = -1;
    *end = -1;
    return;
  }
  if (GST_BUFFER_TIMESTAMP_IS_VALID(buffer)) {
    *start = GST_BUFFER_TIMESTAMP(buffer);
    if (GST_BUFFER_DURATION_IS_VALID(buffer)) {
      *end = *start + GST_BUFFER_DURATION(buffer);
    } else {
      gint fps_n = priv->fps_n;
      gint fps_d = priv->fps_d;
      *end = (fps_n > 0 && fps_d > 0)
          ? *start + gst_util_uint64_scale_int(GST_SECOND, fps_d, fps_n)
          : *start + gst_util_uint64_scale_int(GST_SECOND, 1, 30);
    }
  } else {
    *start = *end = -1;
  }
}

static gboolean overlay_draw_sink_start(GstBaseSink *sink)
{
  overlay_draw_priv_t *priv = overlay_draw_sink_get_priv(GST_AMBA_OVERLAY_DRAW_SINK(sink));
  gulong osd_off, osd_sz;
  if (!priv || !priv->iav_ctx || !priv->iav_ctx->iav_fd_opened) {
    GST_ERROR_OBJECT(sink, "IAV not opened");
    return FALSE;
  }
  if (!priv->iav_ctx->map_overlay.base || priv->iav_ctx->map_overlay.size == 0) {
    GST_ERROR_OBJECT(sink, "Overlay buffer not mapped");
    return FALSE;
  }
  osd_off = priv->osd_offset;
  osd_sz = priv->osd_size;
  if (osd_sz > 0 && (osd_off >= (gulong)priv->iav_ctx->map_overlay.size ||
      osd_off + osd_sz > (gulong)priv->iav_ctx->map_overlay.size)) {
    GST_ERROR_OBJECT(sink, "osd_offset(%lu) + osd_size(%lu) exceeds overlay buffer size(%lu)",
      osd_off, osd_sz, priv->iav_ctx->map_overlay.size);
    return FALSE;
  }
  memset(&priv->stream_params, 0, sizeof(stream_param_t));
  {
    amba_dsp_enc_stream_format_t enc_fmt;
    int enc_w, enc_h;
    memset(&enc_fmt, 0, sizeof(enc_fmt));
    enc_fmt.id = (unsigned char)priv->stream_id;
    if (!priv->iav_ctx->iav_al.f_query_encode_stream_fmt ||
        priv->iav_ctx->iav_al.f_query_encode_stream_fmt(priv->iav_ctx->iav_fd, &enc_fmt) < 0) {
      GST_ERROR_OBJECT(sink, "f_query_encode_stream_fmt failed");
      return FALSE;
    }
    enc_w = enc_fmt.rotate_cw ? (int)enc_fmt.enc_win_height : (int)enc_fmt.enc_win_width;
    enc_h = enc_fmt.rotate_cw ? (int)enc_fmt.enc_win_width : (int)enc_fmt.enc_win_height;
    if (enc_w <= 0 || enc_h <= 0 || enc_w > 4096 || enc_h > 4096) {
      GST_WARNING_OBJECT(sink, "stream %u invalid enc_win from IAV (%dx%d), falling back to 1920x1080",
          (unsigned)priv->stream_id, enc_w, enc_h);
      enc_w = 1920;
      enc_h = 1080;
    }
    priv->stream_params.encode_width = enc_w;
    priv->stream_params.encode_height = enc_h;
    GST_INFO_OBJECT(sink, "stream %u encode_width=%d encode_height=%d (IAV enc_win)",
        (unsigned)priv->stream_id, enc_w, enc_h);
  }
#if defined (BUILD_DSP_AMBA_V6)
  {
    int pf = overlay_draw_get_stream_overlay_pixel_format(priv->iav_ctx, priv->stream_id);
    if (pf < 0 || pf < IAV_OVERLAY_FORMAT_8BIT_FIRST || pf >= IAV_OVERLAY_FORMAT_32BIT_LAST) {
      GST_ERROR_OBJECT(sink, "Invalid overlay pixel format %d for stream %u (check IAV / encode config)",
          pf, (unsigned)priv->stream_id);
      return FALSE;
    }
    priv->pixel_fmt = pf;
    priv->pixel_size = overlay_draw_iav_bytes_per_pixel((unsigned int)pf);
    GST_INFO_OBJECT(sink, "overlay: stream %u IAV pixel_fmt=%d bytes/pixel=%u",
        (unsigned)priv->stream_id, pf, priv->pixel_size);
  }
#endif
  priv->refresh_frame_index = 0;
  return TRUE;
}

static gboolean overlay_draw_sink_stop(GstBaseSink *sink)
{
  overlay_draw_priv_t *priv = overlay_draw_sink_get_priv(GST_AMBA_OVERLAY_DRAW_SINK(sink));
  if (priv) {
    g_mutex_lock(&priv->aux_cache_lock);
    if (priv->aux_cached_buffers) {
      GHashTable *old = priv->aux_cached_buffers;
      priv->aux_cached_buffers = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
          (GDestroyNotify)gst_buffer_unref);
      g_hash_table_unref(old);
    }
    g_mutex_unlock(&priv->aux_cache_lock);
    gst_amba_overlay_draw_disable_overlay(priv);
  }
  return TRUE;
}

static GstFlowReturn overlay_draw_sink_render(GstBaseSink *sink, GstBuffer *buffer)
{
  GstAmbaOverlayDrawSink *s = GST_AMBA_OVERLAY_DRAW_SINK(sink);
  GstElement *parent = GST_ELEMENT_PARENT(s);
  GstAmbaOverlayDraw *bin = parent ? GST_AMBA_OVERLAY_DRAW(parent) : NULL;
  overlay_draw_priv_t *priv = bin ? bin->priv : NULL;
  GstFlowReturn ret = GST_FLOW_OK;
  unsigned int num, j, buf_id;
  gulong total_size = 0, overlay_data_offset;
  GstMemory *mem = NULL;
  GstMapInfo map;
  iav_set_overlay_t *overlay_set;
  struct iav_overlay_area *area;
  osd_info_t *osd;
  unsigned char update = 0;
  GstBuffer *draw_buffer = buffer;
  GstBuffer *merged = NULL;
  GList *aux_list = NULL;
  GHashTableIter iter;
  gpointer key, value;
  guint32 area_hash[MAX_OVERLAY_AREA_NUM];
  gsize area_size[MAX_OVERLAY_AREA_NUM];

  if (!priv || !priv->iav_ctx || !priv->iav_ctx->map_overlay.base) {
    GST_ERROR_OBJECT(sink, "IAV overlay not ready");
    return GST_FLOW_ERROR;
  }
  g_mutex_lock(&priv->aux_cache_lock);
  if (priv->aux_cached_buffers) {
    g_hash_table_iter_init(&iter, priv->aux_cached_buffers);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
      if (value) {
        aux_list = g_list_prepend(aux_list, gst_buffer_ref((GstBuffer *)value));
      }
    }
  }
  g_mutex_unlock(&priv->aux_cache_lock);
  if (aux_list) {
    merged = merge_overlay_buffers(bin, buffer, aux_list);
    g_list_free_full(aux_list, (GDestroyNotify)gst_buffer_unref);
    if (merged) {
      draw_buffer = merged;
    }
  }
  num = gst_buffer_n_memory(draw_buffer);
  if (num == 0) {
    if (merged) {
      gst_buffer_unref(merged);
    }
    return GST_FLOW_ERROR;
  }
  {
    guint frame = priv->refresh_frame_index;
    priv->refresh_frame_index++;
    if (priv->refresh_interval > 1 && (frame % priv->refresh_interval) != 0) {
      GST_DEBUG_OBJECT(sink, "osd refresh skip (interval=%u frame=%u)",
          priv->refresh_interval, frame);
      if (merged) {
        gst_buffer_unref(merged);
      }
      return GST_FLOW_OK;
    }
  }
  {
    AmbaPrivateDataMeta *pm_trace = amba_buffer_get_private_data_meta(draw_buffer);
    if (!pm_trace) {
      pm_trace = amba_buffer_get_private_data_meta(buffer);
    }
    if (pm_trace) {
      GST_INFO_OBJECT(sink,
          "AmbaPrivateDataMeta trace: overlay_draw buffer yuv_seq_num=%u dsp_pts=%u stream_id=%u GST_PTS=%"
          GST_TIME_FORMAT,
          pm_trace->yuv_seq_num, pm_trace->dsp_pts, (unsigned) priv->stream_id,
          GST_TIME_ARGS(GST_BUFFER_PTS(buffer)));
    }
  }
  {
    unsigned int area_count = (num > MAX_OVERLAY_AREA_NUM) ? MAX_OVERLAY_AREA_NUM : num;

    if (!priv->sync_with_pts) {
      /* Before hashing: producer may mark no change (e.g. amba_draw_data_gen static/update cache).
       * Do not skip when area count changed vs last set_overlay — merged multi-pad buffers often
       * arrive with all flags 0 on aux while hardware never had that slot yet. */
      if (overlay_draw_buffer_all_areas_no_change_flags(draw_buffer, area_count) &&
          area_count == priv->last_draw_area_count) {
        GST_DEBUG_OBJECT(sink, "draw-data: all areas attr/data flags 0, skip per-area hash");
        if (priv->sleep_time_us > 0) {
          g_usleep((gulong)priv->sleep_time_us);
        }
        if (merged) {
          gst_buffer_unref(merged);
        }
        return GST_FLOW_OK;
      }
    }
    overlay_draw_buffer_hash_per_area(draw_buffer, area_hash, area_size, MAX_OVERLAY_AREA_NUM);
    if (!priv->sync_with_pts) {
      gboolean all_unchanged = (area_count == priv->last_draw_area_count);
      if (all_unchanged) {
        unsigned int k;
        for (k = 0; k < area_count && all_unchanged; k++) {
          GstMemory *memk = gst_buffer_peek_memory(draw_buffer, k);
          GstMapInfo mapk;
          unsigned int slot;
          if (!memk || !gst_memory_map(memk, &mapk, GST_MAP_READ)) {
            all_unchanged = FALSE;
            break;
          }
          if (mapk.size < OSD_AREA_BLOCK_HEADER_SIZE) {
            gst_memory_unmap(memk, &mapk);
            all_unchanged = FALSE;
            break;
          }
          slot = osd_area_block_resolve_slot((osd_area_block_header_t *)mapk.data, k);
          gst_memory_unmap(memk, &mapk);
          if (slot >= MAX_OVERLAY_AREA_NUM) {
            slot = k;
          }
          all_unchanged = (priv->last_draw_hash[slot] == area_hash[k] &&
              priv->last_draw_size[slot] == area_size[k]);
        }
      }
      if (all_unchanged) {
        if (priv->sleep_time_us > 0) {
          g_usleep((gulong)priv->sleep_time_us);
        }
        if (merged) {
          gst_buffer_unref(merged);
        }
        return GST_FLOW_OK;
      }
    }
  }
  if (num > MAX_OVERLAY_AREA_NUM) {
    num = MAX_OVERLAY_AREA_NUM;
  }
  overlay_set = &priv->overlay_set;
  overlay_set->overlay_insert.id = priv->stream_id;
  overlay_set->overlay_insert.enable = 0;
  overlay_set->overlay_insert.osd_insert_always = priv->insert_always;
  overlay_set->sync_with_pts = priv->sync_with_pts;
  {
    unsigned int area_count = num;
    unsigned char draw_fmt = AMBA_DRAW_FORMAT_8BIT_CLUT;
    int draw_fmt_set = 0;
    unsigned int max_slot_plus_one = area_count;
    for (j = 0; j < area_count; j++) {
      mem = gst_buffer_peek_memory(draw_buffer, j);
      if (!mem) {
        continue;
      }
      if (!gst_memory_map(mem, &map, GST_MAP_READ)) {
        continue;
      }
      {
        osd_area_block_header_t *block_hdr = (osd_area_block_header_t *)map.data;
        unsigned int slot;
        if (map.size < OSD_AREA_BLOCK_HEADER_SIZE) {
          gst_memory_unmap(mem, &map);
          continue;
        }
        if (block_hdr->magic != OSD_AREA_BLOCK_MAGIC) {
          gst_memory_unmap(mem, &map);
          continue;
        }
        if (map.size < block_hdr->block_size || block_hdr->block_size < OSD_AREA_BLOCK_PIXEL_OFFSET ||
            block_hdr->block_size > OSD_AREA_BLOCK_MAX_SIZE) {
          gst_memory_unmap(mem, &map);
          continue;
        }
        if (!draw_fmt_set) {
          draw_fmt = block_hdr->draw_format;
          draw_fmt_set = 1;
        } else if (block_hdr->draw_format != draw_fmt) {
          GST_ERROR_OBJECT(sink, "draw-data: all areas must share draw_format (area %u has %u, expected %u)",
              j, (unsigned)block_hdr->draw_format, (unsigned)draw_fmt);
          gst_memory_unmap(mem, &map);
          if (merged) {
            gst_buffer_unref(merged);
          }
          return GST_FLOW_ERROR;
        }
        slot = osd_area_block_resolve_slot(block_hdr, j);
        if (slot + 1 > max_slot_plus_one) {
          max_slot_plus_one = slot + 1;
        }
        amba_overlay_area_attr_t attr;
        memset(&attr, 0, sizeof(attr));
        attr.enable = block_hdr->enable;
        attr.rect = block_hdr->rect;
        attr.buf_num = priv->buf_num;
        if (block_hdr->enable) {
          overlay_set->overlay_insert.enable = 1;
        }
        {
          unsigned int eff_a, eff_d;
          overlay_effective_change_flags(draw_buffer, j, block_hdr, &eff_a, &eff_d);
          if (eff_a || eff_d) {
            update = 1;
          }
        }
      }
      gst_memory_unmap(mem, &map);
    }
    overlay_set->overlay_max_num = (unsigned char)max_slot_plus_one;
    if (area_count != priv->last_draw_area_count) {
      update = 1;
    }
    /* sync_with_pts: still run overlay/PTS path even when both flags 0 (drawdatagen cache). */
    if (priv->sync_with_pts && overlay_set->overlay_insert.enable) {
      update = 1;
    }
    if (overlay_set->overlay_insert.enable && update) {
#if defined (BUILD_DSP_AMBA_V6)
      if (!overlay_draw_fmt_matches_iav((int)draw_fmt, priv->pixel_fmt)) {
        GST_ERROR_OBJECT(sink,
            "draw_format %u does not match stream %u overlay pixel format %d (see amba_draw_data_gen draw-format vs encode/overlay setup)",
            (unsigned)draw_fmt, (unsigned)priv->stream_id, priv->pixel_fmt);
        if (merged) {
          gst_buffer_unref(merged);
        }
        return GST_FLOW_ERROR;
      }
#else
      if (draw_fmt != AMBA_DRAW_FORMAT_8BIT_CLUT) {
        GST_ERROR_OBJECT(sink, "draw_format %u: this build only supports 8bit CLUT on amba_overlay_draw",
            (unsigned)draw_fmt);
        if (merged) {
          gst_buffer_unref(merged);
        }
        return GST_FLOW_ERROR;
      }
#endif
      if (priv->osd_size <= OVERLAY_YUV_OFFSET) {
        GST_ERROR_OBJECT(sink, "osd_size %lu <= OVERLAY_YUV_OFFSET %u", priv->osd_size, OVERLAY_YUV_OFFSET);
        if (merged) {
          gst_buffer_unref(merged);
        }
        return GST_FLOW_ERROR;
      }
      overlay_data_offset = priv->osd_offset + OVERLAY_YUV_OFFSET;
      total_size = 0;
      for (j = 0; j < area_count; j++) {
        /* Skip CLUT/pixel memcpy when producer says data unchanged; hash fallback for old clients. */
        gboolean skip_pixel_copy;

        mem = gst_buffer_get_memory(draw_buffer, j);
        if (!mem) {
          ret = GST_FLOW_ERROR;
          break;
        }
        if (!gst_memory_map(mem, &map, GST_MAP_READ)) {
          gst_memory_unref(mem);
          ret = GST_FLOW_ERROR;
          break;
        }
        {
          osd_area_block_header_t *block_hdr = (osd_area_block_header_t *)map.data;
          unsigned int slot;
          if (map.size < OSD_AREA_BLOCK_HEADER_SIZE) {
            gst_memory_unmap(mem, &map);
            gst_memory_unref(mem);
            ret = GST_FLOW_ERROR;
            break;
          }
          if (block_hdr->magic != OSD_AREA_BLOCK_MAGIC) {
            gst_memory_unmap(mem, &map);
            gst_memory_unref(mem);
            ret = GST_FLOW_ERROR;
            break;
          }
          if (map.size < block_hdr->block_size || block_hdr->block_size < OSD_AREA_BLOCK_PIXEL_OFFSET ||
              block_hdr->block_size > OSD_AREA_BLOCK_MAX_SIZE) {
            gst_memory_unmap(mem, &map);
            gst_memory_unref(mem);
            ret = GST_FLOW_ERROR;
            break;
          }
          slot = osd_area_block_resolve_slot(block_hdr, j);
          if (slot >= MAX_OVERLAY_AREA_NUM) {
            gst_memory_unmap(mem, &map);
            gst_memory_unref(mem);
            ret = GST_FLOW_ERROR;
            break;
          }
          /* Must upload new area slots (j >= last_draw_area_count): meta may say data unchanged
           * on a later frame while overlay never received this index. */
          {
            unsigned int eff_a, eff_d;
            overlay_effective_change_flags(draw_buffer, j, block_hdr, &eff_a, &eff_d);
            (void)eff_a;
            skip_pixel_copy = (j < priv->last_draw_area_count) &&
                (!eff_d ||
                    (priv->last_draw_hash[slot] == area_hash[j] &&
                        priv->last_draw_size[slot] == area_size[j]));
          }
          amba_overlay_area_attr_t attr;
          memset(&attr, 0, sizeof(attr));
          attr.enable = block_hdr->enable;
          attr.rect = block_hdr->rect;
          attr.buf_num = priv->buf_num;
          osd = &overlay_set->osd[slot];
          area = &overlay_set->overlay_insert.area[slot];
          osd->enable = attr.enable;
          osd->width = attr.rect.width;
          osd->height = attr.rect.height;
          osd->x = attr.rect.x;
          osd->y = attr.rect.y;
          area->width = osd->width;
          area->height = osd->height;
          area->start_x = osd->x;
          area->start_y = osd->y;
          area->pitch = ROUND_UP(ROUND_UP(area->width, OSD_BUF_WIDTH_ALIGN) * priv->pixel_size, OSD_BUF_PITCH_ALIGN);
          area->total_size = area->pitch * area->height;
          area->clut_addr_offset = priv->osd_offset + slot * OVERLAY_CLUT_SIZE;
          area->enable = osd->enable;
          if (!skip_pixel_copy) {
            osd->buf_id = 0;
          }
          osd->buf_num = attr.buf_num;
          for (buf_id = 0; buf_id < osd->buf_num; buf_id++) {
            osd->buf_data[buf_id] = overlay_data_offset + total_size + area->total_size * buf_id;
          }
          total_size += area->total_size * osd->buf_num;
          if (total_size > (unsigned int)(priv->osd_size - OVERLAY_YUV_OFFSET)) {
            GST_ERROR_OBJECT(sink, "areas need total size %lu (id: %u, height: %u, pitch: %u, buf num: %u) > osd_size %lu - clut_size %u",
                total_size, j, area->height, area->pitch, osd->buf_num,
                priv->osd_size, OVERLAY_YUV_OFFSET);
            gst_memory_unmap(mem, &map);
            gst_memory_unref(mem);
            ret = GST_FLOW_ERROR;
            break;
          }
          if (skip_pixel_copy) {
            /* effective data_change==0 or hash match: reuse mapped overlay, no CLUT/pixel upload */
            area->data_addr_offset = osd->buf_data[osd->buf_id];
          } else {
            memcpy(priv->iav_ctx->map_overlay.base + area->clut_addr_offset,
                map.data + OSD_AREA_BLOCK_CLUT_OFFSET, OVERLAY_CLUT_SIZE);
            if (fill_overlay_data(priv, slot, &attr, map.data + OSD_AREA_BLOCK_PIXEL_OFFSET) < 0) {
              gst_memory_unmap(mem, &map);
              gst_memory_unref(mem);
              ret = GST_FLOW_ERROR;
              break;
            }
          }
        }
        gst_memory_unmap(mem, &map);
        gst_memory_unref(mem);
      }
      if (ret == GST_FLOW_OK) {
        /* Do not scale rect here: drawdatagen pixels and area->pitch/total_size were computed for
         * block_hdr->rect. Scaling width/height after fill_overlay_data without recomputing pitch or
         * resampling pixels breaks stride vs width and makes OSD size/colors wrong (e.g. white block
         * not matching ROI). If coord_res != encoder resolution, set coord_res on drawdatagen (caps)
         * and/or overlay_draw to match encode_width/encode_height so ROI is in encoder pixel units. */
        if (priv->coord_res_w > 0 && priv->coord_res_h > 0) {
          int enc_w = priv->stream_params.encode_width;
          int enc_h = priv->stream_params.encode_height;
          if (enc_w > 0 && enc_h > 0 &&
              (enc_w != priv->coord_res_w || enc_h != priv->coord_res_h)) {
            GST_WARNING_OBJECT(sink,
                "coord_res %dx%d != encoder %dx%d (stream %u): overlay uses draw-data rect and pitch "
                "as-is; set coord_res on amba_draw_data_gen (or amba_overlay_draw) to match the stream "
                "encode resolution so ROI placement and size match the encoded picture.",
                priv->coord_res_w, priv->coord_res_h, enc_w, enc_h,
                (unsigned)priv->stream_id);
          }
        }
        {
          unsigned int dsp_pts = 0;
          AmbaPrivateDataMeta *priv_meta = amba_buffer_get_private_data_meta(buffer);
          if (overlay_set->sync_with_pts) {
            if (!priv_meta || priv_meta->dsp_pts <= 0) {
              GST_ERROR_OBJECT(sink, "Invalid private meta (%p) or dsp_pts (%u)", priv_meta,
                  priv_meta ? priv_meta->dsp_pts : 0U);
              ret = GST_FLOW_ERROR;
            }
            dsp_pts = priv_meta->dsp_pts;
            priv->last_dsp_pts = dsp_pts;
            if (priv->iav_ctx->iav_al.f_set_frame_sync(priv->iav_ctx->iav_fd, overlay_set) < 0) {
              GST_ERROR_OBJECT(sink, "f_set_frame_sync failed (stream_id=%u, non-fatal)",
                  (unsigned)priv->stream_id);
              ret = GST_FLOW_ERROR;
            } else if (priv->iav_ctx->iav_al.f_apply_frame_sync(priv->iav_ctx->iav_fd, dsp_pts,
                    (1U << priv->stream_id), 0) < 0) {
              GST_ERROR_OBJECT(sink, "f_apply_frame_sync failed (stream_id=%u dsp_pts=%u, non-fatal)",
                  (unsigned)priv->stream_id, dsp_pts);
              ret = GST_FLOW_ERROR;
            }
          } else {
            if (priv->iav_ctx->iav_al.f_set_overlay(priv->iav_ctx->iav_fd, overlay_set) < 0) {
              GST_ERROR_OBJECT(sink, "f_set_overlay failed (stream_id=%u, non-fatal)",
                  (unsigned)priv->stream_id);
              ret = GST_FLOW_ERROR;
            }
          }
        }
      }
      if (ret == GST_FLOW_OK) {
        unsigned int k, ac = (num > MAX_OVERLAY_AREA_NUM) ? MAX_OVERLAY_AREA_NUM : num;
        for (k = 0; k < ac; k++) {
          GstMemory *memk = gst_buffer_peek_memory(draw_buffer, k);
          GstMapInfo mapk;
          unsigned int slot;
          if (!memk || !gst_memory_map(memk, &mapk, GST_MAP_READ)) {
            continue;
          }
          if (mapk.size < OSD_AREA_BLOCK_HEADER_SIZE) {
            gst_memory_unmap(memk, &mapk);
            continue;
          }
          slot = osd_area_block_resolve_slot((osd_area_block_header_t *)mapk.data, k);
          gst_memory_unmap(memk, &mapk);
          if (slot >= MAX_OVERLAY_AREA_NUM) {
            slot = k;
          }
          priv->last_draw_hash[slot] = area_hash[k];
          priv->last_draw_size[slot] = area_size[k];
        }
        priv->last_draw_area_count = ac;
      }
    }
  }
  if (!priv->sync_with_pts && priv->sleep_time_us > 0) {
    g_usleep((gulong)priv->sleep_time_us);
  }

  if (merged) {
    gst_buffer_unref(merged);
    merged = NULL;
  }
  return ret;
}

/* --- GstAmbaOverlayDrawAux (internal) --- */
static GstFlowReturn overlay_draw_aux_chain(GstPad *pad, GstObject *parent, GstBuffer *buffer);

static GstStaticPadTemplate overlay_aux_sink_factory =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);

static gboolean overlay_draw_aux_query(GstPad *pad, GstObject *parent, GstQuery *query)
{
  if (GST_QUERY_TYPE(query) == GST_QUERY_CAPS) {
    GstCaps *result = gst_caps_from_string(GST_AMBA_DRAW_DATA_CAPS);
    gst_query_set_caps_result(query, result);
    gst_caps_unref(result);
    return TRUE;
  }
  return gst_pad_query_default(pad, parent, query);
}

enum { AUX_PROP_0, AUX_PROP_PAD_NAME };
static void gst_amba_overlay_draw_aux_set_property(GObject *o, guint id, const GValue *v, GParamSpec *pspec);
static void gst_amba_overlay_draw_aux_get_property(GObject *o, guint id, GValue *v, GParamSpec *pspec);
static void gst_amba_overlay_draw_aux_finalize(GObject *o);

static void gst_amba_overlay_draw_aux_class_init(GstAmbaOverlayDrawAuxClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS(klass);
  gobject_class->set_property = gst_amba_overlay_draw_aux_set_property;
  gobject_class->get_property = gst_amba_overlay_draw_aux_get_property;
  gobject_class->finalize = gst_amba_overlay_draw_aux_finalize;
  g_object_class_install_property(gobject_class, AUX_PROP_PAD_NAME,
      g_param_spec_string("pad-name", "Pad name", "Ghost pad name for cache key",
          NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
  gst_element_class_add_pad_template(gstelement_class,
      gst_static_pad_template_get(&overlay_aux_sink_factory));
  gst_element_class_set_static_metadata(gstelement_class,
      "Amba Overlay Draw Aux (internal)", "Sink", "Internal overlay aux sink", "Ambarella");
}

static void gst_amba_overlay_draw_aux_set_property(GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  GstAmbaOverlayDrawAux *self = GST_AMBA_OVERLAY_DRAW_AUX(o);
  GstAmbaOverlayDrawAuxPrivate *apriv = gst_amba_overlay_draw_aux_get_instance_private(self);
  if (id == AUX_PROP_PAD_NAME) {
    g_free(apriv->pad_name);
    apriv->pad_name = g_value_dup_string(v);
  } else {
    G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, pspec);
  }
}

static void gst_amba_overlay_draw_aux_get_property(GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  GstAmbaOverlayDrawAux *self = GST_AMBA_OVERLAY_DRAW_AUX(o);
  GstAmbaOverlayDrawAuxPrivate *apriv = gst_amba_overlay_draw_aux_get_instance_private(self);
  if (id == AUX_PROP_PAD_NAME) {
    g_value_set_string(v, apriv->pad_name);
  } else {
    G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, pspec);
  }
}

static void gst_amba_overlay_draw_aux_finalize(GObject *o)
{
  GstAmbaOverlayDrawAux *self = GST_AMBA_OVERLAY_DRAW_AUX(o);
  GstAmbaOverlayDrawAuxPrivate *apriv = gst_amba_overlay_draw_aux_get_instance_private(self);
  g_free(apriv->pad_name);
  apriv->pad_name = NULL;
  G_OBJECT_CLASS(gst_amba_overlay_draw_aux_parent_class)->finalize(o);
}

static void gst_amba_overlay_draw_aux_init(GstAmbaOverlayDrawAux *self)
{
  GstPad *pad = gst_pad_new_from_static_template(&overlay_aux_sink_factory, "sink");
  gst_pad_set_query_function(pad, GST_DEBUG_FUNCPTR(overlay_draw_aux_query));
  gst_pad_set_chain_function(pad, GST_DEBUG_FUNCPTR(overlay_draw_aux_chain));
  gst_element_add_pad(GST_ELEMENT(self), pad);
}

static GstFlowReturn overlay_draw_aux_chain(GstPad *pad, GstObject *parent, GstBuffer *buffer)
{
  GstAmbaOverlayDrawAux *aux = GST_AMBA_OVERLAY_DRAW_AUX(parent);
  GstAmbaOverlayDrawAuxPrivate *apriv = gst_amba_overlay_draw_aux_get_instance_private(aux);
  GstElement *bin_el = GST_ELEMENT_PARENT(aux);
  GstAmbaOverlayDraw *bin = bin_el ? GST_AMBA_OVERLAY_DRAW(bin_el) : NULL;
  overlay_draw_priv_t *priv = bin ? bin->priv : NULL;

  if (GST_PAD_IS_FLUSHING(pad)) {
    gst_buffer_unref(buffer);
    return GST_FLOW_FLUSHING;
  }
  if (!priv || !priv->aux_cached_buffers || !apriv->pad_name) {
    gst_buffer_unref(buffer);
    return GST_FLOW_OK;
  }
  g_mutex_lock(&priv->aux_cache_lock);
  if (priv->aux_cached_buffers) {
    g_hash_table_replace(priv->aux_cached_buffers, g_strdup(apriv->pad_name), gst_buffer_ref(buffer));
  }
  g_mutex_unlock(&priv->aux_cache_lock);
  gst_buffer_unref(buffer);
  return GST_FLOW_OK;
}

static void gst_amba_overlay_draw_class_init(GstAmbaOverlayDrawClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS(klass);

  gobject_class->finalize = gst_amba_overlay_draw_finalize;
  gobject_class->set_property = gst_amba_overlay_draw_set_property;
  gobject_class->get_property = gst_amba_overlay_draw_get_property;

  gstelement_class->request_new_pad = GST_DEBUG_FUNCPTR(gst_amba_overlay_draw_request_new_pad);
  gstelement_class->release_pad = GST_DEBUG_FUNCPTR(gst_amba_overlay_draw_release_pad);

  g_object_class_install_property(gobject_class, PROP_STREAM_ID,
      g_param_spec_uint("stream_id", "StreamId", "Encode stream id for overlay",
          0, IAV_STREAM_MAX_NUM_ALL, 0, G_PARAM_READWRITE));
  g_object_class_install_property(gobject_class, PROP_OSD_OFFSET,
      g_param_spec_ulong("osd_offset", "OverlayOffset", "Overlay buffer offset address",
          0, G_MAXULONG, 0, G_PARAM_READWRITE));
  g_object_class_install_property(gobject_class, PROP_OSD_SIZE,
      g_param_spec_ulong("osd_size", "OverlaySize", "Overlay buffer size for stream",
          0, G_MAXULONG, 0, G_PARAM_READWRITE));
  g_object_class_install_property(gobject_class, PROP_COORD_RES,
      g_param_spec_string("coord_res", "CoordRes",
          "Coordinate resolution for ROI/bbox; should match encode width x height. "
          "When upstream amba_draw_data_gen sends application/x-amba-draw-data with coord_res, that "
          "value is applied automatically; set this property only to override or when caps lack coord_res.",
          "1920x1080", G_PARAM_READWRITE));
  g_object_class_install_property(gobject_class, PROP_SYNC_WITH_PTS,
      g_param_spec_uchar("sync_pts", "SyncPTS", "OSD sync with PTS (1=recommended for bbox)",
          0, 1, 1, G_PARAM_READWRITE));
  g_object_class_install_property(gobject_class, PROP_OSD_INSERT_ALWAYS,
      g_param_spec_uchar("insert_always", "InsertAlways", "Always insert OSD",
          0, 1, 0, G_PARAM_READWRITE));
  g_object_class_install_property(gobject_class, PROP_BUF_NUM,
      g_param_spec_uint("buf_num", "BufNum", "Buffer count per area for double/triple buffering (1..4)",
          1, OSD_MAX_BUFFER_NUM, 2, G_PARAM_READWRITE));
  g_object_class_install_property(gobject_class, PROP_SLEEP_TIME,
      g_param_spec_uint("sleep_time", "SleepTime",
          "Sleep time (us) after each buffer when sync_pts=FALSE, 0=no sleep (reduces multifilesrc read rate)",
          0, G_MAXUINT, 0, G_PARAM_READWRITE));
  g_object_class_install_property(gobject_class, PROP_REFRESH_INTERVAL,
      g_param_spec_uint("refresh_interval", "RefreshInterval",
          "Run overlay every Nth buffer (default 1=every frame). "
          "Reduces CPU and ioctl load.",
          1, 120, 1, G_PARAM_READWRITE));

  gst_amba_draw_data_area_flags_meta_get_info();

  gst_element_class_add_pad_template(gstelement_class,
      gst_static_pad_template_get(&sink_factory));
  gst_element_class_add_pad_template(gstelement_class,
      gst_static_pad_template_get(&sink_request_factory));
  gst_element_class_set_static_metadata(gstelement_class,
      "Amba Overlay Draw",
      "Sink",
      "Draw overlay data (BMP, string, bbox) onto Ambarella overlay; 16/32bit draw-format on V6 when matched to IAV",
      "pxduan <pxduan@ambarella.com>");
}

static void gst_amba_overlay_draw_init(GstAmbaOverlayDraw *self)
{
  GstElement *main_sink;
  GstPad *sink_pad, *ghost_pad;
  overlay_draw_priv_t *priv = g_malloc0(sizeof(overlay_draw_priv_t));
  self->priv = priv;
  priv->stream_id = 0;
  priv->pixel_size = 1;
  priv->aux_cached_buffers = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
      (GDestroyNotify)gst_buffer_unref);
  g_mutex_init(&priv->aux_cache_lock);
  priv->iav_ctx = acquire_iav_ctx(1);
  if (!priv->iav_ctx) {
    DPRINT_ERROR("acquire_iav_ctx failed\n");
  }
  priv->coord_res_w = 1920;
  priv->coord_res_h = 1080;
  priv->buf_num = 2;
  priv->refresh_interval = 1;
  priv->refresh_frame_index = 0;

  main_sink = GST_ELEMENT(g_object_new(GST_TYPE_AMBA_OVERLAY_DRAW_SINK, "name", "overlay_sink", NULL));
  if (!main_sink) {
    GST_ERROR_OBJECT(self, "Failed to create internal overlay sink");
    return;
  }
  gst_bin_add(GST_BIN(self), main_sink);
  sink_pad = gst_element_get_static_pad(main_sink, "sink");
  ghost_pad = gst_ghost_pad_new("sink", sink_pad);
  gst_object_unref(sink_pad);
  gst_element_add_pad(GST_ELEMENT(self), ghost_pad);
}

static void gst_amba_overlay_draw_disable_overlay(overlay_draw_priv_t *priv)
{
  iav_set_overlay_t *os;

  if (!priv || !priv->iav_ctx || !priv->iav_ctx->iav_fd_opened) {
    return;
  }

  os = &priv->overlay_set;

  os->overlay_insert.enable = 0;
  if (priv->sync_with_pts) {
    if (priv->last_dsp_pts) {
      if (priv->iav_ctx->iav_al.f_set_frame_sync(priv->iav_ctx->iav_fd, os) < 0) {
        DPRINT_ERROR("f_set_frame_sync disable error for stream %d\n", priv->stream_id);
      } else if (priv->iav_ctx->iav_al.f_apply_frame_sync(priv->iav_ctx->iav_fd, priv->last_dsp_pts,
          (1U << priv->stream_id), 1) < 0) {
        DPRINT_ERROR("f_apply_frame_sync disable error for stream %d\n", priv->stream_id);
      }
    } else {
      if (priv->iav_ctx->iav_al.f_set_overlay(priv->iav_ctx->iav_fd, os) < 0) {
        DPRINT_ERROR("f_set_overlay disable error for stream %d (no cached dsp_pts)\n", priv->stream_id);
      }
    }
  } else {
    if (priv->iav_ctx->iav_al.f_set_overlay(priv->iav_ctx->iav_fd, os) < 0) {
      DPRINT_ERROR("f_set_overlay disable error for stream %d\n", priv->stream_id);
    }
  }
}

static void gst_amba_overlay_draw_finalize(GObject *object)
{
  GstAmbaOverlayDraw *self = GST_AMBA_OVERLAY_DRAW(object);
  overlay_draw_priv_t *priv = self->priv;
  if (priv) {
    if (priv->aux_cached_buffers) {
      g_hash_table_unref(priv->aux_cached_buffers);
      priv->aux_cached_buffers = NULL;
    }
    /* Ensure no thread holds the lock before clear (g_mutex_clear on locked mutex is UB) */
    g_mutex_lock(&priv->aux_cache_lock);
    g_mutex_unlock(&priv->aux_cache_lock);
    g_mutex_clear(&priv->aux_cache_lock);
    if (priv->iav_ctx) {
      gst_amba_overlay_draw_disable_overlay(priv);
      release_iav_ctx(1);
      priv->iav_ctx = NULL;
    }
    g_free(priv);
    self->priv = NULL;
  }
  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void gst_amba_overlay_draw_set_property(GObject *object, guint prop_id,
    const GValue *value, GParamSpec *pspec)
{
  GstAmbaOverlayDraw *self = GST_AMBA_OVERLAY_DRAW(object);
  overlay_draw_priv_t *priv = self->priv;

  switch (prop_id) {
    case PROP_STREAM_ID: {
      int sid = (int)g_value_get_uint(value);
      if (sid >= 0 && sid < IAV_STREAM_MAX_NUM_ALL) {
        priv->stream_id = sid;
      }
      break;
    }
    case PROP_OSD_OFFSET:
      priv->osd_offset = g_value_get_ulong(value);
      break;
    case PROP_OSD_SIZE:
      priv->osd_size = g_value_get_ulong(value);
      break;
    case PROP_COORD_RES: {
      const char *s = g_value_get_string(value);
      int w = 0, h = 0;
      if (s && s[0] && sscanf(s, "%dx%d", &w, &h) >= 2 && w > 0 && h > 0) {
        priv->coord_res_w = w;
        priv->coord_res_h = h;
      }
      break;
    }
    case PROP_SYNC_WITH_PTS:
      priv->sync_with_pts = (unsigned char)g_value_get_uchar(value);
      break;
    case PROP_OSD_INSERT_ALWAYS:
      priv->insert_always = (unsigned char)g_value_get_uchar(value);
      break;
    case PROP_BUF_NUM: {
      unsigned int n = g_value_get_uint(value);
      if (n >= 1 && n <= OSD_MAX_BUFFER_NUM) {
        priv->buf_num = n;
      }
      break;
    }
    case PROP_SLEEP_TIME:
      priv->sleep_time_us = g_value_get_uint(value);
      break;
    case PROP_REFRESH_INTERVAL: {
      guint n = g_value_get_uint(value);
      if (n >= 1 && n <= 120) {
        priv->refresh_interval = n;
        priv->refresh_frame_index = 0;
      }
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
      break;
  }
}

static void gst_amba_overlay_draw_get_property(GObject *object, guint prop_id,
    GValue *value, GParamSpec *pspec)
{
  GstAmbaOverlayDraw *self = GST_AMBA_OVERLAY_DRAW(object);
  overlay_draw_priv_t *priv = self->priv;

  switch (prop_id) {
    case PROP_STREAM_ID:
      g_value_set_uint(value, (guint)priv->stream_id);
      break;
    case PROP_OSD_OFFSET:
      g_value_set_ulong(value, priv->osd_offset);
      break;
    case PROP_OSD_SIZE:
      g_value_set_ulong(value, priv->osd_size);
      break;
    case PROP_COORD_RES: {
      gchar buf[32];
      g_snprintf(buf, sizeof(buf), "%dx%d", priv->coord_res_w, priv->coord_res_h);
      g_value_set_string(value, buf);
      break;
    }
    case PROP_SYNC_WITH_PTS:
      g_value_set_uchar(value, priv->sync_with_pts);
      break;
    case PROP_OSD_INSERT_ALWAYS:
      g_value_set_uchar(value, priv->insert_always);
      break;
    case PROP_BUF_NUM:
      g_value_set_uint(value, priv->buf_num);
      break;
    case PROP_SLEEP_TIME:
      g_value_set_uint(value, priv->sleep_time_us);
      break;
    case PROP_REFRESH_INTERVAL:
      g_value_set_uint(value, priv->refresh_interval);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
      break;
  }
}

static GstPad *gst_amba_overlay_draw_request_new_pad(GstElement *element,
    GstPadTemplate *templ, const gchar *req_name, const GstCaps *caps)
{
  GstAmbaOverlayDraw *self = GST_AMBA_OVERLAY_DRAW(element);
  GstElement *aux;
  GstPad *sink_pad, *ghost_pad;
  gchar *pad_name;

  (void)caps;
  (void)templ;
  pad_name = g_strdup(req_name);
  aux = GST_ELEMENT(g_object_new(GST_TYPE_AMBA_OVERLAY_DRAW_AUX,
      "name", req_name, "pad-name", pad_name, NULL));
  g_free(pad_name);
  if (!aux) {
    return NULL;
  }

  gst_bin_add(GST_BIN(self), aux);
  sink_pad = gst_element_get_static_pad(aux, "sink");
  ghost_pad = gst_ghost_pad_new(req_name, sink_pad);
  gst_object_unref(sink_pad);
  gst_element_add_pad(element, ghost_pad);
  return ghost_pad;
}

static void gst_amba_overlay_draw_release_pad(GstElement *element, GstPad *pad)
{
  GstAmbaOverlayDraw *self = GST_AMBA_OVERLAY_DRAW(element);
  overlay_draw_priv_t *priv = self->priv;

  if (priv && priv->aux_cached_buffers) {
    gchar *key = g_strdup(GST_PAD_NAME(pad));
    g_mutex_lock(&priv->aux_cache_lock);
    g_hash_table_remove(priv->aux_cached_buffers, key);
    g_mutex_unlock(&priv->aux_cache_lock);
    g_free(key);
  }
  /* Remove the internal aux element when releasing a request pad (ghost pad) */
  if (GST_IS_GHOST_PAD(pad)) {
    GstPad *target = gst_ghost_pad_get_target(GST_GHOST_PAD(pad));
    if (target) {
      GstElement *aux = gst_pad_get_parent_element(target);
      gst_object_unref(target);
      if (aux) {
        gst_bin_remove(GST_BIN(self), aux);
        gst_object_unref(aux);
      }
    }
  }
  GST_ELEMENT_CLASS(parent_class)->release_pad(element, pad);
}
