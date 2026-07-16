/*
 * drawdatagen_pose.c
 *
 * History:
 *    5/25/2026 - [pxduan] created file
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
 * RTMPose skeleton overlay (17 COCO keypoints) for amba_draw_data_gen.
 */

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "drawdatagen_pose.h"
#include "drawdatagen_common.h"

#define POSE_LINE_NUM 19
#define POSE_KP_RADIUS_DEFAULT  3
#define POSE_LINE_THICK_DEFAULT 2

/* COCO skeleton edges (same order as model_garden rtmpose_utils.h) */
static const int s_pose_line_map[POSE_LINE_NUM][2] = {
  {0, 1}, {0, 2}, {2, 1}, {2, 3}, {1, 4}, {4, 5}, {3, 6},
  {5, 6}, {5, 7}, {6, 8}, {7, 9}, {8, 10}, {11, 12}, {11, 5},
  {12, 6}, {11, 13}, {12, 14}, {13, 15}, {14, 16},
};

static void draw_filled_disc_8bit(unsigned char *content, unsigned int pitch,
    unsigned int area_w, unsigned int area_h,
    int cx, int cy, int radius, unsigned char color)
{
  int y, x;
  int r2 = radius * radius;

  for (y = cy - radius; y <= cy + radius; y++) {
    if (y < 0 || y >= (int)area_h)
      continue;
    for (x = cx - radius; x <= cx + radius; x++) {
      int dx = x - cx;
      int dy = y - cy;
      if (x < 0 || x >= (int)area_w)
        continue;
      if (dx * dx + dy * dy <= r2)
        content[(size_t)y * pitch + (size_t)x] = color;
    }
  }
}

static void draw_line_8bit(unsigned char *content, unsigned int pitch,
    unsigned int area_w, unsigned int area_h,
    int x0, int y0, int x1, int y1, unsigned char color, int thickness)
{
  int dx = abs(x1 - x0);
  int sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0);
  int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  int half = thickness > 0 ? thickness / 2 : 0;

  for (;;) {
    int t;
    for (t = -half; t <= half; t++) {
      int px = x0 + t;
      int py = y0;
      if (px >= 0 && px < (int)area_w && py >= 0 && py < (int)area_h)
        content[(size_t)py * pitch + (size_t)px] = color;
      px = x0;
      py = y0 + t;
      if (px >= 0 && px < (int)area_w && py >= 0 && py < (int)area_h)
        content[(size_t)py * pitch + (size_t)px] = color;
    }
    if (x0 == x1 && y0 == y1)
      break;
    {
      int e2 = 2 * err;
      if (e2 >= dy) {
        err += dy;
        x0 += sx;
      }
      if (e2 <= dx) {
        err += dx;
        y0 += sy;
      }
    }
  }
}

static void draw_filled_disc_16bit(unsigned char *content, unsigned int pitch,
    unsigned int area_w, unsigned int area_h,
    int cx, int cy, int radius, uint16_t color)
{
  int y, x;
  int r2 = radius * radius;

  for (y = cy - radius; y <= cy + radius; y++) {
    if (y < 0 || y >= (int)area_h)
      continue;
    for (x = cx - radius; x <= cx + radius; x++) {
      int dx = x - cx;
      int dy = y - cy;
      uint16_t *row;
      if (x < 0 || x >= (int)area_w)
        continue;
      if (dx * dx + dy * dy <= r2) {
        row = (uint16_t *)(content + (size_t)y * pitch);
        row[x] = color;
      }
    }
  }
}

static void draw_line_16bit(unsigned char *content, unsigned int pitch,
    unsigned int area_w, unsigned int area_h,
    int x0, int y0, int x1, int y1, uint16_t color, int thickness)
{
  int dx = abs(x1 - x0);
  int sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0);
  int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  int half = thickness > 0 ? thickness / 2 : 0;

  for (;;) {
    int t;
    for (t = -half; t <= half; t++) {
      int px = x0 + t;
      int py = y0;
      uint16_t *row;
      if (px >= 0 && px < (int)area_w && py >= 0 && py < (int)area_h) {
        row = (uint16_t *)(content + (size_t)py * pitch);
        row[px] = color;
      }
      px = x0;
      py = y0 + t;
      if (px >= 0 && px < (int)area_w && py >= 0 && py < (int)area_h) {
        row = (uint16_t *)(content + (size_t)py * pitch);
        row[px] = color;
      }
    }
    if (x0 == x1 && y0 == y1)
      break;
    {
      int e2 = 2 * err;
      if (e2 >= dy) {
        err += dy;
        x0 += sx;
      }
      if (e2 <= dx) {
        err += dx;
        y0 += sy;
      }
    }
  }
}

