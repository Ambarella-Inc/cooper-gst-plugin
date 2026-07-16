/*
 * drawdatagen_bmp.h
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
 * BMP picture overlay for amba_draw_data_gen.
 */

#ifndef __DRAWDATAGEN_BMP_H__
#define __DRAWDATAGEN_BMP_H__

#include "overlay_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * drawdatagen_bmp_draw - Draw BMP picture to overlay area
 * @path: BMP file path
 * @roi: area rect
 * @clut: CLUT buffer
 * @data_buf: pixel buffer
 * @bitmap: bitmap_buffer_t for amba_draw_pic_data
 * @draw_format: AMBA_DRAW_FORMAT_8BIT_CLUT etc
 * @area_pitch: pitch of data_buf (bytes per row)
 * Returns: 0 on success, <0 on error
 */
int drawdatagen_bmp_draw(const char *path,
    const amba_rect_t *roi,
    amba_draw_clut_t *clut,
    unsigned char *data_buf,
    bitmap_buffer_t *bitmap,
    int draw_format,
    unsigned int area_pitch);

/** Draw BMP from memory buffer (no temp file). */
int drawdatagen_bmp_draw_from_buffer(const unsigned char *bmp_data, size_t bmp_size,
    const amba_rect_t *roi,
    amba_draw_clut_t *clut,
    unsigned char *data_buf,
    bitmap_buffer_t *bitmap,
    int draw_format,
    unsigned int area_pitch);

#ifdef __cplusplus
}
#endif

#endif /* __DRAWDATAGEN_BMP_H__ */
