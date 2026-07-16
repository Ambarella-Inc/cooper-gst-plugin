/*******************************************************************************
 * am_drawing_data_string.cpp
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
#include <stddef.h>
#include <wctype.h>
#include <sys/types.h>
#include <dirent.h>
#include "pthread.h"

#include "common_err_code_c.h"
#include "internal.h"
#include "debug_log.h"

#include "amba_draw_data_string.h"
#include "osd_draw_default_color.h"

static amba_font_cache_t fonts_caches[MAX_OVERLAY_AREA_NUM] = {0};
static char m_char_set[AMBA_DRAW_STRING_MAX_NUM] =
    " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";;

// use mutex to protect
static pthread_rwlock_t draw_string_ctx_mutex = PTHREAD_RWLOCK_INITIALIZER;

static int find_available_font(char *font)
{
  int result = -1;
  do {
    DIR *dir;
    struct dirent *ptr;
    char fonts_dir[DMAX_FILE_NAME_LENGTH] = "/usr/share/fonts/";
    char *font_extension = NULL;

    if ((dir = opendir(fonts_dir)) == NULL) {
      result = -1;
      perror("OPENDIR");
      break;
    }
    while ((ptr = readdir(dir)) != NULL) {
      if (0 == strcmp(ptr->d_name, ".") || 0 == strcmp(ptr->d_name, ".."))
        continue;
      else if ((font_extension = strchr(ptr->d_name, '.')) != NULL) {
        if (0 == strcmp(font_extension, ".ttf")
            || 0 == strcmp(font_extension, ".ttc")) {
          snprintf(font, DMAX_FILE_NAME_LENGTH - 1, "%s/%s",fonts_dir, ptr->d_name);
          DPRINT_DEBUG("Using a available font: \"%s\"\n", font);
          result = 0;
          break;
        }
      }
    }
    closedir(dir);
    if (result != 0) {
      DPRINT_ERROR("No available fonts in fonts dir: %s\n", fonts_dir);
    }
  } while (0);

  return result;
}

static void adjust_color_with_aphla(amba_draw_clut_t *color,
    amba_draw_clut_t *bg,
    amba_draw_clut_t *new_color)
{
  new_color->y = (uint8_t)((uint32_t)bg->y * (255 - color->a) / 255 +
      (uint32_t)color->y * color->a / 255);
  new_color->u = (uint8_t)((uint32_t)bg->u * (255 - color->a) / 255 +
      (uint32_t)color->u * color->a / 255);
  new_color->v = (uint8_t)((uint32_t)bg->v * (255 - color->a) / 255 +
      (uint32_t)color->v * color->a / 255);
}

static int init_text_info(amba_draw_text_box_t *text,
    amba_draw_clut_t *cluts, int draw_fmt)
{
  int result = 0;

  do {
    if (!cluts) {
      DPRINT_ERROR("Invalid point cluts\n");
      result = -1;
      break;
    }
    amba_draw_font_t *font = &text->font;
    if (0 == font->width) {
      DPRINT_WARNING("font width is 0, adjusted to %d\n", AMBA_DEFAULT_DRAW_FONT_SIZE);
      font->width = AMBA_DEFAULT_DRAW_FONT_SIZE;
    }
    font->height = font->width;

    font->hor_bold = (font->hor_bold < 100) ?
        (font->hor_bold > -100 ? font->hor_bold : -100) : 100;
    font->ver_bold = (font->ver_bold < 100) ?
        (font->ver_bold > -100 ? font->ver_bold : -100) : 100;
    //DPRINT_DEBUG("Font width = %d, height = %d \n", font->width, font->height);

    if (font->ttf_name[0] != '\0') {
      if (access(font->ttf_name, F_OK) == -1) {
        DPRINT_WARNING("Font \"%s\" does not exist!\n", font->ttf_name);
        if ((result = find_available_font(font->ttf_name)) != 0) {
          break;
        }
      }
    } else {
      DPRINT_DEBUG("No font type parameter!\n");
      if ((result = find_available_font(font->ttf_name)) != 0) {
        break;
      }
    }

    font->italic = (font->italic <= 0) ? 0 :
        ((font->italic <= 100) ? font->italic : 100);

    if (draw_fmt == AMBA_DRAW_FORMAT_8BIT_CLUT) {
      uint8_t color_id = text->font_color.id;
      amba_draw_clut_t *fc = &text->font_color.color;
      if (color_id < 8) {
        memcpy(fc, &AMBA_DEFAULT_DRAW_YUV[color_id], sizeof(amba_draw_clut_t));
      }
      result = get_clut_color_index(fc, cluts, &text->m_bitmap.color_index);
      if (result != 0) {
        DPRINT_ERROR("Failed to get clut color index");
        break;
      }

      amba_draw_clut_t *bc = &text->background_color;
      if ((bc->a == 0) && (bc->u == 0) && (bc->v == 0) && (bc->y == 0)) {
        bc->y = AMBA_DEFAULT_DRAW_BACKGROUND_Y;
        bc->u = AMBA_DEFAULT_DRAW_BACKGROUND_U;
        bc->v = AMBA_DEFAULT_DRAW_BACKGROUND_V;
        bc->a = AMBA_DEFAULT_DRAW_BACKGROUND_ALPHA;
        DPRINT_DEBUG("Use default backgroud color!!! \n");
      }
      result = get_clut_color_index(bc, cluts, &text->m_bitmap.background_color_index);
      if (result != 0) {
        DPRINT_ERROR("Failed to get clut color index");
        break;
      }

      amba_draw_clut_t *oc = &text->outline_color;
      if (font->outline_width) {
        if ((oc->a == 0) && (oc->u == 0) && (oc->v == 0) && (oc->y == 0)) {
          oc->y = AMBA_DEFAULT_DRAW_OUTLINE_Y;
          oc->u = AMBA_DEFAULT_DRAW_OUTLINE_U;
          oc->v = AMBA_DEFAULT_DRAW_OUTLINE_V;
          oc->a = AMBA_DEFAULT_DRAW_OUTLINE_ALPHA;
          DPRINT_DEBUG("Use default outline color!!! \n");
        }
        result = get_clut_color_index(oc, cluts, &text->m_bitmap.outline_color_index);
        if (result != 0) {
          DPRINT_ERROR("Failed to get clut color index");
          break;
        }
      }
    } else if (draw_fmt >= AMBA_DRAW_FORMAT_16BIT_FIRST &&
        draw_fmt < AMBA_DRAW_FORMAT_32BIT_LAST) {
      uint32_t color_id = text->font_color.id;
      amba_draw_clut_t *bc = &text->background_color;
      amba_draw_clut_t *oc = &text->outline_color;

      if (draw_fmt == AMBA_DRAW_FORMAT_UYV565 ||
          draw_fmt == AMBA_DRAW_FORMAT_AYUV4444 ||
          draw_fmt == AMBA_DRAW_FORMAT_AYUV1555 ||
          draw_fmt == AMBA_DRAW_FORMAT_YUV1555 ||
          draw_fmt == AMBA_DRAW_FORMAT_AYUV8888) {
        if (color_id < 8) {
          memcpy(&(text->font_color.color), &AMBA_DEFAULT_DRAW_YUV[color_id], sizeof(amba_draw_clut_t));
        }

        if ((bc->a == 0) && (bc->u == 0) && (bc->v == 0) && (bc->y == 0)) {
          bc->y = AMBA_DEFAULT_DRAW_BACKGROUND_Y;
          bc->u = AMBA_DEFAULT_DRAW_BACKGROUND_U;
          bc->v = AMBA_DEFAULT_DRAW_BACKGROUND_V;
          bc->a = AMBA_DEFAULT_DRAW_BACKGROUND_ALPHA;
          DPRINT_DEBUG("Use default backgroud color!!! \n");
        }

        if (font->outline_width) {
          if ((oc->a == 0) && (oc->u == 0) && (oc->v == 0) && (oc->y == 0)) {
            oc->y = AMBA_DEFAULT_DRAW_OUTLINE_Y;
            oc->u = AMBA_DEFAULT_DRAW_OUTLINE_U;
            oc->v = AMBA_DEFAULT_DRAW_OUTLINE_V;
            oc->a = AMBA_DEFAULT_DRAW_OUTLINE_ALPHA;
            DPRINT_DEBUG("Use default outline color!!! \n");
          }
        }
      } else {
        if (color_id < 8) {
          memcpy(&(text->font_color.color), &AMBA_DEFAULT_DRAW_RGB[color_id], sizeof(amba_draw_clut_t));
        }

        if ((bc->a == 0) && (bc->u == 0) && (bc->v == 0) && (bc->y == 0)) {
          bc->y = AMBA_DEFAULT_DRAW_BACKGROUND_R;
          bc->u = AMBA_DEFAULT_DRAW_BACKGROUND_G;
          bc->v = AMBA_DEFAULT_DRAW_BACKGROUND_B;
          bc->a = AMBA_DEFAULT_DRAW_BACKGROUND_ALPHA;
          DPRINT_DEBUG("Use default backgroud color!!! \n");
        }

        if (font->outline_width) {
          if ((oc->a == 0) && (oc->u == 0) && (oc->v == 0) && (oc->y == 0)) {
            oc->y = AMBA_DEFAULT_DRAW_OUTLINE_R;
            oc->u = AMBA_DEFAULT_DRAW_OUTLINE_G;
            oc->v = AMBA_DEFAULT_DRAW_OUTLINE_B;
            oc->a = AMBA_DEFAULT_DRAW_OUTLINE_ALPHA;
            DPRINT_DEBUG("Use default outline color!!! \n");
          }
        }
      }

      // Fill Background color,no outline color support for rgb565
      memcpy(&cluts[AMBA_TEXT_CLUT_ENTRY_BACKGOURND],
          &text->background_color,
          sizeof(amba_draw_clut_t));
      memcpy(&cluts[AMBA_TEXT_CLUT_ENTRY_OUTLINE],
          &text->outline_color,
          sizeof(amba_draw_clut_t));
      memcpy(&cluts[AMBA_TEXT_CLUT_ENTRY_FONT],
          &text->font_color.color,
          sizeof(amba_draw_clut_t));
      /* 0,1: text bg/outline; 2,3,4: drawdatagen bbox/seg; 5~253: AA ramp.
       * adjust_color_with_aphla only fills y/u/v; RGB/YUV is already the composite
       * of font over bg at coverage i/255 — set a to match font so ARGB/AYUV paths
       * are not left at 0 from memset. */
      {
        uint8_t saved_font_alpha = text->font_color.color.a;
        for (uint32_t i = 5; i < AMBA_TEXT_CLUT_ENTRY_FONT; ++i) {
          text->font_color.color.a = (uint8_t) (i);
          adjust_color_with_aphla(&text->font_color.color,
              &text->background_color, &cluts[i]);
          cluts[i].a = saved_font_alpha;
        }
        text->font_color.color.a = saved_font_alpha;
      }
    }else {
      DPRINT_ERROR("Invalid drawing format:%d",draw_fmt);
      result = -1;
      break;
    }
  } while (0);
  return result;
}