static void draw_filled_disc_32bit(unsigned char *content, unsigned int pitch,
    unsigned int area_w, unsigned int area_h,
    int cx, int cy, int radius, uint32_t color)
{
  int y, x;
  int r2 = radius * radius;

  for (y = cy - radius; y <= cy + radius; y++) {
    if (y < 0 || y >= (int)area_h)
      continue;
    for (x = cx - radius; x <= cx + radius; x++) {
      int dx = x - cx;
      int dy = y - cy;
      uint32_t *row;
      if (x < 0 || x >= (int)area_w)
        continue;
      if (dx * dx + dy * dy <= r2) {
        row = (uint32_t *)(content + (size_t)y * pitch);
        row[x] = color;
      }
    }
  }
}

static void draw_line_32bit(unsigned char *content, unsigned int pitch,
    unsigned int area_w, unsigned int area_h,
    int x0, int y0, int x1, int y1, uint32_t color, int thickness)
{
  int dx = abs(x1 - x0);
  int sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0);
  int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  int half = thickness > 0 ? thickness / 2 : 0;

  for (;;) {
    int t;
    for (t = -half; t <= half; t++) {
      int px = x0 + t;
      int py = y0;
      uint32_t *row;
      if (px >= 0 && px < (int)area_w && py >= 0 && py < (int)area_h) {
        row = (uint32_t *)(content + (size_t)py * pitch);
        row[px] = color;
      }
      px = x0;
      py = y0 + t;
      if (px >= 0 && px < (int)area_w && py >= 0 && py < (int)area_h) {
        row = (uint32_t *)(content + (size_t)py * pitch);
        row[px] = color;
      }
    }
    if (x0 == x1 && y0 == y1)
      break;
    {
      int e2 = 2 * err;
      if (e2 >= dy) {
        err += dy;
        x0 += sx;
      }
      if (e2 <= dx) {
        err += dx;
        y0 += sy;
      }
    }
  }
}

