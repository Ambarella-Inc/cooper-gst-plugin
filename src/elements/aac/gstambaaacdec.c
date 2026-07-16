/*
 * gstambaaacdec.c
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
 *
 * ## Example launch lines
 * |[
 * gst-launch-1.0 filesrc location=example.mp4 ! qtdemux ! amba_aac_dec ! audioconvert ! audioresample ! wavenc ! filesink location=/tmp/example.wav
 * ]| Decode aac from mp4 file and encode to wav file.
 * |[
 * gst-launch-1.0 filesrc location=example.adts ! aacparse ! amba_aac_dec ! audioconvert ! audioresample ! lamemp3enc ! filesink location=/tmp/example.mp3
 * ]| Decode standalone aac bitstream and encode to mp3 file.
 *
 */


#include <string.h>
#include <gst/audio/audio.h>
#include <gst/pbutils/codec-utils.h>

#include "internal.h"
#include "gstambaaacdec.h"

#define DEFAULT_DEC_OUT_BUF_SIZE (16384)

GST_DEBUG_CATEGORY_STATIC (gst_amba_aac_dec_debug);
#define GST_CAT_DEFAULT gst_amba_aac_dec_debug

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, "
        "mpegversion = (int) {2, 4}, "
        "stream-format = (string) { raw, adts, adif, loas }")
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    /* cppcheck-suppress unknownMacro */
    GST_STATIC_CAPS ("audio/x-raw, "
        "format = (string) "GST_AUDIO_NE(S16)", "
        "layout = (string) interleaved, "
        "rate = (int) [ 8000, 96000 ], "
        "channels = (int) [ 1, 5 ]")
    );

enum
{
  PROP_0,
  PROP_DOWNSAMPLE_SBR,
  PROP_DOWNMIX,
};

static void gst_amba_aac_dec_reset (GstAmbaAacDec * amdec);
static gboolean gst_amba_aac_dec_open_decoder (GstAmbaAacDec * amdec);
static void gst_amba_aac_dec_close_decoder (GstAmbaAacDec * amdec);

static gboolean gst_amba_aac_dec_start (GstAudioDecoder * dec);
static gboolean gst_amba_aac_dec_stop (GstAudioDecoder * dec);
static gboolean gst_amba_aac_dec_set_format (GstAudioDecoder * dec, GstCaps * caps);
static GstFlowReturn gst_amba_aac_dec_handle_frame (GstAudioDecoder * dec,
    GstBuffer * buffer);
static void gst_amba_aac_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_amba_aac_dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);


#define gst_amba_aac_dec_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstAmbaAacDec, gst_amba_aac_dec, GST_TYPE_AUDIO_DECODER,
   GST_DEBUG_CATEGORY_INIT (gst_amba_aac_dec_debug, "ambaaacdec", 0,
       "Amba AAC decoding"));

static aac_status_msg_t decoder_status[] =
{
  {AAC_DECODE_OK, "OK"},
  {AAC_DECODE_UNSUPPORTED_FORMAT, "Unsupported format"},
  {AAC_DECODE_DECODE_FRAME_ERROR, "Decode frame error"},
  {AAC_DECODE_CRC_CHECK_ERROR, "CRC check error"},
  {AAC_DECODE_INVALID_CODE_BOOK, "Invalid code book"},
  {AAC_DECODE_UNSUPPORTED_WINOW_SHAPE, "Unsupported winow shape"},
  {AAC_DECODE_PREDICTION_NOT_SUPPORTED_IN_LC_AAC,
    "Prediction NOT supported in LC_AAC"
  },
  {AAC_DECODE_UNIMPLEMENTED_CCE, "CCE NOT implemented"},
  {AAC_DECODE_UNIMPLEMENTED_GAIN_CONTROL_DATA,
    "Gain control data NOT implemented"
  },
  {AAC_DECODE_UNIMPLEMENTED_EP_SPECIFIC_CONFIG_PARSE,
    "EP specific config parse NOT implemented"
  },
  {AAC_DECODE_UNIMPLEMENTED_CELP_SPECIFIC_CONFIG_PARSE,
    "CELP specific config parse NOT implemented"
  },
  {AAC_DECODE_UNIMPLEMENTED_HVXC_SPECIFIC_CONFIG_PARSE,
    "HVXC specific config parse NOT implemented"
  },
  {AAC_DECODE_OVERWRITE_BITS_IN_INPUT_BUFFER,
    "Over write bits in input buffer"
  },
  {AAC_DECODE_CANNOT_REACH_BUFFER_FULLNESS,
    "Cannot reach buffer fullness"
  },
  {AAC_DECODE_TNS_RANGE_ERROR, "TNS range error"},
};

