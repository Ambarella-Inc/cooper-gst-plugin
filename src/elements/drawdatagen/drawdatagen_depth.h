/*
 * drawdatagen_depth.h
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
 * Depth overlay helpers for amba_draw_data_gen.
 */

#ifndef __DRAWDATAGEN_DEPTH_H__
#define __DRAWDATAGEN_DEPTH_H__

#include <stdint.h>
#include "overlay_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Fill 256 CLUT entries: index 0 transparent, 1..255 from nn_arm depth colormap. */
void drawdatagen_depth_init_clut(amba_draw_clut_t *clut);

/** Map GRAY8 depth (0=transparent) into 8bit CLUT indices. Supports roi/src scale. */
void drawdatagen_depth_gray8_to_clut(const uint8_t *gray8_src, int src_width, int src_height,
    int src_stride, unsigned char *data_buf, size_t area_pitch, int roi_x, int roi_y,
    int roi_width, int roi_height);

/** Build pixel LUT[256] for non-8bit draw formats (index 0 = transparent). */
void drawdatagen_depth_build_pixel_lut(uint32_t *lut, int draw_format,
    const amba_draw_clut_t *clut);

/** Map GRAY8 depth into 16/32-bit pixel buffer using LUT. */
void drawdatagen_depth_gray8_to_pixels(const uint8_t *gray8_src, int src_width, int src_height,
    int src_stride, unsigned char *data_buf, size_t area_pitch, int roi_x, int roi_y,
    int roi_width, int roi_height, int draw_format, int draw_pix_size,
    const uint32_t *lut);

#ifdef __cplusplus
}
#endif

#endif /* __DRAWDATAGEN_DEPTH_H__ */
