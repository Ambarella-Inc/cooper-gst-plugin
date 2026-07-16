/*******************************************************************************
 * am_drawing_data_picture.cpp
 *
 * History:
 *   3/28/2018 - [Huaiqing Wang] created file
 *   5/12/2024 - [Peng-Xue Duan] created file
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

#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "common_err_code_c.h"
#include "internal.h"
#include "debug_log.h"

#include "amba_draw_data_picture.h"

/* BMP DIB rows are padded to 32-bit boundaries (MSDN). */
static unsigned int bmp_row_bytes(unsigned int width, unsigned int bit_count)
{
  return ((width * bit_count + 31u) / 32u) * 4u;
}

/* Forward declarations for static functions used by amba_draw_pic_data_from_buffer */
static void rgb_to_yuv(const amba_draw_rgb_t *src, amba_draw_clut_t *dst, unsigned char m_alpha);
static void transparency_process(const amba_draw_color_key_t *ck, int width, int height,
    unsigned char *buf, amba_draw_clut_t *cluts, int draw_fmt);
static int picture_zoom(unsigned char *src_buf, int src_width, int src_height,
    int src_stride_bytes,
    unsigned char *dst_buf, amba_rect_t *rect, int draw_fmt);

static int init_bitmap_info(FILE *fp,
   int *cn, int *w, int *h, int *pitch, int draw_fmt)
{
  int result = 0;
  do {
    if (!fp) {
      DPRINT_ERROR("Invalid fp pointer!\n");
      result = -1;
      break;
    }
    amba_draw_bmp_file_header_t fileHeader;
    amba_draw_bmp_info_header_t infoHeader;
    unsigned int mask[3];// = {0x0000f800, 0x000007e0, 0x0000001f};
    //unsigned int argb1555_mask[3];// = {0x00007c00, 0x000003e0, 0x0000001f};
    unsigned int byte_per_pixel = 0;

    if (fread(&fileHeader.bfType, sizeof(fileHeader.bfType), 1, fp) != 1) {
      perror("FREAD");
      result = -1;
      break;
    }
    if (fread(&fileHeader.bfSize, sizeof(fileHeader.bfSize), 1, fp) != 1) {
      perror("FREAD");
      result = -1;
      break;
    }
    if (fread(&fileHeader.bfReserved1, sizeof(fileHeader.bfReserved1), 1, fp) != 1) {
      perror("FREAD");
      result = -1;
      break;
    }
    if (fread(&fileHeader.bfReserved2, sizeof(fileHeader.bfReserved2), 1, fp) != 1) {
      perror("FREAD");
      result = -1;
      break;
    }
    if (fread(&fileHeader.bfOffBits, sizeof(fileHeader.bfOffBits), 1, fp) != 1) {
      perror("FREAD");
      result = -1;
      break;
    }

    unsigned int type = fileHeader.bfType[1] << 8 | fileHeader.bfType[0];
    if (type != AMBA_DRAW_BMP_MAGIC) {
      result = -1;
      DPRINT_ERROR("Invalid type [%d]. Not a bitmap.", type);
      break;
    }

    if (fread(&infoHeader, sizeof(amba_draw_bmp_info_header_t), 1, fp) != 1) {
      perror("FREAD");
      result = -1;
      break;
    }
    unsigned int size_reso = infoHeader.biWidth * infoHeader.biHeight;
    byte_per_pixel = infoHeader.biBitCount / 8;
    size_reso *= byte_per_pixel;

    if (infoHeader.biSizeImage && (infoHeader.biSizeImage != size_reso)) {
      // biSizeImage can be set to 0 for uncompressed RGB bitmaps.
      DPRINT_WARNING("Invalid image size [%u]. Not equal to width[%u] x height[%u].\n",
          infoHeader.biSizeImage,
          infoHeader.biWidth,
          infoHeader.biHeight);
    }
    if (((infoHeader.biWidth * byte_per_pixel) & (OSD_BUF_PITCH_ALIGN - 1))
        || (infoHeader.biHeight & (OVERLAY_HEIGHT_ALIGN - 1))) {
      DPRINT_ERROR("the image size %dx%d, width must be multiple of %d,"
          " height must be multiple of 4.\n", infoHeader.biWidth, infoHeader.biHeight, OSD_BUF_PITCH_ALIGN / byte_per_pixel);
      result = -1;
      break;
    }

    unsigned int offbit = fileHeader.bfOffBits[3] << 24
        | fileHeader.bfOffBits[2] << 16 | fileHeader.bfOffBits[1] << 8
        | fileHeader.bfOffBits[0];
    if (draw_fmt == AMBA_DRAW_FORMAT_8BIT_CLUT) {
      if (infoHeader.biBitCount != 8) {
        DPRINT_ERROR("Invalid [%u]bit. It should be 8bit bitmap picture.\n",
            infoHeader.biBitCount);
        result = -1;
        break;
      }

      *cn = (offbit - sizeof(amba_draw_bmp_file_header_t)
          - sizeof(amba_draw_bmp_info_header_t)) / sizeof(amba_draw_rgb_t);
      if (*cn > (int) OVERLAY_CLUT_MAX_NUM) {
        DPRINT_WARNING("OffsetBits [%u], color number = %d (>[%lu]). Reset to max.\n",
            offbit, *cn,
            OVERLAY_CLUT_MAX_NUM);
        *cn = OVERLAY_CLUT_MAX_NUM;
      }
      //DPRINT_DEBUG("Color number = %d!\n", *cn);
    } else if (draw_fmt >= AMBA_DRAW_FORMAT_16BIT_FIRST
        && draw_fmt < AMBA_DRAW_FORMAT_16BIT_LAST) {
      if (infoHeader.biBitCount != 16) {
        DPRINT_ERROR("Invalid [%u]bit. It should be 16bit picture.\n",
            infoHeader.biBitCount);
        result = -1;
        break;
      }

      if (fread(&mask, sizeof(mask), 1, fp) != 1) {
        perror("FREAD");
        result = -1;
        break;
      }

      if (draw_fmt == AMBA_DRAW_FORMAT_RGB565) {
        if (mask[0] != 0x0000f800 ||
            mask[1] != 0x000007e0 ||
            mask[2] != 0x0000001f) {
          DPRINT_ERROR("The picture is not RGB565 format!\n");
          result = -1;
          break;
        }
      } else if (draw_fmt == AMBA_DRAW_FORMAT_ARGB1555) {
        if (mask[0] != 0x00007c00 ||
            mask[1] != 0x000003e0 ||
            mask[2] != 0x0000001f) {
          DPRINT_ERROR("The picture is not ARGB1555 format!\n");
          result = -1;
          break;
        }
      }
      if (fseek(fp, offbit, SEEK_SET) != 0) {
        perror("FSEEK");
        result = -1;
        break;
      }
    } else if (draw_fmt >= AMBA_DRAW_FORMAT_32BIT_FIRST
        && draw_fmt < AMBA_DRAW_FORMAT_32BIT_LAST) {
      if (infoHeader.biBitCount != 32) {
        DPRINT_ERROR("Invalid [%u]bit. It should be 32bit picture.\n",
            infoHeader.biBitCount);
        result = -1;
        break;
      }
      if (fseek(fp, offbit, SEEK_SET) != 0) {
        perror("FSEEK");
        result = -1;
        break;
      }
    } else {
      DPRINT_ERROR("Invalid drawing format:%d\n",draw_fmt);
      result = -1;
      break;
    }
    *w = (int)infoHeader.biWidth;
    *h = (int)infoHeader.biHeight;
    *pitch = (int)bmp_row_bytes(infoHeader.biWidth, infoHeader.biBitCount);
  } while (0);

  return result;
}