static aac_stream_format_t decoder_stream_fmt[] =
{
  {UNKNOW_BSFORMAT, "Unknown Bitstream Format"},
  {ADIF_BSFORMAT, "ADIF"},
  {ADTS_MPEG4_BSFORMAT, "ADTS MPEG4"},
  {ADTS_MPEG2_BSFORMAT, "ADTS MPEG2"},
  {LOAS_BSFORMAT, "LOAS"},
  {RAW_BSFORMAT, "RAW"},
};

static const char* aac_dec_strerror(gint err)
{
  guint len = sizeof(decoder_status) / sizeof(aac_status_msg_t);
  guint i = 0;
  for (i = 0; i < len; ++ i) {
    if (G_LIKELY(decoder_status[i].error == err)) {
      break;
    }
  }

  return (i >= len) ? "Unknown Error" : decoder_status[i].message;
}

static const char* aac_dec_bsformat_to_str(gint fmt)
{
  guint len = sizeof(decoder_stream_fmt) / sizeof(aac_stream_format_t);
  guint i = 0;
  for (i = 0; i < len; ++ i) {
    if (G_LIKELY(decoder_stream_fmt[i].format == fmt)) {
      break;
    }
  }

  return (i >= len) ? decoder_stream_fmt[0].message : decoder_stream_fmt[i].message;
}

static void
gst_amba_aac_dec_class_init (GstAmbaAacDecClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstAudioDecoderClass *base_class = GST_AUDIO_DECODER_CLASS (klass);

  gobject_class->set_property = gst_amba_aac_dec_set_property;
  gobject_class->get_property = gst_amba_aac_dec_get_property;

  gst_element_class_add_static_pad_template (element_class, &src_template);
  gst_element_class_add_static_pad_template (element_class, &sink_template);

  gst_element_class_set_static_metadata (element_class, "AAC audio decoder",
      "Codec/Decoder/Audio",
      "Free MPEG-2/4 AAC decoder",
      "PengXue Duan <pxduan@ambarella.com>");

  base_class->start = GST_DEBUG_FUNCPTR (gst_amba_aac_dec_start);
  base_class->stop = GST_DEBUG_FUNCPTR (gst_amba_aac_dec_stop);
  base_class->set_format = GST_DEBUG_FUNCPTR (gst_amba_aac_dec_set_format);
  base_class->handle_frame = GST_DEBUG_FUNCPTR (gst_amba_aac_dec_handle_frame);

  g_object_class_install_property (gobject_class, PROP_DOWNMIX,
      g_param_spec_int ("downmix", "DownMix", "0: stereo to mono downmix off  1: stereo to mono downmix on.",
          0, 1,
          0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS) );

  g_object_class_install_property (gobject_class, PROP_DOWNSAMPLE_SBR,
      g_param_spec_int ("downsample-sbr", "Downsampled SBR", "0: downsampled sbr off  1: downsampled sbr on.",
          0, 1,
          0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS) );

}

static void
gst_amba_aac_dec_init (GstAmbaAacDec * amdec)
{
  gst_audio_decoder_set_use_default_pad_acceptcaps (GST_AUDIO_DECODER_CAST
      (amdec), TRUE);
  GST_PAD_SET_ACCEPT_TEMPLATE (GST_AUDIO_DECODER_SINK_PAD (amdec));
  gst_amba_aac_dec_reset (amdec);
}

static void
gst_amba_aac_dec_reset (GstAmbaAacDec * amdec)
{
  amdec->samplerate = 0;
  amdec->channels = 0;
  amdec->m_dec_out_buf_size = DEFAULT_DEC_OUT_BUF_SIZE;
  amdec->downsampled_sbr  = 0;
  amdec->downmix = 0;

}

