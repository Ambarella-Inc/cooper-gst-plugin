/*
 * gstambavencblur.h
 *
 * History:
 *    1/22/2026 - [Cheng Chen] created file
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

/**
 * SECTION: element-amba_venc_blur
 * @title: amba_venc_blur
 *
 * Insert blur on streams with Amba DSP HW.
 *
 */

#ifndef __GST_AMBAVENCBLUR_H__
#define __GST_AMBAVENCBLUR_H__

#include <gst/gst.h>
#include "platform_al.h"
#include "element_common.h"
#include "iav_al.h"
#include "amba_ml_decoded_result.h"


G_BEGIN_DECLS

#define GST_TYPE_AMBAVENCBLUR (gst_amba_venc_blur_get_type())
#define GST_AMBAVENCBLUR(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AMBAVENCBLUR,GstAmbaVencBlur))
#define GST_AMBAVENCBLUR_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AMBAVENCBLUR,GstAmbaVencBlurClass))
#define GST_AMBAVENCBLUR_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_AMBAVENCBLUR,GstAmbaVencBlurClass))
#define GST_IS_AMBAVENCBLUR(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AMBAVENCBLUR))
#define GST_IS_AMBAVENCBLUR_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AMBAVENCBLUR))

typedef struct _GstAmbaVencBlur GstAmbaVencBlur;
typedef struct _GstAmbaVencBlurClass GstAmbaVencBlurClass;

struct _GstAmbaVencBlur
{
  GstBaseSink parent;

  iav_ctx_t * iav_ctx;
  guint8 stream_id;
  guint32 stream_id_map;
  guint32 dsp_pts;
  blur_stream_info_t stream_info[IAV_STREAM_MAX_NUM_ALL];
  iav_blur_stream_cfg_t blur_insert[IAV_STREAM_MAX_NUM_ALL];
  iav_blur_color_cfg_t blur_color_info;
  struct iav_rect enc_win;
};

struct _GstAmbaVencBlurClass
{
  GstBaseSinkClass parent_class;
};

GType gst_amba_venc_blur_get_type (void);

G_END_DECLS

#endif /* __GST_AMBAVENBLUR_H__ */
