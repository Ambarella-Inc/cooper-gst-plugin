/*
 * gst_amba_venc.c
 *
 * History:
 *    4/7/2022 - [Peng-Xue Duan] created file
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

#include "gst_amba_venc.h"


static const gchar * gs_src_pad_names[ D_MAX_STREAM_NUM ] =
{
  "stream0",
  "stream1",
  "stream2",
  "stream3",
  "stream4",
  "stream5",
  "stream6",
  "stream7",
  "stream8",
  "stream9",
  "stream10",
  "stream11",
  "stream12",
  "stream13",
  "stream14",
  "stream15",
};

#define AMBAVENC_DEFAULT_VIDEO_TYPE "h264"
#define AMBAVENC_DEFAULT_VIDEO_WIDTH 1920
#define AMBAVENC_DEFAULT_VIDEO_HEIGHT 1080
#define AMBAVENC_DEFAULT_VIDEO_FRAMERATE 30
#define AMBAVENC_DEFAULT_VIDEO_BITRATE 4000000
#define AMBAVENC_DEFAULT_TIMEOUT_MS 3000

GST_DEBUG_CATEGORY_STATIC (gst_amba_venc_debug);
#define GST_CAT_DEFAULT gst_amba_venc_debug

/* Filter signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  PROP_0,
  PROP_TYPE,
  PROP_WIDTH,
  PROP_HEIGHT,
  PROP_FRAMERATE,
  PROP_BITRATE,
  PROP_TIMEOUT,
  PROP_STREAM_ID,
  PROP_STREAM_BITMAP,
  PROP_VIDEO_CONTROL,
  PROP_ENABLE_SEI_STREAM,
  PROP_DEVICE_FD,
  PROP_FILE_PATH,
};

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */

static GstStaticPadTemplate src_avc_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264, "
        "stream-format=(string) { byte-stream }, "
        "alignment=(string) { nal }"));

static GstStaticPadTemplate src_hevc_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h265, "
        "stream-format=(string) { byte-stream }, "
        "alignment=(string) { nal }"));


#define gst_amba_venc_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstAmbaVenc, gst_amba_venc, GST_TYPE_PUSH_SRC,
    GST_DEBUG_CATEGORY_INIT (gst_amba_venc_debug, "amba_venc", 0,
        "amba_venc") );

static GstFlowReturn gst_amba_venc_create (GstPushSrc *psrc,
    GstBuffer **outbuf);

static void gst_amba_venc_finalize (GObject *gobject);
static void gst_amba_venc_set_property (GObject *object, guint prop_id,
    const GValue *value, GParamSpec *pspec);
static void gst_amba_venc_get_property (GObject *object, guint prop_id,
    GValue *value, GParamSpec *pspec);

static GstStateChangeReturn
gst_amba_venc_change_state (GstElement *element, GstStateChange transition);
/* query functions */
static gboolean
gst_amba_venc_src_query (GstPad *pad, GstObject *parent, GstQuery *query);
/* event functions */
static gboolean
gst_amba_venc_send_event (GstElement *element, GstEvent *event);

static GstPad *
gst_amba_venc_add_pad (GstAmbaVenc *amba_venc, GstStaticPadTemplate *templ,
    GstCaps *caps, char *pad_name);
static void
gst_amba_venc_remove_pads (GstAmbaVenc *amba_venc);

