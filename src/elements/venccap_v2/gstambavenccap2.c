/*
 * gstambavenccap2.c
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
 * SECTION:element-amba_venccap2
 * @title: amba_venccap2
 *
 * prepare on EVK
 * // initialize
 * init.sh --imx274_mipi
 * test_aaa_service -a &
 * // set vout
 * test_encode --hdmi 1080p --resource-cfg x.lua
 * // start encoding
 * test_encode -A -H 1080p -b 0 -e
 * // stop encoding
 * test_encode -A -s
 *
 * With IAV_STRM_INDIV_BSB mode, this element reads encoded video bit-stream from Ambarella platform.
 *
 * ## Example pipelines, single channel for h264
 * |[
 * gst-launch-1.0 -v -e amba_venccap2 stream-id=1 ! queue ! h264parse ! mp4mux ! filesink location=h264.mp4
 * ]|
 *  Read h264 encoded bit-stream and recorded into mp4.
 *
 * ## Example pipelines, single channel for h265
 * |[
 * gst-launch-1.0 -v -e amba_venccap2 stream-id=0 ! queue ! h265parse ! mp4mux ! filesink location=h265.mp4
 * ]|
 *  Read h265 encoded bit-stream and recorded into mp4.
 *
 * ## Example pipelines, single channel for h264 + opus
 * |[
 * gst-launch-1.0 -v -e malsasrc ! queue ! opusenc ! mp4mux name=mux ! filesink location=h264_opus.mp4 -e amba_venccap2 stream-id=1 ! queue ! h264parse ! mux.
 * ]|
 *  Read h264 encoded bit-stream, plus with opus audio, recorded into mp4.
 *
 * ## Example pipelines, single channel for h265 + opus
 * |[
 * gst-launch-1.0 -v -e malsasrc ! queue ! opusenc ! mp4mux name=mux ! filesink location=h265_opus.mp4 -e amba_venccap2 stream-id=0 ! queue ! h265parse ! mux.
 * ]|
 *  Read h265 encoded bit-stream, plus with opus audio, recorded into mp4.
 *
 * ## Example pipelines, single channel for h264, rtp
 * |[
 * gst-launch-1.0 -v -e amba_venccap2 stream-id=1 ! queue ! h264parse ! rtph264pay pt=96 name=pay0
 * ]|
 *  Read h264 encoded bit-stream and do rtp streaming
 *
 */

#include <stdio.h>
#include <string.h>

#include "common_err_code_c.h"

#include "bitstream_state.h"

#include "internal.h"
#include "debug_log.h"

#include "buffer_utils.h"

#include "gst_amba_cavalry_allocator.h"

#include "gstambavenccap2.h"

/* ClockSync args */
// if sync, would drop some gops (pts < 0) at starting
#define DEFAULT_SYNC_WITH_AUDIO         TRUE
//vin num 2->0x3
#define DEFAULT_MAX_VIN_ID              0x0
#define DEFAULT_PROVIDE_CLOCK           TRUE
#define DEFAULT_ALLOC_MEM               (1)
/* alloc_mem: 0 = wrap encoder buffer; 1 = system mem copy; 2 = Cavalry mfd (fd-backed) */
#define AMBAVENCCAP2_ALLOC_MEM_WRAP      (0)
#define AMBAVENCCAP2_ALLOC_MEM_SYSTEM    (1)
#define AMBAVENCCAP2_ALLOC_MEM_CAVALRY   (2)
#define DEFAULT_CLOCK_TYPE              FALSE  // FALSE = hardware clock, TRUE = system clock
#define DEFAULT_TIMEOUT_MS              (0)//(0xffffffff)
#define DEFAULT_FORCE_IDR_DELAY_FRAME   (2)
//#define D_DEBUG_CLOCK_INFO              TRUE

GST_DEBUG_CATEGORY_STATIC (gst_amba_venccap2_debug);
#define GST_CAT_DEFAULT gst_amba_venccap2_debug

enum
{
  PROP_0,
  PROP_ENCODE_SET,
  PROP_ALLOC_MEM,
  PROP_SYNC_WITH_AUDIO,
  PROP_PROVIDE_CLOCK,
  PROP_CLOCK_TYPE,
  PROP_STREAM_IDX,
  PROP_TIMEOUT_MS,
  PROP_WAIT_IAV_SLEEP_US,
  PROP_QUERY_CANVAS,
  PROP_FORCE_IDR,
  PROP_STOP_ENC,
};

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264, "
    "stream-format=(string) { byte-stream, avc, avc3 }, "
    "framerate = " GST_VIDEO_FPS_RANGE ", "
    "alignment=(string) { nal };"
    "video/x-h265, "
    "stream-format=(string) { byte-stream, hvc1, hev1 }, "
    "framerate = " GST_VIDEO_FPS_RANGE ", "
    "alignment=(string) { nal };"
    "image/jpeg, "
    "framerate = (fraction) [ 0/1, MAX ], "
    "parsed=TRUE")
  );

#define gst_amba_venccap2_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstAmbaVenccap2, gst_amba_venccap2, GST_TYPE_PUSH_SRC,
    GST_DEBUG_CATEGORY_INIT (gst_amba_venccap2_debug, "ambavenccap2", 0,
        "ambavenccap2"));

#define AMBAVENCCAP2_DEFAULT_ENC_FORMAT "stream_id:0"

static GstFlowReturn gst_ambavenccap2_create (GstPushSrc * psrc,
    GstBuffer ** outbuf);
static gboolean gst_ambavenccap2_start (GstBaseSrc * bsrc);
static gboolean gst_ambavenccap2_stop (GstBaseSrc * bsrc);
static void gst_ambavenccap2_get_times (GstBaseSrc * src, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end);

static void gst_ambavenccap2_finalize (GObject * gobject);
static void gst_ambavenccap2_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_ambavenccap2_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstClock *
gst_ambavenccap2_provide_clock (GstElement * element);

static GstStateChangeReturn
gst_ambavenccap2_change_state (GstElement * element, GstStateChange transition);

static gboolean
gst_ambavenccap2_send_event (GstElement * element, GstEvent * event);
static gboolean
gst_ambavenccap2_event (GstBaseSrc * bsrc, GstEvent * event);

/* GstReferenceTimestampMeta: raw read_bitstream PTS ticks in meta->pts; tick rate in caps from
 * GstAmbaHwClock::outfreq (same basis as get_cur_clock_dummy / hwtimer). */
static void
gst_ambavenccap2_buffer_add_hw_pts_ref_meta (GstAmbaVenccap2 * thiz, GstBuffer * buf,
    unsigned long hw_pts_raw)
{
  GstCaps *ref;
  guint64 rate_hz;
  GstClockTime raw_pts;
  if (!thiz->hw_pts_ref_caps) {
    GstStructure *s;
    GstCaps *new_caps;

    if (thiz->provided_clock && thiz->provided_clock->outfreq > 0) {
      rate_hz = (guint64) thiz->provided_clock->outfreq;
    } else {
      rate_hz = (guint64) gst_amba_hwtimer_get_outfreq ();
    }

    /* Avoid gst_caps_from_string: (string)raw-ticks must be quoted in caps text
     * because '-' is otherwise parsed as an operator; build caps programmatically. */
    s = gst_structure_new ("timestamp/x-ambarella-iav-arm-pts",
        "tick-rate-hz", G_TYPE_UINT64, rate_hz,
        "pts-value", G_TYPE_STRING, "raw-ticks",
        NULL);
    if (!s) {
      GST_WARNING_OBJECT (thiz, "Failed to create reference timestamp caps structure");
      return;
    }
    new_caps = gst_caps_new_full (s, NULL);
    if (!new_caps) {
      gst_structure_free (s);
      GST_WARNING_OBJECT (thiz, "Failed to create reference timestamp caps");
      return;
    }
    thiz->hw_pts_ref_caps = new_caps;
  }

  ref = thiz->hw_pts_ref_caps;
  raw_pts = (GstClockTime) hw_pts_raw;
  gst_buffer_add_reference_timestamp_meta (buf, ref, raw_pts, GST_CLOCK_TIME_NONE);
  GST_DEBUG_OBJECT (thiz, "Added reference timestamp meta: raw_pts=%" G_GUINT64_FORMAT,
      (guint64) raw_pts);
}

static void
gst_ambavenccap2_get_times (GstBaseSrc * src, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end)
{
  GstAmbaVenccap2 * filter = GST_AMBAVENCCAP2 (src);
  if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer)) {
    *start = GST_BUFFER_TIMESTAMP (buffer);
  } else {
    *start = gst_clock_get_time (GST_ELEMENT_CLOCK (src)) -
        GST_ELEMENT_CAST (src)->base_time;
  }
  if (GST_BUFFER_DURATION_IS_VALID (buffer)) {
    *end = *start + GST_BUFFER_DURATION (buffer);
  } else if (filter->duration > 0) {
    *end = *start + filter->duration;
  } else {
    *start = -1;
    *end = -1;
  }
}

