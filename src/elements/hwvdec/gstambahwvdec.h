/*
 * GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2020 Niels De Graef <niels.degraef@gmail.com>
 * Copyright (C) 2022 PengXue Duan <<user@hostname.org>>
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

#ifndef __GST_AMBAHWVDEC_H__
#define __GST_AMBAHWVDEC_H__

#include <gst/video/video.h>
#include <gst/video/gstvideodecoder.h>

#include "gstambahwclock.h"

#define DMAX_VOUT_STRING_LEN 32
#define DECODER_BSB_ALIGN_SIZE			(1 << 13)

#define D_SIMPLE_VERSION

G_BEGIN_DECLS

#define GST_TYPE_AMBAHWVDEC (gst_amba_hwvdec_get_type())
#define GST_AMBAHWVDEC(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AMBAHWVDEC,GstAmbaHwvdec))
#define GST_AMBAHWVDEC_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AMBAHWVDEC,GstAmbaHwvdecClass))
#define GST_AMBAHWVDEC_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_AMBAHWVDEC,GstAmbaHwvdecClass))
#define GST_IS_AMBAHWVDEC(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AMBAHWVDEC))
#define GST_IS_AMBAHWVDEC_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AMBAHWVDEC))

typedef enum {
  EDecoderMode_Invalid = 0x00,
  EDecoderMode_Normal = 0x01,
  EDecoderMode_Direct = 0x02,
} EDecoderMode;

typedef struct _GstAmbaHwvdec GstAmbaHwvdec;
typedef struct _GstAmbaHwvdecClass GstAmbaHwvdecClass;

typedef gboolean (*tf_vd_sink_event) (GstVideoDecoder *decoder,
  GstEvent *event);

typedef struct {
  unsigned int x, y, w, h;
} SRect;

typedef struct {
  char mode_string[DMAX_VOUT_STRING_LEN];
  char sinktype_string[DMAX_VOUT_STRING_LEN];
  char device_string[DMAX_VOUT_STRING_LEN];

  guchar b_digital_vout;
  guchar b_hdmi_vout;
  guchar b_cvbs_vout;

  guchar vout_id;
} SConfigVout;

typedef struct {
  guchar layers_map; /*!< bit map for pyramid layers, one bit for one layer */
  guchar reserved[3];

  //pyramid
  void *ext_buf_addr;
  guint ext_buf_size;

  EDSPPyramidScale scale_type; /*!< pyramid scale type, @sa EDSPPyramidScale */
  SRect crop_win[DDSP_MAX_PYRAMID_LAYERS]; /*!< Cropping window in each layer output coordinate */

  guint layer1_width; /*!< Rescale size for pyramid layer 1, valid for arbitrary scale, 1/2 for next layers */
  guint layer1_height;
} SDSPPyramidConfig;

typedef struct {
  guint size;
  guint offset;
  guint sc_offset;
  gboolean valid;

  guint8 start_code[4];

  guchar type;
  guchar sc_length;
  guchar reserved[2];

  guint8 *data;

} H264NalUnit;

typedef struct {
  guint size;
  guint offset;
  guint sc_offset;
  gboolean valid;

  guint8 start_code[4];

  guchar type;
  guchar is_first_slice;
  guchar sc_length;
  guchar reserved[1];

  guint8 *data;
} H265NalUnit;

/* Per-decoder context structure */
typedef struct {
  /* Bitstream buffer pointers */
  guchar *mpBitSreamBufferStart;
  guchar *mpBitSreamBufferEnd;
  guchar *mpBitStreamBufferCurPtr;

  /* H265 frame tracking */
  guchar *mH265FrameStartPtr;
  guint mH265FramePTS;

  /* Decode command context */
  amba_dsp_decode_t mDecCmdCtx;

  /* State flags */
  guchar b_1st_frame;
  guchar b_dec_1_frame_done;
  guchar is_first_frame;
  guchar mbStopCmdSent;
  guchar mbGetBasePTS;

  /* PTS and frame tracking */
  glong mBasePTS;
  guint mFrameCount;
  GstClockTime provided_pts;

  /* Output dimensions */
  guint width;
  guint height;

  /* Extradata buffers */
  guchar extradata_buf[256];
  guchar *p_cur_extradata;
  guint extradata_size;
  guint vps_size;
  guint sps_size;
  guint pps_size;

  /* GOP header */
  guchar mpAmbaGopHeader[DAMBA_MAX_GOP_HEADER_LENGTH];

  /* Output state per decoder */
  GstVideoCodecState *output_state;
} GstAmbaHwvdecDecoderCtx;

