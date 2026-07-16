/*
 * am_sei_inject.c
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

static const uint8_t k_start_code4[] = { 0x00, 0x00, 0x00, 0x01 };

static size_t
encode_payload_size (uint8_t *out, size_t out_max, size_t size)
{
  size_t n = 0;

  while (size > 254 && n < out_max) {
    out[n++] = 0xff;
    size -= 0xff;
  }
  if (n < out_max)
    out[n++] = (uint8_t) size;
  return n;
}

static size_t
build_user_data_unregistered (uint8_t *out, size_t out_max,
    const uint8_t *uuid, const uint8_t *payload, size_t payload_len)
{
  uint8_t size_buf[16];
  size_t off = 0;
  size_t size_len;
  size_t content_len = AM_SEI_UUID_BYTES + payload_len;

  if (!out || !uuid || !payload || out_max < 2)
    return 0;

  out[off++] = AM_SEI_SEI_PAYLOAD_TYPE_USER_DATA_UNREGISTERED;
  size_len = encode_payload_size (size_buf, sizeof (size_buf), content_len);
  if (off + size_len + content_len > out_max)
    return 0;

  memcpy (out + off, size_buf, size_len);
  off += size_len;
  memcpy (out + off, uuid, AM_SEI_UUID_BYTES);
  off += AM_SEI_UUID_BYTES;
  memcpy (out + off, payload, payload_len);
  off += payload_len;
  return off;
}

static size_t
rbsp_to_ebsp (const uint8_t *rbsp, size_t rbsp_len, uint8_t *ebsp, size_t ebsp_max)
{
  size_t i;
  size_t j = 0;
  int zero_count = 0;

  for (i = 0; i < rbsp_len; ++i) {
    uint8_t b = rbsp[i];
    if (zero_count >= 2 && b <= 0x03) {
      if (j >= ebsp_max)
        return 0;
      ebsp[j++] = 0x03;
      zero_count = 0;
    }
    if (j >= ebsp_max)
      return 0;
    ebsp[j++] = b;
    if (b == 0x00)
      zero_count++;
    else
      zero_count = 0;
  }

  return j;
}

static int
is_vcl_nal_h264 (uint8_t type)
{
  return type >= AM_SEI_H264_VCL_TYPE_MIN && type <= AM_SEI_H264_VCL_TYPE_MAX;
}

static int
is_vcl_nal_h265 (uint8_t type)
{
  return type <= AM_SEI_H265_VCL_TYPE_MAX;
}

static ptrdiff_t
find_nal_start (const uint8_t *data, size_t len, size_t from)
{
  size_t i = from;
  while (i + AM_SEI_START_CODE3_BYTES <= len) {
    if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1)
      return (ptrdiff_t) (i + AM_SEI_START_CODE3_BYTES);
    if (i + AM_SEI_START_CODE4_BYTES <= len && data[i] == 0 && data[i + 1] == 0 &&
        data[i + 2] == 0 && data[i + 3] == 1) {
      return (ptrdiff_t) (i + AM_SEI_START_CODE4_BYTES);
    }
    i++;
  }
  return -1;
}

static ptrdiff_t
find_insert_offset (const uint8_t *data, size_t len, AmSeiCodec codec)
{
  size_t from = 0;

  /* Insert before first VCL NAL to keep decoder behavior stable and ensure
   * SEI applies to the current AU regardless of frame type ordering. */
  while (1) {
    ptrdiff_t nal_start = find_nal_start (data, len, from);
    int start_code_len = (int) AM_SEI_START_CODE3_BYTES;
    uint8_t type;

    if (nal_start < 0)
      return -1;
    if ((size_t) nal_start >= len)
      return -1;

    if ((size_t) nal_start >= AM_SEI_START_CODE4_BYTES &&
        data[nal_start - 4] == 0 && data[nal_start - 3] == 0 &&
        data[nal_start - 2] == 0 && data[nal_start - 1] == 1) {
      start_code_len = (int) AM_SEI_START_CODE4_BYTES;
    }

    if (codec == AM_SEI_CODEC_H265)
      type = (uint8_t) ((data[nal_start] >> 1) & 0x3f);
    else
      type = (uint8_t) (data[nal_start] & 0x1f);

    if ((codec == AM_SEI_CODEC_H265 && is_vcl_nal_h265 (type)) ||
        (codec == AM_SEI_CODEC_H264 && is_vcl_nal_h264 (type))) {
      return nal_start - start_code_len;
    }

    from = (size_t) nal_start + 1;
    if (from >= len)
      return -1;
  }
}

static size_t
build_sei_nal (uint8_t *out, size_t out_max, AmSeiCodec codec,
    const uint8_t *payload, size_t payload_len)
{
  uint8_t *rbsp = NULL;
  uint8_t *ebsp = NULL;
  size_t off = 0;
  size_t n;
  size_t ebsp_len;
  size_t content_len;
  size_t size_len_max;
  size_t rbsp_max;
  size_t ebsp_max;

  if (!out || !payload || payload_len == 0 || out_max < 8)
    return 0;

  memcpy (out, k_start_code4, sizeof (k_start_code4));
  off += sizeof (k_start_code4);
  if (codec == AM_SEI_CODEC_H265) {
    out[off++] = (AM_SEI_H265_NAL_TYPE_PREFIX_SEI << 1);  /* PREFIX_SEI */
    out[off++] = 0x01;
  } else {
    out[off++] = AM_SEI_H264_NAL_TYPE_SEI; /* H.264 SEI */
  }

  content_len = AM_SEI_UUID_BYTES + payload_len;
  size_len_max = (content_len / 0xff) + 1;
  rbsp_max = 1 + size_len_max + content_len + 1;
  ebsp_max = rbsp_max * 2;

  rbsp = (uint8_t *) malloc (rbsp_max);
  ebsp = (uint8_t *) malloc (ebsp_max);
  if (!rbsp || !ebsp)
    goto fail;

  n = build_user_data_unregistered (rbsp, rbsp_max,
      am_sei_uuid, payload, payload_len);
  if (n == 0)
    goto fail;
  rbsp[n++] = 0x80;  /* rbsp_trailing_bits */

  ebsp_len = rbsp_to_ebsp (rbsp, n, ebsp, ebsp_max);
  if (ebsp_len == 0 || off + ebsp_len > out_max)
    goto fail;

  memcpy (out + off, ebsp, ebsp_len);
  off += ebsp_len;
  free (rbsp);
  free (ebsp);
  return off;

fail:
  free (rbsp);
  free (ebsp);
  return 0;
}

