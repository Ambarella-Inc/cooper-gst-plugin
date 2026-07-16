/*
 * gstambaseimeta.c
 *
 * History:
 *    04/09/2026 - [Yang Yu] created file
 *
 * Copyright (C) 2026 Ambarella International LP
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

#include "gstambaseimeta.h"
#include <string.h>

static gboolean
gst_amba_sei_meta_init (GstMeta *meta, gpointer params, GstBuffer *buffer)
{
  GstAmbaSeiMeta *m = (GstAmbaSeiMeta *) meta;
  (void) params;
  (void) buffer;
  m->present_mask = 0;
  m->timestamp_ns = 0;
  m->gps_valid = 0;
  m->gps_lat_e7 = 0;
  m->gps_lon_e7 = 0;
  m->gps_alt_cm = 0;
  m->payload_version = 0;
  m->payload_flags = 0;
  return TRUE;
}

static gboolean
gst_amba_sei_meta_transform (GstBuffer *transbuf, GstMeta *meta,
    GstBuffer *buffer, GQuark type, gpointer data)
{
  GstAmbaSeiMeta *src = (GstAmbaSeiMeta *) meta;
  GstAmbaSeiMeta *dst;
  (void) buffer;
  (void) type;
  (void) data;

  dst = (GstAmbaSeiMeta *) gst_buffer_add_meta (transbuf,
      GST_AMBA_SEI_META_INFO, NULL);
  if (!dst)
    return FALSE;

  dst->present_mask = src->present_mask;
  dst->timestamp_ns = src->timestamp_ns;
  dst->gps_valid = src->gps_valid;
  dst->gps_lat_e7 = src->gps_lat_e7;
  dst->gps_lon_e7 = src->gps_lon_e7;
  dst->gps_alt_cm = src->gps_alt_cm;
  dst->payload_version = src->payload_version;
  dst->payload_flags = src->payload_flags;
  return TRUE;
}

GType
gst_amba_sei_meta_api_get_type (void)
{
  static GType type = 0;
  if (g_once_init_enter (&type)) {
    GType existing = g_type_from_name ("GstAmbaSeiMetaAPI");
    if (existing != 0) {
      g_once_init_leave (&type, existing);
    } else {
      static const gchar *tags[] = { NULL };
      g_once_init_leave (&type,
          gst_meta_api_type_register ("GstAmbaSeiMetaAPI", tags));
    }
  }
  return type;
}

const GstMetaInfo *
gst_amba_sei_meta_get_info (void)
{
  static const GstMetaInfo *info = NULL;
  if (g_once_init_enter (&info)) {
    const GstMetaInfo *mi = gst_meta_register (GST_AMBA_SEI_META_API_TYPE,
        "GstAmbaSeiMeta",
        sizeof (GstAmbaSeiMeta),
        gst_amba_sei_meta_init,
        NULL,
        gst_amba_sei_meta_transform);
    g_once_init_leave (&info, mi);
  }
  return info;
}

GstAmbaSeiMeta *
gst_buffer_add_amba_sei_meta (GstBuffer *buffer,
    const SeiBoxInfo *info, const SeiBoxDecodeMeta *decode_meta)
{
  GstAmbaSeiMeta *meta;
  SeiBoxTimestamp ts = 0;
  SeiBoxGpsData gps;
  int has_ts;
  int has_gps;

  if (!buffer || !info)
    return NULL;

  memset (&gps, 0, sizeof (gps));
  meta = (GstAmbaSeiMeta *) gst_buffer_add_meta (buffer,
      GST_AMBA_SEI_META_INFO, NULL);
  if (!meta)
    return NULL;

  meta->present_mask = (guint64) sei_box_info_get_present_mask (info);
  has_ts = sei_box_info_is_present (info, SEI_BOX_T_TIMESTAMP);
  has_gps = sei_box_info_is_present (info, SEI_BOX_T_GPS);

  if (has_ts) {
    (void) sei_box_info_get (info, SEI_BOX_T_TIMESTAMP,
        &ts, SEI_BOX_INFO_VALUE_SIZE_TIMESTAMP);
    meta->timestamp_ns = (guint64) ts;
  }

  if (has_gps) {
    (void) sei_box_info_get (info, SEI_BOX_T_GPS, &gps, SEI_BOX_INFO_VALUE_SIZE_GPS);
    meta->gps_valid = gps.valid;
    meta->gps_lat_e7 = gps.lat_e7;
    meta->gps_lon_e7 = gps.lon_e7;
    meta->gps_alt_cm = gps.alt_cm;
  }

  if (decode_meta) {
    meta->payload_version = decode_meta->payload_version;
    meta->payload_flags = decode_meta->payload_flags;
  }

  return meta;
}

GstAmbaSeiMeta *
gst_buffer_get_amba_sei_meta (GstBuffer *buffer)
{
  if (!buffer)
    return NULL;
  return (GstAmbaSeiMeta *) gst_buffer_get_meta (buffer,
      GST_AMBA_SEI_META_API_TYPE);
}

gboolean
gst_buffer_has_amba_sei_meta (GstBuffer *buffer)
{
  return gst_buffer_get_amba_sei_meta (buffer) != NULL;
}
