/*
 * drawdatagen_format.c
 *
 * History:
 *    3/6/2026 - [pxduan] created file
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
 * Format helpers for drawdatagen: background fill, pixel conversion.
 */

#include <stdint.h>
#include <string.h>
#include "drawdatagen_common.h"
#include "osd_draw_types.h"
#include "overlay_common.h"

void drawdatagen_fill_background(unsigned char *data_buf, int height, size_t pitch,
    int draw_format)
{
  unsigned int y;
  if (!data_buf || height <= 0 || pitch == 0)
    return;
  if (draw_format == AMBA_DRAW_FORMAT_8BIT_CLUT) {
    memset(data_buf, AMBA_DRAW_CLUT_ENTRY_BACKGROUND, (size_t)(height * pitch));
    return;
  }
  if (draw_format >= AMBA_DRAW_FORMAT_16BIT_FIRST &&
      draw_format < AMBA_DRAW_FORMAT_16BIT_LAST) {
    /* 16bit: transparent = 0x0000 for RGB565/ARGB1555 etc */
    for (y = 0; y < (unsigned int)height; y++) {
      unsigned int x;
      uint16_t *row = (uint16_t *)(data_buf + y * pitch);
      for (x = 0; x < pitch / 2; x++) {
        row[x] = 0;
      }
    }
    return;
  }
  if (draw_format >= AMBA_DRAW_FORMAT_32BIT_FIRST &&
      draw_format < AMBA_DRAW_FORMAT_32BIT_LAST) {
    /* 32bit: transparent = 0x00000000 */
    for (y = 0; y < (unsigned int)height; y++) {
      unsigned int x;
      uint32_t *row = (uint32_t *)(data_buf + y * pitch);
      for (x = 0; x < pitch / 4; x++) {
        row[x] = 0;
      }
    }
    return;
  }
  memset(data_buf, AMBA_DRAW_CLUT_ENTRY_BACKGROUND, (size_t)(height * pitch));
}

/* YUV (BT.601) to RGB. c has y,u,v,a. */
static void yuv_to_rgb(const amba_draw_clut_t *c, int *r, int *g, int *b)
{
  int y = c->y - 16, u = c->u - 128, v = c->v - 128;
  int rr = (298 * y + 409 * v + 128) >> 8;
  int gg = (298 * y - 100 * u - 208 * v + 128) >> 8;
  int bb = (298 * y + 516 * u + 128) >> 8;
  *r = rr < 0 ? 0 : (rr > 255 ? 255 : rr);
  *g = gg < 0 ? 0 : (gg > 255 ? 255 : gg);
  *b = bb < 0 ? 0 : (bb > 255 ? 255 : bb);
}

