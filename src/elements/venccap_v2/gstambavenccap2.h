/*
 * gstambavenccap2.h
 *
 * History:
 *    8/27/2025 - [pxduan] created file
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
 * SECTION: element-amba_venccap2
 * @title: amba_venccap2
 *
 * amba_venccap2 can be used to capture encoded video bitstream from Amba device.
 * Supports fully automatic PTS calibration for audio-video synchronization.
 *
 */

#ifndef __GST_AMBAVENCCAP2_H__
#define __GST_AMBAVENCCAP2_H__

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

#define GST_TYPE_AMBAVENCCAP2 \
  (gst_amba_venccap2_get_type())
#define GST_AMBAVENCCAP2(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AMBAVENCCAP2,GstAmbaVenccap2))
#define GST_AMBAVENCCAP2_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AMBAVENCCAP2,GstAmbaVenccap2Class))
#define GST_IS_AMBAVENCCAP2(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AMBAVENCCAP2))
#define GST_IS_AMBAVENCCAP2_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AMBAVENCCAP2))

typedef struct {
  unsigned int is_read;
  iav_ctx_t * iav_ctx;
  amba_dsp_release_bitstream_t release_bs;
} amba_release_bitstream_t;

typedef struct _GstAmbaVenccap2 GstAmbaVenccap2;
typedef struct _GstAmbaVenccap2Class GstAmbaVenccap2Class;

/* Definition of structure storing data for this element. */
struct _GstAmbaVenccap2
{
  GstPushSrc pushsrc;

  guint print_info; // for debug purpose

  /*< private >*/
  iav_ctx_t * iav_ctx;

  video_bs_state_t bs_states;

  gboolean sync;

  // clock related
  GstClockTime gst_clok_time;
  guint is_clock_setup;
  guint is_clock_started;
  clock_ctx_t * clock;

  //get encoding configure information if needed
  gchar *enc_info;

  gint fps_n;
  gint fps_d;
  gint fps;
  gint stream_type;
  gint dump_num;

  guchar alloc_mem;
  guchar is_eos_com;
  guint16 enc_dummy_latency;

  GstAllocator *cavalry_allocator;

  gint stream_id;
  guint timeout_ms;
  gint wait_iav_sleep_us;

  GstClockTime first_mono_pts;
/* ts_offset used by basesrc, this is the offset add to the values of gst_times for
  * pseudo live sources. Set at first IDR: basesrc_ts_offset = running_time - cur_pts. */
  GstClockTimeDiff basesrc_ts_offset;
  gboolean first_idr_seen;  /* TRUE after we have set basesrc_ts_offset at first IDR */

  gboolean give_clock;
  gboolean use_system_clock;
  GstAmbaHwClock * provided_clock;
  /* GstReferenceTimestampMeta: cached caps (built once from outfreq) */
  GstCaps *hw_pts_ref_caps;
  GstClockTime duration;
  GstClock *system_clock;
  gboolean clock_sync_ready;  // Flag to ensure delayed base_time check completes before create runs

  guint idle_source_id;       // Idle source id for delayed_base_time_check, 0 if not active
  GMutex idle_mutex;          // Mutex to protect idle source operations

  amba_release_bitstream_t release_param;

  guint last_encoded_frame_num;
  guint cur_slice_tile_num;
  gint64 last_pts;

  iav_flush_frame_type flush_type;  // Flush type, default: IAV_FLUSH_FORCE_IDR_WITH_PTS
  gboolean stop_enc;   // Stop encoding when pipeline stops, default: FALSE
};

struct _GstAmbaVenccap2Class
{
  GstPushSrcClass parent_class;
};

GType gst_amba_venccap2_get_type(void);

/* Smart PTS calibration functions */
void gst_amba_venccap2_enable_pts_calibration(GstAmbaVenccap2 *thiz, gboolean enable);
void gst_amba_venccap2_enable_auto_calculation(GstAmbaVenccap2 *thiz, gboolean enable);
void gst_amba_venccap2_enable_auto_audio_detection(GstAmbaVenccap2 *thiz, gboolean enable);
GstClockTime gst_amba_venccap2_get_calibrated_pts(GstAmbaVenccap2 *thiz, guint64 hw_pts);

G_END_DECLS

#endif /* __GST_AMBAVENCCAP_H__ */

