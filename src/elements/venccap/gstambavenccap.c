/*
 * gstambavenccap.c
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
 * SECTION:element-amba_venccap
 * @title: amba_venccap
 * @see_also: amba_vencdemux
 *
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
 * This element reads encoded video bit-stream from Ambarella platform.
 *
 * ## Example pipelines, single channel for h264
 * |[
 * gst-launch-1.0 -v -e amba_venccap ! amba_vencdemux name=dm dm.stream0 ! queue ! h264parse ! mp4mux ! filesink location=h264.mp4
 * ]|
 *  Read h264 encoded bit-stream and recorded into mp4.
 *
 * ## Example pipelines, single channel for h265
 * |[
 * gst-launch-1.0 -v -e amba_venccap ! amba_vencdemux name=dm dm.stream0 ! queue ! h265parse ! mp4mux ! filesink location=h265.mp4
 * ]|
 *  Read h265 encoded bit-stream and recorded into mp4.
 *
 * ## Example pipelines, two channels for h264
 * |[
 * gst-launch-1.0 -v -e amba_venccap ! amba_vencdemux name=vencdemux vencdemux.stream0 ! queue ! h264parse ! mp4mux ! filesink location=h264_0.mp4 vencdemux.stream1 ! queue ! h264parse ! mp4mux ! filesink location=h264_1.mp4
 * ]|
 *  Read h264 encoded bit-stream and recorded into mp4 (two channels)
 *
 * ## Example pipelines, two channels for h265
 * |[
 * gst-launch-1.0 -v -e amba_venccap ! amba_vencdemux name=vencdemux vencdemux.stream0 ! queue ! h265parse ! mp4mux ! filesink location=h265_0.mp4 vencdemux.stream1 ! queue ! h265parse ! mp4mux ! filesink location=h265_1.mp4
 * ]|
 *  Read h265 encoded bit-stream and recorded into mp4 (two channels)
 *
 * ## Example pipelines, single channel for h264 + opus
 * |[
 * gst-launch-1.0 -v -e malsasrc ! queue ! opusenc ! mp4mux name=mux ! filesink location=h264_opus.mp4 -e amba_venccap ! amba_vencdemux name=d d.stream0 ! queue ! h264parse ! mux.
 * ]|
 *  Read h264 encoded bit-stream, plus with opus audio, recorded into mp4.
 *
 * ## Example pipelines, single channel for h265 + opus
 * |[
 * gst-launch-1.0 -v -e malsasrc ! queue ! opusenc ! mp4mux name=mux ! filesink location=h265_opus.mp4 -e amba_venccap ! amba_vencdemux name=d d.stream0 ! queue ! h265parse ! mux.
 * ]|
 *  Read h265 encoded bit-stream, plus with opus audio, recorded into mp4.
 *
 * ## Example pipelines, two channels for h264 + opus
 * |[
 * gst-launch-1.0 -v -e malsasrc ! queue ! opusenc ! tee name=t t.src_0 ! mp4mux name=mux0 ! filesink location=h264_opus_0.mp4 -e amba_venccap ! amba_vencdemux name=d d.stream0 ! queue ! h264parse ! mux0. t.src_1 ! mp4mux name=mux1 ! filesink location=h264_opus_1.mp4 d.stream1 ! queue ! h264parse ! mux1.
 * ]|
 *  Read h264 encoded bit-stream, plus with opus audio, recorded into mp4.
 *
 * ## Example pipelines, two channels for h265 + opus
 * |[
 * gst-launch-1.0 -v -e malsasrc ! queue ! opusenc ! tee name=t t.src_0 ! mp4mux name=mux0 ! filesink location=h265_opus_0.mp4 -e amba_venccap ! amba_vencdemux name=d d.stream0 ! queue ! h265parse ! mux0. t.src_1 ! mp4mux name=mux1 ! filesink location=h265_opus_1.mp4 d.stream1 ! queue ! h265parse ! mux1.
 * ]|
 *  Read h265 encoded bit-stream, plus with opus audio, recorded into mp4.
 *
 * ## Example pipelines, six channels for h264, h265 + opus -> mp4, heic
 * |[
 * gst-launch-1.0 -v -e malsasrc ! queue ! opusenc ! tee name=t t.src_0 ! mp4mux name=mux0 ! filesink location=h265_opus_0.mp4 -e amba_venccap ! amba_vencdemux heic-mode=1 heic-period=10 heic-dump-local=1 filename-base=amba%06d.heic name=d d.stream0 ! queue ! h265parse ! mux0. t.src_1 ! mp4mux name=mux1 ! filesink location=h265_opus_1.mp4 d.stream1 ! queue ! h265parse ! mux1. t.src_2 ! mp4mux name=mux2 ! filesink location=h265_opus_2.mp4 d.stream2 ! queue ! h265parse ! mux2. t.src_3 ! mp4mux name=mux3 ! filesink location=h265_opus_3.mp4 d.stream3 ! queue ! h265parse ! mux3. t.src_4 ! mp4mux name=mux4 ! filesink location=h265_opus_4.mp4 d.stream4 ! queue ! h264parse ! mux4. t.src_5 ! mp4mux name=mux5 ! filesink location=h265_opus_5.mp4 d.stream5 ! queue ! h264parse ! mux5.
 * ]|
 *
 * ## Example pipelines, single channel for h264, rtp
 * |[
 * gst-launch-1.0 -v -e amba_venccap ! amba_vencdemux name=dm dm.stream0 ! queue ! h264parse ! rtph264pay pt=96 name=pay0
 * ]|
 *  Read h264 encoded bit-stream and do rtp streaming
 *
 */

#include "stdio.h"

#include "gst_amba_cavalry_allocator.h"

#include "common_err_code_c.h"

#include "bitstream_state.h"

#include "internal.h"
#include "debug_log.h"

//#include "amba_direct_mem.h"

#include "buffer_utils.h"

#include "gstambavenccap.h"

/* ClockSync args */
// if sync, would drop some gops (pts < 0) at starting
#define DEFAULT_SYNC_WITH_AUDIO         TRUE
//default max vin num 2->0x3
#define DEFAULT_MAX_VIN_ID              0x0
#define DEFAULT_PROVIDE_CLOCK           TRUE
#define DEFAULT_ALLOC_MEM               (1)
/* alloc_mem: 0 = wrap IAV bitstream ptr; 1 = system mem copy; 2 = Cavalry mfd (fd-backed) copy */
#define AMBAVENCCAP_ALLOC_MEM_WRAP      (0)
#define AMBAVENCCAP_ALLOC_MEM_SYSTEM    (1)
#define AMBAVENCCAP_ALLOC_MEM_DMABUF    (2)
#define USE_SYSTEM_CLOCK                (1)
#define DEFAULT_TIMEOUT_MS              (0)
#define DEFAULT_SLEEP_TIME_US           (100000)

GST_DEBUG_CATEGORY_STATIC (gst_amba_venccap_debug);
#define GST_CAT_DEFAULT gst_amba_venccap_debug

enum
{
  PROP_0,
  PROP_ENCODE_SET,
  PROP_ALLOC_MEM,
  PROP_SYNC_WITH_AUDIO,
  PROP_PROVIDE_CLOCK,
  PROP_STREAM_IDX,
  PROP_TIMEOUT_MS,
  PROP_WAIT_IAV_SLEEP_US,
  PROP_QUERY_CANVAS_STREAM_ID,
  PROP_FORCE_IDR_STREAM_ID,
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

#define gst_amba_venccap_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstAmbaVenccap, gst_amba_venccap, GST_TYPE_PUSH_SRC,
    GST_DEBUG_CATEGORY_INIT (gst_amba_venccap_debug, "ambavenccap", 0,
        "ambavenccap"));

#define AMBAVENCCAP_DEFAULT_ENC_FORMAT "stream_id:0"
static GstFlowReturn gst_ambavenccap_create (GstPushSrc * psrc,
    GstBuffer ** outbuf);
static gboolean gst_ambavenccap_start (GstBaseSrc * bsrc);
static void gst_ambavenccap_get_times (GstBaseSrc * src, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end);

static void gst_ambavenccap_finalize (GObject * gobject);
static void gst_ambavenccap_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_ambavenccap_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstClock *
gst_ambavenccap_provide_clock (GstElement * element);

static GstStateChangeReturn
gst_ambavenccap_change_state (GstElement * element, GstStateChange transition);

static gboolean
gst_ambavenccap_send_event (GstElement * element, GstEvent * event);


