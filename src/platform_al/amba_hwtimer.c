/*
 * amba_hwtimer.c
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

#include "unistd.h"
#include "fcntl.h"

#include "common_err_code_c.h"

#include "clock_if.h"

#include "internal.h"
#include "debug_log.h"

amgst_time_t hwtimer_query_cur_time (clock_ctx_t * thiz)
{
  unsigned char tmp[32] = {0};
  int ret;

  ret = read(thiz->fd, tmp, sizeof(tmp));
  if (0 > ret) {
    DPRINT_ERROR("read hwtimer failed\n");
    return 0;
  }

  return strtoull((const char*) tmp, NULL, 10);
}

void hwtimer_destroy (clock_ctx_t * thiz)
{
  if (0 < thiz->fd) {
    close (thiz->fd);
    thiz->fd= 0;
  }
}

int create_amba_hwtimer (clock_ctx_t * thiz)
{
  thiz->fd = open("/proc/ambarella/ambarella_hwtimer", O_RDONLY);
  if (0 > thiz->fd) {
    DPRINT_ERROR("open hwtimer failed\n");
    return COM_ECODE_OPEN_IO_FAILED;
  }

  thiz->f_query_cur_time = hwtimer_query_cur_time;
  thiz->f_destroy = hwtimer_destroy;

  return COM_ECODE_OK;
}