int init_textinsert_lib(amba_draw_font_t *font, amba_text_bitmap_t *text_bitmap)
{
  int result = 0;
  do {
    if (text_bitmap->m_font_lib_init == 0) {
      text2bitmap_t *m_bitmap = &text_bitmap->m_bitmap;
      font_attribute_t *font_attr = &m_bitmap->font;//{{0}, 0};
      font_attr->size = font->width;
      font_attr->outline_width = font->outline_width;
      font_attr->hori_bold = font->hor_bold;
      font_attr->vert_bold = font->ver_bold;
      font_attr->italic = font->italic;
      font_attr->disable_anti_alias = font->disable_anti_alias;

      m_bitmap->library = (void *)malloc(sizeof(uintptr_t));
      m_bitmap->library_size = sizeof(uintptr_t);
      m_bitmap->face = (void *)malloc(sizeof(uintptr_t));
      m_bitmap->face_size = sizeof(uintptr_t);
      m_bitmap->pixel.pixel_outline = DEFAULT_PIXEL_OUTLINE;//text_bitmap->outline_color_index;
      m_bitmap->pixel.pixel_background = DEFAULT_PIXEL_BACKGROUND;//text_bitmap->background_color_index;
      m_bitmap->pixel.pixel_font = DEFAULT_PIXEL_FONT;//text_bitmap->color_index;
      if (font->ttf_name[0] != '\0') {
        if (access(font->ttf_name, F_OK) == -1) {
          DPRINT_ERROR("Font \"%s\" does not exist!\n", font->ttf_name);
          if ((result = find_available_font(font->ttf_name)) != 0) {
            break;
          }
        }
      } else {
        DPRINT_DEBUG("No font type parameter!\n");
        if ((result = find_available_font(font->ttf_name)) != 0) {
          break;
        }
      }
      snprintf(font_attr->type, sizeof(font_attr->type), "%s", font->ttf_name);
      DPRINT_DEBUG("Font type = %s \n", font_attr->type);
      if (text2bitmap_lib_init(m_bitmap) < 0) {
        result = -1;
        DPRINT_ERROR("Failed to init text insert library.");
        break;
      }
      text_bitmap->m_font_lib_init = 1;
    }
  } while (0);
  return result;
}

int update_textinsert_lib(amba_draw_font_t *font, amba_text_bitmap_t *text_bitmap)
{
  int result = 0;
  do {
    if (text_bitmap->m_font_lib_init) {
      font_attribute_t font_attr;
      memset(&font_attr, 0x0, sizeof(font_attribute_t));
      font_attr.size = font->width;
      if (0 == font->width) {
        DPRINT_ERROR("font 0 == font->width!\n");
        break;
      }
      font_attr.outline_width = font->outline_width;
      font_attr.hori_bold = font->hor_bold;
      font_attr.vert_bold = font->ver_bold;
      font_attr.italic = font->italic;
      font_attr.disable_anti_alias = font->disable_anti_alias;
      snprintf(font_attr.type, sizeof(font_attr.type), "%s",
               font->ttf_name);
      DPRINT_DEBUG("Font type = %s \n", font_attr.type);
      if (memcmp(&font_attr, &(text_bitmap->m_bitmap.font), sizeof(font_attribute_t)) == 0) {
        DPRINT_INFO("font attribute is same, no need to update!\n");
        break;
      }
      if (font->ttf_name[0] != '\0') {
        if (access(font->ttf_name, F_OK) == -1) {
          DPRINT_ERROR("Font \"%s\" does not exist!\n", font->ttf_name);
          if ((result = find_available_font(font->ttf_name)) != 0) {
            break;
          }
        }
      } else {
        DPRINT_DEBUG("No font type parameter!\n");
        if ((result = find_available_font(font->ttf_name)) != 0) {
          break;
        }
      }
      snprintf(font_attr.type, sizeof(font_attr.type), "%s", font->ttf_name);
      memcpy(&text_bitmap->m_bitmap.font, &font_attr, sizeof(font_attribute_t));
      if (text2bitmap_update_font_attribute(&font_attr, &text_bitmap->m_bitmap) < 0) {
        result = -1;
        DPRINT_ERROR("Failed to text2bitmap_update_font_attribute.");
        break;
      }
    } else {
      DPRINT_ERROR("Please call init text insert library first.");
    }
  } while (0);
  return result;
}

int deinit_textinsert_lib(amba_text_bitmap_t *text_bitmap)
{
  int result = 0;
  if (text_bitmap->m_font_lib_init) {
    if (text2bitmap_lib_deinit(&text_bitmap->m_bitmap) < 0) {
      result = -1;
    }
    if (text_bitmap->m_bitmap.library) {
      free(text_bitmap->m_bitmap.library);
      text_bitmap->m_bitmap.library = NULL;
    }
    if (text_bitmap->m_bitmap.face) {
      free(text_bitmap->m_bitmap.face);
      text_bitmap->m_bitmap.face = NULL;
    }
    text_bitmap->m_font_lib_init = 0;
  }
  return result;
}

static unsigned char is_all_included(const char *sub_str, const char *str)
{
  unsigned char ret = 1;

  while (*sub_str != '\0') {
    const char c = *sub_str;
    const char *old_str = str;
    unsigned char is_find = 0;
    while (*old_str != '\0') {
      if (*old_str == c) {
        is_find = 1;
        break;
      }
      old_str++;
    }

    if (!is_find) {
      ret = 0;
      break;
    }

    sub_str++;
  }
  return ret;
}

static void swap(int *a, int *b)
{
  int temp = *a;
  *a = *b;
  *b = temp;
}

