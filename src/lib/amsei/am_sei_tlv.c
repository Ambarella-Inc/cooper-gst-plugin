/*
 * am_sei_tlv.c
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

#include "am_sei_internal.h"

#include <stddef.h>
#include <string.h>

#define AM_SEI_TLV_TYPE_TO_MASK(type) \
  (((type) >= 1 && (type) <= 64) ? (1ULL << ((type) - 1)) : 0ULL)

/* Fixed 16-byte producer identifier used in SEI user_data_unregistered.
 * - Current bytes encode ASCII "amba-sei-v1-0001" for readability.
 * Keep stable unless intentionally introducing a new incompatible family. */
const uint8_t am_sei_uuid[AM_SEI_UUID_BYTES] = {
  0x61, 0x6d, 0x62, 0x61, 0x2d, 0x73, 0x65, 0x69,
  0x2d, 0x76, 0x31, 0x2d, 0x30, 0x30, 0x30, 0x31
};

void
am_sei_put_le16 (uint8_t *p, uint16_t v)
{
  p[0] = (uint8_t) (v);
  p[1] = (uint8_t) (v >> 8);
}

void
am_sei_put_le32 (uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t) (v);
  p[1] = (uint8_t) (v >> 8);
  p[2] = (uint8_t) (v >> 16);
  p[3] = (uint8_t) (v >> 24);
}

void
am_sei_put_le64 (uint8_t *p, uint64_t v)
{
  p[0] = (uint8_t) (v);
  p[1] = (uint8_t) (v >> 8);
  p[2] = (uint8_t) (v >> 16);
  p[3] = (uint8_t) (v >> 24);
  p[4] = (uint8_t) (v >> 32);
  p[5] = (uint8_t) (v >> 40);
  p[6] = (uint8_t) (v >> 48);
  p[7] = (uint8_t) (v >> 56);
}

uint16_t
am_sei_get_le16 (const uint8_t *p)
{
  return (uint16_t) p[0] | ((uint16_t) p[1] << 8);
}