/* Parse BMP header from memory buffer. Returns clut_offset and pixel_offset for 8bit. */
static int init_bitmap_info_from_mem(const unsigned char *data, size_t size,
    int *cn, int *w, int *h, int *pitch, int draw_fmt,
    unsigned int *clut_offset, unsigned int *pixel_offset)
{
  int result = 0;
  size_t min_size = sizeof(amba_draw_bmp_file_header_t) + sizeof(amba_draw_bmp_info_header_t);
  if (!data || size < min_size || !cn || !w || !h || !pitch || !clut_offset || !pixel_offset) {
    DPRINT_ERROR("Invalid params for init_bitmap_info_from_mem\n");
    return -1;
  }
  const amba_draw_bmp_file_header_t *fileHeader = (const amba_draw_bmp_file_header_t *)data;
  const amba_draw_bmp_info_header_t *infoHeader =
      (const amba_draw_bmp_info_header_t *)(data + sizeof(amba_draw_bmp_file_header_t));

  unsigned int type = fileHeader->bfType[1] << 8 | fileHeader->bfType[0];
  if (type != AMBA_DRAW_BMP_MAGIC) {
    DPRINT_ERROR("Invalid type [%u]. Not a bitmap.\n", type);
    return -1;
  }

  unsigned int offbit = fileHeader->bfOffBits[3] << 24 | fileHeader->bfOffBits[2] << 16
      | fileHeader->bfOffBits[1] << 8 | fileHeader->bfOffBits[0];
  if (offbit > size) {
    DPRINT_ERROR("bfOffBits %u exceeds buffer size %zu\n", offbit, size);
    return -1;
  }

  unsigned int byte_per_pixel = infoHeader->biBitCount / 8;
  if (((infoHeader->biWidth * byte_per_pixel) & (OSD_BUF_PITCH_ALIGN - 1))
      || (infoHeader->biHeight & (OVERLAY_HEIGHT_ALIGN - 1))) {
    DPRINT_ERROR("Image size %ux%u: width must align to %d, height to 4.\n",
        infoHeader->biWidth, infoHeader->biHeight, OSD_BUF_PITCH_ALIGN / byte_per_pixel);
    return -1;
  }

  if (draw_fmt == AMBA_DRAW_FORMAT_8BIT_CLUT) {
    if (infoHeader->biBitCount != 8) {
      DPRINT_ERROR("Invalid [%u]bit. Should be 8bit.\n", infoHeader->biBitCount);
      return -1;
    }
    *clut_offset = sizeof(amba_draw_bmp_file_header_t) + infoHeader->biSize;
    *pixel_offset = offbit;
    *cn = (offbit - (unsigned int)*clut_offset) / (int)sizeof(amba_draw_rgb_t);
    if (*cn > (int)OVERLAY_CLUT_MAX_NUM) {
      *cn = OVERLAY_CLUT_MAX_NUM;
    }
  } else if (draw_fmt >= AMBA_DRAW_FORMAT_16BIT_FIRST && draw_fmt < AMBA_DRAW_FORMAT_16BIT_LAST) {
    if (infoHeader->biBitCount != 16) {
      DPRINT_ERROR("Invalid [%u]bit. Should be 16bit.\n", infoHeader->biBitCount);
      return -1;
    }
    *clut_offset = 0;
    *pixel_offset = offbit;
    *cn = 0;
  } else if (draw_fmt >= AMBA_DRAW_FORMAT_32BIT_FIRST && draw_fmt < AMBA_DRAW_FORMAT_32BIT_LAST) {
    if (infoHeader->biBitCount != 32) {
      DPRINT_ERROR("Invalid [%u]bit. Should be 32bit.\n", infoHeader->biBitCount);
      return -1;
    }
    *clut_offset = 0;
    *pixel_offset = offbit;
    *cn = 0;
  } else {
    DPRINT_ERROR("Invalid draw format %d\n", draw_fmt);
    return -1;
  }

  *w = (int)infoHeader->biWidth;
  *h = (int)infoHeader->biHeight;
  *pitch = (int)bmp_row_bytes(infoHeader->biWidth, infoHeader->biBitCount);
  return result;
}