static void
gst_amba_venccap2_class_init (GstAmbaVenccap2Class * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstPushSrcClass *gstpushsrc_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);
  gstbasesrc_class = GST_BASE_SRC_CLASS (klass);
  gstpushsrc_class = GST_PUSH_SRC_CLASS (klass);
  gstelement_class->change_state = gst_ambavenccap2_change_state;
  gstelement_class->provide_clock = GST_DEBUG_FUNCPTR (gst_ambavenccap2_provide_clock);
  gstelement_class->send_event = GST_DEBUG_FUNCPTR (gst_ambavenccap2_send_event);

  // Set element flags to ensure proper state management
  gst_element_class_set_static_metadata (gstelement_class,
      "Ambarella Video Encoder Capture v2",
      "Source/Video",
      "Reads encoded video bitstreams from Amba device",
      "pxduan <pxduan@ambarella.com>");

  gobject_class->finalize = gst_ambavenccap2_finalize;
  gobject_class->set_property = gst_ambavenccap2_set_property;
  gobject_class->get_property = gst_ambavenccap2_get_property;

  g_object_class_install_property (gobject_class, PROP_ENCODE_SET,
      g_param_spec_string ("enc", "Enc",
          "encode setting", AMBAVENCCAP2_DEFAULT_ENC_FORMAT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ALLOC_MEM,
      g_param_spec_uchar ("alloc_mem", "AllocMem",
          "Output buffer memory: 0 = wrap encoder buffer (no copy, not recommended); "
          "1 = system memory copy (default); "
          "2 = Cavalry mfd buffer (fd-backed GstMemory)",
          0, 2, DEFAULT_ALLOC_MEM, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_SYNC_WITH_AUDIO,
      g_param_spec_boolean ("sync", "Synchronize",
      "Synchronize to pipeline clock", DEFAULT_SYNC_WITH_AUDIO,
      G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_PROVIDE_CLOCK,
      g_param_spec_boolean ("provide-clock", "Provide Clock",
          "Provide a clock to be used as the global pipeline clock",
          DEFAULT_PROVIDE_CLOCK, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_CLOCK_TYPE,
      g_param_spec_boolean ("use-system-clock", "Use System Clock",
          "Use system clock instead of hardware clock for timing",
          DEFAULT_CLOCK_TYPE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_STREAM_IDX,
      g_param_spec_int ("stream-id", "Stream Id", "provide captured stream id",
          -1, IAV_STREAM_MAX_NUM_ALL,
          -1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS) );

  g_object_class_install_property (gobject_class, PROP_TIMEOUT_MS,
      g_param_spec_uint ("timeout_ms", "Timeout MS", "Timeout in microsecond for reading bitstream",
          0, 0xffffffff,
          0xffffffff, G_PARAM_READWRITE) );
  g_object_class_install_property (gobject_class, PROP_WAIT_IAV_SLEEP_US,
      g_param_spec_int ("wait_iav_sleep_us", "Wait IAV Ready Sleep US", "Sleep time (us) waiting for IAV enter encoding state",
          -1, G_MAXINT,
          -1, G_PARAM_READWRITE) );

  g_object_class_install_property (gobject_class, PROP_STOP_ENC,
      g_param_spec_boolean ("stop-enc", "Stop Encoding",
          "Stop encoding when pipeline stops",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&srctemplate));

  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_ambavenccap2_start);
  gstbasesrc_class->get_times = GST_DEBUG_FUNCPTR (gst_ambavenccap2_get_times);
  gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_ambavenccap2_stop);
  gstbasesrc_class->event = GST_DEBUG_FUNCPTR (gst_ambavenccap2_event);
  gstpushsrc_class->create = GST_DEBUG_FUNCPTR (gst_ambavenccap2_create);
}

static void
gst_amba_venccap2_init (GstAmbaVenccap2 * thiz)
{
  // Initialize all fields to safe defaults first
  thiz->iav_ctx = NULL;
  thiz->clock = NULL;
  thiz->provided_clock = NULL;
  thiz->system_clock = NULL;
  thiz->enc_info = NULL;
  thiz->is_clock_setup = 0;
  thiz->is_clock_started = 0;
  thiz->alloc_mem = DEFAULT_ALLOC_MEM;
  thiz->cavalry_allocator = NULL;
  thiz->sync = DEFAULT_SYNC_WITH_AUDIO;
  thiz->give_clock = DEFAULT_PROVIDE_CLOCK;
  thiz->use_system_clock = DEFAULT_CLOCK_TYPE;
  thiz->stream_id = -1;
  thiz->timeout_ms = DEFAULT_TIMEOUT_MS;
  thiz->wait_iav_sleep_us = -1;
  thiz->dump_num = 1;
  thiz->first_mono_pts = 0;
  thiz->basesrc_ts_offset = 0;
  thiz->first_idr_seen = FALSE;
  thiz->clock_sync_ready = FALSE;  // Initially not ready
  thiz->idle_source_id = 0;        // No idle source initially
  thiz->flush_type = IAV_FLUSH_FORCE_IDR_WITH_PTS;  // Default flush type
  thiz->stop_enc = FALSE;  // Default: don't stop encoding when pipeline stops
  thiz->hw_pts_ref_caps = NULL;
  g_mutex_init (&thiz->idle_mutex);

  // iav context
  thiz->iav_ctx = acquire_iav_ctx (1);
  if (!thiz->iav_ctx) {
    GST_ERROR("acquire_iav_ctx failed\n");
    return;
  }

  thiz->provided_clock = gst_amba_hw_clock_obtain();
  if (!thiz->provided_clock) {
    GST_ERROR("Failed to obtain hardware clock");
    thiz->give_clock = FALSE;
  }

  thiz->clock = create_clock (NULL,
      thiz->provided_clock->outfreq / 10000, GST_SECOND / 10000);
  if (!thiz->clock) {
    DPRINT_ERROR("clock create failed\n");
    thiz->is_clock_setup = 0;
  } else {
    thiz->is_clock_setup = 1;
  }

  gst_amba_cavalry_allocator_init_once ();
  thiz->cavalry_allocator = gst_amba_cavalry_allocator_get ();

  gst_base_src_set_live (GST_BASE_SRC (thiz), TRUE);
  gst_base_src_set_do_timestamp (GST_BASE_SRC (thiz), TRUE);
  gst_base_src_set_format (GST_BASE_SRC (thiz), GST_FORMAT_TIME);

  // Set PROVIDE_CLOCK flag based on give_clock property
  if (thiz->give_clock) {
    GST_OBJECT_FLAG_SET (thiz, GST_ELEMENT_FLAG_PROVIDE_CLOCK);
    GST_DEBUG_OBJECT(thiz, "Clock providing enabled");
  } else {
    GST_DEBUG_OBJECT(thiz, "Clock providing disabled");
  }
  GST_OBJECT_FLAG_SET (thiz, GST_ELEMENT_FLAG_REQUIRE_CLOCK);
}


static void
gst_ambavenccap2_finalize (GObject * gobject)
{
  GstAmbaVenccap2 *thiz = GST_AMBAVENCCAP2 (gobject);

  // Cancel any pending idle source to prevent use-after-free
  g_mutex_lock (&thiz->idle_mutex);
  if (thiz->idle_source_id > 0) {
    g_source_remove (thiz->idle_source_id);
    thiz->idle_source_id = 0;
    GST_DEBUG_OBJECT (thiz, "Cancelled pending idle source in finalize");
  }
  g_mutex_unlock (&thiz->idle_mutex);
  g_mutex_clear (&thiz->idle_mutex);

  // Clean up object references
  if (thiz->provided_clock) {
    gst_object_unref (thiz->provided_clock);
    thiz->provided_clock = NULL;
  }

  if (thiz->cavalry_allocator) {
    gst_object_unref (thiz->cavalry_allocator);
    thiz->cavalry_allocator = NULL;
  }

  if (thiz->system_clock) {
    gst_object_unref (thiz->system_clock);
    thiz->system_clock = NULL;
  }

  if (thiz->enc_info) {
    g_free (thiz->enc_info);
    thiz->enc_info = NULL;
  }

  if (thiz->iav_ctx) {
    release_iav_ctx (1);
    thiz->iav_ctx = NULL;
  }

  if (thiz->clock) {
    destroy_clock(thiz->clock);
    thiz->clock = NULL;
  }

  if (thiz->hw_pts_ref_caps) {
    gst_caps_unref (thiz->hw_pts_ref_caps);
    thiz->hw_pts_ref_caps = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (gobject);
}

static GstClock *
gst_ambavenccap2_provide_clock (GstElement * element)
{
  GstAmbaVenccap2 * src = GST_AMBAVENCCAP2 (element);
  GstClock *clock = NULL;

  GST_OBJECT_LOCK (src);

  if (!GST_OBJECT_FLAG_IS_SET (src, GST_ELEMENT_FLAG_PROVIDE_CLOCK)) {
    GST_DEBUG_OBJECT (src, "clock provide disabled");
    GST_OBJECT_UNLOCK (src);
    return NULL;
  }

  if (src->use_system_clock) {
    // Ensure system_clock exists
    if (src->system_clock == NULL) {
      src->system_clock = gst_system_clock_obtain();
    }

    if (src->system_clock && GST_IS_CLOCK(src->system_clock)) {
      clock = gst_object_ref(src->system_clock);
    } else {
      GST_DEBUG_OBJECT (src, "system_clock is NULL or invalid");
    }
  } else {
    if (src->provided_clock && GST_IS_CLOCK(src->provided_clock)) {
      clock = gst_object_ref(src->provided_clock);
    } else {
      GST_DEBUG_OBJECT (src, "provided_clock is NULL or invalid");
    }
  }

  GST_OBJECT_UNLOCK (src);
  return clock;
}

static void
gst_ambavenccap2_set_provide_clock (GstAmbaVenccap2 * src, gboolean provide)
{
  GstMessage *clock_message = NULL;
  GST_OBJECT_LOCK (src);

  if (provide) {
    GST_OBJECT_FLAG_SET (src, GST_ELEMENT_FLAG_PROVIDE_CLOCK);

    if (src->use_system_clock) {
      if (src->system_clock == NULL) {
        src->system_clock = gst_system_clock_obtain();
      }
      if (src->system_clock && GST_IS_CLOCK(src->system_clock)) {
        clock_message =
            gst_message_new_clock_provide (GST_OBJECT_CAST (src),
                src->system_clock, TRUE);
      }
    } else {
      if (src->provided_clock && GST_IS_CLOCK(src->provided_clock)) {
        clock_message =
            gst_message_new_clock_provide (GST_OBJECT_CAST (src),
                GST_CLOCK_CAST (src->provided_clock), TRUE);
      }
    }
  } else {
    GST_OBJECT_FLAG_UNSET (src, GST_ELEMENT_FLAG_PROVIDE_CLOCK);

    if (src->use_system_clock) {
      if (src->system_clock && GST_IS_CLOCK(src->system_clock)) {
        clock_message =
            gst_message_new_clock_lost (GST_OBJECT_CAST (src),
                src->system_clock);
      }
    } else {
      if (src->provided_clock && GST_IS_CLOCK(src->provided_clock)) {
        clock_message =
            gst_message_new_clock_lost (GST_OBJECT_CAST (src),
                GST_CLOCK_CAST (src->provided_clock));
      }
    }
  }

  GST_OBJECT_UNLOCK (src);

  if (clock_message) {
    gst_element_post_message (GST_ELEMENT_CAST (src), clock_message);
    // Don't unref here - gst_element_post_message takes ownership
  }
}

static gboolean
gst_ambavenccap2_get_provide_clock (GstAmbaVenccap2 * src)
{
  gboolean result;

  GST_OBJECT_LOCK (src);
  result = GST_OBJECT_FLAG_IS_SET (src, GST_ELEMENT_FLAG_PROVIDE_CLOCK);
  GST_OBJECT_UNLOCK (src);

  return result;
}

static void
gst_ambavenccap2_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAmbaVenccap2 * thiz = GST_AMBAVENCCAP2 (object);
  iav_ctx_t * iav_ctx = thiz->iav_ctx;
  int ret = 0;

  switch (prop_id) {
    case PROP_ENCODE_SET: {
      const gchar *enc_str = g_value_get_string (value);
      if (!enc_str || strlen(enc_str) == 0) {
        GST_ERROR_OBJECT (thiz, "Empty encode setting string");
        return;
      }

      enc_config_t config;
      ret = parse_enc(iav_ctx->iav_fd, enc_str, &config);
      if (ret) {
        DPRINT_ERROR("parse_enc (%s) failed\n", enc_str);
        return;
      }
      if (!iav_ctx->iav_fd_opened) {
        DPRINT_ERROR("iav not opened\n");
        return;
      }
      ret = update_enc (iav_ctx->iav_fd, &config);
      if (ret) {
        DPRINT_ERROR("update encode setting (%s) failed\n", enc_str);
        return;
      }
      get_frame_rate(iav_ctx->iav_fd, &config, thiz->stream_id);
      thiz->fps = config.stream_fps[thiz->stream_id];
      thiz->fps_d = config.framerate_factor[thiz->stream_id][1];
      thiz->fps_n = thiz->fps * thiz->fps_d;//config.framerate_factor[i][0];
      if (thiz->fps_n > 0 && thiz->fps_d >= 0) {
        thiz->duration = gst_util_uint64_scale_int (GST_SECOND,
            thiz->fps_d, thiz->fps_n);
      }
    }break;
    case PROP_ALLOC_MEM:
      thiz->alloc_mem = g_value_get_uchar (value);
      break;
    case PROP_SYNC_WITH_AUDIO:
      thiz->sync = g_value_get_boolean (value);
      break;
    case PROP_PROVIDE_CLOCK:
      thiz->give_clock = g_value_get_boolean (value);
      gst_ambavenccap2_set_provide_clock (thiz, thiz->give_clock);
      break;
    case PROP_CLOCK_TYPE:
      thiz->use_system_clock = g_value_get_boolean (value);
      GST_DEBUG_OBJECT(thiz, "Clock type set to: %s", thiz->use_system_clock ? "system clock" : "hardware clock");
      break;
    case PROP_STREAM_IDX: {
      gint stream_id = g_value_get_int (value);
      if (stream_id < -1 || stream_id >= IAV_STREAM_MAX_NUM_ALL) {
        GST_WARNING_OBJECT (thiz, "Invalid stream-id: %d, must be in range [-1, %d)",
                           stream_id, IAV_STREAM_MAX_NUM_ALL);
        return;
      }
      thiz->stream_id = stream_id;
    } break;
    case PROP_TIMEOUT_MS:
      thiz->timeout_ms = g_value_get_uint (value);
      break;
    case PROP_WAIT_IAV_SLEEP_US:
      thiz->wait_iav_sleep_us = g_value_get_int (value);
      break;
    case PROP_STOP_ENC:
      thiz->stop_enc = g_value_get_boolean (value);
      break;
    default: {
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }break;
  }

}

static void
gst_ambavenccap2_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAmbaVenccap2 *thiz = GST_AMBAVENCCAP2 (object);
  iav_ctx_t * iav_ctx = thiz->iav_ctx;

  switch (prop_id) {
    case PROP_ENCODE_SET: {
      if (!iav_ctx->iav_fd_opened) {
        DPRINT_ERROR("iav not opened\n");
        return;
      }
      if (thiz->enc_info) {
        g_free(thiz->enc_info);
        thiz->enc_info = NULL;
      }
      enc_config_t config;
      thiz->enc_info = get_enc_info_string (iav_ctx->iav_fd, &config);
      if (thiz->enc_info == NULL) {
        DPRINT_ERROR("convert encoding configure information to string failed\n");
        return;
      }

      g_value_set_string (value, thiz->enc_info);

    }
    break;
    case PROP_ALLOC_MEM:
      g_value_set_uchar (value, thiz->alloc_mem);
      break;
    case PROP_SYNC_WITH_AUDIO:
      g_value_set_boolean (value, thiz->sync);
      break;
    case PROP_PROVIDE_CLOCK:
      g_value_set_boolean (value, gst_ambavenccap2_get_provide_clock (thiz));
      break;
    case PROP_CLOCK_TYPE:
      g_value_set_boolean (value, thiz->use_system_clock);
      break;
    case PROP_STREAM_IDX:
      g_value_set_int (value, thiz->stream_id);
      break;
    case PROP_TIMEOUT_MS:
      g_value_set_uint (value, thiz->timeout_ms);
      break;
    case PROP_WAIT_IAV_SLEEP_US:
      g_value_set_int (value, thiz->wait_iav_sleep_us);
      break;
    case PROP_STOP_ENC:
      g_value_set_boolean (value, thiz->stop_enc);
      break;
    default: {
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }break;
  }
}

static GstBuffer *
gst_ambavenccap2_alloc_via_cavalry (GstAmbaVenccap2 * thiz, const guint8 * p_cur, gsize cur_size)
{
  GstBuffer *buf;
  GstMemory *mem;
  GstMapInfo map;
  GstAllocationParams params;

  if G_UNLIKELY (cur_size == 0 || cur_size > G_MAXUINT) {
    GST_ERROR_OBJECT (thiz, "invalid bitstream size %" G_GSIZE_FORMAT, cur_size);
    return NULL;
  }

  if G_UNLIKELY (!thiz->cavalry_allocator) {
    GST_ERROR_OBJECT (thiz, "Cavalry allocator unavailable (alloc_mem=2)");
    return NULL;
  }

  gst_allocation_params_init (&params);
  mem = gst_allocator_alloc (thiz->cavalry_allocator, cur_size, &params);
  if (!mem) {
    GST_ERROR_OBJECT (thiz, "cavalry gst_allocator_alloc failed for %" G_GSIZE_FORMAT " bytes",
        cur_size);
    return NULL;
  }

  if (!gst_memory_map (mem, &map, GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (thiz, "cavalry gst_memory_map (WRITE) failed");
    gst_memory_unref (mem);
    return NULL;
  }
  memcpy (map.data, p_cur, cur_size);
  gst_memory_unmap (mem, &map);

  buf = gst_buffer_new ();
  if (!buf) {
    gst_memory_unref (mem);
    return NULL;
  }
  gst_buffer_append_memory (buf, mem);
  return buf;
}

static void __release_bitstream (gpointer param)
{
  amba_release_bitstream_t *ctx = (amba_release_bitstream_t *) param;
  if (ctx->is_read) {
    ctx->iav_ctx->iav_al.f_release_bitstream(ctx->iav_ctx->iav_fd, &ctx->release_bs);
    ctx->is_read = 0;
  }
}

static void
gst_ambavenccap2_start_clock(GstAmbaVenccap2 *thiz, GstClock *clock, GstClockTime base_time)
{
  if (!thiz->clock || thiz->is_clock_started) {
    return;
  }

  if (clock && base_time != GST_CLOCK_TIME_NONE) {
    if (thiz->use_system_clock) {
      GstClockTimeDiff offset = gst_amba_hw_clock_get_time(thiz->provided_clock) - gst_clock_get_time(clock);
      start_clock_v2(thiz->clock, (base_time + offset));
      DPRINT_NOTICE("system_clock: video base time: %ld (90k) -> %ld (pipeline base time) + %ld = %ld (ns), timestamp : %" GST_TIME_FORMAT
          "\n", thiz->clock->base_src_time,
          base_time, offset, (base_time + offset),
          GST_TIME_ARGS(base_time + offset));
      thiz->first_mono_pts = thiz->clock->base_src_time;
    } else {
      start_clock_v2(thiz->clock, base_time);
      DPRINT_NOTICE("hardware_clock: video base time: %ld (90k) -> %ld (pipeline base time, ns), timestamp : %" GST_TIME_FORMAT
          "cur_time: %ld\n", thiz->clock->base_src_time,
          base_time,
          GST_TIME_ARGS(base_time),
          gst_clock_get_time(clock));

      thiz->first_mono_pts = thiz->clock->base_src_time;
    }
  } else {
    thiz->clock->base_src_time = thiz->first_mono_pts;
    DPRINT_NOTICE("use hardware time as base src time, video base time: %ld (90k) -> %ld (ns), timestamp : %" GST_TIME_FORMAT
        "\n", thiz->clock->base_src_time,
        gst_util_uint64_scale(thiz->first_mono_pts, GST_SECOND, thiz->provided_clock->outfreq),
        GST_TIME_ARGS(gst_util_uint64_scale(thiz->first_mono_pts, GST_SECOND, thiz->provided_clock->outfreq)));
  }

  thiz->is_clock_started = 1;
}


/* Delayed base_time check function */
static gboolean
delayed_base_time_check(GstAmbaVenccap2 *src)
{
  GstElement *element = GST_ELEMENT_CAST(src);
  gboolean should_continue = FALSE;

  // Check if pipeline is fully in PLAYING state
  GstState current_state, pending_state;
  gst_element_get_state(element, &current_state, &pending_state, 0);

  GST_INFO_OBJECT(element, "Delayed check - current: %s, pending: %s",
      gst_element_state_get_name(current_state),
      gst_element_state_get_name(pending_state));

  // Only proceed if pipeline is fully in PLAYING state
  if (current_state == GST_STATE_PLAYING && pending_state == GST_STATE_VOID_PENDING) {
    GST_INFO_OBJECT(element, "Pipeline is fully in PLAYING state, proceeding with base_time synchronization");

    if (src->sync) {
      GST_INFO_OBJECT(element, "=== Acquiring first_mono_pts in PLAYING state ===");
      src->first_mono_pts = amba_hwtimer_get_raw_time(src->provided_clock);
      if (src->first_mono_pts == GST_CLOCK_TIME_NONE) {
        GST_WARNING_OBJECT(src, "Failed to get hardware timer time, using fallback");
        src->first_mono_pts = 0;
      }
      GST_INFO_OBJECT(element, "first_mono_pts acquired: %ld", src->first_mono_pts);


      if (src->iav_ctx->iav_al.f_flush_frame_desc(src->iav_ctx->iav_fd, src->stream_id,
          src->flush_type, src->first_mono_pts) < 0) {
        GST_WARNING_OBJECT(src, "Failed to flush frame descriptor, continuing");
      }

      gst_ambavenccap2_start_clock(src, NULL, GST_CLOCK_TIME_NONE);
    }

    // Signal that clock synchronization is complete
    src->clock_sync_ready = TRUE;
    GST_INFO_OBJECT(element, "Clock synchronization complete, ready for create()");
    should_continue = FALSE;
  } else {
    GST_INFO_OBJECT(element, "Pipeline not yet fully in PLAYING state, will retry later");
    // Return TRUE to keep the idle callback active for retry
    should_continue = TRUE;
  }

  // Clear the source id when callback is done (returning FALSE)
  if (!should_continue) {
    g_mutex_lock (&src->idle_mutex);
    src->idle_source_id = 0;
    g_mutex_unlock (&src->idle_mutex);
  }

  return should_continue;
}

static GstFlowReturn
gst_ambavenccap2_create (GstPushSrc * psrc,
    GstBuffer ** outbuf)
{
  GstAmbaVenccap2 * thiz = GST_AMBAVENCCAP2 (psrc);
  GstFlowReturn flow_ret = GST_FLOW_OK;
  iav_ctx_t * iav_ctx = thiz->iav_ctx;
  iav_al_t * iav_al = &iav_ctx->iav_al;
  unsigned int ret = 0;

  guchar *p_cur;
  gssize cur_size;

  guint stream_idx = 0;

  video_bs_state_t * cur_bs_state;

  amba_dsp_read_bitstream_t read_bs = {0};

  GstBuffer * p_out_buf = NULL;
  GstClockTimeDiff cur_pts = GST_CLOCK_TIME_NONE;

  while (1) {
    if (thiz->sync && !thiz->clock_sync_ready) {
      continue;
    }
    memset(&read_bs, 0x0, sizeof(read_bs));

    read_bs.stream_idx = thiz->stream_id;
    read_bs.timeout_ms = thiz->timeout_ms;
    ret = iav_ctx->iav_al.f_read_bitstream(iav_ctx->iav_fd, &read_bs);

    if (ret == COM_ECODE_BAD_STATE) {
      GST_ERROR_OBJECT (thiz, "read_bitstream failed, ret 0x%08x\n", ret);
      flow_ret = GST_FLOW_ERROR;
      break;
    } else if (COM_ECODE_TRY_AGAIN == ret) {
      goto IF_EOS_COM;
    }

    // stream index
    stream_idx = read_bs.stream_idx;
    thiz->release_param.is_read = 1;
    thiz->release_param.iav_ctx = thiz->iav_ctx;
    thiz->release_param.release_bs.stream_idx = read_bs.stream_idx;
    thiz->release_param.release_bs.framedesc = read_bs.framedesc;

    // state and src pad for this stream id
    cur_bs_state = &thiz->bs_states;
    cur_bs_state->stream_id = stream_idx;

    if (COM_ECODE_COMPLETE == ret) {
      DPRINT_NOTICE("eos comes\n");
      iav_al->f_release_bitstream(iav_ctx->iav_fd, &thiz->release_param.release_bs);
      thiz->release_param.is_read = 0;
      // reset this stream's setting
      cur_bs_state->codec_format = StreamFormat_Invalid;
      cur_bs_state->key_frame_comes = 0;
      goto IF_EOS_COM;
    } else if (COM_ECODE_OK != ret) {
      DPRINT_WARNING("ret 0x%08x here?\n", ret);
      break;
    }

    // check stream format
    if (StreamFormat_Invalid == cur_bs_state->codec_format) {
      cur_bs_state->codec_format = read_bs.stream_format;
    } else {
      if (read_bs.stream_format != cur_bs_state->codec_format) {
        DPRINT_ERROR("stream_format[%d] not match: 0x%02x, 0x%02x\n",
          stream_idx, read_bs.stream_format, cur_bs_state->codec_format);
        iav_al->f_release_bitstream(iav_ctx->iav_fd, &thiz->release_param.release_bs);
        thiz->release_param.is_read = 0;
        flow_ret = GST_FLOW_ERROR;
        break;
      }
    }

    // bitstream
    p_cur = iav_ctx->map_bsb.base + read_bs.offset;
    cur_size = read_bs.size;

    // check avc, hevc, and mjpeg
    if (StreamFormat_H264 == read_bs.stream_format) {
      // wait key frame
      if (!cur_bs_state->key_frame_comes) {
        if (EPredefinedPictureType_IDR != read_bs.hint_frame_type) {
          //DPRINT_NOTICE("h264 stream [%d] wait key frame\n", stream_idx);
          iav_al->f_release_bitstream(iav_ctx->iav_fd, &thiz->release_param.release_bs);
          thiz->release_param.is_read = 0;
          continue;
        }
        if (thiz->is_clock_started && (read_bs.pts < thiz->clock->base_src_time)) {
          iav_al->f_release_bitstream(iav_ctx->iav_fd, &thiz->release_param.release_bs);
          thiz->release_param.is_read = 0;
          continue;

          //DPRINT_NOTICE("stream[%d]: clock base_src_time updated from %ld (90k) to %ld (90k)\n", stream_idx, thiz->clock->base_src_time, read_bs.pts);
          //thiz->clock->base_src_time = read_bs.pts;
        }
        DPRINT_NOTICE("h264 stream [%d] key frame comes\n", stream_idx);
        cur_bs_state->key_frame_comes = 1;
        cur_bs_state->slice_num_per_frame = read_bs.slice_num;
        cur_bs_state->tile_num_per_frame = read_bs.tile_num;
        cur_bs_state->slice_tile_num = read_bs.slice_num * read_bs.tile_num;
        if (cur_bs_state->slice_tile_num == 1) {
          thiz->last_encoded_frame_num = read_bs.encoded_frame_num - 1;
        } else {
          thiz->last_encoded_frame_num = read_bs.encoded_frame_num;
        }
      }
      thiz->cur_slice_tile_num++;

      if (!thiz->is_clock_started) {
        thiz->clock->base_src_time = read_bs.pts;
        thiz->is_clock_started = 1;
        DPRINT_NOTICE("clock started, base_src_time: %ld (90k)\n", thiz->clock->base_src_time);
      }

      if ((read_bs.encoded_frame_num == thiz->last_encoded_frame_num + 1)
          || (read_bs.encoded_frame_num < thiz->last_encoded_frame_num)) {
        if (thiz->cur_slice_tile_num != cur_bs_state->slice_tile_num) {
          DPRINT_WARNING("stream[%d]: encoded frame %d done, queried slice/tile num %d is not equal to %d\n",
              stream_idx, read_bs.encoded_frame_num, thiz->cur_slice_tile_num, cur_bs_state->slice_tile_num);
        }
        thiz->cur_slice_tile_num = 0;
      } else if (read_bs.encoded_frame_num != thiz->last_encoded_frame_num) {
        DPRINT_WARNING("stream[%d]: encoded frame num should be continuous, last: %d cur: %d\n",
            stream_idx, thiz->last_encoded_frame_num, read_bs.encoded_frame_num);
        // Reset slice/tile counter when frame is skipped to avoid cascading errors
        thiz->cur_slice_tile_num = 0;
      }

      /* Calculate calibrated PTS */
      if (read_bs.pts == cur_bs_state->last_pts) {
        if (read_bs.slice_id <= cur_bs_state->last_slice_id) {
          DPRINT_WARNING("PTS same between cur frame %d slice %d and last frame %d slice %d, PTS (%lu)+1\n",
              read_bs.encoded_frame_num, read_bs.slice_id,
              thiz->last_encoded_frame_num, cur_bs_state->last_slice_id,
              read_bs.pts);
          cur_pts = thiz->last_pts + 1;
        } else {
          cur_pts = thiz->last_pts;
        }
      } else {
        cur_pts = get_cur_clock_dummy(thiz->clock, read_bs.pts);
        if (cur_pts < 0) {
          GST_ERROR_OBJECT(thiz, "stream[%d] frame[%d] mono pts[%ld (90k)]: Failed to get current clock time", stream_idx, read_bs.encoded_frame_num, read_bs.pts);
          flow_ret = GST_FLOW_ERROR;
          break;
        }
      }

      cur_bs_state->last_slice_id = read_bs.slice_id;

      /* Debug: Verify pipeline clock and current timestamp */
#ifdef D_DEBUG_CLOCK_INFO
      GstClock *pipeline_clock = gst_element_get_clock(GST_ELEMENT_CAST(thiz));
      if (pipeline_clock) {
        GstClockTime current_time = gst_clock_get_time(pipeline_clock);
        GstClockTime internal_time = gst_clock_get_internal_time(pipeline_clock);
        const gchar *clock_name = GST_OBJECT_NAME(pipeline_clock);

        GST_DEBUG_OBJECT(thiz, "=== Clock Debug Info ===");
        GST_DEBUG_OBJECT(thiz, "Pipeline clock: %s", clock_name ? clock_name : "unnamed");
        GST_DEBUG_OBJECT(thiz, "Current time: %" GST_TIME_FORMAT, GST_TIME_ARGS(current_time));
        GST_DEBUG_OBJECT(thiz, "Internal time: %" GST_TIME_FORMAT, GST_TIME_ARGS(internal_time));
        GST_DEBUG_OBJECT(thiz, "Video PTS (get_cur_clock_dummy): %" GST_TIME_FORMAT, GST_TIME_ARGS(cur_pts));
        GST_DEBUG_OBJECT(thiz, "Hardware PTS (90kHz): %ld", read_bs.pts);
        GST_DEBUG_OBJECT(thiz, "=== End Clock Debug ===");
      } else {
        GST_WARNING_OBJECT(thiz, "No pipeline clock available");
      }
#endif
      if (read_bs.hint_is_keyframe && !thiz->first_idr_seen) {
        GstClockTime running_time_at_first_idr = gst_element_get_current_running_time (GST_ELEMENT_CAST (thiz));
        if (GST_CLOCK_TIME_IS_VALID (running_time_at_first_idr)) {
          thiz->basesrc_ts_offset = (GstClockTimeDiff) (running_time_at_first_idr - cur_pts);
          thiz->first_idr_seen = TRUE;
          GST_INFO_OBJECT (thiz, "first IDR: cur_pts = %" GST_TIME_FORMAT ", running_time = %" GST_TIME_FORMAT ", basesrc_ts_offset = %" GST_TIME_FORMAT,
              GST_TIME_ARGS (cur_pts), GST_TIME_ARGS (running_time_at_first_idr), GST_TIME_ARGS ((GstClockTime) thiz->basesrc_ts_offset));
        }
      }

      if (thiz->dump_num > 0) {
        if (read_bs.hint_is_keyframe) {
          DPRINT_NOTICE("first IDR: stream %d, frame: %d, monopts: %ld (90k), cur_pts: %ld (ns), interval: %ld (ms)\n",
              stream_idx, read_bs.encoded_frame_num, read_bs.pts, cur_pts,
              GST_TIME_AS_MSECONDS (cur_pts));
          thiz->dump_num--;
        }
      }

      if (thiz->alloc_mem == AMBAVENCCAP2_ALLOC_MEM_SYSTEM) {
        p_out_buf = gst_buffer_new_allocate (NULL, cur_size, NULL);
        if (!p_out_buf) {
          GST_ERROR_OBJECT (thiz, "Failed to allocate buffer of size %ld\n", cur_size);
          flow_ret = GST_FLOW_ERROR;
          break;
        }
        ret = gst_buffer_fill (p_out_buf, 0, p_cur, cur_size);
        if (ret != cur_size) {
          GST_ERROR_OBJECT (thiz, "Failed to fill buffer, expected %ld, got %d\n", cur_size, ret);
          gst_buffer_unref (p_out_buf);
          flow_ret = GST_FLOW_ERROR;
          break;
        }
      } else if (thiz->alloc_mem == AMBAVENCCAP2_ALLOC_MEM_CAVALRY) {
        p_out_buf = gst_ambavenccap2_alloc_via_cavalry (thiz, p_cur, (gsize) cur_size);
        if (!p_out_buf) {
          flow_ret = GST_FLOW_ERROR;
          break;
        }
      } else {
        p_out_buf = gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_READONLY, p_cur, cur_size, 0,
            cur_size, &thiz->release_param, __release_bitstream);
      }

      //GST_BUFFER_DTS (p_out_buf)
      GST_BUFFER_PTS (p_out_buf) = cur_pts;
      GST_BUFFER_DURATION (p_out_buf) = thiz->duration;
      gst_ambavenccap2_buffer_add_hw_pts_ref_meta (thiz, p_out_buf, read_bs.pts);

      *outbuf = p_out_buf;

      GST_DEBUG_OBJECT(thiz, "h264_stream_idx: %d, frame: %d, PTS: %ld, duration: %ld\n",
          stream_idx, read_bs.encoded_frame_num, GST_BUFFER_PTS (p_out_buf),
          GST_BUFFER_DURATION (p_out_buf));
      cur_bs_state->last_pts = read_bs.pts;
      thiz->last_pts = cur_pts;
      thiz->last_encoded_frame_num = read_bs.encoded_frame_num;
    } else if (StreamFormat_H265 == read_bs.stream_format) {
      // wait key frame
      if (!cur_bs_state->key_frame_comes) {
        if ((EPredefinedPictureType_IDR != read_bs.hint_frame_type)
          || (read_bs.slice_id != 0) || (read_bs.tile_id != 0)) {
          //DPRINT_NOTICE("h265 stream [%d] wait key frame\n", stream_idx);

          iav_al->f_release_bitstream(iav_ctx->iav_fd, &thiz->release_param.release_bs);
          thiz->release_param.is_read = 0;
          continue;
        }
        if (thiz->is_clock_started && (read_bs.pts < thiz->clock->base_src_time)) {
          iav_al->f_release_bitstream(iav_ctx->iav_fd, &thiz->release_param.release_bs);
          thiz->release_param.is_read = 0;
          continue;
          //DPRINT_NOTICE("stream[%d]: clock base_src_time updated from %ld (90k) to %ld (90k)\n", stream_idx, thiz->clock->base_src_time, read_bs.pts);
          //thiz->clock->base_src_time = read_bs.pts;
        }
        DPRINT_NOTICE("h265 stream [%d] key frame comes\n", stream_idx);
        cur_bs_state->key_frame_comes = 1;
        cur_bs_state->slice_num_per_frame = read_bs.slice_num;
        cur_bs_state->tile_num_per_frame = read_bs.tile_num;
        cur_bs_state->slice_tile_num = read_bs.slice_num * read_bs.tile_num;
        if (cur_bs_state->slice_tile_num == 1) {
          thiz->last_encoded_frame_num = read_bs.encoded_frame_num - 1;
        } else {
          thiz->last_encoded_frame_num = read_bs.encoded_frame_num;
        }
      }

      thiz->cur_slice_tile_num++;

      if (!thiz->is_clock_started) {
        thiz->clock->base_src_time = read_bs.pts;
        thiz->is_clock_started = 1;
        DPRINT_NOTICE("clock started, base_src_time: %ld (90k)\n", thiz->clock->base_src_time);
      }

      if ((read_bs.encoded_frame_num == thiz->last_encoded_frame_num + 1)
          || (read_bs.encoded_frame_num < thiz->last_encoded_frame_num)) {
        if (thiz->cur_slice_tile_num != cur_bs_state->slice_tile_num) {
          DPRINT_WARNING("stream[%d]: encoded frame %d done, queried slice/tile num %d is not equal to %d\n",
              stream_idx, read_bs.encoded_frame_num, thiz->cur_slice_tile_num, cur_bs_state->slice_tile_num);
        }
        thiz->cur_slice_tile_num = 0;
      } else if (read_bs.encoded_frame_num != thiz->last_encoded_frame_num) {
        DPRINT_WARNING("stream[%d]: encoded frame num should be continuous, last: %d cur: %d\n",
            stream_idx, thiz->last_encoded_frame_num, read_bs.encoded_frame_num);
        // Reset slice/tile counter when frame is skipped to avoid cascading errors
        thiz->cur_slice_tile_num = 0;
      }

      /* Calculate calibrated PTS */
      if (read_bs.pts == cur_bs_state->last_pts) {
        if (((read_bs.slice_id == cur_bs_state->last_slice_id)
            && (read_bs.tile_id <= cur_bs_state->last_tile_id))
            || (read_bs.slice_id < cur_bs_state->last_slice_id)) {
          DPRINT_WARNING("PTS same between cur frame %d slice %d tile: %d and last frame %d slice %d tile: %d, PTS(%lu)+1\n",
              read_bs.encoded_frame_num, read_bs.slice_id, read_bs.tile_id,
              thiz->last_encoded_frame_num, cur_bs_state->last_slice_id, cur_bs_state->last_tile_id,
              read_bs.pts);
          cur_pts = thiz->last_pts + 1;
        } else {
          cur_pts = thiz->last_pts;
        }
      } else {
        cur_pts = get_cur_clock_dummy(thiz->clock, read_bs.pts);
        if (cur_pts < 0) {
          GST_ERROR_OBJECT(thiz, "stream[%d] frame[%d] mono pts[%ld (90k)]: Failed to get current clock time", stream_idx, read_bs.encoded_frame_num, read_bs.pts);
          flow_ret = GST_FLOW_ERROR;
          break;
        }
      }

      cur_bs_state->last_slice_id = read_bs.slice_id;
      cur_bs_state->last_tile_id = read_bs.tile_id;

      /* Debug: Verify pipeline clock and current timestamp */
#ifdef D_DEBUG_CLOCK_INFO
      GstClock *pipeline_clock = gst_element_get_clock(GST_ELEMENT_CAST(thiz));
      if (pipeline_clock) {
        GstClockTime current_time = gst_clock_get_time(pipeline_clock);
        GstClockTime internal_time = gst_clock_get_internal_time(pipeline_clock);
        const gchar *clock_name = GST_OBJECT_NAME(pipeline_clock);

        GST_DEBUG_OBJECT(thiz, "=== Clock Debug Info ===");
        GST_DEBUG_OBJECT(thiz, "Pipeline clock: %s", clock_name ? clock_name : "unnamed");
        GST_DEBUG_OBJECT(thiz, "Current time: %" GST_TIME_FORMAT, GST_TIME_ARGS(current_time));
        GST_DEBUG_OBJECT(thiz, "Internal time: %" GST_TIME_FORMAT, GST_TIME_ARGS(internal_time));
        GST_DEBUG_OBJECT(thiz, "Video PTS (get_cur_clock_dummy): %" GST_TIME_FORMAT, GST_TIME_ARGS(cur_pts));
        GST_DEBUG_OBJECT(thiz, "Hardware PTS (90kHz): %ld", read_bs.pts);
        GST_DEBUG_OBJECT(thiz, "=== End Clock Debug ===");
      } else {
        GST_WARNING_OBJECT(thiz, "No pipeline clock available");
      }
#endif
      if (read_bs.slice_id == 0 && read_bs.tile_id == 0 && read_bs.hint_is_keyframe && !thiz->first_idr_seen) {
        GstClockTime running_time_at_first_idr = gst_element_get_current_running_time (GST_ELEMENT_CAST (thiz));
        if (GST_CLOCK_TIME_IS_VALID (running_time_at_first_idr)) {
          thiz->basesrc_ts_offset = (GstClockTimeDiff) (running_time_at_first_idr - cur_pts);
          thiz->first_idr_seen = TRUE;
          GST_INFO_OBJECT (thiz, "first IDR: cur_pts = %" GST_TIME_FORMAT ", running_time = %" GST_TIME_FORMAT ", basesrc_ts_offset = %" GST_TIME_FORMAT,
              GST_TIME_ARGS (cur_pts), GST_TIME_ARGS (running_time_at_first_idr), GST_TIME_ARGS ((GstClockTime) thiz->basesrc_ts_offset));
        }
      }

      if (thiz->dump_num > 0) {
        if (read_bs.slice_id == 0 && read_bs.tile_id == 0) {
          if (read_bs.hint_is_keyframe) {
            DPRINT_NOTICE("first IDR: stream %d, frame: %d, monopts: %ld (90k), cur_pts: %ld (ns), interval: %ld (ms)\n",
                stream_idx, read_bs.encoded_frame_num, read_bs.pts, cur_pts,
                GST_TIME_AS_MSECONDS (cur_pts));
            thiz->dump_num--;
          }
        }
      }

      if (thiz->alloc_mem == AMBAVENCCAP2_ALLOC_MEM_SYSTEM) {
        p_out_buf = gst_buffer_new_allocate (NULL, cur_size, NULL);
        if (!p_out_buf) {
          GST_ERROR_OBJECT (thiz, "Failed to allocate buffer of size %ld\n", cur_size);
          flow_ret = GST_FLOW_ERROR;
          break;
        }
        ret = gst_buffer_fill (p_out_buf, 0, p_cur, cur_size);
        if (ret != cur_size) {
          GST_ERROR_OBJECT (thiz, "Failed to fill buffer, expected %ld, got %d\n", cur_size, ret);
          gst_buffer_unref (p_out_buf);
          flow_ret = GST_FLOW_ERROR;
          break;
        }
      } else if (thiz->alloc_mem == AMBAVENCCAP2_ALLOC_MEM_CAVALRY) {
        p_out_buf = gst_ambavenccap2_alloc_via_cavalry (thiz, p_cur, (gsize) cur_size);
        if (!p_out_buf) {
          flow_ret = GST_FLOW_ERROR;
          break;
        }
      } else {
        p_out_buf = gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_READONLY, p_cur, cur_size, 0,
            cur_size, &thiz->release_param, __release_bitstream);
      }

      GST_BUFFER_PTS (p_out_buf) = cur_pts;

      GST_BUFFER_DURATION (p_out_buf) = thiz->duration;
      gst_ambavenccap2_buffer_add_hw_pts_ref_meta (thiz, p_out_buf, read_bs.pts);
      if ((read_bs.tile_id == read_bs.tile_num - 1) &&
          (read_bs.slice_id == read_bs.slice_num - 1)) {
        gst_buffer_set_flags(p_out_buf, GST_BUFFER_FLAG_MARKER);
      }

      *outbuf = p_out_buf;

      GST_DEBUG_OBJECT(thiz, "h265_stream_idx: %d, frame: %d, slice: %d, tile: %d, ARMPTS: %ld, PTS: %ld, duration: %ld\n",
          stream_idx, read_bs.encoded_frame_num, read_bs.slice_id, read_bs.tile_id,
          read_bs.pts, GST_BUFFER_PTS (p_out_buf),
          GST_BUFFER_DURATION (p_out_buf));
      cur_bs_state->last_pts = read_bs.pts;
      thiz->last_pts = cur_pts;
      thiz->last_encoded_frame_num = read_bs.encoded_frame_num;
    } else if (StreamFormat_JPEG == read_bs.stream_format) {

      if ((EJPEG_MarkerPrefix != p_cur[0]) || (EJPEG_SOI != p_cur[1]) || (EJPEG_MarkerPrefix != p_cur[2]) || (128 > cur_size)) {
        GST_WARNING_OBJECT(thiz, "not find mjpeg header %x%x%x, or invalid data size %ld on stream %d, skip.",
            p_cur[0], p_cur[1], p_cur[2], cur_size, stream_idx);
        iav_al->f_release_bitstream(iav_ctx->iav_fd, &thiz->release_param.release_bs);
        thiz->release_param.is_read = 0;
        continue;
      }

      if (!thiz->is_clock_started) {
        thiz->clock->base_src_time = read_bs.pts;
        thiz->is_clock_started = 1;
        DPRINT_NOTICE("clock started, base_src_time: %ld (90k)\n", thiz->clock->base_src_time);
      } else if (read_bs.pts < thiz->clock->base_src_time) {
        iav_al->f_release_bitstream(iav_ctx->iav_fd, &thiz->release_param.release_bs);
        thiz->release_param.is_read = 0;
        continue;
      }
      cur_bs_state->key_frame_comes = 1;
      cur_bs_state->slice_num_per_frame = read_bs.slice_num;
      cur_bs_state->tile_num_per_frame = read_bs.tile_num;

      /* Calculate calibrated PTS */
      cur_pts = get_cur_clock_dummy(thiz->clock, read_bs.pts);

      /* Debug: Verify pipeline clock and current timestamp */
#ifdef D_DEBUG_CLOCK_INFO
      GstClock *pipeline_clock = gst_element_get_clock(GST_ELEMENT_CAST(thiz));
      if (pipeline_clock) {
        GstClockTime current_time = gst_clock_get_time(pipeline_clock);
        GstClockTime internal_time = gst_clock_get_internal_time(pipeline_clock);
        const gchar *clock_name = GST_OBJECT_NAME(pipeline_clock);

        GST_DEBUG_OBJECT(thiz, "=== Clock Debug Info ===");
        GST_DEBUG_OBJECT(thiz, "Pipeline clock: %s", clock_name ? clock_name : "unnamed");
        GST_DEBUG_OBJECT(thiz, "Current time: %" GST_TIME_FORMAT, GST_TIME_ARGS(current_time));
        GST_DEBUG_OBJECT(thiz, "Internal time: %" GST_TIME_FORMAT, GST_TIME_ARGS(internal_time));
        GST_DEBUG_OBJECT(thiz, "Video PTS (get_cur_clock_dummy): %" GST_TIME_FORMAT, GST_TIME_ARGS(cur_pts));
        GST_DEBUG_OBJECT(thiz, "Hardware PTS (90kHz): %ld", read_bs.pts);
        GST_DEBUG_OBJECT(thiz, "=== End Clock Debug ===");
      } else {
        GST_WARNING_OBJECT(thiz, "No pipeline clock available");
      }
#endif
      if (thiz->dump_num > 0) {
        DPRINT_NOTICE("first jpeg: stream %d, frame: %d, monopts: %ld (90k), cur_pts: %ld (ns), interval: %ld (ms)\n",
            stream_idx, read_bs.encoded_frame_num, read_bs.pts, cur_pts,
            GST_TIME_AS_MSECONDS (cur_pts));
        thiz->dump_num--;
      }

      if (thiz->alloc_mem == AMBAVENCCAP2_ALLOC_MEM_SYSTEM) {
        p_out_buf = gst_buffer_new_allocate (NULL, cur_size, NULL);
        if (!p_out_buf) {
          GST_ERROR_OBJECT (thiz, "Failed to allocate buffer of size %ld\n", cur_size);
          flow_ret = GST_FLOW_ERROR;
          break;
        }
        ret = gst_buffer_fill (p_out_buf, 0, p_cur, cur_size);
        if (ret != cur_size) {
          GST_ERROR_OBJECT (thiz, "Failed to fill buffer, expected %ld, got %d\n", cur_size, ret);
          gst_buffer_unref (p_out_buf);
          flow_ret = GST_FLOW_ERROR;
          break;
        }
      } else if (thiz->alloc_mem == AMBAVENCCAP2_ALLOC_MEM_CAVALRY) {
        p_out_buf = gst_ambavenccap2_alloc_via_cavalry (thiz, p_cur, (gsize) cur_size);
        if (!p_out_buf) {
          flow_ret = GST_FLOW_ERROR;
          break;
        }
      } else {
        p_out_buf = gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_READONLY, p_cur, cur_size, 0,
            cur_size, &thiz->release_param, __release_bitstream);
      }

      GST_BUFFER_PTS (p_out_buf) = cur_pts;

      GST_BUFFER_DURATION (p_out_buf) = thiz->duration;
      gst_ambavenccap2_buffer_add_hw_pts_ref_meta (thiz, p_out_buf, read_bs.pts);

      *outbuf = p_out_buf;

      GST_DEBUG_OBJECT(thiz, "mjpeg_stream_idx: %d, frame: %d, PTS: %ld, duration: %ld\n",
          stream_idx, read_bs.encoded_frame_num, GST_BUFFER_PTS (p_out_buf),
          GST_BUFFER_DURATION (p_out_buf));
      cur_bs_state->last_pts = read_bs.pts;
      thiz->last_encoded_frame_num = read_bs.encoded_frame_num;
    } else {
      GST_ERROR_OBJECT(thiz, "not supported stream(%d) format %d, only support h264/h265/mjpeg.",
          stream_idx, read_bs.stream_format);
      flow_ret = GST_FLOW_ERROR;
      break;
    }

    flow_ret = GST_FLOW_OK;
    break;
IF_EOS_COM:
    if (thiz->is_eos_com) {
      flow_ret = GST_FLOW_EOS;
      break;
    }
    if (iav_al->f_get_iav_state (iav_ctx->iav_fd) == IAV_STATE_IDLE) {
      flow_ret = GST_FLOW_EOS;
      break;
    }
  }

  if ((thiz->alloc_mem)) {
    iav_al->f_release_bitstream(iav_ctx->iav_fd, &thiz->release_param.release_bs);
    thiz->release_param.is_read = 0;
  }

  return flow_ret;
}


