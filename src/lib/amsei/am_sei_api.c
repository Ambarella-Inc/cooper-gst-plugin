/*
 * am_sei_api.c
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

#include <stdlib.h>
#include <string.h>

struct _AmSeiInfo {
  /* Business fields are intentionally stored in one opaque container so
   * encode/decode both operate on the same data model (A -> TLV -> A). */
  uint64_t present_mask;
  AmSeiTimestamp timestamp;
  AmSeiGpsData gps;
};

static uint64_t
am_sei_builtin_mask (void)
{
  /* Parse currently recognizes only built-in TLVs.
   * Future built-in types should be appended here and in am_sei_tlv.c. */
  return AM_SEI_TLVF_TIMESTAMP | AM_SEI_TLVF_GPS;
}

int
am_sei_init (void)
{
  /* no-op by design; reserved for future global resource management */
  return AM_SEI_OK;
}

void
am_sei_deinit (void)
{
  /* no-op by design; reserved for future global resource management */
}

void
am_sei_set_log_level (AmSeiLogLevel level)
{
  am_sei_log_set_level (level);
}

AmSeiLogLevel
am_sei_get_log_level (void)
{
  return am_sei_log_get_level ();
}

AmSeiInfo *
am_sei_info_new (void)
{
  return (AmSeiInfo *) calloc (1, sizeof (AmSeiInfo));
}

void
am_sei_info_free (AmSeiInfo *info)
{
  free (info);
}

void
am_sei_info_reset (AmSeiInfo *info)
{
  if (!info)
    return;
  memset (info, 0, sizeof (*info));
}

int
am_sei_info_set (AmSeiInfo *info, AmSeiTlvType type,
    const void *value, size_t value_size)
{
  /* Built-in TLV write entry.
   * To add a new built-in type, extend this switch and keep these in sync:
   * 1) struct _AmSeiInfo storage fields
   * 2) am_sei_info_get()/am_sei_info_is_present() bit mapping
   * 3) am_sei_tlv.c encode/decode dispatch and payload sizing
   * 4) public enum/size contracts in am_sei.h */
  if (!info || !value) {
    AM_SEI_LOGE ("info_set invalid args info=%p value=%p", (void *) info, value);
    return AM_SEI_EINVAL;
  }

  switch (type) {
    case AM_SEI_T_TIMESTAMP:
      if (value_size < sizeof (AmSeiTimestamp)) {
        AM_SEI_LOGE ("info_set timestamp size too small=%zu", value_size);
        return AM_SEI_EINVAL;
      }
      info->timestamp = *(const AmSeiTimestamp *) value;
      info->present_mask |= AM_SEI_TLVF_TIMESTAMP;
      return AM_SEI_OK;
    case AM_SEI_T_GPS:
      if (value_size < sizeof (AmSeiGpsData)) {
        AM_SEI_LOGE ("info_set gps size too small=%zu", value_size);
        return AM_SEI_EINVAL;
      }
      info->gps = *(const AmSeiGpsData *) value;
      info->present_mask |= AM_SEI_TLVF_GPS;
      return AM_SEI_OK;
    default:
      AM_SEI_LOGW ("info_set unsupported type=%u", (unsigned) type);
      return AM_SEI_UNSUPPORTED_TYPE;
  }
}

int
am_sei_info_get (const AmSeiInfo *info, AmSeiTlvType type,
    void *out_value, size_t out_size)
{
  /* Built-in TLV read entry.
   * Return contract:
   * - AM_SEI_OK: requested type present
   * - AM_SEI_NO_DATA: type recognized but absent in current info
   * - AM_SEI_UNSUPPORTED_TYPE: type not implemented as built-in */
  if (!info || !out_value) {
    AM_SEI_LOGE ("info_get invalid args info=%p out=%p", (const void *) info, out_value);
    return AM_SEI_EINVAL;
  }

  switch (type) {
    case AM_SEI_T_TIMESTAMP:
      if (out_size < sizeof (AmSeiTimestamp)) {
        AM_SEI_LOGE ("info_get timestamp out_size too small=%zu", out_size);
        return AM_SEI_EINVAL;
      }
      if (!(info->present_mask & AM_SEI_TLVF_TIMESTAMP))
        return AM_SEI_NO_DATA;
      *(AmSeiTimestamp *) out_value = info->timestamp;
      return AM_SEI_OK;
    case AM_SEI_T_GPS:
      if (out_size < sizeof (AmSeiGpsData)) {
        AM_SEI_LOGE ("info_get gps out_size too small=%zu", out_size);
        return AM_SEI_EINVAL;
      }
      if (!(info->present_mask & AM_SEI_TLVF_GPS))
        return AM_SEI_NO_DATA;
      *(AmSeiGpsData *) out_value = info->gps;
      return AM_SEI_OK;
    default:
      AM_SEI_LOGW ("info_get unsupported type=%u", (unsigned) type);
      return AM_SEI_UNSUPPORTED_TYPE;
  }
}

int
am_sei_info_is_present (const AmSeiInfo *info, AmSeiTlvType type)
{
  uint64_t bit;
  if (!info || type == AM_SEI_T_INVALID || type > 64)
    return 0;
  bit = 1ULL << (type - 1);
  return (info->present_mask & bit) ? 1 : 0;
}

uint64_t
am_sei_info_get_present_mask (const AmSeiInfo *info)
{
  if (!info)
    return 0;
  return info->present_mask;
}

void
am_sei_decode_meta_init (AmSeiDecodeMeta *meta)
{
  if (!meta)
    return;
  memset (meta, 0, sizeof (*meta));
  meta->struct_size = (uint32_t) sizeof (*meta);
  meta->abi_version = AM_SEI_ABI_VERSION;
}

int
am_sei_inject_au_with_info (const uint8_t *au, size_t au_len, AmSeiCodec codec,
    const AmSeiInfo *info, uint8_t **out_au, size_t *out_len)
{
  if (!info) {
    AM_SEI_LOGE ("inject_with_info info is NULL");
    return AM_SEI_EINVAL;
  }
  AM_SEI_LOGD ("inject_with_info codec=%d au_len=%zu mask=0x%llx",
      (int) codec, au_len, (unsigned long long) info->present_mask);
  /* Injection mask is fully driven by info->present_mask.
   * Element/app controls what to inject by am_sei_info_set/reset. */
  return am_sei_inject_into_au_internal (au, au_len, codec, info->present_mask, info,
      out_au, out_len);
}

int
am_sei_parse_au_to_info (const uint8_t *au, size_t au_len, AmSeiCodec codec,
    AmSeiInfo *info, AmSeiDecodeMeta *meta)
{
  uint16_t payload_version = 0;
  uint16_t payload_flags = 0;
  int rc;

  if (!info) {
    AM_SEI_LOGE ("parse_au_to_info info is NULL");
    return AM_SEI_EINVAL;
  }
  AM_SEI_LOGD ("parse_au_to_info codec=%d au_len=%zu", (int) codec, au_len);
  /* Parse mask is fixed to built-ins by design; app does not register runtime
   * TLV types. Extending parse capability means adding new built-ins in lib. */
  rc = am_sei_parse_au_internal (au, au_len, codec, am_sei_builtin_mask (), info,
      &payload_version, &payload_flags);
  if (meta) {
    am_sei_decode_meta_init (meta);
    meta->payload_version = payload_version;
    meta->payload_flags = payload_flags;
  }
  return rc;
}
