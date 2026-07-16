/*
 * amba_private_data.c
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

#include "amba_private_data.h"

#ifndef DUNUSED
#define DUNUSED(x) (void)(x)
#endif

/* Meta initialization function */
static gboolean
amba_private_data_meta_init(GstMeta *meta, gpointer params, GstBuffer *buffer)
{
    DUNUSED(params);
    DUNUSED(buffer);
    AmbaPrivateDataMeta *priv_meta = (AmbaPrivateDataMeta *)meta;
    priv_meta->dsp_pts = 0;
    priv_meta->mono_pts = 0;
    priv_meta->yuv_seq_num = 0;
    priv_meta->private_data_count = 0;
    memset(priv_meta->private_data, 0, sizeof(priv_meta->private_data));
    return TRUE;
}

/* Meta transform function */
static gboolean
amba_private_data_meta_transform(GstBuffer *transbuf, GstMeta *meta,
                               GstBuffer *buffer, GQuark type, gpointer data)
{
    DUNUSED(buffer);
    DUNUSED(type);
    DUNUSED(data);
    AmbaPrivateDataMeta *priv_meta = (AmbaPrivateDataMeta *)meta;
    AmbaPrivateDataMeta *new_meta = (AmbaPrivateDataMeta *)gst_buffer_add_meta(transbuf,
        AMBA_PRIVATE_DATA_META_INFO, NULL);

    if (!new_meta)
        return FALSE;
    if (priv_meta->private_data_count > AMBA_PRIVATE_DATA_MAX_ENTRIES) {
        GST_ERROR("Invalid private_data_count: %u, max is %d",
                 priv_meta->private_data_count, AMBA_PRIVATE_DATA_MAX_ENTRIES);
        return FALSE;
    }

    new_meta->dsp_pts = priv_meta->dsp_pts;
    new_meta->mono_pts = priv_meta->mono_pts;
    new_meta->yuv_seq_num = priv_meta->yuv_seq_num;
    new_meta->private_data_count = priv_meta->private_data_count;
    memcpy(new_meta->private_data, priv_meta->private_data,
           sizeof(priv_meta->private_data));
    return TRUE;
}

/* Copy AmbaPrivateDataMeta from src buffer to dst buffer */
AmbaPrivateDataMeta *
amba_buffer_copy_private_data_meta(GstBuffer *dst, GstBuffer *src)
{
    AmbaPrivateDataMeta *src_meta = amba_buffer_get_private_data_meta(src);
    if (!src_meta)
        return NULL;
    AmbaPrivateDataMeta *dst_meta = amba_buffer_add_private_data_meta(dst);
    if (!dst_meta)
        return NULL;
    dst_meta->dsp_pts = src_meta->dsp_pts;
    dst_meta->mono_pts = src_meta->mono_pts;
    dst_meta->yuv_seq_num = src_meta->yuv_seq_num;
    dst_meta->private_data_count = src_meta->private_data_count;
    if (src_meta->private_data_count > 0 && src_meta->private_data_count <= AMBA_PRIVATE_DATA_MAX_ENTRIES)
        memcpy(dst_meta->private_data, src_meta->private_data,
               sizeof(src_meta->private_data[0]) * src_meta->private_data_count);
    return dst_meta;
}

/* Meta API type registration */
GType
amba_private_data_meta_api_get_type(void)
{
    /* cppcheck-suppress threadsafety */
    static GType type = 0;
    if (g_once_init_enter(&type)) {
        // Check if type already exists
        GType existing_type = g_type_from_name("AmbaPrivateDataMetaAPI");
        if (existing_type != 0) {
            g_once_init_leave(&type, existing_type);
        } else {
            // Register new type
            /* cppcheck-suppress threadsafety */
            static const gchar *tags[] = { NULL };
            GType _type = gst_meta_api_type_register("AmbaPrivateDataMetaAPI", tags);
            g_once_init_leave(&type, _type);
        }
    }
    return type;
}

/* Meta info registration */
const GstMetaInfo *
amba_private_data_meta_get_info(void)
{
    /* cppcheck-suppress threadsafety */
    static const GstMetaInfo *meta_info = NULL;

    if (g_once_init_enter(&meta_info)) {
        const GstMetaInfo *mi = gst_meta_register(AMBA_PRIVATE_DATA_META_API_TYPE,
            "AmbaPrivateDataMeta",
            sizeof(AmbaPrivateDataMeta),
            amba_private_data_meta_init,
            NULL,
            amba_private_data_meta_transform);
        g_once_init_leave(&meta_info, mi);
    }
    return meta_info;
}

/* Format detection utilities */
gboolean
amba_is_private_format(const GstCaps *caps)
{
    if (!caps)
        return FALSE;

    GstStructure *str = gst_caps_get_structure(caps, 0);
    if (!str)
        return FALSE;

    const gchar *media_type = gst_structure_get_string(str, "media-type");
    if (!media_type)
        media_type = gst_structure_get_name(str);
    return g_strcmp0(media_type, "video/x-amba-private") == 0;
}

