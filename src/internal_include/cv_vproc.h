/*
 * cv_proc.h
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

#ifndef __CV_VPROC_H__
#define __CV_VPROC_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void *virt;
    unsigned long phys;
    unsigned long size;
    int mfd;
} cv_mem_t;

typedef struct {
    int fd_cavalry;

    unsigned int fd_cavalry_opened : 1;
    unsigned int vproc_initized : 1;
    unsigned int nnctrl_initized : 1;
    unsigned int use_fd : 1;
    unsigned int reserved : 28;

    unsigned int used_num; // reference counter

    cv_mem_t lib_mem;

} cv_vproc_ctx_t;

void cleanup_cv_vproc_ctx ();
int setup_cv_vproc_ctx ();
cv_vproc_ctx_t * acquire_cv_vproc_ctx (int auto_setup, unsigned char use_fd);
void release_cv_vproc_ctx (int auto_cleanup);


#ifdef __cplusplus
}
#endif

#endif