/* initialize the amba venc's class */
static void gst_amba_venc_class_init (GstAmbaVencClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstPushSrcClass *gstpushsrc_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);
  gstbasesrc_class = GST_BASE_SRC_CLASS (klass);
  gstpushsrc_class = GST_PUSH_SRC_CLASS (klass);
  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_amba_venc_change_state);
  gstelement_class->send_event = GST_DEBUG_FUNCPTR (gst_amba_venc_send_event);

  gobject_class->finalize = gst_amba_venc_finalize;
  gobject_class->set_property = gst_amba_venc_set_property;
  gobject_class->get_property = gst_amba_venc_get_property;

  g_object_class_install_property (gobject_class, PROP_TYPE,
                                   g_param_spec_string ("type", "Type", "Type of the stream to capture [h264|h265|mjpeg]",
                                       AMBAVENC_DEFAULT_VIDEO_TYPE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS) );

  g_object_class_install_property (gobject_class, PROP_WIDTH,
                                   g_param_spec_uint ("width", "Width", "Width of the stream", 320, 3840,
                                       AMBAVENC_DEFAULT_VIDEO_WIDTH, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS) );

  g_object_class_install_property (gobject_class, PROP_HEIGHT,
                                   g_param_spec_uint ("height", "Height", "Height of the stream", 240, 2160,
                                       AMBAVENC_DEFAULT_VIDEO_HEIGHT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS) );

  g_object_class_install_property (gobject_class, PROP_FRAMERATE,
                                   g_param_spec_uint ("framerate", "Framerate", "Framerate of the stream", 1, 120,
                                       AMBAVENC_DEFAULT_VIDEO_FRAMERATE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS) );

  g_object_class_install_property (gobject_class, PROP_BITRATE,
                                   g_param_spec_uint ("bitrate", "Bitrate", "Bitrate of the stream", 1, 12000000,
                                       AMBAVENC_DEFAULT_VIDEO_BITRATE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS) );

  g_object_class_install_property (gobject_class, PROP_TIMEOUT,
                                   g_param_spec_uint ("timeout", "Timeout", "Timeout of video capture in millisecond, "
                                       "-1 (0xFFFFFFFF) means non-blocking, 0 means blocking",
                                       0, UINT_MAX, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS) );

  g_object_class_install_property (gobject_class, PROP_STREAM_ID,
                                   g_param_spec_uint ("stream-id", "Stream-Id",
                                       "Specify the video stream id to config",
                                       0, 32, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS) );

  g_object_class_install_property (gobject_class, PROP_STREAM_BITMAP,
                                   g_param_spec_uint ("stream-bitmap", "Stream-Bitmap",
                                       "Specify the video streams bitmap for capturing",
                                       1, UINT_MAX, 0xFFFF, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS) );

  g_object_class_install_property (gobject_class, PROP_VIDEO_CONTROL,
                                   g_param_spec_boolean ("video-control", "Video-Control",
                                       "Whether to control the video instance or just capture the stream",
                                       FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS) );

  gst_element_class_set_static_metadata (gstelement_class,
                                         "AmbaVenc",
                                         "Source/Video",
                                         "Reads video frames from Amba device and Separate into independent streams",
                                         "Nick Dong <smdong@ambarella.com>");

  gst_element_class_add_static_pad_template (gstelement_class, &src_factory);

  gstbasesrc_class->start = NULL;
  gstbasesrc_class->stop = NULL;
  gstpushsrc_class->create = gst_amba_venc_create;
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void gst_amba_venc_init (GstAmbaVenc *filter)
{
  int ret = 0;

  do {
    memset (filter, 0x0, sizeof (GstAmbaVenc) );

    // mem init
    amba_direct_mem_init ();

    // iav context
    filter->iav_ctx = acquire_iav_ctx (1);
    if (!filter->iav_ctx) {
      GST_ERROR("acquire_iav_ctx failed\n");
      return;
    }

    filter->type = g_strdup (AMBAVENC_DEFAULT_VIDEO_TYPE);
    filter->width = AMBAVENC_DEFAULT_VIDEO_WIDTH;
    filter->height = AMBAVENC_DEFAULT_VIDEO_HEIGHT;
    filter->framerate = AMBAVENC_DEFAULT_VIDEO_FRAMERATE;
    filter->bitrate = AMBAVENC_DEFAULT_VIDEO_BITRATE;
    filter->timeout = AMBAVENC_DEFAULT_TIMEOUT_MS; //3000ms
    filter->stream_id = 0xffffffff;//0;
    filter->stream_bitmap = 0xFFFF; //default capture all enabled streams
    filter->video_control = FALSE;

    filter->mbSkipSEI = 1;
    filter->mbSkipDelimiter = 1;
    filter->mbRemaingData = 0;
    filter->mRemainingDataOutputIndex = 0;
    filter->mIavFd = -1;
    filter->mpRemainingData = NULL;
    filter->mnRemainingDataSize = 0;
    filter->mRemainBufferTimestamp = 0;
    filter->mLastDTS = 0;

    filter->mVinFramerateNum = 0;
    filter->mVinFramerateDen = 0;
    filter->mVinFramerate = 0;

    for (guint i = 0; i < MAX_STREAM_NUM; i ++) {
      amba_stream_params_t *stream = &filter->stream_config[i];
      stream->mbWaitFirstKeyframe = 1;

      stream->mCodecFormat = StreamFormat_Invalid;
      stream->mBitrate = 1024 * 1024;

      stream->mVideoWidth = 0;
      stream->mVideoHeight = 0;
      stream->mFramerateNum = 0;
      stream->mFramerateDen = 0;
      stream->mFrameType = 0;

      stream->mFramerate = 0;

      stream->mpVideoExtraData = NULL;
      stream->mVideoExtraDataSize = 0;
      stream->mbAlreadySendSyncBuffer = 0;
    }

    gst_base_src_set_live (GST_BASE_SRC (filter), TRUE);
    gst_base_src_set_do_timestamp (GST_BASE_SRC (filter), TRUE);
    gst_base_src_set_format (GST_BASE_SRC (filter), GST_FORMAT_TIME);

    /* src pads will be created in the chain function */
    for (int num = 0; num < MAX_STREAM_NUM; num++) {
      filter->videosrcpad[num] = NULL;
    }

  } while (0);
}

static GstPad *gst_amba_venc_add_pad (GstAmbaVenc *amba_venc, GstStaticPadTemplate *templ,
    GstCaps *caps, gchar *pad_name)
{
  GstPad *pad = NULL;
  GstEvent *event = NULL;
  gchar *stream_id = NULL;
  GstSegment *segment = NULL;

  do {
    if (NULL == (pad = gst_pad_new_from_static_template (templ, pad_name) ) ) {
      GST_ERROR ("Failed to create pad %s from template", pad_name);
      break;
    }

    gst_pad_set_query_function (pad,
      GST_DEBUG_FUNCPTR (gst_amba_venc_src_query) );
    gst_pad_use_fixed_caps (pad);
    gst_pad_set_active (pad, TRUE);

    if (NULL == (stream_id = gst_pad_create_stream_id (pad,
        GST_ELEMENT_CAST (amba_venc), pad_name) ) ) {
      GST_ERROR ("Failed to create stream-id for pad %s", pad_name);
      break;
    }

    if (NULL == (event = gst_event_new_stream_start (stream_id) ) ) {
      GST_ERROR ("Failed to create new stream start event for pad %s", pad_name);
      break;
    }

    gst_pad_push_event (pad, event);
    g_free (stream_id);

    if (caps && gst_pad_set_caps (pad, caps) ) {
      GST_INFO ("caps are set successfully for pad %s", pad_name);
    }

    if (NULL == (segment = gst_segment_new() ) ) {
      GST_ERROR ("Failed to create segment for pad %s", pad_name);
      break;
    }

    gst_segment_init (segment, GST_FORMAT_TIME);

    if (NULL == (event = gst_event_new_segment (segment) ) ) {
      GST_ERROR ("Failed to create new segment event for pad %s", pad_name);
      break;
    }

    gst_pad_push_event (pad, event);

    gst_element_add_pad (GST_ELEMENT (amba_venc), pad);
  } while (0);

  return pad;
}

static void gst_amba_venc_remove_pads (GstAmbaVenc *amba_venc)
{
  for (int num = 0; num < MAX_STREAM_NUM; num++) {
    if (amba_venc->videosrcpad[num]) {
      gst_element_remove_pad (GST_ELEMENT (amba_venc), amba_venc->videosrcpad[num]);
      amba_venc->videosrcpad[num] = NULL;
    }
  }
}

