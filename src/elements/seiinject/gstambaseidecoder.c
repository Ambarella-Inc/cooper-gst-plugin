/*
 * gstambaseidecoder.c
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

#include "gstambaseidecoder.h"
#include "gstambaseimeta.h"
#include <string.h>

/**
 * SECTION:element-amba_sei_decoder
 * @title: amba_sei_decoder
 *
 * `amba_sei_decoder` is a `GstBin` wrapper that:
 * - parses AU-aligned Annex-B H.264/H.265 input for libsei_box payload;
 * - keeps parsed result in a PTS-indexed map before decode;
 * - matches decoded `src` buffers by PTS and attaches `GstAmbaSeiMeta`.
 *
 * Expected input caps:
 * - `stream-format=byte-stream`
 * - `alignment=au`
 *
 * Typical pipeline:
 * |[
 * gst-launch-1.0 amba_venccap2 stream-id=0 ! queue ! h264parse ! \
 *   amba_seiinject lib-log-level=4 ! \
 *   amba_sei_decoder decoder-factory=openh264dec ! fakesink
 * ]|
 */

GST_DEBUG_CATEGORY_STATIC (gst_amba_seidecoder_debug);
#define GST_CAT_DEFAULT gst_amba_seidecoder_debug

typedef struct
{
  guint64 present_mask;
  guint64 timestamp_ns;
  gint gps_valid;
  gint32 gps_lat_e7;
  gint32 gps_lon_e7;
  gint32 gps_alt_cm;
  guint16 payload_version;
  guint16 payload_flags;
} GstAmbaSeiParsedEntry;

enum
{
  PROP_0,
  PROP_DECODER_FACTORY,
  PROP_MAX_ENTRIES,
};

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264, "
        "stream-format=(string)byte-stream, "
        "alignment=(string)au; "
        "video/x-h265, "
        "stream-format=(string)byte-stream, "
        "alignment=(string)au"));

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw"));

#define gst_amba_seidecoder_parent_class parent_class
G_DEFINE_TYPE (GstAmbaSeiDecoder, gst_amba_seidecoder, GST_TYPE_BIN);

static void gst_amba_seidecoder_finalize (GObject *object);
static void gst_amba_seidecoder_set_property (GObject *object, guint prop_id,
    const GValue *value, GParamSpec *pspec);
static void gst_amba_seidecoder_get_property (GObject *object, guint prop_id,
    GValue *value, GParamSpec *pspec);
static GstStateChangeReturn gst_amba_seidecoder_change_state (GstElement *element,
    GstStateChange transition);

static void gst_amba_seidecoder_clear_pts_map (GstAmbaSeiDecoder *self);
static gboolean gst_amba_seidecoder_set_codec_from_caps (GstAmbaSeiDecoder *self,
    const GstCaps *caps);
static GstPadProbeReturn gst_amba_seidecoder_sink_event_probe (GstPad *pad,
    GstPadProbeInfo *info, gpointer user_data);
static GstPadProbeReturn gst_amba_seidecoder_sink_buffer_probe (GstPad *pad,
    GstPadProbeInfo *info, gpointer user_data);
static GstPadProbeReturn gst_amba_seidecoder_src_buffer_probe (GstPad *pad,
    GstPadProbeInfo *info, gpointer user_data);
static gboolean gst_amba_seidecoder_setup_external_pads (GstAmbaSeiDecoder *self);
static gboolean gst_amba_seidecoder_build_internal_decoder (GstAmbaSeiDecoder *self);
static void gst_amba_seidecoder_teardown_internal_decoder (GstAmbaSeiDecoder *self);

static void
gst_amba_seidecoder_parsed_entry_free (gpointer data)
{
  g_free (data);
}

static void
gst_amba_seidecoder_clear_pts_map (GstAmbaSeiDecoder *self)
{
  gpointer k;

  g_mutex_lock (&self->lock);
  while ((k = g_queue_pop_head (&self->pts_order)) != NULL)
    (void) k;
  g_hash_table_remove_all (self->order_map);
  g_hash_table_remove_all (self->pts_map);
  g_mutex_unlock (&self->lock);
}

