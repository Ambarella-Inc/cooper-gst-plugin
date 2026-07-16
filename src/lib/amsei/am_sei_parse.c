/*
 * am_sei_parse.c
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
#include <stdlib.h>
#include <string.h>

static ptrdiff_t
find_start_code (const uint8_t *data, size_t len, size_t from, size_t *sc_len)
{
  size_t i = from;

  while (i + AM_SEI_START_CODE3_BYTES <= len) {
    if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) {
      *sc_len = AM_SEI_START_CODE3_BYTES;
      return (ptrdiff_t) i;
    }
    if (i + AM_SEI_START_CODE4_BYTES <= len && data[i] == 0 && data[i + 1] == 0 &&
        data[i + 2] == 0 && data[i + 3] == 1) {
      *sc_len = AM_SEI_START_CODE4_BYTES;
      return (ptrdiff_t) i;
    }
    i++;
  }
  return -1;
}

static size_t
decode_payload_size (const uint8_t *data, size_t len, size_t *consumed)
{
  size_t i = 0;
  size_t sz = 0;

  while (i < len) {
    sz += data[i];
    if (data[i] != 255) {
      *consumed = i + 1;
      return sz;
    }
    i++;
  }
  *consumed = 0;
  return 0;
}

static size_t
ebsp_to_rbsp (const uint8_t *ebsp, size_t ebsp_len, uint8_t *rbsp, size_t rbsp_max)
{
  size_t i;
  size_t j = 0;
  int zero_count = 0;

  for (i = 0; i < ebsp_len; ++i) {
    uint8_t b = ebsp[i];

    if (zero_count == 2 && b == 0x03) {
      zero_count = 0;
      continue;
    }
    if (j >= rbsp_max)
      return 0;
    rbsp[j++] = b;
    if (b == 0x00)
      zero_count++;
    else
      zero_count = 0;
  }

  return j;
}

static int
parse_sei_rbsp (const uint8_t *rbsp, size_t rbsp_len, uint64_t enabled_mask,
    AmSeiInfo *out_info, uint16_t *out_payload_version, uint16_t *out_payload_flags)
{
  size_t off = 0;
  AM_SEI_LOGD ("parse_sei_rbsp begin len=%zu", rbsp_len);

  while (off + 1 <= rbsp_len) {
    uint32_t payload_type = 0;
    size_t payload_size;
    size_t size_len;
    const uint8_t *payload;
    size_t type_len = 0;
    int rc;

    /* payload_type/payload_size in SEI use 0xff extension bytes. */
    while (off + type_len < rbsp_len && rbsp[off + type_len] == 0xff) {
      payload_type += 0xff;
      type_len++;
    }
    if (off + type_len >= rbsp_len)
      break;
    payload_type += rbsp[off + type_len];
    type_len++;
    off += type_len;
    payload_size = decode_payload_size (rbsp + off, rbsp_len - off, &size_len);
    if (size_len == 0)
      break;
    off += size_len;
    if (off + payload_size > rbsp_len)
      break;

    payload = rbsp + off;
    /* Only parse UUID-matched user_data_unregistered payload.
     * Unknown SEI messages are ignored to keep parser tolerant. */
    if (payload_type == AM_SEI_SEI_PAYLOAD_TYPE_USER_DATA_UNREGISTERED &&
        payload_size > AM_SEI_UUID_BYTES &&
        memcmp (payload, am_sei_uuid, AM_SEI_UUID_BYTES) == 0) {
      rc = am_sei_parse_payload_blob (payload + AM_SEI_UUID_BYTES,
          payload_size - AM_SEI_UUID_BYTES, enabled_mask, out_info,
          out_payload_version, out_payload_flags);
      AM_SEI_LOGD ("target UUID SEI found payload_size=%zu rc=%d", payload_size, rc);
      return rc;
    }

    off += payload_size;
  }

  AM_SEI_LOGT ("target UUID SEI not found in rbsp");
  return AM_SEI_NO_DATA;
}

