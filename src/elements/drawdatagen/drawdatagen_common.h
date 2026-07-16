/*
 * drawdatagen_common.h
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
 * Common helpers for drawdatagen format modules.
 */

#ifndef __DRAWDATAGEN_COMMON_H__
#define __DRAWDATAGEN_COMMON_H__

#include <stdint.h>
#include <string.h>
#include "iav_al.h"
#include "platform_al.h"
#include "overlay_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DRAWDATAGEN_DEFAULT_FONT_PATH "/usr/share/fonts/DroidSans.ttf"

/* Portable strlcpy replacement (g_strlcpy requires GLib 2.68+) */
static inline void drawdatagen_strlcpy(char *dst, const char *src, size_t size)
{
  if (size > 0) {
    size_t len = strlen(src);
    if (len >= size)
      len = size - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
  }
}

/**
 * drawdatagen_area_pitch - Compute area pitch for overlay buffer
 * @width: area width
 * @pix_size: bytes per pixel (1 for 8bit CLUT)
 */
static inline int drawdatagen_area_pitch(int width, int pix_size)
{
  return ROUND_UP(ROUND_UP(width, OSD_BUF_WIDTH_ALIGN) * pix_size, OSD_BUF_PITCH_ALIGN);
}

void drawdatagen_fill_background(unsigned char *data_buf, int height, size_t pitch,
    int draw_format);

/* Set CLUT[BACKGROUND] from hex (0 = default transparent y235,u128,v128,a0) and fill pixels.
 * YUV-family formats: hex MSB..LSB = v/u/y/a. RGB-packed formats: b/g/r/a (r,g,b stored in clut y,u,v). */
void drawdatagen_fill_background_for_area(amba_draw_clut_t *clut, unsigned char *data_buf,
    int height, size_t pitch, int draw_format, uint32_t bg_color_hex);

uint32_t drawdatagen_clut_to_pixel(const amba_draw_clut_t *c, int draw_format);

/* Like drawdatagen_clut_to_pixel but @c is Y,U,V,A (BT.601); RGB-packed formats convert YUV->RGB first. */
uint32_t drawdatagen_yuv_clut_to_pixel(const amba_draw_clut_t *c, int draw_format);

/* True if OSD color hex is b/g/r/a (MSB..LSB); false if v/u/y/a (YUV family). */
int drawdatagen_osd_hex_is_rgb_packed(int draw_format);

uint32_t drawdatagen_pixel_to_argb(uint32_t pixel, int draw_format);

uint32_t drawdatagen_clut_to_argb(const amba_draw_clut_t *c);

#ifdef __cplusplus
}
#endif

#endif /* __DRAWDATAGEN_COMMON_H__ */