static gboolean gst_amba_venc_src_query (GstPad *pad, GstObject *parent, GstQuery *query)
{
  gboolean res = TRUE;

  /* Handle any necessary src queries */
  switch (GST_QUERY_TYPE (query) ) {
    default: {
      res = gst_pad_query_default (pad, parent, query);
    }
    break;
  }

  return res;
}

static gboolean gst_amba_venc_send_event (GstElement *element, GstEvent *event)
{
  gboolean ret = FALSE;

  switch (GST_EVENT_TYPE (event) ) {
    default:
      ret = GST_ELEMENT_CLASS (parent_class)->send_event (element, event);
      break;
  }

  return ret;
}

static void gst_amba_venc_finalize (GObject *gobject)
{
  GstAmbaVenc *this = GST_AMBAVENC (gobject);

  amba_video_reader_deinit();

  if (this->video_control && G_UNLIKELY (0 != amba_video_control_deinit() ) ) {
    GST_ERROR_OBJECT (this, "Failed to deinit amba video control!");
  }


  if (this->mfDSPAL.f_unmap_bsb (this->mIavFd, &this->mMapBSB) ) {
    GST_ERROR ("unmap fail\n");
  }

  for (guint i = 0; i < MAX_STREAM_NUM; i ++) {
    if (this->stream_config[i].mpVideoExtraData) {
      free (this->stream_config[i].mpVideoExtraData);
      this->stream_config[i].mpVideoExtraData = NULL;
    }
  }

  g_free (this->type);
  this->type = NULL;

  if (this->file_path) {
    g_free (this->file_path);
    this->file_path = NULL;
  }

  gst_amba_venc_remove_pads (this);

  G_OBJECT_CLASS (parent_class)->finalize (gobject);
}