static unsigned char * nalu_find_first_avc_slice_header_type (
  unsigned char * p, unsigned int len, unsigned char * out_nal_type)
{
  unsigned char nal_type = 0;

  if (!p) {
    return NULL;
  }

  while (len > 5) {
    if (*p == 0x00) {
      if (* (p + 1) == 0x00) {
        if (* (p + 2) == 0x00) {
          if (* (p + 3) == 0x01) {
            nal_type = (* (p + 4) ) & 0x1F;
            *out_nal_type = nal_type;
            if (nal_type <= ENalType_IDR) {
              return p;
            }
          }

        } else if (* (p + 2) == 0x01) {
          nal_type = (* (p + 3) ) & 0x1F;
          *out_nal_type = nal_type;
          if (nal_type <= ENalType_IDR) {
            return p;
          }
        }
      }
    }

    ++ p;
    len --;
  }

  return NULL;
}

static unsigned char * nalu_find_first_hevc_slice_header_type (
  unsigned char * p, unsigned int len, unsigned char * out_nal_type, unsigned char * is_first_slice)
{
  unsigned char nal_type = 0;

  if (!p) {
    return NULL;
  }

  *is_first_slice = 0;

  while (len > 5) {
    if (*p == 0x00) {
      if (* (p + 1) == 0x00) {
        if (* (p + 2) == 0x00) {
          if (* (p + 3) == 0x01) {
            nal_type = ( ( (* (p + 4) ) >> 1) & 0x3F);
            *out_nal_type = nal_type;
            if (nal_type < EHEVCNalType_VPS) {
              if (p[6] & 0x80) {
                *is_first_slice = 1;
              } else {
                *is_first_slice = 0;
              }

              return p;
            }
          }

        } else if (* (p + 2) == 0x01) {
          nal_type = ( ( (* (p + 3) ) >> 1) & 0x3F);
          *out_nal_type = nal_type;

          if (nal_type < EHEVCNalType_VPS) {
            if (p[5] & 0x80) {
              *is_first_slice = 1;
            } else {
              *is_first_slice = 0;
            }

            return p;
          }
        }
      }
    }

    ++ p;
    len --;
  }

  return NULL;
}


static void
gst_ambavenccap_get_times (GstBaseSrc * src, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end)
{
  GstAmbaVenccap * filter = GST_AMBAVENCCAP (src);
  if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer)) {
    *start = GST_BUFFER_TIMESTAMP (buffer);
    if (GST_BUFFER_DURATION_IS_VALID (buffer)) {
      *end = *start + GST_BUFFER_DURATION (buffer);
    } else {
      int stream_idx = -1;
      GstVideoRegionOfInterestMeta *vmeta = (GstVideoRegionOfInterestMeta *)
          gst_buffer_get_meta (buffer, GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE);

      if (vmeta == NULL) {
        GST_ERROR("gst_buffer_iterate_meta_filtered failed.");
        return;
      }

      GstStructure *s = gst_video_region_of_interest_meta_get_param (vmeta, GST_VENCCAP_META_PARAM_NAME);
      if (s) {
        guint iv = 0;
        if (!gst_structure_get_uint (s, GST_VENCCAP_META_FIELD_STREAM_ID, &iv)) {
          GST_ERROR("get stream id failed.");
          return;
        }
        stream_idx = iv;
      }
      if ((stream_idx >= 0) && (stream_idx < IAV_STREAM_MAX_NUM_ALL)
          && (filter->fps_n[stream_idx] > 0)
          && (filter->fps_d[stream_idx] >= 0)) {
        *end = *start +
            gst_util_uint64_scale_int (GST_SECOND, filter->fps_d[stream_idx],
            filter->fps_n[stream_idx]);
      }
    }
  } else {
    *start = -1;
    *end = -1;
  }
}