static gboolean
gst_amba_seidecoder_set_codec_from_caps (GstAmbaSeiDecoder *self,
    const GstCaps *caps)
{
  GstStructure *s;
  const gchar *name;
  const gchar *stream_format;
  const gchar *alignment;

  if (!caps || gst_caps_is_empty (caps)) {
    self->codec_id = SEI_BOX_CODEC_UNKNOWN;
    return FALSE;
  }

  s = gst_caps_get_structure ((GstCaps *) caps, 0);
  name = gst_structure_get_name (s);
  stream_format = gst_structure_get_string (s, "stream-format");
  alignment = gst_structure_get_string (s, "alignment");

  if (!stream_format || g_strcmp0 (stream_format, "byte-stream") != 0 ||
      !alignment || g_strcmp0 (alignment, "au") != 0) {
    self->codec_id = SEI_BOX_CODEC_UNKNOWN;
    GST_ERROR_OBJECT (self,
        "unsupported caps for sei parse name=%s stream-format=%s alignment=%s "
        "(require byte-stream + au)",
        name ? name : "NULL",
        stream_format ? stream_format : "NULL",
        alignment ? alignment : "NULL");
    return FALSE;
  }

  if (g_strcmp0 (name, "video/x-h264") == 0)
    self->codec_id = SEI_BOX_CODEC_H264;
  else if (g_strcmp0 (name, "video/x-h265") == 0)
    self->codec_id = SEI_BOX_CODEC_H265;
  else
    self->codec_id = SEI_BOX_CODEC_UNKNOWN;
  return (self->codec_id != SEI_BOX_CODEC_UNKNOWN);
}

static gboolean
gst_amba_seidecoder_setup_external_pads (GstAmbaSeiDecoder *self)
{
  GstPad *sink_ghost = NULL;
  GstPad *src_ghost = NULL;

  if (self->sink_pad && self->src_pad)
    return TRUE;

  sink_ghost = gst_ghost_pad_new_no_target ("sink", GST_PAD_SINK);
  src_ghost = gst_ghost_pad_new_no_target ("src", GST_PAD_SRC);
  if (!sink_ghost || !src_ghost) {
    GST_ERROR_OBJECT (self, "Failed to create ghost pads");
    if (sink_ghost)
      gst_object_unref (sink_ghost);
    if (src_ghost)
      gst_object_unref (src_ghost);
    return FALSE;
  }

  gst_pad_set_active (sink_ghost, TRUE);
  gst_pad_set_active (src_ghost, TRUE);
  if (!gst_element_add_pad (GST_ELEMENT (self), sink_ghost) ||
      !gst_element_add_pad (GST_ELEMENT (self), src_ghost)) {
    GST_ERROR_OBJECT (self, "Failed to add ghost pads");
    gst_object_unref (sink_ghost);
    gst_object_unref (src_ghost);
    return FALSE;
  }

  self->sink_pad = sink_ghost;
  self->src_pad = src_ghost;
  /* Keep explicit refs so teardown/finalize can safely operate on pads. */
  gst_object_ref (self->sink_pad);
  gst_object_ref (self->src_pad);
  self->sink_event_probe_id = gst_pad_add_probe (self->sink_pad,
      GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
      gst_amba_seidecoder_sink_event_probe, self, NULL);
  self->sink_buffer_probe_id = gst_pad_add_probe (self->sink_pad,
      GST_PAD_PROBE_TYPE_BUFFER,
      gst_amba_seidecoder_sink_buffer_probe, self, NULL);
  self->src_buffer_probe_id = gst_pad_add_probe (self->src_pad,
      GST_PAD_PROBE_TYPE_BUFFER,
      gst_amba_seidecoder_src_buffer_probe, self, NULL);

  return TRUE;
}

