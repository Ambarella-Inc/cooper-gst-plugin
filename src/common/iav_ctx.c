/*
 * iav_ctx.c
 *
 * History:
 *    5/25/2022 - [Zhi He] created file
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

#include "stdlib.h"
#include "stdio.h"
#include "string.h"

#include "pthread.h"

#include "common_err_code_c.h"

#include "internal.h"

#include "debug_log.h"

#include "iav_al.h"

#include "iav_ctx.h"

// singleton
static iav_ctx_t gs_iav_ctx;
static int gs_iav_ctx_is_setup = 0;

// use mutex to protect
/* Static init required: first user is acquire_iav_ctx() from decoder start (PAUSED). */
static pthread_mutex_t iav_ctx_mutex = PTHREAD_MUTEX_INITIALIZER;

static void cleanup_iav_ctx_internal(iav_ctx_t * thiz, iav_al_t * al)
{
  int ret;

  // unmap dsp
  if (al->f_unmap_dsp && thiz->dsp_mapped) {
    ret = al->f_unmap_dsp (thiz->iav_fd, &thiz->map_dsp);
    thiz->dsp_mapped = 0;
    if (ret) {
      DPRINT_ERROR("f_unmap_dsp failed, ret %d\n", ret);
    }
  }

  // unmap bsb
  if (al->f_unmap_bsb && thiz->bsb_mapped) {
    ret = al->f_unmap_bsb (thiz->iav_fd, &thiz->map_bsb);
    thiz->bsb_mapped = 0;
    if (ret) {
      DPRINT_ERROR ("f_unmap_bsb failed, ret %d\n", ret);
    }
  }

  // unmap overlay
  if (al->f_unmap_overlay && thiz->overlay_mapped) {
    ret = al->f_unmap_overlay (thiz->iav_fd, &thiz->map_overlay);
    thiz->overlay_mapped = 0;
    if (ret) {
      DPRINT_ERROR ("f_unmap_overlay failed, ret %d\n", ret);
    }
  }

   // close fd
   if (thiz->iav_fd_opened) {
     close_iav_handle (thiz->iav_fd);
     thiz->iav_fd = 0;
     thiz->iav_fd_opened = 0;
   }

   return;
}

void cleanup_iav_ctx ()
{
  iav_ctx_t * thiz = &gs_iav_ctx;
  iav_al_t * al = &thiz->iav_al;

  pthread_mutex_lock (&iav_ctx_mutex);

  if (!gs_iav_ctx_is_setup) {
    DPRINT_ERROR ("iav ctx is not setup\n");
    pthread_mutex_unlock (&iav_ctx_mutex);
    return;
  }

  if (thiz->used_num) {
    DPRINT_ERROR ("thiz->used_num (%d) is not zero, someone still hold iav_ctx?\n",
      thiz->used_num);
  }

  if ((thiz->iav_fd > 0) && thiz->iav_fd_opened) {
    cleanup_iav_ctx_internal (thiz, al);
  } else {
    DPRINT_ERROR ("iav fd is not opened or not valid (fd %d), (is opened %d)\n",
      thiz->iav_fd, thiz->iav_fd_opened);
  }

  gs_iav_ctx_is_setup = 0;

  pthread_mutex_unlock (&iav_ctx_mutex);

  return;
}