static int make_bmp_clut_from_mem(const unsigned char *data, size_t data_size,
    unsigned int clut_offset, unsigned int n, unsigned int bmp_size,
    unsigned char *buf, amba_draw_clut_t *cluts, unsigned char m_alpha)
{
  if (!data || !buf || !cluts) {
    DPRINT_ERROR("Invalid pointer for make_bmp_clut_from_mem\n");
    return -1;
  }
  if (n > OVERLAY_CLUT_MAX_NUM) {
    DPRINT_WARNING("Bitmap format not 8-bits, n=%u\n", n);
    return -1;
  }
  size_t clut_size = n * sizeof(amba_draw_rgb_t);
  if (clut_offset + clut_size > data_size) {
    DPRINT_ERROR("CLUT offset %u + size %zu exceeds data %zu\n", clut_offset, clut_size, data_size);
    return -1;
  }
  const amba_draw_rgb_t *palette = (const amba_draw_rgb_t *)(data + clut_offset);
  amba_draw_rgb_t rgb = {0};
  amba_draw_clut_t clut = {0};
  unsigned char is_flag = 1;
  unsigned int count = 0;
  unsigned char color_flag[OVERLAY_CLUT_MAX_NUM] = {0};
  unsigned char new_color_index = 0;

  for (unsigned int d = 0; d < bmp_size && is_flag; ++d) {
    if (buf[d] > n) continue;
    if (!color_flag[buf[d]]) {
      color_flag[buf[d]] = 1;
      ++count;
      if (count == n) is_flag = 0;
    }
  }
  for (unsigned int i = 0; i < n; ++i) {
    rgb = palette[i];
    if (!color_flag[i]) continue;
    rgb_to_yuv(&rgb, &clut, m_alpha);
    get_clut_color_index(&clut, cluts, &new_color_index);
    if (i == new_color_index) continue;
    for (unsigned int d = 0; d < bmp_size; ++d) {
      if (buf[d] == i) buf[d] = new_color_index;
    }
  }
  return 0;
}