struct _GstAmbaHwvdec
{
  /*< private >*/
  GstVideoDecoder parent;

  /*< protected >*/
  GstVideoCodecState * input_state;
  GstVideoCodecState * output_state;  // Legacy: kept for backward compatibility, use decoder_ctx[].output_state

  // simple version
  iav_ctx_t * iav_ctx;
  guint silent;

  guchar *codec_data;
  gulong codecdata_size;

  guint nal_length_size;

  /* used for low-latency vs. high throughput mode decision */
  gboolean is_live;

  stream_codec_info_t mStreamCodecInfo;
  StreamFormat mCodecFormat[DAMBADSP_MAX_DECODER_NUMBER];

  amba_dsp_decoder_info_t mDecoderInfo[DAMBADSP_MAX_DECODER_NUMBER];

  guint mCapMaxCodedWidth[DAMBADSP_MAX_DECODER_NUMBER];
  guint mCapMaxCodedHeight[DAMBADSP_MAX_DECODER_NUMBER];

  /* Per-decoder contexts */
  GstAmbaHwvdecDecoderCtx decoder_ctx[DAMBADSP_MAX_DECODER_NUMBER];

  guchar mbAddAmbaGopHeader;
  guchar mDecId;  // Current active decoder ID
  guchar mbBWplayback;
  guchar mFeedingRule;

  guchar mbDiscardCurrentGOP;
  guchar mbGopBasedFeed;
  guchar mbAutoMapBSB;

  guchar mbSendDecodeReadyMsg;
  guchar mbHEVCPerTile; //some rtsp server send per tile, not per frame
  guchar mTileIndex;
  guchar mTileNum;

  guchar mbExitDecodeMode;
  guchar mbSupportAllframeBackwardPlayback;
  guchar mMaxGopSize;
  guchar mCurGopSize;

  guint mSpecifiedTimeScale;
  guint mSpecifiedFrameTick;

  guint mFrameRateNum;
  guint mFrameRateDen;
  guint mFrameRate;

  guchar mbDumpBitstream;
  guchar mbDumpOnly;
  gushort mDumpIndex;
  void *mpDumper;

  guchar vout_number;
  guchar pyramid_number;
  guchar decoder_number;

  guchar decoder_set_id;
  guchar vout_set_id;
  guchar b_support_ff_fb_bw;
  guchar b_setup_ctx_done;

  guchar vout_id[DAMBADSP_MAX_DECODER_NUMBER];
  guchar pyramid_id[DAMBADSP_MAX_DECODER_NUMBER];
  guchar enable_pb_pyramid[DAMBADSP_MAX_DECODER_NUMBER];
  guchar enable_vout[DAMBADSP_MAX_DECODER_NUMBER];
  guchar is_decoder_created[DAMBADSP_MAX_DECODER_NUMBER];

  guint max_vout0_width;
  guint max_vout0_height;
  guint max_vout1_width;
  guint max_vout1_height;

  SConfigVout vout_configs[DAMBADSP_MAX_VOUT_NUMBER];
  SDSPPyramidConfig pb_pyramid_configs[DAMBADSP_MAX_DECODER_NUMBER];

  GstClockTime hwtimer_outfreq;
};

struct _GstAmbaHwvdecClass
{
  /*< private >*/
  GstVideoDecoderClass parent_class;

  tf_vd_sink_event f_parent_sink_event;
};

GType gst_amba_hwvdec_get_type(void);


G_END_DECLS

#endif /* __GST_AMBAHWVDEC_H__ */
