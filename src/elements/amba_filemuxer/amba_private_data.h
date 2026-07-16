/*
 * amba_private_data.h
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

#ifndef __AMBA_PRIVATE_DATA_H__
#define __AMBA_PRIVATE_DATA_H__

#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

/**
 * @brief Maximum number of private data entries supported
 */
#define AMBA_PRIVATE_DATA_MAX_ENTRIES 3

/**
 * @brief Private data format enumeration
 */
typedef enum {
    AMBA_PRIVATE_FORMAT_NONE = 0,
    AMBA_PRIVATE_FORMAT_ME0,
    AMBA_PRIVATE_FORMAT_ME1,
    AMBA_PRIVATE_FORMAT_CE,
    AMBA_PRIVATE_FORMAT_MAX
} AmbaPrivateFormat;

/**
 * @brief Private data entry structure
 */
typedef struct {
    guint index;         /* Memory index for multi-memory mode */
    guint offset;
    guint size;
    guint width;
    guint height;
    guint pitch;
    AmbaPrivateFormat format;
} AmbaPrivateDataEntry;

/**
 * @brief Custom private data metadata structure
 */
typedef struct {
    GstMeta meta;
    guint dsp_pts;
    guint64 mono_pts;
    /*for pipeline delay comparison. */
    guint yuv_seq_num;
    guint private_data_count;
    AmbaPrivateDataEntry private_data[AMBA_PRIVATE_DATA_MAX_ENTRIES];
} AmbaPrivateDataMeta;

/* Meta API functions */
GType amba_private_data_meta_api_get_type(void);
const GstMetaInfo *amba_private_data_meta_get_info(void);

/* Meta manipulation macros */
#define AMBA_PRIVATE_DATA_META_API_TYPE (amba_private_data_meta_api_get_type())
#define AMBA_PRIVATE_DATA_META_INFO (amba_private_data_meta_get_info())
#define amba_buffer_add_private_data_meta(b) \
    ((AmbaPrivateDataMeta*)gst_buffer_add_meta((b), AMBA_PRIVATE_DATA_META_INFO, NULL))
#define amba_buffer_get_private_data_meta(b) \
    ((AmbaPrivateDataMeta*)gst_buffer_get_meta((b), AMBA_PRIVATE_DATA_META_API_TYPE))

/* Copy AmbaPrivateDataMeta from src buffer to dst buffer. Returns new meta on dst or NULL. */
AmbaPrivateDataMeta *amba_buffer_copy_private_data_meta(GstBuffer *dst, GstBuffer *src);

/* Private format caps creation utilities */
// Create private format caps with width and height directly
GstCaps *amba_create_me0_caps(gint width, gint height);
GstCaps *amba_create_me1_caps(gint width, gint height);
GstCaps *amba_create_ce_caps(gint width, gint height);
// Create private format caps with width and height from video caps
GstCaps *amba_create_me0_caps_from_video(gint width, gint height);
GstCaps *amba_create_me1_caps_from_video(gint width, gint height);
GstCaps *amba_create_ce_caps_from_video(gint width, gint height);

/* Format detection utilities */
gboolean amba_is_private_format(const GstCaps *caps);
AmbaPrivateFormat amba_get_private_format_from_caps(const GstCaps *caps);

/* Format conversion utilities */
const gchar *amba_private_format_to_string(AmbaPrivateFormat format);

/* Get data size from caps */
guint amba_get_data_size_from_caps(const GstCaps *caps);

G_END_DECLS

#endif /* __AMBA_PRIVATE_DATA_H__ */