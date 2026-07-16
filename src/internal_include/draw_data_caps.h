/*
 * draw_data_caps.h
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
 *
 * Caps for amba_draw_data_gen output. Used by overlay (amba_overlay_draw),
 * blur, and other consumers.
 *
 * application/x-amba-draw-data with optional coord_res=(string)WxH (amba_draw_data_gen sets this
 *   from its coord_res property / map dimensions so overlay can align with the encoded picture).
 *
 * Buffer: per-area. Each GstMemory = [osd_area_block_header_t][CLUT 1024][pixels].
 * Background color is only in CLUT[AMBA_DRAW_CLUT_ENTRY_BACKGROUND], not in header.
 * Per-frame attr/data change semantics: GstAmbaDrawDataAreaFlagsMeta on the buffer (per-memory index).
 *   osd_area_block_header_t keeps hdr_reserved[2] where flags used to be (layout unchanged).
 * Header: magic (0x414D), block_size (total bytes). Layout: [0,CLUT_OFFSET)=header,
 * [CLUT_OFFSET,PIXEL_OFFSET)=CLUT, [PIXEL_OFFSET,block_size)=pixels. Parser validates
 * map.size >= block_size and block_size within [PIXEL_OFFSET, MAX_SIZE].
 * area_count = gst_buffer_n_memory(buffer).
 */

#ifndef __DRAW_DATA_CAPS_H__
#define __DRAW_DATA_CAPS_H__

#include <gst/gst.h>
#include "iav_al.h"

#define GST_AMBA_DRAW_DATA_CAPS "application/x-amba-draw-data"

/* Sink-only: buffer payload ignored; output is driven solely by osd (and coord_res).
 * Use to clock draw-data generation off any upstream (e.g. appsrc, videotestsrc) without bmp/text caps. */
#define GST_AMBA_DRAW_DATA_GEN_TRIGGER_CAPS "application/x-amba-drawdatagen-trigger"

/* Optional GstBuffer meta: per-GstMemory-index change hints (layout vs pixel payload).
 * Single byte per area: bitmask (AMBA_DRAW_AREA_FLAG_*). Implementation: common/amba_draw_data_area_flags_meta.c */
#define AMBA_DRAW_AREA_FLAG_ATTR_CHANGED  0x01U /* rect/enable/draw_format vs last frame */
#define AMBA_DRAW_AREA_FLAG_DATA_CHANGED  0x02U /* CLUT+pixel bytes vs last hash */

typedef struct _GstAmbaDrawDataAreaFlagsMeta GstAmbaDrawDataAreaFlagsMeta;

struct _GstAmbaDrawDataAreaFlagsMeta {
  GstMeta meta;
  guint32 valid_mask; /* bit s: flags[s] valid; s = osd_area_block_resolve_slot(header, mem_idx) */
  guint8 flags[MAX_OVERLAY_AREA_NUM];
};

GType gst_amba_draw_data_area_flags_meta_api_get_type(void);
const GstMetaInfo *gst_amba_draw_data_area_flags_meta_get_info(void);

#define GST_AMBA_DRAW_DATA_AREA_FLAGS_META_API_TYPE (gst_amba_draw_data_area_flags_meta_api_get_type())
#define GST_AMBA_DRAW_DATA_AREA_FLAGS_META_INFO (gst_amba_draw_data_area_flags_meta_get_info())

GstAmbaDrawDataAreaFlagsMeta *gst_amba_draw_data_area_flags_meta_get(GstBuffer *buf);
gboolean gst_amba_draw_data_area_flags_meta_set(GstBuffer *buf, guint area_slot, guint8 flags_mask);
void gst_amba_draw_data_area_flags_meta_merge_from_buffer(GstBuffer *dst, GstBuffer *src);

#endif /* __DRAW_DATA_CAPS_H__ */
