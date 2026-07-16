/*
 * clock.c
 *
 * History:
 *    6/17/2022 - [Zhi He] created file
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

#include "stdio.h"
#include "stdlib.h"
#include "string.h"

#include "common_err_code_c.h"

#include "clock_if.h"

#include "internal.h"
#include "debug_log.h"

//#define D_PRINT_CLOCK_DEBUG

extern int create_amba_hwtimer (clock_ctx_t * thiz);

clock_ctx_t * create_clock (char * clock_name,
  amgst_time_t src_freq, amgst_time_t out_freq)
{
  int ret;
  clock_ctx_t * thiz;

  if ((!src_freq) || (!out_freq)) {
    DPRINT_ERROR("bad params\n");
    return NULL;
  }

  thiz = (clock_ctx_t *) malloc(sizeof(clock_ctx_t));
  if (!thiz) {
    DPRINT_ERROR("no memory\n");
    return NULL;
  }
  memset (thiz, 0x0, sizeof(clock_ctx_t));

  thiz->src_freq = src_freq;
  thiz->out_freq = out_freq;

  if (clock_name) {
    ret = create_amba_hwtimer(thiz);
    if (0 > ret) {
      DPRINT_ERROR("create create_amba_hwtimer failed\n");
      free(thiz);
      return NULL;
    }
  }

  return thiz;
}

void destroy_clock (clock_ctx_t * thiz)
{
  if (thiz) {
    if (thiz->f_destroy && thiz->p_priv) {
      thiz->f_destroy(thiz->p_priv);
      thiz->p_priv = NULL;
      thiz->f_destroy = NULL;
    }
    free(thiz);
  }
}

int start_clock(clock_ctx_t * thiz,
  amgst_time_t base_out_time)
{
  if (!thiz) {
    DPRINT_ERROR("null thiz\n");
    return COM_ECODE_BAD_PARAMS;
  }

  thiz->base_src_time = thiz->f_query_cur_time(thiz);

  thiz->base_out_time = base_out_time;

  return COM_ECODE_OK;
}

amgst_time_t get_cur_clock(clock_ctx_t * thiz)
{
  amgst_time_t cur_src_time;
  amgst_time_t cur_out_time;
  amgst_time_diff_t src_diff;
  amgst_time_diff_t out_diff;

  if (!thiz) {
    DPRINT_ERROR("null thiz\n");
    return COM_ECODE_BAD_PARAMS;
  }

  if ((!thiz->src_freq)
    || (!thiz->out_freq)
    || (!thiz->f_query_cur_time)) {
    DPRINT_ERROR("bad state\n");
    return COM_ECODE_BAD_STATE;
  }

  // get cur time
  cur_src_time = thiz->f_query_cur_time(thiz);

  src_diff = cur_src_time - thiz->base_src_time;

  out_diff = src_diff *
    (amgst_time_diff_t) thiz->out_freq / (amgst_time_diff_t) thiz->src_freq;

  cur_out_time = thiz->base_out_time + (amgst_time_t) out_diff;

  return cur_out_time;
}

int start_clock_dummy(clock_ctx_t * thiz,
  amgst_time_t base_out_time,
  amgst_time_t base_src_time)
{
  if (!thiz) {
    DPRINT_ERROR("null thiz\n");
    return COM_ECODE_BAD_PARAMS;
  }

  thiz->base_src_time = base_src_time;
  thiz->base_out_time = base_out_time;

  return COM_ECODE_OK;
}

amgst_time_diff_t get_cur_clock_dummy(clock_ctx_t * thiz,
  amgst_time_t cur_src_time)
{
  amgst_time_diff_t cur_out_time = -1;
  amgst_time_t src_diff;
  amgst_time_t out_diff;

  if (!thiz) {
    DPRINT_ERROR("null thiz\n");
    return -1;
  }

  if ((!thiz->src_freq)
    || (!thiz->out_freq)) {
    DPRINT_ERROR("bad state\n");
    return -1;
  }

  // Handle time wrap-around: when cur_src_time wraps from max to 0
  if (cur_src_time < thiz->base_src_time) {
      DPRINT_ERROR("cur src time(%lu) < base src time(%lu), not supoort for wrap-around case!\n",
          cur_src_time, thiz->base_src_time);
      return -1;
  }

  src_diff = cur_src_time - thiz->base_src_time;

#ifdef D_PRINT_CLOCK_DEBUG
  if (src_diff > (amgst_time_t)G_MAXINT64) {
    DPRINT_ERROR("src diff(%lu) > G_MAXINT64, overflow would occur\n", diff);
    return -1;
  }
#endif

  out_diff = src_diff * (thiz->out_freq / thiz->src_freq);

#ifdef D_PRINT_CLOCK_DEBUG
  // Check for overflow in the result
  if (out_diff > (amgst_time_t)G_MAXINT64) {
    DPRINT_ERROR("out diff(%lu) > G_MAXINT64, overflow would occur\n", out_diff);
    return -1;
  }
#endif

  cur_out_time = (amgst_time_diff_t) (thiz->base_out_time) + (amgst_time_diff_t) out_diff;

  return cur_out_time;
}

int start_clock_v2(clock_ctx_t * thiz,
  amgst_time_t base_out_time)
{
  if (!thiz) {
    DPRINT_ERROR("null thiz\n");
    return COM_ECODE_BAD_PARAMS;
  }

  thiz->base_src_time = (amgst_time_t) (base_out_time * (thiz->src_freq / (double) thiz->out_freq));

  thiz->base_out_time = 0;//base_out_time

  return COM_ECODE_OK;
}

int check_negative_pts(clock_ctx_t * thiz,
  amgst_time_t cur_src_time)
{
  if (!thiz) {
    DPRINT_ERROR("null thiz\n");
    return COM_ECODE_BAD_PARAMS;
  }

  if (cur_src_time < thiz->base_src_time) {
      return -1;
  }

  return 0;
}

