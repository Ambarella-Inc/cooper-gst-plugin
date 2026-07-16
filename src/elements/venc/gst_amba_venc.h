/*
 * gst_amba_venc.h
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

#ifndef __GST_AMBAVENC_H__
#define __GST_AMBAVENC_H__

#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>

G_BEGIN_DECLS

#define GST_TYPE_AMBAVENC \
  (gst_amba_venc_get_type())
#define GST_AMBAVENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AMBAVENC,GstAmbaVenc))
#define GST_AMBAVENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AMBAVENC,GstAmbaVencClass))
#define GST_IS_AMBAVENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AMBAVENC))
#define GST_IS_AMBAVENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AMBAVENC))

typedef struct _GstAmbaVenc GstAmbaVenc;
typedef struct _GstAmbaVencClass GstAmbaVencClass;


typedef struct {
  StreamFormat mCodecFormat;

  guint mBitrate;

  guint mVideoWidth;
  guint mVideoHeight;
  guint mFramerateNum;
  guint mFramerateDen;
  gfloat mFramerate;
  guint mFrameType;

  guchar mbWaitFirstKeyframe;
  guchar mbAlreadySendSyncBuffer;
  guchar reserved[2];

  guchar *mpVideoExtraData;
  guint mVideoExtraDataSize;

} amba_stream_params_t;

typedef enum {
  EBufferType_Invalid = 0,
  EBufferType_VideoES,
  EBufferType_VideoExtraData,
  EBufferType_AudioES,
  EBufferType_AudioExtraData,
  EBufferType_VideoFrameBuffer,
  EBufferType_AudioPCMBuffer,
  EBufferType_AudioFrameBuffer,
  EBufferType_PrivData,

  EBufferType_FlowControl_EOS,
  EBufferType_FlowControl_Pause,
  EBufferType_FlowControl_Resume,
  EBufferType_FlowControl_FinalizeFile,
  EBufferType_FlowControl_PassParameters,

} EBufferType;

typedef enum {
  EBufferCustomizedContentType_Invalid = 0,
  EBufferCustomizedContentType_RingBuffer,
  EBufferCustomizedContentType_FFMpegAVPacket,
  EBufferCustomizedContentType_AllocateByFilter,
  EBufferCustomizedContentType_V4l2DirectBuffer,
} EBufferCustomizedContentType;

typedef struct {
  EBufferType     mBufferType;
  guchar mVideoFrameType, mbAudioPCMChannelInterlave, mAudioChannelNumber, mAudioSampleFormat;
  guchar mbAllocated, mbExtEdge, mContentFormat, mbDataSegment;
  guint   mCustomizedContentFormat;

  //for customized field
  EBufferCustomizedContentType mCustomizedContentType;
  void *mpCustomizedContent;

  //memory
  guchar *mpData;
  gulong    mDataSize;

  guint mAudioBitrate;
  guint mVideoBitrate;

  gushort mMaxVideoGopSize;
  gushort mCurVideoGopSize;

  //us
  glong mPTS;
  glong mDTS;
  glong mNativePTS;
  glong mNativeDTS;
  glong mLinearPTS;
  glong mLinearDTS;

  //video related
  TU32 mVideoWidth, mVideoHeight;
  TU32 mExtVideoWidth, mExtVideoHeight;
  TU32 mVideoFrameRateNum, mVideoFrameRateDen;
  TU32 mVideoBufferLinesize[MEMORY_BLOCK_NUMBER];
  TU32 mVideoOffsetX, mVideoOffsetY;
  TU32 mVideoSampleAspectRatioNum;
  TU32 mVideoSampleAspectRatioDen;
  TU32 mVideoProfileIndicator;
  TU32 mVideoLevelIndicator;

  gfloat mVideoRoughFrameRate;

  guchar *mpSEI;
  guint mSEISize;

} amba_venc_buffer;

typedef struct {
  GstPushSrc pushsrc;

  iav_ctx_t * iav_ctx;

  video_bs_state_t bs_states[IAV_STREAM_MAX_NUM_ALL];
  GstPad * src_pads[IAV_STREAM_MAX_NUM_ALL];

  guint parse_nalu : 1;
  guint reserved0 : 31;
}  _GstAmbaVenc;

typedef struct {
  GstPushSrcClass parent_class;
}  _GstAmbaVencClass;

GType gst_amba_venc_get_type (void);



G_END_DECLS

#endif /* __GST_AMBAVIDEO_H__ */

