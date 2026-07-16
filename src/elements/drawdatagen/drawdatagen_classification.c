/*
 * drawdatagen_classification.c
 *
 * History:
 *    5/9/2026 - [pxduan] created file
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
 * Classification top-k text overlay for amba_draw_data_gen.
 */

#include <stdio.h>
#include <string.h>

#include "drawdatagen_classification.h"
#include "drawdatagen_common.h"
#include "amba_draw_data_string.h"

#define CLS_MARGIN_X 10
#define CLS_MARGIN_Y 8
static int
cls_line_height(int font_size)
{
  int lh = font_size + (font_size + 1) / 2 + 10;
  if (lh < font_size + 22)
    lh = font_size + 22;
  return lh;
}

int drawdatagen_classification_draw(const amba_rect_t *roi,
    const amba_ml_classification_body_t *body,
    const char *font_file,
    int font_size,
    amba_overlay_area_param_t *area_str,
    bitmap_buffer_t *bitmap,
    amba_draw_clut_t *clut,
    unsigned char *data_buf,
    unsigned int area_pitch,
    int draw_format,
    unsigned char stream_rotate,
    uint32_t bg_color_hex,
    int append_mode)
{
  unsigned int k;
  unsigned int nlines;
  int line_h;
  int lx, ly, lh, lw;
  const char *ff;

  if (!roi || !body || !clut || !data_buf || !area_str || !bitmap)
    return -1;
  if (font_size <= 0)
    font_size = 16;
  ff = (font_file && font_file[0]) ? font_file : DRAWDATAGEN_DEFAULT_FONT_PATH;

  if (!append_mode) {
    memset(clut, 0, OVERLAY_CLUT_SIZE);
    drawdatagen_fill_background_for_area(clut, data_buf, roi->height, (size_t)area_pitch, draw_format, bg_color_hex);
  }

  area_str->attr.rect = *roi;
  area_str->attr.rect.pitch = roi->pitch > 0 ? roi->pitch : (int)area_pitch;
  area_str->data.type = AMBA_DRAW_DATA_TYPE_STRING;
  drawdatagen_strlcpy(area_str->data.text.font.ttf_name, ff,
      sizeof(area_str->data.text.font.ttf_name));
  area_str->data.text.font.width = font_size;
  area_str->data.text.font.height = font_size;

  nlines = body->top_k_out;
  if (nlines > AMBA_ML_CLASSIFICATION_TOPK_MAX)
    nlines = AMBA_ML_CLASSIFICATION_TOPK_MAX;

  line_h = cls_line_height(font_size);
  lx = CLS_MARGIN_X;
  ly = CLS_MARGIN_Y;
  lw = (int)roi->width - CLS_MARGIN_X * 2;
  if (lw < 80)
    lw = (int)roi->width > 80 ? ((int)roi->width - 4) : (int)roi->width;
  if (lw < 1)
    lw = (int)roi->width;

  lh = line_h;

  if (nlines == 0) {
    snprintf(area_str->data.text.str, sizeof(area_str->data.text.str),
        "(classification: classes=%u, no ranked entries)",
        (unsigned int)body->num_classes);
    area_str->data.text.str[sizeof(area_str->data.text.str) - 1] = '\0';

    area_str->data.rect.x = lx;
    area_str->data.rect.y = ly;
    area_str->data.rect.width = (unsigned int)lw;
    area_str->data.rect.height = (unsigned int)lh;
    area_str->data.rect.pitch = roi->pitch > 0 ? roi->pitch : (int)area_pitch;

    return amba_draw_str_data(area_str, clut, data_buf, bitmap, draw_format, stream_rotate);
  }

  for (k = 0; k < nlines; k++) {
    const char *lab = body->ranked[k].label[0] ? body->ranked[k].label : "";

    snprintf(area_str->data.text.str, sizeof(area_str->data.text.str),
        "#%u id:%d %.4f %.96s",
        (unsigned)(k + 1), (int)body->ranked[k].class_id,
        (double)body->ranked[k].score, lab);
    area_str->data.text.str[sizeof(area_str->data.text.str) - 1] = '\0';

    if (ly > (int)roi->height - (lh / 4))
      break;

    area_str->data.rect.x = lx;
    area_str->data.rect.y = ly;
    area_str->data.rect.width = (unsigned int)lw;
    area_str->data.rect.height = (unsigned int)lh;
    area_str->data.rect.pitch = roi->pitch > 0 ? roi->pitch : (int)area_pitch;

    if (amba_draw_str_data(area_str, clut, data_buf, bitmap, draw_format, stream_rotate) != 0) {
      return -1;
    }
    ly += lh + (line_h / 6 > 2 ? line_h / 6 : 2);
  }

  return 0;
}
