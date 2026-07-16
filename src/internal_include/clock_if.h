/*
 * clock_if.h
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

#ifndef __CLOCK_IF_H__
#define __CLOCK_IF_H__
#include <glib.h>

typedef guint64 amgst_time_t;
typedef gint64 amgst_time_diff_t;

typedef struct clock_ctx_s clock_ctx_t;

typedef amgst_time_t (* clock_query_cur_time_func_t) (clock_ctx_t * ctx);
typedef void (* clock_destroy_func_t) (clock_ctx_t * ctx);

struct clock_ctx_s {
  amgst_time_t src_freq;
  amgst_time_t out_freq;

  amgst_time_t base_src_time;

  amgst_time_t base_out_time;

  // for derived
  clock_query_cur_time_func_t f_query_cur_time;
  clock_destroy_func_t f_destroy;

  int fd;
  void * p_priv;
};

clock_ctx_t * create_clock (char * clock_name,
  amgst_time_t src_freq, amgst_time_t out_freq);
void destroy_clock (clock_ctx_t * thiz);

int start_clock(clock_ctx_t * thiz,
  amgst_time_t base_out_time);

amgst_time_t get_cur_clock(clock_ctx_t * thiz);

int start_clock_dummy(clock_ctx_t * thiz,
  amgst_time_t base_out_time,
  amgst_time_t base_src_time);

amgst_time_diff_t get_cur_clock_dummy(clock_ctx_t * thiz,
  amgst_time_t cur_src_time);

int start_clock_v2(clock_ctx_t * thiz,
  amgst_time_t base_out_time);

int check_negative_pts(clock_ctx_t * thiz,
  amgst_time_t cur_src_time);

#endif