int amba_draw_pic_data_from_buffer(const unsigned char *bmp_data, size_t bmp_size,
    amba_overlay_area_param_t *area_param,
    amba_draw_clut_t *cluts,
    unsigned char *area_buf,
    bitmap_buffer_t *m_bitmap,
    int draw_fmt)
{
  int result = 0;
  amba_draw_params_t *params = &area_param->data;
  int n = 0;
  const amba_draw_picture_t *pic = &params->pic;
  int bw = 0, bh = 0, bp = 0;
  unsigned int clut_offset = 0, pixel_offset = 0;
  unsigned char m_alpha = 255;
  unsigned char *m_buffer = NULL;

  if (!bmp_data || bmp_size == 0 || !cluts || !area_buf || !m_bitmap) {
    DPRINT_ERROR("invalid params for amba_draw_pic_data_from_buffer\n");
    return -1;
  }
  amba_rect_t *rect = &params->rect;
  if (rect->x < 0) rect->x = 0;
  if (rect->y < 0) rect->y = 0;

  if ((result = init_bitmap_info_from_mem(bmp_data, bmp_size, &n, &bw, &bh, &bp, draw_fmt,
          &clut_offset, &pixel_offset)) != 0) {
    DPRINT_ERROR("init_bitmap_info_from_mem failed\n");
    return result;
  }
  if (bw <= 0 || bh <= 0) {
    DPRINT_ERROR("Invalid picture size %dx%d\n", bw, bh);
    return -1;
  }

  if (rect->width <= 0) rect->width = bw;
  if (rect->height <= 0) rect->height = bh;
  if (rect->pitch <= 0) rect->pitch = bp;

  if (rect->x + rect->width > area_param->attr.rect.width) {
    DPRINT_ERROR("picture xstart %d + width %d out of area width %d\n",
        rect->x, rect->width, area_param->attr.rect.width);
    return -1;
  }
  if (rect->y + rect->height > area_param->attr.rect.height) {
    DPRINT_ERROR("picture ystart %d + height %d out of area height %d\n",
        rect->y, rect->height, area_param->attr.rect.height);
    return -1;
  }

  if (params->pic.use_bmp_alpha) {
    m_alpha = 0;
  } else if (params->pic.alpha) {
    m_alpha = params->pic.alpha;
  }

  unsigned int offset = rect->pitch * rect->y;
  unsigned int pix_size;
  if (draw_fmt == AMBA_DRAW_FORMAT_8BIT_CLUT) {
    pix_size = (unsigned int)bw * (unsigned int)bh;
    offset += rect->x;
  } else if (draw_fmt >= AMBA_DRAW_FORMAT_16BIT_FIRST && draw_fmt < AMBA_DRAW_FORMAT_16BIT_LAST) {
    /* DIB row stride may include padding; bp is bmp_row_bytes from header */
    pix_size = (unsigned int)bp * (unsigned int)bh;
    offset += 2 * rect->x;
  } else if (draw_fmt >= AMBA_DRAW_FORMAT_32BIT_FIRST && draw_fmt < AMBA_DRAW_FORMAT_32BIT_LAST) {
    pix_size = (unsigned int)bp * (unsigned int)bh;
    offset += 4 * rect->x;
  } else {
    DPRINT_ERROR("invalid draw format %d\n", draw_fmt);
    return -1;
  }
  m_buffer = area_buf + offset;

  if (pixel_offset + pix_size > bmp_size) {
    DPRINT_ERROR("BMP pixel data %u + %u exceeds buffer %zu\n", pixel_offset, pix_size, bmp_size);
    return -1;
  }

  if (pix_size > m_bitmap->size) {
    m_bitmap->size = pix_size;
    if (m_bitmap->buf) {
      free(m_bitmap->buf);
      m_bitmap->buf = NULL;
    }
  }
  if (!m_bitmap->buf) {
    m_bitmap->buf = (unsigned char *)malloc(m_bitmap->size);
    if (!m_bitmap->buf) {
      DPRINT_ERROR("Failed to malloc bitmap buffer\n");
      return -1;
    }
  }
  if (draw_fmt == AMBA_DRAW_FORMAT_8BIT_CLUT) {
    memset(m_bitmap->buf, AMBA_DRAW_CLUT_ENTRY_BACKGROUND, m_bitmap->size);
  } else {
    /* 16/32bit: 0xFF per byte is not "transparent"; it becomes 0xFFFF etc. (wrong solid white). */
    memset(m_bitmap->buf, 0, m_bitmap->size);
  }
  memcpy(m_bitmap->buf, bmp_data + pixel_offset, pix_size);

  if (draw_fmt == AMBA_DRAW_FORMAT_8BIT_CLUT) {
    if ((result = make_bmp_clut_from_mem(bmp_data, bmp_size, clut_offset, n, bw * bh,
            m_bitmap->buf, cluts, m_alpha)) != 0) {
      return result;
    }
  }
  transparency_process(&pic->colorkey, bw, bh, m_bitmap->buf, cluts, draw_fmt);
  if ((result = picture_zoom(m_bitmap->buf, bw, bh, bp, m_buffer, rect, draw_fmt)) != 0) {
    return result;
  }
  return 0;
}