static void
gst_amba_seidecoder_teardown_internal_decoder (GstAmbaSeiDecoder *self)
{
  if (!self)
    return;

  if (self->decoder) {
    if (self->sink_pad)
      (void) gst_ghost_pad_set_target (GST_GHOST_PAD (self->sink_pad), NULL);
    if (self->src_pad)
      (void) gst_ghost_pad_set_target (GST_GHOST_PAD (self->src_pad), NULL);
    if (GST_OBJECT_PARENT (self->decoder) == GST_OBJECT (self)) {
      gst_element_set_state (self->decoder, GST_STATE_NULL);
      (void) gst_bin_remove (GST_BIN (self), self->decoder);
    }
    gst_object_unref (self->decoder);
    self->decoder = NULL;
  }
  self->codec_id = SEI_BOX_CODEC_UNKNOWN;
}

static gboolean
gst_amba_seidecoder_build_internal_decoder (GstAmbaSeiDecoder *self)
{
  GstPad *sink_target = NULL;
  GstPad *src_target = NULL;

  if (!self->sink_pad || !self->src_pad) {
    GST_ERROR_OBJECT (self, "external pads are not ready");
    return FALSE;
  }
  if (self->decoder)
    return TRUE;

  self->decoder = gst_element_factory_make (self->decoder_factory, NULL);
  if (!self->decoder) {
    GST_ERROR_OBJECT (self, "Failed to create decoder factory=%s",
        self->decoder_factory ? self->decoder_factory : "(null)");
    return FALSE;
  }
  gst_bin_add (GST_BIN (self), self->decoder);

  sink_target = gst_element_get_static_pad (self->decoder, "sink");
  src_target = gst_element_get_static_pad (self->decoder, "src");
  if (!sink_target || !src_target) {
    GST_ERROR_OBJECT (self, "Decoder pads not found for factory=%s",
        self->decoder_factory ? self->decoder_factory : "(null)");
    goto fail;
  }
  if (!gst_ghost_pad_set_target (GST_GHOST_PAD (self->sink_pad), sink_target) ||
      !gst_ghost_pad_set_target (GST_GHOST_PAD (self->src_pad), src_target)) {
    GST_ERROR_OBJECT (self, "Failed to bind ghost pad targets");
    goto fail;
  }
  gst_object_unref (sink_target);
  gst_object_unref (src_target);
  gst_object_ref (self->decoder);
  GST_INFO_OBJECT (self, "decoder factory active: %s",
      self->decoder_factory ? self->decoder_factory : "(null)");
  return TRUE;

fail:
  if (sink_target)
    gst_object_unref (sink_target);
  if (src_target)
    gst_object_unref (src_target);
  if (self->sink_pad)
    (void) gst_ghost_pad_set_target (GST_GHOST_PAD (self->sink_pad), NULL);
  if (self->src_pad)
    (void) gst_ghost_pad_set_target (GST_GHOST_PAD (self->src_pad), NULL);
  if (self->decoder) {
    if (GST_OBJECT_PARENT (self->decoder) == GST_OBJECT (self))
      (void) gst_bin_remove (GST_BIN (self), self->decoder);
    gst_object_unref (self->decoder);
    self->decoder = NULL;
  }
  return FALSE;
}