static int setup_iav_ctx_internal (iav_ctx_t * thiz, iav_al_t * al)
{
  int ret = COM_ECODE_OK;

  do {
    memset(thiz, 0x0, sizeof(iav_ctx_t));

    initialize_iav_al(&thiz->iav_al);

    thiz->iav_fd = open_iav_handle ();
    if (0 > thiz->iav_fd) {
      DPRINT_ERROR("iav open failed\n");
      ret = COM_ECODE_OPEN_IAV_FAILED;
      break;
    }
    thiz->iav_fd_opened = 1;

    thiz->map_bsb.b_two_times = 1;
    thiz->map_bsb.b_enable_write = 1;//0;to do
    thiz->map_bsb.b_enable_read = 1;
    ret = al->f_map_bsb(thiz->iav_fd, &thiz->map_bsb);
    if (0 > ret) {
      DPRINT_ERROR("f_map_bsb failed, ret %d\n", ret);
      ret = COM_ECODE_MEM_MAP_FAILED;
      break;
    }
    thiz->bsb_mapped = 1;

    ret = al->f_map_dsp(thiz->iav_fd, &thiz->map_dsp);
    if (0 > ret) {
      DPRINT_ERROR("f_map_dsp failed, ret %d\n", ret);
      ret = COM_ECODE_MEM_MAP_FAILED;
      break;
    }
    thiz->dsp_mapped = 1;

    ret = al->f_map_overlay(thiz->iav_fd, &thiz->map_overlay);
    if (0 > ret) {
      DPRINT_ERROR("f_map_overlay failed, ret %d\n", ret);
      ret = COM_ECODE_MEM_MAP_FAILED;
      break;
    }
    thiz->overlay_mapped = 1;

    thiz->map_dec_bsb.b_two_times = 1;
    thiz->map_dec_bsb.b_enable_write = 1;//0;to do
    thiz->map_dec_bsb.b_enable_read = 1;
    ret = al->f_map_dec_bsb(thiz->iav_fd, &thiz->map_dec_bsb);
    if (0 > ret) {
      DPRINT_ERROR("f_map_bsb failed, ret %d\n", ret);
      ret = COM_ECODE_MEM_MAP_FAILED;
      break;
    }
    thiz->dec_bsb_mapped = 1;

    ret = COM_ECODE_OK;
  } while (0);

  return ret;
}

int setup_iav_ctx ()
{
  int ret = COM_ECODE_OK;
  iav_ctx_t * thiz = &gs_iav_ctx;
  iav_al_t * al = &thiz->iav_al;

  pthread_mutex_lock (&iav_ctx_mutex);

  if (gs_iav_ctx_is_setup) {
    DPRINT_ERROR("already setup\n");
    pthread_mutex_unlock (&iav_ctx_mutex);
    return COM_ECODE_NOT_NEEDED_SKIP;
  }

  ret = setup_iav_ctx_internal (thiz, al);

  // failed, cleanup
  if (COM_ECODE_OK != ret) {
    cleanup_iav_ctx_internal(thiz, al);
  } else {
    gs_iav_ctx_is_setup = 1;
  }

  pthread_mutex_unlock (&iav_ctx_mutex);

  return ret;
}

iav_ctx_t * acquire_iav_ctx (int auto_setup)
{
  iav_ctx_t * p_ret_ctx = (iav_ctx_t *) 0;
  int ret = 0;

  pthread_mutex_lock (&iav_ctx_mutex);

  if (gs_iav_ctx_is_setup) {
    p_ret_ctx = &gs_iav_ctx;
    gs_iav_ctx.used_num ++;
  } else {
    if (auto_setup) {
      iav_ctx_t * thiz = &gs_iav_ctx;
      iav_al_t * al = &thiz->iav_al;
      ret = setup_iav_ctx_internal(thiz, al);
      // failed, cleanup
      if (COM_ECODE_OK != ret) {
        DPRINT_ERROR("auto setup failed, ret %d, 0x%08x\n", ret, ret);
        cleanup_iav_ctx_internal(thiz, al);
      } else {
        p_ret_ctx = &gs_iav_ctx;
        gs_iav_ctx_is_setup = 1;
        gs_iav_ctx.used_num = 1;
      }
    } else {
      DPRINT_ERROR("iav_ctx not setup\n");
    }
  }

  pthread_mutex_unlock (&iav_ctx_mutex);
  return p_ret_ctx;
}

void release_iav_ctx (int auto_cleanup)
{
  pthread_mutex_lock (&iav_ctx_mutex);

  if (gs_iav_ctx_is_setup) {
    if (gs_iav_ctx.used_num) {
      gs_iav_ctx.used_num --;
    } else {
      if (auto_cleanup) {
        iav_ctx_t * thiz = &gs_iav_ctx;
        iav_al_t * al = &thiz->iav_al;
        cleanup_iav_ctx_internal(thiz, al);
        gs_iav_ctx_is_setup = 0;
      } else {
        DPRINT_NOTICE("used num is already 0\n");
      }
    }
  } else {
    DPRINT_ERROR("iav_ctx not setup\n");
  }

  pthread_mutex_unlock (&iav_ctx_mutex);
  return;
}

