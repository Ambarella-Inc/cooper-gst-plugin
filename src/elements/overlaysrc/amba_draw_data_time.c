/*******************************************************************************
 * am_drawing_data_time.cpp
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

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "common_err_code_c.h"
#include "internal.h"
#include "debug_log.h"

#include "amba_draw_data_time.h"
#include "amba_draw_data_string.h"


static void get_time_string(char *result_str,
    const amba_draw_time_t *otime)
{
  struct timeval t;
  if (0 == gettimeofday(&t, NULL)) {
    struct tm p;
    localtime_r(&(t.tv_sec), &p);
    char time_form[128] = "%02d:%02d:%02d";
    char form[256] = {'\0'};
    int hours = p.tm_hour;

    if (otime->en_msec) {
      char msec_tmp[4] = {0};
      snprintf(msec_tmp, 4, ".%02hhd", (int8_t) (t.tv_usec / 10000));
      strncat(time_form, msec_tmp, 4);
    }
    if (otime->is_12h) {
      if (hours < 12) {
        strncat(time_form, " am", 4);
        if (hours == 0) {
          hours = 12;
        }
      } else {
        strncat(time_form, " pm", 4);
        if (hours > 12) {
          hours -= 12;
        }
      }
    }
    strncat(time_form, " %s", 4);

    switch (otime->format) {
      case 3:
        strncpy(form, "%s%04d/%02d/%02d ", 128);
        strncat(form, time_form, 128);
        snprintf(result_str, AMBA_DRAW_STRING_MAX_NUM, form,
            otime->pre_str,
            (1900 + p.tm_year),
            (1 + p.tm_mon),
            p.tm_mday,
            hours,
            p.tm_min,
            p.tm_sec,
            otime->suf_str);
       break;
      case 2:
        strncpy(form, "%s%02d/%02d/%04d ", 128);
        strncat(form, time_form, 128);
        snprintf(result_str, AMBA_DRAW_STRING_MAX_NUM, form,
            otime->pre_str,
            p.tm_mday,
            (1 + p.tm_mon),
            (1900 + p.tm_year),
            hours,
            p.tm_min,
            p.tm_sec,
            otime->suf_str);
        break;
      case 1:
        strncpy(form, "%s%02d/%02d/%04d ", 128);
        strncat(form, time_form, 128);
        snprintf(result_str, AMBA_DRAW_STRING_MAX_NUM, form,
            otime->pre_str,
            (1 + p.tm_mon),
            p.tm_mday,
            (1900 + p.tm_year),
            hours,
            p.tm_min,
            p.tm_sec,
            otime->suf_str);
        break;
      case 0:
      default://all other value set to 0
        strncpy(form, "%s%04d-%02d-%02d ", 128);
        strncat(form, time_form, 128);
        snprintf(result_str, AMBA_DRAW_STRING_MAX_NUM, form,
            otime->pre_str,
            (1900 + p.tm_year),
            (1 + p.tm_mon),
            p.tm_mday,
            hours,
            p.tm_min,
            p.tm_sec,
            otime->suf_str);
       break;
    }

  }
}

int amba_draw_time_data(amba_overlay_area_param_t *area_params,
    amba_draw_clut_t *cluts,
    unsigned char *area_buf,
    bitmap_buffer_t *m_bitmap,
    int draw_fmt,
    unsigned char stream_rotate)
{
  int result = 0;
  do {
    amba_draw_params_t *params = &area_params->data;
    amba_draw_time_t *otime = &params->time;
    char time_str[AMBA_DRAW_STRING_MAX_NUM] = {'\0'};
    get_time_string(time_str, otime);
    /* Write into time.text only; drawdatagen filled otime->text (font, colors). Do not use
     * params->text here — that is the STRING overlay slot and would overwrite font with zeros. */
    strncpy(otime->text.str, time_str, AMBA_DRAW_STRING_MAX_NUM);
    otime->text.str[AMBA_DRAW_STRING_MAX_NUM - 1] = '\0';
    params->text = otime->text;

    result = amba_draw_str_data(area_params, cluts, area_buf, m_bitmap, draw_fmt, stream_rotate);
    if (result != 0) {
      DPRINT_ERROR("Failed to create time drawing!\n");
      break;
    }
  } while (0);
  return result;
}

