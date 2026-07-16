/*
 * platform_al.h
 *
 * History:
 *    2020/09/28 - [Zhi He] create file
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

#ifndef __PLATFORM_AL_H__
#define __PLATFORM_AL_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long long dsf_time_t;

#ifndef ROUND_UP
#define ROUND_UP(size, align) (((size) + ((align) - 1)) & ~((align) - 1))
#endif

#ifndef ROUND_DOWN
#define ROUND_DOWN(x, n) ((x) & ~((n) - 1))
#endif


#define D_HAVE_STDIO

#define D_USE_STDLIB_MEM
#define D_USE_STDLIB_STRING

#define D_USE_MATH_LIB

#ifndef D_OS_AMRTOS
#define D_USE_UNISTED_LIB
#endif

#ifndef D_OS_AMRTOS
#define D_USE_SYS_STAT
#endif

#define D_USE_SYS_CONTROL

#define D_USE_SYS_MMAN

#define D_OS_LINUX
#ifdef D_OS_LINUX
#define D_USE_LINXU_FB
#endif

//from make file
//#define D_USE_NEON

#if defined (D_USE_STDLIB_MEM)
#include "stdlib.h"
#include "string.h"
#define PAL_MALLOC malloc
#define PAL_FREE free
#define PAL_MEMSET memset
#define PAL_MEMCPY memcpy
#else
#define PAL_MALLOC pal_malloc
#define PAL_FREE pal_free
#define PAL_MEMSET pal_memset
#define PAL_MEMCPY pal_memcpy
#endif

#if defined (D_USE_STDLIB_STRING)
#include "stdio.h"
#define PAL_STRCPY strcpy
#define PAL_STRNCPY strncpy
#define PAL_STRCMP strcmp
#define PAL_STRNCMP strncmp
#define PAL_STRLEN strlen
#define PAL_STRCHR strchr
#define PAL_STRDUP strdup
#define PAL_ATOI atoi
#define PAL_ATOF atof
#define PAL_INDEX index
#else
#define PAL_STRCPY pal_strcpy
#define PAL_STRNCPY pal_strncpy
#define PAL_STRCMP pal_strcmp
#define PAL_STRNCMP pal_strncmp
#define PAL_STRLEN pal_strlen
#define PAL_STRCHR pal_strchr
#define PAL_STRDUP pal_strdup
#define PAL_ATOI pal_atoi
#define PAL_ATOF pal_atof
#endif

#if defined (D_USE_MATH_LIB)
#ifdef D_OS_AMRTOS
#include <newlibm.h>
#else
#include "math.h"
#endif
#define PAL_ARCTAN atan
#define PAL_SQRT sqrt
#define PAL_EXP exp
#define PAL_ROUND round
#define PAL_MAX fmax
#define PAL_FLOOR floor
#define PAL_ABS abs
#define PAL_FABS fabs
#define PAL_ISNAN isnan
#define PAL_COS cos
#define PAL_SIN sin
#define PAL_POW pow
#else
#define PAL_ARCTAN pal_atan
#define PAL_SQRT pal_sqrt
#define PAL_EXP pal_exp
#define PAL_ROUND pal_round
#define PAL_MAX pal_max
#define PAL_FLOOR pal_floor
#define PAL_ABS pal_abs
#define PAL_FABS pal_fabs
#define PAL_ISNAN pal_isnan
#define PAL_COS pal_cos
#define PAL_SIN pal_sin
#define PAL_POW pal_pow
#endif

#if defined (D_USE_UNISTED_LIB)
#include <unistd.h>
#define PAL_ACCESS access
#else
#endif

#if defined (D_USE_SYS_STAT)
#include <sys/stat.h>
#define PAL_MKDIR mkdir
#else
#endif

#if defined (D_USE_SYS_STAT)
#include <fcntl.h>
#define PAL_OPEN open
#define PAL_READ read
#define PAL_WRITE write
#define PAL_SEEK lseek
#define PAL_CLOSE close
#define PAL_SEEK_SET SEEK_SET
#define PAL_SEEK_END SEEK_END
#define PAL_RDONLY O_RDONLY
#define PAL_CREATE O_CREAT
#define PAL_WRONLY O_WRONLY
#define PAL_IREAD S_IREAD
#define PAL_IWRITE S_IWRITE
#define PAL_IRWXU S_IRWXU
#define PAL_FOPEN fopen
#define PAL_FREAD fread
#define PAL_FWRITE fwrite
#define PAL_FSEEK fseek
#define PAL_FTELL ftell
#define PAL_FCLOSE fclose
#else
#endif

#if defined (D_USE_SYS_CONTROL)
#include <sys/ioctl.h>
#define PAL_IOCTL ioctl
#else
#endif

#if defined (D_USE_SYS_MMAN)
#include <sys/mman.h>
#define PAL_MMAP mmap
#define PAL_PROT_READ PROT_READ
#define PAL_PROT_WRITE PROT_WRITE
#define PAL_MAP_SHARED MAP_SHARED
#else
#endif

#if defined (D_USE_LINXU_FB)
#include <linux/fb.h>
#else
#endif

#if defined (D_USE_NEON)
#include <arm_neon.h>
#else
#endif

#if defined (D_OS_LINUX)
#include "pthread.h"
#include <semaphore.h>
#define pal_thread_t pthread_t
#define pal_mutex_t pthread_mutex_t
#define pal_cond_t pthread_cond_t
#define pal_sem_t sem_t
#elif defined (D_OS_AMRTOS) /* D_OS_AMRTOS */
#include <sys/time.h>
#include <rtos_thread.h>
#include <helper.h>
#define pal_thread_t pthread_t
#define pal_mutex_t pthread_mutex_t
#define pal_cond_t pthread_cond_t
#define pal_sem_t sem_t
extern char *strdup(const char *s);
#endif

int pal_mutex_init(pal_mutex_t *mutex, void *attr);
int pal_mutex_lock(pal_mutex_t *mutex);
int pal_mutex_unlock(pal_mutex_t *mutex);
int pal_mutex_destroy(pal_mutex_t *mutex);

int pal_cond_init(pal_cond_t *cond, void *attr);
int pal_cond_signal(pal_cond_t *cond);
int pal_cond_wait(pal_cond_t *cond, pal_mutex_t *mutex);
int pal_cond_destroy(pal_cond_t *cond);

int pal_sem_wait(pal_sem_t *g_sem);
int pal_init_sem(pal_sem_t *g_sem);
int pal_deinit_sem(pal_sem_t *g_sem);
int pal_signal_sem(pal_sem_t *g_sem, int cnt);

typedef void *(*thread_func)(void *context);

int pal_thread_create(pal_thread_t *thread_ctx,
    void * thread_attr,
    thread_func func, void *context,
    const char *name);

int pal_thread_join(unsigned long thread_ctx,
    const char *name);

int pal_thread_cancel(unsigned long thread_ctx);

// time
dsf_time_t pal_get_time_us();
dsf_time_t pal_get_time_ns();


#ifdef __cplusplus
}
#endif

#endif