/* because two float values do subtraction may occur
 * indivisible problem, so we will do a round for it*/
static unsigned int get_proper_value(float value)
{
  unsigned int tmp = 0;
  if (value - (int) value > 0.5f
      || (value < 0 && value - (int) value > -0.5f)) {
    tmp = (unsigned int) value + 1;
  } else {
    tmp = (unsigned int) value;
  }

  return tmp;
}

static void rgb_to_yuv(const amba_draw_rgb_t *src, amba_draw_clut_t *dst,
    unsigned char m_alpha)
{
  float tmp;
  dst->y = (unsigned char) (0.257f * src->r + 0.504f * src->g + 0.098f * src->b + 16);

  tmp = 0.439f * src->b - 0.291f * src->g - 0.148f * src->r + 128;
  dst->u = (unsigned char) get_proper_value(tmp);

  tmp = 0.439f * src->r - 0.368f * src->g - 0.071f * src->b + 128;
  dst->v = (unsigned char) get_proper_value(tmp);
  dst->a = m_alpha > 0 ? m_alpha : src->reserved;
}

static int make_bmp_clut(FILE *fp,
    unsigned int n, unsigned int size, unsigned char *buf, amba_draw_clut_t *cluts,
    unsigned char m_alpha)
{
  int result = 0;
  do {
    if (!fp || !buf || !cluts) {
      DPRINT_ERROR("Invalid pointer!\n");
      result = -1;
      break;
    }
    if (n > OVERLAY_CLUT_MAX_NUM) {
      DPRINT_WARNING("The Bitmap format is not 8-bits!\n");
      result = -1;
      break;
    }
    amba_draw_rgb_t rgb = {0};
    amba_draw_clut_t clut = {0};
    unsigned char is_flag = 1;
    unsigned int count = 0;
    unsigned char color_flag[OVERLAY_CLUT_MAX_NUM] = {0};
    unsigned char new_color_index = 0;
    for (unsigned int d = 0; d < size && is_flag; ++d) {
      if(buf[d] > n){
        continue;
      }

      if (!color_flag[buf[d]]) {
        color_flag[buf[d]] = 1;
        ++count;
        if (count == n) {
          is_flag = 0;
        }
      }
    }
    for (unsigned int i = 0; i < n; ++i) {
      if (fread(&rgb, sizeof(amba_draw_rgb_t), 1, fp) != 1) {
        perror("FREAD");
        result = -1;
        break;
      }
      if (!color_flag[i]) {
        continue;
      }

      rgb_to_yuv(&rgb, &clut, m_alpha);
      get_clut_color_index(&clut, cluts, &new_color_index);
      if (i == new_color_index) {
        continue;
      }
      for (unsigned int d = 0; d < size; ++d) {
        if (buf[d] == i) {
          buf[d] = new_color_index;
        }
      }
    }
  } while (0);
  return result;
}

