/*
 * gstambahwvdecsrc.h
 *
 * History:
 *    6/16/2022 - [Zhi He] created file
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
 */

/**
 * SECTION: element-amba_hwvdecsrc
 * @title: amba_hwvdecsrc
 *
 * amba_camsrc can be used to capture video frames from Amba HW video decoder.
 *
 */

#ifndef __GST_AMBA_HWVDECSRC_H__
#define __GST_AMBA_HWVDECSRC_H__

#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>

G_BEGIN_DECLS

#define GST_TYPE_AMBA_HWVDECSRC \
  (gst_amba_hwvdecsrc_get_type())
#define GST_AMBA_HWVDECSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AMBA_HWVDECSRC,GstAmbaHwvdecsrc))
#define GST_AMBA_HWVDECSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AMBA_HWVDECSRC,GstAmbaHwvdecsrcClass))
#define GST_IS_AMBA_HWVDECSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AMBA_HWVDECSRC))
#define GST_IS_AMBA_HWVDECSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AMBA_HWVDECSRC))


typedef struct _GstAmbaHwvdecsrc GstAmbaHwvdecsrc;
typedef struct _GstAmbaHwvdecsrcClass GstAmbaHwvdecsrcClass;

struct _GstAmbaHwvdecsrc {
  GstPushSrc pushsrc;

  /*< private >*/
  gchar * str_pixel_format;

  guint buf_id;

  guint width;
  guint height;

  guint framerate_num;
  guint framerate_den;
  gfloat framerate;

  guint pixel_format_fourcc;

  iav_ctx_t * iav_ctx;
};

struct _GstAmbaHwvdecsrcClass {
  GstPushSrcClass parent_class;
};

GType gst_amba_hwvdecsrc_get_type (void);

G_END_DECLS

#endif /* __GST_AMBA_HWVDECSRC_H__ */

