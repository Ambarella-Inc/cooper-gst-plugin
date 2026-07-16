/*
 * drawdatagen_bbox.c
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
 * Bounding box overlay - draws detection rectangles and labels.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "drawdatagen_bbox.h"
#include "drawdatagen_common.h"
#include "amba_ml_decoded_result.h"
#include "amba_draw_data_string.h"
#include "osd_draw_types.h"

#define BBOX_COLOR_IDX_DEFAULT  DRAWDATAGEN_BBOX_CLUT_INDEX
#define BBOX_THICKNESS_DEFAULT  3
#define BBOX_LABEL_MAX_LEN      96
#define BBOX_LABEL_GAP_PX       2 /* gap between bbox edge and label bar when label is outside the box */
#define LANDMARK_HALF_PX        2 /* (2*half+1)x(2*half+1) filled landmark marker */

/* Default bbox line: full red via same packing as bmp_to_* (overlay). */
static uint32_t bbox_color_for_format(int draw_format)
{
  amba_draw_clut_t red;

  if (draw_format == AMBA_DRAW_FORMAT_8BIT_CLUT)
    return 0;
  if (drawdatagen_osd_hex_is_rgb_packed(draw_format)) {
    red.y = 255;
    red.u = 0;
    red.v = 0;
    red.a = 255;
  } else {
    red.y = DRAWDATAGEN_BBOX_CLUT_Y;
    red.u = DRAWDATAGEN_BBOX_CLUT_U;
    red.v = DRAWDATAGEN_BBOX_CLUT_V;
    red.a = DRAWDATAGEN_BBOX_CLUT_A;
  }
  return drawdatagen_clut_to_pixel(&red, draw_format);
}

static void draw_rect_8bit(unsigned char *content, unsigned int pitch,
    unsigned int area_w, unsigned int area_h,
    int x, int y, int w, int h, unsigned char color, int thickness)
{
  unsigned int i, rx, ry, rw, rh;

  if (x < 0)
    x = 0;
  if (y < 0)
    y = 0;
  if (x + w > (int)area_w)
    w = area_w - x;
  if (y + h > (int)area_h)
    h = area_h - y;
  if (w <= 0 || h <= 0)
    return;

  rx = (unsigned int)x;
  ry = (unsigned int)y;
  rw = (unsigned int)w;
  rh = (unsigned int)h;
  if (thickness <= 0) {
    thickness = 1;
  }

  if ((unsigned int)(thickness * 2) > rw || (unsigned int)(thickness * 2) > rh) {
    for (i = 0; i < rh; i++) {
      memset(content + (ry + i) * pitch + rx, color, rw);
    }
    return;
  }
  for (i = 0; i < (unsigned int)thickness; i++) {
    memset(content + (ry + i) * pitch + rx, color, rw);
  }
  for (i = thickness; i < rh - thickness; i++) {
    memset(content + (ry + i) * pitch + rx, color, (unsigned int)thickness);
    memset(content + (ry + i) * pitch + rx + rw - thickness, color, (unsigned int)thickness);
  }
  for (i = rh - thickness; i < rh; i++) {
    memset(content + (ry + i) * pitch + rx, color, rw);
  }
}

static void draw_rect_16bit(unsigned char *content, unsigned int pitch,
    unsigned int area_w, unsigned int area_h,
    int x, int y, int w, int h, uint16_t color, int thickness)
{
  unsigned int i, j, rx, ry, rw, rh;

  if (x < 0)
    x = 0;
  if (y < 0)
    y = 0;
  if (x + w > (int)area_w)
    w = area_w - x;
  if (y + h > (int)area_h)
    h = area_h - y;
  if (w <= 0 || h <= 0)
    return;

  rx = (unsigned int)x;
  ry = (unsigned int)y;
  rw = (unsigned int)w;
  rh = (unsigned int)h;
  if (thickness <= 0) {
    thickness = 1;
  }

  for (i = 0; i < rh; i++) {
    uint16_t *row = (uint16_t *)(content + (ry + i) * pitch);
    int full_row = (i < (unsigned int)thickness || i >= rh - (unsigned int)thickness);
    if (full_row) {
      for (j = 0; j < rw; j++) {
        row[rx + j] = color;
      }
    } else {
      for (j = 0; j < (unsigned int)thickness && j < rw; j++) {
        row[rx + j] = color;
      }
      for (j = (rw > (unsigned int)thickness) ? rw - (unsigned int)thickness : 0; j < rw; j++) {
        row[rx + j] = color;
      }
    }
  }
}

