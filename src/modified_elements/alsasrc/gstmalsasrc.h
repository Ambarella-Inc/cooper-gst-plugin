/* GStreamer
 * Copyright (C)  2005 Wim Taymans <wim@fluendo.com>
 *
 * gstalsasrc.h:
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


#ifndef __GST_MALSASRC_H__
#define __GST_MALSASRC_H__

#include <gst/audio/audio.h>

#include "clock_if.h"

#include "gstmalsa.h"


G_BEGIN_DECLS

#define GST_TYPE_MALSA_SRC            (gst_malsasrc_get_type())
#define GST_MALSA_SRC(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MALSA_SRC,GstMalsaSrc))
#define GST_MALSA_SRC_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MALSA_SRC,GstMalsaSrcClass))
#define GST_IS_MALSA_SRC(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MALSA_SRC))
#define GST_IS_MALSA_SRC_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MALSA_SRC))
#define GST_MALSA_SRC_CAST(obj)       ((GstMalsaSrc *)(obj))

#define GST_MALSA_SRC_GET_LOCK(obj)  (&GST_MALSA_SRC_CAST (obj)->alsa_lock)
#define GST_MALSA_SRC_LOCK(obj)      (g_mutex_lock (GST_MALSA_SRC_GET_LOCK (obj)))
#define GST_MALSA_SRC_UNLOCK(obj)    (g_mutex_unlock (GST_MALSA_SRC_GET_LOCK (obj)))

typedef struct _GstMalsaSrc GstMalsaSrc;
typedef struct _GstMalsaSrcClass GstMalsaSrcClass;

/**
 * GstMalsaSrc:
 *
 * Opaque data structure
 */
struct _GstMalsaSrc {
  GstAudioSrc           src;

  gchar                 *device;

  snd_pcm_t             *handle;
  snd_pcm_hw_params_t   *hwparams;
  snd_pcm_sw_params_t   *swparams;

  GstCaps               *cached_caps;

  snd_pcm_access_t      access;
  snd_pcm_format_t      format;
  guint                 rate;
  guint                 channels;
  gint                  bpf;
  gboolean              driver_timestamps;
  gboolean              use_driver_timestamps;
  gboolean              use_hw_timer;

  guint                 buffer_time;
  guint                 period_time;
  snd_pcm_uframes_t     buffer_size;
  snd_pcm_uframes_t     period_size;

  GMutex                alsa_lock;

  GstClockTime gst_clok_time;
  guint is_clock_setup;
  guint is_clock_started;
  clock_ctx_t * clock;

  guint dump_num;
};

struct _GstMalsaSrcClass {
  GstAudioSrcClass parent_class;
};

GType gst_malsasrc_get_type(void);

G_END_DECLS

#endif /* __GST_MALSASRC_H__ */

