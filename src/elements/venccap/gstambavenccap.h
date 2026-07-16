/*
 * gstambavenccap.h
 *
 * History:
 *    6/2/2022 - [Zhi He] created file
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
 * SECTION: element-amba_venccap
 * @title: amba_amba_venccap
 *
 * amba_venccap can be used to capture encoded video bitstream from Amba device.
 *
 */

#ifndef __GST_AMBAVENCCAP_H__
#define __GST_AMBAVENCCAP_H__

#include <gst/gst.h>
#include <gst/gstallocator.h>
#include <gst/base/gstpushsrc.h>
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>

#include "element_common.h"

#include "clock_if.h"
#include "gstambahwclock.h"

#include "iav_al.h"
#include "iav_ctx.h"


G_BEGIN_DECLS

#define GST_TYPE_AMBAVENCCAP \
  (gst_amba_venccap_get_type())
#define GST_AMBAVENCCAP(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AMBAVENCCAP,GstAmbaVenccap))
#define GST_AMBAVENCCAP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AMBAVENCCAP,GstAmbaVenccapClass))
#define GST_IS_AMBAVENCCAP(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AMBAVENCCAP))
#define GST_IS_AMBAVENCCAP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AMBAVENCCAP))

typedef struct {
  unsigned int is_read;
  iav_ctx_t * iav_ctx;
  amba_dsp_release_bitstream_t release_bs;

  unsigned char * p_data_sim;
} amba_release_bits_t;

typedef struct _GstAmbaVenccap GstAmbaVenccap;
typedef struct _GstAmbaVenccapClass GstAmbaVenccapClass;

/* Definition of structure storing data for this element. */
struct _GstAmbaVenccap
{
  GstPushSrc pushsrc;

  guint print_info; // for debug purpose

  /*< private >*/
  iav_ctx_t * iav_ctx;

  video_bs_state_t bs_states[IAV_STREAM_MAX_NUM_ALL];
  shared_stream_info_u shared_stream_info;

  gboolean sync;

  // clock related
  GstClockTime gst_clok_time;
  guint is_clock_setup;
  guint is_clock_started;
  clock_ctx_t * clock;

  //get encoding configure information if needed
  gchar *enc_info;

  gint fps_n[IAV_STREAM_MAX_NUM_ALL];
  gint fps_d[IAV_STREAM_MAX_NUM_ALL];
  gint fps[IAV_STREAM_MAX_NUM_ALL];
  gint stream_type[IAV_STREAM_MAX_NUM_ALL];
  gint64 last_pts[IAV_STREAM_MAX_NUM_ALL];
  gint dump_num[IAV_STREAM_MAX_NUM_ALL];

  guint force_idr_map;
  guchar enc_dummy_latency[DAMBA_MAX_CANVAS_BUF_NUM_ALIGIN];
  guint canvas_num;
  guint max_stream_num;

  guchar alloc_mem;
  guchar is_eos_com;
  guchar reserved[2];

  GstAllocator *cavalry_allocator;

  gint stream_id;
  guint timeout_ms;
  gint wait_iav_sleep_us;
  guint query_canvas_stream_id_map;

  gchar canvas_stream_id_str[128];
  gchar force_idr_stream_id_str[128];

  GstClockTime first_mono_pts;

  gboolean give_clock;
  GstAmbaHwClock * provided_clock;
  GstClockTimeDiff latency[IAV_STREAM_MAX_NUM_ALL];
  GstClock *system_clock;

  amba_release_bits_t release_param[IAV_STREAM_MAX_NUM_ALL];
};

struct _GstAmbaVenccapClass
{
  GstPushSrcClass parent_class;
};

GType gst_amba_venccap_get_type(void);

G_END_DECLS

#endif /* __GST_AMBAVENCCAP_H__ */

