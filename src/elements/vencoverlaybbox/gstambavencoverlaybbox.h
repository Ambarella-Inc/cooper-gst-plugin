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

#ifndef __GST_AMBAVENCOVERLAY_BBOX_H__
#define __GST_AMBAVENCOVERLAY_BBOX_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideosink.h>

#include "platform_al.h"
#include "element_common.h"
#include "iav_al.h"
#include "overlay_common.h"

G_BEGIN_DECLS

#define GST_TYPE_AMBAVENCOVERLAYBBOX (gst_amba_venc_overlay_bbox_get_type())
#define GST_AMBAVENCOVERLAYBBOX(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AMBAVENCOVERLAYBBOX,GstAmbaVencOverlayBbox))
#define GST_AMBAVENCOVERLAYBBOX_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AMBAVENCOVERLAYBBOX,GstAmbaVencOverlayBboxClass))
#define GST_AMBAVENCOVERLAYBBOX_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_AMBAVENCOVERLAYBBOX,GstAmbaVencOverlayBboxClass))
#define GST_IS_AMBAVENCOVERLAYBBOX(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AMBAVENCOVERLAYBBOX))
#define GST_IS_AMBAVENCOVERLAYBBOX_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AMBAVENCOVERLAYBBOX))


#define THREAD_NUM (4)
#define ALIGN_BITS 64
#define NEON_WIDTH  (8)
//#define OVERLAY_YUV_OFFSET  (1024 * IAV_STREAM_MAX_NUM_ALL)
#define FRAME_DIFF 15

#ifndef MAX_TEXT_SIZE
#define MAX_TEXT_SIZE               200
#endif
#define BOX_COLOR 15
#define TEXT_FORE_COLOR 14
#define TEXT_BACK_COLOR 255


typedef struct _GstAmbaVencOverlayBbox GstAmbaVencOverlayBbox;
typedef struct _GstAmbaVencOverlayBboxClass GstAmbaVencOverlayBboxClass;

typedef struct pts_s {
  gulong mono_pts;
  guint dsp_pts;
} pts_t;

typedef struct thread_arg_t {
  guint shift_bits;
  guint width;
  guint pitch;
  guint start_h;
  guint end_h;
  guchar *addr;
  guchar *dst;
  sem_t notice_sem;
  sem_t finish_sem;
} thread_arg;

typedef struct {
  gint border_thickness;                   /*!< The border thickness of the box. */
  guchar box_color;                           /*!< The color of the box. */
  gint font_size_w;
  gint font_size_h;
  guchar text_fore_color;                          /*!< The color of the title or the text. */
  guchar text_back_color;                          /*!< The color of the title or the text. */
  guchar reserved;
} gst_display_obj_params_t;

typedef struct gst_overlay_box_s {
  gushort offset_x;
  gushort offset_y;
  gushort width;
  gushort height;

  guint area_h;
  guint area_w;
  guint area_pitch;

  guchar color;
  guchar line_thickness;
  guchar reserved[2];
} gst_overlay_box_t;

typedef struct gst_overlay_text_s {
  gst_overlay_box_t *box;

  const gchar *text;
  gushort text_length;

  guchar fore_color;
  guchar back_color;
} gst_overlay_text_t;

typedef struct
{
  GstVideoInfo info;

  guint alpha[IAV_STREAM_MAX_NUM_ALL];
  thread_arg th_arg[IAV_STREAM_MAX_NUM_ALL][THREAD_NUM];
  pthread_t thread[IAV_STREAM_MAX_NUM_ALL][THREAD_NUM];
  iav_set_overlay_t overlay_set[IAV_STREAM_MAX_NUM_ALL];
  void *img_ctx[IAV_STREAM_MAX_NUM_ALL];
  stream_param_t stream_params[IAV_STREAM_MAX_NUM_ALL];
  gulong osd_offset[IAV_STREAM_MAX_NUM_ALL];
  gulong osd_size[IAV_STREAM_MAX_NUM_ALL];

  gchar font_file[DMAX_FILE_NAME_LENGTH]; // absolute path
  gchar bmp_file[DMAX_FILE_NAME_LENGTH];   // bitmap file path
  void *p_font;
  guchar *p_text_font_buffer;
  gint text_buffer_width;
  gint text_buffer_height;
  gint text_buffer_origin_x;
  gint text_buffer_origin_y;

  void *net_result;
  gst_display_obj_params_t display;

  gfloat score_limit;

  unsigned int stream_bit_map;

  guint last_dsp_pts[IAV_STREAM_MAX_NUM_ALL]; /* valid dsp_pts from meta per stream; for finalize apply_frame_sync */

  int stream_id;
  unsigned char area_id;      /* area id for bbox overlay */
  unsigned char bmp_area_id;  /* area id for bitmap overlay */
  unsigned char bitmap_dirty; /* 1: need to redraw bitmap overlay (file/area/roi changed) */
  unsigned char reserved[1];
  bitmap_buffer_t bitmap;

  iav_ctx_t * iav_ctx;

} priv_venc_overlay_bbox_ctx_t;

struct _GstAmbaVencOverlayBbox
{
  GstVideoSink videosink;

  priv_venc_overlay_bbox_ctx_t *priv_ctx;
};

struct _GstAmbaVencOverlayBboxClass
{
  GstVideoSinkClass parent_class;
};

GType gst_amba_venc_overlay_bbox_get_type (void);

G_END_DECLS

#endif /* __GST_AMBAVENCOVERLAY_BBOX_H__ */