static int get_bmp_max_size_from_vector(
    const amba_draw_char_cache_dram_t *char_caches,
    amba_resolution_t *max_bmp)
{
  for (int i = 0; i < AMBA_DRAW_STRING_MAX_NUM; i++) {
    if (max_bmp->width < char_caches[i].width) {
      max_bmp->width = char_caches[i].width;
    }
    if (max_bmp->height < char_caches[i].height) {
      max_bmp->height = char_caches[i].height;
    }
  }
  return 0;
}

static int convert_text_to_bmp_vector(amba_draw_font_t *font,
    const char *str, amba_text_bitmap_t *text_bitmap, amba_draw_char_cache_dram_t *char_caches,
    int skip_cache_read)
{
  int result = 0;

  do {
    unsigned int i = 0;
    if (!text_bitmap->m_font_lib_init) {
      result = init_textinsert_lib(font, text_bitmap);
    } else {
      result = update_textinsert_lib(font, text_bitmap);
    }
    if (result != 0) {
      DPRINT_ERROR("Failed to init text insert library or update font_attribute.\n");
      break;
    }
    wchar_t wstr[AMBA_DRAW_STRING_MAX_NUM] = {L'\0'};
    size_t wlen = mbstowcs(wstr, str, AMBA_DRAW_STRING_MAX_NUM);
    if (wlen == (size_t)-1) {
      DPRINT_ERROR("mbstowcs failed, invalid multibyte sequence\n");
      result = -1;
      break;
    }

    bitmap_info_t bmp_info = {0};
    int font_buf_width = text_bitmap->m_bitmap.font.size;
    amba_resolution_t font_max_bmp = {0};
    if (skip_cache_read) {
      /* Caller holds wrlock (do_cache), skip cache lookup to avoid deadlock */
    } else {
      pthread_rwlock_rdlock(&draw_string_ctx_mutex);
      for (i = 0; i < MAX_OVERLAY_AREA_NUM; i++) {
        if (fonts_caches[i].valid) {
          if (memcmp(font, &fonts_caches[i].font, sizeof(amba_draw_font_t)) == 0) {
            memcpy(&font_max_bmp, &fonts_caches[i].max_size,
                sizeof(amba_resolution_t));
            break;
          }
        }
      }
      pthread_rwlock_unlock(&draw_string_ctx_mutex);
    }

    if (0 == font_max_bmp.height) {
      font_max_bmp.height = font_buf_width * 2 + 3;
    }
    int font_buf_height = font_max_bmp.height;
    size_t font_buf_size = font_buf_width * font_buf_height;
    unsigned char *font_buf = (unsigned char *) malloc(font_buf_size * sizeof(unsigned char));
    size_t len = (wlen < AMBA_DRAW_STRING_MAX_NUM) ? wlen : AMBA_DRAW_STRING_MAX_NUM - 1;
    for (i = 0; i < len; ++i) {
      memset(font_buf, 0, font_buf_size);
      memset(&bmp_info, 0, sizeof(bitmap_info_t));
      if (iswprint(wstr[i])) {
        if (text2bitmap_convert_character(wstr[i], font_buf,
            font_buf_height, font_buf_width, 0,
            &bmp_info, &text_bitmap->m_bitmap) < 0) {
          DPRINT_ERROR("Text2bitmap library: Failed to convert the character[%c]."
                "index[%d] ignored",str[i], i);
          continue;
        }
      } else {
        bmp_info.width = (font_buf_width + 1) / 2;
        DPRINT_NOTICE("Treat control characters as whitespace\n");
      }
      if (bmp_info.width > font_buf_width) {
        DPRINT_WARNING("bmp_info.width %d is greater than font_buf_width %d\n",
             bmp_info.width, font_buf_width);
        bmp_info.width = font_buf_width;
      }
      if (bmp_info.height > font_buf_height) {
        DPRINT_WARNING("bmp_info.height %d is greater than font_buf_height %d\n",
             bmp_info.height, font_buf_height);
        bmp_info.height = font_buf_height;
      }
      char_caches[i].wch = wstr[i];
      char_caches[i].width = bmp_info.width;
      char_caches[i].height = font_buf_height;
      char_caches[i].buffer = (unsigned char *) malloc(char_caches[i].width * char_caches[i].height * sizeof(unsigned char));
      memset(char_caches[i].buffer, 0, (char_caches[i].width * char_caches[i].height * sizeof(unsigned char)));
      uint8_t *src = font_buf;
      uint8_t *dst = char_caches[i].buffer;
      for (int h = 0; h < char_caches[i].height; h++) {
        for (int w = 0; w < char_caches[i].width; w++) {
          dst[w] = src[w];
        }
        src += font_buf_width;
        dst += char_caches[i].width;
      }
    }
    if (font_buf) {
      free(font_buf);
      font_buf = NULL;
    }
  } while (0);
  return result;
}

static int convert_text_to_bmp_do_cache(amba_draw_font_t *font,
    const char *str, amba_text_bitmap_t *text_bitmap)
{
  int result = 0;
  do {
    unsigned int i = 0;
    unsigned char is_find = 0;

    pthread_rwlock_rdlock(&draw_string_ctx_mutex);
    for (i = 0; i < MAX_OVERLAY_AREA_NUM; i++) {
      if (fonts_caches[i].valid) {
        if (memcmp(font, &fonts_caches[i].font, sizeof(amba_draw_font_t)) == 0) {
          is_find = 1;
          break;
        }
      }
    }
    pthread_rwlock_unlock(&draw_string_ctx_mutex);

    if (is_find) {
      //DPRINT_DEBUG("Fonts %s  had been caches.\n", font->ttf_name);
      break;
    }

    pthread_rwlock_wrlock(&draw_string_ctx_mutex);

    for (i = 0; i < MAX_OVERLAY_AREA_NUM; i++) {
      if (fonts_caches[i].valid == 0) {
        memcpy(&fonts_caches[i].font, font, sizeof(amba_draw_font_t));
        result = convert_text_to_bmp_vector(font, str, text_bitmap, fonts_caches[i].char_caches, 1);
        get_bmp_max_size_from_vector(fonts_caches[i].char_caches, &fonts_caches[i].max_size);
        fonts_caches[i].valid = 1;
        break;
      }
    }

    pthread_rwlock_unlock (&draw_string_ctx_mutex);
  } while (0);
  return result;
}

static size_t adjust_str_offset_y(size_t offset_y,
    size_t font_width,
    size_t font_height,
    size_t area_height,
    size_t rect_height,
    unsigned char is_add_y)
{
  size_t result = 0;
  if (is_add_y) {
    if (offset_y >= font_width) {//up
      result = offset_y - (font_height - font_width);
    } else if ((area_height - offset_y - rect_height - font_height > 0) &&
        ((offset_y + rect_height) <= (area_height - font_height))) { // bottom
      result = offset_y + rect_height;
    } else { // in
      result = offset_y;
    }
  } else {
    if (offset_y >= font_width) { // up
      result = offset_y + (font_height - font_width);
      if (result > area_height) {
        result = rect_height; // bottom
      }
    } else { // in
      result = offset_y;
    }
  }
  return result;
}

