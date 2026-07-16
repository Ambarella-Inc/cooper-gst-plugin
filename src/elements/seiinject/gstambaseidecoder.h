/*
 * gstambaseidecoder.h
 *
 * History:
 *    04/09/2026 - [Yang Yu] created file
 *
 * Copyright (C) 2026 Ambarella International LP
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

#ifndef __GST_AMBA_SEI_DECODER_H__
#define __GST_AMBA_SEI_DECODER_H__

#include <gst/gst.h>

typedef struct _SeiBoxInfo SeiBoxInfo;

G_BEGIN_DECLS

#define GST_TYPE_AMBA_SEIDECODER       (gst_amba_seidecoder_get_type ())
#define GST_AMBA_SEIDECODER(obj)       (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_AMBA_SEIDECODER, GstAmbaSeiDecoder))
#define GST_AMBA_SEIDECODER_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_AMBA_SEIDECODER, GstAmbaSeiDecoderClass))
#define GST_IS_AMBA_SEIDECODER(obj)    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_AMBA_SEIDECODER))

typedef struct _GstAmbaSeiDecoder GstAmbaSeiDecoder;
typedef struct _GstAmbaSeiDecoderClass GstAmbaSeiDecoderClass;

struct _GstAmbaSeiDecoder
{
  GstBin parent;

  GstElement *decoder;
  GstPad *sink_pad;
  GstPad *src_pad;
  gulong sink_buffer_probe_id;
  gulong sink_event_probe_id;
  gulong src_buffer_probe_id;

  gchar *decoder_factory;
  guint max_entries;
  guint codec_id;

  GHashTable *pts_map;
  GHashTable *order_map;
  GQueue pts_order;
  GMutex lock;
  guint dropped_no_pts;
  SeiBoxInfo *parse_info;
};

struct _GstAmbaSeiDecoderClass
{
  GstBinClass parent_class;
};

GType gst_amba_seidecoder_get_type (void);

G_END_DECLS

#endif /* __GST_AMBA_SEI_DECODER_H__ */
