/*
 * bitstream_parse.c
 *
 * History:
 *    5/25/2022 - [Zhi He] created file
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

#include "debug_log.h"

#include "internal.h"

unsigned int avc_get_next_frame(unsigned char * pstart,
  unsigned char * pend, unsigned char * p_nal_type,
  unsigned char * need_more_data, unsigned char * reach_tail)
{
  unsigned char *pcur = pstart;
  unsigned int state = 0;
  unsigned int is_header = 1;

  unsigned char nal_type;

  *need_more_data = 0;
  *p_nal_type = 0;

  while (pcur < pend) {
    switch (state) {
      case 0:
        if (*pcur++ == 0x0) {
          state = 1;
        }
        break;
      case 1: //0
        if (*pcur++ == 0x0) {
          state = 2;  // 0 0
        } else {
          state = 0;
        }
        break;
      case 2: //0 0 or 0 0 0 or . . .
        if (*pcur == 0x1) {
          state = 3; // 0 0 1 or 0 0 0 1
        } else if (*pcur != 0x0) {
          state = 0;
        }
        pcur ++;
        break;
      case 3: //0 0 1 or 0 0 0 1
        nal_type = (*pcur) & 0x1F;
        if (ENalType_END_OF_STREAM == nal_type) {
          //end of bitstream
          if (is_header) {
            printf("eos comes, pcur + 1 - pstart is %ld\n", (unsigned long) (pcur + 1 - pstart));
            *p_nal_type = nal_type;
            return pcur + 1 - pstart;
          } else {
            if ((pcur > (pstart + 4)) && (*(pcur - 4) == 0)) {
              printf("before eos in bit-stream, pcur - 4 - pstart is %ld\n", (unsigned long) (pcur - 4 - pstart));
              return pcur - 4 - pstart;
            } else {
              printf("before eos in bit-stream, pcur - 3 - pstart is %ld\n", (unsigned long) (pcur - 3 - pstart));
              return pcur - 3 - pstart;
            }
          }
        } else if ((*pcur)) { //nal uint type
          if (nal_type >= ENalType_NON_IDR_BEGIN && nal_type <= ENalType_IDR) {
            if (!is_header) {
              if (pcur + 16 < pend) {
                if (pcur[1] & 0x80) {
                  if ((pcur > (pstart + 4)) && (*(pcur - 4) == 0)) {
                    return pcur - 4 - pstart;
                  } else {
                    return pcur - 3 - pstart;
                  }
                } else {
                  state = 0;
                }
              } else {
                *need_more_data = 1;
                return 0;
              }
            } else {
              if (pcur + 16 < pend) {
                *p_nal_type = nal_type;
                state = 0;
                is_header = 0;
              } else {
                printf("[error]: must not comes here!\n");
                *need_more_data = 1;
                return 0;
              }
            }
          } else if (nal_type >= ENalType_SEI && nal_type <= ENalType_END_OF_SEQUENCE) {
            if (!is_header) {
              if ((pcur > (pstart + 4)) && (*(pcur - 4) == 0)) {
                return pcur - 4 - pstart;
              } else {
                return pcur - 3 - pstart;
              }
            }
            state = 0;
          } else {
            printf("[error]: why comes here? nal_type %d\n", nal_type);
            state = 0;
          }
        } else {
          state = 1;
        }
        pcur ++;
        break;

      default:
        printf("[error]: must not comes here! state %d\n", state);
        state = 0;
        break;
    }
  }

  if (pcur >= pend) {
    if (is_header) {
      printf("error: reach tail, but not find frame header\n");
    }
    *reach_tail = 1;
  }

  *need_more_data = 1;
  return 0;
}

unsigned int hevc_get_next_frame(unsigned char * pstart,
  unsigned char * pend, unsigned char * p_nal_type,
  unsigned char * need_more_data, unsigned char * reach_tail)
{
  unsigned char *pcur = pstart;
  unsigned int state = 0;
  unsigned int is_header = 1;

  unsigned char   nal_type;

  *need_more_data = 0;
  *p_nal_type = 0;

  while (pcur < pend) {
    switch (state) {
      case 0:
        if (*pcur++ == 0x0) {
          state = 1;
        }
        break;
      case 1://0
        if (*pcur++ == 0x0) {
          state = 2;
        } else {
          state = 0;
        }
        break;
      case 2://0 0
        if (*pcur == 0x1) {
          state = 3;
        } else if (*pcur != 0x0) {
          state = 0;
        }
        pcur ++;
        break;
      case 3://0 0 1
        nal_type = ((*pcur) & 0x7E) >> 1;
        if (EHEVCNalType_EOB == nal_type) {
          //end of bitstream
          if (is_header) {
            printf("eos comes, pcur + 1 - pstart is %ld\n", (unsigned long) (pcur + 1 - pstart));
            *p_nal_type = nal_type;
            return pcur + 1 - pstart;
          } else {
            if ((pcur > (pstart + 4)) && (*(pcur - 4) == 0)) {
              printf("before eos in bit-stream, pcur - 4 - pstart is %ld\n", (unsigned long) (pcur - 4 - pstart));
              return pcur - 4 - pstart;
            } else {
              printf("before eos in bit-stream, pcur - 3 - pstart is %ld\n", (unsigned long) (pcur - 3 - pstart));
              return pcur - 3 - pstart;
            }
          }
        } else {
          if (EHEVCNalType_VPS <= nal_type) {
            if (!is_header) {
              if ((pcur > (pstart + 4)) && (*(pcur - 4) == 0)) {
                return pcur - 4 - pstart;
              } else {
                return pcur - 3 - pstart;
              }
            }
            state = 0;
          } else {
            if (is_header) {
              *p_nal_type = nal_type;
              is_header = 0;
              state = 0;
            } else {
              if (pcur + 4 < pend) {
                if (pcur[2] & 0x80) {
                  if ((pcur > (pstart + 4)) && (*(pcur - 4) == 0)) {
                    return pcur - 4 - pstart;
                  } else {
                    return pcur - 3 - pstart;
                  }
                } else {
                  state = 0;
                }
              } else {
                *need_more_data = 1;
                return 0;
              }
            }
          }
        }
        pcur ++;
        break;

      default:
        printf("[error]: must not comes here! state %d\n", state);
        state = 0;
        break;
    }
  }

  if (pcur >= pend) {
    if (is_header) {
      printf("error: reach tail, but not find frame header\n");
    }
    *reach_tail = 1;
  }

  *need_more_data = 1;
  return 0;
}