static int convert_text_to_bmp_with_rorate(
    amba_draw_text_box_t *text,
    amba_rect_t *rect,
    unsigned char *text_draw_buffer,
    unsigned char is_cache,
    /*unsigned char stream_rotate,
    unsigned char m_area_rotate_mode,*/
    int draw_fmt)
{
  int result = 0;
  do {
    if (!text_draw_buffer) {
      DPRINT_ERROR("Empty pointer");
      result = -1;
      break;
    }
    amba_draw_font_t *font = &text->font;
    amba_draw_char_cache_dram_t char_caches[AMBA_DRAW_STRING_MAX_NUM] = {0};
    amba_resolution_t font_max_bmp = {0};
    unsigned char is_find = 0;
    char *str = text->str;
    if (str[0] == '\0') {
      strncpy(str, "Hello, Ambarella!", AMBA_DRAW_STRING_MAX_NUM - 1);
      str[AMBA_DRAW_STRING_MAX_NUM - 1] = '\0';
    }
    wchar_t w_char_text[AMBA_DRAW_STRING_MAX_NUM] = {L'\0'};
    size_t wlen = mbstowcs(w_char_text, str, AMBA_DRAW_STRING_MAX_NUM);
    if (wlen == (size_t)-1) {
      DPRINT_ERROR("mbstowcs failed in convert_text_to_bmp_with_rorate\n");
      result = -1;
      break;
    }

    if (is_cache) {
      if ((result = convert_text_to_bmp_do_cache(font, m_char_set, &text->m_bitmap)) != 0) {
        DPRINT_ERROR("Failed to convert text to bmp do cache\n!");
        break;
      }
      pthread_rwlock_rdlock(&draw_string_ctx_mutex);
      for (int i = 0; i < MAX_OVERLAY_AREA_NUM; i++) {
        if (fonts_caches[i].valid) {
          if (memcmp(font, &fonts_caches[i].font, sizeof(amba_draw_font_t)) == 0) {
            memcpy(char_caches, fonts_caches[i].char_caches,
                AMBA_DRAW_STRING_MAX_NUM * sizeof(amba_draw_char_cache_dram_t));
            memcpy(&font_max_bmp, &fonts_caches[i].max_size, sizeof(amba_resolution_t));
            is_find = 1;
            break;
          }
        }
      }
      pthread_rwlock_unlock(&draw_string_ctx_mutex);
      if (!is_find) {
        DPRINT_ERROR("Fonts %s  no caches.\n", font->ttf_name);
        result = -1;
        break;
      }
    } else {
      convert_text_to_bmp_vector(font, text->str, &text->m_bitmap, char_caches, 0);
      get_bmp_max_size_from_vector(char_caches, &font_max_bmp);
    }

    int buf_w_max = rect->pitch;
    int offset_x = 0;
    int offset_y = 0;
    bitmap_info_t bmp_info = {0};
    uint8_t *line_head = text_draw_buffer;
    int font_pitch = font->width;
    int font_height = font_max_bmp.height;
    size_t length = wlen;
    unsigned char is_first_line = 1;
    unsigned char is_add_new_line = 0;
    int w = rect->width;
    if (text->m_string_is_from_rect) {
      const amba_point_t *p1 = &text->pp.p1;
      const amba_point_t *p2 = &text->pp.p2;
      int min_x = MIN(p1->x, p2->x);
      int max_x = MAX(p1->x, p2->x);
      w = max_x - min_x;
    }
    for (size_t i = 0; i < length; ++i) {
      is_add_new_line = (offset_x + font_pitch) >= (w);
      if (is_add_new_line) {
        for (int j = 0; j < AMBA_DRAW_STRING_MAX_NUM; j++) {
          if (char_caches[j].wch == w_char_text[i]) {
            if (char_caches[j].buffer) {
              is_add_new_line = (offset_x + char_caches[j].width) >= (w);
            } else {
              is_add_new_line = (offset_x + font_pitch / 2) >= (w);
            }
          }
        }
      }
      if (is_add_new_line || is_first_line) {
        // Add a new line
        //DPRINT_DEBUG("%ld: offset_x = %d, font pitch = %d, font area width = %d \n", i,
            //offset_x, font_pitch, buf_w_max);
        //DPRINT_DEBUG("Add a new line.\n");
        if (text->is_cut_off_string) {
          DPRINT_DEBUG("is_cut_off_string is on,break\n");
          break;
        }
        offset_x = 0;
        if (is_add_new_line) {
          offset_y += font_pitch;
        }
        if ((font_height + offset_y) > rect->height) {
          if (is_first_line) { // No more new line
            DPRINT_ERROR("Cannot draw:drawing area is too small or font size is too "
                "large! font_height: %d,rect height:%d\n",
                font_height, rect->height);
          } else { // No more new line
            DPRINT_WARNING("No more space for a new line,only the %ld character is drawn!\n",(i + 1));
          }
          break;
        }
        line_head = text_draw_buffer + buf_w_max * offset_y;
      }
      is_first_line = 0;
      uint8_t *src = NULL;
      uint8_t *dst = NULL;
      for (int j = 0; j < AMBA_DRAW_STRING_MAX_NUM; j++) {
        if (char_caches[j].wch == w_char_text[i]) {
          if (char_caches[j].buffer) {
            src = char_caches[j].buffer;
            dst = line_head + offset_x;
            for (int h = 0; h < char_caches[j].height; h++) {
              for (int w = 0; w < char_caches[j].width; w++) {
                dst[w] = src[w];
                if (draw_fmt == AMBA_DRAW_FORMAT_8BIT_CLUT) {
                  if (font->disable_anti_alias) {
                    if (src[w] == DEFAULT_PIXEL_BACKGROUND) {
                      dst[w] = text->m_bitmap.background_color_index;
                    } else if (src[w] == DEFAULT_PIXEL_FONT) {
                      dst[w] = text->m_bitmap.color_index;
                    }
                  } else {
                    if (src[w] == DEFAULT_PIXEL_BACKGROUND) {
                      dst[w] = text->m_bitmap.background_color_index;
                    } else if (src[w] == DEFAULT_PIXEL_FONT) {
                      dst[w] = text->m_bitmap.color_index;
                    } else {
                      dst[w] = text->m_bitmap.outline_color_index;
                    }
                  }
                } else {
                  if (src[w] == DEFAULT_PIXEL_FONT) {
                    dst[w] = AMBA_TEXT_CLUT_ENTRY_FONT;
                  }
                }
              }
              src += char_caches[j].width;
              dst += buf_w_max;
            }
            bmp_info.width = char_caches[j].width;
            bmp_info.height = char_caches[j].height;
            break;
          }else{
            if (0 == char_caches[j].width) {
              dst = line_head + offset_x;
              for (int h = 0; h < char_caches[j].height; h++) {
                dst += (buf_w_max + 1) / 2;
              }
            }
          }
        }
      }
      int stride = (bmp_info.width + text->spacing) >= 0
          ? (bmp_info.width + text->spacing)
          : 0;
      offset_x += stride;
      if (text->spacing < 0) {
        for (int h = 0; h < font_height; ++h) {
          int offset = h * buf_w_max + offset_x + stride;
          memset(line_head + offset, 0, -text->spacing);
        }
      }
    }
    if (!is_cache) {
      for (int j = 0; j < AMBA_DRAW_STRING_MAX_NUM; j++) {
        if (char_caches[j].buffer) {
          free(char_caches[j].buffer);
          char_caches[j].buffer = NULL;
        }
      }
    }
  } while (0);
  return result;
}