int drawdatagen_pose_draw(const amba_rect_t *roi,
    const amba_ml_pose_body_t *pose,
    unsigned char color_idx,
    int line_thickness,
    int kp_radius,
    amba_draw_clut_t *clut,
    unsigned char *data_buf,
    unsigned int area_pitch,
    int draw_format,
    int append_mode,
    uint32_t bg_color_hex,
    uint32_t line_color_hex)
{
  int i, li;
  amba_draw_clut_t line_clut;
  uint32_t line_px_u32 = 0;
  uint16_t line_px_u16 = 0;
  int use_custom = (line_color_hex != 0);
  int kp_x[AMBA_ML_POSE_KEYPOINT_NUM];
  int kp_y[AMBA_ML_POSE_KEYPOINT_NUM];
  float kp_sc[AMBA_ML_POSE_KEYPOINT_NUM];

  if (!roi || !clut || !data_buf)
    return -1;
  if (!pose)
    return 0;

  if (color_idx == 0)
    color_idx = DRAWDATAGEN_POSE_CLUT_INDEX;
  if (line_thickness <= 0)
    line_thickness = POSE_LINE_THICK_DEFAULT;
  if (kp_radius <= 0)
    kp_radius = POSE_KP_RADIUS_DEFAULT;

  if (use_custom) {
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
    if (use_custom)
      line_px_u32 = drawdatagen_clut_to_pixel(&line_clut, draw_format);
    else {
      amba_draw_clut_t blue;
      blue.y = DRAWDATAGEN_POSE_CLUT_Y;
      blue.u = DRAWDATAGEN_POSE_CLUT_U;
      blue.v = DRAWDATAGEN_POSE_CLUT_V;
      blue.a = DRAWDATAGEN_POSE_CLUT_A;
      line_px_u32 = drawdatagen_clut_to_pixel(&blue, draw_format);
    }
    line_px_u16 = (uint16_t)line_px_u32;
  }

  if (!append_mode) {
    memset(clut, 0, OVERLAY_CLUT_SIZE);
    if (use_custom) {
      clut[color_idx] = line_clut;
    } else {
      clut[color_idx].y = DRAWDATAGEN_POSE_CLUT_Y;
      clut[color_idx].u = DRAWDATAGEN_POSE_CLUT_U;
      clut[color_idx].v = DRAWDATAGEN_POSE_CLUT_V;
      clut[color_idx].a = DRAWDATAGEN_POSE_CLUT_A;
    }
    drawdatagen_fill_background_for_area(clut, data_buf, roi->height, (size_t)area_pitch,
        draw_format, bg_color_hex);
  } else if (use_custom && draw_format == AMBA_DRAW_FORMAT_8BIT_CLUT) {
    clut[color_idx] = line_clut;
  } else if (!append_mode || draw_format == AMBA_DRAW_FORMAT_8BIT_CLUT) {
    if (!use_custom) {
      clut[color_idx].y = DRAWDATAGEN_POSE_CLUT_Y;
      clut[color_idx].u = DRAWDATAGEN_POSE_CLUT_U;
      clut[color_idx].v = DRAWDATAGEN_POSE_CLUT_V;
      clut[color_idx].a = DRAWDATAGEN_POSE_CLUT_A;
    }
  }

  for (i = 0; i < AMBA_ML_POSE_KEYPOINT_NUM; i++) {
    kp_sc[i] = pose->keypoints[i].score;
    kp_x[i] = (int)pose->keypoints[i].x - (int)roi->x;
    kp_y[i] = (int)pose->keypoints[i].y - (int)roi->y;
  }

  for (li = 0; li < POSE_LINE_NUM; li++) {
    int a = s_pose_line_map[li][0];
    int b = s_pose_line_map[li][1];
    if (a < 0 || a >= AMBA_ML_POSE_KEYPOINT_NUM || b < 0 || b >= AMBA_ML_POSE_KEYPOINT_NUM)
      continue;
    if (kp_sc[a] <= 0.f || kp_sc[b] <= 0.f)
      continue;

    if (draw_format == AMBA_DRAW_FORMAT_8BIT_CLUT) {
      draw_line_8bit(data_buf, area_pitch, roi->width, roi->height,
          kp_x[a], kp_y[a], kp_x[b], kp_y[b], color_idx, line_thickness);
    } else if (draw_format >= AMBA_DRAW_FORMAT_16BIT_FIRST &&
        draw_format < AMBA_DRAW_FORMAT_16BIT_LAST) {
      draw_line_16bit(data_buf, area_pitch, roi->width, roi->height,
          kp_x[a], kp_y[a], kp_x[b], kp_y[b], line_px_u16, line_thickness);
    } else if (draw_format >= AMBA_DRAW_FORMAT_32BIT_FIRST &&
        draw_format < AMBA_DRAW_FORMAT_32BIT_LAST) {
      draw_line_32bit(data_buf, area_pitch, roi->width, roi->height,
          kp_x[a], kp_y[a], kp_x[b], kp_y[b], line_px_u32, line_thickness);
    }
  }

  for (i = 0; i < AMBA_ML_POSE_KEYPOINT_NUM; i++) {
    if (kp_sc[i] <= 0.f)
      continue;
    if (draw_format == AMBA_DRAW_FORMAT_8BIT_CLUT) {
      draw_filled_disc_8bit(data_buf, area_pitch, roi->width, roi->height,
          kp_x[i], kp_y[i], kp_radius, color_idx);
    } else if (draw_format >= AMBA_DRAW_FORMAT_16BIT_FIRST &&
        draw_format < AMBA_DRAW_FORMAT_16BIT_LAST) {
      draw_filled_disc_16bit(data_buf, area_pitch, roi->width, roi->height,
          kp_x[i], kp_y[i], kp_radius, line_px_u16);
    } else if (draw_format >= AMBA_DRAW_FORMAT_32BIT_FIRST &&
        draw_format < AMBA_DRAW_FORMAT_32BIT_LAST) {
      draw_filled_disc_32bit(data_buf, area_pitch, roi->width, roi->height,
          kp_x[i], kp_y[i], kp_radius, line_px_u32);
    }
  }

  return 0;
}
