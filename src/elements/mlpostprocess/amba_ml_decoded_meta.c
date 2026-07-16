/*
 * amba_ml_decoded_meta.c
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
 * GstAmbaMlDecodedMeta implementation.
 */

#include "amba_ml_decoded_result.h"

#ifndef DUNUSED
#define DUNUSED(x) (void)(x)
#endif

static gboolean
gst_amba_ml_decoded_meta_init(GstMeta *meta, gpointer params, GstBuffer *buffer)
{
  DUNUSED(params);
  DUNUSED(buffer);
  GstAmbaMlDecodedMeta *ml_meta = (GstAmbaMlDecodedMeta *)meta;
  ml_meta->n_entries = 0;
  return TRUE;
}

static gboolean
gst_amba_ml_decoded_meta_transform(GstBuffer *transbuf, GstMeta *meta,
    GstBuffer *buffer, GQuark type, gpointer data)
{
  DUNUSED(buffer);
  DUNUSED(type);
  DUNUSED(data);
  GstAmbaMlDecodedMeta *src = (GstAmbaMlDecodedMeta *)meta;
  GstAmbaMlDecodedMeta *dst = (GstAmbaMlDecodedMeta *)gst_buffer_add_meta(transbuf,
      GST_AMBA_ML_DECODED_META_INFO, NULL);
  if (!dst)
    return FALSE;
  dst->n_entries = src->n_entries;
  if (src->n_entries <= GST_AMBA_ML_DECODED_META_MAX_ENTRIES)
    memcpy(dst->entries, src->entries, src->n_entries * sizeof(GstAmbaMlDecodedMetaEntry));
  return TRUE;
}

GType
gst_amba_ml_decoded_meta_api_get_type(void)
{
  static GType type = 0;
  if (g_once_init_enter(&type)) {
    GType existing = g_type_from_name("GstAmbaMlDecodedMetaAPI");
    if (existing != 0)
      g_once_init_leave(&type, existing);
    else {
      static const gchar *tags[] = { NULL };
      g_once_init_leave(&type, gst_meta_api_type_register("GstAmbaMlDecodedMetaAPI", tags));
    }
  }
  return type;
}

const GstMetaInfo *
gst_amba_ml_decoded_meta_get_info(void)
{
  static const GstMetaInfo *info = NULL;
  if (g_once_init_enter(&info)) {
    const GstMetaInfo *mi = gst_meta_register(GST_AMBA_ML_DECODED_META_API_TYPE,
        "GstAmbaMlDecodedMeta",
        sizeof(GstAmbaMlDecodedMeta),
        gst_amba_ml_decoded_meta_init,
        NULL,
        gst_amba_ml_decoded_meta_transform);
    g_once_init_leave(&info, mi);
  }
  return info;
}