int
am_sei_inject_into_au_internal (const uint8_t *au, size_t au_len, AmSeiCodec codec,
    uint64_t enabled_mask, const AmSeiInfo *info, uint8_t **out_au, size_t *out_len)
{
  uint8_t *payload = NULL;
  uint8_t *sei_nal = NULL;
  uint8_t *new_au = NULL;
  size_t payload_len;
  size_t payload_max;
  size_t sei_len;
  size_t sei_nal_max;
  size_t content_len;
  size_t size_len_max;
  size_t rbsp_max;
  ptrdiff_t insert_off;
  int rc = AM_SEI_OK;

  if (!au || !out_au || !out_len) {
    AM_SEI_LOGE ("inject invalid args au=%p out_au=%p out_len=%p",
        (const void *) au, (void *) out_au, (void *) out_len);
    return AM_SEI_EINVAL;
  }
  if (codec != AM_SEI_CODEC_H264 && codec != AM_SEI_CODEC_H265) {
    AM_SEI_LOGE ("inject unsupported codec=%d", (int) codec);
    return AM_SEI_EUNSUPPORTED;
  }
  *out_au = NULL;
  *out_len = 0;
  AM_SEI_LOGD ("inject begin codec=%d au_len=%zu mask=0x%llx",
      (int) codec, au_len, (unsigned long long) enabled_mask);

  /* Fixed payload ceiling keeps maintenance predictable; new built-ins must
   * fit into AM_SEI_MAX_PAYLOAD_BLOB_BYTES or explicitly raise the bound. */
  payload_max = AM_SEI_MAX_PAYLOAD_BLOB_BYTES;
  payload = (uint8_t *) malloc (payload_max);
  if (!payload) {
    AM_SEI_LOGE ("payload malloc failed, size=%zu", payload_max);
    rc = AM_SEI_ENOMEM;
    goto cleanup;
  }

  payload_len = am_sei_build_payload_blob (payload, payload_max, enabled_mask, info);
  if (payload_len == 0) {
    if (enabled_mask == 0) {
      AM_SEI_LOGD ("no payload generated, mask=0x%llx",
          (unsigned long long) enabled_mask);
      rc = AM_SEI_NO_DATA;
    } else {
      AM_SEI_LOGE ("payload build failed under max blob=%u mask=0x%llx",
          (unsigned) AM_SEI_MAX_PAYLOAD_BLOB_BYTES,
          (unsigned long long) enabled_mask);
      rc = AM_SEI_EFORMAT;
    }
    goto cleanup;
  }

  content_len = AM_SEI_UUID_BYTES + payload_len;
  size_len_max = (content_len / 0xff) + 1;
  rbsp_max = 1 + size_len_max + content_len + 1;
  sei_nal_max = 4 + ((codec == AM_SEI_CODEC_H265) ? 2 : 1) + (rbsp_max * 2);
  sei_nal = (uint8_t *) malloc (sei_nal_max);
  if (!sei_nal) {
    AM_SEI_LOGE ("sei_nal malloc failed, size=%zu", sei_nal_max);
    rc = AM_SEI_ENOMEM;
    goto cleanup;
  }

  sei_len = build_sei_nal (sei_nal, sei_nal_max, codec, payload, payload_len);
  if (sei_len == 0) {
    AM_SEI_LOGE ("build_sei_nal failed");
    rc = AM_SEI_EFORMAT;
    goto cleanup;
  }

  insert_off = find_insert_offset (au, au_len, codec);
  /* If no VCL is found, append SEI to AU tail as a safe fallback. */
  if (insert_off < 0) {
    AM_SEI_LOGW ("inject fallback append: no VCL found codec=%d au_len=%zu mask=0x%llx",
        (int) codec, au_len, (unsigned long long) enabled_mask);
    insert_off = (ptrdiff_t) au_len;
  }

  new_au = (uint8_t *) malloc (au_len + sei_len);
  if (!new_au) {
    AM_SEI_LOGE ("new_au malloc failed, size=%zu", au_len + sei_len);
    rc = AM_SEI_ENOMEM;
    goto cleanup;
  }

  memcpy (new_au, au, (size_t) insert_off);
  memcpy (new_au + insert_off, sei_nal, sei_len);
  memcpy (new_au + insert_off + sei_len, au + insert_off,
      au_len - (size_t) insert_off);

  *out_au = new_au;
  *out_len = au_len + sei_len;
  AM_SEI_LOGD ("inject success codec=%d in=%zu out=%zu sei=%zu",
      (int) codec, au_len, *out_len, sei_len);
  rc = AM_SEI_OK;

cleanup:
  if (rc != AM_SEI_OK && new_au) {
    free (new_au);
    new_au = NULL;
  }
  free (payload);
  free (sei_nal);
  return rc;
}

void
am_sei_free (void *ptr)
{
  free (ptr);
}
