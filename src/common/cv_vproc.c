/*
 * cv_vproc.c
 *
 * History:
 *  2021/10/14 - [Xiaopan Zhan] create file
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>

#include "pthread.h"

#include "common_err_code_c.h"
#include "debug_log.h"


#include "internal.h"
#include "vproc.h"
#include "cavalry_mem.h"
#include "nnctrl.h"

#include "cv_vproc.h"
#include "element_common.h"

// singleton
static cv_vproc_ctx_t gs_cv_vproc_ctx;
static int gs_cv_vproc_ctx_is_setup = 0;

// use mutex to protect
static pthread_mutex_t cv_vproc_ctx_mutex;

static int __init_cv_vproc(cv_vproc_ctx_t *thiz)
{
    struct vproc_version ver;
    struct nnctrl_version nnctrl_ver;
    int rval = COM_ECODE_OK;

    if ((thiz->fd_cavalry = open(CAVALRY_DEV_NODE, O_RDWR, 0)) < 0) {
        perror(CAVALRY_DEV_NODE);
        return COM_ECODE_BAD_PARAMS;
    }

    if (cavalry_mem_init(thiz->fd_cavalry, 0) < 0) {
        DPRINT_ERROR("cavalry_mem_init err\n");
        return COM_ECODE_BAD_PARAMS;
    }
    thiz->fd_cavalry_opened = 1;

    if (vproc_get_version(&ver) < 0) {
        DPRINT_ERROR("vproc get version err");
        return COM_ECODE_BAD_PARAMS;
    }
    DPRINT_INFO ("%s: %u.%u.%u, mod-time: 0x%x\n",
        ver.description, ver.major, ver.minor, ver.patch, ver.mod_time);
#ifdef SDK_VER_LESS_THAN_030011
    if (vproc_init("/usr/local/vproc/vproc.bin", (uint32_t *) (&thiz->lib_mem.size)) < 0) {
        DPRINT_ERROR("vproc_init err\n");
        return COM_ECODE_OPEN_VP_FAILED;
    }
#else
    if (vproc_init("/usr/share/ambarella/vproc/vproc.bin", (uint32_t *) (&thiz->lib_mem.size)) < 0) {
        DPRINT_ERROR("vproc_init err\n");
        return COM_ECODE_OPEN_VP_FAILED;
    }
#endif
    if (!thiz->use_fd) {
       if (cavalry_mem_alloc(&thiz->lib_mem.size, &thiz->lib_mem.phys, &thiz->lib_mem.virt, 0) < 0) {
          DPRINT_ERROR("cavalry_mem_alloc err\n");
          return COM_ECODE_NO_MEMORY;
       }
    } else {
      if (cavalry_mem_alloc_mfd(thiz->lib_mem.size, &thiz->lib_mem.mfd, &thiz->lib_mem.virt, 0) < 0) {
          DPRINT_ERROR("cavalry_mem_alloc_mfd err\n");
          return COM_ECODE_NO_MEMORY;
      }
    }
    if (!thiz->use_fd) {
       rval = vproc_load(thiz->fd_cavalry, thiz->lib_mem.virt, thiz->lib_mem.phys, thiz->lib_mem.size);
       if (rval < 0) {
          cavalry_mem_free(thiz->lib_mem.size, thiz->lib_mem.phys, thiz->lib_mem.virt);
          thiz->lib_mem.virt = NULL;
          vproc_exit();
          DPRINT_ERROR("vproc_load failed, return %d\n", rval);
          return COM_ECODE_OPEN_VP_FAILED;
       }
	} else {
      rval = vproc_load_mfd(thiz->fd_cavalry, thiz->lib_mem.virt, thiz->lib_mem.mfd, thiz->lib_mem.size);
      if (rval < 0) {
         cavalry_mem_free_mfd(thiz->lib_mem.size, thiz->lib_mem.mfd, thiz->lib_mem.virt);
         thiz->lib_mem.virt = NULL;
         vproc_exit();
         DPRINT_ERROR("vproc_load failed, return %d\n", rval);
         return COM_ECODE_OPEN_VP_FAILED;
      }
    }

    thiz->vproc_initized = 1;

    rval = nnctrl_init(thiz->fd_cavalry, 0);
    if (rval < 0) {
        DPRINT_ERROR("nnctrl_init err\n");
        return COM_ECODE_BAD_PARAMS;
    }

    memset(&nnctrl_ver, 0, sizeof(nnctrl_ver));
    nnctrl_get_version(&nnctrl_ver);
    DPRINT_INFO ("%s: %u.%u.%u, mod-time: 0x%x\n",
        nnctrl_ver.description, nnctrl_ver.major, nnctrl_ver.minor, nnctrl_ver.patch, nnctrl_ver.mod_time);
    thiz->nnctrl_initized = 1;

    return COM_ECODE_OK;
}

static void __deinit_cv_vproc(cv_vproc_ctx_t *thiz)
{
    if (thiz->nnctrl_initized) {
        nnctrl_exit();
        thiz->nnctrl_initized = 0;
    }
    if (thiz->vproc_initized) {
        vproc_exit();
        thiz->vproc_initized = 0;
    }
    if (thiz->fd_cavalry_opened) {
        cavalry_mem_exit();
        close(thiz->fd_cavalry);
        thiz->fd_cavalry = -1;
    }

    return;
}

void cleanup_cv_vproc_ctx ()
{
    cv_vproc_ctx_t * thiz = &gs_cv_vproc_ctx;

    pthread_mutex_lock (&cv_vproc_ctx_mutex);

    if (!gs_cv_vproc_ctx_is_setup) {
        DPRINT_ERROR ("cv vproc ctx is not setup\n");
        pthread_mutex_unlock (&cv_vproc_ctx_mutex);
        return;
    }

    if (thiz->used_num) {
        DPRINT_ERROR ("thiz->used_num (%d) is not zero, someone still hold cv_vproc_ctx?\n",
            thiz->used_num);
    }

    if ((thiz->fd_cavalry > 0) && thiz->fd_cavalry_opened) {
        __deinit_cv_vproc(thiz);
    } else {
        DPRINT_ERROR ("cv_vproc fd is not opened or not valid (fd %d), (is opened %d)\n",
            thiz->fd_cavalry, thiz->fd_cavalry_opened);
    }

    gs_cv_vproc_ctx_is_setup = 0;

    pthread_mutex_unlock (&cv_vproc_ctx_mutex);

    return;
}


int setup_cv_vproc_ctx ()
{
    int ret = COM_ECODE_OK;
    cv_vproc_ctx_t * thiz = &gs_cv_vproc_ctx;

    pthread_mutex_lock (&cv_vproc_ctx_mutex);

    if (gs_cv_vproc_ctx_is_setup) {
        DPRINT_ERROR("already setup\n");
        pthread_mutex_unlock (&cv_vproc_ctx_mutex);
        return COM_ECODE_NOT_NEEDED_SKIP;
    }
    memset(thiz, 0x0, sizeof(cv_vproc_ctx_t));
    ret = __init_cv_vproc (thiz);

    // failed, cleanup
    if (COM_ECODE_OK != ret) {
        __deinit_cv_vproc(thiz);
    } else {
        gs_cv_vproc_ctx_is_setup = 1;
    }

    pthread_mutex_unlock (&cv_vproc_ctx_mutex);

    return ret;
}

cv_vproc_ctx_t * acquire_cv_vproc_ctx (int auto_setup, unsigned char use_fd)
{
    cv_vproc_ctx_t * p_ret_ctx = (cv_vproc_ctx_t *) 0;
    int ret = 0;

    pthread_mutex_lock (&cv_vproc_ctx_mutex);

    if (gs_cv_vproc_ctx_is_setup) {
        p_ret_ctx = &gs_cv_vproc_ctx;
        gs_cv_vproc_ctx.used_num ++;
    } else {
        if (auto_setup) {
            cv_vproc_ctx_t * thiz = &gs_cv_vproc_ctx;
            memset(thiz, 0x0, sizeof(cv_vproc_ctx_t));
            thiz->use_fd = use_fd;
            ret = __init_cv_vproc(thiz);
            // failed, cleanup
            if (COM_ECODE_OK != ret) {
                DPRINT_ERROR("auto setup failed, ret %d, 0x%08x\n", ret, ret);
                __deinit_cv_vproc(thiz);
            } else {
                p_ret_ctx = &gs_cv_vproc_ctx;
                gs_cv_vproc_ctx_is_setup = 1;
                gs_cv_vproc_ctx.used_num = 1;
            }
        } else {
            DPRINT_ERROR("cv_vproc_ctx not setup\n");
        }
    }

    pthread_mutex_unlock (&cv_vproc_ctx_mutex);
    return p_ret_ctx;
}

void release_cv_vproc_ctx (int auto_cleanup)
{
    pthread_mutex_lock (&cv_vproc_ctx_mutex);

    if (gs_cv_vproc_ctx_is_setup) {
        if (gs_cv_vproc_ctx.used_num) {
            gs_cv_vproc_ctx.used_num --;
        } else {
            if (auto_cleanup) {
                __deinit_cv_vproc(&gs_cv_vproc_ctx);
                gs_cv_vproc_ctx_is_setup = 0;
            } else {
                DPRINT_NOTICE("used num is already 0\n");
            }
        }
    } else {
        DPRINT_ERROR("cv_vproc_ctx not setup\n");
    }

    pthread_mutex_unlock (&cv_vproc_ctx_mutex);
    return;
}