static gboolean
gst_ambavenccap2_start (GstBaseSrc * bsrc)
{
  GstAmbaVenccap2 * thiz = GST_AMBAVENCCAP2 (bsrc);
  iav_ctx_t * iav_ctx = thiz->iav_ctx;
  enc_config_t config;
  int ret = 0;
  unsigned int canvas_id = 0;

  if (!iav_ctx) {
    GST_ERROR_OBJECT(thiz, "iav_ctx is NULL");
    return FALSE;
  }

  if (!iav_ctx->iav_fd_opened) {
    GST_ERROR_OBJECT(thiz, "iav not opened");
    return FALSE;
  }

  if (thiz->wait_iav_sleep_us >= 0) {
    GST_DEBUG_OBJECT(thiz, "Waiting for IAV encoding state, sleep_us=%d", thiz->wait_iav_sleep_us);
    while (iav_ctx->iav_al.f_get_iav_state(iav_ctx->iav_fd) != IAV_STATE_ENCODING) {
      g_usleep (thiz->wait_iav_sleep_us);
    };
    GST_DEBUG_OBJECT(thiz, "IAV encoding state reached");
  } else {
    int iav_state = iav_ctx->iav_al.f_get_iav_state(iav_ctx->iav_fd);
    GST_DEBUG_OBJECT(thiz, "IAV state: %d, expected: %d", iav_state, IAV_STATE_ENCODING);
    if (iav_state != IAV_STATE_ENCODING) {
      GST_ERROR_OBJECT(thiz, "iav state is not encoding (current: %d)", iav_state);
      return FALSE;
    }
  }

  canvas_id = thiz->iav_ctx->iav_al.f_get_enc_src_canvas_id(thiz->iav_ctx->iav_fd, thiz->stream_id);
  thiz->enc_dummy_latency = thiz->iav_ctx->iav_al.f_get_enc_dummy_latency(thiz->iav_ctx->iav_fd, canvas_id);

  memset(&config, 0x0, sizeof(enc_config_t));
  ret = get_enc_info_config (iav_ctx->iav_fd, &config, 1 << thiz->stream_id, DEFAULT_MAX_VIN_ID);
  if (ret < 0) {
    GST_ERROR ("get encoding information failed\n");
    return FALSE;
  }

  thiz->stream_type = config.encode_fmt[thiz->stream_id].type;

  if (thiz->stream_type == IAV_STREAM_TYPE_H265 ||
      thiz->stream_type == IAV_STREAM_TYPE_H264) {
    if (thiz->enc_dummy_latency == 0) {
      thiz->flush_type = IAV_FLUSH_FORCE_IDR_ENABLE;
      if (thiz->sync) {
        GST_WARNING ("enc_dummy_latency == 0, flush frame: force idr without pts,"
          "may lead to out of sync for audio/video");
      }
    }
  } else {
    thiz->flush_type = IAV_FLUSH_FORCE_IDR_DISABLE;
  }
  if (thiz->fps == 0) {
    thiz->fps = config.stream_fps[thiz->stream_id];
    thiz->fps_d = config.framerate_factor[thiz->stream_id][1];
    thiz->fps_n = thiz->fps * thiz->fps_d;
    if (thiz->fps_n > 0 && thiz->fps_d >= 0) {
      thiz->duration = gst_util_uint64_scale_int (GST_SECOND,
          thiz->fps_d, thiz->fps_n);
    }
  }

  return TRUE;
}

