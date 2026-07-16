/*
 * overlay_common.c
 *
 * History:
 *    4/01/2024 - [pxduan] created file
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

#include "pthread.h"

#include "common_err_code_c.h"

#include "internal.h"

#include "debug_log.h"

#include "iav_al.h"

#include "overlay_common.h"

#if 0
int alloc_overlay_size_ave_with_stream(iav_map_overlay_t *map_overlay, unsigned int stream_num,
    unsigned int area_num)
{
  /* Split into MAX_ENCODE_STREAM parts as overlay memory management.
  * This is just example to evenly split the overlay memory for all streams.
  * For customer real use case, it's freely to split the overlay memory unevenly
  * for each stream.
  */
  unsigned char *mem = NULL;
  if (stream_num <= 0) {
    stream_num = OSD_DEFAULT_ENCODE_STREAM_NUM;
    DPRINT_WARNING("not specify stream number, default %d\n", OSD_DEFAULT_ENCODE_STREAM_NUM);
  }

  if (area_num <= 0) {
    area_num = OSD_DEFAULT_AREA_NUM;
    DPRINT_WARNING("not specify area number, default %d\n", OSD_DEFAULT_AREA_NUM);
  }

  int overlay_obj_max = map_overlay->stream_max_num * map_overlay->area_max_num;

  if (!map_overlay->overlay_size_alloc) {
    if (!map_overlay->stream_max_num) {
      map_overlay->stream_max_num = stream_num;
    } else {
      if (map_overlay->stream_max_num < stream_num) {
        map_overlay->stream_max_num = stream_num;
      }
    }

    if (!map_overlay->area_max_num) {
      map_overlay->area_max_num = area_num;
    } else {
      if (map_overlay->area_max_num < area_num) {
        map_overlay->area_max_num = area_num;
      }
    }

    overlay_obj_max = map_overlay->stream_max_num * map_overlay->area_max_num;

    /*if (!map_overlay->update_intervals) {
      map_overlay->update_intervals = OSD_DEFAULT_UPDATE_INTERVALS;
      DPRINT_WARNING("not setup update_intervals, default %d\n", map_overlay->update_intervals);
    }*/

    //map_overlay->clut_offset = 0;
    //map_overlay->yuv_offset = OVERLAY_CLUT_SIZE * overlay_obj_max;
    //map_overlay->clut_num = OVERLAY_CLUT_NUM / map_overlay->stream_max_num;
    map_overlay->area_max_size = (map_overlay->size -  OVERLAY_CLUT_SIZE * overlay_obj_max) /
        overlay_obj_max;

    /*map_overlay->buffer_num = (map_overlay->update_intervals >= DOUBLE_BUFFER_MIN_UPDATE_INTERVALS ?
        2 : OSD_MAX_BUFFER_NUM);
    map_overlay->overlay_yuv_size /= map_overlay->buffer_num;*/

    memset(map_overlay->base, 0, map_overlay->size);

    //DPRINT_INFO("overlay: use [%d] buffer strategy. overlay size per stream is 0x%x\n",
        //map_overlay->buffer_num, map_overlay->overlay_yuv_size);
    map_overlay->overlay_size_alloc = 1;
  } else {
    if (stream_num + map_overlay->cur_stream_num > map_overlay->stream_max_num) {
      DPRINT_ERROR("not enough max stream number %d, expect %d\n", map_overlay->stream_max_num,
          stream_num + map_overlay->cur_stream_num);
      return -1;
    }

    if (area_num > map_overlay->area_max_num) {
      DPRINT_ERROR("not enough max area number %d, expect %d\n", map_overlay->area_max_num,
          area_num);
      return -1;
    }

  }

  map_overlay->cur_clut_addr = map_overlay->base +
      OVERLAY_CLUT_SIZE * map_overlay->area_max_num * map_overlay->cur_stream_num;
  map_overlay->cur_yuv_addr = map_overlay->base + OVERLAY_CLUT_SIZE * overlay_obj_max + map_overlay->area_max_size
      * map_overlay->area_max_num * map_overlay->cur_stream_num;
  // map_overlay->clut_offset += OVERLAY_CLUT_SIZE * map_overlay->area_max_num * stream_num;
  //map_overlay->yuv_offset += map_overlay->overlay_yuv_size * map_overlay->buffer_num
      //* map_overlay->area_max_num * stream_num;
  map_overlay->cur_stream_num += stream_num;

  return 0;
}
#endif
int get_clut_color_index(const amba_draw_clut_t *clut,
    amba_draw_clut_t *cluts,
    unsigned char *index)
{
  int ret = 0;
  unsigned char is_find = 0;
  unsigned char is_insert = 0;
  do {
    if (!clut || !cluts) {
      DPRINT_ERROR("Invalid pointer (cluts)\n");
      ret = -1;
      break;
    }
    if (clut->a == 0) {
      *index = AMBA_DRAW_CLUT_ENTRY_BACKGROUND;
      is_find = 1;
      break;
    }
    if (clut->y == cluts[AMBA_DRAW_CLUT_ENTRY_BACKGROUND].y && clut->u == cluts[AMBA_DRAW_CLUT_ENTRY_BACKGROUND].u
        && clut->v == cluts[AMBA_DRAW_CLUT_ENTRY_BACKGROUND].v && clut->a == cluts[AMBA_DRAW_CLUT_ENTRY_BACKGROUND].a) {
      *index = AMBA_DRAW_CLUT_ENTRY_BACKGROUND;
      is_find = 1;
      break;
    }
    const size_t count = OVERLAY_CLUT_MAX_NUM - 1;
    for (size_t i = 0; i < count; i++) {
      if (clut->y == cluts[i].y && clut->u == cluts[i].u
          && clut->v == cluts[i].v && clut->a == cluts[i].a) {
        *index = i;
        is_find = 1;
        break;
      }
    }

    if (!is_find) {
      //cluts[255] for the background color.
      for (size_t i = 0; i < count; i++) {
        amba_draw_clut_t *cl = &cluts[i];
        if (cl->y == 0 && cl->u == 0
            && cl->v == 0 && cl->a == 0) {
          *index = i;
          cl->y = clut->y;
          cl->u = clut->u;
          cl->v = clut->v;
          cl->a = clut->a;
          is_insert = 1;
          break;
        }
      }
      if (!is_insert) {
        DPRINT_ERROR("Area total color number is %ld now, can't add more!", count);
        ret = -1;
      }
    }
  } while (0);
  return ret;
}