int
am_sei_parse_au_internal (const uint8_t *au, size_t au_len, AmSeiCodec codec,
    uint64_t enabled_mask, AmSeiInfo *out_info,
    uint16_t *out_payload_version, uint16_t *out_payload_flags)
{
  size_t cursor = 0;
  AM_SEI_LOGD ("parse_au begin codec=%d len=%zu", (int) codec, au_len);

  if (!au || !out_info) {
    AM_SEI_LOGE ("parse_au invalid args au=%p out_info=%p",
        (const void *) au, (void *) out_info);
    return AM_SEI_EINVAL;
  }
  if (codec != AM_SEI_CODEC_H264 && codec != AM_SEI_CODEC_H265) {
    AM_SEI_LOGE ("parse_au unsupported codec=%d", (int) codec);
    return AM_SEI_EUNSUPPORTED;
  }

  am_sei_info_reset (out_info);
  if (out_payload_version)
    *out_payload_version = 0;
  if (out_payload_flags)
    *out_payload_flags = 0;

  while (cursor < au_len) {
    size_t sc_len = 0;
    ptrdiff_t sc_pos = find_start_code (au, au_len, cursor, &sc_len);
    ptrdiff_t next_sc_pos;
    size_t nal_start;
    size_t nal_end;
    uint8_t nal_type;
    const uint8_t *rbsp;
    size_t rbsp_len;
    int rc;

    if (sc_pos < 0)
      break;
    nal_start = (size_t) sc_pos + sc_len;
    if (nal_start >= au_len)
      break;

    next_sc_pos = find_start_code (au, au_len, nal_start, &sc_len);
    nal_end = (next_sc_pos < 0) ? au_len : (size_t) next_sc_pos;
    if (nal_end <= nal_start) {
      cursor = nal_start + 1;
      continue;
    }

    if (codec == AM_SEI_CODEC_H265) {
      nal_type = (uint8_t) ((au[nal_start] >> 1) & 0x3f);
      if (nal_type != AM_SEI_H265_NAL_TYPE_PREFIX_SEI ||
          nal_end - nal_start <= 2) {
        cursor = nal_end;
        continue;
      }
      rbsp = au + nal_start + 2;
      rbsp_len = nal_end - nal_start - 2;
    } else {
      nal_type = (uint8_t) (au[nal_start] & 0x1f);
      if (nal_type != AM_SEI_H264_NAL_TYPE_SEI || nal_end <= nal_start + 1) {
        cursor = nal_end;
        continue;
      }
      rbsp = au + nal_start + 1;
      rbsp_len = nal_end - nal_start - 1;
    }

    {
      /* NAL payload is EBSP; convert to RBSP before parsing SEI syntax. */
      uint8_t *rbsp_buf = (uint8_t *) malloc (rbsp_len);
      size_t unescaped_len;
      if (!rbsp_buf) {
        AM_SEI_LOGE ("rbsp malloc failed, size=%zu", rbsp_len);
        return AM_SEI_ENOMEM;
      }
      unescaped_len = ebsp_to_rbsp (rbsp, rbsp_len, rbsp_buf, rbsp_len);
      if (unescaped_len == 0) {
        AM_SEI_LOGE ("ebsp_to_rbsp failed for nal_type=%u len=%zu", nal_type, rbsp_len);
        free (rbsp_buf);
        cursor = nal_end;
        continue;
      }
      rc = parse_sei_rbsp (rbsp_buf, unescaped_len, enabled_mask, out_info,
          out_payload_version, out_payload_flags);
      free (rbsp_buf);
    }
    if (rc == AM_SEI_OK) {
      AM_SEI_LOGD ("parse_au success");
      return AM_SEI_OK;
    }
    if (rc == AM_SEI_EFORMAT) {
      AM_SEI_LOGD ("parse_au format error");
      return AM_SEI_EFORMAT;
    }

    cursor = nal_end;
  }

  AM_SEI_LOGD ("parse_au no target sei");
  return AM_SEI_NO_DATA;
}