static void gst_amba_venc_set_property (GObject *object, guint prop_id,
                                        const GValue *value, GParamSpec *pspec)
{
  GstAmbaVenc *filter = GST_AMBAVENC (object);

  switch (prop_id) {
    case PROP_TYPE: {
      if (G_UNLIKELY (!g_value_get_string (value) ) ) {
        GST_ERROR ("type property cannot be NULL");
        break;
      }

      g_free (filter->type);
      filter->type = g_strdup (g_value_get_string (value) );
    }
    break;

    case PROP_WIDTH: {
      filter->width = g_value_get_uint (value);
    }
    break;

    case PROP_HEIGHT: {
      filter->height = g_value_get_uint (value);
    }
    break;

    case PROP_FRAMERATE: {
      filter->framerate = g_value_get_uint (value);
    }
    break;

    case PROP_BITRATE: {
      filter->bitrate = g_value_get_uint (value);
    }
    break;

    case PROP_TIMEOUT: {
      filter->timeout = g_value_get_uint (value);
    }
    break;

    case PROP_STREAM_ID: {
      filter->stream_id = g_value_get_uint (value);
    }
    break;

    case PROP_STREAM_BITMAP: {
      filter->stream_bitmap = g_value_get_uint (value);
    }
    break;

    case PROP_VIDEO_CONTROL: {
      filter->video_control = g_value_get_boolean (value);
    }
    break;

    case PROP_ENABLE_SEI_STREAM: {
      if (G_UNLIKELY (!g_value_get_uchar (value) ) ) {
        filter->mbSkipSEI = 1;

      } else {
        filter->mbSkipSEI = 0;
      }
    }
    break;

    case PROP_DEVICE_FD: {
      filter->mIavFd = g_value_get_int (value);
    }
    break;

    case PROP_FILE_PATH: {
      if (G_UNLIKELY (g_value_get_string (value) ) ) {
        filter->file_path = g_strdup (g_value_get_string (value) );
      }
    }
    break;

    default: {
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
    break;
  }
}

static void gst_amba_venc_get_property (GObject *object, guint prop_id,
                                        GValue *value, GParamSpec *pspec)
{
  GstAmbaVenc *filter = GST_AMBAVENC (object);

  switch (prop_id) {
    case PROP_TYPE: {
      g_value_set_string (value, filter->type);
    }
    break;

    case PROP_WIDTH: {
      g_value_set_uint (value, filter->width);
    }
    break;

    case PROP_HEIGHT: {
      g_value_set_uint (value, filter->height);
    }
    break;

    case PROP_FRAMERATE: {
      g_value_set_uint (value, filter->framerate);
    }
    break;

    case PROP_BITRATE: {
      g_value_set_uint (value, filter->bitrate);
    }
    break;

    case PROP_TIMEOUT: {
      g_value_set_uint (value, filter->timeout);
    }
    break;

    case PROP_STREAM_ID: {
      g_value_set_uint (value, filter->stream_id);
    }
    break;

    case PROP_STREAM_BITMAP: {
      g_value_set_uint (value, filter->stream_bitmap);
    }
    break;

    case PROP_VIDEO_CONTROL: {
      g_value_set_boolean (value, filter->video_control);
    }
    break;

    case PROP_ENABLE_SEI_STREAM: {
      g_value_set_uchar (value, !filter->mbSkipSEI);
    }
    break;

    case PROP_DEVICE_FD: {
      g_value_set_int (value, filter->mIavFd);
    }
    break;

    default: {
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
    break;
  }
}

static gint amba_video_encode (GstAmbaVenc *filter,
  amba_venc_buffer *out_buffer)
{
  TU32 skip_size = 0;
  gint ret = 0;
  amba_dsp_release_bitstream_t release_param;
  release_param.framedesc = NULL;
  out_buffer->mpSEI = NULL;
  out_buffer->mSEISize = 0;
  amba_stream_params_t *stream = NULL;

  guint current_remaining = 0;
  guint all_cached_frames = 0;
  guint output_index = 0;

  if (filter->mbRemaingData) {
    filter->mbRemaingData = 0;

    output_index = filter->mRemainingDataOutputIndex;

    if (filter->mRemainingDataOutputIndex >= MAX_STREAM_NUM) {
      GST_ERROR ("Invalid stream_id %d", filter->mRemainingDataOutputIndex);
      return -1;
    }

    stream = &filter->stream_config[output_index];

    if (StreamFormat_H264 == stream->mCodecFormat) {
      skip_size = gstSkipSEI (filter->mpRemainingData, filter->mnRemainingDataSize);

    } else if (StreamFormat_H265 == stream->mCodecFormat) {
      skip_size = gstSkipSEIHEVC (filter->mpRemainingData, filter->mnRemainingDataSize);

    } else {
      skip_size = 0;
    }

    if ( (!filter->mbSkipSEI) && skip_size) {
      out_buffer->mpSEI = filter->mpRemainingData;
      out_buffer->mSEISize = skip_size;
    }

    filter->mpRemainingData += skip_size;
    filter->mnRemainingDataSize -= skip_size;

    //out_buffer->SetBufferFlags(CIBuffer::KEY_FRAME);
    out_buffer->mBufferType = EBufferType_VideoES;
    out_buffer->mpCustomizedContent = NULL;
    out_buffer->mCustomizedContentType = EBufferCustomizedContentType_Invalid;

    out_buffer->mVideoWidth = stream->mVideoWidth;
    out_buffer->mVideoHeight = stream->mVideoHeight;
    out_buffer->mVideoBitrate = stream->mBitrate;
    out_buffer->mVideoFrameRateNum = stream->mFramerateNum;
    out_buffer->mVideoFrameRateDen = stream->mFramerateDen;
    out_buffer->mVideoRoughFrameRate = stream->mFramerate;
    out_buffer->mVideoFrameType = stream->mFrameType;
    out_buffer->mContentFormat = stream->mCodecFormat;

    out_buffer->mpData = filter->mpRemainingData;
    out_buffer->mDataSize = filter->mnRemainingDataSize;

    if (StreamFormat_H265 == stream->mCodecFormat) {
      out_buffer->mbDataSegment = 1;
    }

    out_buffer->mPTS = filter->mRemainBufferTimestamp;
    out_buffer->mNativePTS = filter->mRemainBufferTimestamp;
    out_buffer->mLinearPTS = filter->mRemainBufferTimestamp;

    out_buffer->mDTS = filter->mLastDTS;
    out_buffer->mNativeDTS = filter->mLastDTS;
    out_buffer->mLinearDTS = filter->mLastDTS;

    current_remaining = 0;
    all_cached_frames = 0;
    ret = GST_FLOW_OK;
    goto ENCODE_EXIT;
  }

  if (filter->mfDSPAL.f_read_bitstream && (0 < filter->mIavFd) ) {
    amba_dsp_read_bitstream_t param;
    TU8 *p = NULL;
    TU32 data_size = 0;
    TU8 nal_type = 0;
    TInt ret_tmp = 0;
    TU8 *p_tmp = NULL;

    ret_tmp = filter->mfDSPAL.f_is_ready_for_read_bitstream (filter->mIavFd);

    if (!ret_tmp) {
      sleep (1);
      GST_INFO ("wait encode start.\n");
      ret = GST_FLOW_CUSTOM_SUCCESS_1;
      goto ENCODE_EXIT;
    }

    param.stream_id = filter->stream_id;
    param.timeout_ms = 1000;
    ret_tmp = filter->mfDSPAL.f_read_bitstream (filter->mIavFd, &param);

    if (0 < ret_tmp) {
      if (1 != ret_tmp) {
        sleep (1);
        GST_INFO ("wait encode start..\n");

      } else {
        GST_INFO ("wait encode start...\n");
      }

      ret = GST_FLOW_CUSTOM_SUCCESS_1;
      goto ENCODE_EXIT;

    } else if (0 > ret_tmp) {
      GST_ERROR ("read bitstream error\n");
      ret = GST_FLOW_CUSTOM_ERROR;
      goto ENCODE_EXIT;
    }

    if (MAX_STREAM_NUM <= param.stream_id) {
      GST_ERROR ("stream_id (%d) exceed max value %d\n", param.stream_id, MAX_STREAM_NUM);
      ret = GST_FLOW_CUSTOM_ERROR;
      goto ENCODE_EXIT;
    }

    stream = &filter->stream_config[param.stream_id];

    if (!param.size) {
      GST_INFO ("stream(%d) end\n", param.stream_id);
      stream->mFramerateNum = 0;
      stream->mCodecFormat = StreamFormat_Invalid;
      stream->mbWaitFirstKeyframe = 1;
      ret = GST_FLOW_CUSTOM_SUCCESS_1;
      goto ENCODE_EXIT;
    }

    if (0xffffffff == filter->stream_id) {
      output_index = param.stream_id;

    } else {
      if (filter->stream_id & (1 << param.stream_id) ) {
        output_index = 0;

      } else {
        ret = GST_FLOW_CUSTOM_SUCCESS_1;
        goto ENCODE_EXIT;
      }
    }

    release_param.framedesc = param.framedesc;
    release_param.stream_id = param.stream_id;

    stream = &filter->stream_config[output_index];

    if (!stream->mFramerateNum) {
      if (filter->mfDSPAL.f_get_stream_framefactor) {
        amba_dsp_stream_framefactor_t framefactor;
        ret_tmp = filter->mfDSPAL.f_get_stream_framefactor (filter->mIavFd, output_index, &framefactor);

        if (!ret_tmp) {
          stream->mFramerateNum = filter->mVinFramerateNum;
          stream->mFramerateDen = (gulong) ( (gulong) filter->mVinFramerateDen * (gulong) framefactor.framefactor_den) / (gulong) framefactor.framefactor_num;
          stream->mFramerate = (gfloat) stream->mFramerateNum / (gfloat) stream->mFramerateDen;
          GST_INFO ("stream [%d], get framerate num %d, den %d, fps %f\n", output_index, stream->mFramerateNum, stream->mFramerateDen, stream->mFramerate);

        } else {
          stream->mFramerateNum = DDefaultVideoFramerateNum;
          stream->mFramerateDen = DDefaultVideoFramerateDen;
          stream->mFramerate = (gfloat) stream->mFramerateNum / (gfloat) stream->mFramerateDen;
          GST_WARNING ("f_get_stream_framefactor fail, stream [%d], guess framerate num %d, den %d, fps %f\n", output_index, stream->mFramerateNum, stream->mFramerateDen, stream->mFramerate);
        }

      } else {
        stream->mFramerateNum = DDefaultVideoFramerateNum;
        stream->mFramerateDen = DDefaultVideoFramerateDen;
        stream->mFramerate = (gfloat) stream->mFramerateNum / (gfloat) stream->mFramerateDen;
        GST_WARNING ("no f_get_stream_framefactor, stream [%d], guess framerate num %d, den %d, fps %f\n", output_index, stream->mFramerateNum, stream->mFramerateDen, stream->mFramerate);
      }
    }

    if (StreamFormat_Invalid == stream->mCodecFormat) {
      stream->mCodecFormat = (StreamFormat) param.stream_type;
      GST_INFO ("stream[%d] start, video format %d\n", output_index, stream->mCodecFormat);

    } else if (stream->mCodecFormat != (StreamFormat) param.stream_type) {
      GST_WARNING ("stream[%d], video format change? 0x%x --> 0x%x\n", output_index, stream->mCodecFormat, param.stream_type);
      stream->mCodecFormat = (StreamFormat) param.stream_type;
    }

    out_buffer->mContentFormat = stream->mCodecFormat;

    p = (guchar *) filter->mMapBSB.base + param.offset;
    data_size = param.size;

    if (filter->mbSkipDelimiter) {
      skip_size = gstSkipDelimter (p);
      p += skip_size;
      data_size -= skip_size;
    }

    stream->mFrameType = param.hint_frame_type;

    if (StreamFormat_H264 == stream->mCodecFormat) {

      p_tmp = nalu_find_first_avc_slice_header_type (p, data_size, nal_type);

      if (!p_tmp) {
        GST_INFO ("stream[%d] bad data %02x %02x %02x %02x, %02x %02x %02x %02x, no start code? skip\n",
                  output_index, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
        ret = GST_FLOW_CUSTOM_SUCCESS_1;
        goto ENCODE_EXIT;
      }

      out_buffer->mbDataSegment = 0;

      if (ENalType_IDR == nal_type) {
        stream->mbWaitFirstKeyframe = 0;

        guchar has_sps = 0, has_pps = 0, has_idr = 0;
        guchar *p_sps = NULL, *p_pps = NULL, *p_pps_end = NULL, *p_idr = NULL;
        gstFindH264SpsPpsIdr (p, data_size, has_sps, has_pps, has_idr, p_sps, p_pps, p_pps_end, p_idr);

        if (has_sps && has_pps && p_sps && p_pps_end) {

          guint new_extra_data_size = (guint) (p_pps_end - p_sps);

          if (stream->mpVideoExtraData && (stream->mVideoExtraDataSize < new_extra_data_size) ) {
            free (stream->mpVideoExtraData);
            stream->mpVideoExtraData = NULL;
          }

          if (!stream->mpVideoExtraData) {
            stream->mpVideoExtraData = (guchar *) malloc (new_extra_data_size);

            if (!stream->mpVideoExtraData) {
              GST_ERROR ("no memory\n");
              ret = GST_FLOW_CUSTOM_ERROR;
              goto ENCODE_EXIT;
            }
          }

          stream->mVideoExtraDataSize = new_extra_data_size;
          memcpy (stream->mpVideoExtraData, p_sps, stream->mVideoExtraDataSize);

          out_buffer->mBufferType = EBufferType_VideoExtraData;
          out_buffer->mpCustomizedContent = NULL;
          out_buffer->mCustomizedContentType = EBufferCustomizedContentType_Invalid;

          out_buffer->mVideoWidth = param.video_width;
          out_buffer->mVideoHeight = param.video_height;

          out_buffer->mVideoBitrate = stream->mBitrate;
          out_buffer->mVideoFrameRateNum = stream->mFramerateNum;
          out_buffer->mVideoFrameRateDen = stream->mFramerateDen;
          out_buffer->mVideoRoughFrameRate = stream->mFramerate;
          out_buffer->mVideoFrameType = stream->mFrameType;

          out_buffer->mpData = stream->mpVideoExtraData;
          out_buffer->mDataSize = stream->mVideoExtraDataSize;

          filter->mLastDTS = param.pts;
          filter->mRemainBufferTimestamp = param.pts;

          out_buffer->mPTS = filter->mRemainBufferTimestamp;
          out_buffer->mNativePTS = filter->mRemainBufferTimestamp;
          out_buffer->mLinearPTS = filter->mRemainBufferTimestamp;

          out_buffer->mDTS = filter->mLastDTS;
          out_buffer->mNativeDTS = filter->mLastDTS;
          out_buffer->mLinearDTS = filter->mLastDTS;

          current_remaining = 1;
          all_cached_frames = 1;

          p += stream->mVideoExtraDataSize;
          data_size -= stream->mVideoExtraDataSize;

          filter->mpRemainingData = p;
          filter->mnRemainingDataSize = data_size;

          stream->mVideoWidth = param.video_width;
          stream->mVideoHeight = param.video_height;
          filter->mRemainingDataOutputIndex = output_index;
          filter->mbRemaingData = 1;
          ret = GST_FLOW_OK;
          goto ENCODE_EXIT;
        }

      } else if (stream->mbWaitFirstKeyframe) {
        GST_INFO ("stream[%d] skip till key frame\n", output_index);
        ret = GST_FLOW_CUSTOM_SUCCESS_1;
        goto ENCODE_EXIT;
      }

      out_buffer->mBufferType = EBufferType_VideoES;
      out_buffer->mpCustomizedContent = NULL;
      out_buffer->mCustomizedContentType = EBufferCustomizedContentType_Invalid;

      out_buffer->mVideoWidth = stream->mVideoWidth;
      out_buffer->mVideoHeight = stream->mVideoHeight;
      out_buffer->mVideoBitrate = stream->mBitrate;
      out_buffer->mVideoFrameRateNum = stream->mFramerateNum;
      out_buffer->mVideoFrameRateDen = stream->mFramerateDen;
      out_buffer->mVideoRoughFrameRate = stream->mFramerate;
      out_buffer->mVideoFrameType = stream->mFrameType;

      skip_size = gstSkipSEI (p, data_size);

      if ( (!filter->mbSkipSEI) && skip_size) {
        out_buffer->mpSEI = p;
        out_buffer->mSEISize = skip_size;
      }

      p += skip_size;
      data_size -= skip_size;

      out_buffer->mpData = p;
      out_buffer->mDataSize = data_size;

      if (EPredefinedPictureType_B == param.hint_frame_type) {
        filter->mLastDTS += stream->mFramerateDen;

      } else {
        filter->mLastDTS = param.pts;
      }

      out_buffer->mPTS = (glong) param.pts;
      out_buffer->mNativePTS = (glong) param.pts;
      out_buffer->mLinearPTS = (glong) param.pts;

      out_buffer->mDTS = filter->mLastDTS;
      out_buffer->mNativeDTS = filter->mLastDTS;
      out_buffer->mLinearDTS = filter->mLastDTS;

      current_remaining = 0;
      all_cached_frames = 0;

    } else if (StreamFormat_H265 == stream->mCodecFormat) {
      TU8 is_first_slice = 0;

      p_tmp = nalu_find_first_hevc_slice_header_type (p, data_size, nal_type, is_first_slice);

      if (!p_tmp) {
        GST_INFO ("stream[%d] bad data %02x %02x %02x %02x, %02x %02x %02x %02x, no start code? skip\n",
                  output_index, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
        return GST_FLOW_CUSTOM_SUCCESS_1;
      }

      out_buffer->mbDataSegment = 1;

      if ( (EHEVCNalType_IDR_W_RADL == nal_type) || (EHEVCNalType_IDR_N_LP == nal_type) ) {
        if (stream->mbWaitFirstKeyframe && (!is_first_slice) ) {
          GST_INFO ("stream[%d], skip till key frame (first slice)\n", output_index);
          return GST_FLOW_CUSTOM_SUCCESS_1;//COM_ECODE_OK_NoOutputYet;
        }


        if (is_first_slice) {
          guchar has_vps = 0, has_sps = 0, has_pps = 0, has_idr = 0;
          guchar *p_vps = NULL, *p_sps = NULL, *p_pps = NULL, *p_pps_end = NULL, *p_idr = NULL;
          gstFindH265VpsSpsPpsIdr (p, data_size, has_vps, has_sps, has_pps, has_idr, p_vps, p_sps, p_pps, p_pps_end, p_idr);

          if (has_vps && has_sps && has_pps && p_vps && p_sps && p_pps_end) {

            guint new_extra_data_size = (guint) (p_pps_end - p_vps);

            if (stream->mpVideoExtraData && (stream->mVideoExtraDataSize < new_extra_data_size) ) {
              free (stream->mpVideoExtraData);
              stream->mpVideoExtraData = NULL;
            }

            if (!stream->mpVideoExtraData) {
              stream->mpVideoExtraData = (guchar *) malloc (new_extra_data_size);

              if (!stream->mpVideoExtraData) {
                GST_ERROR ("no memory\n");
                return GST_FLOW_CUSTOM_ERROR;//int_NoMemory;
              }
            }

            stream->mVideoExtraDataSize = new_extra_data_size;
            memcpy (stream->mpVideoExtraData, p_vps, stream->mVideoExtraDataSize);

            out_buffer->mBufferType = EBufferType_VideoExtraData;
            out_buffer->mpCustomizedContent = NULL;
            out_buffer->mCustomizedContentType = EBufferCustomizedContentType_Invalid;

            out_buffer->mVideoWidth = param.video_width;
            out_buffer->mVideoHeight = param.video_height;

            out_buffer->mVideoBitrate = stream->mBitrate;
            out_buffer->mVideoFrameRateNum = stream->mFramerateNum;
            out_buffer->mVideoFrameRateDen = stream->mFramerateDen;
            out_buffer->mVideoRoughFrameRate = stream->mFramerate;
            out_buffer->mVideoFrameType = stream->mFrameType;

            out_buffer->mpData = stream->mpVideoExtraData;
            out_buffer->mDataSize = stream->mVideoExtraDataSize;

            filter->mLastDTS = param.pts;
            filter->mRemainBufferTimestamp = param.pts;

            out_buffer->mPTS = filter->mRemainBufferTimestamp;
            out_buffer->mNativePTS = filter->mRemainBufferTimestamp;
            out_buffer->mLinearPTS = filter->mRemainBufferTimestamp;

            out_buffer->mDTS = filter->mLastDTS;
            out_buffer->mNativeDTS = filter->mLastDTS;
            out_buffer->mLinearDTS = filter->mLastDTS;

            current_remaining = 1;
            all_cached_frames = 1;

            data_size -= (guint) (p_idr - p);
            p = p_idr;

            filter->mpRemainingData = p;
            filter->mnRemainingDataSize = data_size;

            stream->mVideoWidth = param.video_width;
            stream->mVideoHeight = param.video_height;
            filter->mbRemaingData = 1;
            filter->mRemainingDataOutputIndex = output_index;
            stream->mbWaitFirstKeyframe = 0;
            ret = GST_FLOW_OK;
            goto ENCODE_EXIT;

          } else {
            GST_ERROR ("do not find vps, sps, pps?\n");
          }
        }

      } else if (stream->mbWaitFirstKeyframe) {
        GST_INFO ("stream[%d] skip till key frame\n", output_index);
        ret = GST_FLOW_CUSTOM_SUCCESS_1;
        goto ENCODE_EXIT;
      }

      out_buffer->mBufferType = EBufferType_VideoES;
      out_buffer->mpCustomizedContent = NULL;
      out_buffer->mCustomizedContentType = EBufferCustomizedContentType_Invalid;

      out_buffer->mVideoWidth = stream->mVideoWidth;
      out_buffer->mVideoHeight = stream->mVideoHeight;
      out_buffer->mVideoBitrate = stream->mBitrate;
      out_buffer->mVideoFrameRateNum = stream->mFramerateNum;
      out_buffer->mVideoFrameRateDen = stream->mFramerateDen;
      out_buffer->mVideoRoughFrameRate = stream->mFramerate;
      out_buffer->mVideoFrameType = stream->mFrameType;

      if (is_first_slice) {
        if (EPredefinedPictureType_B == param.hint_frame_type) {
          filter->mLastDTS += stream->mFramerateDen;

        } else {
          filter->mLastDTS = param.pts;
        }
      }

      skip_size = gstSkipSEIHEVC (p, data_size);

      if ( (!filter->mbSkipSEI) && skip_size) {
        out_buffer->mpSEI = p;
        out_buffer->mSEISize = skip_size;
      }

      p += skip_size;
      data_size -= skip_size;

      out_buffer->mpData = p;
      out_buffer->mDataSize = data_size;

      out_buffer->mPTS = (glong) param.pts;
      out_buffer->mNativePTS = (glong) param.pts;
      out_buffer->mLinearPTS = (glong) param.pts;

      out_buffer->mDTS = filter->mLastDTS;
      out_buffer->mNativeDTS = filter->mLastDTS;
      out_buffer->mLinearDTS = filter->mLastDTS;

      current_remaining = 0;
      all_cached_frames = 0;

    } else if (StreamFormat_JPEG == stream->mCodecFormat) {

      p = (guchar *) filter->mMapBSB.base + param.offset;
      data_size = param.size;

      if (G_UNLIKELY ( (EJPEG_MarkerPrefix != p[0]) || (EJPEG_SOI != p[1]) || (EJPEG_MarkerPrefix != p[2]) || (128 > data_size) ) ) {
        GST_ERROR ("not find mjpeg header, or invalid data size %d\n", data_size);
        ret = GST_FLOW_CUSTOM_ERROR;
        goto ENCODE_EXIT;
      }

      out_buffer->mbDataSegment = 0;
      stream->mbWaitFirstKeyframe = 0;

      if (!stream->mbAlreadySendSyncBuffer) {
        stream->mbAlreadySendSyncBuffer = 1;
        stream->mVideoWidth = param.video_width;
        stream->mVideoHeight = param.video_height;
      }

      out_buffer->mVideoWidth = param.video_width;
      out_buffer->mVideoHeight = param.video_height;

      out_buffer->mVideoBitrate = stream->mBitrate;
      out_buffer->mVideoFrameRateNum = stream->mFramerateNum;
      out_buffer->mVideoFrameRateDen = stream->mFramerateDen;
      out_buffer->mVideoRoughFrameRate = stream->mFramerate;
      out_buffer->mVideoFrameType = stream->mFrameType;

      out_buffer->mPTS = (glong) param.pts;
      out_buffer->mNativePTS = (glong) param.pts;
      out_buffer->mLinearPTS = (glong) param.pts;

      out_buffer->mDTS = (glong) param.pts;
      out_buffer->mNativeDTS = (glong) param.pts;
      out_buffer->mLinearDTS = (glong) param.pts;

      out_buffer->mBufferType = EBufferType_VideoES;
      out_buffer->mpCustomizedContent = NULL;
      out_buffer->mCustomizedContentType = EBufferCustomizedContentType_Invalid;

      out_buffer->mpData = p;
      out_buffer->mDataSize = (gulong) data_size;

    } else {
      GST_ERROR ("not h264 or h265 or mjpeg bitstream\n");
      ret = GST_FLOW_CUSTOM_ERROR;
      goto ENCODE_EXIT;
    }

  } else {
    GST_ERROR ("error\n");
    ret = GST_FLOW_CUSTOM_ERROR;
    goto ENCODE_EXIT;
  }

ENCODE_EXIT:

  if (filter->mfDSPAL.f_release_bitstream && release_param.framedesc != NULL) {
    filter->mfDSPAL.f_release_bitstream (filter->mIavFd, &release_param);
  }

  return ret;
}

static GstBuffer * __alloc_buffer(unsigned char * data, unsigned int size,
  void * shared_stream_info)
{
  GstBuffer *buf;
  GstMemory *mem;

  buf = gst_buffer_new ();
  mem = amba_direct_mem_alloc_user_data_0 (data, size, 0, shared_stream_info);
  if ((!buf) || (!mem)) {
    if (buf) {
      gst_buffer_unref (buf);
    }
    if (mem) {
      g_slice_free(mem);
    }
    GST_ERROR ("no memory");
    return NULL;
  }

  gst_buffer_append_memory (buf, mem);

  return buf;
}

static GstFlowReturn gst_amba_venc_create (GstPushSrc * psrc,
    GstBuffer ** outbuf)
{
  GstFlowReturn flow_ret = GST_FLOW_OK;
  GstAmbaVenc * filter = GST_AMBAVENC (psrc);
  iav_ctx_t * iav_ctx = filter->iav_ctx;
  iav_al_t * iav_al = &iav_ctx->iav_al;
  int ret = 0;

  guchar *p_cur;
  gssize cur_size;
  guint stream_idx = 0;

  video_bs_state_t * cur_bs_state;
  GstPad * cur_src_pad;

  GstBuffer * out_buffer;

  unsigned char * p_tmp;
  unsigned char nal_type;
  unsigned char is_first_slice;

  amba_dsp_read_bitstream_t read_bs;

  do {

    ret = iav_al->f_is_ready_for_read_bitstream(iav_ctx->iav_fd);
    if (!ret) {
      GST_ERROR("encoder not started");
      flow_ret = GST_FLOW_ERROR;
      break;
    }

    read_bs.stream_idx = 0xffffffff;
    read_bs.timeout_ms = 1000;
    ret = iav_ctx->iav_al.f_read_bitstream(iav_ctx->iav_fd, &read_bs);
    if (ret) {
      GST_ERROR("read_bitstream failed");
      flow_ret = GST_FLOW_ERROR;
      break;
    }

    // check the stream_id
    if (read_bs.stream_idx >= IAV_STREAM_MAX_NUM_ALL) {
      GST_ERROR("read_bs.stream_id %d exceed max %d",
        read_bs.stream_idx, IAV_STREAM_MAX_NUM_ALL);
      flow_ret = GST_FLOW_ERROR;
      iav_al->f_release_bitstream(iav_ctx->iav_fd, &read_bs);
      break;
    }

    // stream index
    stream_idx = read_bs.stream_idx;

    // state and src pad for this stream id
    cur_bs_state = &filter->bs_states[stream_idx];
    cur_src_pad = filter->src_pads[stream_idx];

    // bitstream
    p_cur = iav_ctx->map_bsb.base + read_bs.offset;
    cur_size = read_bs.size;

    // check stream format
    if (StreamFormat_Invalid == cur_bs_state->codec_format) {
      cur_bs_state->codec_format = read_bs->stream_format;
    } else {
      if (read_bs->stream_format != cur_bs_state->codec_format) {
        GST_ERROR("stream_format[%d] not match: 0x%02x, 0x%02x\n",
          stream_idx, read_bs->stream_format, cur_bs_state->codec_format);
        flow_ret = GST_FLOW_ERROR;
        break;
      }
    }

    // check avc and hevc
    if (StreamFormat_H264 == read_bs->stream_format) {

      // check slice type
      p_tmp = nalu_find_first_avc_slice_header_type(p_cur, cur_size, &nal_type);
      if (!p_tmp) {
        GST_ERROR("not found h264 slice header, stream_idx %d", stream_idx);
        flow_ret = GST_FLOW_ERROR;
        break;
      }

      // wait key frame
      if (!cur_bs_state->key_frame_comes) {
        if (ENalType_IDR != nal_type) {
          GST_DEBUG("stream [%d] wait key frame\n", stream_idx);
          iav_al->f_release_bitstream(iav_ctx->iav_fd, &read_bs);
          continue;
        }
        GST_DEBUG("stream [%d] key frame comes\n", stream_idx);
        cur_bs_state->key_frame_comes = 1;
      }

      // add pad if needed
      if (!cur_src_pad) {
        filter->src_pads[stream_idx] =
          gst_amba_venc_add_pad (filter, &src_avc_template, NULL,
            gs_src_pad_names[stream_idx]);
        cur_src_pad = filter->src_pads[stream_idx];
      }

      out_buffer = __alloc_buffer(p_cur, cur_size);

      flow_ret = gst_pad_push(filter->src_pads[stream_idx], out_buffer);
      if (flow_ret != GST_FLOW_OK) {
        GST_ERROR("Failed to push buffer to src pad [%d]", stream_idx);
        gst_buffer_unref(out_buffer);
        out_buffer = NULL;
        break;
      }

    } else if (StreamFormat_H265 == read_bs->stream_format) {

      // check slice type
      p_tmp = nalu_find_first_hevc_slice_header_type(p_cur, cur_size,
        &nal_type, &is_first_slice);
      if (!p_tmp) {
        GST_ERROR("not found h265 slice header, stream_idx %d", stream_idx);
        flow_ret = GST_FLOW_ERROR;
        break;
      }

      // wait key frame
      if (!cur_bs_state->key_frame_comes) {
        if ( ( (EHEVCNalType_IDR_W_RADL != nal_type) && (EHEVCNalType_IDR_N_LP != nal_type) )
          || (!is_first_slice) ) {
          GST_DEBUG("stream [%d] wait key frame\n", stream_idx);
          iav_al->f_release_bitstream(iav_ctx->iav_fd, &read_bs);
          continue;
        }
        GST_DEBUG("stream [%d] key frame comes\n", stream_idx);
        cur_bs_state->key_frame_comes = 1;
      }

      // add pad if needed
      if (!cur_src_pad) {
        filter->src_pads[stream_idx] =
          gst_amba_venc_add_pad (filter, &src_hevc_template, NULL,
            gs_src_pad_names[stream_idx]);
        cur_src_pad = filter->src_pads[stream_idx];
      }

      out_buffer = __alloc_buffer(p_cur, cur_size);

      flow_ret = gst_pad_push(filter->src_pads[stream_idx], out_buffer);
      if (flow_ret != GST_FLOW_OK) {
        GST_ERROR("Failed to push buffer to src pad [%d]", stream_idx);
        gst_buffer_unref(out_buffer);
        out_buffer = NULL;
        break;
      }

    }

  } while (0);

  iav_al->f_release_bitstream(iav_ctx->iav_fd, &read_bs);

  return flow_ret;
}

static GstStateChangeReturn gst_amba_venc_change_state (GstElement *element, GstStateChange transition)
{
  GstAmbaVenc *ambavenc = GST_AMBAVENC (element);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY: {
      quit_video_query_loop();
      gst_amba_venc_remove_pads (ambavenc);
    }
    break;

    default: {
      break;
    }
  }

  return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
}

