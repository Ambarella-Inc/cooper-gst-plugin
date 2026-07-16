/*
 * sei.h
 *
 * SEI (Supplemental Enhancement Information) handling for H.264 and H.265.
 * Builds user_data_unregistered SEI with UUID and custom payload, and
 * injects into bitstream before the first VCL NAL.
 *
 * Copyright (C) 2025 Ambarella International LP
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __SEI_H__
#define __SEI_H__

#include <gst/gst.h>
#include <stdint.h>
#include <stddef.h>

G_BEGIN_DECLS

/* UUIDs for custom SEI (16 bytes each). Different UUIDs for timestamp vs GPS. */
#define SEI_UUID_TIMESTAMP_LEN 16
#define SEI_UUID_GPS_LEN       16

extern const guint8 sei_uuid_timestamp[SEI_UUID_TIMESTAMP_LEN];
extern const guint8 sei_uuid_gps[SEI_UUID_GPS_LEN];

/**
 * sei_build_user_data_unregistered:
 * @buf: output buffer (must have enough space)
 * @uuid: 16-byte UUID
 * @payload: custom payload data
 * @payload_len: length of payload
 *
 * Builds one user_data_unregistered SEI payload (type 5) into @buf.
 * Returns number of bytes written, or 0 on error.
 */
size_t sei_build_user_data_unregistered (guint8 *buf, size_t buf_size,
    const guint8 *uuid, const guint8 *payload, size_t payload_len);

/**
 * sei_inject_into_h264:
 * Returns new GstBuffer with SEI injected, or same buffer ref, or NULL.
 */
GstBuffer *sei_inject_into_h264 (GstBuffer *buffer,
    const guint8 *timestamp_payload, size_t timestamp_len,
    const guint8 *gps_payload, size_t gps_len);

/**
 * sei_inject_into_h265:
 * Same for H.265/HEVC byte stream.
 */
GstBuffer *sei_inject_into_h265 (GstBuffer *buffer,
    const guint8 *timestamp_payload, size_t timestamp_len,
    const guint8 *gps_payload, size_t gps_len);

/**
 * sei_inject_into_buffer:
 * @buffer: GstBuffer (H.264 or H.265 byte stream)
 * @is_h265: TRUE for H.265, FALSE for H.264
 * @timestamp_payload: timestamp SEI payload (can be NULL)
 * @timestamp_len: length
 * @gps_payload: GPS SEI payload (can be NULL)
 * @gps_len: length
 *
 * Returns a new GstBuffer with SEI injected (caller must unref).
 * If nothing to inject, returns the same buffer with ref count increased.
 * On error returns NULL (caller keeps original buffer).
 */
GstBuffer *sei_inject_into_buffer (GstBuffer *buffer, gboolean is_h265,
    const guint8 *timestamp_payload, size_t timestamp_len,
    const guint8 *gps_payload, size_t gps_len);

G_END_DECLS

#endif /* __SEI_H__ */
