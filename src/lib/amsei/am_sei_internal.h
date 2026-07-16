/*
 * am_sei_internal.h
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

#ifndef __AM_SEI_INTERNAL_H__
#define __AM_SEI_INTERNAL_H__

#include <stddef.h>
#include <stdint.h>

#include "am_sei.h"
#include "am_sei_log.h"

#define AM_SEI_UUID_BYTES 16U
/* Fixed bytes before TLV blob payload:
 * magic(4) + version(2) + flags(2) + blob_len(4). */
#define AM_SEI_PAYLOAD_HEADER_BYTES 12U
/* Hard upper bound for one AMSE payload blob (header + TLVs).
 * Keep this bounded to simplify maintenance and avoid scattered size math. */
#define AM_SEI_MAX_PAYLOAD_BLOB_BYTES 512U
/* Annex-B and NAL constants used by inject/parse internals. */
#define AM_SEI_START_CODE3_BYTES 3U
#define AM_SEI_START_CODE4_BYTES 4U
#define AM_SEI_SEI_PAYLOAD_TYPE_USER_DATA_UNREGISTERED 5U
#define AM_SEI_H264_NAL_TYPE_SEI 6U
#define AM_SEI_H264_VCL_TYPE_MIN 1U
#define AM_SEI_H264_VCL_TYPE_MAX 5U
#define AM_SEI_H265_NAL_TYPE_PREFIX_SEI 39U
#define AM_SEI_H265_VCL_TYPE_MAX 31U

extern const uint8_t am_sei_uuid[AM_SEI_UUID_BYTES];

void am_sei_put_le16 (uint8_t *p, uint16_t v);
void am_sei_put_le32 (uint8_t *p, uint32_t v);
void am_sei_put_le64 (uint8_t *p, uint64_t v);
uint16_t am_sei_get_le16 (const uint8_t *p);
uint32_t am_sei_get_le32 (const uint8_t *p);
uint64_t am_sei_get_le64 (const uint8_t *p);

int am_sei_parse_payload_blob (const uint8_t *payload, size_t payload_len,
    uint64_t enabled_mask, AmSeiInfo *out_info,
    uint16_t *out_payload_version, uint16_t *out_payload_flags);
size_t am_sei_build_payload_blob (uint8_t *out, size_t out_max,
    uint64_t enabled_mask, const AmSeiInfo *info);

int am_sei_inject_into_au_internal (const uint8_t *au, size_t au_len, AmSeiCodec codec,
    uint64_t enabled_mask, const AmSeiInfo *info, uint8_t **out_au, size_t *out_len);
int am_sei_parse_au_internal (const uint8_t *au, size_t au_len, AmSeiCodec codec,
    uint64_t enabled_mask, AmSeiInfo *out_info,
    uint16_t *out_payload_version, uint16_t *out_payload_flags);

#endif  /* __AM_SEI_INTERNAL_H__ */
