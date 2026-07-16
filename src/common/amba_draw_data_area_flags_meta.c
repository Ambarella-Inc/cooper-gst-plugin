 /*
 * amba_draw_data_area_flags_meta.c
 *
 * History:
 *    3/27/2026 - [pxduan] created file
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
 * Transmit areas' attribute/data change flags with metadata.
 */


#include "draw_data_caps.h"

#ifndef DUNUSED
#define DUNUSED(x) (void)(x)
#endif

static gboolean
gst_amba_draw_data_area_flags_meta_init(GstMeta *meta, gpointer params, GstBuffer *buffer)
{
  GstAmbaDrawDataAreaFlagsMeta *m = (GstAmbaDrawDataAreaFlagsMeta *)meta;
  DUNUSED(params);
  DUNUSED(buffer);
  m->valid_mask = 0;
  return TRUE;
}

static gboolean
gst_amba_draw_data_area_flags_meta_transform(GstBuffer *transbuf, GstMeta *meta,
    GstBuffer *buffer, GQuark type, gpointer data)
{
  GstAmbaDrawDataAreaFlagsMeta *src = (GstAmbaDrawDataAreaFlagsMeta *)meta;
  GstAmbaDrawDataAreaFlagsMeta *dst;
  DUNUSED(buffer);
  DUNUSED(type);
  DUNUSED(data);
  dst = (GstAmbaDrawDataAreaFlagsMeta *)gst_buffer_add_meta(transbuf,
      GST_AMBA_DRAW_DATA_AREA_FLAGS_META_INFO, NULL);
  if (!dst)
    return FALSE;
  dst->valid_mask = src->valid_mask;
  memcpy(dst->flags, src->flags, sizeof(dst->flags));
  return TRUE;
}

GType
gst_amba_draw_data_area_flags_meta_api_get_type(void)
{
  static GType type = 0;
  if (g_once_init_enter(&type)) {
    GType existing = g_type_from_name("GstAmbaDrawDataAreaFlagsMetaAPI");
    if (existing != 0)
      g_once_init_leave(&type, existing);
    else {
      static const gchar *tags[] = { NULL };
      g_once_init_leave(&type, gst_meta_api_type_register("GstAmbaDrawDataAreaFlagsMetaAPI", tags));
    }
  }
  return type;
}

const GstMetaInfo *
gst_amba_draw_data_area_flags_meta_get_info(void)
{
  static const GstMetaInfo *info = NULL;
  if (g_once_init_enter(&info)) {
    const GstMetaInfo *mi = gst_meta_register(GST_AMBA_DRAW_DATA_AREA_FLAGS_META_API_TYPE,
        "GstAmbaDrawDataAreaFlagsMeta",
        sizeof(GstAmbaDrawDataAreaFlagsMeta),
        gst_amba_draw_data_area_flags_meta_init,
        NULL,
        gst_amba_draw_data_area_flags_meta_transform);
    g_once_init_leave(&info, mi);
  }
  return info;
}

GstAmbaDrawDataAreaFlagsMeta *
gst_amba_draw_data_area_flags_meta_get(GstBuffer *buf)
{
  if (!buf)
    return NULL;
  return (GstAmbaDrawDataAreaFlagsMeta *)gst_buffer_get_meta(buf,
      GST_AMBA_DRAW_DATA_AREA_FLAGS_META_API_TYPE);
}

gboolean
gst_amba_draw_data_area_flags_meta_set(GstBuffer *buf, guint area_slot, guint8 flags_mask)
{
  GstAmbaDrawDataAreaFlagsMeta *m;
  if (!buf || area_slot >= MAX_OVERLAY_AREA_NUM)
    return FALSE;
  m = gst_amba_draw_data_area_flags_meta_get(buf);
  if (!m) {
    m = (GstAmbaDrawDataAreaFlagsMeta *)gst_buffer_add_meta(buf,
        GST_AMBA_DRAW_DATA_AREA_FLAGS_META_INFO, NULL);
    if (!m)
      return FALSE;
  }
  m->valid_mask |= (1u << area_slot);
  m->flags[area_slot] = flags_mask;
  return TRUE;
}

void
gst_amba_draw_data_area_flags_meta_merge_from_buffer(GstBuffer *dst, GstBuffer *src)
{
  GstAmbaDrawDataAreaFlagsMeta *srcm, *dstm;
  guint s;
  if (!dst || !src)
    return;
  srcm = gst_amba_draw_data_area_flags_meta_get(src);
  if (!srcm || !srcm->valid_mask)
    return;
  dstm = gst_amba_draw_data_area_flags_meta_get(dst);
  if (!dstm) {
    dstm = (GstAmbaDrawDataAreaFlagsMeta *)gst_buffer_add_meta(dst,
        GST_AMBA_DRAW_DATA_AREA_FLAGS_META_INFO, NULL);
    if (!dstm)
      return;
  }
  for (s = 0; s < MAX_OVERLAY_AREA_NUM; s++) {
    if (srcm->valid_mask & (1u << s)) {
      dstm->valid_mask |= (1u << s);
      dstm->flags[s] = srcm->flags[s];
    }
  }
}