static int convert_text_to_bmp_keep_no_rorate(
    amba_draw_text_box_t *text,
    amba_rect_t *rect,
    unsigned char *text_draw_buffer,
    unsigned char is_cache,
    unsigned char stream_rotate,
    /*unsigned char m_area_rotate_mode,*/
    int draw_fmt)
{
  int result = 0;
  do {
    if (!text_draw_buffer) {
      DPRINT_ERROR("Empty pointer");
      result = -1;
      break;
    }
    amba_draw_font_t *font = &text->font;
    amba_draw_char_cache_dram_t char_caches[AMBA_DRAW_STRING_MAX_NUM] = {0};
    amba_resolution_t font_max_bmp = {0};
    unsigned char is_find = 0;
    char *str = text->str;
    if (str[0] == '\0') {
      strncpy(str, "Hello, Ambarella!", AMBA_DRAW_STRING_MAX_NUM - 1);
      str[AMBA_DRAW_STRING_MAX_NUM - 1] = '\0';
    }
    wchar_t w_char_text[AMBA_DRAW_STRING_MAX_NUM] = {L'\0'};
    size_t wlen = mbstowcs(w_char_text, str, AMBA_DRAW_STRING_MAX_NUM);
    if (wlen == (size_t)-1) {
      DPRINT_ERROR("mbstowcs failed in convert_text_to_bmp_keep_no_rorate\n");
      result = -1;
      break;
    }

    if (is_cache) {
      if ((result = convert_text_to_bmp_do_cache(font, m_char_set, &text->m_bitmap)) != 0) {
        DPRINT_ERROR("Failed to convert text to bmp do cache\n!");
        break;
      }
      pthread_rwlock_rdlock(&draw_string_ctx_mutex);
      for (int i = 0; i < MAX_OVERLAY_AREA_NUM; i++) {
        if (fonts_caches[i].valid) {
          if (memcmp(font, &fonts_caches[i].font, sizeof(amba_draw_font_t)) == 0) {
            memcpy(char_caches, fonts_caches[i].char_caches,
                AMBA_DRAW_STRING_MAX_NUM * sizeof(amba_draw_char_cache_dram_t));
            memcpy(&font_max_bmp, &fonts_caches[i].max_size, sizeof(amba_resolution_t));
            is_find = 1;
            break;
          }
        }
      }
      pthread_rwlock_unlock(&draw_string_ctx_mutex);
      if (!is_find) {
        DPRINT_ERROR("Fonts %s  no caches.\n", font->ttf_name);
        result = -1;
        break;
      }
    } else {
      convert_text_to_bmp_vector(font, text->str, &text->m_bitmap, char_caches, 0);
      get_bmp_max_size_from_vector(char_caches, &font_max_bmp);
    }

    int font_pitch = font->width;
    int font_height = font_max_bmp.height;
    size_t length = wlen;
    bitmap_info_t bmp_info = {0};
    int buf_w_max = rect->pitch;
    int rorate = stream_rotate;
    const amba_point_t *p1 = &text->pp.p1;
    const amba_point_t *p2 = &text->pp.p2;
    int min_y = MIN(p1->y, p2->y);
    int min_x = MIN(p1->x, p2->x);
    int max_x = MAX(p1->x, p2->x);
    int max_y = MAX(p1->y, p2->y);
    int offset_x = min_x;
    int offset_y = min_y;
    int line_offset_x = 0;
    int size_w = rect->width;
    int size_h = rect->height;
    int drawable_height = MIN((max_y - min_y), size_h);
    int drawable_width = MIN((max_x - min_x), size_w);
    if (max_x >= size_w || max_y >= size_h) {
      DPRINT_WARNING("The data rect's size[%dx%d] can't exceed area's size[%dx%d]", max_x,
           max_y, size_w, size_h);
      break;
    }
    int offset = 0;
    int offset_new_line = 0;
    unsigned char is_add_x = 1;
    unsigned char is_add_y = 1;
    unsigned char is_rorate_90 = 0;
    if (rorate & AMBA_DRAW_ROTATE_90) {
      is_rorate_90 = 1;
      swap(&drawable_width, &drawable_height);
    }
    // Take the starting point of drawing as the coordinate origin,
    // According to the original width of area, it is the x axis, and the height is the y axis.
    // Calculate the increase/decrease of x and y axis during each drawing.
    if (is_rorate_90) {
      {
        offset_y = max_y;
        offset_x = min_x;
        is_add_x = 1;
        is_add_y = 0;
        is_rorate_90 = 1;
      }
      if (rorate & AMBA_DRAW_VERTICAL_FLIP) {
        is_add_y = !is_add_y;
        offset_y = min_y;
      }
      if (rorate & AMBA_DRAW_HORIZONTAL_FLIP) {
        offset_x = max_x;
        is_add_x = !is_add_x;
      }
      offset_x = adjust_str_offset_y((size_t)offset_x, (size_t)font_pitch,
          (size_t)font_height, size_w, drawable_height, is_add_x);
    } else {
      {
        offset_x = min_x;
        offset_y = min_y;
        is_add_x = 1;
        is_add_y = 1;
      }
      if (rorate & AMBA_DRAW_VERTICAL_FLIP) {
        offset_y = max_y;
        is_add_y = !is_add_y;
      }
      if (rorate & AMBA_DRAW_HORIZONTAL_FLIP) {
        offset_x = max_x;
        is_add_x = !is_add_x;
      }
      offset_y =
          adjust_str_offset_y((size_t)offset_y, (size_t)font_pitch,
          (size_t)font_height, (size_t)size_h, drawable_height, is_add_y);
    }
    offset = rect->pitch * offset_y;
    int offset_x_bak = offset_x;
    int offset_y_bak = offset_y;
    unsigned char *line_head = text_draw_buffer + offset;
    unsigned char is_add_new_line = 0;
    unsigned char is_no_space = 0;
    int rorate_90_offset = 0;
    int32_t cur_line_draw_w = 0;
    if (is_rorate_90) {
      offset_y = offset_y_bak;
    } else {
      offset_x = 0;
      if (rorate & AMBA_DRAW_HORIZONTAL_FLIP) {
        offset_x = offset_x_bak;
      }
    }
    for (size_t i = 0; i < length; ++i) {
      is_add_new_line = (cur_line_draw_w + font_pitch) > drawable_width;
      if (is_add_new_line) {
        for (int j = 0; j < AMBA_DRAW_STRING_MAX_NUM; j++) {
          if (char_caches[j].wch == w_char_text[i]) {
            if (char_caches[j].buffer) {
              is_add_new_line = (cur_line_draw_w + char_caches[j].width) > drawable_width;
            } else {
              is_add_new_line = (cur_line_draw_w + font_pitch / 2) > drawable_width;
            }
          }
        }
      }
      if (is_add_new_line) {
        if (text->is_cut_off_string) {
          DPRINT_NOTICE("is_cut_off_string is on,break\n");
          break;
        }
        cur_line_draw_w = 0;
        if (is_rorate_90) {
          offset_y = offset_y_bak;
          rorate_90_offset = 0;
        } else {
          offset_x = 0;
          if (rorate & AMBA_DRAW_HORIZONTAL_FLIP) {
            offset_x = offset_x_bak;
          }
        }
        if (is_rorate_90) {
          is_no_space = (font_height + offset_x) > size_w;
          if (rorate & AMBA_DRAW_VERTICAL_FLIP) {
            is_no_space = (offset_x - font_height) < 0;
          }
          if (is_no_space) {
            DPRINT_WARNING("No more space for a new line, drop left characters! font "
                "height:%u,offset_x:%u,"
                "area height:%u\n",
                font_height, offset_x, size_h);
            break;
          } else {
            if (rorate & AMBA_DRAW_VERTICAL_FLIP) {
              offset_new_line -= font_pitch;
              offset_x -= font_height;
            } else {
              offset_new_line += font_pitch;
              offset_x += font_height;
            }
            line_offset_x = offset_x;
          }
        } else {
          is_no_space = (font_height + offset_y) > size_h;
          if (rorate & AMBA_DRAW_VERTICAL_FLIP) {
            is_no_space = (offset_y - font_height) < 0;
          }
          if (is_no_space) {
            DPRINT_WARNING("No more space for a new line, drop left characters! font "
                "height:%u,offset_y:%u,"
                "area height:%u\n",
                font_height, offset_y, size_h);
            break;
          } else {
            if (rorate & AMBA_DRAW_VERTICAL_FLIP) {
              offset_new_line -= (font_pitch * buf_w_max);
              offset_y -= font_height;
            } else {
              offset_new_line += (font_pitch * buf_w_max);
              offset_y += font_height;
            }
          }
        }
        DPRINT_NOTICE("%ld: offset_x = %d, font pitch = %d, font area width = %d \n", i,
            offset_x, font_pitch, buf_w_max);
        DPRINT_NOTICE("Add a new line.\n");
      }
      unsigned char *src = NULL;
      unsigned char *dst = NULL;
      int pos = 0;
      /*if (draw_fmt >= AMBA_DRAW_FORMAT_16BIT_FIRST &&
          draw_fmt < AMBA_DRAW_FORMAT_16BIT_LAST) {
        offset_new_line *= 2;
      } else if (draw_fmt >= AMBA_DRAW_FORMAT_32BIT_FIRST &&
          draw_fmt < AMBA_DRAW_FORMAT_32BIT_LAST) {
        offset_new_line *= 4;
      }*/
      dst = line_head + offset_x_bak + offset_new_line;
      int64_t diff_pos = 0;
      int64_t size_buf = size_w * size_h;
      unsigned char is_skip_char = 0;
      for (int j = 0; j < AMBA_DRAW_STRING_MAX_NUM; j++) {
        if (char_caches[j].wch == w_char_text[i]) {
          if (char_caches[j].buffer) {
            src = char_caches[j].buffer;
            for (int h = 0; !is_skip_char && h < char_caches[j].height; h++) {
              for (int w = 0; w < char_caches[j].width; w++) {
                if (is_rorate_90) {
                  if (is_add_y) {
                    pos = (w + rorate_90_offset) * size_w;
                  } else {
                    pos = -(w + rorate_90_offset) * size_w;
                  }
                } else {
                  if (!is_add_x) {
                    pos = offset_x_bak - w - offset_x;
                  } else {
                    pos = w + offset_x;
                  }
                }
                diff_pos = dst + pos - text_draw_buffer;
                if (diff_pos < size_buf && diff_pos >= 0) {
                  dst[pos] = src[w];
                  if (draw_fmt == AMBA_DRAW_FORMAT_8BIT_CLUT) {
                    if (font->disable_anti_alias) {
                      if (src[w] == DEFAULT_PIXEL_BACKGROUND) {
                        dst[pos] = text->m_bitmap.background_color_index;
                      } else if (src[w] == DEFAULT_PIXEL_FONT) {
                        dst[pos] = text->m_bitmap.color_index;
                      }
                    } else {
                      if (src[w] == DEFAULT_PIXEL_BACKGROUND) {
                        dst[pos] = text->m_bitmap.background_color_index;//text->color_index;
                      } else if (src[w] == DEFAULT_PIXEL_FONT) {
                        dst[pos] = text->m_bitmap.color_index;
                      } else {
                        dst[pos] = text->m_bitmap.outline_color_index;
                      }
                    }
                  } else {
                    if (src[w] == DEFAULT_PIXEL_FONT) {
                      dst[pos] = AMBA_TEXT_CLUT_ENTRY_FONT;
                    }
                  }
                } else {
                  DPRINT_ERROR("Illegal Access: char[%c] index[%ld] diff_pos[%ld] "
                        "buf[%p] size_buf[%ld]", str[i], i, diff_pos, text_draw_buffer, size_buf);
                  is_skip_char = 1;
                  break;
                }
              }
              src += char_caches[j].width;
              if (is_rorate_90) {
                if (!is_add_x) {
                  dst -= 1;
                } else {
                  dst += 1;
                }
              } else {
                if (!is_add_y) {
                  dst -= buf_w_max;
                } else {
                  dst += buf_w_max;
                }
              }
            }
            bmp_info.width = char_caches[j].width;
            bmp_info.height = char_caches[j].height;
            break;
          } else {
            if (0 == char_caches[j].width) {
              dst = line_head + line_offset_x;
              for (int h = 0; h < char_caches[j].height; h++) {
                dst += (buf_w_max + 1) / 2;
              }
            }
          }
        }
      }
      rorate_90_offset += bmp_info.width;
      cur_line_draw_w += bmp_info.width;
      int stride = (bmp_info.width + text->spacing) >= 0
          ? (bmp_info.width + text->spacing) : 0;
      line_offset_x += stride;
      if (!is_rorate_90) {
        offset_x += stride;
      }
      if (text->spacing < 0) {
        for (int h = 0; h < font_height; ++h) {
          int offset = h * buf_w_max + line_offset_x + stride;
          memset(line_head + offset, 0, -text->spacing);
        }
      }
    }
    if (!is_cache) {
      for (int j = 0; j < AMBA_DRAW_STRING_MAX_NUM; j++) {
        if (char_caches[j].buffer) {
          free(char_caches[j].buffer);
          char_caches[j].buffer = NULL;
        }
      }
    }
  } while (0);
  return result;
}


