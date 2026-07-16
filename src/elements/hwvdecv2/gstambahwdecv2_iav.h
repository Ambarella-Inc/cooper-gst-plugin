/*
 * gstambahwvdecv2_iav.h
 *
 * History:
 *    4/6/2026 - [Dashun Pei] created file
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


#ifndef __GST_AMBA_HWVDECV2_IAV_H__
#define __GST_AMBA_HWVDECV2_IAV_H__

#include <gst/gst.h>
#include <gst/video/gstvideodecoder.h>

G_BEGIN_DECLS

typedef struct _GstAmbaHwvdecV2 GstAmbaHwvdecV2;

gboolean gst_ambahwvdecv2_iav_init_ctx (GstAmbaHwvdecV2 * self);
void gst_ambahwvdecv2_iav_release_ctx (GstAmbaHwvdecV2 * self);

gboolean gst_ambahwvdecv2_iav_prepare (GstAmbaHwvdecV2 * self,
    GstVideoCodecState * state);

void gst_ambahwvdecv2_iav_shutdown_decoder (GstAmbaHwvdecV2 * self);

gboolean gst_ambahwvdecv2_iav_flush_decoder (GstAmbaHwvdecV2 * self);

/**
 * Runs H.264 BSB feed + canvas query + GDMA (IAV_PART_DSP -> Cavalry dmabuf).
 * If @produced is TRUE, caller should add GstVideoMeta and push @out_buf.
 */
GstFlowReturn gst_ambahwvdecv2_iav_fill_output (GstAmbaHwvdecV2 * self,
    GstVideoDecoder * decoder, GstVideoCodecFrame * frame, GstBuffer * in_buf,
    GstBuffer * out_buf, gsize nv12_buffer_size,
    guint (*assign_slot) (GstBuffer *, gpointer), gpointer slot_user,
    guint32 * frame_id_seq, guint * out_width, guint * out_height, guint * out_pitch,
    guint * out_slot, gboolean * produced);

G_END_DECLS

#endif /* __GST_AMBA_HWVDECV2_IAV_H__ */
