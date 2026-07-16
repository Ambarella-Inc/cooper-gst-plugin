/*
 * gstambaaacdec.h
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
 * SECTION:element-amba_aac_dec
 * @title: amba_aac_dec
 * @see_also: amba_aac_enc
 *
 * amba_aac_dec decodes AAC (MPEG-4 part 3) stream.
 */

#ifndef __GST_AMBAAACDEC_H__
#define __GST_AMBAAACDEC_H__

#include <gst/gst.h>
#include <gst/audio/gstaudiodecoder.h>

#include "new_aac_audio_dec.h"
#include "amba_audio_define.h"

G_BEGIN_DECLS

typedef struct {
  gint format;
  char message[128];
} aac_stream_format_t;

typedef struct _GstAmbaAacDec {
  GstAudioDecoder parent;

  gint      samplerate; /* sample rate of the last MPEG frame    */
  gint      channels;   /* number of channels of the last frame  */
  guint      bps;        /* bytes per sample                      */
  //guchar    *channel_positions;
  GstAudioChannelPosition aac_positions[6], gst_positions[6];
  gboolean   need_reorder;
  gint       reorder_map[64];

  gint stream_fmt;
  char stream_fmt_info[128]; //!<Audio stream format information

  gint downsampled_sbr;
  gint downmix;

  au_aacdec_config_t m_dec_conf;

  guint *m_dec_buffer;
  guint m_dec_buffer_size;

  gint *m_dec_out_buffer;
  guint m_dec_out_buf_size;

  guchar m_is_initialized;
  guchar reserved[3];
} GstAmbaAacDec;

typedef struct _GstAmbaAacDecClass {
  GstAudioDecoderClass parent_class;
} GstAmbaAacDecClass;

GType gst_amba_aac_dec_get_type (void);

#define GST_TYPE_AMBA_AAC_DEC \
  (gst_amba_aac_dec_get_type ())
#define GST_AMBA_AAC_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_AMBA_AAC_DEC, GstAmbaAacDec))
#define GST_AMBA_AAC_DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_AMBA_AAC_DEC, GstAmbaAacDecClass))
#define GST_IS_AMBA_AAC_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_AMBA_AAC_DEC))
#define GST_IS_AMBA_AAC_DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_AMBA_AAC_DEC))


G_END_DECLS

#endif /* __GST_FAAD_H__ */
