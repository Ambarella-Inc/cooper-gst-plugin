/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 * Copyright (C) 2009      David Schleef <ds@schleef.org>
 *
 * gst1394clock.c: Clock for use by IEEE 1394 plugins
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

#include "stdio.h"
#include "stdlib.h"
#include "string.h"

#include "unistd.h"
#include "fcntl.h"
#include "internal.h"

#include "gstambahwclock.h"


GST_DEBUG_CATEGORY_STATIC (gst_amba_hw_clock_debug);
#define GST_CAT_DEFAULT gst_amba_hw_clock_debug

static void gst_amba_hw_clock_class_init (GstAmbaHwClockClass * klass, gpointer class_data);
static void gst_amba_hw_clock_init (GTypeInstance * instance, gpointer g_class);

static GstClockTime gst_amba_hw_clock_get_internal_time (GstClock * clock);
static GstClockTime gst_amba_hw_clock_get_resolution (GstClock * clock);
static void gst_amba_hw_clock_finalize (GObject * object);

static GstSystemClockClass *parent_class = NULL;

/* Singleton instance and mutex for thread-safe access */
static GstAmbaHwClock *g_hw_clock_instance = NULL;
static GMutex g_hw_clock_mutex;


GType
gst_amba_hw_clock_get_type (void)
{
  static gsize clock_type_once = 0;

  if (g_once_init_enter (&clock_type_once)) {
    static const GTypeInfo clock_info = {
      sizeof (GstAmbaHwClockClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_amba_hw_clock_class_init,
      NULL,
      NULL,
      sizeof (GstAmbaHwClock),
      4,
      (GInstanceInitFunc) gst_amba_hw_clock_init,
      NULL
    };

    GType type = g_type_register_static (GST_TYPE_SYSTEM_CLOCK, "GstAmbaHwClock",
        &clock_info, 0);
    g_once_init_leave (&clock_type_once, (gsize) type);
  }
  return (GType) clock_type_once;
}

GstClockTime
gst_amba_hwtimer_get_outfreq (void)
{
  unsigned char tmp[64] = {0};
  int ret = 0;
  GstClockTime outfreq = 0;
  char *endptr;

  int fd = open("/proc/ambarella/ambarella_hwtimer_outfreq", O_RDONLY);
  if (fd < 0) {
    GST_ERROR("open ambarella_hwtimer_outfreq failed: %s\n", strerror(errno));
    return DDefaultTimeScale;
  }
  ret = read(fd, tmp, sizeof(tmp) - 1);
  if (ret <= 0) {
    GST_ERROR("read hwtimer outfreq failed, ret=%d, errno=%d (%s)\n",
      ret, errno, strerror(errno));
    close (fd);
    return DDefaultTimeScale;
  }

  tmp[ret] = '\0';

  outfreq = strtoull((const char*) tmp, &endptr, 10);
  if (endptr == (const char*) tmp) {
    GST_ERROR("invalid freq string: '%s'\n", tmp);
    close (fd);
    return DDefaultTimeScale;
  }

  close (fd);
  fd = -1;

  GST_INFO("read hwtimer outfreq: %ld", outfreq);

  return outfreq;

}

static void
gst_amba_hw_clock_class_init (GstAmbaHwClockClass * klass, gpointer class_data)
{
  DUNUSED(class_data);
  GstClockClass *gstclock_class = (GstClockClass *) klass;
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gstclock_class->get_internal_time = gst_amba_hw_clock_get_internal_time;
  gstclock_class->get_resolution = gst_amba_hw_clock_get_resolution;

  gobject_class->finalize = gst_amba_hw_clock_finalize;

  GST_DEBUG_CATEGORY_INIT (gst_amba_hw_clock_debug, "ambahwclock", 0, "ambahwclock");
}

static void
gst_amba_hw_clock_init (GTypeInstance * instance, gpointer g_class)
{
  DUNUSED(g_class);
  GstAmbaHwClock * clock = GST_AMBA_HW_CLOCK_CAST (instance);
  GST_OBJECT_FLAG_SET (clock, GST_CLOCK_FLAG_CAN_SET_MASTER);

  clock->last_time = 0;
  clock->time_offset = 0;
  clock->fd = -1;

  clock->fd = open("/proc/ambarella/ambarella_hwtimer", O_RDONLY);
  if (clock->fd < 0) {
    GST_WARNING("open hwtimer failed: %s, hardware clock will not be available\n", strerror(errno));
    clock->fd = -1;
  } else {
    GST_DEBUG("hardware timer opened successfully, fd=%d\n", clock->fd);
  }
  clock->outfreq = gst_amba_hwtimer_get_outfreq ();
}

/**
 * gst_amba_hw_clock_obtain:
 *
 * Obtain the shared #GstAmbaHwClock instance (singleton pattern).
 * If the instance doesn't exist, it will be created.
 * The caller should call gst_object_unref() when done.
 *
 * This function is thread-safe and can be called from multiple threads.
 *
 * Returns: (transfer full): the shared #GstAmbaHwClock with increased refcount
 */
GstAmbaHwClock *
gst_amba_hw_clock_obtain (void)
{
  GstAmbaHwClock *clock;

  g_mutex_lock (&g_hw_clock_mutex);

  if (g_hw_clock_instance == NULL) {
    g_hw_clock_instance =
        GST_AMBA_HW_CLOCK (g_object_new (GST_TYPE_AMBA_HW_CLOCK,
            "name", "GstAmbaHwClock",
            "clock-type", GST_CLOCK_TYPE_OTHER, NULL));

    /* Clear floating flag */
    gst_object_ref_sink (g_hw_clock_instance);

    GST_INFO ("Created shared hardware clock instance %p", g_hw_clock_instance);
  }

  /* Increase refcount for caller */
  clock = GST_AMBA_HW_CLOCK (gst_object_ref (g_hw_clock_instance));

  g_mutex_unlock (&g_hw_clock_mutex);

  return clock;
}

/**
 * gst_amba_hw_clock_new:
 * @name: the name of the clock (ignored, kept for compatibility)
 *
 * Deprecated: Use gst_amba_hw_clock_obtain() instead.
 * This function now returns the shared singleton instance.
 *
 * Returns: (transfer full): the shared #GstAmbaHwClock with increased refcount
 */
GstAmbaHwClock *
gst_amba_hw_clock_new (const gchar * name)
{
  DUNUSED(name);
  GST_WARNING ("gst_amba_hw_clock_new() is deprecated, use gst_amba_hw_clock_obtain() instead");
  return gst_amba_hw_clock_obtain ();
}

static void
gst_amba_hw_clock_finalize (GObject * object)
{
  GstAmbaHwClock *clock = GST_AMBA_HW_CLOCK (object);

  /* Clear singleton reference if this is the singleton instance */
  g_mutex_lock (&g_hw_clock_mutex);
  if (g_hw_clock_instance == clock) {
    GST_INFO ("Destroying shared hardware clock instance %p", clock);
    g_hw_clock_instance = NULL;
  }
  g_mutex_unlock (&g_hw_clock_mutex);

  if (0 < clock->fd) {
    close (clock->fd);
    clock->fd = 0;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/**
 * gst_amba_hw_clock_reset:
 * @clock: a #GstAmbaHwClock
 * @time: a #GstClockTime (unused, kept for compatibility)
 *
 * Reset the clock. This is a no-op in the simplified design.
 * Clock synchronization is handled by GStreamer's pipeline clock system.
 */
void
gst_amba_hw_clock_reset (GstAmbaHwClock * clock, GstClockTime time)
{
  DUNUSED(time);

  // Simplified design: no offset maintenance, let GStreamer handle synchronization
  clock->time_offset = 0;

  GST_DEBUG_OBJECT (clock, "clock reset (simplified design, no offset applied)");
}

/**
 * amba_hwtimer_get_raw_time:
 * @clock: a #GstAmbaHwClock
 *
 * Get raw hardware timer time in 90kHz units.
 * This function reads the hardware timer device and parses the time value.
 *
 * Returns: raw hardware time in 90kHz units, or GST_CLOCK_TIME_NONE on error
 */
GstClockTime
amba_hwtimer_get_raw_time(GstAmbaHwClock *clock)
{
  unsigned char tmp[64] = {0};
  int ret = 0;
  GstClockTime time_90k = 0;
  char *endptr;

  if (!clock) {
    GST_ERROR("clock is NULL");
    return GST_CLOCK_TIME_NONE;
  }

  if (clock->fd <= 0) {
    GST_ERROR("invalid file descriptor: %d", clock->fd);
    return GST_CLOCK_TIME_NONE;
  }

  GST_OBJECT_LOCK (clock);

  ret = read(clock->fd, tmp, sizeof(tmp) - 1);
  if (ret <= 0) {
    GST_ERROR("read hwtimer failed, ret=%d, errno=%d (%s)", ret, errno, strerror(errno));
    GST_OBJECT_UNLOCK (clock);
    return GST_CLOCK_TIME_NONE;
  }

  tmp[ret] = '\0';

  // Improved error checking - allow comma as thousands separator
  time_90k = strtoull((const char*) tmp, &endptr, 10);
  if (endptr == (const char*) tmp) {
    GST_ERROR("invalid time string: '%s'", tmp);
    GST_OBJECT_UNLOCK (clock);
    return GST_CLOCK_TIME_NONE;
  }

  GST_DEBUG("read %d bytes: '%s', converted to %" GST_TIME_FORMAT " (90kHz)",
      ret, tmp, GST_TIME_ARGS(time_90k));

  GST_OBJECT_UNLOCK (clock);
  return time_90k;
}

/**
 * amba_hwtimer_get_ns_time:
 * @clock: a #GstAmbaHwClock
 *
 * Get hardware timer time converted to nanoseconds.
 * This function handles the conversion from 90kHz hardware time to
 * GStreamer's standard nanosecond time format.
 *
 * Returns: hardware time in nanoseconds, or GST_CLOCK_TIME_NONE on error
 */
static GstClockTime
amba_hwtimer_get_ns_time(GstAmbaHwClock *clock)
{
  if (!clock) {
    GST_ERROR("clock is NULL");
    return GST_CLOCK_TIME_NONE;
  }

  GstClockTime raw_time = amba_hwtimer_get_raw_time(clock);

  if (raw_time == GST_CLOCK_TIME_NONE) {
    GST_DEBUG("failed to get raw hardware time");
    return GST_CLOCK_TIME_NONE;
  }

  // Convert 90kHz timestamp to nanoseconds with higher precision
  // Use 64-bit arithmetic to avoid precision loss
  GstClockTime ns_time = gst_util_uint64_scale(raw_time, GST_SECOND, clock->outfreq);

  GST_DEBUG("converted %" GST_TIME_FORMAT " (90kHz) to %" GST_TIME_FORMAT " (ns)",
      GST_TIME_ARGS(raw_time), GST_TIME_ARGS(ns_time));

  return ns_time;
}

/**
 * gst_amba_hw_clock_get_internal_time:
 * @clock: a #GstClock
 *
 * GStreamer internal clock function - MUST be monotonic.
 * This is the core function called by GStreamer's clock system for all
 * internal time operations. It provides a monotonic time source that
 * never goes backwards, which is essential for GStreamer's scheduling
 * and synchronization algorithms.
 *
 * Key characteristics:
 * - Returns time in nanoseconds (GStreamer standard)
 * - Guarantees monotonic behavior (never decreases)
 * - Provides error recovery (uses cached time on hardware failure)
 * - Used by GStreamer for buffer timing and synchronization
 * - Thread-safe: multiple elements can call this concurrently
 *
 * Returns: monotonic time in nanoseconds, or cached time on error
 */
static GstClockTime
gst_amba_hw_clock_get_internal_time (GstClock * clock)
{
  GstAmbaHwClock *_ambaclock = GST_AMBA_HW_CLOCK_CAST (clock);
  GstClockTime hw_time;
  GstClockTime result;
  unsigned char tmp[64] = {0};
  int ret;
  char *endptr;

  if (_ambaclock->fd <= 0) {
    GST_ERROR_OBJECT (clock, "invalid file descriptor: %d", _ambaclock->fd);
    return _ambaclock->last_time;
  }

  /* Single lock for both hardware read and last_time update */
  GST_OBJECT_LOCK (clock);

  ret = read(_ambaclock->fd, tmp, sizeof(tmp) - 1);
  if (ret <= 0) {
    GST_DEBUG_OBJECT (clock, "hardware time query failed, using cached time");
    result = _ambaclock->last_time;
    GST_OBJECT_UNLOCK (clock);
    return result;
  }

  tmp[ret] = '\0';
  GstClockTime time_90k = strtoull((const char*) tmp, &endptr, 10);
  if (endptr == (const char*) tmp) {
    GST_DEBUG_OBJECT (clock, "invalid time string, using cached time");
    result = _ambaclock->last_time;
    GST_OBJECT_UNLOCK (clock);
    return result;
  }

  /* Convert to nanoseconds */
  hw_time = gst_util_uint64_scale(time_90k, GST_SECOND, _ambaclock->outfreq);

  /* Ensure clock is monotonic (GStreamer core requirement) */
  if (hw_time > _ambaclock->last_time) {
    _ambaclock->last_time = hw_time;
    result = hw_time;
  } else {
    GST_DEBUG_OBJECT (clock, "clock time went backwards, using cached time");
    result = _ambaclock->last_time;
  }

  GST_OBJECT_UNLOCK (clock);

  return result;
}

/**
 * gst_amba_hw_clock_get_resolution:
 * @clock: a #GstClock
 *
 * Get the resolution (precision) of the hardware clock.
 * This tells GStreamer the smallest time unit this clock can measure.
 *
 * For example:
 * - 90kHz clock → resolution ≈ 11111 nanoseconds (1/90000 second)
 * - 1MHz clock → resolution = 1000 nanoseconds (1 microsecond)
 *
 * Returns: clock resolution in nanoseconds
 */
static GstClockTime
gst_amba_hw_clock_get_resolution (GstClock * clock)
{
  GstAmbaHwClock *_ambaclock = GST_AMBA_HW_CLOCK_CAST (clock);

  /* Resolution = 1 / outfreq seconds, converted to nanoseconds */
  /* Use gst_util_uint64_scale for precision without overflow */
  GstClockTime resolution = gst_util_uint64_scale (GST_SECOND, 1, _ambaclock->outfreq);

  GST_DEBUG_OBJECT (clock, "clock resolution: %" GST_TIME_FORMAT " (outfreq=%lu Hz)",
      GST_TIME_ARGS (resolution), (unsigned long) _ambaclock->outfreq);

  return resolution;
}

/**
 * gst_amba_hw_clock_get_time:
 * @clock: a #GstAmbaHwClock
 *
 * Get raw hardware time for external synchronization and debugging.
 * This function returns the actual hardware time without any monotonic
 * guarantees or error recovery. It is intended for external code that
 * needs the true hardware time for synchronization purposes.
 *
 * Key characteristics:
 * - Returns time in nanoseconds (GStreamer standard)
 * - Returns actual hardware time (may go backwards)
 * - No error recovery (returns GST_CLOCK_TIME_NONE on failure)
 * - Used for external synchronization and debugging
 *
 * Note: This is different from get_internal_time() which provides
 * monotonic behavior for GStreamer's internal use.
 *
 * Returns: raw hardware time in nanoseconds, or GST_CLOCK_TIME_NONE on error
 */
GstClockTime
gst_amba_hw_clock_get_time (GstAmbaHwClock * clock)
{
  return amba_hwtimer_get_ns_time(clock);
}

/**
 * gst_amba_hw_clock_get_audio_aligned_time:
 * @clock: a #GstAmbaHwClock
 * @sample_rate: audio sample rate (e.g., 44100, 48000)
 *
 * Get hardware time aligned to audio sample rate for precise audio synchronization.
 * This function provides time stamps that are properly aligned to audio sample
 * boundaries, reducing precision mismatch between hardware clock and audio samples.
 *
 * Key characteristics:
 * - Returns time in nanoseconds (GStreamer standard)
 * - Aligned to audio sample rate boundaries
 * - Reduces precision mismatch between 90kHz hardware clock and audio sample rates
 * - Provides more accurate timing for audio synchronization
 *
 * Returns: audio-aligned hardware time in nanoseconds, or GST_CLOCK_TIME_NONE on error
 */
GstClockTime
gst_amba_hw_clock_get_audio_aligned_time (GstAmbaHwClock * clock, guint sample_rate)
{
  if (!clock || sample_rate == 0) {
    GST_ERROR("Invalid parameters: clock=%p, sample_rate=%u", clock, sample_rate);
    return GST_CLOCK_TIME_NONE;
  }

  GstClockTime raw_time = amba_hwtimer_get_raw_time(clock);
  if (raw_time == GST_CLOCK_TIME_NONE) {
    GST_DEBUG("Failed to get raw hardware time for audio alignment");
    return GST_CLOCK_TIME_NONE;
  }

  // Convert 90kHz to nanoseconds first
  GstClockTime ns_time = gst_util_uint64_scale(raw_time, GST_SECOND, clock->outfreq);

  // Align to audio sample rate boundaries
  // Calculate sample duration in nanoseconds
  GstClockTime sample_duration = GST_SECOND / sample_rate;

  // Align the timestamp to the nearest sample boundary
  GstClockTime aligned_time = (ns_time / sample_duration) * sample_duration;

  GST_DEBUG("Audio alignment: raw=%" GST_TIME_FORMAT " (90kHz), ns=%" GST_TIME_FORMAT ", aligned=%" GST_TIME_FORMAT " (sample_rate=%u)",
      GST_TIME_ARGS(raw_time), GST_TIME_ARGS(ns_time), GST_TIME_ARGS(aligned_time), sample_rate);

  return aligned_time;
}


/**
 * gst_amba_hw_clock_adjust:
 * @clock: a #GstAmbaHwClock
 * @time: a #GstClockTime
 *
 * Adjust @time. In simplified design, this is a no-op.
 * Clock synchronization is handled by GStreamer's pipeline clock system.
 *
 * Returns: @time unchanged
 */
GstClockTime
gst_amba_hw_clock_adjust (GstAmbaHwClock * clock, GstClockTime time)
{
  DUNUSED(clock);
  // Simplified design: no offset applied, return original time directly
  return time;
}