static void transparency_process(const amba_draw_color_key_t *ck,
    int width, int height, unsigned char *buf,
    amba_draw_clut_t *cluts, int draw_fmt)
{
  switch (draw_fmt) {
    case AMBA_DRAW_FORMAT_8BIT_CLUT: {
      const amba_draw_clut_t *ckey = &ck->color;
      unsigned char cr = ck->range;
      for (unsigned int c = 0; c < (unsigned int) OVERLAY_CLUT_MAX_NUM; ++ c) {
        if ((cluts[c].y <= ckey->y + cr && cluts[c].y + cr >= ckey->y)
            && (cluts[c].u <= ckey->u + cr && cluts[c].u + cr >= ckey->u)
            && (cluts[c].v <= ckey->v + cr && cluts[c].v + cr >= ckey->v)) {
          /*DPRINT_NOTICE("User transparent color(%d,%d,%d), Clut index = %d!\n",
              cluts[c].y,
              cluts[c].u,
              cluts[c].v,
              c);*/
          for (int d = 0; d < width * height; ++d) {
            if (buf[d] == c) {
              buf[d] = AMBA_DRAW_CLUT_ENTRY_BACKGROUND;
            }
          }
        }
      }
    } break;
    case AMBA_DRAW_FORMAT_AYUV4444:
    case AMBA_DRAW_FORMAT_ABGR4444:
    case AMBA_DRAW_FORMAT_ARGB4444: {
      unsigned short ckey = ck->rgb;
      if ((ckey & 0xf000) != 0xf000) {
        for (int d = 0; d < 2 * width * height; d += 2) {
          unsigned short value = buf[d] | buf[d + 1] << 8;
          if ((value & 0x0fff) == (ckey & 0x0fff)) {
            buf[d + 1] = (unsigned char) (ckey >> 8);
          }
        }
      }
    } break;
    case AMBA_DRAW_FORMAT_RGBA4444:
    case AMBA_DRAW_FORMAT_BGRA4444: {
      unsigned short ckey = ck->rgb;
      if ((ckey & 0xf) != 0xf) {
        for (int d = 0; d < 2 * width * height; d += 2) {
          unsigned short value = buf[d] | buf[d + 1] << 8;
          if ((value & 0xfff0) == (ckey & 0xfff0)) {
            buf[d] = (unsigned char) (ckey & 0xff);
          }
        }
      }
    } break;
    case AMBA_DRAW_FORMAT_RGBA5551:
    case AMBA_DRAW_FORMAT_BGRA5551: {
      unsigned short ckey = ck->rgb;
      if ((ckey & 0x1) == 0) {
        for (int d = 0; d < 2 * width * height; d += 2) {
          unsigned short value = buf[d] | buf[d + 1] << 8;
          if ((value & 0xfffe) == (ckey & 0xfffe)) {
            buf[d] &= 0xfe;
          }
        }
      }
    } break;
    case AMBA_DRAW_FORMAT_AYUV1555:
    case AMBA_DRAW_FORMAT_ABGR1555:
    case AMBA_DRAW_FORMAT_ARGB1555: {
      unsigned short ckey = ck->rgb;
      if ((ckey & 0x8000) == 0) {
        for (int d = 0; d < 2 * width * height; d += 2) {
          unsigned short value = buf[d] | buf[d + 1] << 8;
          if ((value & 0x7fff) == (ckey & 0x7fff)) {
            buf[d + 1] &= 0x7f;
          }
        }
      }
    } break;
    case AMBA_DRAW_FORMAT_RGBA8888: {
      const amba_draw_argb_t *argb = &ck->argb;
      if (argb->a != 255) {
        for (int d = 0; d < 4 * width * height; d += 4) {
          if (buf[d + 1] == argb->b && buf[d + 2] == argb->g && buf[d + 3] == argb->r) {
            buf[d] = argb->a;
          }
        }
      }
    } break;
    case AMBA_DRAW_FORMAT_BGRA8888: {
      const amba_draw_argb_t *argb = &ck->argb;
      if (argb->a != 255) {
        for (int d = 0; d < 4 * width * height; d += 4) {
          if (buf[d + 1] == argb->r && buf[d + 2] == argb->g && buf[d + 3] == argb->b) {
            buf[d] = argb->a;
          }
        }
      }
    } break;
    case AMBA_DRAW_FORMAT_ABGR8888: {
      const amba_draw_argb_t *argb = &ck->argb;
      if (argb->a != 255) {
        for (int d = 0; d < 4 * width * height; d += 4) {
          if (buf[d] == argb->r && buf[d + 1] == argb->g && buf[d + 2] == argb->b) {
            buf[d + 3] = argb->a;
          }
        }
      }
    } break;
    case AMBA_DRAW_FORMAT_AYUV8888:
    case AMBA_DRAW_FORMAT_ARGB8888: {
      const amba_draw_argb_t *argb = &ck->argb;
      if (argb->a != 255) {
        for (int d = 0; d < 4 * width * height; d += 4) {
          if (buf[d] == argb->b && buf[d + 1] == argb->g && buf[d + 2] == argb->r) {
            buf[d + 3] = argb->a;
          }
        }
      }
    } break;
    default:
      break;
  }
}