uint32_t
am_sei_get_le32 (const uint8_t *p)
{
  return (uint32_t) p[0] | ((uint32_t) p[1] << 8) |
      ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

uint64_t
am_sei_get_le64 (const uint8_t *p)
{
  return (uint64_t) p[0] | ((uint64_t) p[1] << 8) |
      ((uint64_t) p[2] << 16) | ((uint64_t) p[3] << 24) |
      ((uint64_t) p[4] << 32) | ((uint64_t) p[5] << 40) |
      ((uint64_t) p[6] << 48) | ((uint64_t) p[7] << 56);
}

static int
default_encode_timestamp (const AmSeiInfo *info, uint8_t *out,
    uint16_t out_max, uint16_t *out_len, void *user_data)
{
  /* Wire format for AM_SEI_T_TIMESTAMP: little-endian uint64 nanoseconds. */
  AmSeiTimestamp ts_ns = 0;
  (void) user_data;
  if (!out || !out_len || out_max < AM_SEI_TIMESTAMP_BYTES)
    return AM_SEI_EINVAL;
  if (info) {
    (void) am_sei_info_get (info, AM_SEI_T_TIMESTAMP, &ts_ns, sizeof (ts_ns));
  }
  am_sei_put_le64 (out, ts_ns);
  *out_len = AM_SEI_TIMESTAMP_BYTES;
  return AM_SEI_OK;
}

static int
default_encode_gps (const AmSeiInfo *info, uint8_t *out,
    uint16_t out_max, uint16_t *out_len, void *user_data)
{
  /* Wire format for AM_SEI_T_GPS: [valid:1][lat_e7:4][lon_e7:4][alt_cm:4]. */
  AmSeiGpsData gps_fix;
  (void) user_data;
  if (!out || !out_len || out_max < AM_SEI_GPS_BYTES)
    return AM_SEI_EINVAL;
  memset (&gps_fix, 0, sizeof (gps_fix));
  if (info) {
    (void) am_sei_info_get (info, AM_SEI_T_GPS, &gps_fix, sizeof (gps_fix));
  }
  out[0] = gps_fix.valid ? 1 : 0;
  am_sei_put_le32 (out + 1, (uint32_t) gps_fix.lat_e7);
  am_sei_put_le32 (out + 5, (uint32_t) gps_fix.lon_e7);
  am_sei_put_le32 (out + 9, (uint32_t) gps_fix.alt_cm);
  *out_len = AM_SEI_GPS_BYTES;
  return AM_SEI_OK;
}

static int
default_decode_timestamp (const uint8_t *data, uint16_t len,
    AmSeiInfo *out_info, void *user_data)
{
  /* Keep decode strict on fixed size to reject corrupted/truncated payload. */
  AmSeiTimestamp ts_ns;
  (void) user_data;
  if (!data || !out_info || len != AM_SEI_TIMESTAMP_BYTES)
    return AM_SEI_EFORMAT;
  ts_ns = am_sei_get_le64 (data);
  return am_sei_info_set (out_info, AM_SEI_T_TIMESTAMP, &ts_ns, sizeof (ts_ns));
}

static int
default_decode_gps (const uint8_t *data, uint16_t len,
    AmSeiInfo *out_info, void *user_data)
{
  /* Decode must mirror default_encode_gps() byte layout exactly. */
  AmSeiGpsData gps;
  (void) user_data;
  if (!data || !out_info || len != AM_SEI_GPS_BYTES)
    return AM_SEI_EFORMAT;
  memset (&gps, 0, sizeof (gps));
  gps.valid = data[0] ? 1 : 0;
  gps.lat_e7 = (int32_t) am_sei_get_le32 (data + 1);
  gps.lon_e7 = (int32_t) am_sei_get_le32 (data + 5);
  gps.alt_cm = (int32_t) am_sei_get_le32 (data + 9);
  return am_sei_info_set (out_info, AM_SEI_T_GPS, &gps, sizeof (gps));
}

size_t
am_sei_build_payload_blob (uint8_t *out, size_t out_max,
    uint64_t enabled_mask, const AmSeiInfo *info)
{
  uint16_t flags = 0;
  size_t off = 0;
  size_t blob_start;
  uint16_t value_len = 0;
  int rc;

  if (!out || out_max < AM_SEI_PAYLOAD_HEADER_BYTES) {
    AM_SEI_LOGE ("build_payload_blob invalid buffer out=%p out_max=%zu",
        (void *) out, out_max);
    return 0;
  }
  /* Blob layout:
   * [magic(4)][version(2)][flags(2)][blob_len(4)][TLV...]
   * Keep this contract in sync with am_sei_parse_payload_blob(). */
  out[off++] = AM_SEI_MAGIC_0;
  out[off++] = AM_SEI_MAGIC_1;
  out[off++] = AM_SEI_MAGIC_2;
  out[off++] = AM_SEI_MAGIC_3;
  am_sei_put_le16 (out + off, AM_SEI_PAYLOAD_VERSION_V1);
  off += 2;
  am_sei_put_le16 (out + off, 0);
  off += 2;
  blob_start = off;
  am_sei_put_le32 (out + off, 0);
  off += 4;

  if (enabled_mask & AM_SEI_TLVF_TIMESTAMP) {
    if (off + AM_SEI_TLV_HEADER_BYTES >= out_max)
      return 0;
    rc = default_encode_timestamp (info, out + off + AM_SEI_TLV_HEADER_BYTES,
        (uint16_t) ((out_max - off - AM_SEI_TLV_HEADER_BYTES) > 0xffff ?
            0xffff : (out_max - off - AM_SEI_TLV_HEADER_BYTES)),
        &value_len, NULL);
    if (rc != AM_SEI_NO_DATA && value_len > 0) {
      /* Built-in TLV extension point: add a new block mirroring this pattern
       * (encode -> write type/len -> advance off -> set header flag bit). */
      if (rc != AM_SEI_OK || off + AM_SEI_TLV_HEADER_BYTES + value_len > out_max)
        return 0;
      am_sei_put_le16 (out + off, AM_SEI_T_TIMESTAMP);
      am_sei_put_le16 (out + off + 2, value_len);
      off += AM_SEI_TLV_HEADER_BYTES + value_len;
      flags |= (uint16_t) AM_SEI_FLAG_HAS_TIMESTAMP;
    }
  }

  if (enabled_mask & AM_SEI_TLVF_GPS) {
    if (off + AM_SEI_TLV_HEADER_BYTES >= out_max)
      return 0;
    rc = default_encode_gps (info, out + off + AM_SEI_TLV_HEADER_BYTES,
        (uint16_t) ((out_max - off - AM_SEI_TLV_HEADER_BYTES) > 0xffff ?
            0xffff : (out_max - off - AM_SEI_TLV_HEADER_BYTES)),
        &value_len, NULL);
    if (rc != AM_SEI_NO_DATA && value_len > 0) {
      if (rc != AM_SEI_OK || off + AM_SEI_TLV_HEADER_BYTES + value_len > out_max)
        return 0;
      am_sei_put_le16 (out + off, AM_SEI_T_GPS);
      am_sei_put_le16 (out + off + 2, value_len);
      off += AM_SEI_TLV_HEADER_BYTES + value_len;
      flags |= (uint16_t) AM_SEI_FLAG_HAS_GPS;
    }
  }

  if (flags == 0)
    return 0;

  /* Header flags summarize built-in TLVs actually serialized in this blob. */
  am_sei_put_le16 (out + 6, flags);
  am_sei_put_le32 (out + blob_start, (uint32_t) (off - (blob_start + 4)));
  AM_SEI_LOGD ("payload built len=%zu flags=0x%04x enabled-mask=0x%llx",
      off, flags, (unsigned long long) enabled_mask);
  return off;
}

int
am_sei_parse_payload_blob (const uint8_t *payload, size_t payload_len,
    uint64_t enabled_mask, AmSeiInfo *out_info,
    uint16_t *out_payload_version, uint16_t *out_payload_flags)
{
  size_t off = 0;
  uint32_t blob_len;
  int decoded_any = 0;

  if (!payload || !out_info || payload_len < AM_SEI_PAYLOAD_HEADER_BYTES) {
    AM_SEI_LOGE ("parse_payload_blob invalid args payload=%p out_info=%p len=%zu",
        (const void *) payload, (void *) out_info, payload_len);
    return AM_SEI_EFORMAT;
  }

  if (payload[0] != AM_SEI_MAGIC_0 || payload[1] != AM_SEI_MAGIC_1 ||
      payload[2] != AM_SEI_MAGIC_2 || payload[3] != AM_SEI_MAGIC_3) {
    AM_SEI_LOGE ("parse_payload_blob magic mismatch");
    return AM_SEI_EFORMAT;
  }

  am_sei_info_reset (out_info);
  if (out_payload_version)
    *out_payload_version = 0;
  if (out_payload_flags)
    *out_payload_flags = 0;
  off = 4;
  if (out_payload_version)
    *out_payload_version = am_sei_get_le16 (payload + off);
  off += 2;
  if (out_payload_flags)
    *out_payload_flags = am_sei_get_le16 (payload + off);
  off += 2;
  blob_len = am_sei_get_le32 (payload + off);
  off += 4;

  if (off + blob_len > payload_len) {
    AM_SEI_LOGE ("parse_payload_blob blob_len overflow blob_len=%u payload_len=%zu",
        blob_len, payload_len);
    return AM_SEI_EFORMAT;
  }

  while (off + AM_SEI_TLV_HEADER_BYTES <= payload_len) {
    uint16_t type = am_sei_get_le16 (payload + off);
    uint16_t len = am_sei_get_le16 (payload + off + 2);
    const uint8_t *data;

    off += AM_SEI_TLV_HEADER_BYTES;
    if (type == AM_SEI_T_INVALID)
      break;
    if (off + len > payload_len) {
      AM_SEI_LOGE ("parse_payload_blob tlv length overflow type=%u len=%u",
          (unsigned) type, (unsigned) len);
      return AM_SEI_EFORMAT;
    }

    data = payload + off;
    if (enabled_mask & AM_SEI_TLV_TYPE_TO_MASK (type)) {
      /* Decoder dispatch for built-in TLVs.
       * Unknown or disabled types are skipped (forward compatibility).
       *
       * Built-in TLV extension checklist:
       * 1) Add type/value contract in am_sei.h (enum + value size macro).
       * 2) Add storage + set/get path in am_sei_api.c (AmSeiInfo).
       * 3) Add encode branch in am_sei_build_payload_blob() and update flags.
       * 4) Add decode branch below and keep wire format symmetric with encode.
       * 5) Update inject payload_max estimation in am_sei_inject.c. */
      int rc = AM_SEI_UNSUPPORTED_TYPE;
      switch (type) {
        case AM_SEI_T_TIMESTAMP:
          rc = default_decode_timestamp (data, len, out_info, NULL);
          break;
        case AM_SEI_T_GPS:
          rc = default_decode_gps (data, len, out_info, NULL);
          break;
        default:
          rc = AM_SEI_UNSUPPORTED_TYPE;
          break;
      }
      if (rc != AM_SEI_OK && rc != AM_SEI_NO_DATA &&
          rc != AM_SEI_UNSUPPORTED_TYPE)
        return rc;
      if (rc == AM_SEI_OK)
        decoded_any = 1;
    }

    off += len;
  }

  if (decoded_any) {
    return AM_SEI_OK;
  }
  AM_SEI_LOGD ("parse_payload_blob no enabled built-in tlv decoded");
  return AM_SEI_NO_DATA;
}
