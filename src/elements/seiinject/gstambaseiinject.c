/*
 * gstambaseiinject.c
 *
 * History:
 *    04/09/2026 - [Yang Yu] created file
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

#include "gstambaseiinject.h"
#include "sei_box.h"
#include <gst/gstbuffer.h>
#include <string.h>

/**
 * SECTION:element-amba_seiinject
 * @title: amba_seiinject
 *
 * `amba_seiinject` is a `GstBaseTransform` element that injects libsei_box payload
 * into Annex-B AU-aligned H.264/H.265 streams.
 *
 * Behavior summary:
 * - Works on `video/x-h264` or `video/x-h265` with `stream-format=byte-stream`
 *   and `alignment=au`.
 * - Timestamp TLV prefers the first `GstReferenceTimestampMeta` on the buffer
 *   (raw `timestamp` field, e.g. from `amba_venccap2`); otherwise PTS/DTS.
 *   Optional GPS TLV.
 * - Falls back to passthrough when no TLV is enabled or codec is unsupported.
 * - Optional `self-verify` re-parses injected output for debug logging only.
 *
 * Typical pipeline:
 * |[
 * gst-launch-1.0 amba_venccap2 stream-id=0 ! queue ! h264parse ! \
 *   amba_seiinject lib-log-level=4 ! \
 *   amba_sei_decoder decoder-factory=openh264dec ! fakesink
 * ]|
 */

GST_DEBUG_CATEGORY_STATIC (gst_amba_seiinject_debug);
#define GST_CAT_DEFAULT gst_amba_seiinject_debug

enum {
  PROP_0,
  PROP_ADD_TIMESTAMP,
  PROP_ADD_GPS,
  PROP_SELF_VERIFY,
  PROP_GPS_DEVICE,
  PROP_LIB_LOG_LEVEL,
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
    GST_STATIC_CAPS ("video/x-h264, "
        "stream-format=(string)byte-stream, "
        "alignment=(string)au; "
        "video/x-h265, "
        "stream-format=(string)byte-stream, "
        "alignment=(string)au"));

#define gst_amba_seiinject_parent_class parent_class
G_DEFINE_TYPE (GstAmbaSeiInject, gst_amba_seiinject, GST_TYPE_BASE_TRANSFORM);

static void gst_amba_seiinject_finalize (GObject *object);
static void gst_amba_seiinject_set_property (GObject *object, guint prop_id,
    const GValue *value, GParamSpec *pspec);
static void gst_amba_seiinject_get_property (GObject *object, guint prop_id,
    GValue *value, GParamSpec *pspec);
static GstCaps *gst_amba_seiinject_transform_caps (GstBaseTransform *trans,
    GstPadDirection direction, GstCaps *caps, GstCaps *filter);
static GstFlowReturn gst_amba_seiinject_transform (GstBaseTransform *trans,
    GstBuffer *in_buf, GstBuffer *out_buf);
static gboolean gst_amba_seiinject_set_caps (GstBaseTransform *trans,
    GstCaps *incaps, GstCaps *outcaps);
static GstFlowReturn gst_amba_seiinject_prepare_output_buffer (GstBaseTransform *trans,
    GstBuffer *input, GstBuffer **outbuf);
static const gchar *sei_box_codec_name (guint codec_id);
static void gst_amba_seiinject_update_passthrough (GstAmbaSeiInject *self);
static void gst_amba_seiinject_fill_info (GstAmbaSeiInject *self, GstBuffer *buffer);

/* First GstReferenceTimestampMeta on the buffer: raw timestamp field only */
static gboolean
gst_amba_seiinject_timestamp_from_reference_meta (GstBuffer *buffer,
    SeiBoxTimestamp *ts_out)
{
  GstMeta *meta;
  gpointer state = NULL;

  g_return_val_if_fail (buffer != NULL, FALSE);
  g_return_val_if_fail (ts_out != NULL, FALSE);

  while ((meta = gst_buffer_iterate_meta (buffer, &state))) {
    if (meta->info == gst_reference_timestamp_meta_get_info ()) {
      GstReferenceTimestampMeta *rtm = (GstReferenceTimestampMeta *) meta;
      *ts_out = (SeiBoxTimestamp) rtm->timestamp;
      return TRUE;
    }
  }

  return FALSE;
}