static int convert_text_to_bmp(amba_draw_text_box_t *text,
    amba_rect_t *rect, uint8_t *text_draw_buffer,
    unsigned char stream_rotate, unsigned char m_area_rotate_mode,
    int draw_fmt)
{
  int result = 0;
  unsigned char is_need_cache = 0;
  do {
    if (!text_draw_buffer) {
      DPRINT_ERROR("Empty pointer\n");
      result = -1;
      break;
    }

    is_need_cache = is_all_included(text->str, m_char_set);

    if (text->m_string_is_from_rect &&
        (m_area_rotate_mode == AMBA_DRAW_AREA_RECT_STRING_NO_ROTATE)) {
      if ((result = convert_text_to_bmp_keep_no_rorate(
          text, rect, text_draw_buffer, is_need_cache,
          stream_rotate, /*m_area_rotate_mode,*/ draw_fmt)) != 0) {
        DPRINT_ERROR("Failed to convert text to bmp by cache\n");
        result = -1;
        break;
      }
    } else {
      if ((result = convert_text_to_bmp_with_rorate(
          text, rect, text_draw_buffer, is_need_cache,
          /*stream_rotate, m_area_rotate_mode, */draw_fmt)) != 0) {
        DPRINT_ERROR("Failed to convert text to bmp by cache\n");
        result = -1;
        break;
      }
    }
  } while (0);
  return result;
}

static void bmp_to_rgb565(amba_draw_clut_t *cluts,
    unsigned char *bitmap_buf, unsigned char *m_buffer, amba_rect_t *rect)
{
  unsigned char *bmp_buf = bitmap_buf;
  for (int i = 0; i < rect->height; ++i) {
    uint16_t *dst = (uint16_t *) m_buffer;
    for (int j = 0; j < rect->width; j++) {
      amba_draw_clut_t *tmp = &cluts[bmp_buf[0]];
      dst[j] = (uint16_t)((((uint16_t)tmp->y >> 3) << 11) |
          (((uint16_t)tmp->u >> 2) << 5) |
          ((uint16_t)tmp->v >> 3));
      bmp_buf++;
    }

    m_buffer += rect->pitch;
  }
}

static void bmp_to_uyv565(amba_draw_clut_t *cluts,
    unsigned char *bitmap_buf, unsigned char *m_buffer, amba_rect_t *rect)
{
  unsigned char *bmp_buf = bitmap_buf;
  for (int i = 0; i < rect->height; ++i) {
    uint16_t *dst = (uint16_t *) m_buffer;
    for (int j = 0; j < rect->width; j++) {
      amba_draw_clut_t *tmp = &cluts[bmp_buf[0]];
      dst[j] = (uint16_t)((((uint16_t)tmp->u >> 3) << 11) |
          (((uint16_t)tmp->y >> 2) << 5) |
          ((uint16_t)tmp->v >> 3));
      bmp_buf++;
    }

    m_buffer += rect->pitch;
  }
}

static void bmp_to_bgr565(amba_draw_clut_t *cluts,
    unsigned char *bitmap_buf, unsigned char *m_buffer, amba_rect_t *rect)
{
  unsigned char *bmp_buf = bitmap_buf;
  for (int i = 0; i < rect->height; ++i) {
    uint16_t *dst = (uint16_t *) m_buffer;
    for (int j = 0; j < rect->width; j++) {
      amba_draw_clut_t *tmp = &cluts[bmp_buf[0]];
      dst[j] = (uint16_t)((((uint16_t)tmp->v >> 3) << 11) |
          (((uint16_t)tmp->u >> 2) << 5) |
          ((uint16_t)tmp->y >> 3));
      bmp_buf++;
    }

    m_buffer += rect->pitch;
  }
}

static void bmp_to_ayuv4444(amba_draw_clut_t *cluts,
    unsigned char *bitmap_buf, unsigned char *m_buffer, amba_rect_t *rect)
{
  unsigned char *bmp_buf = bitmap_buf;
  for (int i = 0; i < rect->height; ++i) {
    uint16_t *dst = (uint16_t *) m_buffer;
    for (int j = 0; j < rect->width; j++) {
      amba_draw_clut_t *tmp = &cluts[bmp_buf[0]];
      dst[j] = (uint16_t)((((uint16_t)tmp->a >> 4) << 12) |
          (((uint16_t)tmp->y >> 4) << 8) |
          (((uint16_t)tmp->u >> 4) << 4) |
          ((uint16_t)tmp->v >> 4));
      bmp_buf++;
    }

    m_buffer += rect->pitch;
  }
}

static void bmp_to_rgba4444(amba_draw_clut_t *cluts,
    unsigned char *bitmap_buf, unsigned char *m_buffer, amba_rect_t *rect)
{
  unsigned char *bmp_buf = bitmap_buf;
  for (int i = 0; i < rect->height; ++i) {
    uint16_t *dst = (uint16_t *) m_buffer;
    for (int j = 0; j < rect->width; j++) {
      amba_draw_clut_t *tmp = &cluts[bmp_buf[0]];
      dst[j] = (uint16_t)((((uint16_t)tmp->y >> 4) << 12) |
          (((uint16_t)tmp->u >> 4) << 8) |
          (((uint16_t)tmp->v >> 4) << 4) |
          ((uint16_t)tmp->a >> 4));
      bmp_buf++;
    }

    m_buffer += rect->pitch;
  }
}

static void bmp_to_bgra4444(amba_draw_clut_t *cluts,
    unsigned char *bitmap_buf, unsigned char *m_buffer, amba_rect_t *rect)
{
  unsigned char *bmp_buf = bitmap_buf;
  for (int i = 0; i < rect->height; ++i) {
    uint16_t *dst = (uint16_t *) m_buffer;
    for (int j = 0; j < rect->width; j++) {
      amba_draw_clut_t *tmp = &cluts[bmp_buf[0]];
      dst[j] = (uint16_t)((((uint16_t)tmp->v >> 4) << 12) |
          (((uint16_t)tmp->u >> 4) << 8) |
          (((uint16_t)tmp->y >> 4) << 4) |
          ((uint16_t)tmp->a >> 4));
      bmp_buf++;
    }

    m_buffer += rect->pitch;
  }
}

