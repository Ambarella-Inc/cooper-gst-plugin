/*
 * am_sei_log.c
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

#include "am_sei_log.h"

#include <stdio.h>
#include <string.h>

typedef struct {
  AmSeiLogLevel level;
  AmSeiLogSink sink;
  void *sink_user_data;
} AmSeiLogState;

static AmSeiLogState g_am_sei_log_state = {
  .level = AM_SEI_LOG_WARN,
  .sink = NULL,
  .sink_user_data = NULL,
};

static const char *
level_to_string (AmSeiLogLevel level)
{
  switch (level) {
    case AM_SEI_LOG_ERROR: return "ERROR";
    case AM_SEI_LOG_WARN: return "WARN";
    case AM_SEI_LOG_INFO: return "INFO";
    case AM_SEI_LOG_DEBUG: return "DEBUG";
    case AM_SEI_LOG_TRACE: return "TRACE";
    default: return "UNKNOWN";
  }
}

static void
default_sink (void *user_data, AmSeiLogLevel level, const char *file, int line,
    const char *func, const char *message)
{
  (void) user_data;
  fprintf (stderr, "[am_sei][%s] %s:%d %s: %s\n",
      level_to_string (level), file ? file : "?", line, func ? func : "?", message ? message : "");
}

void
am_sei_log_set_level (AmSeiLogLevel level)
{
  if (level < AM_SEI_LOG_OFF || level > AM_SEI_LOG_TRACE) {
    AmSeiLogLevel bad = level;
    level = AM_SEI_LOG_WARN;
    am_sei_log_message (AM_SEI_LOG_WARN, __FILE__, __LINE__, __func__,
        "invalid log level %d, fallback to %d",
        (int) bad, (int) level);
  }
  g_am_sei_log_state.level = level;
  am_sei_log_message (AM_SEI_LOG_INFO, __FILE__, __LINE__, __func__,
      "log level updated to %d", (int) level);
}

AmSeiLogLevel
am_sei_log_get_level (void)
{
  return g_am_sei_log_state.level;
}

void
am_sei_log_set_sink (AmSeiLogSink sink, void *user_data)
{
  g_am_sei_log_state.sink = sink;
  g_am_sei_log_state.sink_user_data = user_data;
}

void
am_sei_log_vmessage (AmSeiLogLevel level, const char *file, int line,
    const char *func, const char *fmt, va_list ap)
{
  char msg[512];
  AmSeiLogState snapshot;

  if (!fmt)
    return;

  snapshot = g_am_sei_log_state;

  if (snapshot.level == AM_SEI_LOG_OFF || level > snapshot.level)
    return;

  vsnprintf (msg, sizeof (msg), fmt, ap);
  msg[sizeof (msg) - 1] = '\0';

  if (snapshot.sink) {
    snapshot.sink (snapshot.sink_user_data, level, file, line, func, msg);
  } else {
    default_sink (NULL, level, file, line, func, msg);
  }
}

void
am_sei_log_message (AmSeiLogLevel level, const char *file, int line,
    const char *func, const char *fmt, ...)
{
  va_list ap;
  va_start (ap, fmt);
  am_sei_log_vmessage (level, file, line, func, fmt, ap);
  va_end (ap);
}