/* Convert 16bit/32bit pixel to ARGB for video output. (a<<24)|(r<<16)|(g<<8)|b */
uint32_t drawdatagen_pixel_to_argb(uint32_t pixel, int draw_format)
{
  uint8_t r = 0, g = 0, b = 0, a = 255;
  amba_draw_clut_t c;
  int rr, gg, bb;

  if (draw_format >= AMBA_DRAW_FORMAT_16BIT_FIRST &&
      draw_format < AMBA_DRAW_FORMAT_16BIT_LAST) {
    uint16_t p = (uint16_t)(pixel & 0xffff);
    switch (draw_format) {
    case AMBA_DRAW_FORMAT_RGB565:
      r = (uint8_t)((p >> 11) & 0x1F);
      r = (uint8_t)((r << 3) | (r >> 2));
      g = (uint8_t)((p >> 5) & 0x3F);
      g = (uint8_t)((g << 2) | (g >> 4));
      b = (uint8_t)(p & 0x1F);
      b = (uint8_t)((b << 3) | (b >> 2));
      break;
    case AMBA_DRAW_FORMAT_UYV565: {
      uint8_t u5 = (uint8_t)((p >> 11) & 0x1F);
      uint8_t y6 = (uint8_t)((p >> 5) & 0x3F);
      uint8_t v5 = (uint8_t)(p & 0x1F);
      c.u = (unsigned char)((u5 << 3) | (u5 >> 2));
      c.y = (unsigned char)((y6 << 2) | (y6 >> 4));
      c.v = (unsigned char)((v5 << 3) | (v5 >> 2));
      c.a = 255;
      yuv_to_rgb(&c, &rr, &gg, &bb);
      r = (uint8_t)rr;
      g = (uint8_t)gg;
      b = (uint8_t)bb;
    } break;
    case AMBA_DRAW_FORMAT_BGR565: {
      uint8_t bh = (uint8_t)((p >> 11) & 0x1F);
      uint8_t gm = (uint8_t)((p >> 5) & 0x3F);
      uint8_t rl = (uint8_t)(p & 0x1F);
      b = (uint8_t)((bh << 3) | (bh >> 2));
      g = (uint8_t)((gm << 2) | (gm >> 4));
      r = (uint8_t)((rl << 3) | (rl >> 2));
    } break;
    case AMBA_DRAW_FORMAT_AYUV4444:
      c.a = (unsigned char)(((p >> 12) & 0xF) * 17);
      c.y = (unsigned char)(((p >> 8) & 0xF) * 17);
      c.u = (unsigned char)(((p >> 4) & 0xF) * 17);
      c.v = (unsigned char)((p & 0xF) * 17);
      yuv_to_rgb(&c, &rr, &gg, &bb);
      r = (uint8_t)rr;
      g = (uint8_t)gg;
      b = (uint8_t)bb;
      a = c.a;
      break;
    case AMBA_DRAW_FORMAT_RGBA4444:
      r = (uint8_t)(((p >> 12) & 0xF) * 17);
      g = (uint8_t)(((p >> 8) & 0xF) * 17);
      b = (uint8_t)(((p >> 4) & 0xF) * 17);
      a = (uint8_t)((p & 0xF) * 17);
      break;
    case AMBA_DRAW_FORMAT_BGRA4444:
      b = (uint8_t)(((p >> 12) & 0xF) * 17);
      g = (uint8_t)(((p >> 8) & 0xF) * 17);
      r = (uint8_t)(((p >> 4) & 0xF) * 17);
      a = (uint8_t)((p & 0xF) * 17);
      break;
    case AMBA_DRAW_FORMAT_ABGR4444:
      a = (uint8_t)(((p >> 12) & 0xF) * 17);
      b = (uint8_t)(((p >> 8) & 0xF) * 17);
      g = (uint8_t)(((p >> 4) & 0xF) * 17);
      r = (uint8_t)((p & 0xF) * 17);
      break;
    case AMBA_DRAW_FORMAT_ARGB4444:
      a = (uint8_t)(((p >> 12) & 0xF) * 17);
      r = (uint8_t)(((p >> 8) & 0xF) * 17);
      g = (uint8_t)(((p >> 4) & 0xF) * 17);
      b = (uint8_t)((p & 0xF) * 17);
      break;
    case AMBA_DRAW_FORMAT_AYUV1555:
    case AMBA_DRAW_FORMAT_YUV1555:
      c.a = (unsigned char)((p & 0x8000) ? 255 : 0);
      c.y = (unsigned char)(((p >> 10) & 0x1F) << 3);
      c.u = (unsigned char)(((p >> 5) & 0x1F) << 3);
      c.v = (unsigned char)((p & 0x1F) << 3);
      yuv_to_rgb(&c, &rr, &gg, &bb);
      r = (uint8_t)rr;
      g = (uint8_t)gg;
      b = (uint8_t)bb;
      a = c.a;
      break;
    case AMBA_DRAW_FORMAT_RGBA5551: {
      uint8_t rv = (uint8_t)((p >> 11) & 0x1F);
      uint8_t gv = (uint8_t)((p >> 6) & 0x1F);
      uint8_t bv = (uint8_t)((p >> 1) & 0x1F);
      r = (uint8_t)((rv << 3) | (rv >> 2));
      g = (uint8_t)((gv << 3) | (gv >> 2));
      b = (uint8_t)((bv << 3) | (bv >> 2));
      a = (p & 1) ? 255 : 0;
    } break;
    case AMBA_DRAW_FORMAT_BGRA5551: {
      uint8_t bv = (uint8_t)((p >> 11) & 0x1F);
      uint8_t gv = (uint8_t)((p >> 6) & 0x1F);
      uint8_t rv = (uint8_t)((p >> 1) & 0x1F);
      b = (uint8_t)((bv << 3) | (bv >> 2));
      g = (uint8_t)((gv << 3) | (gv >> 2));
      r = (uint8_t)((rv << 3) | (rv >> 2));
      a = (p & 1) ? 255 : 0;
    } break;
    case AMBA_DRAW_FORMAT_ABGR1555: {
      uint8_t b5 = (uint8_t)((p >> 10) & 0x1F);
      uint8_t g5 = (uint8_t)((p >> 5) & 0x1F);
      uint8_t r5 = (uint8_t)(p & 0x1F);
      a = (unsigned char)((p & 0x8000) ? 255 : 0);
      b = (uint8_t)((b5 << 3) | (b5 >> 2));
      g = (uint8_t)((g5 << 3) | (g5 >> 2));
      r = (uint8_t)((r5 << 3) | (r5 >> 2));
    } break;
    case AMBA_DRAW_FORMAT_ARGB1555: {
      uint8_t r5 = (uint8_t)((p >> 10) & 0x1F);
      uint8_t g5 = (uint8_t)((p >> 5) & 0x1F);
      uint8_t b5 = (uint8_t)(p & 0x1F);
      a = (unsigned char)((p & 0x8000) ? 255 : 0);
      r = (uint8_t)((r5 << 3) | (r5 >> 2));
      g = (uint8_t)((g5 << 3) | (g5 >> 2));
      b = (uint8_t)((b5 << 3) | (b5 >> 2));
    } break;
    default:
      return 0;
    }
  } else if (draw_format >= AMBA_DRAW_FORMAT_32BIT_FIRST &&
      draw_format < AMBA_DRAW_FORMAT_32BIT_LAST) {
    switch (draw_format) {
    case AMBA_DRAW_FORMAT_AYUV8888:
      c.y = (unsigned char)((pixel >> 16) & 0xFF);
      c.u = (unsigned char)((pixel >> 8) & 0xFF);
      c.v = (unsigned char)(pixel & 0xFF);
      c.a = (unsigned char)(pixel >> 24);
      yuv_to_rgb(&c, &rr, &gg, &bb);
      r = (uint8_t)rr;
      g = (uint8_t)gg;
      b = (uint8_t)bb;
      a = c.a;
      break;
    case AMBA_DRAW_FORMAT_RGBA8888:
      r = (uint8_t)((pixel >> 24) & 0xFF);
      g = (uint8_t)((pixel >> 16) & 0xFF);
      b = (uint8_t)((pixel >> 8) & 0xFF);
      a = (uint8_t)(pixel & 0xFF);
      break;
    case AMBA_DRAW_FORMAT_BGRA8888:
      b = (uint8_t)((pixel >> 24) & 0xFF);
      g = (uint8_t)((pixel >> 16) & 0xFF);
      r = (uint8_t)((pixel >> 8) & 0xFF);
      a = (uint8_t)(pixel & 0xFF);
      break;
    case AMBA_DRAW_FORMAT_ABGR8888:
      a = (uint8_t)((pixel >> 24) & 0xFF);
      b = (uint8_t)((pixel >> 16) & 0xFF);
      g = (uint8_t)((pixel >> 8) & 0xFF);
      r = (uint8_t)(pixel & 0xFF);
      break;
    case AMBA_DRAW_FORMAT_ARGB8888:
      a = (uint8_t)((pixel >> 24) & 0xFF);
      r = (uint8_t)((pixel >> 16) & 0xFF);
      g = (uint8_t)((pixel >> 8) & 0xFF);
      b = (uint8_t)(pixel & 0xFF);
      break;
    default:
      return 0;
    }
  } else {
    return 0;
  }
  return (uint32_t)(((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b);
}

/* Convert CLUT entry (YUV) to ARGB. */
uint32_t drawdatagen_clut_to_argb(const amba_draw_clut_t *c)
{
  int r, g, b;
  yuv_to_rgb(c, &r, &g, &b);
  return (uint32_t)(((uint32_t)c->a << 24) | ((uint32_t)r << 16) |
      ((uint32_t)g << 8) | (uint32_t)b);
}

/* Match amba_draw_data_string.c bmp_to_* packing: y,u,v,a are either YUV or R,G,B per format. */
int drawdatagen_osd_hex_is_rgb_packed(int draw_format)
{
  switch (draw_format) {
  case AMBA_DRAW_FORMAT_RGB565:
  case AMBA_DRAW_FORMAT_BGR565:
  case AMBA_DRAW_FORMAT_RGBA4444:
  case AMBA_DRAW_FORMAT_BGRA4444:
  case AMBA_DRAW_FORMAT_ABGR4444:
  case AMBA_DRAW_FORMAT_ARGB4444:
  case AMBA_DRAW_FORMAT_RGBA5551:
  case AMBA_DRAW_FORMAT_BGRA5551:
  case AMBA_DRAW_FORMAT_ABGR1555:
  case AMBA_DRAW_FORMAT_ARGB1555:
  case AMBA_DRAW_FORMAT_RGBA8888:
  case AMBA_DRAW_FORMAT_BGRA8888:
  case AMBA_DRAW_FORMAT_ABGR8888:
  case AMBA_DRAW_FORMAT_ARGB8888:
    return 1;
  default:
    return 0;
  }
}

/* Convert YUV+alpha to pixel value for format. */
uint32_t drawdatagen_clut_to_pixel(const amba_draw_clut_t *c, int draw_format)
{
  const amba_draw_clut_t *t = c;
  if (draw_format == AMBA_DRAW_FORMAT_8BIT_CLUT)
    return 0;

  switch (draw_format) {
  case AMBA_DRAW_FORMAT_RGB565:
    return (uint32_t)((((uint16_t)t->y >> 3) << 11) | (((uint16_t)t->u >> 2) << 5) |
        ((uint16_t)t->v >> 3));
  case AMBA_DRAW_FORMAT_UYV565:
    return (uint32_t)((((uint16_t)t->u >> 3) << 11) | (((uint16_t)t->y >> 2) << 5) |
        ((uint16_t)t->v >> 3));
  case AMBA_DRAW_FORMAT_BGR565:
    return (uint32_t)((((uint16_t)t->v >> 3) << 11) | (((uint16_t)t->u >> 2) << 5) |
        ((uint16_t)t->y >> 3));
  case AMBA_DRAW_FORMAT_AYUV4444:
    return (uint32_t)((((uint16_t)t->a >> 4) << 12) | (((uint16_t)t->y >> 4) << 8) |
        (((uint16_t)t->u >> 4) << 4) | ((uint16_t)t->v >> 4));
  case AMBA_DRAW_FORMAT_RGBA4444:
    return (uint32_t)((((uint16_t)t->y >> 4) << 12) | (((uint16_t)t->u >> 4) << 8) |
        (((uint16_t)t->v >> 4) << 4) | ((uint16_t)t->a >> 4));
  case AMBA_DRAW_FORMAT_BGRA4444:
    return (uint32_t)((((uint16_t)t->v >> 4) << 12) | (((uint16_t)t->u >> 4) << 8) |
        (((uint16_t)t->y >> 4) << 4) | ((uint16_t)t->a >> 4));
  case AMBA_DRAW_FORMAT_ABGR4444:
    return (uint32_t)((((uint16_t)t->a >> 4) << 12) | (((uint16_t)t->v >> 4) << 8) |
        (((uint16_t)t->u >> 4) << 4) | ((uint16_t)t->y >> 4));
  case AMBA_DRAW_FORMAT_ARGB4444:
    return (uint32_t)((((uint16_t)t->a >> 4) << 12) | (((uint16_t)t->y >> 4) << 8) |
        (((uint16_t)t->u >> 4) << 4) | ((uint16_t)t->v >> 4));
  case AMBA_DRAW_FORMAT_AYUV1555:
  case AMBA_DRAW_FORMAT_YUV1555:
    return (uint32_t)(((uint16_t)(t->a != 0 ? 1 : 0) << 15) | (((uint16_t)t->y >> 3) << 10) |
        (((uint16_t)t->u >> 3) << 5) | ((uint16_t)t->v >> 3));
  case AMBA_DRAW_FORMAT_RGBA5551:
    return (uint32_t)((((uint16_t)t->y >> 3) << 11) | (((uint16_t)t->u >> 3) << 6) |
        (((uint16_t)t->v >> 3) << 1) | (uint16_t)(t->a != 0 ? 1 : 0));
  case AMBA_DRAW_FORMAT_BGRA5551:
    return (uint32_t)((((uint16_t)t->v >> 3) << 11) | (((uint16_t)t->u >> 3) << 6) |
        (((uint16_t)t->y >> 3) << 1) | (uint16_t)(t->a != 0 ? 1 : 0));
  case AMBA_DRAW_FORMAT_ABGR1555:
    return (uint32_t)(((uint16_t)(t->a != 0 ? 1 : 0) << 15) | (((uint16_t)t->v >> 3) << 10) |
        (((uint16_t)t->u >> 3) << 5) | ((uint16_t)t->y >> 3));
  case AMBA_DRAW_FORMAT_ARGB1555:
    return (uint32_t)(((uint16_t)(t->a != 0 ? 1 : 0) << 15) | (((uint16_t)t->y >> 3) << 10) |
        (((uint16_t)t->u >> 3) << 5) | ((uint16_t)t->v >> 3));
  case AMBA_DRAW_FORMAT_AYUV8888:
    return (uint32_t)(((uint32_t)t->a << 24) | ((uint32_t)t->y << 16) |
        ((uint32_t)t->u << 8) | (uint32_t)t->v);
  case AMBA_DRAW_FORMAT_RGBA8888:
    return (uint32_t)(((uint32_t)t->y << 24) | ((uint32_t)t->u << 16) |
        ((uint32_t)t->v << 8) | (uint32_t)t->a);
  case AMBA_DRAW_FORMAT_BGRA8888:
    return (uint32_t)(((uint32_t)t->v << 24) | ((uint32_t)t->u << 16) |
        ((uint32_t)t->y << 8) | (uint32_t)t->a);
  case AMBA_DRAW_FORMAT_ABGR8888:
    return (uint32_t)(((uint32_t)t->a << 24) | ((uint32_t)t->v << 16) |
        ((uint32_t)t->u << 8) | (uint32_t)t->y);
  case AMBA_DRAW_FORMAT_ARGB8888:
    return (uint32_t)(((uint32_t)t->a << 24) | ((uint32_t)t->y << 16) |
        ((uint32_t)t->u << 8) | (uint32_t)t->v);
  default:
    return 0;
  }
}

uint32_t drawdatagen_yuv_clut_to_pixel(const amba_draw_clut_t *c, int draw_format)
{
  int r, g, b;
  amba_draw_clut_t rgb;

  if (!c)
    return 0;
  if (!drawdatagen_osd_hex_is_rgb_packed(draw_format))
    return drawdatagen_clut_to_pixel(c, draw_format);
  yuv_to_rgb(c, &r, &g, &b);
  rgb.y = (unsigned char)r;
  rgb.u = (unsigned char)g;
  rgb.v = (unsigned char)b;
  rgb.a = c->a;
  return drawdatagen_clut_to_pixel(&rgb, draw_format);
}

void drawdatagen_fill_background_for_area(amba_draw_clut_t *clut, unsigned char *data_buf,
    int height, size_t pitch, int draw_format, uint32_t bg_color_hex)
{
  amba_draw_clut_t bg;
  int custom = (bg_color_hex != 0);
  unsigned int y;

  if (!clut || !data_buf || height <= 0 || pitch == 0)
    return;
  if (custom) {
    if (drawdatagen_osd_hex_is_rgb_packed(draw_format)) {
      bg.v = (unsigned char)((bg_color_hex >> 24) & 0xff);
      bg.u = (unsigned char)((bg_color_hex >> 16) & 0xff);
      bg.y = (unsigned char)((bg_color_hex >> 8) & 0xff);
      bg.a = (unsigned char)(bg_color_hex & 0xff);
    } else {
      bg.v = (unsigned char)((bg_color_hex >> 24) & 0xff);
      bg.u = (unsigned char)((bg_color_hex >> 16) & 0xff);
      bg.y = (unsigned char)((bg_color_hex >> 8) & 0xff);
      bg.a = (unsigned char)(bg_color_hex & 0xff);
    }
  } else {
    bg.v = 128;
    bg.u = 128;
    bg.y = 235;
    bg.a = 0;
  }
  clut[AMBA_DRAW_CLUT_ENTRY_BACKGROUND] = bg;

  if (draw_format == AMBA_DRAW_FORMAT_8BIT_CLUT) {
    memset(data_buf, AMBA_DRAW_CLUT_ENTRY_BACKGROUND, (size_t)(height * pitch));
    return;
  }
  if (!custom) {
    drawdatagen_fill_background(data_buf, height, pitch, draw_format);
    return;
  }
  {
    uint32_t px = drawdatagen_clut_to_pixel(&bg, draw_format);
    if (draw_format >= AMBA_DRAW_FORMAT_16BIT_FIRST &&
        draw_format < AMBA_DRAW_FORMAT_16BIT_LAST) {
      for (y = 0; y < (unsigned int)height; y++) {
        unsigned int x;
        uint16_t *row = (uint16_t *)(data_buf + y * pitch);
        for (x = 0; x < pitch / 2; x++) {
          row[x] = (uint16_t)px;
        }
      }
      return;
    }
    if (draw_format >= AMBA_DRAW_FORMAT_32BIT_FIRST &&
        draw_format < AMBA_DRAW_FORMAT_32BIT_LAST) {
      for (y = 0; y < (unsigned int)height; y++) {
        unsigned int x;
        uint32_t *row = (uint32_t *)(data_buf + y * pitch);
        for (x = 0; x < pitch / 4; x++) {
          row[x] = px;
        }
      }
    }
  }
}
