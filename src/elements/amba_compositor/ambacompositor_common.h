/*
 * ambacompositor_common.h
 *
 * History:
 *    3/17/2026 - [Da-Shun Pei] created file
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

/**
 * SECTION: element-ambacompositor
 * @title: ambacompositor_common
 *
 * Shared frame info for ambacompositor element.
 * Upstream elements (e.g. decoder) attach GstambacompositorFrameInfoMeta to buffers.
 */

#ifndef __ambacompositor_COMMON_H__
#define __ambacompositor_COMMON_H__

#include <gst/gst.h>

G_BEGIN_DECLS

/**
 * GstambacompositorFrameInfo:
 * Frame info passed from sink to ambacompositor proc().
 */
typedef struct {
  gint decID;
  guint frameID;
  guint width;
  guint height;
  guint pitch;
  guint format;          /* e.g. GST_VIDEO_FORMAT_* or custom */
  gulong phyaddr_y;
  gulong phyaddr_uv;
  guint64 playback_pts;
  guint64 system_time;
} GstambacompositorFrameInfo;

/**
 * GstambacompositorFrameInfoMeta:
 * GstMeta carrying frame info for ambacompositor input.
 */
typedef struct {
  GstMeta meta;
  GstambacompositorFrameInfo info;
} GstambacompositorFrameInfoMeta;

GType gst_ambacompositor_frame_info_meta_api_get_type(void);
const GstMetaInfo *gst_ambacompositor_frame_info_meta_get_info(void);

#define GST_ambacompositor_FRAME_INFO_META_API_TYPE (gst_ambacompositor_frame_info_meta_api_get_type())
#define GST_ambacompositor_FRAME_INFO_META_INFO (gst_ambacompositor_frame_info_meta_get_info())

#define gst_buffer_get_ambacompositor_frame_info_meta(b) \
  ((GstambacompositorFrameInfoMeta *)gst_buffer_get_meta((b), GST_ambacompositor_FRAME_INFO_META_API_TYPE))

#define gst_buffer_add_ambacompositor_frame_info_meta(b, info) \
  ((GstambacompositorFrameInfoMeta *)gst_buffer_add_meta((b), GST_ambacompositor_FRAME_INFO_META_INFO, (info)))

G_END_DECLS

#endif /* __ambacompositor_COMMON_H__ */
