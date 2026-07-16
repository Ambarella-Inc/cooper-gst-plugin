/*
 * gstambahwvdecv2.h
 *
 * History:
 *    4/6/2026 - [Dashun Pei] created file
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
 * SECTION:element-gstambahwvdecv2
 * @title: gstambahwvdecv2
 *
 * Use amba hardware decoding, then query yuv from iav canvas
 * GstVideoDecoder -> video/x-raw NV12 Cavalry pool (IAV BSB + canvas + GDMA dmabuf)
 * Output: video/x-raw (NV12).
 */

#ifndef __GST_AMBA_HWVDECV2_H__
#define __GST_AMBA_HWVDECV2_H__

#include <gst/video/gstvideodecoder.h>

#include "iav_al.h"
#include "iav_ctx.h"

G_BEGIN_DECLS

#define GST_TYPE_AMBA_HWVDECV2 (gst_amba_hwvdecv2_get_type ())
#define GST_AMBA_HWVDECV2(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_AMBA_HWVDECV2, GstAmbaHwvdecV2))
#define GST_AMBA_HWVDECV2_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_AMBA_HWVDECV2, GstAmbaHwvdecV2Class))
#define GST_IS_AMBA_HWVDECV2(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_AMBA_HWVDECV2))
#define GST_IS_AMBA_HWVDECV2_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_AMBA_HWVDECV2))

/* Default max coded WxH when caps are still 0x0 (byte-stream before SPS). Smaller than
 * 1080p shrinks contiguous Cavalry pool (max_buffers * NV12 size). Must match prepare. */
#define GST_AMBA_HWVDECV2_DEFAULT_CODED_WIDTH 1920
#define GST_AMBA_HWVDECV2_DEFAULT_CODED_HEIGHT 1088

typedef struct _GstAmbaHwvdecV2 GstAmbaHwvdecV2;
typedef struct _GstAmbaHwvdecV2Class GstAmbaHwvdecV2Class;

struct _GstAmbaHwvdecV2 {
  GstVideoDecoder parent;

  iav_ctx_t *iav_ctx;
  gboolean iav_ctx_acquired;
  struct iav_querydesc *query_desc;

  guint hwtimer_outfreq;
  gboolean mbAutoMapBSB;
  gboolean iav_pipeline_ready;

  guint hw_decoder_id;
  guint canvas_id;
  guint max_coded_width;
  guint max_coded_height;
  guint mFrameRateNum;
  guint mFrameRateDen;
  gboolean mbAddAmbaGopHeader;

  amba_dsp_decoder_info_t mDecoderInfo;

  guchar *mpBitSreamBufferStart;
  guchar *mpBitSreamBufferEnd;
  guchar *mpBitStreamBufferCurPtr;

  amba_dsp_decode_t mDecCmdCtx;

  gboolean is_decoder_created;
  gboolean b_1st_frame;
  guint mCurGopSize;

  guchar extradata_buf[256];
  guchar *p_cur_extradata;
  guint extradata_size;
  guint sps_size;
  guint pps_size;
  guchar mpAmbaGopHeader[DAMBA_MAX_GOP_HEADER_LENGTH];

  guint32 last_canvas_seq;
  gboolean canvas_init_done;
  gboolean canvas_yuv_disabled;

  /* IAV canvas: cap max (ioctl) + live query; drive NV12 pitch and pad caps WxH. */
  gboolean canvas_cap_valid;
  guint canvas_cap_w;
  guint canvas_cap_h;
  gboolean canvas_out_valid;
  guint canvas_out_w;
  guint canvas_out_h;
  /* Last IAV canvas Y line pitch (yuv->pitch) before GDMA; 0 until first picture. */
  guint last_canvas_src_pitch;

  GstBufferPool *out_pool;

  guint width;
  guint height;
  guint32 frame_id_seq;
  guint pool_slot_counter;
  gsize nv12_buffer_size;
  guint nv12_y_pitch;

  guint8 dec_id;
  guint8 slave_id;
  guint num_decoders;
  gboolean cavalry_phys_alloc;
  guint64 phys_base;

  gboolean contiguous_pool;
  guint alloc_nv12_width;
  guint alloc_nv12_height;

  gboolean alloc_preset_valid;
  guint alloc_preset_w;
  guint alloc_preset_h;

  gchar *dump_nv12_dir;
  gint dump_nv12_frame_id;
  guint dump_nv12_num_frames;

  guint codec_format;
  guint nal_length_size;
  guint mSpecifiedTimeScale;
  guint mSpecifiedFrameTick;
  guint mFrameCount;

  gboolean verbose;
};

struct _GstAmbaHwvdecV2Class {
  GstVideoDecoderClass parent_class;
};

GType gst_amba_hwvdecv2_get_type (void);

/* IAV path updates canvas_out_* then refreshes NV12 pitch before GDMA. */
void gst_amba_hwvdecv2_refresh_nv12_size (GstAmbaHwvdecV2 * self);

G_END_DECLS

#endif /* __GST_AMBA_HWVDECV2_H__ */