static gboolean
gst_ambavenccap2_stop (GstBaseSrc * bsrc)
{
  GstAmbaVenccap2 * thiz = GST_AMBAVENCCAP2 (bsrc);

  // Cancel any pending idle source to prevent use-after-free
  g_mutex_lock (&thiz->idle_mutex);
  if (thiz->idle_source_id > 0) {
    g_source_remove (thiz->idle_source_id);
    thiz->idle_source_id = 0;
    GST_DEBUG_OBJECT(thiz, "Cancelled pending idle source in stop");
  }
  g_mutex_unlock (&thiz->idle_mutex);

  if (thiz->stop_enc) {
    if (stop_encode (thiz->iav_ctx->iav_fd, 1 << thiz->stream_id) < 0) {
      GST_ERROR_OBJECT(thiz, "Failed to stop encoding for stream %d", thiz->stream_id);
    }
  }

  // Clean up any resources if needed
  if (thiz->sync && thiz->provided_clock) {
    thiz->is_clock_started = 0;
  }

  // Reset clock sync ready flag
  thiz->clock_sync_ready = FALSE;
  return TRUE;
}

/* unlock and unlock_stop removed */

static GstStateChangeReturn
gst_ambavenccap2_change_state (GstElement * element, GstStateChange transition)
{
  GstAmbaVenccap2 * src = GST_AMBAVENCCAP2 (element);
  GstStateChangeReturn ret= GST_STATE_CHANGE_SUCCESS;

  GST_DEBUG_OBJECT(element, "change_state called with transition %d", transition);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      // NOTE: Do NOT call gst_amba_hw_clock_reset() on the shared singleton clock!
      // The shared hardware clock should be read-only from elements' perspective.
      // Each element manages its own timing state via clock_ctx_t and first_mono_pts.
      // Resetting the shared clock would affect all other pipelines using it.

      // Reset element-local clock state instead
      if (src->clock) {
        src->clock->base_src_time = 0;
      }
      src->is_clock_started = 0;
      src->first_mono_pts = 0;

      // Reset clock sync ready flag
      src->clock_sync_ready = FALSE;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING: {
      // Reset flag before starting delayed sync
      src->clock_sync_ready = FALSE;
      if (src->give_clock) {
        if (src->use_system_clock) {
          if (!src->system_clock) {
            src->system_clock = gst_system_clock_obtain();
            if (!src->system_clock) {
              GST_ERROR_OBJECT(element, "Failed to obtain system clock");
              return GST_STATE_CHANGE_FAILURE;
            }
          }

          gst_element_post_message (element, gst_message_new_clock_provide (GST_OBJECT_CAST (element),
              src->system_clock, TRUE));
        } else {
          if (src->provided_clock) {
            GST_INFO_OBJECT(element, "Providing hardware clock as master clock");
            gst_element_post_message (element, gst_message_new_clock_provide (GST_OBJECT_CAST (element),
                GST_CLOCK_CAST (src->provided_clock), TRUE));
          } else {
            GST_DEBUG_OBJECT(element, "Not providing clock: provided_clock=%p", src->provided_clock);
          }
        }
      }

      // Save idle source id to allow cancellation in stop/finalize
      g_mutex_lock (&src->idle_mutex);
      src->idle_source_id = g_idle_add((GSourceFunc)delayed_base_time_check, src);
      GST_DEBUG_OBJECT(element, "Added idle source with id %u", src->idle_source_id);
      g_mutex_unlock (&src->idle_mutex);

    } break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      if (src->give_clock) {
        if (src->use_system_clock && GST_IS_CLOCK(src->system_clock)) {
          gst_element_post_message (element,
              gst_message_new_clock_lost (GST_OBJECT_CAST (element),
                  src->system_clock));
        } else {
          if (GST_IS_CLOCK(src->provided_clock)) {
            gst_element_post_message (element,
                gst_message_new_clock_lost (GST_OBJECT_CAST (element),
                    GST_CLOCK_CAST (src->provided_clock)));
          }
        }
      }
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}


