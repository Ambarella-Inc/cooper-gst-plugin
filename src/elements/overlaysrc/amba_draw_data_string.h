/*
 * amba_draw_data_string.h
 *
 * History:
 *    4/16/2024 - [Peng-Xue Duan] created file
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
 */

#ifndef _AMBA_DRAW_DATA_STRING_H_
#define _AMBA_DRAW_DATA_STRING_H_

#include "overlay_common.h"

#define DEFAULT_PIXEL_BACKGROUND (0)
#define DEFAULT_PIXEL_OUTLINE (1)
#define DEFAULT_PIXEL_FONT (0xff)

typedef struct {
  int valid;
  amba_draw_font_t font;
  amba_resolution_t max_size;
  amba_draw_char_cache_dram_t char_caches[AMBA_DRAW_STRING_MAX_NUM];
} amba_font_cache_t;

enum {
  AMBA_DEFAULT_DRAW_FONT_SIZE         = 24,
  AMBA_DEFAULT_DRAW_OUTLINE_Y         = 12,
  AMBA_DEFAULT_DRAW_OUTLINE_U         = 128,
  AMBA_DEFAULT_DRAW_OUTLINE_V         = 128,
  AMBA_DEFAULT_DRAW_OUTLINE_R         = 0,
  AMBA_DEFAULT_DRAW_OUTLINE_G         = 0,
  AMBA_DEFAULT_DRAW_OUTLINE_B         = 0,
  AMBA_DEFAULT_DRAW_OUTLINE_ALPHA     = 0xff,
  AMBA_DEFAULT_DRAW_BACKGROUND_Y      = 235,
  AMBA_DEFAULT_DRAW_BACKGROUND_U      = 128,
  AMBA_DEFAULT_DRAW_BACKGROUND_V      = 128,
  AMBA_DEFAULT_DRAW_BACKGROUND_R      = 255,
  AMBA_DEFAULT_DRAW_BACKGROUND_G      = 255,
  AMBA_DEFAULT_DRAW_BACKGROUND_B      = 255,
  AMBA_DRAW_FULL_TRANSPARENT          = 0x0,
  AMBA_DEFAULT_DRAW_BACKGROUND_ALPHA  = AMBA_DRAW_FULL_TRANSPARENT,
  AMBA_TEXT_CLUT_ENTRY_BACKGOURND        = 0,
  AMBA_TEXT_CLUT_ENTRY_OUTLINE           = 1,
  AMBA_TEXT_CLUT_ENTRY_FONT              = AMBA_DRAW_CLUT_ENTRY_BACKGROUND - 1,
};

int init_textinsert_lib(amba_draw_font_t *font, amba_text_bitmap_t *text_bitmap);
int update_textinsert_lib(amba_draw_font_t *font, amba_text_bitmap_t *text_bitmap);

int deinit_textinsert_lib(amba_text_bitmap_t *text_bitmap);

int amba_draw_str_data(amba_overlay_area_param_t *area_param,
    amba_draw_clut_t *cluts,
    unsigned char *area_buf,
    bitmap_buffer_t *m_bitmap,
    int draw_format,
    unsigned char stream_rotate);

#endif /* _AM_DRAWING_DATA_STRING_H_ */