/* src_stride_bytes: bytes per row in src_buf (BMP DIB padding included). */
static int picture_zoom(unsigned char *src_buf, int src_width, int src_height,
    int src_stride_bytes,
    unsigned char *dst_buf, amba_rect_t *rect,
    int draw_fmt)
{
  int result = 0;
  do {
    if (!src_buf || !dst_buf) {
      result = -1;
      DPRINT_ERROR("Invalid buffer pointer!\n");
      break;
    }
    if (rect->width == 0 || rect->height == 0) {
      DPRINT_ERROR("Invalid dst rect[%dx%d] size for picture zoom!\n", rect->width, rect->height);
      result = -1;
      break;
    }
    float dw = (float) (src_width) / (float) (rect->width);
    float dh = (float) (src_height) / (float) (rect->height);
    if (draw_fmt >= AMBA_DRAW_FORMAT_16BIT_FIRST &&
        draw_fmt < AMBA_DRAW_FORMAT_16BIT_LAST) {
      int src_max = src_stride_bytes * src_height - 2;
      for (int i = 0; i < rect->height; ++i) {
        int h = src_height - (int) (i * dh) - 1;
        for (int j = 0; j < rect->width; ++j) {
          int t = h * src_stride_bytes + (int) (j * dw) * 2;
          if (t > src_max) {
            t = src_max;
          }
          dst_buf[i * rect->pitch + 2 * j] = src_buf[t];
          dst_buf[i * rect->pitch + 2 * j + 1] = src_buf[t + 1];
        }
      }
    } else if (draw_fmt == AMBA_DRAW_FORMAT_8BIT_CLUT) {
      int src_max = src_stride_bytes * src_height - 1;
      for (int i = 0; i < rect->height; ++i) {
        int h = src_height - (int) (i * dh) - 1;
        for (int j = 0; j < rect->width; ++j) {
          int t = h * src_stride_bytes + (int) (j * dw);
          if (t > src_max) {
            t = src_max;
          }
          dst_buf[i * rect->pitch + j] = src_buf[t];
        }
      }
    } else if (draw_fmt >= AMBA_DRAW_FORMAT_32BIT_FIRST &&
        draw_fmt < AMBA_DRAW_FORMAT_32BIT_LAST) {
      int src_max = src_stride_bytes * src_height - 4;
      for (int i = 0; i < rect->height; ++i) {
        int h = src_height - (int) (i * dh) - 1;
        for (int j = 0; j < rect->width; ++j) {
          int t = h * src_stride_bytes + (int) (j * dw) * 4;
          if (t > src_max) {
            t = src_max;
          }
          dst_buf[i * rect->pitch + 4 * j] = src_buf[t];
          dst_buf[i * rect->pitch + 4 * j + 1] = src_buf[t + 1];
          dst_buf[i * rect->pitch + 4 * j + 2] = src_buf[t + 2];
          dst_buf[i * rect->pitch + 4 * j + 3] = src_buf[t + 3];
        }
      }
    }
  } while (0);

  return result;
}