/* Timer data for delayed force IDR */
typedef struct {
  GstAmbaVenccap2 *src;
  unsigned int stream_id;
} ForceIdrTimerData;

/* Shared: trigger force IDR and free timer_data; clock_id may be NULL (for GLib timer path). */
static void
force_idr_trigger_and_cleanup (ForceIdrTimerData *timer_data, GstClockID clock_id)
{
  GstAmbaVenccap2 *src = timer_data->src;
  unsigned int stream_format;
  enc_force_idr_t force_idr = {0};

  force_idr.enc_index = timer_data->stream_id;
  stream_format = (src->stream_type == IAV_STREAM_TYPE_H264) ?
      StreamFormat_H264 : StreamFormat_H265;
  force_idr.stream_type = (stream_format == StreamFormat_H264) ?
      IAV_STREAM_TYPE_H264 : IAV_STREAM_TYPE_H265;

  if (enc_force_idr (src->iav_ctx->iav_fd, &force_idr) < 0)
    GST_ERROR_OBJECT (src, "Failed to force IDR for stream %u", timer_data->stream_id);

  if (clock_id)
    gst_clock_id_unref (clock_id);
  g_object_unref (timer_data->src);
  g_free (timer_data);
}

static gboolean
force_idr_clock_callback (GstClock *clock, GstClockTime time, GstClockID id, gpointer user_data)
{
  (void) clock;
  (void) time;
  ForceIdrTimerData *timer_data = (ForceIdrTimerData *)user_data;
  GST_INFO_OBJECT (timer_data->src, "Clock-triggered force IDR for stream %d at time %" GST_TIME_FORMAT,
      timer_data->stream_id, GST_TIME_ARGS (time));
  force_idr_trigger_and_cleanup (timer_data, id);
  return FALSE;
}

