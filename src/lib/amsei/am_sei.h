/*
 * am_sei.h
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

#ifndef __AM_SEI_H__
#define __AM_SEI_H__

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===== Macros ===== */
/* Public ABI tag for exported structs. */
#define AM_SEI_ABI_VERSION 1U

/* Payload magic: "AMSE". */
#define AM_SEI_MAGIC_0 0x41  /* 'A' */
#define AM_SEI_MAGIC_1 0x4d  /* 'M' */
#define AM_SEI_MAGIC_2 0x53  /* 'S' */
#define AM_SEI_MAGIC_3 0x45  /* 'E' */
#define AM_SEI_PAYLOAD_VERSION_V1 1U /* Payload version stored in blob header. */

/* Binary payload fixed sizes (wire format). */
#define AM_SEI_TLV_HEADER_BYTES 4U
#define AM_SEI_TIMESTAMP_BYTES  8U
#define AM_SEI_GPS_BYTES        13U

/* ===== Enums ===== */
/* Public log level accepted by am_sei_set_log_level(). */
typedef enum {
  AM_SEI_LOG_OFF = 0,
  AM_SEI_LOG_ERROR = 1,
  AM_SEI_LOG_WARN = 2,
  AM_SEI_LOG_INFO = 3,
  AM_SEI_LOG_DEBUG = 4,
  AM_SEI_LOG_TRACE = 5
} AmSeiLogLevel;

/* Built-in TLV business type IDs. */
typedef enum {
  AM_SEI_T_INVALID = 0,
  AM_SEI_T_TIMESTAMP = 1, /* uint64 ns */
  AM_SEI_T_GPS = 2,       /* valid + lat/lon/alt */
  AM_SEI_T_RESERVED_START = 16
} AmSeiTlvType;

/* Payload header flags derived from built-in type IDs. */
#define AM_SEI_FLAG_HAS_TIMESTAMP (1U << (AM_SEI_T_TIMESTAMP - 1))
#define AM_SEI_FLAG_HAS_GPS       (1U << (AM_SEI_T_GPS - 1))

/* Common TLV bit aliases for masks accepted/returned by API. */
#define AM_SEI_TLVF_TIMESTAMP AM_SEI_FLAG_HAS_TIMESTAMP
#define AM_SEI_TLVF_GPS       AM_SEI_FLAG_HAS_GPS

/* Annex-B codec discriminator. */
typedef enum {
  AM_SEI_CODEC_UNKNOWN = 0,
  AM_SEI_CODEC_H264 = 1,
  AM_SEI_CODEC_H265 = 2,
  AM_SEI_CODEC_RESERVED_START = 16
} AmSeiCodec;

/* Common return status. */
typedef enum {
  AM_SEI_OK = 0,              /* Operation succeeded. */
  AM_SEI_NO_DATA = 1,         /* Valid input, but no target business data produced. */
  AM_SEI_UNSUPPORTED_TYPE = 2,/* Requested built-in type is not implemented. */
  AM_SEI_EINVAL = -1,         /* Invalid argument (NULL pointer, bad size, etc.). */
  AM_SEI_ENOMEM = -2,         /* Memory allocation failed. */
  AM_SEI_EFORMAT = -3,        /* Input/output binary format is invalid/corrupted. */
  AM_SEI_EUNSUPPORTED = -4    /* Unsupported codec or feature in current build. */
} AmSeiStatus;

/* ===== Structs / Typedefs ===== */
/* Built-in business value for AM_SEI_T_TIMESTAMP. */
typedef uint64_t AmSeiTimestamp;

/* Built-in business value for AM_SEI_T_GPS. */
typedef struct {
  int32_t valid;
  int32_t lat_e7;
  int32_t lon_e7;
  int32_t alt_cm;
  uint64_t reserved_u64[2];
} AmSeiGpsData;

/*
 * Built-in type/value contract for am_sei_info_set/get:
 * - AM_SEI_T_TIMESTAMP <-> AmSeiTimestamp
 * - AM_SEI_T_GPS       <-> AmSeiGpsData
 */