static void bmp_to_abgr4444(amba_draw_clut_t *cluts,
    unsigned char *bitmap_buf, unsigned char *m_buffer, amba_rect_t *rect)
{
  unsigned char *bmp_buf = bitmap_buf;
  for (int i = 0; i < rect->height; ++i) {
    uint16_t *dst = (uint16_t *) m_buffer;
    for (int j = 0; j < rect->width; j++) {
      amba_draw_clut_t *tmp = &cluts[bmp_buf[0]];
      dst[j] = (uint16_t)((((uint16_t)tmp->a >> 4) << 12) |
          (((uint16_t)tmp->v >> 4) << 8) |
          (((uint16_t)tmp->u >> 4) << 4) |
          ((uint16_t)tmp->y >> 4));
      bmp_buf++;
    }

    m_buffer += rect->pitch;
  }
}

static void bmp_to_argb4444(amba_draw_clut_t *cluts,
    unsigned char *bitmap_buf, unsigned char *m_buffer, amba_rect_t *rect)
{
  unsigned char *bmp_buf = bitmap_buf;
  for (int i = 0; i < rect->height; ++i) {
    uint16_t *dst = (uint16_t *) m_buffer;
    for (int j = 0; j < rect->width; j++) {
      amba_draw_clut_t *tmp = &cluts[bmp_buf[0]];
      dst[j] = (uint16_t)((((uint16_t)tmp->a >> 4) << 12) |
          (((uint16_t)tmp->y >> 4) << 8) |
          (((uint16_t)tmp->u >> 4) << 4) |
          ((uint16_t)tmp->v >> 4));
      bmp_buf++;
    }

    m_buffer += rect->pitch;
  }
}

static void bmp_to_ayuv1555(amba_draw_clut_t *cluts,
    unsigned char *bitmap_buf, unsigned char *m_buffer, amba_rect_t *rect)
{
  unsigned char *bmp_buf = bitmap_buf;
  for (int i = 0; i < rect->height; ++i) {
    uint16_t *dst = (uint16_t *) m_buffer;
    for (int j = 0; j < rect->width; j++) {
      amba_draw_clut_t *tmp = &cluts[bmp_buf[0]];
      dst[j] = (uint16_t)(((uint16_t)(tmp->a != 0 ? 1 : 0) << 15) |
          (((uint16_t)tmp->y >> 3) << 10) |
          (((uint16_t)tmp->u >> 3) << 5) |
          ((uint16_t)tmp->v >> 3));
      bmp_buf++;
    }

    m_buffer += rect->pitch;
  }
}

static void bmp_to_yuv1555(amba_draw_clut_t *cluts,
    unsigned char *bitmap_buf, unsigned char *m_buffer, amba_rect_t *rect)
{
  unsigned char *bmp_buf = bitmap_buf;
  for (int i = 0; i < rect->height; ++i) {
    uint16_t *dst = (uint16_t *) m_buffer;
    for (int j = 0; j < rect->width; j++) {
      amba_draw_clut_t *tmp = &cluts[bmp_buf[0]];
      dst[j] = (uint16_t)(((uint16_t)(tmp->a != 0 ? 1 : 0) << 15) |
          (((uint16_t)tmp->y >> 3) << 10) |
          (((uint16_t)tmp->u >> 3) << 5) |
          ((uint16_t)tmp->v >> 3));
      bmp_buf++;
    }

    m_buffer += rect->pitch;
  }
}

static void bmp_to_rgba5551(amba_draw_clut_t *cluts,
    unsigned char *bitmap_buf, unsigned char *m_buffer, amba_rect_t *rect)
{
  unsigned char *bmp_buf = bitmap_buf;
  for (int i = 0; i < rect->height; ++i) {
    uint16_t *dst = (uint16_t *) m_buffer;
    for (int j = 0; j < rect->width; j++) {
      amba_draw_clut_t *tmp = &cluts[bmp_buf[0]];
      dst[j] = (uint16_t)((((uint16_t)tmp->y >> 3) << 11) |
          (((uint16_t)tmp->u >> 3) << 6) |
          (((uint16_t)tmp->v >> 3) << 1) |
          (uint16_t)(tmp->a != 0 ? 1 : 0));
      bmp_buf++;
    }

    m_buffer += rect->pitch;
  }
}

static void bmp_to_bgra5551(amba_draw_clut_t *cluts,
    unsigned char *bitmap_buf, unsigned char *m_buffer, amba_rect_t *rect)
{
  unsigned char *bmp_buf = bitmap_buf;
  for (int i = 0; i < rect->height; ++i) {
    uint16_t *dst = (uint16_t *) m_buffer;
    for (int j = 0; j < rect->width; j++) {
      amba_draw_clut_t *tmp = &cluts[bmp_buf[0]];
      dst[j] = (uint16_t)((((uint16_t)tmp->v >> 3) << 11) |
          (((uint16_t)tmp->u >> 3) << 6) |
          (((uint16_t)tmp->y >> 3) << 1) |
          (uint16_t)(tmp->a != 0 ? 1 : 0));
      bmp_buf++;
    }

    m_buffer += rect->pitch;
  }
}

static void bmp_to_abgr1555(amba_draw_clut_t *cluts,
    unsigned char *bitmap_buf, unsigned char *m_buffer, amba_rect_t *rect)
{
  unsigned char *bmp_buf = bitmap_buf;
  for (int i = 0; i < rect->height; ++i) {
    uint16_t *dst = (uint16_t *) m_buffer;
    for (int j = 0; j < rect->width; j++) {
      amba_draw_clut_t *tmp = &cluts[bmp_buf[0]];
      dst[j] = (uint16_t)(((uint16_t)(tmp->a != 0 ? 1 : 0) << 15) |
          (((uint16_t)tmp->v >> 3) << 10) |
          (((uint16_t)tmp->u >> 3) << 5) |
          ((uint16_t)tmp->y >> 3));
      bmp_buf++;
    }

    m_buffer += rect->pitch;
  }
}

static void bmp_to_argb1555(amba_draw_clut_t *cluts,
    unsigned char *bitmap_buf, unsigned char *m_buffer, amba_rect_t *rect)
{
  unsigned char *bmp_buf = bitmap_buf;
  for (int i = 0; i < rect->height; ++i) {
    uint16_t *dst = (uint16_t *) m_buffer;
    for (int j = 0; j < rect->width; j++) {
      amba_draw_clut_t *tmp = &cluts[bmp_buf[0]];
      dst[j] = (uint16_t)(((uint16_t)(tmp->a != 0 ? 1 : 0) << 15) |
          (((uint16_t)tmp->y >> 3) << 10) |
          (((uint16_t)tmp->u >> 3) << 5) |
          ((uint16_t)tmp->v >> 3));
      bmp_buf++;
    }

    m_buffer += rect->pitch;
  }
}

static void bmp_to_ayuv8888(amba_draw_clut_t *cluts,
    unsigned char *bitmap_buf, unsigned char *m_buffer, amba_rect_t *rect)
{
  unsigned char *bmp_buf = bitmap_buf;
  for (int i = 0; i < rect->height; ++i) {
    uint32_t *dst = (uint32_t *) m_buffer;
    for (int j = 0; j < rect->width; j++) {
      amba_draw_clut_t *tmp = &cluts[bmp_buf[0]];
      dst[j] = (uint32_t) ((tmp->a << 24) |
          (tmp->y << 16) |
          (tmp->u << 8) |
          tmp->v);
      bmp_buf++;
    }

    m_buffer += rect->pitch;
  }
}

static void bmp_to_rgba8888(amba_draw_clut_t *cluts,
    unsigned char *bitmap_buf, unsigned char *m_buffer, amba_rect_t *rect)
{
  unsigned char *bmp_buf = bitmap_buf;
  for (int i = 0; i < rect->height; ++i) {
    uint32_t *dst = (uint32_t *) m_buffer;
    for (int j = 0; j < rect->width; j++) {
      amba_draw_clut_t *tmp = &cluts[bmp_buf[0]];
      dst[j] = (uint32_t) ((tmp->y << 24) |
          (tmp->u << 16) |
          (tmp->v << 8) |
          tmp->a);
      bmp_buf++;
    }

    m_buffer += rect->pitch;
  }
}