static gboolean
force_idr_timer_callback (gpointer user_data)
{
  ForceIdrTimerData *timer_data = (ForceIdrTimerData *)user_data;
  GST_INFO_OBJECT (timer_data->src, "Timer-triggered force IDR for stream %d", timer_data->stream_id);
  force_idr_trigger_and_cleanup (timer_data, NULL);
  return FALSE;
}

/* Handle ForceKeyUnit event: compute diff, subtract estimated delay; if result > 0 schedule
 * timer, else trigger force IDR immediately. Invalid running_time also triggers immediately. */
static gboolean
gst_ambavenccap2_handle_force_key_unit (GstAmbaVenccap2 * src, GstEvent * event)
{
  GstClockTime running_time;
  gboolean all_headers;
  guint count;
  unsigned int stream_format;
  enc_force_idr_t force_idr = {0};

  if (!gst_video_event_parse_upstream_force_key_unit (event, &running_time,
          &all_headers, &count)) {
    GST_WARNING_OBJECT (src, "Failed to parse upstream ForceKeyUnit event");
    return FALSE;
  }

  GST_INFO_OBJECT (src, "Received upstream ForceKeyUnit event: running_time=%"
      GST_TIME_FORMAT ", all_headers=%d, count=%u",
      GST_TIME_ARGS (running_time), all_headers, count);

  if (src->stream_type != IAV_STREAM_TYPE_H264 &&
      src->stream_type != IAV_STREAM_TYPE_H265) {
    GST_DEBUG_OBJECT (src, "ForceKeyUnit event ignored for stream type %d "
        "(only H264/H265 supported)", src->stream_type);
    return FALSE;
  }

  stream_format = (src->stream_type == IAV_STREAM_TYPE_H264) ?
      StreamFormat_H264 : StreamFormat_H265;
  force_idr.enc_index = src->stream_id;
  force_idr.stream_type = (stream_format == StreamFormat_H264) ?
      IAV_STREAM_TYPE_H264 : IAV_STREAM_TYPE_H265;

 /* No valid running_time from event → trigger immediately */
  if (!GST_CLOCK_TIME_IS_VALID (running_time)) {
    GST_INFO_OBJECT (src, "ForceKeyUnit: no valid running_time, triggering immediately");
    goto trigger_immediately;
  }

/* Convert event running_time to pipeline running_time for trigger.
 * With do-timestamp: base_src set DTS, so muxer's event running_time = DTS = pipeline_running_time + basesrc_ts_offset
 *   => target = event_running_time - basesrc_ts_offset - encoder_delay.
 * Without do-timestamp: base_src doesn't set DTS, so muxer's event running_time = PTS = pipeline_running_time(for live src)
 *   => target = event_running_time - encoder_delay only.
 */
{
  GstClockTime now = gst_element_get_current_running_time (GST_ELEMENT (src));
  /* Calculate encoder delay (estimated 2 frames) */
  GstClockTime encoder_delay_ns = 0;
  if (src->fps_n > 0 && src->fps_d > 0) {
    encoder_delay_ns = DEFAULT_FORCE_IDR_DELAY_FRAME * (GstClockTime) GST_SECOND * src->fps_d / src->fps_n;
  }
  gboolean do_timestamp = gst_base_src_get_do_timestamp (GST_BASE_SRC (src));
  GstClockTime dts_offset = do_timestamp ? src->basesrc_ts_offset : 0;
  GstClockTime target_running_time = running_time - dts_offset - encoder_delay_ns;
  /* Time to wait so that we trigger when running_time reaches target_running_time */
  GstClockTimeDiff time_to_wait_ns = (GstClockTimeDiff) (target_running_time - now);

  GST_INFO_OBJECT (src, "ForceKeyUnit: event_rt=%" GST_TIME_FORMAT " - dts_offset=%" GST_TIME_FORMAT " (do_ts=%d) - enc_delay=%" GST_TIME_FORMAT
      " => target_rt=%" GST_TIME_FORMAT "; now=%" GST_TIME_FORMAT " => wait=%" GST_TIME_FORMAT,
      GST_TIME_ARGS (running_time), GST_TIME_ARGS (dts_offset), do_timestamp, GST_TIME_ARGS (encoder_delay_ns),
      GST_TIME_ARGS (target_running_time), GST_TIME_ARGS (now), GST_TIME_ARGS ((GstClockTime) time_to_wait_ns));

   if (time_to_wait_ns <= 0) {
     GST_INFO_OBJECT (src, "ForceKeyUnit: target already reached or in past (target=%" GST_TIME_FORMAT ", now=%" GST_TIME_FORMAT "), trigger now",
         GST_TIME_ARGS (target_running_time), GST_TIME_ARGS (now));
     goto trigger_immediately;
   }

   /* Schedule: trigger when pipeline clock has advanced by time_to_wait_ns (≈ target_running_time) */
   GstClock *pipeline_clock = gst_element_get_clock (GST_ELEMENT (src));

   if (pipeline_clock) {
     GstClockTime now_clock = gst_clock_get_time (pipeline_clock);
     GstClockTime trigger_clock_time = now_clock + (GstClockTime) time_to_wait_ns;
     GstClockID clock_id = gst_clock_new_single_shot_id (pipeline_clock, trigger_clock_time);

     if (clock_id) {
       ForceIdrTimerData *timer_data = g_new (ForceIdrTimerData, 1);
       timer_data->src = g_object_ref (src);
       timer_data->stream_id = src->stream_id;

       GST_INFO_OBJECT (src, "ForceKeyUnit: scheduled at target_running_time=%" GST_TIME_FORMAT
           " in %" GST_TIME_FORMAT " (encoder_delay=%" GST_TIME_FORMAT ")",
           GST_TIME_ARGS (target_running_time), GST_TIME_ARGS ((GstClockTime) time_to_wait_ns), GST_TIME_ARGS (encoder_delay_ns));

       gst_clock_id_wait_async (clock_id, (GstClockCallback)force_idr_clock_callback, timer_data, NULL);
       gst_object_unref (pipeline_clock);
       return TRUE;
     }
     gst_object_unref (pipeline_clock);
   }

   /* Fallback: GLib timer when pipeline clock unavailable */
   guint64 time_to_wait_ms = MAX (1, ((guint64) time_to_wait_ns + 500000) / 1000000);
   ForceIdrTimerData *timer_data = g_new (ForceIdrTimerData, 1);
   timer_data->src = g_object_ref (src);
   timer_data->stream_id = src->stream_id;
   GST_INFO_OBJECT (src, "ForceKeyUnit: scheduled in %" G_GUINT64_FORMAT " ms (GLib fallback, target=%" GST_TIME_FORMAT ")",
       time_to_wait_ms, GST_TIME_ARGS (target_running_time));
   g_timeout_add (time_to_wait_ms, force_idr_timer_callback, timer_data);
   return TRUE;

  }

trigger_immediately:
  if (enc_force_idr (src->iav_ctx->iav_fd, &force_idr) < 0) {
    GST_WARNING_OBJECT (src, "Failed to force IDR for stream %d", src->stream_id);
    return FALSE;
  }
  GST_INFO_OBJECT (src, "Successfully triggered immediate force IDR for stream %d", src->stream_id);
  return TRUE;
}

