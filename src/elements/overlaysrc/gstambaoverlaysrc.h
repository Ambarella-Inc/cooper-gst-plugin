/*
 * gstambaoverlaysrc.h
 *
 * History:
 *    4/19/2024 - [Peng-Xue Duan] created file
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
 * SECTION: element-amba_overlay_src
 * @title: amba_overlay_src
 *
 * amba_overlay_src can be used to capture encoded video bitstream from Amba device.
 *
 */

#ifndef __GST_AMBAOVERLAYSRC_H__
#define __GST_AMBAOVERLAYSRC_H__

#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>

#include "clock_if.h"
#include "gstambahwclock.h"

#include "iav_ctx.h"
#include "element_common.h"
#include "overlay_common.h"


G_BEGIN_DECLS

#define GST_TYPE_AMBAOVERLAYSRC \
  (gst_amba_overlay_src_get_type())
#define GST_AMBAOVERLAYSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AMBAOVERLAYSRC,GstAmbaOverlaySrc))
#define GST_AMBAOVERLAYSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AMBAOVERLAYSRC,GstAmbaOverlaySrcClass))
#define GST_IS_AMBAOVERLAYSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AMBAOVERLAYSRC))
#define GST_IS_AMBAOVERLAYSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AMBAOVERLAYSRC))

#define EDRAWFORMATNAME_8BIT "8bit" //!< 8bit YUV lookup table
#define EDRAWFORMATNAME_RGB565 "rgb565" //!< RGB565,value 0x0000 is full transparent
#define EDRAWFORMATNAME_UYV565 "uyv565"
#define EDRAWFORMATNAME_BGR565 "bgr565"
#define EDRAWFORMATNAME_AYUV4444 "ayuv4444"
#define EDRAWFORMATNAME_RGBA4444 "rgba4444"
#define EDRAWFORMATNAME_BGRA4444 "bgra4444"
#define EDRAWFORMATNAME_ABGR4444 "abgr4444"
#define EDRAWFORMATNAME_ARGB4444 "argb4444"
#define EDRAWFORMATNAME_AYUV1555 "ayuv1555"
#define EDRAWFORMATNAME_YUV1555 "yuv1555"
#define EDRAWFORMATNAME_RGBA5551 "rgba5551"
#define EDRAWFORMATNAME_BGRA5551 "bgra5551"
#define EDRAWFORMATNAME_ABGR1555 "abgr1555"
#define EDRAWFORMATNAME_ARGB1555 "argb1555"
#define EDRAWFORMATNAME_AYUV8888 "ayuv8888"
#define EDRAWFORMATNAME_RGBA8888 "rgba8888"
#define EDRAWFORMATNAME_BGRA8888 "bgra8888"
#define EDRAWFORMATNAME_ABGR8888 "abgr8888"
#define EDRAWFORMATNAME_ARGB8888 "argb8888" //!< ARGB8888

#define EDRAWTYPENAME_STRING "string"
#define EDRAWTYPENAME_PICTURE "picture"
#define EDRAWTYPENAME_TIME "time"

typedef struct _GstAmbaOverlaySrc GstAmbaOverlaySrc;
typedef struct _GstAmbaOverlaySrcClass GstAmbaOverlaySrcClass;

typedef struct {
  unsigned char always_insert; //!< To specify whether to always insert
  unsigned char sync_with_pts; //!< To specify use frame sync method, it is usefull to do overlay for a specify frame
  unsigned char stream_rotate;
  unsigned char area_num; //!< To specify area number in one stream
  //unsigned char reserved;

  amba_overlay_area_param_t area[MAX_OVERLAY_AREA_NUM];
} amba_overlay_params_t;

typedef struct {
  /*< private >*/
  //iav_ctx_t * iav_ctx;

  // clock related
  GstClockTime gst_clok_time;
  guint is_clock_started;
  gboolean give_clock;
  GstAmbaHwClock * provided_clock;

  guchar refresh;
  guchar reserved[3];

  int draw_format;
  char draw_fmt_name[128];
  guint draw_pix_size;

  //get overlay configure information if needed
  gchar *osd_info;

  amba_overlay_params_t osd_param;//[IAV_STREAM_MAX_NUM_ALL];

  bitmap_buffer_t bitmap;

  unsigned int frame_count[MAX_OVERLAY_AREA_NUM];

  unsigned char reset_bg[MAX_OVERLAY_AREA_NUM];
} priv_overlay_src_ctx_t;

/* Definition of structure storing data for this element. */
struct _GstAmbaOverlaySrc
{
  GstPushSrc pushsrc;

  guint print_info; // for debug purpose


  priv_overlay_src_ctx_t *priv_ctx;

};

struct _GstAmbaOverlaySrcClass
{
  GstPushSrcClass parent_class;
};

GType gst_amba_overlay_src_get_type(void);

G_END_DECLS

#endif /* __GST_AMBAVENCCAP_H__ */