static GstPadProbeReturn
gst_amba_seidecoder_sink_event_probe (GstPad *pad,
    GstPadProbeInfo *info, gpointer user_data)
{
  GstAmbaSeiDecoder *self = GST_AMBA_SEIDECODER (user_data);
  GstEvent *event = GST_PAD_PROBE_INFO_EVENT (info);
  GstEventType type;
  (void) pad;

  if (!event)
    return GST_PAD_PROBE_OK;

  type = GST_EVENT_TYPE (event);
  if (type == GST_EVENT_FLUSH_START || type == GST_EVENT_SEGMENT) {
    GST_DEBUG_OBJECT (self, "state map-clear event=%s",
        GST_EVENT_TYPE_NAME (event));
    gst_amba_seidecoder_clear_pts_map (self);
  } else if (type == GST_EVENT_CAPS) {
    GstCaps *caps = NULL;
    gst_event_parse_caps (event, &caps);
    if (!gst_amba_seidecoder_set_codec_from_caps (self, caps)) {
      GST_ELEMENT_ERROR (self, CORE, NEGOTIATION,
          ("amba_sei_decoder requires stream-format=byte-stream, alignment=au"),
          ("Incoming caps are not supported for SEI parsing"));
      return GST_PAD_PROBE_DROP;
    }
  }

  return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn
gst_amba_seidecoder_sink_buffer_probe (GstPad *pad,
    GstPadProbeInfo *info, gpointer user_data)
{
  GstAmbaSeiDecoder *self = GST_AMBA_SEIDECODER (user_data);
  GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER (info);
  GstMapInfo map;
  GstClockTime pts;
  int rc;
  SeiBoxDecodeMeta meta;
  GstAmbaSeiParsedEntry *entry = NULL;
  guint64 *key = NULL;
  gboolean mapped = FALSE;
  (void) pad;

  if (!buffer || self->codec_id == SEI_BOX_CODEC_UNKNOWN || !self->parse_info)
    return GST_PAD_PROBE_OK;

  pts = GST_BUFFER_PTS (buffer);
  if (!GST_CLOCK_TIME_IS_VALID (pts)) {
    self->dropped_no_pts++;
    GST_WARNING_OBJECT (self,
        "sink drop=no-pts count=%u", self->dropped_no_pts);
    return GST_PAD_PROBE_OK;
  }

  if (!gst_buffer_map (buffer, &map, GST_MAP_READ))
    return GST_PAD_PROBE_OK;
  mapped = TRUE;

  sei_box_info_reset (self->parse_info);
  sei_box_decode_meta_init (&meta);
  rc = sei_box_parse_au_to_info (map.data, map.size, (SeiBoxCodec) self->codec_id,
      self->parse_info, &meta);
  if (rc != SEI_BOX_OK) {
    if (rc == SEI_BOX_NO_DATA) {
      GST_DEBUG_OBJECT (self,
          "parse result=no-data pts=%" GST_TIME_FORMAT,
          GST_TIME_ARGS (pts));
    } else {
      GST_WARNING_OBJECT (self,
          "parse result=error rc=%d pts=%" GST_TIME_FORMAT,
          rc, GST_TIME_ARGS (pts));
    }
    goto cleanup;
  }

  entry = g_new0 (GstAmbaSeiParsedEntry, 1);
  entry->present_mask = (guint64) sei_box_info_get_present_mask (self->parse_info);
  if (sei_box_info_is_present (self->parse_info, SEI_BOX_T_TIMESTAMP)) {
    SeiBoxTimestamp ts = 0;
    (void) sei_box_info_get (self->parse_info, SEI_BOX_T_TIMESTAMP,
        &ts, SEI_BOX_INFO_VALUE_SIZE_TIMESTAMP);
    entry->timestamp_ns = (guint64) ts;
  }
  if (sei_box_info_is_present (self->parse_info, SEI_BOX_T_GPS)) {
    SeiBoxGpsData gps;
    memset (&gps, 0, sizeof (gps));
    (void) sei_box_info_get (self->parse_info, SEI_BOX_T_GPS,
        &gps, SEI_BOX_INFO_VALUE_SIZE_GPS);
    entry->gps_valid = gps.valid;
    entry->gps_lat_e7 = gps.lat_e7;
    entry->gps_lon_e7 = gps.lon_e7;
    entry->gps_alt_cm = gps.alt_cm;
  }
  entry->payload_version = meta.payload_version;
  entry->payload_flags = meta.payload_flags;

  key = g_new (guint64, 1);
  *key = (guint64) pts;

  g_mutex_lock (&self->lock);
  if (g_hash_table_contains (self->pts_map, key)) {
    GList *old_link = (GList *) g_hash_table_lookup (self->order_map, key);
    if (old_link)
      g_queue_delete_link (&self->pts_order, old_link);
    (void) g_hash_table_remove (self->order_map, key);
  }
  g_hash_table_insert (self->pts_map, key, entry);
  g_queue_push_tail (&self->pts_order, key);
  g_hash_table_insert (self->order_map, key,
      g_queue_peek_tail_link (&self->pts_order));
  while (g_hash_table_size (self->pts_map) > self->max_entries) {
    guint64 *old_key = (guint64 *) g_queue_pop_head (&self->pts_order);
    if (!old_key)
      break;
    (void) g_hash_table_remove (self->order_map, old_key);
    (void) g_hash_table_remove (self->pts_map, old_key);
    GST_WARNING_OBJECT (self,
        "state map-evict pts=%" GST_TIME_FORMAT " max-entries=%u",
        GST_TIME_ARGS ((GstClockTime) (*old_key)), self->max_entries);
  }
  g_mutex_unlock (&self->lock);

  key = NULL;
  entry = NULL;

cleanup:
  if (mapped)
    gst_buffer_unmap (buffer, &map);
  if (key)
    g_free (key);
  if (entry)
    g_free (entry);
  return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn
gst_amba_seidecoder_src_buffer_probe (GstPad *pad,
    GstPadProbeInfo *info, gpointer user_data)
{
  GstAmbaSeiDecoder *self = GST_AMBA_SEIDECODER (user_data);
  GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER (info);
  GstBuffer *writable = NULL;
  GstClockTime pts;
  GstAmbaSeiParsedEntry local_entry;
  GstAmbaSeiParsedEntry *entry = NULL;
  guint64 key;
  (void) pad;

  if (!buffer)
    return GST_PAD_PROBE_OK;

  pts = GST_BUFFER_PTS (buffer);
  if (!GST_CLOCK_TIME_IS_VALID (pts))
    return GST_PAD_PROBE_OK;

  key = (guint64) pts;
  memset (&local_entry, 0, sizeof (local_entry));

  g_mutex_lock (&self->lock);
  entry = (GstAmbaSeiParsedEntry *) g_hash_table_lookup (self->pts_map, &key);
  if (entry) {
    GList *link = (GList *) g_hash_table_lookup (self->order_map, &key);
    local_entry = *entry;
    if (link)
      g_queue_delete_link (&self->pts_order, link);
    (void) g_hash_table_remove (self->order_map, &key);
    (void) g_hash_table_remove (self->pts_map, &key);
  }
  g_mutex_unlock (&self->lock);

  if (!entry) {
    GST_DEBUG_OBJECT (self,
        "src meta=miss pts=%" GST_TIME_FORMAT,
        GST_TIME_ARGS (pts));
    return GST_PAD_PROBE_OK;
  }

  /* Src-probe buffers may be non-writable; convert before adding meta. */
  writable = gst_buffer_make_writable (buffer);
  if (!writable)
    return GST_PAD_PROBE_OK;
  if (writable != buffer) {
    GST_LOG_OBJECT (self,
        "src writable=cow pts=%" GST_TIME_FORMAT,
        GST_TIME_ARGS (pts));
    GST_PAD_PROBE_INFO_DATA (info) = writable;
  } else {
    GST_LOG_OBJECT (self,
        "src writable=reuse pts=%" GST_TIME_FORMAT,
        GST_TIME_ARGS (pts));
  }

  {
    GstAmbaSeiMeta *meta = (GstAmbaSeiMeta *) gst_buffer_add_meta (writable,
        GST_AMBA_SEI_META_INFO, NULL);
    if (meta) {
      meta->present_mask = local_entry.present_mask;
      meta->timestamp_ns = local_entry.timestamp_ns;
      meta->gps_valid = local_entry.gps_valid;
      meta->gps_lat_e7 = local_entry.gps_lat_e7;
      meta->gps_lon_e7 = local_entry.gps_lon_e7;
      meta->gps_alt_cm = local_entry.gps_alt_cm;
      meta->payload_version = local_entry.payload_version;
      meta->payload_flags = local_entry.payload_flags;

      GST_DEBUG_OBJECT (self,
          "src meta=added pts=%" GST_TIME_FORMAT
          " mask=0x%" G_GINT64_MODIFIER "x ts=%" G_GUINT64_FORMAT
          " gps(valid=%d lat=%d lon=%d alt=%d) ver=%u flags=0x%04x",
          GST_TIME_ARGS (pts),
          local_entry.present_mask,
          local_entry.timestamp_ns,
          local_entry.gps_valid, local_entry.gps_lat_e7,
          local_entry.gps_lon_e7, local_entry.gps_alt_cm,
          local_entry.payload_version, local_entry.payload_flags);
    } else {
      GST_WARNING_OBJECT (self,
          "src meta=add-failed pts=%" GST_TIME_FORMAT,
          GST_TIME_ARGS (pts));
    }
  }

  return GST_PAD_PROBE_OK;
}

static void
gst_amba_seidecoder_init (GstAmbaSeiDecoder *self)
{
  self->decoder = NULL;
  self->sink_pad = NULL;
  self->src_pad = NULL;
  self->sink_buffer_probe_id = 0;
  self->sink_event_probe_id = 0;
  self->src_buffer_probe_id = 0;
  self->decoder_factory = g_strdup ("avdec_h264");
  self->max_entries = 512;
  self->codec_id = SEI_BOX_CODEC_UNKNOWN;
  self->dropped_no_pts = 0;
  self->parse_info = sei_box_info_new ();
  self->pts_map = g_hash_table_new_full (g_int64_hash, g_int64_equal,
      g_free, gst_amba_seidecoder_parsed_entry_free);
  self->order_map = g_hash_table_new (g_int64_hash, g_int64_equal);
  g_queue_init (&self->pts_order);
  g_mutex_init (&self->lock);
  (void) sei_box_init ();
  (void) gst_amba_seidecoder_setup_external_pads (self);
}

static void
gst_amba_seidecoder_finalize (GObject *object)
{
  GstAmbaSeiDecoder *self = GST_AMBA_SEIDECODER (object);
  gpointer k;
  gst_amba_seidecoder_teardown_internal_decoder (self);
  if (self->sink_pad && self->sink_event_probe_id) {
    gst_pad_remove_probe (self->sink_pad, self->sink_event_probe_id);
    self->sink_event_probe_id = 0;
  }
  if (self->sink_pad && self->sink_buffer_probe_id) {
    gst_pad_remove_probe (self->sink_pad, self->sink_buffer_probe_id);
    self->sink_buffer_probe_id = 0;
  }
  if (self->src_pad && self->src_buffer_probe_id) {
    gst_pad_remove_probe (self->src_pad, self->src_buffer_probe_id);
    self->src_buffer_probe_id = 0;
  }
  if (self->sink_pad) {
    if (GST_OBJECT_PARENT (self->sink_pad) == GST_OBJECT (self)) {
      gst_pad_set_active (self->sink_pad, FALSE);
      (void) gst_element_remove_pad (GST_ELEMENT (self), self->sink_pad);
    }
    gst_object_unref (self->sink_pad);
    self->sink_pad = NULL;
  }
  if (self->src_pad) {
    if (GST_OBJECT_PARENT (self->src_pad) == GST_OBJECT (self)) {
      gst_pad_set_active (self->src_pad, FALSE);
      (void) gst_element_remove_pad (GST_ELEMENT (self), self->src_pad);
    }
    gst_object_unref (self->src_pad);
    self->src_pad = NULL;
  }

  gst_amba_seidecoder_clear_pts_map (self);
  while ((k = g_queue_pop_head (&self->pts_order)) != NULL)
    (void) k;
  if (self->order_map) {
    g_hash_table_unref (self->order_map);
    self->order_map = NULL;
  }
  if (self->pts_map) {
    g_hash_table_unref (self->pts_map);
    self->pts_map = NULL;
  }
  g_mutex_clear (&self->lock);

  if (self->parse_info) {
    sei_box_info_free (self->parse_info);
    self->parse_info = NULL;
  }
  g_clear_pointer (&self->decoder_factory, g_free);
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_amba_seidecoder_set_property (GObject *object, guint prop_id,
    const GValue *value, GParamSpec *pspec)
{
  GstAmbaSeiDecoder *self = GST_AMBA_SEIDECODER (object);

  switch (prop_id) {
    case PROP_DECODER_FACTORY:
    {
      gchar *new_factory = g_value_dup_string (value);
      GstState cur = GST_STATE_NULL;
      GstState pending = GST_STATE_VOID_PENDING;
      if (!new_factory || !new_factory[0]) {
        g_clear_pointer (&new_factory, g_free);
        new_factory = g_strdup ("avdec_h264");
      }
      (void) gst_element_get_state (GST_ELEMENT (self), &cur, &pending, 0);
      if (cur != GST_STATE_NULL || pending != GST_STATE_VOID_PENDING) {
        GST_WARNING_OBJECT (self,
            "ignore decoder-factory change outside NULL state (set before PLAYING)");
        g_free (new_factory);
        break;
      }
      g_free (self->decoder_factory);
      self->decoder_factory = new_factory;
      GST_INFO_OBJECT (self, "decoder factory configured: %s",
          self->decoder_factory ? self->decoder_factory : "(null)");
      break;
    }
    case PROP_MAX_ENTRIES:
      self->max_entries = g_value_get_uint (value);
      if (self->max_entries == 0)
        self->max_entries = 1;
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_amba_seidecoder_change_state (GstElement *element, GstStateChange transition)
{
  GstAmbaSeiDecoder *self = GST_AMBA_SEIDECODER (element);
  GstStateChangeReturn ret;

  if (transition == GST_STATE_CHANGE_NULL_TO_READY) {
    if (!self->decoder && !gst_amba_seidecoder_build_internal_decoder (self)) {
      GST_ERROR_OBJECT (self, "failed to build decoder for factory=%s",
          self->decoder_factory ? self->decoder_factory : "(null)");
      return GST_STATE_CHANGE_FAILURE;
    }
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  if (ret != GST_STATE_CHANGE_FAILURE &&
      transition == GST_STATE_CHANGE_READY_TO_NULL) {
    gst_amba_seidecoder_teardown_internal_decoder (self);
  }
  return ret;
}

static void
gst_amba_seidecoder_get_property (GObject *object, guint prop_id,
    GValue *value, GParamSpec *pspec)
{
  GstAmbaSeiDecoder *self = GST_AMBA_SEIDECODER (object);

  switch (prop_id) {
    case PROP_DECODER_FACTORY:
      g_value_set_string (value, self->decoder_factory);
      break;
    case PROP_MAX_ENTRIES:
      g_value_set_uint (value, self->max_entries);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_amba_seidecoder_class_init (GstAmbaSeiDecoderClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_amba_seidecoder_debug, "ambaseidecoder", 0,
      "Amba SEI decoder with PTS join and GstMeta");

  gobject_class->finalize = gst_amba_seidecoder_finalize;
  gobject_class->set_property = gst_amba_seidecoder_set_property;
  gobject_class->get_property = gst_amba_seidecoder_get_property;
  element_class->change_state = gst_amba_seidecoder_change_state;

  g_object_class_install_property (gobject_class, PROP_DECODER_FACTORY,
      g_param_spec_string ("decoder-factory", "Decoder factory",
          "Factory name of internal decoder element",
          "avdec_h264",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MAX_ENTRIES,
      g_param_spec_uint ("max-entries", "Max entries",
          "Maximum retained PTS->SEI entries before cleanup",
          1, 65535, 512,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element_class,
      "Amba SEI decoder",
      "Decoder/Video",
      "Parse SEI from encoded AU and attach metadata on decoded output",
      "Yang Yu <yyua@ambarella.com>");
  gst_element_class_add_static_pad_template (element_class, &sink_factory);
  gst_element_class_add_static_pad_template (element_class, &src_factory);
}
