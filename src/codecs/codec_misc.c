/*
 * codec_misc.c
 *
 * History:
 *    8/26/2014 - [Zhi He] created file
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

unsigned char *gstNALUFindNextStartCode (unsigned char *p, unsigned int len)
{
  unsigned int state = 0;

  while (len) {
    switch (state) {
      case 0:
        if (! (*p) ) {
          state = 1;
        }

        break;

      case 1: //0
        if (! (*p) ) {
          state = 2;

        } else {
          state = 0;
        }

        break;

      case 2: //0 0
        if (! (*p) ) {
          state = 3;

        } else {
          state = 0;
        }

        break;

      case 3: //0 0 0
        if (! (*p) ) {
          state = 3;

        } else if (1 == (*p) ) {
          return (p + 1);

        } else {
          state = 0;
        }

        break;

      default:
        GST_ERROR ("impossible to comes here\n");
        break;

    }

    p++;
    len --;
  }

  return NULL;
}

unsigned char *gstNALUFindIDR (unsigned char *p, unsigned int len)
{
  if (G_UNLIKELY (NULL == p) ) {
    return NULL;
  }

  while (len) {
    if (*p == 0x00) {
      if (* (p + 1) == 0x00) {
        if (* (p + 2) == 0x00) {
          if (* (p + 3) == 0x01) {
            if ( ( (* (p + 4) ) & 0x1F) == ENalType_IDR) {
              return p;
            }
          }

        } else if (* (p + 2) == 0x01) {
          if ( ( (* (p + 3) ) & 0x1F) == ENalType_IDR) {
            return p;
          }
        }
      }
    }

    ++ p;
    len --;
  }

  return NULL;
}

unsigned char *gstNALUFindSPSHeader (unsigned char *p, unsigned int len)
{
  if (G_UNLIKELY (NULL == p) ) {
    return NULL;
  }

  while (len) {
    if (*p == 0x00) {
      if (* (p + 1) == 0x00) {
        if (* (p + 2) == 0x00) {
          if (* (p + 3) == 0x01) {
            if ( ( (* (p + 4) ) & 0x1F) == ENalType_SPS) {
              return p;
            }
          }
        }
      }
    }

    ++p;
    len --;
  }

  return NULL;
}

unsigned char *gstNALUFindPPSEnd (unsigned char *p, unsigned int len)
{
  unsigned int find_pps = 0;
  unsigned int find_sps = 0;
  unsigned int nal_type = 0;

  if (G_UNLIKELY (NULL == p) ) {
    return NULL;
  }

  while (len) {
    if (*p == 0x00) {
      if (* (p + 1) == 0x00) {
        if (* (p + 2) == 0x00) {
          if (* (p + 3) == 0x01) {
            nal_type = p[4] & 0x1F;

            if (ENalType_IDR == nal_type) {
              g_assert (find_sps);
              g_assert (find_pps);
              return p;

            } else if (ENalType_SPS == nal_type) {
              find_sps = 1;

            } else if (ENalType_PPS == nal_type) {
              find_pps = 1;

            } else {
              if (find_pps) {
                g_assert (find_sps);
                return p;
              }
            }
          }
        }
      }
    }

    ++ p;
    len --;
  }

  return NULL;
}

unsigned char *gstGenerateAACExtraData (unsigned int samplerate, unsigned int channel_number, unsigned int &size)
{
  SSimpleAudioSpecificConfig *p_simple_header = NULL;

  size = 2;
  p_simple_header = (SSimpleAudioSpecificConfig *) malloc ( (size + 3) & (~3), "GAAE");
  p_simple_header->audioObjectType = eAudioObjectType_AAC_LC;//hard code here

  switch (samplerate) {
    case 44100:
      samplerate = eSamplingstrequencyIndex_44100;
      break;

    case 48000:
      samplerate = eSamplingstrequencyIndex_48000;
      break;

    case 24000:
      samplerate = eSamplingstrequencyIndex_24000;
      break;

    case 16000:
      samplerate = eSamplingstrequencyIndex_16000;
      break;

    case 8000:
      samplerate = eSamplingstrequencyIndex_8000;
      break;

    case 12000:
      samplerate = eSamplingstrequencyIndex_12000;
      break;

    case 32000:
      samplerate = eSamplingstrequencyIndex_32000;
      break;

    case 22050:
      samplerate = eSamplingstrequencyIndex_22050;
      break;

    case 11025:
      samplerate = eSamplingstrequencyIndex_11025;
      break;

    default:
      GST_ERROR ("NOT support sample rate (%d) here.\n", samplerate);
      break;
  }

  p_simple_header->samplingstrequencyIndex_high = samplerate >> 1;
  p_simple_header->samplingstrequencyIndex_low = samplerate & 0x1;
  p_simple_header->channelConfiguration = channel_number;
  p_simple_header->bitLeft = 0;

  return (unsigned char *) p_simple_header;
}

void gstParseAACExtraData (unsigned char *extra_data, unsigned int *samplerate, unsigned int *channel_number)
{
  if (extra_data && samplerate && channel_number) {
    SSimpleAudioSpecificConfig *p_simple_header = (SSimpleAudioSpecificConfig *) extra_data;
    unsigned int samplingstrequencyIndex =
      ( (unsigned int) p_simple_header->samplingstrequencyIndex_high << 1) | ( (unsigned int) p_simple_header->samplingstrequencyIndex_low);

    switch (samplingstrequencyIndex) {
      case eSamplingstrequencyIndex_44100:
        samplerate[0] = 44100;
        break;

      case eSamplingstrequencyIndex_48000:
        samplerate[0] = 48000;
        break;

      case eSamplingstrequencyIndex_24000:
        samplerate[0] = 24000;
        break;

      case eSamplingstrequencyIndex_16000:
        samplerate[0] = 16000;
        break;

      case eSamplingstrequencyIndex_8000:
        samplerate[0] = 8000;
        break;

      case eSamplingstrequencyIndex_12000:
        samplerate[0] = 12000;
        break;

      case eSamplingstrequencyIndex_32000:
        samplerate[0] = 32000;
        break;

      case eSamplingstrequencyIndex_22050:
        samplerate[0] = 22050;
        break;

      case eSamplingstrequencyIndex_11025:
        samplerate[0] = 11025;
        break;

      default:
        GST_ERROR ("bad sample rate index (%d)?.\n", samplingstrequencyIndex);
        break;
    }

    channel_number[0] = p_simple_header->channelConfiguration;
  }

  return;
}

unsigned char gstGetAACSamplingstrequencyIndex (unsigned int samplerate)
{
  switch (samplerate) {
    case 96000:
      return eSamplingstrequencyIndex_96000;
      break;

    case 88200:
      return eSamplingstrequencyIndex_88200;
      break;

    case 64000:
      return eSamplingstrequencyIndex_64000;
      break;

    case 48000:
      return eSamplingstrequencyIndex_48000;
      break;

    case 44100:
      return eSamplingstrequencyIndex_44100;
      break;

    case 24000:
      return eSamplingstrequencyIndex_24000;
      break;

    case 16000:
      return eSamplingstrequencyIndex_16000;
      break;

    case 8000:
      return eSamplingstrequencyIndex_8000;
      break;

    case 12000:
      return eSamplingstrequencyIndex_12000;
      break;

    case 32000:
      return eSamplingstrequencyIndex_32000;
      break;

    case 22050:
      return eSamplingstrequencyIndex_22050;
      break;

    case 11025:
      return eSamplingstrequencyIndex_11025;
      break;

    default:
      GST_ERROR ("NOT support sample rate (%d) here.\n", samplerate);
      break;
  }

  return 0;
}

int gstGetH264Extradata (unsigned char *data_base, unsigned int data_size, unsigned char *&p_extradata, unsigned int &extradata_size)
{
  unsigned char has_sps = 0, has_pps = 0;

  unsigned char *ptr = data_base, *ptr_end = data_base + data_size;
  unsigned int nal_type = 0;

  if (G_UNLIKELY ( (NULL == ptr) || (!data_size) ) ) {
    GST_ERROR ("NULL pointer(%p) or zero size(%d)\n", data_base, data_size);
    return COM_ECODE_BAD_PARAMS;
  }

  p_extradata = NULL;
  extradata_size = 0;

  while (ptr < ptr_end) {
    if (*ptr == 0x00) {
      if (* (ptr + 1) == 0x00) {
        if (* (ptr + 2) == 0x00) {
          if (* (ptr + 3) == 0x01) {
            nal_type = ptr[4] & 0x1F;

            if (ENalType_IDR == nal_type) {
              g_assert (has_sps);
              g_assert (has_pps);
              g_assert (p_extradata);
              extradata_size = (unsigned int) (ptr - p_extradata);
              return COM_ECODE_OK;

            } else if (ENalType_SPS == nal_type) {
              has_sps = 1;
              p_extradata = ptr;
              ptr += 3;

            } else if (ENalType_PPS == nal_type) {
              g_assert (has_sps);
              g_assert (p_extradata);
              has_pps = 1;
              ptr += 3;

            } else if (has_sps && has_pps) {
              g_assert (p_extradata);
              extradata_size = (unsigned int) (ptr - p_extradata);
              return COM_ECODE_OK;
            }
          }

        } else if (* (ptr + 2) == 0x01) {
          nal_type = ptr[3] & 0x1F;

          if (ENalType_IDR == nal_type) {
            g_assert (has_sps);
            g_assert (has_pps);
            g_assert (p_extradata);
            extradata_size = (unsigned int) (ptr - p_extradata);
            return COM_ECODE_OK;

          } else if (ENalType_SPS == nal_type) {
            has_sps = 1;
            p_extradata = ptr;
            ptr += 2;

          } else if (ENalType_PPS == nal_type) {
            g_assert (has_sps);
            g_assert (p_extradata);
            has_pps = 1;
            ptr += 2;

          } else if (has_sps && has_pps) {
            g_assert (p_extradata);
            extradata_size = (unsigned int) (ptr - p_extradata);
            return COM_ECODE_OK;
          }
        }
      }
    }

    ++ptr;
  }

  if (p_extradata) {
    extradata_size = (unsigned int) (data_base + data_size - p_extradata);
    return COM_ECODE_OK;
  }

  return int_NotExist;
}

int gstGetH264SPSPPS (unsigned char *data_base, unsigned int data_size, unsigned char *&p_sps, unsigned int &sps_size, unsigned char *&p_pps, unsigned int &pps_size)
{
  unsigned char has_sps = 0, has_pps = 0;

  unsigned char *ptr = data_base, *ptr_end = data_base + data_size;
  unsigned int nal_type = 0;

  if (G_UNLIKELY ( (NULL == ptr) || (!data_size) ) ) {
    GST_ERROR ("NULL pointer(%p) or zero size(%d)\n", data_base, data_size);
    return COM_ECODE_BAD_PARAMS;
  }

  while (ptr < ptr_end) {
    if (*ptr == 0x00) {
      if (* (ptr + 1) == 0x00) {
        if (* (ptr + 2) == 0x00) {
          if (* (ptr + 3) == 0x01) {
            nal_type = ptr[4] & 0x1F;

            if (ENalType_SPS == nal_type) {
              has_sps = 1;
              p_sps = ptr;
              ptr += 3;

            } else if (ENalType_PPS == nal_type) {
              g_assert (has_sps);
              g_assert (p_sps);
              p_pps = ptr;
              has_pps = 1;
              ptr += 3;

            } else if (has_sps && has_pps) {
              sps_size = (unsigned int) (p_pps - p_sps);
              pps_size = (unsigned int) (ptr - p_pps);
              return COM_ECODE_OK;
            }
          }

        } else if (* (ptr + 2) == 0x01) {
          nal_type = ptr[3] & 0x1F;

          if (ENalType_SPS == nal_type) {
            has_sps = 1;
            p_sps = ptr;
            ptr += 2;

          } else if (ENalType_PPS == nal_type) {
            g_assert (has_sps);
            has_pps = 1;
            p_pps = ptr;
            ptr += 2;

          } else if (has_sps && has_pps) {
            sps_size = (unsigned int) (p_pps - p_sps);
            pps_size = (unsigned int) (ptr - p_pps);
            return COM_ECODE_OK;
          }
        }
      }
    }

    ++ptr;
  }

  if (has_sps && has_pps) {
    sps_size = (unsigned int) (p_pps - p_sps);
    pps_size = (unsigned int) (ptr - p_pps);
    return COM_ECODE_OK;
  }

  return int_NotExist;
}

int gstGetH265Extradata (unsigned char *data_base, unsigned int data_size, unsigned char *&p_extradata, unsigned int &extradata_size)
{
  unsigned char has_vps = 0, has_sps = 0, has_pps = 0;

  unsigned char *ptr = data_base, *ptr_end = data_base + data_size;
  unsigned int nal_type = 0;

  if (G_UNLIKELY ( (NULL == ptr) || (!data_size) ) ) {
    GST_ERROR ("NULL pointer(%p) or zero size(%d)\n", data_base, data_size);
    return COM_ECODE_BAD_PARAMS;
  }

  p_extradata = NULL;
  extradata_size = 0;

  while (ptr < ptr_end) {
    if (*ptr == 0x00) {
      if (* (ptr + 1) == 0x00) {
        if (* (ptr + 2) == 0x00) {
          if (* (ptr + 3) == 0x01) {
            nal_type = (ptr[4] >> 1) & 0x3F;

            if ( (EHEVCNalType_IDR_W_RADL == nal_type) || (EHEVCNalType_IDR_N_LP == nal_type) ) {
              g_assert (has_vps);
              g_assert (has_sps);
              g_assert (has_pps);
              g_assert (p_extradata);
              extradata_size = (unsigned int) (ptr - p_extradata);
              return COM_ECODE_OK;

            } else if (EHEVCNalType_VPS == nal_type) {
              has_vps = 1;
              p_extradata = ptr;
              ptr += 3;

            } else if (EHEVCNalType_SPS == nal_type) {
              g_assert (has_vps);
              g_assert (p_extradata);
              has_sps = 1;
              ptr += 3;

            } else if (EHEVCNalType_PPS == nal_type) {
              g_assert (has_vps);
              g_assert (has_sps);
              g_assert (p_extradata);
              has_pps = 1;
              ptr += 3;

            } else if (has_vps && has_sps && has_pps) {
              g_assert (p_extradata);
              extradata_size = (unsigned int) (ptr - p_extradata);
              return COM_ECODE_OK;
            }
          }

        } else if (* (ptr + 2) == 0x01) {
          nal_type = (ptr[3] >> 1) & 0x3F;

          if ( (EHEVCNalType_IDR_W_RADL == nal_type) || (EHEVCNalType_IDR_N_LP == nal_type) ) {
            g_assert (has_vps);
            g_assert (has_sps);
            g_assert (has_pps);
            g_assert (p_extradata);
            extradata_size = (unsigned int) (ptr - p_extradata);
            return COM_ECODE_OK;

          } else if (EHEVCNalType_VPS == nal_type) {
            has_vps = 1;
            p_extradata = ptr;
            ptr += 2;

          } else if (EHEVCNalType_SPS == nal_type) {
            g_assert (has_vps);
            has_sps = 1;
            p_extradata = ptr;
            ptr += 2;

          } else if (EHEVCNalType_PPS == nal_type) {
            g_assert (has_vps);
            g_assert (has_sps);
            g_assert (p_extradata);
            has_pps = 1;
            ptr += 2;

          } else if (has_vps && has_sps && has_pps) {
            g_assert (p_extradata);
            extradata_size = (unsigned int) (ptr - p_extradata);
            return COM_ECODE_OK;
          }
        }
      }
    }

    ++ptr;
  }

  if (p_extradata) {
    extradata_size = (unsigned int) (data_base + data_size - p_extradata);
    return COM_ECODE_OK;
  }

  return int_NotExist;
}

int gstGetH265VPSSPSPPS (unsigned char *data_base, unsigned int data_size, unsigned char *&p_vps, unsigned int &vps_size, unsigned char *&p_sps, unsigned int &sps_size, unsigned char *&p_pps, unsigned int &pps_size)
{
  unsigned char has_vps = 0, has_sps = 0, has_pps = 0;

  unsigned char *ptr = data_base, *ptr_end = data_base + data_size;
  unsigned int nal_type = 0;

  if (G_UNLIKELY ( (NULL == ptr) || (!data_size) ) ) {
    GST_ERROR ("NULL pointer(%p) or zero size(%d)\n", data_base, data_size);
    return COM_ECODE_BAD_PARAMS;
  }

  while (ptr < ptr_end) {
    if (*ptr == 0x00) {
      if (* (ptr + 1) == 0x00) {
        if (* (ptr + 2) == 0x00) {
          if (* (ptr + 3) == 0x01) {
            nal_type = (ptr[4] >> 1) & 0x3F;

            if (EHEVCNalType_VPS == nal_type) {
              has_vps = 1;
              p_vps = ptr;
              ptr += 3;

            } else if (EHEVCNalType_SPS == nal_type) {
              g_assert (has_vps);
              p_sps = ptr;
              has_sps = 1;
              ptr += 3;

            } else if (EHEVCNalType_PPS == nal_type) {
              g_assert (has_vps);
              g_assert (has_sps);
              p_pps = ptr;
              has_pps = 1;
              ptr += 3;

            } else if (has_vps && has_sps && has_pps) {
              vps_size = (unsigned int) (p_sps - p_vps);
              sps_size = (unsigned int) (p_pps - p_sps);
              pps_size = (unsigned int) (ptr - p_pps);
              return COM_ECODE_OK;
            }
          }

        } else if (* (ptr + 2) == 0x01) {
          nal_type = (ptr[3] >> 1) & 0x3F;

          if (EHEVCNalType_VPS == nal_type) {
            has_vps = 1;
            p_vps = ptr;
            ptr += 2;

          } else if (EHEVCNalType_SPS == nal_type) {
            g_assert (has_vps);
            has_sps = 1;
            p_sps = ptr;
            ptr += 2;

          } else if (EHEVCNalType_PPS == nal_type) {
            g_assert (has_vps);
            g_assert (has_sps);
            has_pps = 1;
            p_pps = ptr;
            ptr += 2;

          } else if (has_vps && has_sps && has_pps) {
            vps_size = (unsigned int) (p_sps - p_vps);
            sps_size = (unsigned int) (p_pps - p_sps);
            pps_size = (unsigned int) (ptr - p_pps);
            return COM_ECODE_OK;
          }
        }
      }
    }

    ++ptr;
  }

  if (has_vps && has_sps && has_pps) {
    vps_size = (unsigned int) (p_sps - p_vps);
    sps_size = (unsigned int) (p_pps - p_sps);
    pps_size = (unsigned int) (ptr - p_pps);
    return COM_ECODE_OK;
  }

  return int_NotExist;
}

unsigned char *gstNALUFindFirstAVCSliceHeader (unsigned char *p, unsigned int len)
{
  if (G_UNLIKELY (NULL == p) ) {
    return NULL;
  }

  while (len > 5) {
    if (*p == 0x00) {
      if (* (p + 1) == 0x00) {
        if (* (p + 2) == 0x00) {
          if (* (p + 3) == 0x01) {
            if ( ( (* (p + 4) ) & 0x1F) <= ENalType_IDR) {
              return p;
            }
          }

        } else if (* (p + 2) == 0x01) {
          if ( ( (* (p + 3) ) & 0x1F) <= ENalType_IDR) {
            return p;
          }
        }
      }
    }

    ++ p;
    len --;
  }

  return NULL;
}

unsigned char * nalu_find_first_avc_slice_header_type (
  unsigned char * p, unsigned int len, unsigned char * out_nal_type)
{
  unsigned char nal_type = 0;

  if (!p) {
    return NULL;
  }

  while (len > 5) {
    if (*p == 0x00) {
      if (* (p + 1) == 0x00) {
        if (* (p + 2) == 0x00) {
          if (* (p + 3) == 0x01) {
            nal_type = (* (p + 4) ) & 0x1F;
            *out_nal_type = nal_type;
            if (nal_type <= ENalType_IDR) {
              return p;
            }
          }

        } else if (* (p + 2) == 0x01) {
          nal_type = (* (p + 3) ) & 0x1F;
          *out_nal_type = nal_type;
          if (nal_type <= ENalType_IDR) {
            return p;
          }
        }
      }
    }

    ++ p;
    len --;
  }

  return NULL;
}

unsigned char * nalu_find_first_hevc_slice_header_type (
  unsigned char * p, unsigned int len, unsigned char * out_nal_type, unsigned char * is_first_slice)
{
  unsigned char nal_type = 0;

  if (!p) {
    return NULL;
  }

  *is_first_slice = 0;

  while (len > 5) {
    if (*p == 0x00) {
      if (* (p + 1) == 0x00) {
        if (* (p + 2) == 0x00) {
          if (* (p + 3) == 0x01) {
            nal_type = ( ( (* (p + 4) ) >> 1) & 0x3F);
            *out_nal_type = nal_type;

            if (nal_type < EHEVCNalType_VPS) {
              if (p[6] & 0x80) {
                *is_first_slice = 1;
              } else {
                *is_first_slice = 0;
              }

              return p;
            }
          }

        } else if (* (p + 2) == 0x01) {
          nal_type = ( ( (* (p + 3) ) >> 1) & 0x3F);
          *out_nal_type = nal_type;

          if (nal_type < EHEVCNalType_VPS) {
            if (p[5] & 0x80) {
              *is_first_slice = 1;
            } else {
              *is_first_slice = 0;
            }

            return p;
          }
        }
      }
    }

    ++ p;
    len --;
  }

  return NULL;
}

unsigned char *gstNALUFindFirstHEVCVPSSPSPPSAndSliceNalType (unsigned char *p, unsigned int len, unsigned char &nal_type)
{
  if (G_UNLIKELY (NULL == p) ) {
    return NULL;
  }

  while (len > 5) {
    if (*p == 0x00) {
      if (* (p + 1) == 0x00) {
        if (* (p + 2) == 0x00) {
          if (* (p + 3) == 0x01) {
            nal_type = ( ( (* (p + 4) ) >> 1) & 0x3F);

            if (nal_type < EHEVCNalType_AUD) {
              return p;
            }
          }

        } else if (* (p + 2) == 0x01) {
          nal_type = ( ( (* (p + 3) ) >> 1) & 0x3F);

          if (nal_type < EHEVCNalType_AUD) {
            return p;
          }
        }
      }
    }

    ++ p;
    len --;
  }

  return NULL;
}

unsigned char *gstNALUFindFirstAVCNalType (unsigned char *p, unsigned int len, unsigned char &nal_type)
{
  unsigned char naltype = 0;

  if (G_UNLIKELY (NULL == p) ) {
    return NULL;
  }

  while (len > 5) {
    if (*p == 0x00) {
      if (* (p + 1) == 0x00) {
        if (* (p + 2) == 0x00) {
          if (* (p + 3) == 0x01) {
            naltype = ( (* (p + 4) ) & 0x1F);

            if ( (naltype <= ENalType_IDR) || (naltype == ENalType_SPS) || (naltype == ENalType_PPS) ) {
              nal_type = naltype;
              return p;
            }
          }

        } else if (* (p + 2) == 0x01) {
          naltype = ( (* (p + 3) ) & 0x1F);

          if ( (naltype <= ENalType_IDR) || (naltype == ENalType_SPS) || (naltype == ENalType_PPS) ) {
            nal_type = naltype;
            return p;
          }
        }
      }
    }

    ++ p;
    len --;
  }

  return NULL;
}

int gstParseJPEG (unsigned char *p, unsigned int size, SJPEGInfo *info)
{
  unsigned char *p_cur = p, *p_limit = p + size;
  unsigned char segment_type, start_image = 0, end_image = 0, start_scan = 0, start_frame = 0;
  unsigned int segment_length = 0;
  SJPEGQtTable *p_cur_table = NULL;

  if (G_UNLIKELY ( (!p) || (!size) || (!info) ) ) {
    GST_ERROR ("NULL (%p) or zero size(%d) or NULL info (%p)\n", p, size, info);
    return COM_ECODE_BAD_PARAMS;
  }

  if ( (EJPEG_MarkerPrefix != p[0]) || (EJPEG_SOI != p[1]) || (EJPEG_MarkerPrefix != p[2]) ) {
    GST_ERROR ("bad jpeg header: %02x %02x %02x %02x\n", p[0], p[1], p[2], p[3]);
    return int_DataCorruption;
  }

  info->width = 0;
  info->height = 0;
  info->type = 0;
  info->number_qt_tables = 0;
  info->precision = 0;
  memset (&info->qt_tables[0], 0x0, sizeof (info->qt_tables) );
  info->total_tables_length = 0;
  info->p_jpeg_content = NULL;
  info->jpeg_content_length = 0;
  info->p_app_reserved = NULL;
  info->app_reserved_length = 0;

  while (p_cur < p_limit) {

    if (EJPEG_MarkerPrefix != p_cur[0]) {
      GST_ERROR ("no marker prefix: %02x %02x %02x %02x\n", p_cur[0], p_cur[1], p_cur[2], p_cur[3]);
      return int_DataCorruption;
    }

    segment_type = p_cur[1];

    if (EJPEG_SOI == segment_type) {
      start_image = 1;
      p_cur += 2;

    } else if (EJPEG_EOI == segment_type) {
      GST_ERROR ("get EOI now?\n");
      return int_DataCorruption;

    } else if (EJPEG_SOS == segment_type) {
      start_scan = 1;
      p_cur += 2;
      DBER16 (segment_length, p_cur);
      p_cur += segment_length;
      info->p_jpeg_content = p_cur;

      unsigned char *p_end = p_limit - 6;

      while ( (p_end + 1) < p_limit) {
        if ( (EJPEG_MarkerPrefix == p_end[0]) && (EJPEG_EOI == p_end[1]) ) {
          end_image = 1;
          g_assert (info->p_jpeg_content);
          info->jpeg_content_length = (unsigned int) (p_end - info->p_jpeg_content);
          break;
        }

        p_end ++;
      }

      break;

    } else if (EJPEG_DQT == segment_type) {
      if ( (info->number_qt_tables + 1) >= DMAX_JPEG_QT_TABLE_NUMBER) {
        GST_ERROR ("too many tables? %d\n", info->number_qt_tables);
        return int_DataCorruption;
      }

      p_cur_table = &info->qt_tables[info->number_qt_tables];
      p_cur += 2;
      DBER16 (segment_length, p_cur);

      p_cur_table->p_table = p_cur + 3;

      if (0 == (p_cur[2] >> 4) ) {
        p_cur_table->length = 64;

      } else if (1 == (p_cur[2] >> 4) ) {
        info->precision |= (1 << info->number_qt_tables);
        p_cur_table->length = 128;

      } else {
        GST_ERROR ("bad precise\n");
        return int_DataCorruption;
      }

      info->total_tables_length += p_cur_table->length;
      p_cur += segment_length;
      info->number_qt_tables ++;

    } else if ( (EJPEG_APP_MIN <= segment_type) && (EJPEG_APP_MAX >= segment_type) ) {
      p_cur += 2;
      DBER16 (segment_length, p_cur);
      info->p_app_reserved = p_cur - 2;
      info->app_reserved_length = segment_length + 2;
      p_cur += segment_length;

    } else if ( (EJPEG_JPEG_MIN <= segment_type) && (EJPEG_JPEG_MAX >= segment_type) ) {
      p_cur += 2;
      DBER16 (segment_length, p_cur);
      p_cur += segment_length;

    } else if ( (EJPEG_REV_MIN <= segment_type) && (EJPEG_REV_MAX >= segment_type) ) {
      p_cur += 2;
      DBER16 (segment_length, p_cur);
      p_cur += segment_length;

    } else {
      switch (segment_type) {

        case EJPEG_SOF0:
        case EJPEG_SOF1:
        case EJPEG_SOF2:
        case EJPEG_SOF3:
        case EJPEG_SOF5:
        case EJPEG_SOF6:
        case EJPEG_SOF7:
        case EJPEG_SOF8:
        case EJPEG_SOF9:
        case EJPEG_SOF10:
        case EJPEG_SOF11:
        case EJPEG_SOF13:
        case EJPEG_SOF14:
        case EJPEG_SOF15: {
          g_assert (!start_frame);
          start_frame = 1;

          p_cur += 2;
          DBER16 (segment_length, p_cur);

          unsigned char *ptmp = p_cur + 3;
          DBER16 (info->height, ptmp);
          ptmp += 2;
          DBER16 (info->width, ptmp);
          ptmp += 2;

          unsigned int ncomponents = ptmp[0];
          ptmp ++;

          unsigned int sampling_factor_h[4] = {1};
          unsigned int sampling_factor_v[4] = {1};

          if (3 == ncomponents) {

            sampling_factor_h[0] = (ptmp[1] >> 4) & 0xf;
            sampling_factor_v[0] = (ptmp[1]) & 0xf;

            sampling_factor_h[1] = (ptmp[4] >> 4) & 0xf;
            sampling_factor_v[1] = (ptmp[4]) & 0xf;

            sampling_factor_h[2] = (ptmp[7] >> 4) & 0xf;
            sampling_factor_v[2] = (ptmp[7]) & 0xf;

            if ( (sampling_factor_h[1] == sampling_factor_h[2]) && ( (sampling_factor_h[1] << 1) == sampling_factor_h[0]) ) {
              if (sampling_factor_v[1] == sampling_factor_v[2]) {
                if ( (sampling_factor_v[1] << 1) == sampling_factor_v[0]) {
                  info->type = EJpegTypeInFrameHeader_YUV420;

                } else if (sampling_factor_v[1] == sampling_factor_v[0]) {
                  info->type = EJpegTypeInFrameHeader_YUV422;

                } else {
                  GST_ERROR ("only support 420 and 422. %02x %02x %02x\n", ptmp[1], ptmp[4], ptmp[7]);
                  gstPrintMemory (ptmp, 16);
                  return int_BadFormat;
                }

              } else {
                GST_ERROR ("v sampling not expected. %02x %02x %02x\n", ptmp[1], ptmp[4], ptmp[7]);
                gstPrintMemory (ptmp, 16);
                return int_BadFormat;
              }

            } else if ( (sampling_factor_h[1] == sampling_factor_h[2]) && (sampling_factor_h[1] == sampling_factor_h[0]) ) {
              info->type = EJpegTypeInFrameHeader_YUV444;

            }  else {
              GST_ERROR ("h sampling not expected. %02x %02x %02x\n", ptmp[1], ptmp[4], ptmp[7]);
              gstPrintMemory (ptmp, 16);
              return int_BadFormat;
            }

          } else if (1 == ncomponents) {
            info->type = EJpegTypeInFrameHeader_GREY8;

          } else {
            GST_WARNING ("not support components %d\n", ncomponents);
          }

          p_cur += segment_length;
        }
        break;

        case EJPEG_DHT:
        case EJPEG_DAT:
        case EJPEG_DNL:
        case EJPEG_DRI:
        case EJPEG_DHP:
        case EJPEG_EXP:
        case EJPEG_COMMENT:
          p_cur += 2;
          DBER16 (segment_length, p_cur);
          p_cur += segment_length;
          break;

        default:
          p_cur += 2;
          DBER16 (segment_length, p_cur);
          p_cur += segment_length;
          GST_WARNING ("unknown jpeg segment, %02x, %d\n", segment_type, segment_length);
          break;
      }
    }
  }

  if (!start_image) {
    GST_ERROR ("do not find soi\n");
    return int_DataMissing;
  }

  if (!start_frame) {
    GST_ERROR ("do not find sof\n");
    return int_DataMissing;
  }

  if (!start_scan) {
    GST_ERROR ("do not find sos\n");
    return int_DataMissing;
  }

  if (!end_image) {
    GST_ERROR ("do not find eoi\n");
    return int_DataMissing;
  }

  return COM_ECODE_OK;
}

void gstAmendVideoResolution (unsigned int &width, unsigned int &height)
{
  if ( (1920 == width) && (1088 == height) ) {
    height = 1080;
    GST_LOG ("Amend height to 1080\n");

  } else if ( (1920 == height) && (1088 == width) ) {
    width = 1080;
    GST_LOG ("Amend width to 1080\n");
  }
}

int gstGetH264SizeFromSPS (unsigned char *p_sps, unsigned int &width, unsigned int &height)
{
  int err = COM_ECODE_OK;
  SCodecVideoCommon *p_video_parser = gstGetVideoCodecParser (p_sps + 5, 40, StreamFormat_H264, err);

  if (G_UNLIKELY (!p_video_parser || COM_ECODE_OK != err) ) {
    GST_ERROR ("gstGetVideoCodecParser failed, ret %d, %s\n", err, gstGetErrorCodeString (err) );

    if (p_video_parser) {
      free (p_video_parser, "GAVC");
    }

    return int_DataCorruption;

  } else {
    width = p_video_parser->max_width;
    height = p_video_parser->max_height;
  }

  if (p_video_parser) {
    free (p_video_parser, "GAVC");
  }

  return err;
}

