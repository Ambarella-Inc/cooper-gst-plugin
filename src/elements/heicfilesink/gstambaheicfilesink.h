/*
 * gstambaheicfilesink.h
 *
 * History:
 *    6/11/2022 - [Zhi He] created file
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
 * SECTION: element-amba_heicfilesink
 * @title: amba_heicfilesink
 *
 * amba_heicfilesink can be used to store HEIC files.
 *
 */

#ifndef __GST_AMBAHEICFILESINK_H__
#define __GST_AMBAHEICFILESINK_H__

#include <gst/gst.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <stdio.h>

#include <gst/base/gstbasesink.h>
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>

#include "file_dumper.h"

G_BEGIN_DECLS

#define GST_TYPE_AMBAHEICFILESINK \
  (gst_amba_heicfilesink_get_type())
#define GST_AMBAHIECFILESINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AMBAHEICFILESINK,GstAmbaHeicfilesink))
#define GST_AMBAHEICFILESINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AMBAHEICFILESINK,GstAmbaHeicfilesinkClass))
#define GST_IS_AMBAHEICFILESINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AMBAHEICFILESINK))
#define GST_IS_AMBAHEICFILESINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AMBAHEICFILESINK))
#define GST_AMBAHEICFILESINK_CAST(obj) ((GstAmbaHeicfilesink *)(obj))

typedef struct _GstAmbaHeicfilesink GstAmbaHeicfilesink;
typedef struct _GstAmbaHeicfilesinkClass GstAmbaHeicfilesinkClass;


/**
 * GstAmbaHeicfilesink:
 *
 * Opaque #GstAmbaHeicfilesink structure.
 */
struct _GstAmbaHeicfilesink {
  GstBaseSink parent;

  gchar * filename_base;

  file_dump_t file_dump;
};

struct _GstAmbaHeicfilesinkClass {
  GstBaseSinkClass parent_class;
};

G_GNUC_INTERNAL GType gst_amba_heicfilesink_get_type (void);

G_END_DECLS

#endif /* __GST_AMBAHEICFILESINK_H__ */

