/*
 * timestamp.c
 *
 * Copyright (C) 2025 Ambarella International LP
 */

#include "timestamp.h"
#include <string.h>

/* We use 8-byte uint64 little-endian: nanoseconds. */
#define TIMESTAMP_PAYLOAD_BYTES 8

size_t
timestamp_fill_payload (GstElement *element, GstBuffer *buffer,
    guint8 *out, size_t out_max)
{
  GstClockTime pts;
  guint64 ns;

  if (!out || out_max < TIMESTAMP_PAYLOAD_BYTES)
    return 0;

  pts = GST_BUFFER_PTS (buffer);
  if (!GST_CLOCK_TIME_IS_VALID (pts))
    pts = GST_BUFFER_DTS (buffer);
  if (!GST_CLOCK_TIME_IS_VALID (pts) && element) {
    GstClock *clock = gst_element_get_clock (element);
    if (clock) {
      GstClockTime now = gst_clock_get_time (clock);
      GstClockTime base = gst_element_get_base_time (element);
      if (now > base)
        pts = now - base;
      gst_object_unref (clock);
    }
  }
  if (!GST_CLOCK_TIME_IS_VALID (pts))
    ns = 0;
  else
    ns = (guint64) pts;

  /* Little-endian uint64 */
  out[0] = (guint8) (ns);
  out[1] = (guint8) (ns >> 8);
  out[2] = (guint8) (ns >> 16);
  out[3] = (guint8) (ns >> 24);
  out[4] = (guint8) (ns >> 32);
  out[5] = (guint8) (ns >> 40);
  out[6] = (guint8) (ns >> 48);
  out[7] = (guint8) (ns >> 56);
  return TIMESTAMP_PAYLOAD_BYTES;
}