static const gchar *
sei_box_codec_name (guint codec_id)
{
  switch ((SeiBoxCodec) codec_id) {
    case SEI_BOX_CODEC_H264:
      return "h264";
    case SEI_BOX_CODEC_H265:
      return "h265";
    default:
      return "unknown";
  }
}

static void
gst_amba_seiinject_update_passthrough (GstAmbaSeiInject *self)
{
  gboolean no_tlv_enabled = (!self->add_timestamp && !self->add_gps);
  gboolean codec_unsupported = (self->codec_id != SEI_BOX_CODEC_H264 &&
      self->codec_id != SEI_BOX_CODEC_H265);
  gboolean should_passthrough = no_tlv_enabled || codec_unsupported;

  gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (self), should_passthrough);
  GST_INFO_OBJECT (self,
      "Transform passthrough %s (add-timestamp=%d add-gps=%d codec=%s)",
      should_passthrough ? "enabled" : "disabled",
      self->add_timestamp ? 1 : 0, self->add_gps ? 1 : 0,
      sei_box_codec_name (self->codec_id));
}

static void
gst_amba_seiinject_fill_info (GstAmbaSeiInject *self, GstBuffer *buffer)
{
  if (!self->info)
    return;

  sei_box_info_reset (self->info);

  if (self->add_timestamp) {
    SeiBoxTimestamp ts_ns = 0;

    if (!gst_amba_seiinject_timestamp_from_reference_meta (buffer, &ts_ns)) {
      GstClockTime pts = GST_BUFFER_PTS (buffer);
      if (GST_CLOCK_TIME_IS_VALID (pts)) {
        ts_ns = (SeiBoxTimestamp) pts;
        GST_DEBUG_OBJECT (self,
            "SEI timestamp fallback: no GstReferenceTimestampMeta, using PTS=%"
            G_GUINT64_FORMAT, (guint64) ts_ns);
      } else {
        pts = GST_BUFFER_DTS (buffer);
        if (GST_CLOCK_TIME_IS_VALID (pts)) {
          ts_ns = (SeiBoxTimestamp) pts;
          GST_DEBUG_OBJECT (self,
              "SEI timestamp fallback: no GstReferenceTimestampMeta, using DTS=%"
              G_GUINT64_FORMAT, (guint64) ts_ns);
        } else {
          GST_WARNING_OBJECT (self,
              "SEI timestamp fallback: no GstReferenceTimestampMeta and no valid "
              "PTS/DTS (timestamp TLV value 0)");
        }
      }
    }
    (void) sei_box_info_set (self->info, SEI_BOX_T_TIMESTAMP,
        &ts_ns, SEI_BOX_INFO_VALUE_SIZE_TIMESTAMP);
  }

  if (self->add_gps) {
    SeiBoxGpsData gps;
    int32_t base_lat = 223000000;   /* 22.3000000 */
    int32_t base_lon = 1141700000;  /* 114.1700000 */
    memset (&gps, 0, sizeof (gps));
    self->gps_tick++;
    gps.valid = (self->gps_device && self->gps_device[0] != '\0') ? 1 : 0;
    gps.lat_e7 = base_lat + (int32_t) (self->gps_tick % 100);
    gps.lon_e7 = base_lon + (int32_t) (self->gps_tick % 100);
    gps.alt_cm = 2500;
    (void) sei_box_info_set (self->info, SEI_BOX_T_GPS, &gps,
        SEI_BOX_INFO_VALUE_SIZE_GPS);
  }
}

