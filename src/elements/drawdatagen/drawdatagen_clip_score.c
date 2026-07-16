/*
 * drawdatagen_clip_score.c
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
 * CLIP / LongCLIP match-score text overlay for amba_draw_data_gen.
 */

#include <stdio.h>
#include <string.h>

#include "drawdatagen_clip_score.h"
#include "drawdatagen_common.h"
#include "amba_draw_data_string.h"

#define CLIP_SCORE_MARGIN_X 10
#define CLIP_SCORE_MARGIN_Y 8

int drawdatagen_clip_score_draw(const amba_rect_t *roi,
    const amba_ml_embedding_body_t *body,
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
  int lx, ly, lh, lw;
  const char *ff;
  const char *lab;

  if (!roi || !body || !clut || !data_buf || !area_str || !bitmap)
    return -1;
  if (font_size <= 0)
    font_size = 16;
  ff = (font_file && font_file[0]) ? font_file : DRAWDATAGEN_DEFAULT_FONT_PATH;

  if (!append_mode) {
    memset(clut, 0, OVERLAY_CLUT_SIZE);
    drawdatagen_fill_background_for_area(clut, data_buf, roi->height, (size_t)area_pitch,
        draw_format, bg_color_hex);
  }

  area_str->attr.rect = *roi;
  area_str->attr.rect.pitch = roi->pitch > 0 ? roi->pitch : (int)area_pitch;
  area_str->data.type = AMBA_DRAW_DATA_TYPE_STRING;
  drawdatagen_strlcpy(area_str->data.text.font.ttf_name, ff,
      sizeof(area_str->data.text.font.ttf_name));
  area_str->data.text.font.width = font_size;
  area_str->data.text.font.height = font_size;

  lx = CLIP_SCORE_MARGIN_X;
  ly = CLIP_SCORE_MARGIN_Y;
  lw = (int)roi->width - CLIP_SCORE_MARGIN_X * 2;
  if (lw < 80)
    lw = (int)roi->width > 80 ? ((int)roi->width - 4) : (int)roi->width;
  if (lw < 1)
    lw = (int)roi->width;
  lh = font_size + (font_size + 1) / 2 + 10;
  if (lh < font_size + 22)
    lh = font_size + 22;

  lab = body->match_label[0] ? body->match_label : "ref";
  if (body->match_valid) {
    snprintf(area_str->data.text.str, sizeof(area_str->data.text.str),
        "%.96s score=%.4f", lab, (double)body->match_score);
  } else {
    snprintf(area_str->data.text.str, sizeof(area_str->data.text.str),
        "CLIP dim=%u (no ref)", (unsigned int)body->dim);
  }
  area_str->data.text.str[sizeof(area_str->data.text.str) - 1] = '\0';

  area_str->data.rect.x = lx;
  area_str->data.rect.y = ly;
  area_str->data.rect.width = (unsigned int)lw;
  area_str->data.rect.height = (unsigned int)lh;
  area_str->data.rect.pitch = roi->pitch > 0 ? roi->pitch : (int)area_pitch;

  return amba_draw_str_data(area_str, clut, data_buf, bitmap, draw_format, stream_rotate);
}