static void bmp_to_bgra8888(amba_draw_clut_t *cluts,
    unsigned char *bitmap_buf, unsigned char *m_buffer, amba_rect_t *rect)
{
  unsigned char *bmp_buf = bitmap_buf;
  for (int i = 0; i < rect->height; ++i) {
    uint32_t *dst = (uint32_t *) m_buffer;
    for (int j = 0; j < rect->width; j++) {
      amba_draw_clut_t *tmp = &cluts[bmp_buf[0]];
      dst[j] = (uint32_t) ((tmp->v << 24) |
          (tmp->u << 16) |
          (tmp->y << 8) |
          tmp->a);
      bmp_buf++;
    }

    m_buffer += rect->pitch;
  }
}

static void bmp_to_abgr8888(amba_draw_clut_t *cluts,
    unsigned char *bitmap_buf, unsigned char *m_buffer, amba_rect_t *rect)
{
  unsigned char *bmp_buf = bitmap_buf;
  for (int i = 0; i < rect->height; ++i) {
    uint32_t *dst = (uint32_t *) m_buffer;
    for (int j = 0; j < rect->width; j++) {
      amba_draw_clut_t *tmp = &cluts[bmp_buf[0]];
      dst[j] = (uint32_t) ((tmp->a << 24) |
          (tmp->v << 16) |
          (tmp->u << 8) |
          tmp->y);
      bmp_buf++;
    }

    m_buffer += rect->pitch;
  }
}

static void bmp_to_argb8888(amba_draw_clut_t *cluts,
    unsigned char *bitmap_buf, unsigned char *m_buffer, amba_rect_t *rect)
{
  unsigned char *bmp_buf = bitmap_buf;
  for (int i = 0; i < rect->height; ++i) {
    uint32_t *dst = (uint32_t *) m_buffer;
    for (int j = 0; j < rect->width; j++) {
      amba_draw_clut_t *tmp = &cluts[bmp_buf[0]];
      dst[j] = (uint32_t) ((tmp->a << 24) |
          (tmp->y << 16) |
          (tmp->u << 8) |
          tmp->v);
      bmp_buf++;
    }

    m_buffer += rect->pitch;
  }
}

int amba_draw_str_data(amba_overlay_area_param_t *area_param,
    amba_draw_clut_t *cluts,
    unsigned char *area_buf,
    bitmap_buffer_t *m_bitmap,
    int draw_fmt,
    unsigned char stream_rotate)
{
  int result = 0;
  unsigned char *bitmap_buf = NULL;
  amba_draw_params_t *params = &area_param->data;
  unsigned char *m_buffer = NULL;

  do {
    if (!cluts) {
      DPRINT_ERROR("Invalid point cluts\n");
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
    if (rect->width <= 0) {
      rect->width = area_param->attr.rect.width;
    }
    if (rect->height <= 0) {
      rect->height = area_param->attr.rect.height;
    }

    amba_draw_text_box_t *text = &params->text;

    if ((result = init_text_info(text, cluts, draw_fmt)) != 0) {
      break;
    }

    size_t buf_size = rect->height * rect->width;
    amba_rect_t bitmap_rect = params->rect;
    bitmap_rect.pitch = rect->width;
    if (draw_fmt != AMBA_DRAW_FORMAT_8BIT_CLUT) {
      if (buf_size > m_bitmap->size) {
        if (m_bitmap->buf) {
          free(m_bitmap->buf);
          m_bitmap->buf = NULL;
        }
        m_bitmap->size = buf_size;
      }
      if (m_bitmap->buf == NULL) {
        m_bitmap->buf = (unsigned char *) malloc(m_bitmap->size);
        if (m_bitmap->buf == NULL) {
          DPRINT_ERROR("malloc bitmap buffer failed\n");
          result = -1;
          break;
        }
      }

      memset(m_bitmap->buf, AMBA_DRAW_CLUT_ENTRY_BACKGROUND, m_bitmap->size);
      bitmap_buf = m_bitmap->buf;
    }

    uint32_t offset = area_param->attr.rect.pitch * rect->y;
    if (draw_fmt == AMBA_DRAW_FORMAT_8BIT_CLUT) {
      offset += rect->x;
    } else if (draw_fmt >= AMBA_DRAW_FORMAT_16BIT_FIRST &&
        draw_fmt < AMBA_DRAW_FORMAT_16BIT_LAST) {
      offset += 2 * rect->x;
    } else if (draw_fmt >= AMBA_DRAW_FORMAT_32BIT_FIRST &&
        draw_fmt < AMBA_DRAW_FORMAT_32BIT_LAST) {
      offset += 4 * rect->x;
    }
    m_buffer = area_buf + offset;

    if (draw_fmt == AMBA_DRAW_FORMAT_8BIT_CLUT) {
      if ((result = convert_text_to_bmp(text, rect, m_buffer, stream_rotate, area_param->attr.rotate, draw_fmt)) != 0) {
        DPRINT_ERROR("Failed to convert text to bmp!!!");
        break;
      }

    } else if (draw_fmt >= AMBA_DRAW_FORMAT_16BIT_FIRST &&
        draw_fmt < AMBA_DRAW_FORMAT_32BIT_LAST) {
      if ((result = convert_text_to_bmp(text, &bitmap_rect, bitmap_buf, stream_rotate, area_param->attr.rotate, draw_fmt)) != 0) {
        DPRINT_ERROR("Failed to convert text to bmp!!!");
        break;
      }

      switch (draw_fmt) {
        case AMBA_DRAW_FORMAT_RGB565:
          bmp_to_rgb565(cluts, bitmap_buf, m_buffer, rect);
          break;
        case AMBA_DRAW_FORMAT_UYV565:
          bmp_to_uyv565(cluts, bitmap_buf, m_buffer, rect);
          break;
        case AMBA_DRAW_FORMAT_BGR565:
          bmp_to_bgr565(cluts, bitmap_buf, m_buffer, rect);
          break;
        case AMBA_DRAW_FORMAT_AYUV4444:
          bmp_to_ayuv4444(cluts, bitmap_buf, m_buffer, rect);
          break;
        case AMBA_DRAW_FORMAT_RGBA4444:
          bmp_to_rgba4444(cluts, bitmap_buf, m_buffer, rect);
          break;
        case AMBA_DRAW_FORMAT_BGRA4444:
          bmp_to_bgra4444(cluts, bitmap_buf, m_buffer, rect);
          break;
        case AMBA_DRAW_FORMAT_ABGR4444:
          bmp_to_abgr4444(cluts, bitmap_buf, m_buffer, rect);
          break;
        case AMBA_DRAW_FORMAT_ARGB4444:
          bmp_to_argb4444(cluts, bitmap_buf, m_buffer, rect);
          break;
        case AMBA_DRAW_FORMAT_AYUV1555:
          bmp_to_ayuv1555(cluts, bitmap_buf, m_buffer, rect);
          break;
        case AMBA_DRAW_FORMAT_YUV1555:
          bmp_to_yuv1555(cluts, bitmap_buf, m_buffer, rect);
          break;
        case AMBA_DRAW_FORMAT_RGBA5551:
          bmp_to_rgba5551(cluts, bitmap_buf, m_buffer, rect);
          break;
        case AMBA_DRAW_FORMAT_BGRA5551:
          bmp_to_bgra5551(cluts, bitmap_buf, m_buffer, rect);
          break;
        case AMBA_DRAW_FORMAT_ABGR1555:
          bmp_to_abgr1555(cluts, bitmap_buf, m_buffer, rect);
          break;
        case AMBA_DRAW_FORMAT_ARGB1555:
          bmp_to_argb1555(cluts, bitmap_buf, m_buffer, rect);
          break;
        case AMBA_DRAW_FORMAT_AYUV8888:
          bmp_to_ayuv8888(cluts, bitmap_buf, m_buffer, rect);
          break;
        case AMBA_DRAW_FORMAT_RGBA8888:
          bmp_to_rgba8888(cluts, bitmap_buf, m_buffer, rect);
          break;
        case AMBA_DRAW_FORMAT_BGRA8888:
          bmp_to_bgra8888(cluts, bitmap_buf, m_buffer, rect);
          break;
        case AMBA_DRAW_FORMAT_ABGR8888:
          bmp_to_abgr8888(cluts, bitmap_buf, m_buffer, rect);
          break;
        case AMBA_DRAW_FORMAT_ARGB8888:
          bmp_to_argb8888(cluts, bitmap_buf, m_buffer, rect);
          break;
        default:
          DPRINT_ERROR("not supported img format: %d\n", draw_fmt);
          break;
      }
    }
  } while (0);
  return result;
}