static void
gst_amba_seiinject_init (GstAmbaSeiInject *self)
{
  self->add_timestamp = TRUE;
  self->add_gps = FALSE;
  self->self_verify = FALSE;
  self->gps_device = g_strdup ("");
  self->lib_log_level = SEI_BOX_LOG_WARN;
  self->codec_id = SEI_BOX_CODEC_UNKNOWN;
  self->gps_tick = 0;
  self->info = sei_box_info_new ();
  sei_box_init ();
  sei_box_set_log_level ((SeiBoxLogLevel) self->lib_log_level);
  gst_base_transform_set_in_place (GST_BASE_TRANSFORM (self), FALSE);
  gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (self), FALSE);
  GST_INFO_OBJECT (self, "Transform passthrough disabled (default)");
}

static void
gst_amba_seiinject_finalize (GObject *object)
{
  GstAmbaSeiInject *self = GST_AMBA_SEIINJECT (object);
  sei_box_info_free (self->info);
  self->info = NULL;
  g_free (self->gps_device);
  self->gps_device = NULL;
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_amba_seiinject_set_property (GObject *object, guint prop_id,
    const GValue *value, GParamSpec *pspec)
{
  GstAmbaSeiInject *self = GST_AMBA_SEIINJECT (object);

  switch (prop_id) {
    case PROP_ADD_TIMESTAMP:
      self->add_timestamp = g_value_get_boolean (value);
      gst_amba_seiinject_update_passthrough (self);
      break;
    case PROP_ADD_GPS:
      self->add_gps = g_value_get_boolean (value);
      gst_amba_seiinject_update_passthrough (self);
      break;
    case PROP_SELF_VERIFY:
      self->self_verify = g_value_get_boolean (value);
      break;
    case PROP_GPS_DEVICE: {
      const gchar *v = g_value_get_string (value);
      g_free (self->gps_device);
      self->gps_device = g_strdup (v ? v : "");
      break;
    }
    case PROP_LIB_LOG_LEVEL:
      self->lib_log_level = g_value_get_uint (value);
      sei_box_set_log_level ((SeiBoxLogLevel) self->lib_log_level);
      GST_INFO_OBJECT (self, "libsei_box log level set to %u", self->lib_log_level);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_amba_seiinject_get_property (GObject *object, guint prop_id,
    GValue *value, GParamSpec *pspec)
{
  GstAmbaSeiInject *self = GST_AMBA_SEIINJECT (object);

  switch (prop_id) {
    case PROP_ADD_TIMESTAMP:
      g_value_set_boolean (value, self->add_timestamp);
      break;
    case PROP_ADD_GPS:
      g_value_set_boolean (value, self->add_gps);
      break;
    case PROP_SELF_VERIFY:
      g_value_set_boolean (value, self->self_verify);
      break;
    case PROP_GPS_DEVICE:
      g_value_set_string (value, self->gps_device ? self->gps_device : "");
      break;
    case PROP_LIB_LOG_LEVEL:
      g_value_set_uint (value, self->lib_log_level);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstCaps *
gst_amba_seiinject_transform_caps (GstBaseTransform *trans,
    GstPadDirection direction, GstCaps *caps, GstCaps *filter)
{
  (void) trans;
  (void) direction;
  if (filter) {
    GstCaps *intersection = gst_caps_intersect_full (filter, caps,
        GST_CAPS_INTERSECT_FIRST);
    return intersection;
  }
  return gst_caps_ref (caps);
}

static void
set_caps_is_h265 (GstAmbaSeiInject *self, GstCaps *caps)
{
  GstStructure *s;
  const gchar *name;
  if (!caps || gst_caps_is_empty (caps))
    return;
  s = gst_caps_get_structure (caps, 0);
  name = gst_structure_get_name (s);
  if (g_strcmp0 (name, "video/x-h264") == 0)
    self->codec_id = SEI_BOX_CODEC_H264;
  else if (g_strcmp0 (name, "video/x-h265") == 0)
    self->codec_id = SEI_BOX_CODEC_H265;
  else
    self->codec_id = SEI_BOX_CODEC_UNKNOWN;
}

static GstFlowReturn
gst_amba_seiinject_prepare_output_buffer (GstBaseTransform *trans,
    GstBuffer *input, GstBuffer **outbuf)
{
  GstAmbaSeiInject *self = GST_AMBA_SEIINJECT (trans);
  GstBuffer *buf;

  /*
   * Keep prepare_output_buffer aligned with BaseTransform runtime decision:
   * when passthrough is active, forward original AU as-is.
   */
  if (gst_base_transform_is_passthrough (GST_BASE_TRANSFORM (self))) {
    *outbuf = gst_buffer_ref (input);
    return GST_FLOW_OK;
  }

  buf = gst_buffer_new ();
  if (!buf)
    return GST_FLOW_ERROR;
  gst_buffer_copy_into (buf, input, GST_BUFFER_COPY_METADATA, 0, -1);
  *outbuf = buf;
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_amba_seiinject_transform (GstBaseTransform *trans,
    GstBuffer *in_buf, GstBuffer *out_buf)
{
  GstAmbaSeiInject *self = GST_AMBA_SEIINJECT (trans);
  GstMapInfo in_map;
  guint8 *injected = NULL;
  size_t injected_len = 0;
  GstFlowReturn flow = GST_FLOW_OK;
  int rc;
  gboolean in_mapped = FALSE;
  gboolean do_copy_through = FALSE;

  if (!gst_buffer_map (in_buf, &in_map, GST_MAP_READ)) {
    GST_WARNING_OBJECT (self, "Failed to map input buffer, cannot inject");
    flow = GST_FLOW_ERROR;
    goto cleanup;
  }
  in_mapped = TRUE;

  if (!self->info) {
    GST_WARNING_OBJECT (self, "SEI info object unavailable");
    do_copy_through = TRUE;
    goto cleanup;
  }
  gst_amba_seiinject_fill_info (self, in_buf);
  rc = sei_box_inject_au_with_info (in_map.data, in_map.size,
      (SeiBoxCodec) self->codec_id, self->info, &injected, &injected_len);

  if (rc == SEI_BOX_NO_DATA) {
    GST_DEBUG_OBJECT (self, "No SEI payload generated, fallback copy-through");
    do_copy_through = TRUE;
    goto cleanup;
  }

  if (rc < 0 || !injected) {
    GST_WARNING_OBJECT (self, "SEI inject failed (rc=%d), fallback copy-through", rc);
    do_copy_through = TRUE;
    goto cleanup;
  }

  /*
   * Replace output payload with libsei_box-owned injected AU bytes.
   * Ownership is transferred to GstMemory and released via sei_box_free().
   */
  gst_buffer_remove_all_memory (out_buf);
  gst_buffer_append_memory (out_buf,
      gst_memory_new_wrapped (GST_MEMORY_FLAG_READONLY,
          injected, injected_len, 0, injected_len,
          injected, (GDestroyNotify) sei_box_free));
  GST_DEBUG_OBJECT (self,
      "SEI inject success codec=%s in=%" G_GSIZE_FORMAT " out=%" G_GSIZE_FORMAT
      " tlv-mask=0x%" G_GINT64_MODIFIER "x",
      sei_box_codec_name (self->codec_id),
      gst_buffer_get_size (in_buf), injected_len,
      sei_box_info_get_present_mask (self->info));
  // self verify for debug usage
  if (self->self_verify) {
    SeiBoxDecodeMeta meta;
    int parse_rc = sei_box_parse_au_to_info (injected, injected_len,
        (SeiBoxCodec) self->codec_id, self->info, &meta);
    if (parse_rc == SEI_BOX_OK) {
      SeiBoxTimestamp ts_ns = 0;
      SeiBoxGpsData gps;
      int ts_present = sei_box_info_is_present (self->info, SEI_BOX_T_TIMESTAMP);
      int gps_present = sei_box_info_is_present (self->info, SEI_BOX_T_GPS);
      memset (&gps, 0, sizeof (gps));
      if (ts_present)
        (void) sei_box_info_get (self->info, SEI_BOX_T_TIMESTAMP,
            &ts_ns, SEI_BOX_INFO_VALUE_SIZE_TIMESTAMP);
      if (gps_present)
        (void) sei_box_info_get (self->info, SEI_BOX_T_GPS,
            &gps, SEI_BOX_INFO_VALUE_SIZE_GPS);
      GST_DEBUG_OBJECT (self,
          "SEI payload parsed version=%u flags=0x%04x ts_present=%d ts_ns=%"
          G_GUINT64_FORMAT " gps_present=%d gps_valid=%d lat_e7=%d lon_e7=%d alt_cm=%d",
          meta.payload_version, meta.payload_flags,
          ts_present ? 1 : 0,
          (guint64) ts_ns,
          gps_present ? 1 : 0,
          gps.valid, gps.lat_e7, gps.lon_e7, gps.alt_cm);
    } else {
      GST_WARNING_OBJECT (self,
          "SEI payload parse after inject failed rc=%d", parse_rc);
    }
  }
  injected = NULL;

cleanup:
  if (in_mapped)
    gst_buffer_unmap (in_buf, &in_map);
  if (injected)
    sei_box_free (injected);
  if (do_copy_through) {
    if (out_buf != in_buf) {
      gst_buffer_copy_into (out_buf, in_buf, GST_BUFFER_COPY_ALL, 0, -1);
      gst_buffer_resize (out_buf, 0, gst_buffer_get_size (in_buf));
    }
    flow = GST_FLOW_OK;
  }
  return flow;
}

/* Called when caps are set; we need to know is_h265 from negotiated caps. */
static gboolean
gst_amba_seiinject_set_caps (GstBaseTransform *trans, GstCaps *incaps,
    GstCaps *outcaps)
{
  GstAmbaSeiInject *self = GST_AMBA_SEIINJECT (trans);
  (void) outcaps;
  set_caps_is_h265 (self, incaps);
  gst_amba_seiinject_update_passthrough (self);
  return TRUE;
}

static void
gst_amba_seiinject_class_init (GstAmbaSeiInjectClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_amba_seiinject_debug, "ambaseiinject", 0,
      "Amba SEI inject: timestamp and GPS in video bitstream");

  gobject_class->finalize = gst_amba_seiinject_finalize;
  gobject_class->set_property = gst_amba_seiinject_set_property;
  gobject_class->get_property = gst_amba_seiinject_get_property;

  g_object_class_install_property (gobject_class, PROP_ADD_TIMESTAMP,
      g_param_spec_boolean ("add-timestamp", "Add timestamp",
          "Add timestamp to SEI (custom UUID)", TRUE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ADD_GPS,
      g_param_spec_boolean ("add-gps", "Add GPS",
          "Add GPS to SEI (custom UUID)", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SELF_VERIFY,
      g_param_spec_boolean ("self-verify", "Self verify",
          "Parse injected AU for self-check debug logging", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_GPS_DEVICE,
      g_param_spec_string ("gps-device", "GPS device",
          "Serial device for GPS NMEA (e.g. /dev/ttyUSB0). Empty to disable.",
          "", G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_LIB_LOG_LEVEL,
      g_param_spec_uint ("lib-log-level", "Lib log level",
          "Global libsei_box log level: 0=OFF,1=ERROR,2=WARN,3=INFO,4=DEBUG,5=TRACE",
          SEI_BOX_LOG_OFF, SEI_BOX_LOG_TRACE, SEI_BOX_LOG_WARN,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element_class,
      "Amba SEI inject",
      "Filter/Video",
      "Inject timestamp and GPS into H.264/H.265 bitstream via SEI",
      "Yang Yu <yyua@ambarella.com>");

  gst_element_class_add_static_pad_template (element_class, &sink_factory);
  gst_element_class_add_static_pad_template (element_class, &src_factory);

  trans_class->transform_caps = GST_DEBUG_FUNCPTR (gst_amba_seiinject_transform_caps);
  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_amba_seiinject_set_caps);
  trans_class->prepare_output_buffer = GST_DEBUG_FUNCPTR (gst_amba_seiinject_prepare_output_buffer);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_amba_seiinject_transform);
}