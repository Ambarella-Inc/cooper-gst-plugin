/*
 * drawdatagen_time.h
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
 * Time overlay for amba_draw_data_gen.
 */

#ifndef __DRAWDATAGEN_TIME_H__
#define __DRAWDATAGEN_TIME_H__

#include "overlay_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * drawdatagen_time_draw - Draw timestamp to overlay area
 * @pre_str: prefix string
 * @suf_str: suffix string
 * @en_msec: enable millisecond
 * @format: date format 0-3
 * @is_12h: 12h/24h
 * @font_file: TTF font path
 * @font_size: font size
 * @roi: area rect
 * @area_time: persistent amba_overlay_area_param_t
 * @clut: CLUT buffer
 * @data_buf: pixel buffer
 * @bitmap: bitmap_buffer_t
 * @draw_format: draw format
 * @stream_rotate: stream rotate
 * Returns: 0 on success, <0 on error
 */
int drawdatagen_time_draw(const char *pre_str,
    const char *suf_str,
    int en_msec,
    int format,
    int is_12h,
    const char *font_file,
    int font_size,
    const amba_rect_t *roi,
    amba_overlay_area_param_t *area_time,
    amba_draw_clut_t *clut,
    unsigned char *data_buf,
    bitmap_buffer_t *bitmap,
    int draw_format,
    unsigned char stream_rotate);

#ifdef __cplusplus
}
#endif

#endif /* __DRAWDATAGEN_TIME_H__ */