static void draw_rect_32bit(unsigned char *content, unsigned int pitch,
    unsigned int area_w, unsigned int area_h,
    int x, int y, int w, int h, uint32_t color, int thickness)
{
  unsigned int i, j, rx, ry, rw, rh;

  if (x < 0)
    x = 0;
  if (y < 0)
    y = 0;
  if (x + w > (int)area_w)
    w = area_w - x;
  if (y + h > (int)area_h)
    h = area_h - y;
  if (w <= 0 || h <= 0)
    return;

  rx = (unsigned int)x;
  ry = (unsigned int)y;
  rw = (unsigned int)w;
  rh = (unsigned int)h;
  if (thickness <= 0) {
    thickness = 1;
  }

  for (i = 0; i < rh; i++) {
    uint32_t *row = (uint32_t *)(content + (ry + i) * pitch);
    int full_row = (i < (unsigned int)thickness || i >= rh - (unsigned int)thickness);
    if (full_row) {
      for (j = 0; j < rw; j++) {
        row[rx + j] = color;
      }
    } else {
      for (j = 0; j < (unsigned int)thickness && j < rw; j++) {
        row[rx + j] = color;
      }
      for (j = (rw > (unsigned int)thickness) ? rw - (unsigned int)thickness : 0; j < rw; j++) {
        row[rx + j] = color;
      }
    }
  }
}

static void draw_face_landmarks(const amba_rect_t *roi,
    unsigned char *data_buf, unsigned int area_pitch,
    const amba_ml_detection_t *d, int draw_format,
    unsigned char lm_clut_idx, uint16_t lm_px_u16, uint32_t lm_px_u32)
{
  int pi;
  const int half = LANDMARK_HALF_PX;
  const int side = half * 2 + 1;

  if ((d->flags & AMBA_ML_DETECTION_FLAG_HAS_LANDMARKS) == 0)
    return;

  for (pi = 0; pi < 5; pi++) {
    int cx = (int)d->landmark_x[pi] - (int)roi->x;
    int cy = (int)d->landmark_y[pi] - (int)roi->y;
    int x0 = cx - half;
    int y0 = cy - half;
    int bw = side;
    int bh = side;

    if (bw <= 0 || bh <= 0)
      continue;
    if (x0 < 0) {
      bw += x0;
      x0 = 0;
    }
    if (y0 < 0) {
      bh += y0;
      y0 = 0;
    }
    if (x0 + bw > (int)roi->width)
      bw = (int)roi->width - x0;
    if (y0 + bh > (int)roi->height)
      bh = (int)roi->height - y0;
    if (bw <= 0 || bh <= 0)
      continue;

    if (draw_format == AMBA_DRAW_FORMAT_8BIT_CLUT) {
      draw_rect_8bit(data_buf, area_pitch, roi->width, roi->height,
          x0, y0, bw, bh, lm_clut_idx, side);
    } else if (draw_format >= AMBA_DRAW_FORMAT_16BIT_FIRST &&
        draw_format < AMBA_DRAW_FORMAT_16BIT_LAST) {
      draw_rect_16bit(data_buf, area_pitch, roi->width, roi->height,
          x0, y0, bw, bh, lm_px_u16, side);
    } else if (draw_format >= AMBA_DRAW_FORMAT_32BIT_FIRST &&
        draw_format < AMBA_DRAW_FORMAT_32BIT_LAST) {
      draw_rect_32bit(data_buf, area_pitch, roi->width, roi->height,
          x0, y0, bw, bh, lm_px_u32, side);
    }
  }
}

