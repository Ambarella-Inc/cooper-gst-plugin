/*
 * codec_interface.c
 *
 * History:
 *    11/18/2013 - [Zhi He] created file
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

#include "stdlib.h"
#include "stdio.h"
#include "string.h"

#include "internal.h"

#include "codec_interface.h"

extern SCodecVideoH264 *gstGetVideoCodecH264Parser (guchar *p_data, gulong data_size, int &ret);

SCodecVideoCommon *gstGetVideoCodecParser (guchar *p_data, gulong data_size, StreamFormat format, int &ret)
{
  g_assert (p_data);
  g_assert (data_size);

  if (G_LIKELY (p_data && data_size) ) {
    switch (format) {
      case StreamFormat_H264:
        return (SCodecVideoCommon *) gstGetVideoCodecH264Parser (p_data, data_size, ret);

      default:
        GST_ERROR ("gstGetVideoCodecParser, unsupported format=%d\n", format);
        return NULL;
    }

  } else {
    GST_ERROR ("invalid params, p_data=%p data_size=%lu\n", p_data, data_size);
    return NULL;
  }
}

void gstReleaseVideoCodecParser (SCodecVideoCommon *parser)
{
  if (G_LIKELY (parser) ) {
    free (parser);

  } else {
    GST_ERROR ("NULL parser\n");
  }
}

guint gstGetADTSFrameLength (guchar *p)
{
  return ( ( (guint) (p[3] & 0x3) ) << 11) | ( ( (guint) p[4]) << 3) | ( ( (guint) p[5] >> 5) & 0x7);
}

int gstParseADTSHeader (guchar *p, SADTSHeader *header)
{
  if (G_UNLIKELY ( (!p) || (!header) ) ) {
    GST_ERROR ("NULL parameters in gstParseADTSHeader\n");
    return COM_ECODE_BAD_PARAMS;
  }

  if (G_UNLIKELY ( (0xff != p[0]) || (0xf0 != (p[1] & 0xf0) ) ) ) {
    GST_ERROR ("not find sync byte in gstParseADTSHeader\n");
    return int_DataCorruption;
  }

  header->ID = (p[1] >> 3) & 0x01;
  header->layer = (p[1] >> 1) & 0x03;
  header->protection_absent = p[1] & 0x01;

  header->profile = (p[2] >> 6) & 0x03;
  header->sampling_frequency_index = (p[2] >> 2) & 0x0f;
  header->private_bit = (p[2] >> 1) & 0x01;
  header->channel_configuration = ( (p[2] & 0x01) << 2) | ( (p[3] >> 6) & 0x3);
  header->original_copy = (p[3] >> 5) & 0x01;
  header->home = (p[3] >> 4) & 0x01;

  header->copyright_identification_bit = (p[3] >> 3) & 0x01;
  header->copyright_identification_start = (p[3] >> 2) & 0x01;

  header->aac_frame_length = ( ( (gushort) p[3] & 0x03) << 11) | ( (gushort) p[4] << 3) | ( ( (gushort) p[5] >> 5) & 0x7);

  header->adts_buffer_fullness = ( ( (gushort) p[5] & 0x1f) << 6) | ( ( (gushort) p[6] >> 2) & 0x3f);
  header->number_of_raw_data_blocks_in_frame = p[6] & 0x03;

  return COM_ECODE_OK;
}

int gstBuildADTSHeader (guchar *p,
                           guchar ID, guchar layer, guchar protection_absent, guchar profile, guchar sampling_frequency_index, guchar private_bit, guchar channel_configuration,
                           guchar original_copy, guchar home, guchar copyright_identification_bit, guchar copyright_identification_start,
                           guint frame_length, gushort adts_buffer_fullness, guchar number_of_raw_data_blocks_in_frame)
{
  if (G_UNLIKELY (!p) ) {
    GST_ERROR ("NULL parameters in gstBuildADTSHeader\n");
    return COM_ECODE_BAD_PARAMS;
  }

  p[0] = 0xFF;
  p[1] = (0xF0) | ( (ID & 0x01) << 3) | ( (layer & 0x03) << 1) | (protection_absent & 0x01);

  p[2] = ( (profile & 0x03) << 6) | ( (sampling_frequency_index & 0x0f) << 2) | ( (private_bit & 0x01) << 1) | ( (channel_configuration >> 2) & 0x01);
  p[3] = ( (channel_configuration & 0x03) << 6) | ( (original_copy & 0x01) << 5) | ( (home & 0x01) << 4) | ( (copyright_identification_bit & 0x01) << 3)
         | ( (copyright_identification_start & 0x01) << 3) | ( (frame_length >> 11) & 0x03);
  p[4] = (frame_length >> 3) & 0xFF;
  p[5] = ( (frame_length & 0x07) << 5) | ( (adts_buffer_fullness >> 6) & 0x1F);
  p[6] = ( (adts_buffer_fullness & 0x3f) << 2) | (number_of_raw_data_blocks_in_frame & 0x03);

  return COM_ECODE_OK;
}

guint gstGetADTSSamplingstrequency (guchar sampling_frequency_index)
{
  switch (sampling_frequency_index) {

    case EADTSSamplingstrequency_96000:
      return 96000;
      break;

    case EADTSSamplingstrequency_88200:
      return 88200;
      break;

    case EADTSSamplingstrequency_64000:
      return 64000;
      break;

    case EADTSSamplingstrequency_48000:
      return 48000;
      break;

    case EADTSSamplingstrequency_44100:
      return 44100;
      break;

    case EADTSSamplingstrequency_32000:
      return 32000;
      break;

    case EADTSSamplingstrequency_24000:
      return 24000;
      break;

    case EADTSSamplingstrequency_22050:
      return 22050;
      break;

    case EADTSSamplingstrequency_16000:
      return 16000;
      break;

    case EADTSSamplingstrequency_12000:
      return 12000;
      break;

    case EADTSSamplingstrequency_11025:
      return 11025;
      break;

    case EADTSSamplingstrequency_8000:
      return 8000;
      break;

    case EADTSSamplingstrequency_7350:
      return 7350;
      break;

    default:
      break;
  }

  return 0;
}


