/*
 * sei.c
 *
 * SEI build and inject for H.264 and H.265 byte stream (Annex B).
 *
 * Copyright (C) 2025 Ambarella International LP
 */

#include "sei.h"
#include <string.h>

/* UUID for timestamp SEI (user-defined 16 bytes) */
const guint8 sei_uuid_timestamp[SEI_UUID_TIMESTAMP_LEN] = {
  0x61, 0x6d, 0x62, 0x61, 0x2d, 0x74, 0x73, 0x2d,  /* "amba-ts-" */
  0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38   /* "12345678" */
};

/* UUID for GPS SEI (user-defined 16 bytes) */
const guint8 sei_uuid_gps[SEI_UUID_GPS_LEN] = {
  0x61, 0x6d, 0x62, 0x61, 0x2d, 0x67, 0x70, 0x73,  /* "amba-gps" */
  0x2d, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37   /* "-1234567" */
};

/* SEI payload type: User data unregistered (ITU-T H.264 table 7-1, H.265 table 7-1) */
#define SEI_USER_DATA_UNREGISTERED  5

/* Variable-length size encoding: bytes 0-254 = size, 255 = add 255 and continue */
static size_t
sei_encode_payload_size (guint8 *out, size_t out_max, size_t size)
{
  size_t n = 0;
  while (size > 254 && n < out_max) {
    out[n++] = 255;
    size -= 255;
  }
  if (n < out_max && size <= 254)
    out[n++] = (guint8) size;
  return n;
}

/* Build one SEI message: payload_type (1) + payload_size (var) + payload_content */
static size_t
sei_build_one_message (guint8 *buf, size_t buf_size, guint8 payload_type,
    const guint8 *payload, size_t payload_len)
{
  size_t off = 0;
  guint8 size_buf[8];
  size_t size_len;

  if (buf_size < 2)
    return 0;

  buf[off++] = payload_type;
  size_len = sei_encode_payload_size (size_buf, sizeof (size_buf), payload_len);
  if (off + size_len + payload_len > buf_size)
    return 0;
  memcpy (buf + off, size_buf, size_len);
  off += size_len;
  if (payload_len)
    memcpy (buf + off, payload, payload_len);
  off += payload_len;
  return off;
}

size_t
sei_build_user_data_unregistered (guint8 *buf, size_t buf_size,
    const guint8 *uuid, const guint8 *payload, size_t payload_len)
{
  /* SEI payload = 16-byte UUID + custom payload */
  size_t content_len = 16 + payload_len;
  guint8 content[256];
  size_t content_max = sizeof (content);

  if (content_len > content_max || buf_size < 2)
    return 0;
  memcpy (content, uuid, 16);
  if (payload_len)
    memcpy (content + 16, payload, payload_len);
  return sei_build_one_message (buf, buf_size, SEI_USER_DATA_UNREGISTERED,
      content, content_len);
}

/* Annex B start code (3 or 4 bytes) */
#define START_CODE_3 0x000001
#define START_CODE_4 0x00000001

static const guint8 start_code_4[] = { 0x00, 0x00, 0x00, 0x01 };

/* Find next NAL start in Annex B stream. Returns offset of first byte of NAL (after start code), or -1. */
static gssize
find_nal_start (const guint8 *data, gsize size, gsize from)
{
  gsize i = from;
  while (i + 3 <= size) {
    if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) {
      return (gssize) (i + 3);
    }
    if (i + 4 <= size && data[i] == 0 && data[i + 1] == 0 &&
        data[i + 2] == 0 && data[i + 3] == 1) {
      return (gssize) (i + 4);
    }
    i++;
  }
  return -1;
}

/* Get NAL type for H.264 (first byte after start code & 0x1F) */
static guint8
h264_nal_type (const guint8 *nal)
{
  return nal[0] & 0x1F;
}

/* Get NAL type for H.265 (first byte >> 1) */
static guint8
h265_nal_type (const guint8 *nal)
{
  return (nal[0] >> 1) & 0x3F;
}

/* H.264: VCL NAL types 1-5 (slice). H.265: VCL NAL types 0-9. */
static gboolean
is_vcl_nal_h264 (guint8 type)
{
  return type >= 1 && type <= 5;
}

static gboolean
is_vcl_nal_h265 (guint8 type)
{
  return type <= 9;
}

/* Compute total size of SEI NAL(s) we will inject (start code + NAL header + payloads). */
static size_t
sei_nal_total_size (gboolean is_h265,
    size_t timestamp_len, size_t gps_len)
{
  size_t total = 0;
  guint8 dummy[256];
  size_t n;

  /* One NAL with both payloads (two user_data_unregistered in one SEI NAL). */
  total += 4;  /* start code */
  if (is_h265)
    total += 2;  /* HEVC NAL header */
  else
    total += 1;  /* H.264 NAL type byte (0x06) */

  if (timestamp_len > 0) {
    n = sei_build_user_data_unregistered (dummy, sizeof (dummy),
        sei_uuid_timestamp, NULL, timestamp_len);
    total += n;
  }
  if (gps_len > 0) {
    n = sei_build_user_data_unregistered (dummy, sizeof (dummy),
        sei_uuid_gps, NULL, gps_len);
    total += n;
  }
  return total;
}

/* No helper: we build a new buffer and copy. */

