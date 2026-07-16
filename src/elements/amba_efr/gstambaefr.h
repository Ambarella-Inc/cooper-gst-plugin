/*
 * gstambaefr.h
 *
 * History:
 *    5/20/2025 - [Ji Zhang] created file
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

/**
 * SECTION: element-amba_efr
 * @title: amba_efr
 *
 * amba_efr is efr sink element
 *
 */


#ifndef __GST_AMBA_EFR_H__
#define __GST_AMBA_EFR_H__

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>
#include "element_common.h"

#define GST_TYPE_AMBA_EFR \
  (gst_amba_efr_get_type())
#define GST_AMBA_EFR(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AMBA_EFR,GstAmbaEfr))
#define GST_AMBA_EFR_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AMBA_EFR,GstAmbaEfrClass))
#define GST_IS_AMBA_EFR(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AMBA_EFR))
#define GST_IS_AMBA_EFR_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AMBA_EFR))

G_BEGIN_DECLS

#if defined (BUILD_DSP_AMBA_V5)
#define GST_AMBA_EFR_PITCH_ALIGN LPDDR4_ALIGN
#elif defined (BUILD_DSP_AMBA_V6)
#define GST_AMBA_EFR_PITCH_ALIGN IAV_DSP_BUF_PITCH_ALIGN
#endif

#define GST_EFR_RAW_BUF_NUM (4)

typedef struct _GstAmbaEfr      GstAmbaEfr;
typedef struct _GstAmbaEfrClass GstAmbaEfrClass;

struct _GstAmbaEfr {
  GstBaseSink parent;
  gchar *location;
  FILE *file;

  guint width;
  guint height;

  guint file_type;
  gint vinc_id;
  gboolean live_mode;
  gint fd_audio_tick;

  iav_ctx_t * iav_ctx;
  amba_iav_partition_t iav_partition;
  amba_resource_info_t resource_info;
  amba_efr_setup_t efr_setup;
};

struct _GstAmbaEfrClass {
  GstBaseSinkClass parent_class;
};

GType gst_amba_efr_get_type (void);

G_END_DECLS

#endif /* __GST_AMBA_EFR_H__ */