/* GstBaseSrc::event - handles events on the source pad (upstream events) */
static gboolean
gst_ambavenccap2_event (GstBaseSrc * bsrc, GstEvent * event)
{
  GstAmbaVenccap2 * src = GST_AMBAVENCCAP2 (bsrc);
  gboolean ret = TRUE;

  GST_DEBUG_OBJECT (src, "GstBaseSrc::event called with event type: %s",
      gst_event_type_get_name (GST_EVENT_TYPE (event)));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CUSTOM_DOWNSTREAM:
    case GST_EVENT_CUSTOM_UPSTREAM:
      if (gst_video_event_is_force_key_unit (event)) {
        GST_INFO_OBJECT (src, "GstBaseSrc::event - Detected ForceKeyUnit event !");
        /* Only handle if element is started (stream_type is set in start()) */
        if (src->stream_type != 0) {
          ret = gst_ambavenccap2_handle_force_key_unit (src, event);
        } else {
          GST_WARNING_OBJECT (src, "Element not started yet, cannot handle ForceKeyUnit");
          ret = FALSE;
        }
      } else {
        ret = GST_BASE_SRC_CLASS (parent_class)->event (bsrc, event);
      }
      break;

    default:
      ret = GST_BASE_SRC_CLASS (parent_class)->event (bsrc, event);
      break;
  }

  return ret;
}

