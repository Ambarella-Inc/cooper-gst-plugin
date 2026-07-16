/*
 * am_sei_log.h
 *
 * History:
 *    04/09/2026 - [Yang Yu] created file
 *
 * Copyright (C) 2026 Ambarella International LP
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

#ifndef __AM_SEI_LOG_H__
#define __AM_SEI_LOG_H__

#include <stdarg.h>
#include "am_sei.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*AmSeiLogSink) (void *user_data, AmSeiLogLevel level,
    const char *file, int line, const char *func, const char *message);

void am_sei_log_set_level (AmSeiLogLevel level);
AmSeiLogLevel am_sei_log_get_level (void);
void am_sei_log_set_sink (AmSeiLogSink sink, void *user_data);

void am_sei_log_message (AmSeiLogLevel level, const char *file, int line,
    const char *func, const char *fmt, ...);
void am_sei_log_vmessage (AmSeiLogLevel level, const char *file, int line,
    const char *func, const char *fmt, va_list ap);

#define AM_SEI_LOG(level, fmt, ...) \
  am_sei_log_message ((level), __FILE__, __LINE__, __func__, (fmt), ##__VA_ARGS__)

#define AM_SEI_LOGE(fmt, ...) AM_SEI_LOG (AM_SEI_LOG_ERROR, (fmt), ##__VA_ARGS__)
#define AM_SEI_LOGW(fmt, ...) AM_SEI_LOG (AM_SEI_LOG_WARN, (fmt), ##__VA_ARGS__)
#define AM_SEI_LOGI(fmt, ...) AM_SEI_LOG (AM_SEI_LOG_INFO, (fmt), ##__VA_ARGS__)
#define AM_SEI_LOGD(fmt, ...) AM_SEI_LOG (AM_SEI_LOG_DEBUG, (fmt), ##__VA_ARGS__)
#define AM_SEI_LOGT(fmt, ...) AM_SEI_LOG (AM_SEI_LOG_TRACE, (fmt), ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif  /* __AM_SEI_LOG_H__ */
