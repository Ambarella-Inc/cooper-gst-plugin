/*
 * gstambavideoscale.h
 *
 * History:
 *    4/28/2025 - [Scott(Shou-Wen) Yu] created file
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
 * SECTION: element-amba_videoscale
 * @title: amba_videoscale
 *
 * amba_videoscale can be used to do NV12 frame scaling with Amba image scaler HW.
 *
 */

#ifndef __GST_AMBA_VIDEOSCALE_H__
#define __GST_AMBA_VIDEOSCALE_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideofilter.h>

G_BEGIN_DECLS

#define GST_TYPE_AMBA_VIDEOSCALE \
  (gst_amba_videoscale_get_type())
#define GST_AMBA_VIDEOSCALE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AMBA_VIDEOSCALE,GstAmbaVideoScale))
#define GST_AMBA_VIDEOSCALE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AMBA_VIDEOSCALE,GstAmbaVideoScaleClass))
#define GST_IS_AMBA_VIDEOSCALE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AMBA_VIDEOSCALE))
#define GST_IS_AMBA_VIDEOSCALE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AMBA_VIDEOSCALE))
#define GST_AMBA_VIDEOSCALE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_AMBA_VIDEOSCALE,GstAmbaVideoScaleClass))
  #define GST_AMBA_VIDEOSCALE_CAST(obj) ((GstAmbaVideoScale *)(obj))

typedef struct _GstAmbaVideoScale GstAmbaVideoScale;
typedef struct _GstAmbaVideoScaleClass GstAmbaVideoScaleClass;

struct _GstAmbaVideoScale {
  GstVideoFilter parent;

  /*< private >*/
  iav_ctx_t * iav_ctx;

  GstVideoInfo in_info;
  GstVideoInfo out_info;
  GstCaps *incaps;
  GstCaps *outcaps;
  GstAllocator *cavalry_allocator;
  GstMemory *input_mem;   // internal use for input copy
  GstMemory *output_mem;  // internal use for output copy
  GstMapInfo input_map;
  GstMapInfo output_map;

  GstQuery *decide_query;

  gboolean zero_copy;
  gboolean logged_zero_copy_path;
};

struct _GstAmbaVideoScaleClass {
  GstVideoFilterClass parent_class;
};

GType gst_amba_videoscale_get_type (void);

G_END_DECLS

#endif /* __GST_AMBA_VIDEOSCALE_H__ */