static void
gst_amba_venccap_class_init (GstAmbaVenccapClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstPushSrcClass *gstpushsrc_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);
  gstbasesrc_class = GST_BASE_SRC_CLASS (klass);
  gstpushsrc_class = GST_PUSH_SRC_CLASS (klass);
  gstelement_class->change_state = gst_ambavenccap_change_state;
  gstelement_class->provide_clock = GST_DEBUG_FUNCPTR (gst_ambavenccap_provide_clock);
  gstelement_class->send_event = GST_DEBUG_FUNCPTR (gst_ambavenccap_send_event);

  gobject_class->finalize = gst_ambavenccap_finalize;
  gobject_class->set_property = gst_ambavenccap_set_property;
  gobject_class->get_property = gst_ambavenccap_get_property;

  g_object_class_install_property (gobject_class, PROP_ENCODE_SET,
      g_param_spec_string ("enc", "Enc",
          "encode setting", AMBAVENCCAP_DEFAULT_ENC_FORMAT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ALLOC_MEM,
      g_param_spec_uchar ("alloc_mem", "AllocMem",
          "Output buffer memory: 0 = wrap encoder buffer (no copy, not recommended); "
          "1 = allocate and copy (system memory, default); "
          "2 = copy into Cavalry mfd buffer (fd-backed GstMemory)",
          0, 2, DEFAULT_ALLOC_MEM, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_SYNC_WITH_AUDIO,
      g_param_spec_boolean ("sync", "Synchronize",
      "Synchronize to pipeline clock", DEFAULT_SYNC_WITH_AUDIO,
      G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_PROVIDE_CLOCK,
      g_param_spec_boolean ("provide-clock", "Provide Clock",
          "Provide a clock to be used as the global pipeline clock",
          DEFAULT_PROVIDE_CLOCK, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

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

  g_object_class_install_property (gobject_class, PROP_QUERY_CANVAS_STREAM_ID,
      g_param_spec_string ("query_canvas_stream_id", "Query Canvas Stream ID",
          "Specify the stream id that needed to query canvas buffer PTS before force idr", "-1",
          G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_FORCE_IDR_STREAM_ID,
      g_param_spec_string ("force_idr_stream_id", "Force IDR Stream ID",
          "Specify the stream id that needed to do force idr", "-1",
          G_PARAM_READWRITE));

  gst_element_class_set_details_simple(gstelement_class,
    "Amba Video Encoder, capture",
    "ambavenccap",
    "Reads encoded video bitstreams from Amba device",
    "Zhi He <zhe@ambarella.com>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&srctemplate));

  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_ambavenccap_start);
  gstbasesrc_class->get_times = GST_DEBUG_FUNCPTR (gst_ambavenccap_get_times);
  gstpushsrc_class->create = GST_DEBUG_FUNCPTR (gst_ambavenccap_create);
}

static void
gst_amba_venccap_init (GstAmbaVenccap * thiz)
{
  // mem init
  //amba_direct_mem_init ();

  // iav context
  thiz->iav_ctx = acquire_iav_ctx (1);
  if (!thiz->iav_ctx) {
    GST_ERROR("acquire_iav_ctx failed\n");
    return;
  }

  thiz->give_clock = DEFAULT_PROVIDE_CLOCK;

  thiz->provided_clock = gst_amba_hw_clock_obtain();
  if (!thiz->provided_clock) {
    GST_ERROR("Failed to create hardware clock");
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

  thiz->alloc_mem = DEFAULT_ALLOC_MEM;

  gst_amba_cavalry_allocator_init_once ();
  thiz->cavalry_allocator = gst_amba_cavalry_allocator_get ();

  thiz->sync = DEFAULT_SYNC_WITH_AUDIO;

  thiz->system_clock = NULL;
  thiz->stream_id = -1;
  thiz->timeout_ms = DEFAULT_TIMEOUT_MS;
  thiz->wait_iav_sleep_us = -1;
  thiz->force_idr_map = 0xffffffff;
  thiz->query_canvas_stream_id_map = 0xffffffff;

  for (int i = 0; i < IAV_STREAM_MAX_NUM_ALL; i++) {
    thiz->last_pts[i] = GST_CLOCK_TIME_NONE;
    thiz->dump_num[i] = 1;
  }

  gst_base_src_set_live (GST_BASE_SRC (thiz), TRUE);
  gst_base_src_set_do_timestamp (GST_BASE_SRC (thiz), TRUE);
  gst_base_src_set_format (GST_BASE_SRC (thiz), GST_FORMAT_TIME);

  GST_OBJECT_FLAG_SET (thiz, GST_ELEMENT_FLAG_PROVIDE_CLOCK);
  GST_OBJECT_FLAG_SET (thiz, GST_ELEMENT_FLAG_REQUIRE_CLOCK);
}

static void
gst_ambavenccap_finalize (GObject * gobject)
{
  GstAmbaVenccap *thiz = GST_AMBAVENCCAP (gobject);

  if (thiz->iav_ctx) {
    release_iav_ctx (1);
    thiz->iav_ctx = NULL;
  }

  if (thiz->clock) {
    destroy_clock(thiz->clock);
    thiz->clock = NULL;
  }

  if (thiz->enc_info) {
    g_free(thiz->enc_info);
    thiz->enc_info = NULL;
  }

  if (thiz->provided_clock) {
    gst_object_unref (thiz->provided_clock);
  }

  if (thiz->system_clock) {
    gst_object_unref (thiz->system_clock);
  }

  if (thiz->cavalry_allocator) {
    gst_object_unref (thiz->cavalry_allocator);
    thiz->cavalry_allocator = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (gobject);
}

static GstClock *
gst_ambavenccap_provide_clock (GstElement * element)
{
  GstAmbaVenccap * src = GST_AMBAVENCCAP (element);

  GST_OBJECT_LOCK (src);

  if (!GST_OBJECT_FLAG_IS_SET (src, GST_ELEMENT_FLAG_PROVIDE_CLOCK)) {
    GST_DEBUG_OBJECT (src, "clock provide disabled");
    GST_OBJECT_UNLOCK (src);
    return NULL;
  }

#if USE_SYSTEM_CLOCK
  // Ensure system_clock exists
  if (src->system_clock == NULL) {
    src->system_clock = gst_system_clock_obtain();
  }

  if (src->system_clock) {
    GstClock *clock = gst_object_ref(src->system_clock);
    GST_OBJECT_UNLOCK (src);
    return clock;
  } else {
    GST_OBJECT_UNLOCK (src);
    return NULL;
  }
#else
  GstClock *clock = GST_CLOCK_CAST (gst_object_ref (src->provided_clock));
  GST_OBJECT_UNLOCK (src);
  return clock;
#endif
}

static void
gst_ambavenccap_set_provide_clock (GstAmbaVenccap * src, gboolean provide)
{
  GstMessage *clock_message = NULL;
  GST_OBJECT_LOCK (src);
  if (provide) {
    GST_OBJECT_FLAG_SET (src, GST_ELEMENT_FLAG_PROVIDE_CLOCK);

#if USE_SYSTEM_CLOCK
    if (src->system_clock == NULL) {
      src->system_clock = gst_system_clock_obtain();
    }
    if (src->system_clock) {
      clock_message =
          gst_message_new_clock_provide (GST_OBJECT_CAST (src),
              src->system_clock, TRUE);
    }
#else
    if (src->provided_clock) {
      clock_message =
          gst_message_new_clock_provide (GST_OBJECT_CAST (src),
              GST_CLOCK_CAST (src->provided_clock), TRUE);
    }
#endif
  } else {
    GST_OBJECT_FLAG_UNSET (src, GST_ELEMENT_FLAG_PROVIDE_CLOCK);
#if USE_SYSTEM_CLOCK
    if (src->system_clock) {
      clock_message =
          gst_message_new_clock_lost (GST_OBJECT_CAST (src),
              src->system_clock);
    }
#else
    if (src->provided_clock) {
      clock_message =
          gst_message_new_clock_lost (GST_OBJECT_CAST (src),
              GST_CLOCK_CAST (src->provided_clock));
    }
#endif

  }
  GST_OBJECT_UNLOCK (src);

  if (clock_message)
    gst_element_post_message (GST_ELEMENT_CAST (src), clock_message);
}

static gboolean
gst_ambavenccap_get_provide_clock (GstAmbaVenccap * src)
{
  gboolean result;

  GST_OBJECT_LOCK (src);
  result = GST_OBJECT_FLAG_IS_SET (src, GST_ELEMENT_FLAG_PROVIDE_CLOCK);
  GST_OBJECT_UNLOCK (src);

  return result;
}

static int parse_id (const char *custom_properties, guint *id_mask)
{
  if (custom_properties) {
    char **options;
    unsigned int i = 0, len = 0;

    options = g_strsplit (custom_properties, ",", -1);
    len = g_strv_length (options);

    *id_mask = 0;
    for (i = 0; i < len; ++i) {
      g_strstrip (options[i]);
      gint cur_stream_id = (gint) g_ascii_strtoll (options[i], NULL, 10);
      if (cur_stream_id >= 0) {
        *id_mask |= (1 << cur_stream_id);
      }
    }
    g_strfreev (options);

  } else {
    printf("no params\n");
    return -1;
  }

  return 0;
}


static void
gst_ambavenccap_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAmbaVenccap * thiz = GST_AMBAVENCCAP (object);
  iav_ctx_t * iav_ctx = thiz->iav_ctx;
  int ret = 0;

  switch (prop_id) {
    case PROP_ENCODE_SET: {
      enc_config_t config;
      ret = parse_enc(iav_ctx->iav_fd, g_value_get_string (value), &config);
      if (ret) {
        DPRINT_ERROR("parse_enc (%s) failed\n",
          g_value_get_string (value));
        return;
      }
      if (!iav_ctx->iav_fd_opened) {
        DPRINT_ERROR("iav not opened\n");
        return;
      }
      ret = update_enc (iav_ctx->iav_fd, &config);
      if (ret) {
        DPRINT_ERROR("update encode setting (%s) failed\n",
          g_value_get_string (value));
        return;
      }
      amba_resource_info_t resource_info = {0};
      if (iav_ctx->iav_al.f_get_resource_info(iav_ctx->iav_fd, &resource_info) < 0) {
        DPRINT_ERROR("f_get_resource_info failed\n");
        return;
      } else {
        for (unsigned int i = 0; i < resource_info.max_stream_num; i++) {
          get_frame_rate(iav_ctx->iav_fd, &config, i);
          thiz->fps[i] = config.stream_fps[i];
          thiz->fps_d[i] = config.framerate_factor[i][1];
          thiz->fps_n[i] = thiz->fps[i] * thiz->fps_d[i];//config.framerate_factor[i][0];
          if (thiz->fps_n[i] > 0 && thiz->fps_d[i] >= 0) {
            thiz->latency[i] = 2 * gst_util_uint64_scale_int (thiz->provided_clock->outfreq,
                thiz->fps_d[i], thiz->fps_n[i]);
          }
        }
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
      gst_ambavenccap_set_provide_clock (thiz, thiz->give_clock);
      break;
    case PROP_STREAM_IDX:
      thiz->stream_id = g_value_get_int (value);
      thiz->force_idr_map = 1 << thiz->stream_id;
      thiz->query_canvas_stream_id_map = 1 << thiz->stream_id;
      break;
    case PROP_TIMEOUT_MS:
      thiz->timeout_ms = g_value_get_uint (value);
      break;
    case PROP_WAIT_IAV_SLEEP_US:
      thiz->wait_iav_sleep_us = g_value_get_int (value);
      break;
    case PROP_QUERY_CANVAS_STREAM_ID:
      strncpy(thiz->canvas_stream_id_str, g_value_get_string (value), 127);
      if (parse_id(thiz->canvas_stream_id_str, &thiz->query_canvas_stream_id_map) < 0) {
        GST_ERROR("parse_id (%s) failed\n",
          g_value_get_string (value));
        return;
      }
      break;
    case PROP_FORCE_IDR_STREAM_ID:
      strncpy(thiz->force_idr_stream_id_str, g_value_get_string (value), 127);
      if (parse_id(thiz->force_idr_stream_id_str, &thiz->force_idr_map) < 0) {
        GST_ERROR("parse_id (%s) failed\n",
          g_value_get_string (value));
        return;
      }
      break;
    default: {
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }break;
  }

}

static void
gst_ambavenccap_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAmbaVenccap *thiz = GST_AMBAVENCCAP (object);
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
      g_value_set_boolean (value, gst_ambavenccap_get_provide_clock (thiz));
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
    case PROP_QUERY_CANVAS_STREAM_ID:
      g_value_set_string (value, thiz->canvas_stream_id_str);
      break;
    case PROP_FORCE_IDR_STREAM_ID:
      g_value_set_string (value, thiz->force_idr_stream_id_str);
      break;
    default: {
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }break;
  }
}

static gboolean __force_idr_in_stream (int iav_fd, unsigned int stream_id, unsigned int stream_format)
{
  enc_force_idr_t force_idr = {0};
  force_idr.enc_index = stream_id;
  if (stream_format == StreamFormat_H264 || stream_format == IAV_STREAM_TYPE_H264) {
    force_idr.stream_type = IAV_STREAM_TYPE_H264;
  } else if (stream_format == StreamFormat_H265 || stream_format == IAV_STREAM_TYPE_H265) {
    force_idr.stream_type = IAV_STREAM_TYPE_H265;
  } else {
    return TRUE;
  }
  enc_force_idr(iav_fd, &force_idr);
  return TRUE;
}

static gboolean __sync_force_idr_in_stream (int iav_fd, unsigned int stream_id,
    unsigned int pts, unsigned int stream_type)
{
  enc_force_idr_t force_idr = {0};
  force_idr.enc_index = stream_id;
  force_idr.pts = pts;
  force_idr.stream_type = stream_type;

  sync_frame_force_idr(iav_fd, &force_idr);

  return TRUE;
}

static int __do_query_yuv_force_idr (GstAmbaVenccap * thiz)
{
  guint cur_id = 0;
  guint dsp_pts[DAMBA_MAX_YUV_BUF_NUM] = {0};
  guint mono_pts[DAMBA_MAX_YUV_BUF_NUM] = {0};
  amba_dsp_query_yuv_buffer_t yuv_buf;

  while (thiz->force_idr_map) {
    for (unsigned int i = 0; i < thiz->max_stream_num; i++) {
      if (thiz->force_idr_map & (1 << i)) {
        if (thiz->query_canvas_stream_id_map & (1 << i)) {
          cur_id = thiz->iav_ctx->iav_al.f_get_enc_src_canvas_id(thiz->iav_ctx->iav_fd, i);
          if (dsp_pts[cur_id] == 0) {
            memset(&yuv_buf, 0x0, sizeof(yuv_buf));
            yuv_buf.capture_select = CAPTURE_PREVIEW_BUFFER;
            yuv_buf.non_block_flag = 1;
            amba_canvas_opt_t *opt = &yuv_buf.canvas_options;
            opt->canvas_num = thiz->canvas_num;
            opt->canvas_buffer_map = 1 << cur_id;
            if (thiz->iav_ctx->iav_al.f_query_yuv_buffer(thiz->iav_ctx->iav_fd, &yuv_buf) < 0) {
              GST_ERROR("f_query_yuv_buffer failed.");
              return -1;
            }
            if (yuv_buf.yuv_ctx[cur_id].mono_pts >= thiz->first_mono_pts) {
              dsp_pts[cur_id] = yuv_buf.yuv_ctx[cur_id].dsp_pts;
              mono_pts[cur_id] = yuv_buf.yuv_ctx[cur_id].mono_pts;
            } else {
              continue;
            }
          }

          if (thiz->enc_dummy_latency[cur_id] && dsp_pts[cur_id]) {
            __sync_force_idr_in_stream(thiz->iav_ctx->iav_fd, i, dsp_pts[cur_id],
                thiz->stream_type[i]);
            if (thiz->iav_ctx->iav_al.f_apply_frame_sync(thiz->iav_ctx->iav_fd, dsp_pts[cur_id],
                    (1U << i), 0) < 0) {
              GST_ERROR_OBJECT(thiz, "f_apply_frame_sync error!\n");
              return -1;
            }
            GST_FIXME_OBJECT(thiz, "frame sync on stream: %d: mono_pts: %d, dsp_pts: %d\n", i, mono_pts[cur_id], dsp_pts[cur_id]);
          } else {
            __force_idr_in_stream(thiz->iav_ctx->iav_fd, i, thiz->stream_type[i]);
            GST_FIXME_OBJECT(thiz, "stream: %d: start mono_pts: %d, dsp_pts: %d\n", i, mono_pts[cur_id], dsp_pts[cur_id]);
          }
        } else {
          __force_idr_in_stream(thiz->iav_ctx->iav_fd, i, thiz->stream_type[i]);
          GST_FIXME_OBJECT(thiz, "stream: %d: start mono_pts: %d, dsp_pts: %d\n", i, mono_pts[cur_id], dsp_pts[cur_id]);
        }
        thiz->force_idr_map &= (~(1 << i));
      }
    }
  }
  return 0;
}

static void __release_bitstream (gpointer param)
{
  amba_release_bits_t *ctx = (amba_release_bits_t *) param;
  for (int i = 0; i < IAV_STREAM_MAX_NUM_ALL; i++) {
    if (ctx[i].is_read && (!ctx[i].p_data_sim)) {
      ctx[i].iav_ctx->iav_al.f_release_bitstream(ctx[i].iav_ctx->iav_fd, &ctx[i].release_bs);
      ctx[i].is_read = 0;
    }
  }
}

static GstBuffer *
gst_ambavenccap_alloc_via_cavalry (GstAmbaVenccap * thiz, const guint8 * p_cur, gsize cur_size)
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

/* GstElement vmethod implementations */
static GstFlowReturn
gst_ambavenccap_create (GstPushSrc * psrc,
    GstBuffer ** outbuf)
{
  GstAmbaVenccap * thiz = GST_AMBAVENCCAP (psrc);

  GstFlowReturn flow_ret = GST_FLOW_OK;
  iav_ctx_t * iav_ctx = thiz->iav_ctx;
  iav_al_t * iav_al = &iav_ctx->iav_al;
  unsigned int ret = 0;

  guchar *p_cur;
  gssize cur_size;

  guint stream_idx = 0;

  video_bs_state_t * cur_bs_state;

  unsigned char * p_tmp;
  unsigned char nal_type;
  unsigned char is_first_slice;

  amba_dsp_read_bitstream_t read_bs = {0};

  GstBuffer * p_out_buf = NULL;
  GstVideoRegionOfInterestMeta *vroi_meta = NULL;
  GstStructure *s = NULL;
  GstClockTimeDiff cur_pts = GST_CLOCK_TIME_NONE;

  while (1) {
    memset(&read_bs, 0x0, sizeof(read_bs));
    if (thiz->stream_id >= 0) {
      read_bs.stream_idx = thiz->stream_id;
    } else {
      read_bs.stream_idx = 0xffffffff;
    }
    read_bs.timeout_ms = thiz->timeout_ms;
    ret = iav_ctx->iav_al.f_read_bitstream(iav_ctx->iav_fd, &read_bs);

    if (ret == COM_ECODE_BAD_STATE) {
      DPRINT_ERROR("read_bitstream failed, ret 0x%08x\n", ret);
      flow_ret = GST_FLOW_ERROR;
      break;
    } else if (COM_ECODE_TRY_AGAIN == ret) {
      //GST_DEBUG_OBJECT (thiz, "try to read bitstream again.");
      goto IF_EOS_COM;
    }

    // check the stream_id
    if (read_bs.stream_idx >= IAV_STREAM_MAX_NUM_ALL) {
      DPRINT_ERROR("read_bs.stream_idx %d exceed max %d.\n",
        read_bs.stream_idx, IAV_STREAM_MAX_NUM_ALL);
      flow_ret = GST_FLOW_ERROR;
      break;
    }

    // stream index
    stream_idx = read_bs.stream_idx;
    thiz->release_param[stream_idx].is_read = 1;
    thiz->release_param[stream_idx].iav_ctx = thiz->iav_ctx;
    thiz->release_param[stream_idx].release_bs.stream_idx = read_bs.stream_idx;
    thiz->release_param[stream_idx].release_bs.framedesc = read_bs.framedesc;
    thiz->release_param[stream_idx].p_data_sim = read_bs.p_data_sim;

    // state and src pad for this stream id
    cur_bs_state = &thiz->bs_states[stream_idx];
    cur_bs_state->stream_id = stream_idx;

    if (COM_ECODE_COMPLETE == ret) {
      DPRINT_NOTICE("eos comes\n");
      if (!read_bs.p_data_sim) {
        iav_al->f_release_bitstream(iav_ctx->iav_fd, &thiz->release_param[stream_idx].release_bs);
        thiz->release_param[stream_idx].is_read = 0;
      }
      // reset this stream's setting
      cur_bs_state->codec_format = StreamFormat_Invalid;
      cur_bs_state->key_frame_comes = 0;
      goto IF_EOS_COM;
    } else if (COM_ECODE_OK != ret) {
      DPRINT_WARNING("ret 0x%08x here?\n", ret);
      break;
    }

    if (thiz->sync) {
      if (read_bs.pts < thiz->first_mono_pts) {
        //GST_FIXME_OBJECT (thiz, "to sync, drop frame %d pts %ld < %ld in stream %d\n", read_bs.encoded_frame_num, read_bs.pts, thiz->first_mono_pts[stream_idx], stream_idx);
        if (!read_bs.p_data_sim) {
          iav_al->f_release_bitstream(iav_ctx->iav_fd, &thiz->release_param[stream_idx].release_bs);
          thiz->release_param[stream_idx].is_read = 0;
        }

        continue;
      }
    }

    // bitstream
    if (read_bs.p_data_sim) {
      p_cur = read_bs.p_data_sim;
    } else {
      p_cur = iav_ctx->map_bsb.base + read_bs.offset;
    }
    cur_size = read_bs.size;

    // check stream format
    if (StreamFormat_Invalid == cur_bs_state->codec_format) {
      cur_bs_state->codec_format = read_bs.stream_format;
    } else {
      if (read_bs.stream_format != cur_bs_state->codec_format) {
        DPRINT_ERROR("stream_format[%d] not match: 0x%02x, 0x%02x\n",
          stream_idx, read_bs.stream_format, cur_bs_state->codec_format);
        flow_ret = GST_FLOW_ERROR;
        break;
      }
    }

    thiz->shared_stream_info.ul_v = 0;
    thiz->shared_stream_info.info_v.stream_idx = stream_idx;
    thiz->shared_stream_info.info_v.stream_fmt = read_bs.stream_format;

    // check avc, hevc, and mjpeg
    if (StreamFormat_H264 == read_bs.stream_format) {
      // check slice type
      p_tmp = nalu_find_first_avc_slice_header_type(p_cur, cur_size, &nal_type);
      if (!p_tmp) {
        GST_WARNING_OBJECT(thiz, "not found h264 slice header on stream_idx %d, skip.\n", stream_idx);
        if (!read_bs.p_data_sim) {
          iav_al->f_release_bitstream(iav_ctx->iav_fd, &thiz->release_param[stream_idx].release_bs);
          thiz->release_param[stream_idx].is_read = 0;
        }
        continue;
      }

      // wait key frame
      if (!cur_bs_state->key_frame_comes) {
        if (ENalType_IDR != nal_type) {
          //DPRINT_NOTICE("h264 stream [%d] wait key frame\n", stream_idx);

          if (!read_bs.p_data_sim) {
            iav_al->f_release_bitstream(iav_ctx->iav_fd, &thiz->release_param[stream_idx].release_bs);
            thiz->release_param[stream_idx].is_read = 0;
          }
          continue;
        }
        DPRINT_NOTICE("h264 stream [%d] key frame comes\n", stream_idx);
        cur_bs_state->key_frame_comes = 1;
        cur_bs_state->slice_num_per_frame = read_bs.slice_num;
        cur_bs_state->tile_num_per_frame = read_bs.tile_num;
        thiz->shared_stream_info.info_v.is_key_frame = 1;
      } else if (ENalType_IDR == nal_type) {
        thiz->shared_stream_info.info_v.is_key_frame = 1;
      } else {
        thiz->shared_stream_info.info_v.is_key_frame = 0;
      }

      thiz->shared_stream_info.info_v.is_frame_start = 1;

      if ((gint64)read_bs.pts == thiz->last_pts[stream_idx]) {
        if (read_bs.slice_id <= cur_bs_state->last_slice_id) {
          DPRINT_WARNING("cur pts (%lu) == last pts (%lu)\n", read_bs.pts, thiz->last_pts[stream_idx]);
          if (!read_bs.p_data_sim) {
            iav_al->f_release_bitstream(iav_ctx->iav_fd, &thiz->release_param[stream_idx].release_bs);
            thiz->release_param[stream_idx].is_read = 0;
          }
          __force_idr_in_stream(iav_ctx->iav_fd, stream_idx, read_bs.stream_format);
          cur_bs_state->key_frame_comes = 0;
          continue;
        }
      }

      cur_bs_state->last_slice_id = read_bs.slice_id;

      if (!thiz->is_clock_started) {
        thiz->clock->base_src_time = read_bs.pts;
        thiz->is_clock_started = 1;
        DPRINT_NOTICE("clock started, base_src_time: %ld (90k)\n", thiz->clock->base_src_time);
      }
      cur_pts = get_cur_clock_dummy(thiz->clock, read_bs.pts);

      if (thiz->dump_num[stream_idx] > 0) {
        if (thiz->shared_stream_info.info_v.is_key_frame) {
          DPRINT_NOTICE("first IDR: stream %d, frame: %d, monopts: %ld (90k), cur_pts: %ld (ns), interval: %ld (ms)\n",
              stream_idx, read_bs.encoded_frame_num, read_bs.pts, cur_pts,
              GST_TIME_AS_MSECONDS (cur_pts));
          thiz->dump_num[stream_idx]--;
        }
      }

#if 0
      p_out_buf = alloc_gst_buffer_amba_direct_mem (p_cur,
        cur_size, thiz->alloc_mem, (void *) thiz->shared_stream_info.ul_v);
      if (!p_out_buf) {
        DPRINT_ERROR("not memory\n");
        flow_ret = GST_FLOW_ERROR;
        break;
      }
#else
      if (thiz->alloc_mem == AMBAVENCCAP_ALLOC_MEM_SYSTEM) {
        p_out_buf = gst_buffer_new_allocate (NULL, cur_size, NULL);
        ret = gst_buffer_fill (p_out_buf, 0, p_cur, cur_size);
        g_assert (ret == cur_size);
      } else if (thiz->alloc_mem == AMBAVENCCAP_ALLOC_MEM_DMABUF) {
        p_out_buf = gst_ambavenccap_alloc_via_cavalry (thiz, p_cur, (gsize) cur_size);
        if (!p_out_buf) {
          flow_ret = GST_FLOW_ERROR;
          break;
        }
      } else {
        p_out_buf = gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_READONLY, p_cur, cur_size, 0,
            cur_size, thiz->release_param, __release_bitstream);
      }
      vroi_meta = gst_buffer_add_video_region_of_interest_meta (p_out_buf,
          GST_VENCCAP_META_NAME,
          0, 0,
          read_bs.video_width, read_bs.video_height);
      if (!vroi_meta) {
        GST_ERROR_OBJECT (thiz,
            "Unable to attach GstVideoRegionOfInterestMeta to buffer");
        gst_buffer_unref (p_out_buf);
        flow_ret = GST_FLOW_ERROR;
        break;
      }

      s = gst_structure_new (GST_VENCCAP_META_PARAM_NAME,
          GST_VENCCAP_META_FIELD_STREAM_ID, G_TYPE_UINT, stream_idx,
          GST_VENCCAP_META_FIELD_STREAM_FORMAT, G_TYPE_UINT, thiz->shared_stream_info.info_v.stream_fmt,
          GST_VENCCAP_META_FIELD_KEY_FRAME, G_TYPE_UINT, thiz->shared_stream_info.info_v.is_key_frame,
          GST_VENCCAP_META_FIELD_FRAME_START, G_TYPE_UINT, thiz->shared_stream_info.info_v.is_frame_start,
          GST_VENCCAP_META_FIELD_FORMAT_CHANGE, G_TYPE_UINT, thiz->shared_stream_info.info_v.is_format_changed,
          GST_VENCCAP_META_FIELD_EXTRADATA, G_TYPE_UINT, thiz->shared_stream_info.info_v.with_extradata,
          GST_VENCCAP_META_FIELD_FPS_N, G_TYPE_INT, thiz->fps_n[stream_idx],
          GST_VENCCAP_META_FIELD_FPS_D, G_TYPE_INT, thiz->fps_d[stream_idx],
          NULL);

      gst_video_region_of_interest_meta_add_param (vroi_meta, s);

#endif

      GST_BUFFER_PTS (p_out_buf) = cur_pts;
      if ((thiz->fps_n[stream_idx] > 0) && (thiz->fps_d[stream_idx] >= 0)) {
        GST_BUFFER_DURATION (p_out_buf) = gst_util_uint64_scale_int (GST_SECOND,
            thiz->fps_d[stream_idx], thiz->fps_n[stream_idx]);
      }

      *outbuf = p_out_buf;

      GST_DEBUG_OBJECT(thiz, "h264_stream_idx: %d, frame: %d, PTS: %ld, PTS diff: %ld, duration: %ld\n",
          stream_idx, read_bs.encoded_frame_num, GST_BUFFER_PTS (p_out_buf),
          GST_BUFFER_PTS (p_out_buf) - thiz->last_pts[stream_idx],
          GST_BUFFER_DURATION (p_out_buf));
      thiz->last_pts[stream_idx] = (gint64)read_bs.pts;
    } else if (StreamFormat_H265 == read_bs.stream_format) {
      // check slice type
      p_tmp = nalu_find_first_hevc_slice_header_type(p_cur, cur_size,
        &nal_type, &is_first_slice);
      if (!p_tmp) {
        GST_WARNING_OBJECT(thiz, "not found h265 slice header on stream_idx %d, skip.", stream_idx);
        //print_memory_u8(p_cur, 32);
        if (!read_bs.p_data_sim) {
          iav_al->f_release_bitstream(iav_ctx->iav_fd, &thiz->release_param[stream_idx].release_bs);
          thiz->release_param[stream_idx].is_read = 0;
        }
        continue;
      }

      // wait key frame
      if (!cur_bs_state->key_frame_comes) {
        if ( ( (EHEVCNalType_IDR_W_RADL != nal_type) && (EHEVCNalType_IDR_N_LP != nal_type) )
          || (!is_first_slice) ) {
          //DPRINT_NOTICE("h265 stream [%d] wait key frame\n", stream_idx);

          if (!read_bs.p_data_sim) {
            iav_al->f_release_bitstream(iav_ctx->iav_fd, &thiz->release_param[stream_idx].release_bs);
            thiz->release_param[stream_idx].is_read = 0;
          }
          continue;
        }
        DPRINT_NOTICE("h265 stream [%d] key frame comes\n", stream_idx);
        cur_bs_state->key_frame_comes = 1;
        cur_bs_state->slice_num_per_frame = read_bs.slice_num;
        cur_bs_state->tile_num_per_frame = read_bs.tile_num;
        thiz->shared_stream_info.info_v.is_key_frame = 1;
      } else if ( (EHEVCNalType_IDR_W_RADL == nal_type) || (EHEVCNalType_IDR_N_LP == nal_type) ) {
        thiz->shared_stream_info.info_v.is_key_frame = 1;
      } else {
        thiz->shared_stream_info.info_v.is_key_frame = 0;
      }

      if ((gint64)read_bs.pts == thiz->last_pts[stream_idx]) {
        if (read_bs.slice_id < cur_bs_state->last_slice_id) {
          DPRINT_WARNING("slice: %d, cur pts (%lu) == last pts (%lu)\n", read_bs.slice_id, read_bs.pts, thiz->last_pts[stream_idx]);
          if (!read_bs.p_data_sim) {
            iav_al->f_release_bitstream(iav_ctx->iav_fd, &thiz->release_param[stream_idx].release_bs);
            thiz->release_param[stream_idx].is_read = 0;
          }
          __force_idr_in_stream(iav_ctx->iav_fd, stream_idx, read_bs.stream_format);
          cur_bs_state->key_frame_comes = 0;
          continue;
        } else if ((read_bs.slice_id == cur_bs_state->last_slice_id)) {
          if (read_bs.tile_id <= cur_bs_state->last_tile_id) {
            DPRINT_WARNING("tile: %d, cur pts (%lu) == last pts (%lu)\n", read_bs.tile_id, read_bs.pts, thiz->last_pts[stream_idx]);
            if (!read_bs.p_data_sim) {
              iav_al->f_release_bitstream(iav_ctx->iav_fd, &thiz->release_param[stream_idx].release_bs);
              thiz->release_param[stream_idx].is_read = 0;
            }
            __force_idr_in_stream(iav_ctx->iav_fd, stream_idx, read_bs.stream_format);
            cur_bs_state->key_frame_comes = 0;

            continue;
          }
        }
      }

      cur_bs_state->last_slice_id = read_bs.slice_id;
      cur_bs_state->last_tile_id = read_bs.tile_id;

      if (!thiz->is_clock_started) {
        thiz->clock->base_src_time = read_bs.pts;
        thiz->is_clock_started = 1;
        DPRINT_NOTICE("clock started, base_src_time: %ld (90k)\n", thiz->clock->base_src_time);
      }
      cur_pts = get_cur_clock_dummy(thiz->clock, read_bs.pts);

      if (is_first_slice) {
        thiz->shared_stream_info.info_v.is_frame_start = 1;
        if (thiz->dump_num[stream_idx] > 0) {
          if (thiz->shared_stream_info.info_v.is_key_frame) {
            DPRINT_NOTICE("first IDR: stream %d, frame: %d, monopts: %ld (90k), cur_pts: %ld (ns), interval: %ld (ms)\n",
                stream_idx, read_bs.encoded_frame_num, read_bs.pts, cur_pts,
                GST_TIME_AS_MSECONDS (cur_pts));
          }
          thiz->dump_num[stream_idx]--;
        }
      }

#if 0
      p_out_buf = alloc_gst_buffer_amba_direct_mem (p_cur,
        cur_size, thiz->alloc_mem, (void *) thiz->shared_stream_info.ul_v);
      if (!p_out_buf) {
        DPRINT_ERROR("not memory\n");
        flow_ret = GST_FLOW_ERROR;
        break;
      }
#else
      if (thiz->alloc_mem == AMBAVENCCAP_ALLOC_MEM_SYSTEM) {
        p_out_buf = gst_buffer_new_allocate (NULL, cur_size, NULL);
        ret = gst_buffer_fill (p_out_buf, 0, p_cur, cur_size);
        g_assert (ret == cur_size);
      } else if (thiz->alloc_mem == AMBAVENCCAP_ALLOC_MEM_DMABUF) {
        p_out_buf = gst_ambavenccap_alloc_via_cavalry (thiz, p_cur, (gsize) cur_size);
        if (!p_out_buf) {
          flow_ret = GST_FLOW_ERROR;
          break;
        }
      } else {
        p_out_buf = gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_READONLY, p_cur, cur_size, 0,
            cur_size, thiz->release_param, __release_bitstream);
      }
      vroi_meta = gst_buffer_add_video_region_of_interest_meta (p_out_buf,
          GST_VENCCAP_META_NAME,
          0, 0,
          read_bs.video_width, read_bs.video_height);
      if (!vroi_meta) {
        GST_ERROR_OBJECT (thiz,
            "Unable to attach GstVideoRegionOfInterestMeta to buffer");
        gst_buffer_unref (p_out_buf);
        flow_ret = GST_FLOW_ERROR;
        break;
      }

      s = gst_structure_new (GST_VENCCAP_META_PARAM_NAME,
          GST_VENCCAP_META_FIELD_STREAM_ID, G_TYPE_UINT, stream_idx,
          GST_VENCCAP_META_FIELD_STREAM_FORMAT, G_TYPE_UINT, thiz->shared_stream_info.info_v.stream_fmt,
          GST_VENCCAP_META_FIELD_KEY_FRAME, G_TYPE_UINT, thiz->shared_stream_info.info_v.is_key_frame,
          GST_VENCCAP_META_FIELD_FRAME_START, G_TYPE_UINT, thiz->shared_stream_info.info_v.is_frame_start,
          GST_VENCCAP_META_FIELD_FORMAT_CHANGE, G_TYPE_UINT, thiz->shared_stream_info.info_v.is_format_changed,
          GST_VENCCAP_META_FIELD_EXTRADATA, G_TYPE_UINT, thiz->shared_stream_info.info_v.with_extradata,
          GST_VENCCAP_META_FIELD_FPS_N, G_TYPE_INT, thiz->fps_n[stream_idx],
          GST_VENCCAP_META_FIELD_FPS_D, G_TYPE_INT, thiz->fps_d[stream_idx],
          NULL);

      gst_video_region_of_interest_meta_add_param (vroi_meta, s);

#endif

      GST_BUFFER_PTS (p_out_buf) = cur_pts;
      if ((thiz->fps_n[stream_idx] > 0) && (thiz->fps_d[stream_idx] >= 0)) {
        GST_BUFFER_DURATION (p_out_buf) = gst_util_uint64_scale_int (GST_SECOND,
            thiz->fps_d[stream_idx], thiz->fps_n[stream_idx]);
      }
      if ((read_bs.tile_id == read_bs.tile_num - 1) &&
          (read_bs.slice_id == read_bs.slice_num - 1)) {
        gst_buffer_set_flags(p_out_buf, GST_BUFFER_FLAG_MARKER);
      }

      *outbuf = p_out_buf;

      GST_DEBUG_OBJECT(thiz, "h265_stream_idx: %d, frame: %d, slice: %d, tile: %d, is frame start: %d, ARMPTS: %ld, PTS: %ld, PTS diff: %ld, duration: %ld\n",
          stream_idx, read_bs.encoded_frame_num, read_bs.slice_id, read_bs.tile_id,
          thiz->shared_stream_info.info_v.is_frame_start, read_bs.pts, GST_BUFFER_PTS (p_out_buf),
          GST_BUFFER_PTS (p_out_buf) - thiz->last_pts[stream_idx],
          GST_BUFFER_DURATION (p_out_buf));
      thiz->last_pts[stream_idx] = (gint64)read_bs.pts;
    } else if (StreamFormat_JPEG == read_bs.stream_format) {

      if ((EJPEG_MarkerPrefix != p_cur[0]) || (EJPEG_SOI != p_cur[1]) || (EJPEG_MarkerPrefix != p_cur[2]) || (128 > cur_size)) {
        GST_WARNING_OBJECT(thiz, "not find mjpeg header %x%x%x, or invalid data size %ld on stream %d, skip.",
            p_cur[0], p_cur[1], p_cur[2], cur_size, stream_idx);
        if (!read_bs.p_data_sim) {
          iav_al->f_release_bitstream(iav_ctx->iav_fd, &thiz->release_param[stream_idx].release_bs);
          thiz->release_param[stream_idx].is_read = 0;
        }
        continue;
      }

      cur_bs_state->key_frame_comes = 1;
      cur_bs_state->slice_num_per_frame = read_bs.slice_num;
      cur_bs_state->tile_num_per_frame = read_bs.tile_num;
      thiz->shared_stream_info.info_v.is_key_frame = 1;
      thiz->shared_stream_info.info_v.is_frame_start = 1;

      if (!thiz->is_clock_started) {
        thiz->clock->base_src_time = read_bs.pts;
        thiz->is_clock_started = 1;
        DPRINT_NOTICE("clock started, base_src_time: %ld (90k)\n", thiz->clock->base_src_time);
      }
      cur_pts = get_cur_clock_dummy(thiz->clock, read_bs.pts);

      if (thiz->dump_num[stream_idx] > 0) {
        DPRINT_NOTICE("first jpeg: stream %d, frame: %d, monopts: %ld (90k), cur_pts: %ld (ns), interval: %ld (ms)\n",
            stream_idx, read_bs.encoded_frame_num, read_bs.pts, cur_pts,
            GST_TIME_AS_MSECONDS (cur_pts));
        thiz->dump_num[stream_idx]--;
      }

      if (thiz->alloc_mem == AMBAVENCCAP_ALLOC_MEM_SYSTEM) {
        p_out_buf = gst_buffer_new_allocate (NULL, cur_size, NULL);
        ret = gst_buffer_fill (p_out_buf, 0, p_cur, cur_size);
        g_assert (ret == cur_size);
      } else if (thiz->alloc_mem == AMBAVENCCAP_ALLOC_MEM_DMABUF) {
        p_out_buf = gst_ambavenccap_alloc_via_cavalry (thiz, p_cur, (gsize) cur_size);
        if (!p_out_buf) {
          flow_ret = GST_FLOW_ERROR;
          break;
        }
      } else {
        p_out_buf = gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_READONLY, p_cur, cur_size, 0,
            cur_size, thiz->release_param, __release_bitstream);
      }
      vroi_meta = gst_buffer_add_video_region_of_interest_meta (p_out_buf,
          GST_VENCCAP_META_NAME,
          0, 0,
          read_bs.video_width, read_bs.video_height);
      if (!vroi_meta) {
        GST_ERROR_OBJECT (thiz,
            "Unable to attach GstVideoRegionOfInterestMeta to buffer");
        gst_buffer_unref (p_out_buf);
        flow_ret = GST_FLOW_ERROR;
        break;
      }

      s = gst_structure_new (GST_VENCCAP_META_PARAM_NAME,
          GST_VENCCAP_META_FIELD_STREAM_ID, G_TYPE_UINT, stream_idx,
          GST_VENCCAP_META_FIELD_STREAM_FORMAT, G_TYPE_UINT, thiz->shared_stream_info.info_v.stream_fmt,
          GST_VENCCAP_META_FIELD_KEY_FRAME, G_TYPE_UINT, thiz->shared_stream_info.info_v.is_key_frame,
          GST_VENCCAP_META_FIELD_FRAME_START, G_TYPE_UINT, thiz->shared_stream_info.info_v.is_frame_start,
          GST_VENCCAP_META_FIELD_FORMAT_CHANGE, G_TYPE_UINT, thiz->shared_stream_info.info_v.is_format_changed,
          GST_VENCCAP_META_FIELD_EXTRADATA, G_TYPE_UINT, thiz->shared_stream_info.info_v.with_extradata,
          GST_VENCCAP_META_FIELD_FPS_N, G_TYPE_INT, thiz->fps_n[stream_idx],
          GST_VENCCAP_META_FIELD_FPS_D, G_TYPE_INT, thiz->fps_d[stream_idx],
          NULL);

      gst_video_region_of_interest_meta_add_param (vroi_meta, s);

      GST_BUFFER_PTS (p_out_buf) = cur_pts;
      if ((thiz->fps_n[stream_idx] > 0) && (thiz->fps_d[stream_idx] >= 0)) {
        GST_BUFFER_DURATION (p_out_buf) = gst_util_uint64_scale_int (GST_SECOND,
            thiz->fps_d[stream_idx], thiz->fps_n[stream_idx]);
      }

      *outbuf = p_out_buf;

      GST_DEBUG_OBJECT(thiz, "mjpeg_stream_idx: %d, frame: %d, PTS: %ld, PTS diff: %ld, duration: %ld\n",
          stream_idx, read_bs.encoded_frame_num, GST_BUFFER_PTS (p_out_buf),
          GST_BUFFER_PTS (p_out_buf) - thiz->last_pts[stream_idx],
          GST_BUFFER_DURATION (p_out_buf));
      thiz->last_pts[stream_idx] = (gint64) read_bs.pts;
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

  if ((thiz->alloc_mem) && (!read_bs.p_data_sim)) {
    iav_al->f_release_bitstream(iav_ctx->iav_fd, &thiz->release_param[stream_idx].release_bs);
    thiz->release_param[stream_idx].is_read = 0;
  }

  return flow_ret;
}


static gboolean
gst_ambavenccap_start (GstBaseSrc * bsrc)
{
  GstAmbaVenccap * thiz = GST_AMBAVENCCAP (bsrc);
  iav_ctx_t * iav_ctx = thiz->iav_ctx;
  enc_config_t config;
  int ret = 0;
  unsigned int valid_map = 0;

  if (!iav_ctx->iav_fd_opened) {
    GST_ERROR ("iav not opened\n");
    return FALSE;
  }

  if (thiz->wait_iav_sleep_us >= 0) {
    while (iav_ctx->iav_al.f_get_iav_state(iav_ctx->iav_fd) != IAV_STATE_ENCODING) {
      g_usleep (thiz->wait_iav_sleep_us);
    };
  } else {
    if (iav_ctx->iav_al.f_get_iav_state(iav_ctx->iav_fd) != IAV_STATE_ENCODING) {
      GST_ERROR ("iav state is not encoding\n");
      return FALSE;
    }
  }

  amba_resource_info_t resource_info = {0};
  if (iav_ctx->iav_al.f_get_resource_info(iav_ctx->iav_fd, &resource_info) < 0) {
    GST_ERROR ("f_get_resource_info failed\n");
    return FALSE;
  }

  thiz->canvas_num = resource_info.canvas_num;
  thiz->max_stream_num = resource_info.max_stream_num;

  for (unsigned int i = 0; i < resource_info.canvas_num; i++) {
    thiz->enc_dummy_latency[i] = resource_info.canvas_enc_dummy_latency[i];
  }
  memset(&config, 0x0, sizeof(enc_config_t));
  ret = get_enc_info_config (iav_ctx->iav_fd, &config, ((1 << resource_info.max_stream_num) - 1), DEFAULT_MAX_VIN_ID);
  if (ret < 0) {
    GST_ERROR ("get encoding information failed\n");
    return FALSE;
  }

  for (unsigned int i = 0; i < resource_info.max_stream_num; i++) {
    thiz->stream_type[i] = config.encode_fmt[i].type;
    if (config.encode_fmt[i].type == IAV_STREAM_TYPE_H265 ||
        config.encode_fmt[i].type == IAV_STREAM_TYPE_H264) {
      if (thiz->wait_iav_sleep_us >= 0) {
        while (thiz->iav_ctx->iav_al.f_get_stream_state(thiz->iav_ctx->iav_fd, i) != IAV_STREAM_STATE_ENCODING) {
          g_usleep (thiz->wait_iav_sleep_us);
        };
      } else {
        if (thiz->iav_ctx->iav_al.f_get_stream_state(thiz->iav_ctx->iav_fd, i) != IAV_STREAM_STATE_ENCODING) {
          GST_WARNING ("stream %d state is not encoding\n", i);
          thiz->force_idr_map &= (~(1 << i));
          thiz->query_canvas_stream_id_map &= (~(1 << i));
        }
      }
    } else {
      thiz->force_idr_map &= (~(1 << i));
      thiz->query_canvas_stream_id_map &= (~(1 << i));
    }
    if (thiz->fps[i] == 0) {
      thiz->fps[i] = config.stream_fps[i];
      thiz->fps_d[i] = config.framerate_factor[i][1];
      thiz->fps_n[i] = thiz->fps[i] * thiz->fps_d[i];
      if (thiz->fps_n[i] > 0 && thiz->fps_d[i] >= 0) {
        thiz->latency[i] = 2 * gst_util_uint64_scale_int (thiz->provided_clock->outfreq,
            thiz->fps_d[i], thiz->fps_n[i]);
      }
    }
    valid_map |= 1 << i;
  }

  thiz->force_idr_map &= valid_map;
  thiz->query_canvas_stream_id_map &= valid_map;

  return TRUE;
}

static GstClock *
get_pipeline_clock_comprehensive(GstElement *element)
{
  GstClock *clock = NULL;

  // Method 1: Direct acquisition
  clock = gst_element_get_clock(element);
  if (clock) {
    return clock;
  }

  // Method 2: Get through parent object chain
  GstObject *current = GST_OBJECT_CAST(element);
  while (current) {
    if (GST_IS_PIPELINE(current)) {
      // For pipeline, try to get its clock
      GstPipeline *pipeline = GST_PIPELINE_CAST(current);
      clock = gst_pipeline_get_pipeline_clock(pipeline);
      if (clock) {
        break;
      }
    }

    GstObject *parent = gst_object_get_parent(current);
    gst_object_unref(current);
    current = parent;
  }

  return clock;
}

static GstClockTime
get_base_time_from_parent_chain(GstElement *element)
{
  GstClockTime base_time = GST_CLOCK_TIME_NONE;
  GstObject *current = GST_OBJECT_CAST(element);

  while (current) {
    if (GST_IS_PIPELINE(current)) {
      GstElement *pipeline_element = GST_ELEMENT_CAST(current);
      base_time = gst_element_get_base_time(pipeline_element);
      break;
    }

    GstObject *parent = gst_object_get_parent(current);
    gst_object_unref(current);
    current = parent;
  }

  return base_time;
}

static GstClockTime
get_base_time_comprehensive(GstElement *element)
{
  GstClockTime base_time = GST_CLOCK_TIME_NONE;

  // Method 1: Direct acquisition
  base_time = gst_element_get_base_time(element);
  if (base_time != GST_CLOCK_TIME_NONE) {
    return base_time;
  }

  // Method 2: Get through parent object chain
  base_time = get_base_time_from_parent_chain(element);
  if (base_time != GST_CLOCK_TIME_NONE) {
    return base_time;
  }

  return GST_CLOCK_TIME_NONE;
}

static void
gst_ambavenccap_start_clock(GstAmbaVenccap *thiz, GstClock *clock, GstClockTime base_time)
{
  if (!thiz->clock || thiz->is_clock_started) {
    return;
  }

  if (clock && base_time != GST_CLOCK_TIME_NONE) {
    GstClockTimeDiff latency = gst_amba_hw_clock_get_time(thiz->provided_clock) - gst_clock_get_time(clock);
    start_clock_v2(thiz->clock, (base_time + latency));
    DPRINT_NOTICE("video base time: %ld (90k) -> %ld (pipeline base time) + %ld = %ld (ns), timestamp : %" GST_TIME_FORMAT
        "\n", thiz->clock->base_src_time,
        base_time, latency, (base_time + latency),
        GST_TIME_ARGS(base_time + latency));

    thiz->first_mono_pts = thiz->clock->base_src_time;
  } else {
    thiz->clock->base_src_time = thiz->first_mono_pts;
    DPRINT_NOTICE("no pipeline clock, use hardware time as base src time, video base time: %ld (90k) -> %ld (ns), timestamp : %" GST_TIME_FORMAT
        "\n", thiz->clock->base_src_time,
        thiz->first_mono_pts,
        GST_TIME_ARGS(thiz->first_mono_pts));
  }

  thiz->is_clock_started = 1;
}

static GstStateChangeReturn
gst_ambavenccap_change_state (GstElement * element, GstStateChange transition)
{
  GstAmbaVenccap * src = GST_AMBAVENCCAP (element);
  GstStateChangeReturn ret= GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      gst_amba_hw_clock_reset (src->provided_clock, 0);
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING: {
#if USE_SYSTEM_CLOCK
      if (src->system_clock == NULL) {
        src->system_clock = gst_system_clock_obtain ();
      }
      if (src->give_clock && src->system_clock) {
        gst_element_post_message (element,
            gst_message_new_clock_provide (GST_OBJECT_CAST (element),
                src->system_clock, TRUE));
      }
#else
      if (src->give_clock && src->provided_clock) {
        gst_element_post_message (element,
            gst_message_new_clock_provide (GST_OBJECT_CAST (element),
                GST_CLOCK_CAST (src->provided_clock), TRUE));
      }
#endif
    } break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  if (transition == GST_STATE_CHANGE_PAUSED_TO_PLAYING && ret == GST_STATE_CHANGE_SUCCESS) {
    if (src->sync) {
      src->first_mono_pts = amba_hwtimer_get_raw_time(src->provided_clock);

      GstClock *pipeline_clock = get_pipeline_clock_comprehensive(element);
      if (pipeline_clock == NULL) {
        GST_ERROR_OBJECT(element, "get pipeline clock failed.");
        ret = GST_STATE_CHANGE_FAILURE;
      }
      GstClockTime base_time = get_base_time_comprehensive(element);
      if (base_time == GST_CLOCK_TIME_NONE) {
        GST_ERROR_OBJECT(element, "get pipeline base time failed.");
        ret = GST_STATE_CHANGE_FAILURE;
      }
      gst_ambavenccap_start_clock(src, pipeline_clock, base_time);
      gst_object_unref(pipeline_clock);

      if (__do_query_yuv_force_idr(src) < 0) {
        return GST_STATE_CHANGE_FAILURE;
      }
    } else {
      for (unsigned int i = 0; i < src->max_stream_num; i++) {
        if (src->force_idr_map & (1 << i)) {
          __force_idr_in_stream(src->iav_ctx->iav_fd, i, src->stream_type[i]);
        }
      }
      src->force_idr_map = 0;
    }

  }

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
#if USE_SYSTEM_CLOCK
      if (src->give_clock && src->system_clock) {
        gst_element_post_message (element,
            gst_message_new_clock_lost (GST_OBJECT_CAST (element),
                src->system_clock));
      }
#else
      if (src->give_clock && src->provided_clock) {
        gst_element_post_message (element,
            gst_message_new_clock_lost (GST_OBJECT_CAST (element),
                GST_CLOCK_CAST (src->provided_clock)));
      }
#endif
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

static gboolean
gst_ambavenccap_send_event (GstElement * element, GstEvent * event)
{
  GstAmbaVenccap * src = GST_AMBAVENCCAP (element);
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
  GST_DEBUG_OBJECT (src, "send event   ******* (%s:%s) E (type: %s (%d), %s) %p\n",
      GST_DEBUG_PAD_NAME (GST_BASE_SRC_CAST (src)->srcpad),
      tstr, GST_EVENT_TYPE (event), sstr, event);
  g_free (sstr);

  if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
    src->is_eos_com = 1;
  }
  GST_OBJECT_UNLOCK (src);

  ret = GST_ELEMENT_CLASS (parent_class)->send_event (element, event);

  return ret;
}

