/*
 * gstambaseimeta.h
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

#ifndef __GST_AMBA_SEI_META_H__
#define __GST_AMBA_SEI_META_H__

#include <gst/gst.h>
#include "sei_box.h"

G_BEGIN_DECLS

typedef struct _GstAmbaSeiMeta GstAmbaSeiMeta;

struct _GstAmbaSeiMeta
{
  GstMeta meta;

  guint64 present_mask;
  guint64 timestamp_ns;
  gint gps_valid;
  gint32 gps_lat_e7;
  gint32 gps_lon_e7;
  gint32 gps_alt_cm;
  guint16 payload_version;
  guint16 payload_flags;
};

GType gst_amba_sei_meta_api_get_type (void);
const GstMetaInfo *gst_amba_sei_meta_get_info (void);

#define GST_AMBA_SEI_META_API_TYPE (gst_amba_sei_meta_api_get_type())
#define GST_AMBA_SEI_META_INFO (gst_amba_sei_meta_get_info())

GstAmbaSeiMeta *gst_buffer_add_amba_sei_meta (GstBuffer *buffer,
    const SeiBoxInfo *info, const SeiBoxDecodeMeta *decode_meta);
GstAmbaSeiMeta *gst_buffer_get_amba_sei_meta (GstBuffer *buffer);

/* Helper to check if meta exists on buffer (convenience wrapper). */
gboolean gst_buffer_has_amba_sei_meta (GstBuffer *buffer);

G_END_DECLS

#endif /* __GST_AMBA_SEI_META_H__ */