/* Get private format from caps */
AmbaPrivateFormat
amba_get_private_format_from_caps(const GstCaps *caps)
{
    if (!amba_is_private_format(caps))
        return AMBA_PRIVATE_FORMAT_NONE;

    const gchar *format_str = gst_structure_get_string(gst_caps_get_structure(caps, 0), "format");
    if (!format_str)
        return AMBA_PRIVATE_FORMAT_NONE;

    // Direct string comparison for efficiency
    if (g_strcmp0(format_str, "ME0") == 0)
        return AMBA_PRIVATE_FORMAT_ME0;
    else if (g_strcmp0(format_str, "ME1") == 0)
        return AMBA_PRIVATE_FORMAT_ME1;
    else if (g_strcmp0(format_str, "CE") == 0)
        return AMBA_PRIVATE_FORMAT_CE;
    else
        return AMBA_PRIVATE_FORMAT_NONE;
}

/* Format conversion utilities */
const gchar *
amba_private_format_to_string(AmbaPrivateFormat format)
{
    switch (format) {
        case AMBA_PRIVATE_FORMAT_ME0:
            return "ME0";
        case AMBA_PRIVATE_FORMAT_ME1:
            return "ME1";
        case AMBA_PRIVATE_FORMAT_CE:
            return "CE";
        case AMBA_PRIVATE_FORMAT_NONE:
        default:
            return "UNKNOWN";
    }
}

/* Private format size calculation helper */
static guint
amba_private_data_calculate_size(AmbaPrivateFormat format, const GstCaps *caps)
{
    if (!caps || format == AMBA_PRIVATE_FORMAT_NONE)
        return 0;

    GstStructure *str = gst_caps_get_structure(caps, 0);
    if (!str)
        return 0;

    gint width, height;
    if (!gst_structure_get_int(str, "width", &width) ||
        !gst_structure_get_int(str, "height", &height) ||
        width <= 0 || height <= 0) {
        return 0;
    }
    guint base_size = width * height;

    switch (format) {
        case AMBA_PRIVATE_FORMAT_ME0:
        case AMBA_PRIVATE_FORMAT_ME1:
        case AMBA_PRIVATE_FORMAT_CE:
            return base_size;
        case AMBA_PRIVATE_FORMAT_NONE:
        default:
            return 0;
    }
}

/* Get video size from caps */
guint
amba_get_data_size_from_caps(const GstCaps *caps)
{
    if (!caps)
        return 0;

    // Check if it's a private format
    if (amba_is_private_format(caps)) {
        AmbaPrivateFormat format = amba_get_private_format_from_caps(caps);
        return amba_private_data_calculate_size(format, caps);
    }

    // For standard video formats, use GStreamer's video API
    GstVideoInfo info;
    if (gst_video_info_from_caps(&info, caps)) {
        return GST_VIDEO_INFO_SIZE(&info);
    }

    return 0;
}

/* Private format caps creation utilities */

GstCaps *
amba_create_me0_caps(gint width, gint height)
{
    return gst_caps_new_simple("video/x-amba-private",
                              "format", G_TYPE_STRING, "ME0",
                              "width", G_TYPE_INT, width,
                              "height", G_TYPE_INT, height,
                              NULL);
}

GstCaps *
amba_create_me1_caps(gint width, gint height)
{
    return gst_caps_new_simple("video/x-amba-private",
                              "format", G_TYPE_STRING, "ME1",
                              "width", G_TYPE_INT, width,
                              "height", G_TYPE_INT, height,
                              NULL);
}

GstCaps *
amba_create_ce_caps(gint width, gint height)
{
    return gst_caps_new_simple("video/x-amba-private",
                              "format", G_TYPE_STRING, "CE",
                              "width", G_TYPE_INT, width,
                              "height", G_TYPE_INT, height,
                              NULL);
}

GstCaps *
amba_create_me0_caps_from_video(gint width, gint height)
{
    return gst_caps_new_simple("video/x-amba-private",
                              "format", G_TYPE_STRING, "ME0",
                              "width", G_TYPE_INT, GST_ROUND_UP_N(width, 16)>>3,
                              "height", G_TYPE_INT, GST_ROUND_UP_N(height, 16)>>3,
                              NULL);
}

GstCaps *
amba_create_me1_caps_from_video(gint width, gint height)
{
    return gst_caps_new_simple("video/x-amba-private",
                              "format", G_TYPE_STRING, "ME1",
                              "width", G_TYPE_INT, GST_ROUND_UP_N(width, 8)>>2,
                              "height", G_TYPE_INT, GST_ROUND_UP_N(height, 8)>>2,
                              NULL);
}

GstCaps *
amba_create_ce_caps_from_video(gint width, gint height)
{
    return gst_caps_new_simple("video/x-amba-private",
                              "format", G_TYPE_STRING, "CE",
                              "width", G_TYPE_INT, GST_ROUND_UP_N(width, 8)>>2,
                              "height", G_TYPE_INT, height,
                              NULL);
}
