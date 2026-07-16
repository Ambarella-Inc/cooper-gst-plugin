/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2005 Wim Taymans <wim@fluendo.com>
 * Copyright (C) 2009      David Schleef <ds@schleef.org>
 *
 * gst1394clock.h: Clock for use by the IEEE 1394
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

#ifndef __GST_AMBA_HW_CLOCK_H__
#define __GST_AMBA_HW_CLOCK_H__

#include <gst/gst.h>
#include <gst/gstsystemclock.h>


G_BEGIN_DECLS

#define GST_TYPE_AMBA_HW_CLOCK (gst_amba_hw_clock_get_type())
G_DECLARE_FINAL_TYPE (GstAmbaHwClock, gst_amba_hw_clock, GST, AMBA_HW_CLOCK,
    GstSystemClock)
#define GST_AMBA_HW_CLOCK_CAST(obj) ((GstAmbaHwClock*)(obj))

typedef GstClockTime (*GstAmbaHwClockGetTimeFunc) (GstClock *clock, gpointer user_data);

/**
 * GstAmbaHwClock:
 * @clock: parent #GstSystemClock
 *
 * Opaque #GstAmbaHwClock.
 */
struct _GstAmbaHwClock {
  GstSystemClock clock;

  int fd;

  /*< private >*/
  GstClockTime     last_time;
  GstClockTimeDiff time_offset;
  GstClockTime     outfreq;
};

/**
 * gst_amba_hw_clock_obtain:
 *
 * Obtain the shared #GstAmbaHwClock instance (singleton pattern).
 * If the instance doesn't exist, it will be created.
 * The caller should call gst_object_unref() when done.
 *
 * Returns: (transfer full): the shared #GstAmbaHwClock with increased refcount
 */
GstAmbaHwClock* gst_amba_hw_clock_obtain (void);

/**
 * gst_amba_hw_clock_new:
 * @name: the name of the clock (ignored, kept for compatibility)
 *
 * Deprecated: Use gst_amba_hw_clock_obtain() instead.
 * This function now returns the shared singleton instance.
 *
 * Returns: (transfer full): the shared #GstAmbaHwClock with increased refcount
 */
GstAmbaHwClock* gst_amba_hw_clock_new (const gchar *name);

void gst_amba_hw_clock_reset (GstAmbaHwClock *clock, GstClockTime time);

GstClockTime gst_amba_hw_clock_get_time (GstAmbaHwClock * clock);

GstClockTime gst_amba_hw_clock_get_audio_aligned_time (GstAmbaHwClock * clock, guint sample_rate);

GstClockTime gst_amba_hw_clock_adjust (GstAmbaHwClock * clock, GstClockTime time);

GstClockTime amba_hwtimer_get_raw_time (GstAmbaHwClock * clock);

GstClockTime gst_amba_hwtimer_get_outfreq (void);

G_END_DECLS

#endif /* __GST_AMBA_HW_CLOCK_H__ */
