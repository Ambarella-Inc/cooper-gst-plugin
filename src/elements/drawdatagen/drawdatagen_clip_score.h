/*
 * drawdatagen_clip_score.h
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
 * CLIP match-score overlay helpers for amba_draw_data_gen.
 */

#ifndef __DRAWDATAGEN_CLIP_SCORE_H__
#define __DRAWDATAGEN_CLIP_SCORE_H__

#include "amba_ml_decoded_result.h"
#include "amba_draw_data_string.h"
#include "drawdatagen_common.h"

#ifdef __cplusplus
extern "C" {
#endif

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
    int append_mode);

#ifdef __cplusplus
}
#endif

#endif /* __DRAWDATAGEN_CLIP_SCORE_H__ */