int drawdatagen_bbox_draw(const amba_rect_t *roi,
    const void *ml_result,
    unsigned int det_num,
    unsigned char color_idx,
    int thickness,
    amba_draw_clut_t *clut,
    unsigned char *data_buf,
    unsigned int area_pitch,
    const char *font_file,
    int font_size,
    amba_overlay_area_param_t *area_str,
    bitmap_buffer_t *bitmap,
    int draw_format,
    int append_mode,
    uint32_t bg_color_hex,
    uint32_t line_color_hex)
{
  const amba_ml_bbox_result_t *res = (const amba_ml_bbox_result_t *)ml_result;
  unsigned int k;
  int draw_labels = (font_file && font_file[0] && font_size > 0 && area_str && bitmap) ? 1 : 0;
  int use_custom_line = (line_color_hex != 0);
  amba_draw_clut_t line_clut;
  uint32_t line_px_u32 = 0;
  uint16_t line_px_u16 = 0;
  uint32_t lm_px_u32 = 0;
  uint16_t lm_px_u16 = 0;

  if (!roi || !clut || !data_buf)
    return -1;
  if (color_idx == 0)
    color_idx = BBOX_COLOR_IDX_DEFAULT;
  if (thickness <= 0)
    thickness = BBOX_THICKNESS_DEFAULT;

  if (use_custom_line) {
    if (drawdatagen_osd_hex_is_rgb_packed(draw_format)) {
      line_clut.v = (unsigned char)((line_color_hex >> 24) & 0xff);
      line_clut.u = (unsigned char)((line_color_hex >> 16) & 0xff);
      line_clut.y = (unsigned char)((line_color_hex >> 8) & 0xff);
      line_clut.a = (unsigned char)(line_color_hex & 0xff);
    } else {
      line_clut.v = (unsigned char)((line_color_hex >> 24) & 0xff);
      line_clut.u = (unsigned char)((line_color_hex >> 16) & 0xff);
      line_clut.y = (unsigned char)((line_color_hex >> 8) & 0xff);
      line_clut.a = (unsigned char)(line_color_hex & 0xff);
    }
  }
  if (draw_format != AMBA_DRAW_FORMAT_8BIT_CLUT) {
    if (use_custom_line)
      line_px_u32 = drawdatagen_clut_to_pixel(&line_clut, draw_format);
    else
      line_px_u32 = bbox_color_for_format(draw_format);
    line_px_u16 = (uint16_t)line_px_u32;
  }

  if (draw_format != AMBA_DRAW_FORMAT_8BIT_CLUT) {
    amba_draw_clut_t lm_clut;

    lm_clut.y = DRAWDATAGEN_LANDMARK_CLUT_Y;
    lm_clut.u = DRAWDATAGEN_LANDMARK_CLUT_U;
    lm_clut.v = DRAWDATAGEN_LANDMARK_CLUT_V;
    lm_clut.a = DRAWDATAGEN_LANDMARK_CLUT_A;
    lm_px_u32 = drawdatagen_clut_to_pixel(&lm_clut, draw_format);
    lm_px_u16 = (uint16_t)lm_px_u32;
  }

  if (!append_mode) {
    /* CLUT: background + box color. Default: DRAWDATAGEN_BBOX_CLUT_*; optional line_color_hex (YUV: v/u/y/a; RGB565: b/g/r/a). */
    memset(clut, 0, OVERLAY_CLUT_SIZE);
    for (k = 1; k < OVERLAY_CLUT_MAX_NUM - 1; k++) {
      clut[k].v = 0;
      clut[k].u = 128;
      clut[k].y = 255;
      clut[k].a = 255;
    }
    if (use_custom_line) {
      clut[color_idx] = line_clut;
    } else {
      clut[DRAWDATAGEN_BBOX_CLUT_INDEX].y = DRAWDATAGEN_BBOX_CLUT_Y;
      clut[DRAWDATAGEN_BBOX_CLUT_INDEX].u = DRAWDATAGEN_BBOX_CLUT_U;
      clut[DRAWDATAGEN_BBOX_CLUT_INDEX].v = DRAWDATAGEN_BBOX_CLUT_V;
      clut[DRAWDATAGEN_BBOX_CLUT_INDEX].a = DRAWDATAGEN_BBOX_CLUT_A;
    }
    drawdatagen_fill_background_for_area(clut, data_buf, roi->height, (size_t)area_pitch, draw_format, bg_color_hex);
  } else if (use_custom_line && draw_format == AMBA_DRAW_FORMAT_8BIT_CLUT) {
    clut[color_idx] = line_clut;
  }

  if (draw_format == AMBA_DRAW_FORMAT_8BIT_CLUT) {
    clut[DRAWDATAGEN_LANDMARK_CLUT_INDEX].y = DRAWDATAGEN_LANDMARK_CLUT_Y;
    clut[DRAWDATAGEN_LANDMARK_CLUT_INDEX].u = DRAWDATAGEN_LANDMARK_CLUT_U;
    clut[DRAWDATAGEN_LANDMARK_CLUT_INDEX].v = DRAWDATAGEN_LANDMARK_CLUT_V;
    clut[DRAWDATAGEN_LANDMARK_CLUT_INDEX].a = DRAWDATAGEN_LANDMARK_CLUT_A;
  }

  if (res && det_num > 0) {
    if (det_num > AMBA_ML_DETECTION_MAX_NUM)
      det_num = AMBA_ML_DETECTION_MAX_NUM;
    for (k = 0; k < det_num; k++) {
      const amba_ml_detection_t *d = &res->detections.detections[k];
      {
        int x1 = d->x_start, y1 = d->y_start, x2 = d->x_end, y2 = d->y_end;
        int bx = x1 - roi->x, by = y1 - roi->y, bw = x2 - x1, bh = y2 - y1;
        if (bw <= 0 || bh <= 0)
          continue;
        /* Clamp bbox to roi bounds to avoid any out-of-bounds (width/height) */
        if (bx < 0) {
          bw += bx;
          bx = 0;
        }
        if (by < 0) {
          bh += by;
          by = 0;
        }
        if (bx + bw > (int)roi->width)
          bw = roi->width - bx;
        if (by + bh > (int)roi->height)
          bh = roi->height - by;
        if (bw <= 0 || bh <= 0)
          continue;
        /* Draw label first, then bbox on top so bbox outline stays visible.
         * Text only when mlpostprocess filled a non-empty label (label file / class name). */
        if (draw_labels && d->label[0]) {
          char label_buf[BBOX_LABEL_MAX_LEN];
          /* Single-line; bitmap height can exceed font_size (outline/grid). amba_draw_data_string
           * requires rect.height >= font_height or convert_text_to_bmp fails. */
          int label_h = font_size + (font_size + 1) / 2 + 10;
          if (label_h < font_size + 22)
            label_h = font_size + 22;
          /*
           * Prefer drawing the label outside the bbox (above, else below) so segmentation
           * fill drawn underneath is not covered by opaque text background.
           */
          int ly = by - label_h - BBOX_LABEL_GAP_PX;
          if (ly < 0 && by + bh + BBOX_LABEL_GAP_PX + label_h <= (int)roi->height) {
            ly = by + bh + BBOX_LABEL_GAP_PX;
          } else if (ly < 0) {
            ly = by; /* top edge: not enough room above or below */
          }
          /* lw: wide enough for ~20 chars to avoid wrap; when bbox near right edge, extend label left */
          int lw = font_size * 18 + 32;
          int lx = bx;
          if (lw > (int)roi->width - bx) {
            lx = (int)roi->width - lw;
            if (lx < 0) {
              lw = roi->width;
              lx = 0;
            }
          }
          if (lw < 80) {
            lw = 80;
          }

          snprintf(label_buf, sizeof(label_buf), "%s %.2f", d->label, (double)d->score);
          label_buf[BBOX_LABEL_MAX_LEN - 1] = '\0';

          area_str->attr.rect = *roi;
          area_str->attr.rect.pitch = (int)area_pitch;
          area_str->data.type = AMBA_DRAW_DATA_TYPE_STRING;
          drawdatagen_strlcpy(area_str->data.text.str, label_buf, sizeof(area_str->data.text.str));
          area_str->data.text.font.width = font_size;
          area_str->data.text.font.height = font_size;
          drawdatagen_strlcpy(area_str->data.text.font.ttf_name,
              font_file, sizeof(area_str->data.text.font.ttf_name));
          area_str->data.rect.x = lx;
          area_str->data.rect.y = ly;
          area_str->data.rect.width = lw;
          area_str->data.rect.height = label_h;
          area_str->data.rect.pitch = area_pitch;

          amba_draw_str_data(area_str, clut, data_buf, bitmap, draw_format, 0);
        }
        if (draw_format == AMBA_DRAW_FORMAT_8BIT_CLUT) {
          draw_rect_8bit(data_buf, area_pitch, roi->width, roi->height,
              bx, by, bw, bh, color_idx, thickness);
        } else if (draw_format >= AMBA_DRAW_FORMAT_16BIT_FIRST &&
            draw_format < AMBA_DRAW_FORMAT_16BIT_LAST) {
          draw_rect_16bit(data_buf, area_pitch, roi->width, roi->height,
              bx, by, bw, bh, line_px_u16, thickness);
        } else if (draw_format >= AMBA_DRAW_FORMAT_32BIT_FIRST &&
            draw_format < AMBA_DRAW_FORMAT_32BIT_LAST) {
          draw_rect_32bit(data_buf, area_pitch, roi->width, roi->height,
              bx, by, bw, bh, line_px_u32, thickness);
        }
        draw_face_landmarks(roi, data_buf, area_pitch, d, draw_format,
            DRAWDATAGEN_LANDMARK_CLUT_INDEX, lm_px_u16, lm_px_u32);
      }
    }
  }
  /* init_text_info uses clut[5..13] for anti-aliasing; clut[0..4] reserved for text/bbox/seg. */
  return 0;
}
