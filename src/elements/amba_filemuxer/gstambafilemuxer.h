/*
 * gstambafilemuxer.h
 *
 * History:
 *    08/01/2025 - [Yang Yu] created file
 *
 * Copyright (C) 2025 Ambarella International LP
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
 * SECTION: element-amba_filemuxer
 * @title: amba_filemuxer
 *
 * amba_filemuxer can be used to combine NV12 video data with private data (ME0, ME1, CE)
 * into a single buffer with custom metadata.
 *
 */

#ifndef __GST_AMBA_FILEMUXER_H__
#define __GST_AMBA_FILEMUXER_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/base/gstaggregator.h>
#include "amba_private_data.h"

G_BEGIN_DECLS

/* Maximum number of private data pads */
#define AMBA_FILEMUXER_MAX_PRIVATE_PADS AMBA_PRIVATE_DATA_MAX_ENTRIES

#define GST_TYPE_AMBA_FILEMUXER \
  (gst_amba_filemuxer_get_type())
#define GST_AMBA_FILEMUXER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AMBA_FILEMUXER,GstAmbaFileMuxer))
#define GST_AMBA_FILEMUXER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AMBA_FILEMUXER,GstAmbaFileMuxerClass))
#define GST_AMBA_FILEMUXER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_AMBA_FILEMUXER,GstAmbaFileMuxerClass))
#define GST_IS_AMBA_FILEMUXER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AMBA_FILEMUXER))
#define GST_IS_AMBA_FILEMUXER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AMBA_FILEMUXER))

typedef struct {
    GstPad *pad;
    AmbaPrivateFormat format;
    gint width;
    gint height;
    gint pitch;
    gint aligned_height;
    guint input_size;   /* Input size from filesrc, calculated from width and height */
    guint output_size;  /* Output size for downstream, calculated from pitch and aligned_height */
    GstBuffer *temp_buffer;
    gboolean has_data;
} AmbaPrivatePadInfo;

typedef struct _GstAmbaFileMuxer GstAmbaFileMuxer;
typedef struct _GstAmbaFileMuxerClass GstAmbaFileMuxerClass;

struct _GstAmbaFileMuxer {
    GstAggregator parent;

    /* Properties */
    guint private_pad_count;
    gchar *private_formats;
    gboolean packed_mode;      /* TRUE: single memory, FALSE: multi-memory */
    gboolean dma_output_mode;  /* TRUE: use DMA allocator, FALSE: use default allocator */
    guint alignment;           /* Memory alignment requirement for output buffers */
    /* Internal state */
    gboolean caps_negotiated;
    gint video_width;
    gint video_height;
    gint video_pitch;
    gint video_aligned_height;   /* Height rounded up to 16 for output buffer allocation */
    GstVideoFormat video_format; /* Current video format (NV12, NV16, etc.) */
    guint video_input_size;      /* Input size from filesrc, calculated from width and height */
    guint video_output_size;     /* Output size for downstream, calculated from pitch and aligned height */
    guint total_output_size;

    /* DMA allocator management */
    GstAllocator *cavalry_allocator;

    /* Pad management */
    GstPad *video_sink_pad;
    AmbaPrivatePadInfo private_pads[AMBA_FILEMUXER_MAX_PRIVATE_PADS];

    /* Aggregation state */
    guint active_private_count;
    gboolean has_private_data;
};

struct _GstAmbaFileMuxerClass {
    GstAggregatorClass parent_class;
};

GType gst_amba_filemuxer_get_type (void);

G_END_DECLS

#endif /* __GST_AMBA_FILEMUXER_H__ */