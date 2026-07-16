/*
 * gstambafilevenc.h
 *
 * History:
 *    4/6/2025 - [Cheng Chen] created file
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
 * SECTION: element-amba_file_venc
 * @title: amba_file_venc
 *
 * aamba_file_venc can be used to do NV12 frame transform bitstream with Amba DSP HW.
 *
 */

#ifndef __GST_AMBA_FILE_VENC_H__
#define __GST_AMBA_FILE_VENC_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/base/gstbasetransform.h>
#include "bitstream_state.h"
#include "iav_al.h"

G_BEGIN_DECLS

#define GST_TYPE_AMBA_FILEVENC \
  (gst_amba_filevenc_get_type())
#define GST_AMBA_FILEVENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AMBA_FILEVENC,GstAmbaFileVenc))
#define GST_AMBA_FILEVENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AMBA_FILEVENC,GstAmbaFileVencClass))
#define GST_AMBA_FILEVENC_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_AMBA_FILEVENC,GstAmbaFileVencClass))
#define GST_IS_AMBA_FILEVENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AMBA_FILEVENC))
#define GST_IS_AMBA_FILEVENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AMBA_FILEVENC))

typedef struct _GstAmbaFileVenc GstAmbaFileVenc;
typedef struct _GstAmbaFileVencClass GstAmbaFileVencClass;

struct _GstAmbaFileVenc {
  GstBaseTransform parent;

  gint pitch;
  gint width;
  gint height;
  guint stream_id;
  guint stream_type;
  guint use_me0;
  guint frame_rate;
  guint cur_frame_cnt;
  guint64 gst_pts;
  guint64 dsp_pts_counter;

  guint8 *me1_buf;
  guint me1_size;
  guint8 *me0_buf;
  guint me0_size;

  /*< private >*/
  iav_ctx_t *iav_ctx;
  amba_resource_info_t res_info;
  gchar *enc_info;
  gboolean start_flag;
  gboolean is_last_h265_frame;

  video_bs_state_t bs_states[IAV_STREAM_MAX_NUM_ALL];

  iav_efm_usr_cfg_t cfg;
  iav_efm_buf_info_t req_buf;
  iav_efm_feed_cfg_t feed_cfg;
};

struct _GstAmbaFileVencClass {
  GstBaseTransformClass parent_class;
};

GType gst_amba_filevenc_get_type (void);


G_END_DECLS

#endif /* __GST_AMBA_FILEVENC_H__ */