/* GstElement::send_event - handles events sent to the element */
static gboolean
gst_ambavenccap2_send_event (GstElement * element, GstEvent * event)
{
  GstAmbaVenccap2 * src = GST_AMBAVENCCAP2 (element);
  gboolean ret = TRUE;

  const GstStructure *s;
  const gchar *tstr;
  gchar *sstr;

  GST_OBJECT_LOCK (src);

  tstr = gst_event_type_get_name (GST_EVENT_TYPE (event));

  if ((s = gst_event_get_structure (event))) {
    sstr = gst_structure_to_string (s);
  } else {
    sstr = g_strdup ("");
  }
  GST_DEBUG_OBJECT (src, "GstElement::send_event called with event type: %s (%d), %s",
      tstr, GST_EVENT_TYPE (event), sstr);
  g_free (sstr);

  GST_OBJECT_UNLOCK (src);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      GST_OBJECT_LOCK (src);
      src->is_eos_com = 1;
      GST_OBJECT_UNLOCK (src);
      ret = GST_ELEMENT_CLASS (parent_class)->send_event (element, event);
      break;

    case GST_EVENT_CUSTOM_DOWNSTREAM:
    case GST_EVENT_CUSTOM_UPSTREAM:
      if (gst_video_event_is_force_key_unit (event)) {
        GST_INFO_OBJECT (src, "GstElement::send_event - Detected ForceKeyUnit event !");
        /* Only handle if element is started (stream_type is set in start()) */
        if (src->stream_type != 0) {
          ret = gst_ambavenccap2_handle_force_key_unit (src, event);
        } else {
          GST_WARNING_OBJECT (src, "Element not started yet, cannot handle ForceKeyUnit");
          ret = FALSE;
        }
        gst_event_unref (event);
      } else {
        ret = GST_ELEMENT_CLASS (parent_class)->send_event (element, event);
      }
      break;

    default:
      ret = GST_ELEMENT_CLASS (parent_class)->send_event (element, event);
      break;
  }

  return ret;
}
