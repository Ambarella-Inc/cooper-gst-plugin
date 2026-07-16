/*
 * drawdatagen_bmp.c
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
 * BMP overlay - calls amba_draw_pic_data.
 */

#include <string.h>

#include "internal.h"
#include "drawdatagen_common.h"
#include "drawdatagen_bmp.h"
#include "amba_draw_data_picture.h"

int drawdatagen_bmp_draw(const char *path,
    const amba_rect_t *roi,
    amba_draw_clut_t *clut,
    unsigned char *data_buf,
    bitmap_buffer_t *bitmap,
    int draw_format,
    unsigned int area_pitch)
{
  amba_overlay_area_param_t area_param = {0};

  if (!path || path[0] == '\0' || !roi || !clut || !data_buf || !bitmap)
    return -1;

  area_param.attr.rect = *roi;
  area_param.attr.rect.pitch = area_pitch;
  area_param.data.type = AMBA_DRAW_DATA_TYPE_PICTURE;
  drawdatagen_strlcpy(area_param.data.pic.filename, path, sizeof(area_param.data.pic.filename));
  area_param.data.pic.use_bmp_alpha = 0;
  area_param.data.pic.alpha = 255;
  area_param.data.rect.width = roi->width;
  area_param.data.rect.height = roi->height;
  area_param.data.rect.pitch = area_pitch;

  return amba_draw_pic_data(&area_param, clut, data_buf, bitmap, draw_format);
}

int drawdatagen_bmp_draw_from_buffer(const unsigned char *bmp_data, size_t bmp_size,
    const amba_rect_t *roi,
    amba_draw_clut_t *clut,
    unsigned char *data_buf,
    bitmap_buffer_t *bitmap,
    int draw_format,
    unsigned int area_pitch)
{
  amba_overlay_area_param_t area_param = {0};

  if (!bmp_data || bmp_size == 0 || !roi || !clut || !data_buf || !bitmap)
    return -1;

  area_param.attr.rect = *roi;
  area_param.attr.rect.pitch = area_pitch;
  area_param.data.type = AMBA_DRAW_DATA_TYPE_PICTURE;
  area_param.data.pic.use_bmp_alpha = 0;
  area_param.data.pic.alpha = 255;
  area_param.data.rect.width = roi->width;
  area_param.data.rect.height = roi->height;
  area_param.data.rect.pitch = area_pitch;

  return amba_draw_pic_data_from_buffer(bmp_data, bmp_size, &area_param,
      clut, data_buf, bitmap, draw_format);
}
