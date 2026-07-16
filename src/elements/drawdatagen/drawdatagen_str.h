/*
 * drawdatagen_str.h
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
 * String overlay for amba_draw_data_gen.
 */

#ifndef __DRAWDATAGEN_STR_H__
#define __DRAWDATAGEN_STR_H__

#include "overlay_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * drawdatagen_str_draw - Draw string to overlay area
 * @str: text string
 * @font_file: TTF font path
 * @font_size: font width/height
 * @roi: area rect
 * @area_str: persistent amba_overlay_area_param_t (font lib state)
 * @clut: CLUT buffer
 * @data_buf: pixel buffer
 * @bitmap: bitmap_buffer_t
 * @draw_format: draw format
 * @stream_rotate: stream rotate mode
 * Returns: 0 on success, <0 on error
 */
int drawdatagen_str_draw(const char *str,
    const char *font_file,
    int font_size,
    const amba_rect_t *roi,
    amba_overlay_area_param_t *area_str,
    amba_draw_clut_t *clut,
    unsigned char *data_buf,
    bitmap_buffer_t *bitmap,
    int draw_format,
    unsigned char stream_rotate);

/**
 * drawdatagen_text_deinit - Deinit text font lib for string/time areas
 * Call in element finalize.
 */
void drawdatagen_text_deinit(amba_overlay_area_param_t *area_str,
    amba_overlay_area_param_t *area_time);

#ifdef __cplusplus
}
#endif

#endif /* __DRAWDATAGEN_STR_H__ */