/* Build SEI NAL into pre-allocated buffer. Returns bytes written. */
static size_t
build_sei_nal (guint8 *out, size_t out_max, gboolean is_h265,
    const guint8 *ts_payload, size_t ts_len,
    const guint8 *gps_payload, size_t gps_len)
{
  size_t off = 0;
  size_t n;

  if (out_max < 8)
    return 0;

  memcpy (out, start_code_4, 4);
  off += 4;

  if (is_h265) {
    out[off++] = (39 << 1);   /* NAL type 39 = PREFIX_SEI */
    out[off++] = 0x01;        /* nuh_temporal_id_plus1 */
  } else {
    out[off++] = 0x06;        /* H.264 NAL type 6 = SEI */
  }

  if (ts_len > 0) {
    n = sei_build_user_data_unregistered (out + off, out_max - off,
        sei_uuid_timestamp, ts_payload, ts_len);
    if (n == 0)
      return 0;
    off += n;
  }
  if (gps_len > 0) {
    n = sei_build_user_data_unregistered (out + off, out_max - off,
        sei_uuid_gps, gps_payload, gps_len);
    if (n == 0)
      return 0;
    off += n;
  }
  return off;
}

/* Find offset (in bytes) where we should insert SEI: before first VCL NAL. */
static gssize
find_insert_offset (const guint8 *data, gsize size, gboolean is_h265)
{
  gsize from = 0;
  gssize nal_start;
  guint8 type;
  int start_code_len;

  while (1) {
    nal_start = find_nal_start (data, size, from);
    if (nal_start < 0)
      return -1;
    type = is_h265 ? h265_nal_type (data + nal_start) : h264_nal_type (data + nal_start);
    if (is_h265 ? is_vcl_nal_h265 (type) : is_vcl_nal_h264 (type)) {
      if ((gsize)nal_start >= 4 && data[nal_start - 4] == 0 &&
          data[nal_start - 3] == 0 && data[nal_start - 2] == 0 && data[nal_start - 1] == 1)
        start_code_len = 4;
      else
        start_code_len = 3;
      return (gssize) (nal_start - start_code_len);
    }
    from = (gsize) nal_start + 1;
    if (from >= size)
      return -1;
  }
}

GstBuffer *
sei_inject_into_h264 (GstBuffer *buffer,
    const guint8 *timestamp_payload, size_t timestamp_len,
    const guint8 *gps_payload, size_t gps_len)
{
  return sei_inject_into_buffer (buffer, FALSE,
      timestamp_payload, timestamp_len, gps_payload, gps_len);
}

GstBuffer *
sei_inject_into_h265 (GstBuffer *buffer,
    const guint8 *timestamp_payload, size_t timestamp_len,
    const guint8 *gps_payload, size_t gps_len)
{
  return sei_inject_into_buffer (buffer, TRUE,
      timestamp_payload, timestamp_len, gps_payload, gps_len);
}

GstBuffer *
sei_inject_into_buffer (GstBuffer *buffer, gboolean is_h265,
    const guint8 *timestamp_payload, size_t timestamp_len,
    const guint8 *gps_payload, size_t gps_len)
{
  GstMapInfo map;
  gsize buf_size;
  const guint8 *data;
  gssize insert_off;
  size_t sei_size;
  guint8 *sei_nal = NULL;
  GstBuffer *out_buf = NULL;
  GstMapInfo out_map;

  if ((!timestamp_payload || timestamp_len == 0) &&
      (!gps_payload || gps_len == 0))
    return gst_buffer_ref (buffer);

  sei_size = sei_nal_total_size (is_h265, timestamp_len, gps_len);
  sei_nal = (guint8 *) g_malloc (sei_size);
  if (!sei_nal)
    return NULL;

  sei_size = build_sei_nal (sei_nal, sei_size, is_h265,
      timestamp_payload, timestamp_len, gps_payload, gps_len);
  if (sei_size == 0)
    goto fail;

  if (!gst_buffer_map (buffer, &map, GST_MAP_READ))
    goto fail;
  data = map.data;
  buf_size = map.size;
  insert_off = find_insert_offset (data, buf_size, is_h265);
  gst_buffer_unmap (buffer, &map);

  if (insert_off < 0)
    insert_off = (gssize) buf_size;

  out_buf = gst_buffer_new_allocate (NULL, buf_size + sei_size, NULL);
  if (!out_buf)
    goto fail;
  if (!gst_buffer_map (out_buf, &out_map, GST_MAP_WRITE)) {
    gst_buffer_unref (out_buf);
    goto fail;
  }
  if (!gst_buffer_map (buffer, &map, GST_MAP_READ)) {
    gst_buffer_unmap (out_buf, &out_map);
    gst_buffer_unref (out_buf);
    goto fail;
  }
  data = map.data;
  memcpy (out_map.data, data, (size_t) insert_off);
  memcpy (out_map.data + insert_off, sei_nal, sei_size);
  memcpy (out_map.data + insert_off + sei_size, data + insert_off,
      buf_size - insert_off);
  gst_buffer_unmap (buffer, &map);
  gst_buffer_unmap (out_buf, &out_map);
  gst_buffer_copy_into (out_buf, buffer, GST_BUFFER_COPY_METADATA, 0, -1);
  g_free (sei_nal);
  return out_buf;

fail:
  g_free (sei_nal);
  return NULL;
}
