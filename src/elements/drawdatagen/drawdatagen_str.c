/*
 * drawdatagen_str.c
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
 * String overlay - calls amba_draw_str_data.
 */

#include <string.h>

#include "internal.h"
#include "drawdatagen_common.h"
#include "drawdatagen_str.h"
#include "amba_draw_data_string.h"

int drawdatagen_str_draw(const char *str,
    const char *font_file,
    int font_size,
    const amba_rect_t *roi,
    amba_overlay_area_param_t *area_str,
    amba_draw_clut_t *clut,
    unsigned char *data_buf,
    bitmap_buffer_t *bitmap,
    int draw_format,
    unsigned char stream_rotate)
{
  int area_pitch;

  if (!roi || !area_str || !clut || !data_buf || !bitmap)
    return -1;

  /* Must match draw_one_area_per_area buffer pitch (8/16/32bit); do not assume 1 byte/pixel. */
  area_pitch = roi->pitch > 0 ? roi->pitch : drawdatagen_area_pitch(roi->width, 1);

  area_str->attr.rect = *roi;
  area_str->attr.rect.pitch = area_pitch;
  area_str->data.type = AMBA_DRAW_DATA_TYPE_STRING;
  if (str)
    drawdatagen_strlcpy(area_str->data.text.str, str, sizeof(area_str->data.text.str));
  else
    area_str->data.text.str[0] = '\0';
  area_str->data.text.font.width = font_size;
  area_str->data.text.font.height = font_size;
  drawdatagen_strlcpy(area_str->data.text.font.ttf_name,
      (font_file && font_file[0]) ? font_file : DRAWDATAGEN_DEFAULT_FONT_PATH,
      sizeof(area_str->data.text.font.ttf_name));
  area_str->data.rect.x = 0;
  area_str->data.rect.y = 0;
  area_str->data.rect.width = roi->width;
  area_str->data.rect.height = roi->height;
  area_str->data.rect.pitch = area_pitch;

  return amba_draw_str_data(area_str, clut, data_buf, bitmap, draw_format, stream_rotate);
}

void drawdatagen_text_deinit(amba_overlay_area_param_t *area_str,
    amba_overlay_area_param_t *area_time)
{
  if (area_str && area_str->data.text.m_bitmap.m_font_lib_init) {
    deinit_textinsert_lib(&area_str->data.text.m_bitmap);
  }
  if (area_time && area_time->data.text.m_bitmap.m_font_lib_init) {
    deinit_textinsert_lib(&area_time->data.text.m_bitmap);
  }
}