static gboolean
gst_amba_aac_dec_start (GstAudioDecoder * dec)
{
  //GstAmbaAacDec *amdec = GST_AMBA_AAC_DEC (dec);

  GST_DEBUG_OBJECT (dec, "start");
  //gst_amba_aac_dec_reset (amdec);

  /* call upon legacy upstream byte support (e.g. seeking) */
  gst_audio_decoder_set_estimate_rate (dec, TRUE);
  /* never mind a few errors */
  gst_audio_decoder_set_max_errors (dec, 10);

  return TRUE;
}

static gboolean
gst_amba_aac_dec_stop (GstAudioDecoder * dec)
{
  GstAmbaAacDec *amdec = GST_AMBA_AAC_DEC (dec);

  GST_DEBUG_OBJECT (dec, "stop");
  gst_amba_aac_dec_close_decoder (amdec);
  gst_amba_aac_dec_reset (amdec);

  return TRUE;
}

static gboolean
gst_amba_aac_dec_set_format (GstAudioDecoder * dec, GstCaps * caps)
{
  GstAmbaAacDec *amdec = GST_AMBA_AAC_DEC (dec);
  GstStructure *s = gst_caps_get_structure (caps, 0);
  GstBuffer *buf;
  const GValue *value;
  GstMapInfo map;
  guint8 *cdata;
  gsize csize;
  guint rate_idx = 0;
  const gchar *str = NULL;
  gint i = 4;

  if ((str = gst_structure_get_string (s, "stream-format"))) {
    if (strcmp (str, AAC_TRANSPORT_FMT_ADTS) == 0) {
      strncpy(amdec->stream_fmt_info, AAC_TRANSPORT_FMT_ADTS, 127);
      if (!gst_structure_get_int (s, "mpegversion", &i) || i == 4) {
        amdec->stream_fmt = ADTS_MPEG4_BSFORMAT;
      } else {
        amdec->stream_fmt = ADTS_MPEG2_BSFORMAT;
      }
    } else if (strcmp (str, AAC_TRANSPORT_FMT_RAW) == 0) {
      strncpy(amdec->stream_fmt_info, AAC_TRANSPORT_FMT_RAW, 127);
      amdec->stream_fmt = RAW_BSFORMAT;
    } else if (strcmp (str, AAC_TRANSPORT_FMT_ADIF) == 0) {
      strncpy(amdec->stream_fmt_info, AAC_TRANSPORT_FMT_ADIF, 127);
      amdec->stream_fmt = ADIF_BSFORMAT;
    } else if (strcmp (str, AAC_TRANSPORT_FMT_LOAS) == 0) {
      strncpy(amdec->stream_fmt_info, AAC_TRANSPORT_FMT_LOAS, 127);
      amdec->stream_fmt = LOAS_BSFORMAT;
    } else {
      GST_WARNING_OBJECT (amdec, "unknown stream-format: %s", str);
      strncpy(amdec->stream_fmt_info, AAC_TRANSPORT_FMT_UNKNOWN, 127);
      amdec->stream_fmt = UNKNOW_BSFORMAT;
    }
  } else {
    GST_WARNING_OBJECT (amdec, "no stream-format, default RAW");
    strncpy(amdec->stream_fmt_info, AAC_TRANSPORT_FMT_RAW, 127);
    amdec->stream_fmt = RAW_BSFORMAT;
  }

  if ((value = gst_structure_get_value (s, "codec_data"))) {

    buf = gst_value_get_buffer (value);
    g_return_val_if_fail (buf != NULL, FALSE);

    gst_buffer_map (buf, &map, GST_MAP_READ);
    cdata = map.data;
    csize = map.size;

    if (csize < 2) {
      GST_DEBUG_OBJECT (amdec, "codec_data less than 2 bytes long");
      gst_buffer_unmap (buf, &map);
      return FALSE;
    }

    rate_idx = ((cdata[0] & 0x07) << 1) | ((cdata[1] & 0x80) >> 7);
    amdec->channels = (cdata[1] & 0x78) >> 3;

    amdec->samplerate = gst_codec_utils_aac_get_sample_rate_from_index (rate_idx);

    GST_DEBUG_OBJECT (amdec,
        "codec_data: object_type=%d, sample_rate=%d, channels=%d",
        ((cdata[0] & 0xf8) >> 3),
        amdec->samplerate,
        amdec->channels);

    gst_buffer_unmap (buf, &map);
  } else {
    if (gst_structure_get_int (s, "rate", &amdec->samplerate) &&
        gst_structure_get_int (s, "channels", &amdec->channels)) {
      GST_DEBUG_OBJECT (amdec, "Caps: sample_rate=%d, channels=%d",
          amdec->samplerate, amdec->channels);
    } else {
      GST_WARNING_OBJECT (amdec, "not found sample_rate and channels in caps.");
    }
  }

  if (!gst_amba_aac_dec_open_decoder (amdec)) {
    GST_DEBUG_OBJECT (amdec, "failed to create decoder");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_amba_aac_dec_chanpos_to_gst (GstAmbaAacDec * amdec,
    GstAudioChannelPosition * pos, guint num)
{
  gboolean ret = TRUE;
  switch (num) {
    case 1:
      pos[0] = GST_AUDIO_CHANNEL_POSITION_MONO;
      break;
    case 2:
      pos[0] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
      pos[1] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
      break;
    case 3:
      pos[0] = GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER;
      pos[1] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
      pos[2] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
      break;
    case 4:
      pos[0] = GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER;
      pos[1] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
      pos[2] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
      pos[3] = GST_AUDIO_CHANNEL_POSITION_REAR_CENTER;
      break;
    case 5:
      pos[0] = GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER;
      pos[1] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
      pos[2] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
      pos[3] = GST_AUDIO_CHANNEL_POSITION_REAR_LEFT;
      pos[4] = GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT;
      break;
    default:
      GST_ERROR_OBJECT (amdec, "unknown channel %d.", num);
      ret = FALSE;
      break;
  }
  return ret;
}

static gboolean
gst_amba_aac_dec_update_caps (GstAmbaAacDec * amdec, au_aacdec_config_t * info)
{
  gboolean ret = TRUE;
  gboolean fmt_change = FALSE;
  GstAudioInfo ainfo;
  gint i;
  GstAudioChannelPosition position[6];

  /* see if we need to renegotiate */
  if (info->sample_freq != amdec->samplerate ||
      info->outNumCh != amdec->channels/* || !amdec->channel_positions*/) {
    fmt_change = TRUE;
  }

  if (G_LIKELY (gst_pad_has_current_caps (GST_AUDIO_DECODER_SRC_PAD (amdec))
      && !fmt_change)) {
    return TRUE;
  }

  /* store new negotiation information */
  //if (G_UNLIKELY (1 == amdec->m_dec_conf.frameCounter)) {
    /* This is the sample rate of decoded PCM audio data */
  amdec->samplerate = info->sample_freq;
  amdec->channels = info->outNumCh;
  //}

  if (!gst_amba_aac_dec_chanpos_to_gst (amdec,
      amdec->aac_positions, amdec->channels)) {
    GST_ERROR_OBJECT (amdec, "Could not map channel positions");
    return FALSE;
  }

  memcpy (position, amdec->aac_positions, sizeof (position));
  gst_audio_channel_positions_to_valid_order (position, amdec->channels);
  memcpy (amdec->gst_positions, position,
      amdec->channels * sizeof (GstAudioChannelPosition));

  /* get the remap table */
  memset (amdec->reorder_map, 0, sizeof (amdec->reorder_map));
  amdec->need_reorder = FALSE;
  if (gst_audio_get_channel_reorder_map (amdec->channels, amdec->aac_positions,
          amdec->gst_positions, amdec->reorder_map)) {
    for (i = 0; i < amdec->channels; i++) {
      GST_DEBUG_OBJECT (amdec, "remap %d -> %d", i, amdec->reorder_map[i]);
      if (amdec->reorder_map[i] != i) {
        amdec->need_reorder = TRUE;
      }
    }
  }

  /* FIXME: Use the GstAudioInfo of GstAudioDecoder for all of this */
  gst_audio_info_init (&ainfo);
  gst_audio_info_set_format (&ainfo, GST_AUDIO_FORMAT_S16, amdec->samplerate,
      amdec->channels, position);

  ret = gst_audio_decoder_set_output_format (GST_AUDIO_DECODER (amdec), &ainfo);

  GST_INFO_OBJECT (amdec, "AAC Decoder detect transport bitstream format: %s",
      aac_dec_bsformat_to_str(amdec->m_dec_conf.bsFormat));
  GST_INFO_OBJECT (amdec, "AAC Decoder mode: %d", aacdec_get_mode(&amdec->m_dec_conf));

  return ret;
}

static GstFlowReturn
gst_amba_aac_dec_handle_frame (GstAudioDecoder * dec, GstBuffer * buffer)
{
  GstAmbaAacDec *amdec;
  GstFlowReturn ret = GST_FLOW_OK;
  GstMapInfo map;
  gsize input_size;
  guchar *input_data;
  GstBuffer *outbuf;
  //faacDecFrameInfo info;
  au_aacdec_config_t *conf = NULL;
  guint output_size = 0;

  amdec = GST_AMBA_AAC_DEC (dec);

  /* no fancy draining */
  if (G_UNLIKELY (!buffer))
    return GST_FLOW_OK;

  gst_buffer_map (buffer, &map, GST_MAP_READ);
  input_data = map.data;
  input_size = map.size;

  GstMapInfo omap;
  conf = &amdec->m_dec_conf;

  conf->dec_rptr = input_data;
  conf->dec_wptr = amdec->m_dec_out_buffer;
  conf->consumedByte = 0;

  aacdec_decode (conf);

  gst_buffer_unmap (buffer, &map);

  if (G_UNLIKELY (conf->ErrorStatus)) {
    gst_audio_decoder_finish_frame (dec, NULL, 1);
    GST_ERROR_OBJECT (amdec, "decoding error: %s, consumed %u bytes!",
        aac_dec_strerror (conf->ErrorStatus),
        conf->consumedByte);
    /* Skip broken data */
    conf->consumedByte = (conf->consumedByte == 0) ?
        input_size : conf->consumedByte;
    return ret;
  }

  if (G_UNLIKELY(conf->consumedByte == ((guint)-1))) {
    GST_ERROR_OBJECT (amdec, "failed to decode %lu bytes!", input_size);
    return FALSE;
  }

  GST_LOG_OBJECT (amdec, "%d bytes consumed, %d samples decoded",
      conf->consumedByte, conf->frameSize);

  if (G_LIKELY (amdec->m_dec_conf.has_dec_out)) {
    if (!gst_amba_aac_dec_update_caps (amdec, conf)) {
      GST_ELEMENT_ERROR (amdec, CORE, NEGOTIATION, (NULL),
          ("Setting caps on source pad failed"));
      return GST_FLOW_ERROR;
    }

    output_size = amdec->m_dec_conf.frameSize *
        amdec->m_dec_conf.outNumCh * amdec->bps;

    /* FIXME, add bufferpool and allocator support to the base class */
    outbuf = gst_buffer_new_allocate (NULL, output_size, NULL);
    if (!outbuf) {
      GST_ERROR_OBJECT (amdec, "failed to allocate %u bytes for outuput buffer!", output_size);
      return GST_FLOW_ERROR;
    }

    gst_buffer_map (outbuf, &omap, GST_MAP_READWRITE);

    if (amdec->need_reorder) {
      gint16 *dest, *src, i, j;

      dest = (gint16 *) omap.data;
      src = (gint16 *) conf->dec_wptr;

      for (i = 0; i < conf->frameSize; i++) {
        for (j = 0; j < conf->outNumCh; j++) {
          dest[amdec->reorder_map[j]] = *src++;
        }
        dest += conf->outNumCh;
      }
    } else {
      memcpy(omap.data, conf->dec_wptr, output_size);
    }

    gst_buffer_unmap (outbuf, &omap);

    ret = gst_audio_decoder_finish_frame (dec, outbuf, 1);
  }

  return ret;
}

static gboolean
gst_amba_aac_dec_open_decoder (GstAmbaAacDec * amdec)
{
  guint size = 0;
  audio_lib_info_t lib_info;

  gst_amba_aac_dec_close_decoder (amdec);
  if (G_LIKELY(!amdec->m_dec_out_buffer)) {
    amdec->m_dec_out_buffer = (gint *) g_malloc(amdec->m_dec_out_buf_size);
  }
  if (G_LIKELY(!amdec->m_dec_out_buffer)) {
    GST_ERROR_OBJECT (amdec, "Failed to allocate AAC decode output buffer!");
    return FALSE;
  }

  memset(&amdec->m_dec_conf, 0, sizeof(au_aacdec_config_t));
  amdec->m_dec_conf.bDownSample = amdec->downsampled_sbr;
  amdec->m_dec_conf.bBitstreamDownMix = amdec->downmix;

  amdec->m_dec_conf.sample_freq = amdec->samplerate;
  amdec->m_dec_conf.outNumCh = amdec->channels;
  amdec->m_dec_conf.bsFormat = amdec->stream_fmt;

  amdec->bps = sizeof(gint16);

  size = aacdec_get_mem_size (amdec->channels);
  GST_INFO_OBJECT (amdec, "AAC Decoder working memory is %u bytes", size);
  if (amdec->m_dec_buffer_size < size) {
    amdec->m_dec_buffer_size = size;
    if (amdec->m_dec_buffer) {
      g_free (amdec->m_dec_buffer);
      amdec->m_dec_buffer = NULL;
    }
  }
  if (!amdec->m_dec_buffer) {
    amdec->m_dec_buffer = (guint *) g_malloc(amdec->m_dec_buffer_size);
    if (!amdec->m_dec_buffer) {
      GST_ERROR_OBJECT (amdec, "Failed to allocate AAC codec decode buffer!");
      goto setup_failed;
    }
  }

  amdec->m_dec_conf.codec_lib_mem_addr = amdec->m_dec_buffer;
  amdec->m_dec_conf.codec_lib_mem_size = amdec->m_dec_buffer_size;

  aacdec_setup(&amdec->m_dec_conf);
  if (G_UNLIKELY(amdec->m_dec_conf.ErrorStatus)) {
    GST_ERROR_OBJECT (amdec, "aacdec_setup failed: %s",
        aac_dec_strerror(amdec->m_dec_conf.ErrorStatus));
    goto setup_failed;
  }
  aacdec_open(&amdec->m_dec_conf);
  if (G_UNLIKELY(amdec->m_dec_conf.ErrorStatus)) {
    GST_ERROR_OBJECT (amdec, "aacdec_open failed: %s",
        aac_dec_strerror(amdec->m_dec_conf.ErrorStatus));
    goto setup_failed;
  }
  amdec->m_is_initialized = 1;
  GST_INFO_OBJECT (amdec, "AAC codec is initialized for decoding!");

  memset(&lib_info, 0, sizeof(audio_lib_info_t));
  report_lib_info_aac_dec(&lib_info);
  GST_INFO_OBJECT (amdec, "\nAAC Decoder Info:"
      "\n SVN  REV: %s"
      "\n SVN HTTP: %s",
      lib_info.svnrev,
      lib_info.svnhttp);

  return TRUE;

  /* ERRORS */
setup_failed:
  {
    GST_ELEMENT_ERROR (amdec, LIBRARY, SETTINGS, (NULL), (NULL));
    if (amdec->m_dec_out_buffer) {
      g_free (amdec->m_dec_out_buffer);
      amdec->m_dec_out_buffer = NULL;
    }
    if (amdec->m_dec_buffer) {
      g_free (amdec->m_dec_buffer);
      amdec->m_dec_buffer = NULL;
    }
    return FALSE;
  }
}

static void
gst_amba_aac_dec_close_decoder (GstAmbaAacDec * amdec)
{
  if (amdec->m_is_initialized) {
    if (amdec->m_dec_out_buffer) {
        g_free (amdec->m_dec_out_buffer);
        amdec->m_dec_out_buffer = NULL;
    }
    amdec->m_dec_out_buf_size = 0;
    if (amdec->m_dec_buffer) {
        g_free (amdec->m_dec_buffer);
        amdec->m_dec_buffer = NULL;
    }
    amdec->m_dec_buffer_size = 0;

    amdec->m_is_initialized = 0;
  }
}

static void
gst_amba_aac_dec_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstAmbaAacDec *amdec = GST_AMBA_AAC_DEC (object);

  switch (prop_id) {
    case PROP_DOWNMIX:
      g_value_set_int (value, amdec->downmix);
      break;
    case PROP_DOWNSAMPLE_SBR:
      g_value_set_int (value, amdec->downsampled_sbr);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

}

static void
gst_amba_aac_dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAmbaAacDec *amdec = GST_AMBA_AAC_DEC (object);

  switch (prop_id) {
    case PROP_DOWNMIX:
      amdec->downmix = g_value_get_int (value);
      break;
    case PROP_DOWNSAMPLE_SBR:
      amdec->downsampled_sbr = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

}
