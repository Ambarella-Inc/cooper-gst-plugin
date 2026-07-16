/*
 * debug_log.h
 *
 * History:
 *    5/24/2022 - [Zhi He] created file
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

#ifndef __DEBUG_LOG_H__
#define __DEBUG_LOG_H__

#ifdef __cplusplus
extern "C" {
#endif

// use gst log system
//#define USE_GST_LOG_SYSTEM

#ifdef USE_GST_LOG_SYSTEM

#include <gst/gst.h>
#include <gst/gstinfo.h>

#define DPRINT_ERROR GST_ERROR
#define DPRINT_WARNING GST_WARNING
#define DPRINT_NOTICE GST_INFO
#define DPRINT_INFO GST_INFO
#define DPRINT_DEBUG GST_DEBUG
#define DPRINT_LOG GST_LOG
#define DPRINT_FIXME GST_FIXME
#define DPRINT_TRACE GST_TRACE
#define DPRINT_VERBOSE GST_TRACE

#else

//debug print

#include <stdio.h>

#define ON_LINUX

extern int g_log_level_agp; // for amba gst plugin

#define DLogLevel_None 0x00
#define DLogLevel_Error 0x01
#define DLogLevel_Warning 0x02
#define DLogLevel_Notice 0x03
#define DLogLevel_Info 0x04
#define DLogLevel_Debug 0x05
#define DLogLevel_Log 0x06
#define DLogLevel_Fixme 0x07
#define DLogLevel_Trace 0x08
#define DLogLevel_Verbose 0x09

void set_log_level_amba_gst_plugin(int log_level);

#ifdef ON_LINUX

#define D_LOG_PRINT_BY_LEVEL_S(level, tag, format, args...)  do { \
        if (g_log_level_agp >= level) { \
            printf("%s", tag); \
            printf(format, ##args);  \
        } \
    } while (0)

#define D_LOG_PRINT_TRACE_BY_LEVEL_S(level, tag, format, args...)  do { \
        if (g_log_level_agp >= level) { \
            printf("%s", tag); \
            printf(format, ##args);  \
            printf("            [trace] file %s.\n            function: %s: line %d\n", __FILE__, __FUNCTION__, __LINE__); \
        } \
    } while (0)

#define DPRINT_VERBOSE(format, args...)   D_LOG_PRINT_BY_LEVEL_S(DLogLevel_Verbose, "    [Verbose]: ", format, ##args)
#define DPRINT_TRACE(format, args...)   D_LOG_PRINT_BY_LEVEL_S(DLogLevel_Trace, "    [Trace]: ", format, ##args)
#define DPRINT_FIXME(format, args...)   D_LOG_PRINT_BY_LEVEL_S(DLogLevel_Fixme, "    [Fixme]: ", format, ##args)
#define DPRINT_LOG(format, args...)   D_LOG_PRINT_BY_LEVEL_S(DLogLevel_Log, "   [Log]: ", format, ##args)
#define DPRINT_DEBUG(format, args...)   D_LOG_PRINT_BY_LEVEL_S(DLogLevel_Debug, "   [Debug]: ", format, ##args)
#define DPRINT_INFO(format, args...)   D_LOG_PRINT_BY_LEVEL_S(DLogLevel_Info, "  [Info]: ", format, ##args)
#define DPRINT_NOTICE(format, args...)   D_LOG_PRINT_BY_LEVEL_S(DLogLevel_Notice, "  [Notice]: ", format, ##args)
#define DPRINT_WARNING(format, args...)   D_LOG_PRINT_TRACE_BY_LEVEL_S(DLogLevel_Warning, "[Warning]: ", format, ##args)
#define DPRINT_ERROR(format, args...)   D_LOG_PRINT_TRACE_BY_LEVEL_S(DLogLevel_Error, "[Error]: ", format, ##args)

#else

#define D_LOG_PRINT_BY_LEVEL_S(level, tag, format, ...)  do { \
        if (g_log_level_agp >= level) { \
            printf("%s", tag); \
            printf(format, __VA_ARGS__);  \
        } \
    } while (0)

#define D_LOG_PRINT_TRACE_BY_LEVEL_S(level, tag, format, ...)  do { \
        if (g_log_level_agp >= level) { \
            printf("%s", tag); \
            printf(format, __VA_ARGS__);  \
            printf("            [trace] file %s.\n            function: %s: line %d\n", __FILE__, __FUNCTION__, __LINE__); \
        } \
    } while (0)

#define DPRINT_VERBOSE(format, ...)   D_LOG_PRINT_BY_LEVEL_S(DLogLevel_Verbose, "    [Verbose]: ", format, __VA_ARGS__)
#define DPRINT_TRACE(format, ...)   D_LOG_PRINT_BY_LEVEL_S(DLogLevel_Log, "    [Trace]: ", format, __VA_ARGS__)
#define DPRINT_FIXME(format, ...)   D_LOG_PRINT_BY_LEVEL_S(DLogLevel_Log, "    [Fixme]: ", format, __VA_ARGS__)
#define DPRINT_LOG(format, ...)   D_LOG_PRINT_BY_LEVEL_S(DLogLevel_Log, "   [Log]: ", format, __VA_ARGS__)
#define DPRINT_DEBUG(format, ...)   D_LOG_PRINT_BY_LEVEL_S(DLogLevel_Debug, "   [Debug]: ", format, __VA_ARGS__)
#define DPRINT_INFO(format, ...)   D_LOG_PRINT_BY_LEVEL_S(DLogLevel_Info, "  [Info]: ", format, __VA_ARGS__)
#define DPRINT_NOTICE(format, ...)   D_LOG_PRINT_BY_LEVEL_S(DLogLevel_Notice, "  [Notice]: ", format, __VA_ARGS__)
#define DPRINT_WARNING(format, ...)   D_LOG_PRINT_TRACE_BY_LEVEL_S(DLogLevel_Warning, "[Warning]: ", format, __VA_ARGS__)
#define DPRINT_ERROR(format, ...)   D_LOG_PRINT_TRACE_BY_LEVEL_S(DLogLevel_Error, "[Error]: ", format, __VA_ARGS__)

#endif

#endif

#define DPRINT printf

void print_memory_u8(unsigned char *p, unsigned int len);
void print_memory_ull(unsigned long long *p, unsigned int len);
void print_memory_u32(unsigned int *p, unsigned int len);

#ifdef __cplusplus
}
#endif

#endif


