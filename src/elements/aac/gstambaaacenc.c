/*
 * gstambaaacenc.c
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
 * ## Example launch line
 * |[
 * gst-launch-1.0 -e audiotestsrc wave=sine ! audioconvert ! audio/x-raw,rate=48000,channels=2,format=S32LE ! amba_aac_enc ! mp4mux ! filesink location=/tmp/sine.mp4
 * ]| Encode a sine beep as aac and write to mp4 container.
 * |[
 * gst-launch-1.0 -e audiotestsrc wave=sine ! audioconvert ! audio/x-raw,rate=44100,channels=2,format=S16LE ! amba_aac_enc fftype=adts ! aacparse ! filesink location=/tmp/sine.adts
 * ]| Encode a sine beep as aac and write to adts file.
 *
 */


#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include <gst/audio/audio.h>
#include <gst/pbutils/codec-utils.h>

#include "internal.h"
#include "gstambaaacenc.h"

#define DEFAULT_BITRATE (128000)
#define DEFAULT_SAMPLE_RATE (48000)
#define AMBA_AAC_ENC_CODECDATA_LEN (2)
#define AMBA_AAC_ENC_MPEGVERSION (4)

#define SAMPLE_RATES " 8000, " \
                    "11025, " \
                    "12000, " \
                    "16000, " \
                    "22050, " \
                    "24000, " \
                    "32000, " \
                    "44100, " \
                    "48000"

GST_DEBUG_CATEGORY_STATIC (gst_amba_aac_enc_debug);
#define GST_CAT_DEFAULT gst_amba_aac_enc_debug

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    /* cppcheck-suppress unknownMacro */
    GST_STATIC_CAPS ("audio/x-raw, "
        "format = (string) { "GST_AUDIO_NE(S16)", "GST_AUDIO_NE(S32)" }, "
        "layout = (string) interleaved, "
        "rate = (int) { " SAMPLE_RATES " }, "
        "channels = (int) [ 1, 5 ]")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, "
        "mpegversion = (int) 4, "
        "channels = (int) [ 1, 5 ], "
        "rate = (int) {" SAMPLE_RATES "}, "
        "stream-format = (string) { raw, adts, adif, loas }, "
        "base-profile = (string) { lc, he-aac-v1, he-aac-v2 }; "
        "audio/mpeg, "                     \
        "mpegversion = (int) 2, "   \
        "channels = (int) [ 1, 5 ], "      \
        "rate = (int) {" SAMPLE_RATES "}, "   \
        "stream-format = (string) { raw, adts, adif }, " \
        "profile = (string) { lc }")
    );

enum
{
  PROP_0,
  PROP_FORMAT,
  PROP_BITRATE,
  PROP_FFTYPE,
  PROP_TNS,
  PROP_PNS,
  PROP_CRC,
  PROP_PERCEPTUAL_MODE,
  PROP_QUANTIZER_QUALITY,
};

static void gst_amba_aac_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_amba_aac_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static gboolean gst_amba_aac_enc_stop (GstAudioEncoder * benc);
static gboolean gst_amba_aac_enc_set_format (GstAudioEncoder * benc,
    GstAudioInfo * info);
static GstFlowReturn gst_amba_aac_enc_handle_frame (GstAudioEncoder * benc,
    GstBuffer * buf);

#define gst_amba_aac_enc_parent_class parent_class
 G_DEFINE_TYPE_WITH_CODE (GstAmbaAacEnc, gst_amba_aac_enc, GST_TYPE_AUDIO_ENCODER,
    GST_DEBUG_CATEGORY_INIT (gst_amba_aac_enc_debug, "ambaaacenc", 0,
        "Amba AAC encoding"));

static aac_status_msg_t encoder_status[] =
{
  {AAC_ENCODE_OK,                      "OK"},
  {AAC_ENCODE_INVALID_POINTER,         "Invalid pointer"},
  {AAC_ENCODE_FAILED,                  "Encode failed"},
  {AAC_ENCODE_UNSUPPORTED_SAMPLE_RATE, "Unsupported sample rate"},
  {AAC_ENCODE_UNSUPPORTED_CH_CFG,      "Unsupported channel configuration"},
  {AAC_ENCODE_UNSUPPORTED_BIT_RATE,    "Unsupported bit rate"},
  {AAC_ENCODE_UNSUPPORTED_MODE,        "Unsupported Mode"},
  {AAC_ENCODE_TOP_ERR_1,               "Top layer error 1"},
  {AAC_ENCODE_TOP_ERR_2,               "Top layer error 2"},
  {AAC_ENCODE_TOP_ERR_3,               "Top layer error 3"},
  {AAC_ENCODE_TOP_ERR_4,               "Top layer error 4"},
  {AAC_ENCODE_TOP_ERR_5,               "Top layer error 5"},
  {AAC_ENCODE_TOP_ERR_6,               "Top layer error 6"},
  {AAC_ENCODE_TOP_ERR_7,               "Top layer error 7"},
  {AAC_ENCODE_TOP_ERR_8,               "Top layer error 8"},
  {AAC_ENCODE_TOP_ERR_9,               "Top layer error 9"},
  {AAC_ENCODE_TOP_ERR_10,              "Top layer error 10"},
  {AAC_ENCODE_TOP_ERR_11,              "Top layer error 11"},
  {AAC_ENCODE_TOP_ERR_12,              "Top layer error 12"},
  {AAC_ENCODE_TOP_ERR_13,              "Top layer error 13"},
  {AAC_ENCODE_TOP_ERR_14,              "Top layer error 14"},
  {AAC_ENCODE_TOP_ERR_15,              "Top layer error 15"},
  {AAC_ENCODE_TOP_ERR_16,              "Top layer error 16"},
};

/* describe the channels position */
static const GstAudioChannelPosition aac_channel_positions[][6] = {
  {   /* 1 ch: Mono */
      GST_AUDIO_CHANNEL_POSITION_MONO},
  {   /* 2 ch: front left + front right (front stereo) */
      GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
      GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT},
  {   /* 3 ch: front center + front stereo */
      GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
      GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
      GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT},
  {   /* 4 ch: front center + front stereo + back center */
      GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
      GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
      GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
      GST_AUDIO_CHANNEL_POSITION_REAR_CENTER},
  {   /* 5 ch: front center + front stereo + back stereo */
      GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
      GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
      GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
      GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
      GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT},
  {   /* 6ch: front center + front stereo + back stereo + LFE */
      GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
      GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
      GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
      GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
      GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
      GST_AUDIO_CHANNEL_POSITION_LFE1}
};