#define AM_SEI_INFO_VALUE_SIZE_TIMESTAMP ((size_t) sizeof (AmSeiTimestamp))
#define AM_SEI_INFO_VALUE_SIZE_GPS       ((size_t) sizeof (AmSeiGpsData))

/* Opaque per-frame container (encode input / decode output). */
typedef struct _AmSeiInfo AmSeiInfo;

/* Optional decode metadata output. */
typedef struct {
  /* Must be initialized by am_sei_decode_meta_init(). */
  uint32_t struct_size;
  /* ABI version for this metadata struct. */
  uint32_t abi_version;
  /* Payload header version parsed from AMSE blob.
   * This reflects on-wire producer version, not API version. */
  uint16_t payload_version;
  /* Payload header flags parsed from AMSE blob.
   * This reflects what payload declares on wire.
   * Note: decode result in AmSeiInfo (present_mask) may be narrower if
   * parsing/filtering rejects some declared TLVs. */
  uint16_t payload_flags;
  /* Reserved for future extension. */
  uint64_t reserved_u64[4];
} AmSeiDecodeMeta;

/* Library lifecycle and global config.
 * NOTE: init/deinit are currently no-op compatibility hooks kept for future
 * global resource setup/teardown; safe to call repeatedly. */
int am_sei_init (void);

/* Deinitialize library-level resources (currently no-op). */
void am_sei_deinit (void);

/* Set global libamsei log level.
 * IN: level
 */
void am_sei_set_log_level (AmSeiLogLevel level);

/* Get current global libamsei log level. */
AmSeiLogLevel am_sei_get_log_level (void);

/* Reusable per-frame info object operations. */
/* Allocate one reusable per-frame info container. */
AmSeiInfo *am_sei_info_new (void);

/* Release info container allocated by am_sei_info_new().
 * IN: info
 */
void am_sei_info_free (AmSeiInfo *info);

/* Clear all values/present bits for reusing info on next frame.
 * INOUT: info
 */
void am_sei_info_reset (AmSeiInfo *info);

/* Set one built-in typed value in info.
 * INOUT: info
 * IN: type, value, value_size
 */
int am_sei_info_set (AmSeiInfo *info, AmSeiTlvType type,
    const void *value, size_t value_size);

/* Get one built-in typed value from info.
 * IN: info, type, out_size
 * OUT: out_value
 */
int am_sei_info_get (const AmSeiInfo *info, AmSeiTlvType type,
    void *out_value, size_t out_size);

/* Check whether a type is present in info (1/0).
 * IN: info, type
 */
int am_sei_info_is_present (const AmSeiInfo *info, AmSeiTlvType type);

/* Get bitmask of present types in info (AM_SEI_TLVF_*).
 * IN: info
 */
uint64_t am_sei_info_get_present_mask (const AmSeiInfo *info);

/* Initialize decode meta struct_size/abi_version and zero others.
 * OUT: meta
 */
void am_sei_decode_meta_init (AmSeiDecodeMeta *meta);

/* Core AU operations. */
/*
 * Inject SEI payload into one Annex-B AU.
 * out_au is heap-allocated on AM_SEI_OK and must be released by am_sei_free().
 * IN: au, au_len, codec, info
 * OUT: out_au, out_len
 */
int am_sei_inject_au_with_info (const uint8_t *au, size_t au_len,
    AmSeiCodec codec, const AmSeiInfo *info, uint8_t **out_au, size_t *out_len);

/*
 * Parse one Annex-B AU into info.
 * meta is optional; pass NULL if decode metadata is not needed.
 * IN: au, au_len, codec
 * OUT: info
 * OUT optional: meta
 */
int am_sei_parse_au_to_info (const uint8_t *au, size_t au_len,
    AmSeiCodec codec, AmSeiInfo *info, AmSeiDecodeMeta *meta);

/* Free memory returned by libamsei output APIs.
 * IN: ptr
 */
void am_sei_free (void *ptr);

#ifdef __cplusplus
}
#endif

#endif  /* __AM_SEI_H__ */