int amba_draw_pic_data(amba_overlay_area_param_t *area_param,
    amba_draw_clut_t *cluts,
    unsigned char *area_buf,
    bitmap_buffer_t *m_bitmap,
    int draw_fmt)
{
  int result = 0;
  FILE * fp = NULL;
  amba_draw_params_t *params = &area_param->data;
  int n = 0;
  const amba_draw_picture_t *pic = &params->pic;
  int bw = 0, bh = 0, bp = 0;
  unsigned char m_alpha = 255;
  unsigned char *m_buffer = NULL;
  do {

    if (!cluts || !area_buf) {
      DPRINT_ERROR("invalid params\n");
      result = -1;
      break;
    }
    amba_rect_t *rect = &params->rect;
    if (rect->x < 0) {
      rect->x = 0;
    }
    if (rect->y < 0) {
      rect->y = 0;
    }
    if (pic->filename[0] == '\0') {
      DPRINT_ERROR("no filename\n");
      result = -1;
      break;
    } else {
      if (!(fp = fopen(pic->filename, "r"))) {
        DPRINT_ERROR("Failed to open bitmap file [%s].\n", pic->filename);
        result = -1;
        break;
      }

      if ((result = init_bitmap_info(fp, &n, &bw, &bh, &bp, draw_fmt)) != 0) {
        DPRINT_ERROR("init_bitmap_info failed\n");
        break;
      }
      if (bw <= 0 || bh <= 0) {
        DPRINT_ERROR("Invalid picture size(%d x %d)\n", bw, bh);
        result = -1;
        break;
      }

      if (rect->width <= 0) {
        rect->width = bw;
      }
      if (rect->height <= 0) {
        rect->height = bh;
      }
      if (rect->pitch <= 0) {
        rect->pitch = bp;
      }

      if (rect->x + rect->width > area_param->attr.rect.width) {
        DPRINT_ERROR("picture xstart %d + width %d was out of area width %d\n",
            rect->x, rect->width, area_param->attr.rect.width);
        result = -1;
        break;
      }

      if (rect->y + rect->height > area_param->attr.rect.height) {
        DPRINT_ERROR("picture ystart %d + height %d was out of area height %d\n",
            rect->y, rect->height, area_param->attr.rect.height);
        result = -1;
        break;
      }

      if (params->pic.use_bmp_alpha) {
        m_alpha = 0;
      } else if (params->pic.alpha) {
        m_alpha = params->pic.alpha;
      }

      unsigned int offset = rect->pitch * rect->y;
      unsigned int bmp_size;
      if (draw_fmt == AMBA_DRAW_FORMAT_8BIT_CLUT) {
        bmp_size = (unsigned int)bw * (unsigned int)bh;
        offset += rect->x;
      } else if (draw_fmt >= AMBA_DRAW_FORMAT_16BIT_FIRST &&
          draw_fmt < AMBA_DRAW_FORMAT_16BIT_LAST) {
        bmp_size = (unsigned int)bp * (unsigned int)bh;
        offset += 2 * rect->x;
      } else if (draw_fmt >= AMBA_DRAW_FORMAT_32BIT_FIRST &&
          draw_fmt < AMBA_DRAW_FORMAT_32BIT_LAST) {
        bmp_size = (unsigned int)bp * (unsigned int)bh;
        offset += 4 * rect->x;
      } else {
        DPRINT_ERROR("invalid draw format %d\n", draw_fmt);
        result = -1;
        break;
      }
      m_buffer = area_buf + offset;

      if (bmp_size > m_bitmap->size) {
        m_bitmap->size = bmp_size;
        if (m_bitmap->buf) {
          free(m_bitmap->buf);
          m_bitmap->buf = NULL;
        }
      }
      if (!m_bitmap->buf) {
        m_bitmap->buf = (unsigned char *) malloc(m_bitmap->size);
        if (!m_bitmap->buf) {
          DPRINT_ERROR("Failed to malloc new memory!\n");
          result = -1;
          break;
        }
      }
      if (draw_fmt == AMBA_DRAW_FORMAT_8BIT_CLUT) {
        memset(m_bitmap->buf, AMBA_DRAW_CLUT_ENTRY_BACKGROUND, m_bitmap->size);
      } else {
        memset(m_bitmap->buf, 0, m_bitmap->size);
      }

      //make a copy of bmp data info
      if (draw_fmt == AMBA_DRAW_FORMAT_8BIT_CLUT) {
        if (fseek(fp, n * sizeof(amba_draw_rgb_t), SEEK_CUR) != 0) {
          perror("FSEEK");
          result = -1;
          break;
        }
      }
      if (fread(m_bitmap->buf, bmp_size, 1, fp) != 1) {
        perror("FREAD");
        result = -1;
        break;
      }

      if (draw_fmt == AMBA_DRAW_FORMAT_8BIT_CLUT) {
        if (fseek(fp, -(n * sizeof(amba_draw_rgb_t) + bmp_size), SEEK_CUR) != 0) {
          perror("FSEEK");
          result = -1;
          break;
        }
        if ((result = make_bmp_clut(fp, n, bmp_size, m_bitmap->buf, cluts, m_alpha)) != 0) {
          break;
        }
      }
      transparency_process(&pic->colorkey, bw, bh, m_bitmap->buf, cluts, draw_fmt);
      /* because bmp data store order is from bottom to top, so adjust it here,
       * and then zoom the picture to specified rect*/
      if ((result = picture_zoom(m_bitmap->buf, bw, bh, bp, m_buffer, rect, draw_fmt)) != 0) {
        break;
      }
    }

  } while (0);
  if (fp) {
    fclose(fp);
  }
  return result;
}

