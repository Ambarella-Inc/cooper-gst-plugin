/*
 * platform_os.c
 *
 * History:
 *    4/21/2022 - [Zhi He] created file
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

#if defined (D_OS_LINUX)

#include "common_err_code_c.h"

#include "debug_log.h"
#include "platform_al.h"

#include "pthread.h"
#include <semaphore.h>

int pal_mutex_init(pal_mutex_t *mutex, void *attr)
{
    return pthread_mutex_init(mutex, (const pthread_mutexattr_t*) attr);
}

int pal_mutex_lock(pal_mutex_t *mutex)
{
    return pthread_mutex_lock(mutex);
}

int pal_mutex_unlock(pal_mutex_t *mutex)
{
    return pthread_mutex_unlock(mutex);
}

int pal_mutex_destroy(pal_mutex_t *mutex)
{
    return pthread_mutex_destroy(mutex);
}

int pal_cond_init(pal_cond_t *cond, void *attr)
{
    return pthread_cond_init(cond, (const pthread_condattr_t*) attr);
}

int pal_cond_signal(pal_cond_t *cond)
{
    return pthread_cond_signal(cond);
}

int pal_cond_wait(pal_cond_t *cond, pal_mutex_t *mutex)
{
    return pthread_cond_wait(cond, mutex);
}

int pal_cond_destroy(pal_cond_t *cond)
{
    return pthread_cond_destroy(cond);
}

int pal_thread_create(pal_thread_t *thread_ctx,
    void * thread_attr,
    thread_func func, void *context,
    const char *name)
{
    pthread_create(thread_ctx, (const pthread_attr_t*) thread_attr, func, context);
    return 0;
}

int pal_thread_join(unsigned long thread_ctx,
    const char *name)
{
    void *pp;
    pthread_join(thread_ctx, &pp);
    return 0;
}

int pal_thread_cancel(unsigned long thread_ctx)
{
    pthread_cancel(thread_ctx);
    return 0;
}

int pal_init_sem(pal_sem_t *g_sem)
{
    int ret = 0;
    if (sem_init(g_sem, 0, 0)) {
        perror("sem_init");
        ret = -1;
    }

    return ret;
}

int pal_deinit_sem(pal_sem_t *g_sem)
{
    int ret = 0;
    if (sem_destroy(g_sem)) {
        perror("sem_close");
        ret = -1;
    }

    return ret;
}

int pal_signal_sem(pal_sem_t *g_sem, int cnt)
{
    int ret = 0;
    int i;
    for (i = 0; i < cnt; ++i) {
        if (sem_post(g_sem)) {
            perror("[NL]: sem_post");
            ret = -1;
        }
    }

    return ret;
}

int pal_sem_wait(pal_sem_t *g_sem)
{
    int ret = 0;

    sem_wait(g_sem);

    return ret;
}

#elif defined (D_OS_AMRTOS)

#include "common_err_code_c.h"
#include "amba_dsf_if.h"

#include "internal_log.h"
#include "platform_al.h"

int pal_mutex_init(pal_mutex_t *mutex, void *attr)
{
    return pthread_mutex_init(mutex, attr);
}

int pal_mutex_lock(pal_mutex_t *mutex)
{
    return pthread_mutex_lock(mutex);
}

int pal_mutex_unlock(pal_mutex_t *mutex)
{
    return pthread_mutex_unlock(mutex);
}

int pal_mutex_destroy(pal_mutex_t *mutex)
{
    return pthread_mutex_destroy(mutex);
}

int pal_cond_init(pal_cond_t *cond, void *attr)
{
    return pthread_cond_init(cond, attr);
}

int pal_cond_signal(pal_cond_t *cond)
{
    return pthread_cond_signal(cond);
}

int pal_cond_wait(pal_cond_t *cond, pal_mutex_t *mutex)
{
    return pthread_cond_wait(cond, mutex);
}

int pal_cond_destroy(pal_cond_t *cond)
{
    return pthread_cond_destroy(cond);
}

int pal_thread_create(pal_thread_t *thread_ctx,
    void * thread_attr,
    thread_func func, void *context,
    const char *name)
{
    // resume task
    return rtos_thread_create(thread_ctx, func, context);
}

int pal_thread_join(unsigned long thread_ctx,
    const char *name)
{
    // pasue task
    rtos_thread_join(thread_ctx);
    return 0;
}

int pal_thread_cancel(unsigned long thread_ctx)
{
    printf("Canceling thread is not supported!\n");
    return -1;
}

int pal_init_sem(pal_sem_t *g_sem)
{
    sem_init(g_sem);
    return 0;
}

int pal_deinit_sem(pal_sem_t *g_sem)
{
    return 0;
}

int pal_signal_sem(pal_sem_t *g_sem, int cnt)
{
    int i = 0;
    for (i = 0; i < cnt; ++i) {
        sem_put(g_sem);
    }
    return 0;
}

int pal_sem_wait(pal_sem_t *g_sem)
{
    int ret = 0;

    sem_get(g_sem);

    return ret;
}

char *strdup(const char *s)
{
    char *ret = NULL;
    int len = strlen(s);

    if (len >= 0) {
        ret = PAL_MALLOC(len + 1);
        if (ret) {
            PAL_MEMCPY(ret, s, len + 1);
        }
    }

    return ret;
}
#endif

