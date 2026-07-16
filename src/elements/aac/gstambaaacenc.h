/*
 * gstambaaacenc.h
 *
 * History:
 *    5/21/2025 - [pxduan] created file
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
 * SECTION:element-amba_aac_enc
 * @title: amba_aac_enc
 * @see_also: amba_aac_dec
 *
 * amba_aac_enc encodes raw audio to AAC (MPEG-4 part 3) streams.
 *
 */

#ifndef __GST_AMBAAACENC_H__
#define __GST_AMBAAACENC_H__

#include <gst/gst.h>
#include <gst/audio/gstaudioencoder.h>

#include "new_aac_audio_enc.h"
#include "amba_audio_define.h"

G_BEGIN_DECLS

enum AAC_QUANTIZER_QUALITY
{
  QUANTIZER_QUALITY_LOW     = 0,
  QUANTIZER_QUALITY_HIGH    = 1,
  QUANTIZER_QUALITY_HIGHEST = 2,
};

enum AAC_TRANS_FORMAT
{
  ADIF = 'a',
  LOAS = 'l',
  MP4FILE = 'm',
  RAW  = 'r',
  ADTS = 't',
};

#define AAC_PERCEPTUAL_MODE_NORMAL "normal"
#define AAC_PERCEPTUAL_MODE_FINE "fine"
#define AAC_PERCEPTUAL_MODE_FINEST "finest"

#define AAC_QUANTIZER_QUALITY_LOW "low"
#define AAC_QUANTIZER_QUALITY_HIGH "high"
#define AAC_QUANTIZER_QUALITY_HIGHEST "highest"

#define AAC_FORMAT_AAC "aac"
#define AAC_FORMAT_AACPLUS "aacplus"
#define AAC_FORMAT_AACPLUS_PS "aacplusps"

typedef struct _GstAmbaAacEnc GstAmbaAacEnc;
typedef struct _GstAmbaAacEncClass GstAmbaAacEncClass;

struct _GstAmbaAacEnc
{
  GstAudioEncoder parent;

  gboolean opened;
  gboolean need_reopen;

  gboolean needs_reorder;

  amba_audio_info_t m_src_audio_info;

  gchar sample_format_str[128];
  gchar channel_str[128];
  gchar aac_format_str[128];
  gchar perceptual_mode_str[128];
  gchar quantizer_quality_str[128];

  GstAudioFormat sample_format;     //!<Audio sample format
  gint samples;
  gint mpegversion;

  guint bitrate;
  guint tns;
  guint pns;
  guint crc;
  guchar quantizer_quality;
  guchar format;
  guchar perceptual_mode;
  gchar fftype;

  au_aacenc_config_t m_enc_conf;

  guchar *m_enc_buffer;
  guint m_enc_buffer_size;

  guchar *m_enc_inter_buffer;
  guint m_enc_inter_buf_size;

  guchar m_is_initialized;
  guchar reserved[3];
};

struct _GstAmbaAacEncClass
{
  GstAudioEncoderClass parent_class;
};

GType gst_amba_aac_enc_get_type (void);

#define GST_TYPE_AMBA_AAC_ENC \
  (gst_amba_aac_enc_get_type())
#define GST_AMBA_AAC_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AMBA_AAC_ENC,GstAmbaAacEnc))
#define GST_AMBA_AAC_ENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AMBA_AAC_ENC,GstAmbaAacEncClass))
#define GST_IS_AMBA_AAC_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AMBA_AAC_ENC))
#define GST_IS_AMBA_AAC_ENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AMBA_AAC_ENC))

G_END_DECLS

#endif /* __GST_FFMPEGAUDENC_H__ */
