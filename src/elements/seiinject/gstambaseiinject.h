/*
 * gstambaseiinject.h
 *
 * History:
 *    04/09/2026 - [Yang Yu] created file
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

#ifndef __GST_AMBA_SEIINJECT_H__
#define __GST_AMBA_SEIINJECT_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>

G_BEGIN_DECLS

#define GST_TYPE_AMBA_SEIINJECT       (gst_amba_seiinject_get_type ())
#define GST_AMBA_SEIINJECT(obj)       (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_AMBA_SEIINJECT, GstAmbaSeiInject))
#define GST_AMBA_SEIINJECT_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_AMBA_SEIINJECT, GstAmbaSeiInjectClass))
#define GST_IS_AMBA_SEIINJECT(obj)    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_AMBA_SEIINJECT))
#define GST_AMBA_SEIINJECT_CAST(obj)  ((GstAmbaSeiInject *)(obj))

typedef struct _GstAmbaSeiInject GstAmbaSeiInject;
typedef struct _GstAmbaSeiInjectClass GstAmbaSeiInjectClass;
typedef struct _SeiBoxInfo SeiBoxInfo;

struct _GstAmbaSeiInject {
  GstBaseTransform parent;
  gboolean add_timestamp;
  gboolean add_gps;
  gboolean self_verify;
  gchar   *gps_device;
  guint   lib_log_level;
  guint   codec_id;  /* from negotiated caps, aligned with SeiBoxCodec */
  guint64 gps_tick;
  SeiBoxInfo *info;
};

struct _GstAmbaSeiInjectClass {
  GstBaseTransformClass parent_class;
};

GType gst_amba_seiinject_get_type (void);

G_END_DECLS

#endif /* __GST_AMBA_SEIINJECT_H__ */
