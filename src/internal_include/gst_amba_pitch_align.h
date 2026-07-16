/*
 * gst_amba_pitch_align.h
 *
 * History:
 *    5/7/2026 - [Peng-Xue Duan] created file
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
 *
 * DSP / Cavalry buffer pitch and size rounding helpers.
 */

#ifndef __GST_AMBA_PITCH_ALIGN_H__
#define __GST_AMBA_PITCH_ALIGN_H__

#include <gst/gst.h>

#include "iav_ioctl.h"
#include "cavalry_mem.h"

G_BEGIN_DECLS

#ifndef IAV_DSP_BUF_PITCH_ALIGN
#define IAV_DSP_BUF_PITCH_ALIGN (128)
#endif
#ifndef CAVALRY_PORT_PITCH_ALIGN
#define CAVALRY_PORT_PITCH_ALIGN (128)
#endif

static inline gsize
gst_amba_pitch_gcd (gsize a, gsize b)
{
  while (b != 0) {
    gsize t = b;
    b = a % b;
    a = t;
  }
  return a;
}

static inline gsize
gst_amba_pitch_lcm_dsp_and_cavalry_step (void)
{
  gsize a = (gsize) IAV_DSP_BUF_PITCH_ALIGN;
  gsize b = (gsize) CAVALRY_PORT_PITCH_ALIGN;
  gsize g = gst_amba_pitch_gcd (a, b);

  return g ? (a / g) * b : 0;
}

static inline guint
gst_amba_align_pitch_up (guint pitch, gsize step)
{
  if (step == 0)
    return pitch;
  return (guint) (((gsize) pitch + step - 1) / step * step);
}

static inline guint
gst_amba_round_size_up_odd_units (gsize raw, gsize unit)
{
  gsize n;

  if (unit == 0)
    return (guint) raw;
  n = (raw + unit - 1) / unit;
  if ((n & 1) == 0)
    n++;
  return (guint) (n * unit);
}

/** @raw rounded up to an odd multiple of IAV_DSP_BUF_PITCH_ALIGN (gsize; for large dmabuf). */
static inline gsize
gst_amba_round_gsize_up_odd_units (gsize raw, gsize unit)
{
  gsize n;

  if (unit == 0)
    return raw;
  n = (raw + unit - 1) / unit;
  if ((n & 1) == 0)
    n++;
  return n * unit;
}

/**
 * gst_amba_align_is_odd_dsp_pitch:
 * @pitch: Y/UV row stride in bytes.
 *
 * Returns %TRUE if @pitch is 128 times an odd integer.
 */
static inline gboolean
gst_amba_align_is_odd_dsp_pitch (guint pitch)
{
  if (pitch == 0 || (pitch % 128u) != 0u)
    return FALSE;
  return ((pitch / 128u) & 1u) != 0u;
}


/**
 * AMBA_ALIGN_PITCH_DSP_AND_CAVALRY:
 * Round pitch (bytes/row) up to a multiple of both IAV_DSP_BUF_PITCH_ALIGN and
 * CAVALRY_PORT_PITCH_ALIGN.
 */
 #define AMBA_ALIGN_PITCH_DSP_AND_CAVALRY(pitch) \
 gst_amba_align_pitch_up ((guint) (pitch), gst_amba_pitch_lcm_dsp_and_cavalry_step ())

 #define AMBA_ROUND_SIZE_ODD_DSP_PITCH_UNITS(raw) \
 gst_amba_round_size_up_odd_units ((raw), (gsize) IAV_DSP_BUF_PITCH_ALIGN)

#define AMBA_ROUND_GSIZE_ODD_DSP_PITCH_UNITS(raw) \
  gst_amba_round_gsize_up_odd_units ((raw), (gsize) IAV_DSP_BUF_PITCH_ALIGN)

#define AMBA_ALIGN_IS_ODD_DSP_PITCH(pitch) \
  gst_amba_align_is_odd_dsp_pitch ((pitch))

G_END_DECLS

#endif /* __GST_AMBA_PITCH_ALIGN_H__ */