static void
gst_amba_aac_enc_class_init (GstAmbaAacEncClass * klass)
{
  GObjectClass *gobject_class;
  GstAudioEncoderClass *base_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  base_class = (GstAudioEncoderClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_amba_aac_enc_set_property;
  gobject_class->get_property = gst_amba_aac_enc_get_property;

  gst_element_class_add_static_pad_template (gstelement_class, &src_factory);
  gst_element_class_add_static_pad_template (gstelement_class, &sink_factory);
  gst_element_class_set_static_metadata (gstelement_class, "AAC audio encoder",
      "Codec/Encoder/Audio",
      "AMBA AAC encoder",
      "pxduan <pxduan@ambarella.com>");

  base_class->stop = GST_DEBUG_FUNCPTR (gst_amba_aac_enc_stop);
  base_class->set_format = GST_DEBUG_FUNCPTR (gst_amba_aac_enc_set_format);
  base_class->handle_frame = GST_DEBUG_FUNCPTR (gst_amba_aac_enc_handle_frame);

  g_object_class_install_property (gobject_class, PROP_FORMAT,
      g_param_spec_string ("enc-mode", "AAC encoding mode",
          "Setup AAC encoding mode", AAC_FORMAT_AAC,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_BITRATE,
      g_param_spec_uint ("bitrate", "Encoding Bit-rate", "Specify an encoding bit-rate (in bps).",
          0, 320000,
          DEFAULT_BITRATE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS) );

  g_object_class_install_property (gobject_class, PROP_FFTYPE,
      g_param_spec_string ("fftype", "FFType",
          "Specify AAC transition format", AAC_TRANSPORT_FMT_ADTS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TNS,
      g_param_spec_uint ("tns", "TNS", "Specify temporal noise shaping.",
          0, 0xffffffff,
          1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS) );

  g_object_class_install_property (gobject_class, PROP_PNS,
      g_param_spec_uint ("pns", "PNS", "Specify perceptual noise substitution.",
          0, 0xffffffff,
          1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS) );

  g_object_class_install_property (gobject_class, PROP_CRC,
      g_param_spec_uint ("crc", "CRC", "Specify cyclic redundancy check.",
          0, 0xffffffff,
          1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS) );

  g_object_class_install_property (gobject_class, PROP_PERCEPTUAL_MODE,
      g_param_spec_string ("perceptual", "Perceptual Mode",
          "Specify perceptual mode", AAC_PERCEPTUAL_MODE_NORMAL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_QUANTIZER_QUALITY,
      g_param_spec_string ("quality", "Quantizer Quality",
          "Specify quantizer quality", AAC_QUANTIZER_QUALITY_HIGH,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

}

static void
gst_amba_aac_enc_init (GstAmbaAacEnc * enc)
{
  GST_DEBUG_OBJECT (enc, "init");

  GST_PAD_SET_ACCEPT_TEMPLATE (GST_AUDIO_ENCODER_SINK_PAD (enc));

  strncpy(enc->m_src_audio_info.codec_info, AAC_TRANSPORT_FMT_ADTS, 127);
  enc->bitrate           = DEFAULT_BITRATE;
  enc->tns               = 1;
  enc->pns               = 1;
  enc->crc               = 1;
  enc->quantizer_quality = QUANTIZER_QUALITY_HIGH;
  enc->format            = AACPLAIN;
  enc->fftype            = ADTS;
  enc->perceptual_mode   = AAC_ENCODE_NORMAL;
}

static int check_encode_src_parameter(GstAmbaAacEnc * enc)
{
  int ret = -1;
  amba_audio_info_t *info = &enc->m_src_audio_info;
  do {
    if ((enc->sample_format != GST_AUDIO_FORMAT_S16LE) &&
        (enc->sample_format != GST_AUDIO_FORMAT_S32LE)) {
      GST_ERROR("Invalid input audio sample type! "
          "S16LE/S32LE is required!");
      break;
    }

    if ((AACPLAIN == enc->format) &&
      (info->sample_rate != 8000)  &&
      (info->sample_rate != 11025) &&
      (info->sample_rate != 12000) &&
      (info->sample_rate != 16000) &&
      (info->sample_rate != 22050) &&
      (info->sample_rate != 24000) &&
      (info->sample_rate != 32000) &&
      (info->sample_rate != 44100) &&
      (info->sample_rate != 48000)) {
      break;
    }

    if (((AACPLUS == enc->format) ||
        (AACPLUS_PS == enc->format)) &&
        (info->sample_rate != 32000) &&
        (info->sample_rate != 44100) &&
        (info->sample_rate != 48000)) {
      break;
    }

    if ((enc->format == AACPLUS_PS) &&
        (info->channels == 1)) {
      break;
    }

    switch(enc->format) {
      case AACPLAIN: {
        uint32_t max = 0;
        uint32_t min = 0;
        switch(info->sample_rate) {
          case 48000:
          case 44100:
          case 32000:
          case 24000:
          case 22050:
          case 16000: {
            min = info->channels * 16000;
            max = info->channels * info->sample_rate * 6144 / 1024;
          } break;
          case 12000:
          case 11025: {
            min = info->channels * 8000;
            max = info->channels * 40000;
          } break;
          case 8000: {
            min = info->channels * 8000;
            max = info->channels * 12000;
          } break;
          default: {
            min = 16000;
            max = 160000;
          } break;
        }
        if (enc->bitrate > max) {
          GST_WARNING("Bit rate exceeds AAC maximum bit rate(%u), "
              "reset to %u", max, max);
          enc->bitrate = max;
        } else if (enc->bitrate < min) {
          GST_WARNING("Bit rate is less than AAC minimum bit rate(%u) "
              "reset to %u", min, min);
          enc->bitrate = min;
        }
      } break;
      case AACPLUS: {
        uint32_t min = info->channels * 14000;
        uint32_t max = info->channels * 64000;
        if (enc->bitrate > max) {
          GST_WARNING("Bit rate exceeds AAC Plus maximum bit rate(%u) "
               "reset to %u", max, max);
          enc->bitrate = max;
        } else if (enc->bitrate < min) {
          GST_WARNING("Bit rate is less than AAC Plus minimum bit rate(%u) "
               "reset to %u", min, min);
          enc->bitrate = min;
        }
      } break;
      case AACPLUS_PS: {
        uint32_t min = 14000;
        uint32_t max = 64000;
        if (enc->bitrate > max) {
          GST_WARNING("Bit rate exceeds AAC Plus PS maximum bit rate(%u) "
               "reset to %u", max, max);
          enc->bitrate = max;
        } else if (enc->bitrate < min) {
          GST_WARNING("Bit rate is less than AAC Plus PS minimum bit rate"
               "(%u) reset to %u", min, min);
          enc->bitrate = min;
        }
      } break;
      default: break;
    }
    ret = 0;
  } while(0);
  if (ret < 0) {
    GST_ERROR("Audio codec aac doesn't accept current audio source config!"
        "\nWrong source audio parameters:"
        "\n Sample Format: %s"
        "\n   Sample Rate: %u"
        "\n      Channels: %u"
        "\nRequired source audio parameters:"
        "\n Sample Format: s16le|s32le"
        "\n   Sample Rate: %s"
        "\n      Channels: %s",
        gst_audio_format_to_string(enc->sample_format),
        info->sample_rate,
        info->channels,
        (enc->format == AACPLAIN) ?
        "8000|11025|12000|16000|22050|24000|32000|44100|48000" :
        "32000|44100|48000",
        (enc->format == AACPLUS_PS) ? "2" : "1 | 2");
  }

  return ret;
}

static uint32_t get_encode_required_chunk_size(GstAmbaAacEnc * enc)
{
  uint32_t chunk_size = 0;
  amba_audio_info_t *info = &enc->m_src_audio_info;
  switch(enc->format) {
    case AACPLAIN: {
      enc->samples = 1024;
      chunk_size = 1024 * info->channels * info->sample_size;
    } break;
    case AACPLUS:
    case AACPLUS_PS: {
      enc->samples = 2048;
      chunk_size = 2048 * info->channels * info->sample_size;
    } break;
    case AACPLUS_SPEECH:
    case AACPLUS_SPEECH_PS:
    default:
      enc->samples = 0;
      chunk_size = 0;
      break;
  }

  return chunk_size;
}

static int get_channel_mode (unsigned int channel_num)
{
  int channel_mode = AAC_UNDEFINED_CHANNEL_MODE;
  switch(channel_num) {
    case 1: {
      channel_mode = AAC_MONO;
    } break;
    case 2: {
      channel_mode = AAC_STEREO;
    } break;
    case 3: {
      channel_mode = AAC_CH_MODE_3_0;
    } break;
    case 4: {
      channel_mode = AAC_CH_MODE_3_1;
    } break;
    case 5: {
      channel_mode = AAC_CH_MODE_3_2;
    } break;
    default: {
      channel_mode = AAC_UNDEFINED_CHANNEL_MODE;
    } break;
  }
  return channel_mode;
}

static void
gst_amba_aac_close_encoder (GstAmbaAacEnc *enc)
{
  if (enc->m_is_initialized) {
    if (enc->m_enc_buffer) {
        g_free(enc->m_enc_buffer);
        enc->m_enc_buffer = NULL;
    }
    enc->m_enc_buffer_size = 0;

    if (enc->m_enc_inter_buffer) {
      g_free(enc->m_enc_inter_buffer);
      enc->m_enc_inter_buffer = NULL;
    }
    enc->m_enc_inter_buf_size = 0;

    enc->m_is_initialized = 0;
  }
}

static gboolean
gst_amba_aac_open_encoder (GstAmbaAacEnc * enc)
{
  amba_audio_info_t *info = &enc->m_src_audio_info;
  gint size = 0;
  audio_lib_info_t lib_info;

  g_return_val_if_fail (info->sample_rate != 0 && info->channels != 0, FALSE);

  /* clean up in case of re-configure */
  gst_amba_aac_close_encoder (enc);

  if (check_encode_src_parameter(enc) < 0) {
    return FALSE;
  }

  memset(&enc->m_enc_conf, 0, sizeof(au_aacenc_config_t));
  enc->m_enc_conf.sample_freq      = info->sample_rate;
  enc->m_enc_conf.Src_numCh        = info->channels;
  enc->m_enc_conf.bitRate          = enc->bitrate;
  enc->m_enc_conf.quantizerQuality = enc->quantizer_quality;
  enc->m_enc_conf.tns              = enc->tns;
  enc->m_enc_conf.crc              = enc->crc;
  enc->m_enc_conf.pns              = enc->pns;
  enc->m_enc_conf.ffType           = enc->fftype;
  enc->m_enc_conf.enc_mode         = enc->format;
  enc->m_enc_conf.perceptual_mode  = enc->perceptual_mode;
  enc->m_enc_conf.original_copy                  = 0;
  enc->m_enc_conf.copyright_identification_bit   = 0;
  enc->m_enc_conf.copyright_identification_start = 0;
  enc->m_enc_conf.channelMode = get_channel_mode (info->channels);
  enc->m_enc_conf.sendSbrHeader = 0;
  size = aacenc_get_mem_size(enc->m_enc_conf.Src_numCh,
      enc->m_enc_conf.sample_freq,
      enc->m_enc_conf.bitRate,
      enc->m_enc_conf.enc_mode);
  if (size <= 0) {
    GST_ERROR("Invalid AAC encoder configuration:"
        "\n       Sample Rate: %u"
        "\n    Source Channel: %d"
        "\n          Bit Rate: %u"
        "\n Quantizer Quality: %u"
        "\n               tns: %u"
        "\n               crc: %u"
        "\n               pns: %u"
        "\n  Transport Format: %c: %s"
        "\n       Encode Mode: %d"
        "\n   Perceptual Mode: %hhu",
        enc->m_enc_conf.sample_freq,
        enc->m_enc_conf.Src_numCh,
        enc->m_enc_conf.bitRate,
        enc->m_enc_conf.quantizerQuality,
        enc->m_enc_conf.tns,
        enc->m_enc_conf.crc,
        enc->m_enc_conf.pns,
        enc->m_enc_conf.ffType,
        (const char*)info->codec_info,
        enc->m_enc_conf.enc_mode,
        enc->m_enc_conf.perceptual_mode);
    return FALSE;
  }

  if ((gint) enc->m_enc_buffer_size < size) {
    enc->m_enc_buffer_size = (guint) size;
    if (enc->m_enc_buffer) {
      g_free(enc->m_enc_buffer);
      enc->m_enc_buffer = NULL;
    }
  }
  if (!enc->m_enc_buffer) {
    enc->m_enc_buffer = (guchar *) g_malloc(enc->m_enc_buffer_size);
    if (!enc->m_enc_buffer) {
      GST_ERROR("Failed to allocate AAC codec encode buffer!");
      return FALSE;
    }
  }
  enc->m_enc_conf.codec_lib_mem_adr = (uint32_t*)enc->m_enc_buffer;
  enc->m_enc_conf.codec_lib_mem_size = enc->m_enc_buffer_size;
  if ((enc->m_enc_conf.Src_numCh != 2) && (enc->format == AACPLUS_PS)) {
    GST_ERROR("AAC Plus PS requires stereo audio input, "
        "but source audio channel number is: %u",
        info->channels);
    goto setup_failed;
  }
  if (enc->format == AACPLUS_PS) {
    /* AACPlus_PS's output channel number is always 1 */
    info->channels = 1;
  }
  aacenc_setup(&enc->m_enc_conf);
  if (enc->m_enc_conf.ErrorStatus) {
    GST_ERROR("aacenc_setup failed: 0x%08x", enc->m_enc_conf.ErrorStatus);
    goto setup_failed;
  }
  aacenc_open(&enc->m_enc_conf);
  if (enc->m_enc_conf.ErrorStatus) {
    GST_ERROR("aacenc_open failed: 0x%08x", enc->m_enc_conf.ErrorStatus);
    goto setup_failed;
  }
  enc->m_is_initialized = 1;
  GST_INFO_OBJECT(enc, "AAC codec is initialized for encoding!");

  memset(&lib_info, 0, sizeof(audio_lib_info_t));
  report_lib_info_aac_enc(&lib_info);
  GST_INFO_OBJECT(enc, "\nAAC Encoder Info:"
       "\n SVN  REV: %s"
       "\n SVN HTTP: %s",
       lib_info.svnrev,
       lib_info.svnhttp);

  return TRUE;

  /* ERRORS */
setup_failed:
  {
    GST_ELEMENT_ERROR (enc, LIBRARY, SETTINGS, (NULL), (NULL));
    if (enc->m_enc_buffer) {
      g_free(enc->m_enc_buffer);
      enc->m_enc_buffer = NULL;
    }
    return FALSE;
  }
}



static gboolean
gst_amba_aac_enc_stop (GstAudioEncoder * benc)
{
  GstAmbaAacEnc *enc = GST_AMBA_AAC_ENC (benc);

  GST_DEBUG_OBJECT (enc, "stop");
  gst_amba_aac_close_encoder (enc);

  return TRUE;
}

/* check downstream caps to configure format */
static void
gst_amba_aac_enc_negotiate (GstAmbaAacEnc * enc)
{
  GstCaps *caps;
  enc->mpegversion = 4;

  caps = gst_pad_get_allowed_caps (GST_AUDIO_ENCODER_SRC_PAD (enc));

  GST_DEBUG_OBJECT (enc, "allowed caps: %" GST_PTR_FORMAT, caps);

  if (caps && gst_caps_get_size (caps) > 0) {
    GstStructure *s = gst_caps_get_structure (caps, 0);
    const gchar *str = NULL;
    gint i = 4;

    if ((str = gst_structure_get_string (s, "stream-format"))) {
      if (strcmp (str, AAC_TRANSPORT_FMT_ADTS) == 0) {
        GST_DEBUG_OBJECT (enc, "use ADTS format for output");
        strncpy(enc->m_src_audio_info.codec_info, AAC_TRANSPORT_FMT_ADTS, 127);
        enc->fftype = ADTS;
      } else if (strcmp (str, AAC_TRANSPORT_FMT_RAW) == 0) {
        GST_DEBUG_OBJECT (enc, "use RAW format for output");
        strncpy(enc->m_src_audio_info.codec_info, AAC_TRANSPORT_FMT_RAW, 127);
        enc->fftype = RAW;
      } else if (strcmp (str, AAC_TRANSPORT_FMT_ADIF) == 0) {
        GST_DEBUG_OBJECT (enc, "use ADIF format for output");
        strncpy(enc->m_src_audio_info.codec_info, AAC_TRANSPORT_FMT_ADIF, 127);
        enc->fftype = ADIF;
      } else if (strcmp (str, AAC_TRANSPORT_FMT_LOAS) == 0) {
        GST_DEBUG_OBJECT (enc, "use LOAS format for output");
        strncpy(enc->m_src_audio_info.codec_info, AAC_TRANSPORT_FMT_LOAS, 127);
        enc->fftype = LOAS;
      } else {
        GST_WARNING_OBJECT (enc, "unknown stream-format: %s, default ADTS", str);
        strncpy(enc->m_src_audio_info.codec_info, AAC_TRANSPORT_FMT_ADTS, 127);
        enc->fftype = ADTS;
      }
    }

    if ((str = gst_structure_get_string (s, "profile"))) {
      if (strcmp (str, "lc") == 0) {
        GST_DEBUG_OBJECT (enc, "using AAC-LC profile for output");
        enc->format = AACPLAIN;
      } else if (strcmp (str, "he-aac-v1") == 0) {
        GST_DEBUG_OBJECT (enc, "using SBR (HE-AACv1) profile for output");
        enc->format = AACPLUS;
      } else if (strcmp (str, "he-aac-v2") == 0) {
        GST_DEBUG_OBJECT (enc, "using PS (HE-AACv2) profile for output");
        enc->format = AACPLUS_PS;
      }
    }

    if (!gst_structure_get_int (s, "mpegversion", &i) || i == 4) {
      enc->mpegversion = 4;
    } else {
      enc->mpegversion = 2;
    }

  }

  if (caps)
    gst_caps_unref (caps);
}


static gboolean
gst_amba_aac_enc_configure_source_pad (GstAmbaAacEnc * enc)
{
  GstCaps *caps = NULL;
  gint index = -1, core_index = -1;
  GstBuffer *codec_data;
  GstMapInfo map;
  gboolean ret = FALSE;
  gchar profile_str[128] = {'\0'};
  guint aot = 0x0, ext_aot = 0x0;
  guint codec_size = AMBA_AAC_ENC_CODECDATA_LEN;
  guint core_sample_rate = enc->m_src_audio_info.sample_rate / 2;

  if ((index = gst_codec_utils_aac_get_index_from_sample_rate (enc->m_src_audio_info.sample_rate)) < 0) {
    GST_ERROR_OBJECT (enc, "Invalid sampling rate (%d) index (%d)", enc->m_src_audio_info.sample_rate, index);
    return FALSE;
  }
  codec_data = gst_buffer_new_and_alloc (AMBA_AAC_ENC_CODECDATA_LEN + 2);
  gst_buffer_map (codec_data, &map, GST_MAP_WRITE);

  switch (enc->format) {
    case AACPLUS: {
      ext_aot = 0x05;
      aot = 0x02;
      strncpy (profile_str, "he-aac-v1", 127);
      if ((core_index = gst_codec_utils_aac_get_index_from_sample_rate (core_sample_rate)) < 0) {
        gst_buffer_unmap (codec_data, &map);
        gst_buffer_unref (codec_data);
        GST_ERROR_OBJECT (enc, "Invalid core sampling rate (%d) for SBR", core_sample_rate);
        return FALSE;
      }
      map.data[0] = ((ext_aot << 3) | (core_index >> 1));
      map.data[1] = ((core_index & 0x01) << 7) | (enc->m_src_audio_info.channels << 3) | (index >> 1);
      map.data[2] = ((index & 0x01) << 7) | (aot << 2);
      codec_size += 2;
    } break;
    case AACPLUS_PS: {
      ext_aot = 0x1d;
      aot = 0x02;
      strncpy (profile_str, "he-aac-v2", 127);
      if ((core_index = gst_codec_utils_aac_get_index_from_sample_rate (core_sample_rate)) < 0) {
        gst_buffer_unmap (codec_data, &map);
        gst_buffer_unref (codec_data);
        GST_ERROR_OBJECT (enc, "Invalid core sampling rate (%d) for SBR + PS", core_sample_rate);
        return FALSE;
      }
      map.data[0] = ((ext_aot << 3) | (core_index >> 1));
      map.data[1] = ((core_index & 0x01) << 7) | (enc->m_src_audio_info.channels << 3) | (index >> 1);
      map.data[2] = ((index & 0x01) << 7) | (aot << 2);
      codec_size += 2;
    } break;
    case AACPLAIN:
    default: {
      if (enc->mpegversion == 2) {
        aot = 0x01;
      } else {
        aot = 0x02;
      }
      map.data[0] = ((aot << 3) | (index >> 1));
      map.data[1] = ((index & 0x01) << 7) | (enc->m_src_audio_info.channels << 3);
      strncpy (profile_str, "lc", 127);
    } break;
  }

  caps = gst_caps_new_simple ("audio/mpeg",
      "mpegversion", G_TYPE_INT, enc->mpegversion,
      "channels", G_TYPE_INT, enc->m_src_audio_info.channels,
      "rate", G_TYPE_INT, enc->m_src_audio_info.sample_rate,
      "stream-format", G_TYPE_STRING, enc->m_src_audio_info.codec_info,
      NULL);

  if (!gst_codec_utils_aac_caps_set_level_and_profile (caps, map.data,
      codec_size)) {
    gst_buffer_unmap (codec_data, &map);
    gst_buffer_unref (codec_data);
    gst_caps_unref (caps);
    GST_ERROR_OBJECT (enc, "Invalid codec data");
    return FALSE;
  }

  gst_caps_set_simple (caps, "profile", G_TYPE_STRING, profile_str, NULL);

  gst_buffer_unmap (codec_data, &map);

  if (enc->fftype == ADTS) {
    gst_caps_set_simple (caps,
        "framed", G_TYPE_BOOLEAN, TRUE, NULL);
  } else if (enc->fftype == RAW) {
    gst_caps_set_simple (caps,
        "codec_data", GST_TYPE_BUFFER, codec_data, NULL);
  }
  gst_buffer_unref (codec_data);

  ret = gst_audio_encoder_set_output_format (GST_AUDIO_ENCODER (enc), caps);
  gst_caps_unref (caps);

  return ret;
}

static void
gst_amba_aac_set_tags (GstAmbaAacEnc * enc)
{
  GstTagList *taglist;

  /* create a taglist and add a bitrate tag to it */
  taglist = gst_tag_list_new_empty ();
  gst_tag_list_add (taglist, GST_TAG_MERGE_REPLACE,
      GST_TAG_BITRATE, enc->bitrate, NULL);

  gst_audio_encoder_merge_tags (GST_AUDIO_ENCODER (enc), taglist,
      GST_TAG_MERGE_REPLACE);

  gst_tag_list_unref (taglist);
}

static gboolean
gst_amba_aac_enc_set_format (GstAudioEncoder * benc, GstAudioInfo * info)
{
  gboolean ret = FALSE;
  GstAmbaAacEnc *enc = GST_AMBA_AAC_ENC (benc);

  enc->m_src_audio_info.channels = GST_AUDIO_INFO_CHANNELS (info);
  enc->m_src_audio_info.sample_rate = GST_AUDIO_INFO_RATE (info);
  enc->sample_format = GST_AUDIO_INFO_FORMAT (info);
  enc->m_src_audio_info.sample_size = GST_AUDIO_INFO_WIDTH (info) >> 3;

  enc->m_src_audio_info.chunk_size = get_encode_required_chunk_size(enc);
  if (enc->m_src_audio_info.chunk_size <= 0) {
    GST_ERROR_OBJECT(enc, "Failed to get codec aac required chunk size!");
    return FALSE;
  }

  gst_amba_aac_enc_negotiate (enc);

  if (!gst_amba_aac_open_encoder (enc)) {
    goto set_failed;
  }

  /* create reverse caps */
  ret = gst_amba_aac_enc_configure_source_pad (enc);

  if (!ret) {
    return FALSE;
  }

  gst_amba_aac_set_tags (enc);

  /* report needs to base class */
  gst_audio_encoder_set_frame_samples_min (benc, enc->samples);
  gst_audio_encoder_set_frame_samples_max (benc, enc->samples);
  gst_audio_encoder_set_frame_max (benc, 1);

  return ret;

/* ERROR */
set_failed:
  GST_WARNING_OBJECT (enc, "amba aac doesn't support the current configuration");
  return FALSE;
}

static const char* aac_enc_strerror(int32_t err)
{
  uint32_t len = sizeof(encoder_status) / sizeof(aac_status_msg_t);
  uint32_t i = 0;
  for (i = 0; i < len; ++ i) {
    if (encoder_status[i].error == err) {
      break;
    }
  }

  return (i >= len) ? "Unknown Error" : encoder_status[i].message;
}

static void format_convert(uint8_t *input,
    uint8_t *output,
    GstAudioFormat from,
    GstAudioFormat to,
    uint32_t sample_count)
{
  uint8_t *out = output;
  for (uint32_t i = 0; i < sample_count; ++ i) {
    float tmp = 0.0f;
    switch(from) {
      case GST_AUDIO_FORMAT_U8: {
        tmp = ((int)(input[i] - 128)) / 128.0f;
      } break;
      case GST_AUDIO_FORMAT_S16LE: {
        int8_t *in = (int8_t*)(input + (i * 2));
        tmp = ((in[1] << 8) | (uint8_t)in[0]) / 32768.0f;
      } break;
      case GST_AUDIO_FORMAT_S16BE: {
        int8_t *in = (int8_t*)(input + (i * 2));
        tmp = ((in[0] << 8) | (uint8_t)in[1]) / 32768.0f;
      } break;
      case GST_AUDIO_FORMAT_S24LE: {
        uint8_t *in = (input + (i * 3));
        tmp = ((((int8_t)in[2]) << 16) | (in[1] << 8) | in[0]) / 8388608.0f;
      } break;
      case GST_AUDIO_FORMAT_S24BE: {
        uint8_t *in = (input + (i * 3));
        tmp = ((((int8_t)in[0]) << 16) | (in[1] << 8) | in[2]) / 8388608.0f;
      } break;
      case GST_AUDIO_FORMAT_S32LE: {
        uint8_t *in = (input + (i * 4));
        tmp = ((((int8_t)in[3]) << 24) | (in[2] << 16) |
            (in[1] << 8) | in[0]) / 2147483648.0f;
      } break;
      case GST_AUDIO_FORMAT_S32BE: {
        uint8_t *in = (input + (i * 4));
        tmp = ((((int8_t)in[0]) << 24) | (in[1] << 16) |
            (in[2] << 8) | in[3]) / 2147483648.0f;
      } break;
      case GST_AUDIO_FORMAT_F32LE: {
        tmp = *((float*)(input + i * 4));
      } break;
      default: {
        GST_ERROR("Cannot convert audio sample format %s to float!",
            gst_audio_format_to_string(from));
      } break;
    }
    switch(to) {
      case GST_AUDIO_FORMAT_U8: {
        out[i] = (uint8_t)lrintf(MAX(-128, MIN(tmp * 128.0f, 127)) + 128);
      } break;
      case GST_AUDIO_FORMAT_S16LE: {
        uint8_t *out = (output + i * 2);
        int16_t sample =
            (int16_t)lrintf(MAX(-32768, MIN(tmp * 32768.0f, 32767)));
        out[0] = sample & 0xff;
        out[1] = (sample >> 8) & 0xff;
      } break;
      case GST_AUDIO_FORMAT_S16BE: {
        uint8_t *out = (output + i * 2);
        int16_t sample =
            (int16_t)lrintf(MAX(-32768, MIN(tmp * 32768.0f, 32767)));
        out[0] = (sample >> 8) & 0xff;
        out[1] = sample & 0xff;
      } break;
      case GST_AUDIO_FORMAT_S24LE: {
        uint8_t *out = (output + i * 3);
        int32_t sample =
            (int32_t)lrintf(MAX(-8388608, MIN(tmp * 8388608.0f, 8388607)));
        out[0] = sample & 0xff;
        out[1] = (sample >> 8) & 0xff;
        out[2] = (sample >> 16) & 0xff;
      } break;
      case GST_AUDIO_FORMAT_S24BE: {
        uint8_t *out = (output + i * 3);
        int32_t sample =
            (int32_t)lrintf(MAX(-8388608, MIN(tmp * 8388608.0f, 8388607)));
        out[0] = (sample >> 16) & 0xff;
        out[1] = (sample >> 8) & 0xff;
        out[2] = sample & 0xff;
      } break;
      case GST_AUDIO_FORMAT_S32LE: {
        uint8_t *out = (output + i * 4);
        int32_t sample =
            (int32_t)lrintf(MAX(-2147483648, MIN(tmp * 2147483648.0f, (float)(2147483647))));
        out[0] = sample & 0xff;
        out[1] = (sample >> 8) & 0xff;
        out[2] = (sample >> 16) & 0xff;
        out[3] = (sample >> 24) & 0xff;
      } break;
      case GST_AUDIO_FORMAT_S32BE: {
        uint8_t *out = (output + i * 4);
        int32_t sample =
            (int32_t)lrintf(MAX(-2147483648, MIN(tmp * 2147483648.0f, (float)(2147483647))));
        out[0] = (sample >> 24) & 0xff;
        out[1] = (sample >> 16) & 0xff;
        out[2] = (sample >> 8)  & 0xff;
        out[3] = sample & 0xff;
      } break;
      case GST_AUDIO_FORMAT_F32LE: {
        *((float*)(output + i * 4)) = tmp;
      } break;
      default: {
        GST_ERROR("Cannot convert float to audio sample format %s!",
            gst_audio_format_to_string(to));
      } break;
    }
  }
}

static uint32_t amba_aac_encode(GstAmbaAacEnc * enc, uint8_t *input, uint32_t in_data_size,
    uint8_t *output, uint32_t *out_data_size)
{
  *out_data_size = 0;
  int32_t *src_ptr = (int32_t*) input;
  uint32_t size = (uint32_t) (in_data_size / sizeof(int32_t) * sizeof(int16_t));
  if (enc->m_enc_inter_buf_size < size) {
    if (enc->m_enc_inter_buffer) {
      g_free(enc->m_enc_inter_buffer);
      enc->m_enc_inter_buffer = NULL;
    }
    enc->m_enc_inter_buf_size = size;
    enc->m_enc_inter_buffer = (uint8_t *) g_malloc(size);
  }
  if (enc->m_enc_inter_buffer == NULL) {
    GST_ERROR_OBJECT(enc, "no memory for intermediate buffer.");
    return 0;
  }
  if (enc->sample_format == GST_AUDIO_FORMAT_S32LE) {
    format_convert(input,
        enc->m_enc_inter_buffer,
        enc->sample_format,
        GST_AUDIO_FORMAT_S16LE,
        (in_data_size / enc->m_src_audio_info.sample_size));
    src_ptr = (int32_t*)enc->m_enc_inter_buffer;
  }
  enc->m_enc_conf.enc_rptr = src_ptr;
  enc->m_enc_conf.enc_wptr = output;
  aacenc_encode(&enc->m_enc_conf);
  if (enc->m_enc_conf.ErrorStatus) {
    GST_ERROR_OBJECT(enc, "AAC encoding error: %s, encode mode: %d!",
        aac_enc_strerror(enc->m_enc_conf.ErrorStatus),
        enc->m_enc_conf.enc_mode);
  } else {
    *out_data_size = (uint32_t)((enc->m_enc_conf.nBitsInRawDataBlock + 7) >> 3);
  }

  return *out_data_size;
}

static GstFlowReturn
gst_amba_aac_enc_handle_frame (GstAudioEncoder * benc, GstBuffer * buf)
{
  GstAmbaAacEnc *enc = GST_AMBA_AAC_ENC (benc);
  GstFlowReturn ret = GST_FLOW_OK;
  guint8 *data = NULL;
  gsize size = 0;
  GstMapInfo map = {0};
  GstMapInfo omap = {0};
  guint outsize = 0;
  GstBuffer *outbuf = NULL;
  GstAudioInfo *info =
      gst_audio_encoder_get_audio_info (GST_AUDIO_ENCODER (benc));

  GST_DEBUG_OBJECT (enc,
      "Received time %" GST_TIME_FORMAT ", duration %" GST_TIME_FORMAT
      ", size %" G_GSIZE_FORMAT, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buf)), gst_buffer_get_size (buf));

  if (G_LIKELY (buf)) {
    if (memcmp (info->position, aac_channel_positions[info->channels - 1],
            sizeof (GstAudioChannelPosition) * info->channels) != 0) {
      buf = gst_buffer_make_writable (buf);
      gst_audio_buffer_reorder_channels (buf, info->finfo->format,
          info->channels, info->position,
          aac_channel_positions[info->channels - 1]);
    }
    gst_buffer_map (buf, &map, GST_MAP_READ);
    data = map.data;
    size = map.size;
  } else {
    GST_DEBUG_OBJECT (benc, "no data");
    return GST_FLOW_OK;
  }

  outbuf = gst_audio_encoder_allocate_output_buffer (benc,
      enc->m_src_audio_info.chunk_size);
  if (!outbuf) {
    if (data) {
      gst_buffer_unmap (buf, &map);
    }
    return GST_FLOW_ERROR;
  }

  gst_buffer_map (outbuf, &omap, GST_MAP_WRITE);

  amba_aac_encode(enc, data, size, omap.data, &outsize);

  gst_buffer_unmap (buf, &map);

  if (outsize <= 0) {
    GST_ELEMENT_ERROR (enc, STREAM, ENCODE, (NULL),
        ("Encoding failed (%u): %s", outsize, aac_enc_strerror(enc->m_enc_conf.ErrorStatus)));
    goto encode_failed;
  } else if (outsize > enc->m_src_audio_info.chunk_size) {// MAX_AUDIO_BUFFER_SIZE
    GST_ELEMENT_ERROR (enc, STREAM, ENCODE, (NULL),
        ("Encoded size %u is higher than max audio buffer size (%u bytes)",
            outsize, enc->m_src_audio_info.chunk_size));
    goto encode_failed;
  }

  GST_DEBUG_OBJECT (enc, "Output packet is %u bytes", outsize);
  gst_buffer_unmap (outbuf, &omap);
  gst_buffer_resize (outbuf, 0, outsize);

  ret = gst_audio_encoder_finish_frame (benc, outbuf, enc->samples);

  return ret;

  /* ERRORS */
encode_failed:
  {
    gst_buffer_unmap (outbuf, &omap);
    gst_buffer_unref (outbuf);
    return GST_FLOW_ERROR;
  }
}

static void parse_aac_format (GstAmbaAacEnc *enc, const char *custom_properties)
{
  if (!strcmp(custom_properties, AAC_FORMAT_AAC)) {
    strncpy(enc->aac_format_str, AAC_FORMAT_AAC, 127);
    enc->format = AACPLAIN;
  } else if (!strcmp(custom_properties, AAC_FORMAT_AACPLUS)) {
    strncpy(enc->aac_format_str, AAC_FORMAT_AACPLUS, 127);
    enc->format = AACPLUS;
  } else if (!strcmp(custom_properties, AAC_FORMAT_AACPLUS_PS)) {
    strncpy(enc->aac_format_str, AAC_FORMAT_AACPLUS_PS, 127);
    enc->format = AACPLUS_PS;
  } else {
    GST_WARNING("Unsupported format: %s, use default!",
        custom_properties);
    strncpy(enc->aac_format_str, AAC_FORMAT_AAC, 127);
    enc->format = AACPLAIN;
  }

}

static void parse_fftype (GstAmbaAacEnc *enc, const char *custom_properties)
{
  if (!strcmp(custom_properties, AAC_TRANSPORT_FMT_RAW)) {
    strncpy(enc->m_src_audio_info.codec_info, AAC_TRANSPORT_FMT_RAW, 127);
    enc->fftype = RAW;
  } else if (!strcmp(custom_properties, AAC_TRANSPORT_FMT_ADIF)) {
    strncpy(enc->m_src_audio_info.codec_info, AAC_TRANSPORT_FMT_ADIF, 127);
    enc->fftype = ADIF;
  } else if (!strcmp(custom_properties, AAC_TRANSPORT_FMT_ADTS)) {
    strncpy(enc->m_src_audio_info.codec_info, AAC_TRANSPORT_FMT_ADTS, 127);
    enc->fftype = ADTS;
  } else if (!strcmp(custom_properties, AAC_TRANSPORT_FMT_LOAS)) {
    strncpy(enc->m_src_audio_info.codec_info, AAC_TRANSPORT_FMT_LOAS, 127);
    enc->fftype = LOAS;
  } else if (!strcmp(custom_properties, AAC_TRANSPORT_FMT_MP4FILE)) {
    strncpy(enc->m_src_audio_info.codec_info, AAC_TRANSPORT_FMT_MP4FILE, 127);
    enc->fftype = MP4FILE;
  } else {
    GST_WARNING("Invalid \"fftype\" %s, use ADTS by default!",
        custom_properties);
    strncpy(enc->m_src_audio_info.codec_info, AAC_TRANSPORT_FMT_ADTS, 127);
    enc->fftype = ADTS;
  }

}

static void parse_perceptual_mode (GstAmbaAacEnc *enc, const char *custom_properties)
{
  if (!strcmp(custom_properties, AAC_PERCEPTUAL_MODE_NORMAL)) {
    strncpy(enc->perceptual_mode_str, AAC_PERCEPTUAL_MODE_NORMAL, 127);
    enc->perceptual_mode = AAC_ENCODE_NORMAL;
  } else if (!strcmp(custom_properties, AAC_PERCEPTUAL_MODE_FINE)) {
    strncpy(enc->perceptual_mode_str, AAC_PERCEPTUAL_MODE_FINE, 127);
    enc->perceptual_mode = AAC_ENCODE_FINE;
  } else if (!strcmp(custom_properties, AAC_PERCEPTUAL_MODE_FINEST)) {
    strncpy(enc->perceptual_mode_str, AAC_PERCEPTUAL_MODE_FINEST, 127);
    enc->perceptual_mode = AAC_ENCODE_SUPER_FINE;
  } else {
    GST_WARNING("Invalid perceptual mode setting: %s, use \"normal\" as default!",
        custom_properties);
    strncpy(enc->perceptual_mode_str, AAC_PERCEPTUAL_MODE_NORMAL, 127);
    enc->perceptual_mode = AAC_ENCODE_NORMAL;
  }

}

static void parse_quantizer_quality (GstAmbaAacEnc *enc, const char *custom_properties)
{
  if (!strcmp(custom_properties, AAC_QUANTIZER_QUALITY_LOW)) {
    strncpy(enc->quantizer_quality_str, AAC_QUANTIZER_QUALITY_LOW, 127);
    enc->quantizer_quality = QUANTIZER_QUALITY_LOW;
  } else if (!strcmp(custom_properties, AAC_QUANTIZER_QUALITY_HIGH)) {
    strncpy(enc->quantizer_quality_str, AAC_QUANTIZER_QUALITY_HIGH, 127);
    enc->quantizer_quality = QUANTIZER_QUALITY_HIGH;
  } else if (!strcmp(custom_properties, AAC_QUANTIZER_QUALITY_HIGHEST)) {
    strncpy(enc->quantizer_quality_str, AAC_QUANTIZER_QUALITY_HIGHEST, 127);
    enc->quantizer_quality = QUANTIZER_QUALITY_HIGHEST;
  } else {
    GST_WARNING("Invalid quantizer quality setting: %s, use high as default!",
        custom_properties);
    strncpy(enc->quantizer_quality_str, AAC_QUANTIZER_QUALITY_HIGH, 127);
    enc->quantizer_quality = QUANTIZER_QUALITY_HIGH;
  }

}

static void
gst_amba_aac_enc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstAmbaAacEnc *enc = GST_AMBA_AAC_ENC (object);

  switch (prop_id) {
    case PROP_FORMAT:
      g_value_set_string (value, enc->aac_format_str);
      break;
    case PROP_BITRATE:
      g_value_set_uint (value, enc->bitrate);
      break;
    case PROP_FFTYPE:
      g_value_set_string (value, enc->m_src_audio_info.codec_info);
      break;
    case PROP_TNS:
      g_value_set_uint (value, enc->tns);
      break;
    case PROP_PNS:
      g_value_set_uint (value, enc->pns);
      break;
    case PROP_CRC:
      g_value_set_uint (value, enc->crc);
      break;
    case PROP_PERCEPTUAL_MODE:
      g_value_set_string (value, enc->perceptual_mode_str);
      break;
    case PROP_QUANTIZER_QUALITY:
      g_value_set_string (value, enc->quantizer_quality_str);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

}

static void
gst_amba_aac_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAmbaAacEnc *enc = GST_AMBA_AAC_ENC (object);

  switch (prop_id) {
    case PROP_FORMAT:
      parse_aac_format(enc, g_value_get_string (value));
      break;
    case PROP_BITRATE: {
      enc->bitrate = g_value_get_uint (value);
      if (enc->m_is_initialized) {
        enc->m_enc_conf.bitRate = enc->bitrate;
        aacenc_bitrate_change(&enc->m_enc_conf);
      }
    } break;
    case PROP_FFTYPE:
      parse_fftype(enc, g_value_get_string (value));
      break;
    case PROP_TNS:
      enc->tns = g_value_get_uint (value);
      break;
    case PROP_PNS:
      enc->pns = g_value_get_uint (value);
      break;
    case PROP_CRC:
      enc->crc = g_value_get_uint (value);
      break;
    case PROP_PERCEPTUAL_MODE:
      parse_perceptual_mode(enc, g_value_get_string (value));
      break;
    case PROP_QUANTIZER_QUALITY:
      parse_quantizer_quality(enc, g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

}
