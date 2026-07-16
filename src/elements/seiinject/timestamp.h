/*
 * timestamp.h
 *
 * Timestamp payload for SEI: format and fill from GstBuffer PTS or clock.
 *
 * Copyright (C) 2025 Ambarella International LP
 */

#ifndef __TIMESTAMP_H__
#define __TIMESTAMP_H__

#include <gst/gst.h>
#include <stddef.h>

G_BEGIN_DECLS

/* Maximum bytes for timestamp payload (e.g. 8 bytes for uint64 ns, or formatted string). */
#define TIMESTAMP_PAYLOAD_MAX 32

/**
 * timestamp_fill_payload:
 * @element: GstElement (e.g. amba_seiinject) for clock when PTS invalid
 * @buffer: GstBuffer (may have PTS)
 * @out: output buffer for payload
 * @out_max: size of @out
 *
 * Fills @out with timestamp payload. Uses buffer PTS if valid, else element's clock.
 * Format: 8 bytes, uint64 little-endian, nanoseconds.
 * Returns number of bytes written (8), or 0 on error.
 */
size_t timestamp_fill_payload (GstElement *element, GstBuffer *buffer,
    guint8 *out, size_t out_max);

G_END_DECLS

#endif /* __TIMESTAMP_H__ */
