/*
 * utils.c
 *
 * History:
 *    7/28/2015 - [Zhi He] created file
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

#include "utils.h"

unsigned int gstSkipDelimter (unsigned char *p)
{
  if ( (0 == p[0]) && (0 == p[1]) && (0 == p[2]) && (1 == p[3]) && (ENalType_AUD == (p[4] & 0x1f) ) ) {
    return 6;
  }

  return 0;
}

unsigned int gstSkipSEI (unsigned char *p, unsigned int len)
{
  unsigned char *po = p;
  unsigned int state = 0;
  unsigned int find_sei_nalu_type = 0;

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

        } else if (1 == (*p) ) {
          if (ENalType_SEI == (p[1] & 0x1f) ) {
            find_sei_nalu_type = 1;

          } else {
            if (!find_sei_nalu_type) {
              return 0;
            }

            return (unsigned int) ( (p - 2) - po);
          }

          state = 0;

        } else {
          state = 0;
        }

        break;

      case 3: //0 0 0
        if (! (*p) ) {
          state = 3;

        } else if (1 == (*p) ) {
          if (ENalType_SEI == (p[1] & 0x1f) ) {
            find_sei_nalu_type = 1;

          } else {
            if (!find_sei_nalu_type) {
              return 0;
            }

            return (unsigned int) ( (p - 3) - po);
          }

          state = 0;

        } else {
          state = 0;
        }

        break;

      default:
        printf ("impossible to comes here\n");
        break;

    }

    p ++;
    len --;
  }

  return 0;
}

unsigned int gstSkipDelimterHEVC (unsigned char *p)
{
  if ( (0 == p[0]) && (0 == p[1]) && (0 == p[2]) && (1 == p[3]) && (EHEVCNalType_AUD == ( (p[4] >> 1) & 0x3f) ) ) {
    return 7;
  }

  return 0;
}

unsigned int gfSkipSEIHEVC (unsigned char *p, unsigned int len)
{
  unsigned char *po = p;
  unsigned int state = 0;
  unsigned char nal_type = 0;
  unsigned int find_sei_nalu_type = 0;

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

        } else if (1 == (*p) ) {
          nal_type = ( (p[1] >> 1) & 0x3f);

          if ( (EHEVCNalType_SUFFIX_SEI == nal_type) || (EHEVCNalType_PREFIX_SEI == nal_type) ) {
            find_sei_nalu_type = 1;

          } else {
            if (!find_sei_nalu_type) {
              return 0;
            }

            return (unsigned int) ( (p - 2) - po);
          }

          state = 0;

        } else {
          state = 0;
        }

        break;

      case 3: //0 0 0
        if (! (*p) ) {
          state = 3;

        } else if (1 == (*p) ) {
          nal_type = ( (p[1] >> 1) & 0x3f);

          if ( (EHEVCNalType_SUFFIX_SEI == nal_type) || (EHEVCNalType_PREFIX_SEI == nal_type) ) {
            find_sei_nalu_type = 1;

          } else {
            if (!find_sei_nalu_type) {
              return 0;
            }

            return (unsigned int) ( (p - 3) - po);
          }

          state = 0;

        } else {
          state = 0;
        }

        break;

      default:
        printf ("impossible to comes here\n");
        break;

    }

    p ++;
    len --;
  }

  return 0;
}

void gstFillAmbaH264GopHeader(unsigned char *p_gop_header, unsigned int frame_tick, unsigned int time_scale, unsigned int pts, unsigned char gopsize, unsigned char m)
{
  DUNUSED(pts);
  unsigned int tick_high = frame_tick;
  unsigned int tick_low = tick_high & 0x0000ffff;
  unsigned int scale_high = time_scale;
  unsigned int scale_low = scale_high & 0x0000ffff;
  unsigned int pts_high = 0;
  unsigned int pts_low = 0;

  tick_high >>= 16;
  scale_high >>= 16;

  p_gop_header[0] = 0; // start code prefix
  p_gop_header[1] = 0;
  p_gop_header[2] = 0;
  p_gop_header[3] = 1;

  p_gop_header[4] = 0x7a; // nal type = 0x1a
  p_gop_header[5] = 0x01; // version main
  p_gop_header[6] = 0x01; // version sub

  p_gop_header[7] = tick_high >> 10;
  p_gop_header[8] = tick_high >> 2;
  p_gop_header[9] = (tick_high << 6) | (1 << 5) | (tick_low >> 11);
  p_gop_header[10] = tick_low >> 3;

  p_gop_header[11] = (tick_low << 5) | (1 << 4) | (scale_high >> 12);
  p_gop_header[12] = scale_high >> 4;
  p_gop_header[13] = (scale_high << 4) | (1 << 3) | (scale_low >> 13);
  p_gop_header[14] = scale_low >> 5;

  p_gop_header[15] = (scale_low << 3) | (1 << 2) | (pts_high >> 14);
  p_gop_header[16] = pts_high >> 6;

  p_gop_header[17] = (pts_high << 2) | (1 << 1) | (pts_low >> 15);
  p_gop_header[18] = pts_low >> 7;
  p_gop_header[19] = (pts_low << 1) | 1;

  p_gop_header[20] = gopsize;
  p_gop_header[21] = (m & 0xf) << 4;
}

void gstUpdateAmbaH264GopHeader(unsigned char *p_gop_header, unsigned int pts, unsigned char gopsize)
{
  unsigned int pts_high = (pts >> 16) & 0x0000ffff;
  unsigned int pts_low = pts & 0x0000ffff;

  p_gop_header[15] = (p_gop_header[15]  & 0xFC) | (pts_high >> 14);
  p_gop_header[16] = pts_high >> 6;

  p_gop_header[17] = (pts_high << 2) | (1 << 1) | (pts_low >> 15);
  p_gop_header[18] = pts_low >> 7;
  p_gop_header[19] = (pts_low << 1) | 1;

  p_gop_header[20] = gopsize;
}

void gstFillAmbaH265GopHeader(unsigned char *p_gop_header, unsigned int frame_tick, unsigned int time_scale, unsigned int pts, unsigned char gopsize, unsigned char m)
{
  DUNUSED(pts);
  unsigned int tick_high = frame_tick;
  unsigned int tick_low = tick_high & 0x0000ffff;
  unsigned int scale_high = time_scale;
  unsigned int scale_low = scale_high & 0x0000ffff;
  unsigned int pts_high = 0;
  unsigned int pts_low = 0;

  tick_high >>= 16;
  scale_high >>= 16;

  p_gop_header[0] = 0; // start code prefix
  p_gop_header[1] = 0;
  p_gop_header[2] = 0;
  p_gop_header[3] = 1;

  p_gop_header[4] = 0x34; // nal type = 0x1a
  p_gop_header[5] = 0x01;
  p_gop_header[6] = 0x01; // version main
  p_gop_header[7] = 0x01; // version sub

  p_gop_header[8] = tick_high >> 10;
  p_gop_header[9] = tick_high >> 2;
  p_gop_header[10] = (tick_high << 6) | (1 << 5) | (tick_low >> 11);
  p_gop_header[11] = tick_low >> 3;

  p_gop_header[12] = (tick_low << 5) | (1 << 4) | (scale_high >> 12);
  p_gop_header[13] = scale_high >> 4;
  p_gop_header[14] = (scale_high << 4) | (1 << 3) | (scale_low >> 13);
  p_gop_header[15] = scale_low >> 5;

  p_gop_header[16] = (scale_low << 3) | (1 << 2) | (pts_high >> 14);
  p_gop_header[17] = pts_high >> 6;

  p_gop_header[18] = (pts_high << 2) | (1 << 1) | (pts_low >> 15);
  p_gop_header[19] = pts_low >> 7;
  p_gop_header[20] = (pts_low << 1) | 1;

  p_gop_header[21] = gopsize;
  p_gop_header[22] = (m & 0xf) << 4;
}

void gstUpdateAmbaH265GopHeader(unsigned char *p_gop_header, unsigned int pts, unsigned char gopsize)
{
  unsigned int pts_high = (pts >> 16) & 0x0000ffff;
  unsigned int pts_low = pts & 0x0000ffff;

  p_gop_header[16] = (p_gop_header[16]  & 0xFC) | (pts_high >> 14);
  p_gop_header[17] = pts_high >> 6;

  p_gop_header[18] = (pts_high << 2) | (1 << 1) | (pts_low >> 15);
  p_gop_header[19] = pts_low >> 7;
  p_gop_header[20] = (pts_low << 1) | 1;

  p_gop_header[21] = gopsize;
}

