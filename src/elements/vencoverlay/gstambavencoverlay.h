/*
 * GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2020 Niels De Graef <niels.degraef@gmail.com>
 * Copyright (C) 2022 PengXue Duan <<pxduan@ambarella.com>>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GST_AMBAVENCOVERLAY_H__
#define __GST_AMBAVENCOVERLAY_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideosink.h>

#include "platform_al.h"
#include "element_common.h"
#include "iav_al.h"

G_BEGIN_DECLS

#define GST_TYPE_AMBAVENCOVERLAY (gst_amba_venc_overlay_get_type())
#define GST_AMBAVENCOVERLAY(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AMBAVENCOVERLAY,GstAmbaVencOverlay))
#define GST_AMBAVENCOVERLAY_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AMBAVENCOVERLAY,GstAmbaVencOverlayClass))
#define GST_AMBAVENCOVERLAY_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_AMBAVENCOVERLAY,GstAmbaVencOverlayClass))
#define GST_IS_AMBAVENCOVERLAY(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AMBAVENCOVERLAY))
#define GST_IS_AMBAVENCOVERLAY_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AMBAVENCOVERLAY))


typedef struct _GstAmbaVencOverlay GstAmbaVencOverlay;
typedef struct _GstAmbaVencOverlayClass GstAmbaVencOverlayClass;

typedef struct
{
  GstVideoInfo info;

  //get encoding configure information if needed
  gchar *enc_info;

  gint fps_n[IAV_STREAM_MAX_NUM_ALL];
  gint fps_d[IAV_STREAM_MAX_NUM_ALL];
  gint fps[IAV_STREAM_MAX_NUM_ALL];

  guint force_idr[IAV_STREAM_MAX_NUM_ALL];

  guint alpha[IAV_STREAM_MAX_NUM_ALL];
  iav_set_overlay_t overlay_set[IAV_STREAM_MAX_NUM_ALL];
  void *img_ctx[IAV_STREAM_MAX_NUM_ALL];
  stream_param_t stream_params[IAV_STREAM_MAX_NUM_ALL];

  gulong osd_offset[IAV_STREAM_MAX_NUM_ALL];
  gulong osd_size[IAV_STREAM_MAX_NUM_ALL];

  gint pixel_fmt[IAV_STREAM_MAX_NUM_ALL];
  guint pixel_size[IAV_STREAM_MAX_NUM_ALL];

  gchar font_file[DMAX_FILE_NAME_LENGTH]; // absolute path
  void *p_font;
  guchar *p_text_font_buffer;
  gint text_buffer_width;
  gint text_buffer_height;
  gint text_buffer_origin_x;
  gint text_buffer_origin_y;

  gfloat score_limit;

  int stream_id;
  unsigned int stream_id_map;

  iav_ctx_t * iav_ctx;

} priv_venc_overlay_ctx_t;

struct _GstAmbaVencOverlay
{
  GstVideoSink videosink;

  priv_venc_overlay_ctx_t *priv_ctx;
};

struct _GstAmbaVencOverlayClass
{
  GstVideoSinkClass parent_class;
};

GType gst_amba_venc_overlay_get_type (void);

G_END_DECLS

#endif /* __GST_AMBAVENCOVERLAY_H__ */
