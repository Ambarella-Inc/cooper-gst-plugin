/*
 * gstambavprocimgcvt.h
 *
 * History:
 *    8/4/2025 - [Cheng Chen] created file
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
 * SECTION: element-amba_img_cvt
 * @title: amba_img_cvt
 *
 * amba_img_cvt can be used to do YUV420 Planar image transform YUV420 image with Amba VProc HW.
 *
 */

#ifndef __GST_AMBA_VPROC_IMGCVT_H__
#define __GST_AMBA_VPROC_IMGCVT_H__

#include <gst/base/gstbasetransform.h>
#include "cv_vproc.h"
#include "iav_ctx.h"

G_BEGIN_DECLS

#define GST_TYPE_AMBA_VPROC_IMCVT \
  (gst_amba_vproc_imcvt_get_type())
#define GST_AMBA_VPROC_IMCVT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AMBA_VPROC_IMCVT,GstAmbaVprocImcvt))
#define GST_AMBA_VPROC_IMCVT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AMBA_VPROC_IMCVT,GstAmbaVprocImcvtClass))
#define GST_AMBA_VPROC_IMCVT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_AMBA_VPROC_IMCVT,GstAmbaVprocImcvtClass))
#define GST_IS_AMBA_VPROC_IMCVT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AMBA_VPROC_IMCVT))
#define GST_IS_AMBA_VPROC_IMCVT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AMBA_VPROC_IMCVT))

enum format_cvt_type {
  IYUV_TO_NV12 = 0,
  NV12_TO_RGBP,
  IYUV_TO_RGBP,
};

typedef struct _GstAmbaVprocImcvt GstAmbaVprocImcvt;
typedef struct _GstAmbaVprocImcvtClass GstAmbaVprocImcvtClass;

struct cv_mem {
  void *virt;
  int mfd;
  unsigned long size;
};

struct _GstAmbaVprocImcvt {
  GstBaseTransform parent;
  GstQuery *decide_query;
  GstAllocator *cavalry_allocator;
  GstCaps *incaps;
  gchar *colorimetry;
  gchar *range;
  iav_ctx_t * iav_ctx;
  cv_vproc_ctx_t *cv_ctx;
  guint8 *load_vect_data;
  guint32 load_vect_data_sz;
  gboolean load_vect_data_inited;
  gint iw, ih, ic;
  gint ow, oh, oc;
  gint ip, op;
  gint format_cvt_type;
  gboolean zero_copy;
  /*!< One-shot: first-frame stderr diagnostic for input path (zero-copy vs copy). */
  gboolean logged_input_path_once;
};

struct _GstAmbaVprocImcvtClass {
  GstBaseTransformClass parent_class;
};

GType gst_amba_vproc_imcvt_get_type (void);


G_END_DECLS

#endif /* __GST_AMBA_VPROC_IMGCVT_H__ */
