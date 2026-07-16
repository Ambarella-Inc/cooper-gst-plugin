/*
 * drawdatagen_time.c
 *
 * History:
 *    3/3/2026 - [pxduan] created file
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
 *
 * Time overlay - calls amba_draw_time_data.
 */

#include <string.h>

#include "internal.h"
#include "drawdatagen_common.h"
#include "drawdatagen_time.h"
#include "amba_draw_data_time.h"

int drawdatagen_time_draw(const char *pre_str,
    const char *suf_str,
    int en_msec,
    int format,
    int is_12h,
    const char *font_file,
    int font_size,
    const amba_rect_t *roi,
    amba_overlay_area_param_t *area_time,
    amba_draw_clut_t *clut,
    unsigned char *data_buf,
    bitmap_buffer_t *bitmap,
    int draw_format,
    unsigned char stream_rotate)
{
  int area_pitch;

  if (!roi || !area_time || !clut || !data_buf || !bitmap)
    return -1;

  /* Must match draw_one_area_per_area buffer pitch (8/16/32bit); do not assume 1 byte/pixel. */
  area_pitch = roi->pitch > 0 ? roi->pitch : drawdatagen_area_pitch(roi->width, 1);

  area_time->attr.rect = *roi;
  area_time->attr.rect.pitch = area_pitch;
  area_time->data.type = AMBA_DRAW_DATA_TYPE_TIME;
  if (pre_str)
    drawdatagen_strlcpy(area_time->data.time.pre_str, pre_str, sizeof(area_time->data.time.pre_str));
  else
    area_time->data.time.pre_str[0] = '\0';
  if (suf_str)
    drawdatagen_strlcpy(area_time->data.time.suf_str, suf_str, sizeof(area_time->data.time.suf_str));
  else
    area_time->data.time.suf_str[0] = '\0';
  area_time->data.time.en_msec = en_msec;
  area_time->data.time.format = format;
  area_time->data.time.is_12h = is_12h;
  area_time->data.time.text.font.width = font_size;
  area_time->data.time.text.font.height = font_size;
  drawdatagen_strlcpy(area_time->data.time.text.font.ttf_name,
      (font_file && font_file[0]) ? font_file : DRAWDATAGEN_DEFAULT_FONT_PATH,
      sizeof(area_time->data.time.text.font.ttf_name));
  area_time->data.rect.x = 0;
  area_time->data.rect.y = 0;
  area_time->data.rect.width = roi->width;
  area_time->data.rect.height = roi->height;
  area_time->data.rect.pitch = area_pitch;

  return amba_draw_time_data(area_time, clut, data_buf, bitmap, draw_format, stream_rotate);
}
