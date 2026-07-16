/*
 * amba_draw_data_picture.h
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

#ifndef _AMBA_DRAW_DATA_PICTURE_H_
#define _AMBA_DRAW_DATA_PICTURE_H_

#include "overlay_common.h"

typedef struct {
  unsigned char b;
  unsigned char g;
  unsigned char r;
  unsigned char reserved;
} amba_draw_rgb_t;

typedef struct {
  unsigned char  bfType[2];     // file type
  unsigned char  bfSize[4];     //file size
  unsigned char  bfReserved1[2];
  unsigned char  bfReserved2[2];
  unsigned char  bfOffBits[4];
} amba_draw_bmp_file_header_t;

typedef struct {
  unsigned int biSize;
  unsigned int biWidth;     //bmp width
  unsigned int biHeight;    //bmp height
  unsigned short biPlanes;
  unsigned short biBitCount;  // 1,4,8,16,24 ,32 color attribute
  unsigned int biCompression;
  unsigned int biSizeImage;   //Image size
  unsigned int biXPelsPerMerer;
  unsigned int biYPelsPerMerer;
  unsigned int biClrUsed;
  unsigned int biClrImportant;
} amba_draw_bmp_info_header_t;

enum {
    AMBA_DRAW_BMP_MAGIC        = 0x4D42,
    AMBA_DRAW_NONE_TRANSPARENT = 0xff,
};

int amba_draw_pic_data(amba_overlay_area_param_t *area_param,
    amba_draw_clut_t *cluts,
    unsigned char *area_buf,
    bitmap_buffer_t *m_bitmap,
    int draw_format);

/** Draw BMP from memory buffer (avoids temp file). */
int amba_draw_pic_data_from_buffer(const unsigned char *bmp_data, size_t bmp_size,
    amba_overlay_area_param_t *area_param,
    amba_draw_clut_t *cluts,
    unsigned char *area_buf,
    bitmap_buffer_t *m_bitmap,
    int draw_format);

#endif /* _AM_DRAWING_DATA_PICTURE_H_ */
