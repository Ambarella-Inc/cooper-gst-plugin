/*
 * amba_audio_define.h
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

#ifndef AMBA_AUDIO_DEFINE_H_
#define AMBA_AUDIO_DEFINE_H_

#include <stdint.h>

#define AM_AUDIO_SAMPLE_FORMAT_U8 "u8"
#define AM_AUDIO_SAMPLE_FORMAT_S16LE "s16le"
#define AM_AUDIO_SAMPLE_FORMAT_S16BE "s16be"
#define AM_AUDIO_SAMPLE_FORMAT_S24LE "s24le"
#define AM_AUDIO_SAMPLE_FORMAT_S32LE "s32le"
#define AM_AUDIO_SAMPLE_FORMAT_S32BE "s32be"

#define AM_AUDIO_CHANNEL_MONO "mono"
#define AM_AUDIO_CHANNEL_STEREO "stereo"
#define AM_AUDIO_CHANNEL_MANUAL "manual"

#define AAC_TRANSPORT_FMT_RAW "raw"
#define AAC_TRANSPORT_FMT_LOAS "loas"
#define AAC_TRANSPORT_FMT_ADIF "adif"
#define AAC_TRANSPORT_FMT_ADTS "adts"
#define AAC_TRANSPORT_FMT_MP4FILE "mp4file"
#define AAC_TRANSPORT_FMT_UNKNOWN "unknown"


/*! @enum AM_AUDIO_SAMPLE_FORMAT
 *  @brief This enumeration defines audio format
 */
enum AM_AUDIO_SAMPLE_FORMAT
{
  AM_SAMPLE_U8,       //!< unsigned 8 bites
  AM_SAMPLE_ALAW,     //!< G.711 Alaw
  AM_SAMPLE_ULAW,     //!< G.711 ULaw
  AM_SAMPLE_S16LE,    //!< Signed 16 bits Little Endian
  AM_SAMPLE_S16BE,    //!< Signed 16 bits Big Endian
  AM_SAMPLE_S24LE,    //!< Signed 24 bits Little Endian
  AM_SAMPLE_S24BE,    //!< Signed 24 bits Little Endian
  AM_SAMPLE_S24_32LE, //!< Signed 24 bits Little Endian in lSB of 32 bit words
  AM_SAMPLE_S24_32BE, //!< Signed 24 bits Little Endian in LSB of 32 bit words
  AM_SAMPLE_S32LE,    //!< Signed 32 bits Little Endian
  AM_SAMPLE_S32BE,    //!< Signed 32 bits Big Endian
  AM_SAMPLE_F32LE,    //!< 32 bits IEEE floating point PCM, little endian
  AM_SAMPLE_F32BE,    //!< 32 bits IEEE floating point PCM, big endian
  AM_SAMPLE_INVALID   //!< Invalid type
};

/*! @enum AM_AUDIO_TYPE
 *  @brief This enumeration audio type
 */
enum AM_AUDIO_TYPE
{
  AM_AUDIO_NULL    = -2, //!<audio null
  AM_AUDIO_LPCM    = 4,  //!<audio little endian PCM
  AM_AUDIO_BPCM    = 5,  //!<audio big endian PCM
  AM_AUDIO_FPCM    = 6,  //!<audio floating point PCM
  AM_AUDIO_G711A   = 7,  //!<audio g711a
  AM_AUDIO_G711U   = 8,  //!<audio g711u
  AM_AUDIO_G726_40 = 9,  //!<audio g726_40
  AM_AUDIO_G726_32 = 10, //!<audio g726_32
  AM_AUDIO_G726_24 = 11, //!<audio g726_24
  AM_AUDIO_G726_16 = 12, //!<audio g726_16
  AM_AUDIO_AAC     = 13, //!<audio aac
  AM_AUDIO_OPUS    = 14, //!<audio opus
  AM_AUDIO_SPEEX   = 15, //!<audio speex
  AM_AUDIO_MP3     = 16, //!<audio mp3
  AM_AUDIO_RAW     = 17, //!<audio raw
};

/*! @struct AM_AUDIO_INFO
 *  @brief This structure contains audio info.
 */
typedef struct {
  char codec_info[128];           //!<Audio codec specific information
  uint32_t sample_rate;       //!<sample rate
  uint32_t channels;          //!<audio channels
  uint32_t pkt_pts_increment; //!<Stands for PTS duration of a audio packet
  uint32_t sample_size;       //!<How many bytes an audio sample has
  uint32_t chunk_size;        //!<How many bytes an audio packet has
  int32_t  sample_format;     //!<Audio sample format
  int32_t type;         //!<Audio type @sa AM_AUDIO_TYPE
} amba_audio_info_t;

typedef struct {
  int32_t     error;
  char message[128];
} aac_status_msg_t;

/*! Test if a and b are equal, this is case insensitive
 * @param a C style string, must NOT be NULL
 * @param b C style string, must NOT be NULL
 * @return true if a and b are equal, otherwise return false
 */
#ifndef is_str_equal
#define is_str_equal(a,b) \
  ((strlen(a) == strlen(b)) && (0 == strcasecmp(a,b)))
#endif

#endif /* AM_AUDIO_DEFINE_H_ */
