/*
 * h265_parser.c
 *
 * History:
 *    12/18/2014 - [Zhi He] created file
 *
 * Copyright (C) 2022 Ambarella International LP
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

#include "stdlib.h"
#include "stdio.h"
#include "string.h"

#include "internal.h"

#include "codec_interface.h"

#define MAX_SPATIAL_SEGMENTATION 4096 // max. value of u(12) field
#define MIN_CACHE_BITS 25
#define MAX_SUB_LAYERS 7
#define MAX_VPS_COUNT 16
#define MAX_SPS_COUNT 32
#define MAX_PPS_COUNT 256
#define MAX_SHORT_TERM_RPS_COUNT 64
#define MAX_CU_SIZE 128

typedef struct {
  const guchar *buffer, *buffer_end;
  guint index;
  guint size_in_bits;
} GetbitsContext;

#define READ_LE_32(x)   \
  ((((const guchar*)(x))[3] << 24)   | \
   (((const guchar*)(x))[2] << 16)    |   \
   (((const guchar*)(x))[1] <<  8)    |  \
   ((const guchar*)(x))[0])

#define READ_BE_32(x)   \
  ((((const guchar*)(x))[0] << 24) | \
   (((const guchar*)(x))[1] << 16) |  \
   (((const guchar*)(x))[2] <<  8) |   \
   ((const guchar*)(x))[3])

#define BITS_OPEN_READER(name, gb)   \
  guint name##_index = (gb)->index;   \
  guint name##_cache  =   0

#define BITS_CLOSE_READER(name, gb) \
  (gb)->index = name##_index; \
  name##_cache  =   0

#define BITS_OPEN_READER_SKIP(name, gb)   \
  guint name##_index = (gb)->index

#define BITS_CLOSE_READER_SKIP(name, gb) \
  (gb)->index = name##_index

#define BITS_UPDATE_CACHE_BE(name, gb) \
  name##_cache = READ_BE_32(((const guchar *)(gb)->buffer)+(name##_index>>3)) << (name##_index&0x07)
#define BITS_UPDATE_CACHE_LE(name, gb) \
  name##_cache = READ_LE_32(((const guchar *)(gb)->buffer)+(name##_index>>3)) >> (name##_index&0x07)

#define BITS_SKIP_CACHE(name, gb, num) name##_cache >>= (num)
#define BITS_SKIP_COUNTER(name, gb, num) name##_index += (num)

#define BITS_SKIP_BITS(name, gb, num) do {  \
    BITS_SKIP_CACHE(name, gb, num); \
    BITS_SKIP_COUNTER(name, gb, num);   \
  } while (0)

#define BITS_LAST_SKIP_BITS(name, gb, num) BITS_SKIP_COUNTER(name, gb, num)
#define BITS_LAST_SKIP_CACHE(name, gb, num)

#define NEG_USR32(a,s) (((guint)(a))>>(32-(s)))
#define BITS_SHOW_UBITS(name, gb, num) NEG_USR32(name ## _cache, num)

#define BITS_GET_CACHE(name, gb) ((guint)name##_cache)

const guchar simple_log2_table[256] = {
  0, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
  6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
  7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
  7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
  7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
  7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7
};

const guchar simple_golomb_vlc_len[512] = {
  19, 17, 15, 15, 13, 13, 13, 13, 11, 11, 11, 11, 11, 11, 11, 11, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
  7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
  3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
  3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
  3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
};

const guchar simple_ue_golomb_vlc_code[512] = {
  32, 32, 32, 32, 32, 32, 32, 32, 31, 32, 32, 32, 32, 32, 32, 32, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
  7, 7, 7, 7, 8, 8, 8, 8, 9, 9, 9, 9, 10, 10, 10, 10, 11, 11, 11, 11, 12, 12, 12, 12, 13, 13, 13, 13, 14, 14, 14, 14,
  3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

const TS8 simple_se_golomb_vlc_code[512] = {
  17, 17, 17, 17, 17, 17, 17, 17, 16, 17, 17, 17, 17, 17, 17, 17,  8, -8,  9, -9, 10, -10, 11, -11, 12, -12, 13, -13, 14, -14, 15, -15,
  4,  4,  4,  4, -4, -4, -4, -4,  5,  5,  5,  5, -5, -5, -5, -5,  6,  6,  6,  6, -6, -6, -6, -6,  7,  7,  7,  7, -7, -7, -7, -7,
  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3,
  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
};

static gint simple_log2_c (guint v)
{
  gint n = 0;

  if (v & 0xffff0000) {
    v >>= 16;
    n += 16;
  }

  if (v & 0xff00) {
    v >>= 8;
    n += 8;
  }

  n += simple_log2_table[v];

  return n;
}

static inline gint _get_se_golomb (GetbitsContext *gb)
{
  guint buf;
  gint log;

  BITS_OPEN_READER (re, gb);
  BITS_UPDATE_CACHE_BE (re, gb);
  buf = BITS_GET_CACHE (re, gb);

  if (buf >= (1 << 27) ) {
    buf >>= 32 - 9;
    BITS_LAST_SKIP_BITS (re, gb, simple_golomb_vlc_len[buf]);
    BITS_CLOSE_READER (re, gb);

    return simple_se_golomb_vlc_code[buf];

  } else {
    log = simple_log2_c (buf);
    BITS_LAST_SKIP_BITS (re, gb, 31 - log);
    BITS_UPDATE_CACHE_BE (re, gb);
    buf = BITS_GET_CACHE (re, gb);
    buf >>= log;

    BITS_LAST_SKIP_BITS (re, gb, 32 - log);
    BITS_CLOSE_READER (re, gb);

    if (buf & 1) {
      buf = - (buf >> 1);

    } else      {
      buf = (buf >> 1);
    }

    return buf;
  }
}

static inline gint _get_ue_golomb (GetbitsContext *gb)
{
  guint buf;
  gint log;

  BITS_OPEN_READER (re, gb);
  BITS_UPDATE_CACHE_BE (re, gb);
  buf = BITS_GET_CACHE (re, gb);

  if (buf >= (1 << 27) ) {
    buf >>= 32 - 9;
    BITS_LAST_SKIP_BITS (re, gb, simple_golomb_vlc_len[buf]);
    BITS_CLOSE_READER (re, gb);
    return simple_ue_golomb_vlc_code[buf];

  } else {
    log = 2 * simple_log2_c (buf) - 31;
    buf >>= log;
    buf--;
    BITS_LAST_SKIP_BITS (re, gb, 32 - log);
    BITS_CLOSE_READER (re, gb);
    return buf;
  }
}

static inline gint _get_ue_golomb_31 (GetbitsContext *gb)
{
  guint buf;

  BITS_OPEN_READER (re, gb);

  BITS_UPDATE_CACHE_BE (re, gb);
  buf = BITS_GET_CACHE (re, gb);

  buf >>= 32 - 9;

  BITS_LAST_SKIP_BITS (re, gb, simple_golomb_vlc_len[buf]);
  BITS_CLOSE_READER (re, gb);

  return simple_ue_golomb_vlc_code[buf];
}

static inline guint _show_bits (GetbitsContext *s, gint n)
{
  gint tmp;
  BITS_OPEN_READER (re, s);
  BITS_UPDATE_CACHE_BE (re, s);
  tmp = BITS_SHOW_UBITS (re, s, n);
  return tmp;
}

static inline guint _show_bits1 (GetbitsContext *s)
{
  return _show_bits (s, 1);
}

static inline guint _get_bits (GetbitsContext *s, gint n)
{
  gint tmp;
  BITS_OPEN_READER (re, s);
  BITS_UPDATE_CACHE_BE (re, s);
  tmp = BITS_SHOW_UBITS (re, s, n);
  BITS_LAST_SKIP_BITS (re, s, n);
  BITS_CLOSE_READER (re, s);
  return tmp;
}

static inline guint _get_bits1 (GetbitsContext *s)
{
  guint index = s->index;
  guchar result  = s->buffer[index >> 3];

  result <<= index & 7;
  result >>= 8 - 1;

  index++;
  s->index = index;

  return result;
}

static inline guint _get_bits_long (GetbitsContext *s, gint n)
{
  if (!n) {
    return 0;

  } else if (n <= MIN_CACHE_BITS) {
    return _get_bits (s, n);

  } else {
    unsigned ret = _get_bits (s, 16) << (n - 16);
    return ret | _get_bits (s, n - 16);
  }
}

static gulong _get_bits64 (GetbitsContext *s, gint n)
{
  if (n <= 32) {
    return _get_bits_long (s, n);

  } else {
    gulong ret = (gulong) _get_bits_long (s, n - 32) << 32;
    return ret | _get_bits_long (s, 32);
  }
}

static inline void _skip_bits (GetbitsContext *s, gint n)
{
  BITS_OPEN_READER_SKIP (re, s);
  BITS_LAST_SKIP_BITS (re, s, n);
  BITS_CLOSE_READER_SKIP (re, s);
}

static inline void _skip_bits1 (GetbitsContext *s)
{
  _skip_bits (s, 1);
}

static inline guint _show_bits_long (GetbitsContext *s, gint n)
{
  if (n <= MIN_CACHE_BITS) {
    return _show_bits (s, n);

  } else {
    GetbitsContext gb = *s;
    return _get_bits_long (&gb, n);
  }
}

static inline void _skip_bits_long (GetbitsContext *s, gint n)
{
  s->index += n;
}

static inline unsigned _get_ue_golomb_long (GetbitsContext *gb)
{
  unsigned buf, log;

  buf = _show_bits_long (gb, 32);
  log = 31 - simple_log2_c (buf);
  _skip_bits_long (gb, log);

  return _get_bits_long (gb, log + 1) - 1;
}

static inline gint _get_se_golomb_long (GetbitsContext *gb)
{
  guint buf = _get_ue_golomb_long (gb);

  if (buf & 1) {
    buf = (buf + 1) >> 1;

  } else {
    buf = - (buf >> 1);
  }

  return buf;
}

typedef struct SHVCCProfileTierLevel {
  guchar  profile_space;
  guchar  tier_flag;
  guchar  profile_idc;
  guint profile_compatibility_flags;
  gulong constraint_indicator_flags;
  guchar  level_idc;
} SHVCCProfileTierLevel;

static guchar *_rbsp_from_nalu (const guchar *src, guint src_len, guint *dst_len)
{
  guchar *dst;
  guint i, len;

  dst = (guchar *) malloc (src_len, "RBSP");

  if (!dst) {
    return NULL;
  }

  i = len = 0;

  while (i < 2 && i < src_len) {
    dst[len++] = src[i++];
  }

  while (i + 2 < src_len) {
    if (!src[i] && !src[i + 1] && src[i + 2] == 3) {
      dst[len++] = src[i++];
      dst[len++] = src[i++];
      i++;

    } else {
      dst[len++] = src[i++];
    }
  }

  while (i < src_len) {
    dst[len++] = src[i++];
  }

  *dst_len = len;
  return dst;
}

static void _skip_sub_layer_ordering_info (GetbitsContext *gb)
{
  _get_ue_golomb_long (gb);
  _get_ue_golomb_long (gb);
  _get_ue_golomb_long (gb);
}

static void _skip_scaling_list_data (GetbitsContext *gb)
{
  gint i, j, k, num_coeffs;

  for (i = 0; i < 4; i++) {
    for (j = 0; j < (i == 3 ? 2 : 6); j++) {
      if (!_get_bits1 (gb) ) {
        _get_ue_golomb_long (gb);

      } else {
        num_coeffs = DMIN (64, 1 << (4 + (i << 1) ) );

        if (i > 1) {
          _get_se_golomb_long (gb);
        }

        for (k = 0; k < num_coeffs; k++) {
          _get_se_golomb_long (gb);
        }
      }
    }
  }
}

static void _skip_sub_layer_hrd_parameters (GetbitsContext *gb, guint cpb_cnt_minus1, guchar sub_pic_hrd_params_present_flag)
{
  guint i;

  for (i = 0; i <= cpb_cnt_minus1; i++) {
    _get_ue_golomb_long (gb);
    _get_ue_golomb_long (gb);

    if (sub_pic_hrd_params_present_flag) {
      _get_ue_golomb_long (gb);
      _get_ue_golomb_long (gb);
    }

    _skip_bits1 (gb);
  }
}

static void _skip_hrd_parameters (GetbitsContext *gb, guchar cprms_present_flag, guint max_sub_layers_minus1)
{
  guint i;
  guchar sub_pic_hrd_params_present_flag = 0;
  guchar nal_hrd_parameters_present_flag = 0;
  guchar vcl_hrd_parameters_present_flag = 0;

  if (cprms_present_flag) {
    nal_hrd_parameters_present_flag = _get_bits1 (gb);
    vcl_hrd_parameters_present_flag = _get_bits1 (gb);

    if (nal_hrd_parameters_present_flag ||
        vcl_hrd_parameters_present_flag) {
      sub_pic_hrd_params_present_flag = _get_bits1 (gb);

      if (sub_pic_hrd_params_present_flag)
        /*
         * tick_divisor_minus2                          u(8)
         * du_cpb_removal_delay_increment_length_minus1 u(5)
         * sub_pic_cpb_params_in_pic_timing_sei_flag    u(1)
         * dpb_output_delay_du_length_minus1            u(5)
         */
      {
        _skip_bits (gb, 19);
      }

      /*
       * bit_rate_scale u(4)
       * cpb_size_scale u(4)
       */
      _skip_bits (gb, 8);

      if (sub_pic_hrd_params_present_flag) {
        _skip_bits (gb, 4);  // cpb_size_du_scale
      }

      /*
       * initial_cpb_removal_delay_length_minus1 u(5)
       * au_cpb_removal_delay_length_minus1      u(5)
       * dpb_output_delay_length_minus1          u(5)
       */
      _skip_bits (gb, 15);
    }
  }

  for (i = 0; i <= max_sub_layers_minus1; i++) {
    guint cpb_cnt_minus1            = 0;
    guchar low_delay_hrd_flag             = 0;
    guchar fixed_pic_rate_within_cvs_flag = 0;
    guchar fixed_pic_rate_general_flag    = _get_bits1 (gb);

    if (!fixed_pic_rate_general_flag) {
      fixed_pic_rate_within_cvs_flag = _get_bits1 (gb);
    }

    if (fixed_pic_rate_within_cvs_flag) {
      _get_ue_golomb_long (gb);  // elemental_duration_in_tc_minus1

    } else {
      low_delay_hrd_flag = _get_bits1 (gb);
    }

    if (!low_delay_hrd_flag) {
      cpb_cnt_minus1 = _get_ue_golomb_long (gb);
    }

    if (nal_hrd_parameters_present_flag)
      _skip_sub_layer_hrd_parameters (gb, cpb_cnt_minus1,
                                      sub_pic_hrd_params_present_flag);

    if (vcl_hrd_parameters_present_flag)
      _skip_sub_layer_hrd_parameters (gb, cpb_cnt_minus1,
                                      sub_pic_hrd_params_present_flag);
  }
}

static void _skip_timing_info (GetbitsContext *gb)
{
  _skip_bits_long (gb, 32);
  _skip_bits_long (gb, 32);

  if (_get_bits1 (gb) ) {
    _get_ue_golomb_long (gb);
  }
}

static void _hvcc_parse_vui (GetbitsContext *gb, SHEVCDecoderConfigurationRecord *record, guint max_sub_layers_minus1)
{
  guint min_spatial_segmentation_idc;

  if (_get_bits1 (gb) ) {
    if (_get_bits (gb, 8) == 255) {
      _skip_bits_long (gb, 32);
    }
  }

  if (_get_bits1 (gb) ) {
    _skip_bits1 (gb);
  }

  if (_get_bits1 (gb) ) {
    _skip_bits (gb, 4);

    if (_get_bits1 (gb) ) {
      _skip_bits (gb, 24);
    }
  }

  if (_get_bits1 (gb) ) {
    _get_ue_golomb_long (gb);
    _get_ue_golomb_long (gb);
  }

  _skip_bits (gb, 3);

  if (_get_bits1 (gb) ) {
    _get_ue_golomb_long (gb);
    _get_ue_golomb_long (gb);
    _get_ue_golomb_long (gb);
    _get_ue_golomb_long (gb);
  }

  if (_get_bits1 (gb) ) {
    _skip_timing_info (gb);

    if (_get_bits1 (gb) ) {
      _skip_hrd_parameters (gb, 1, max_sub_layers_minus1);
    }
  }

  if (_get_bits1 (gb) ) {
    _skip_bits (gb, 3);
    min_spatial_segmentation_idc = _get_ue_golomb_long (gb);
    record->min_spatial_segmentation_idc = DMIN (record->min_spatial_segmentation_idc, min_spatial_segmentation_idc);

    _get_ue_golomb_long (gb);
    _get_ue_golomb_long (gb);
    _get_ue_golomb_long (gb);
    _get_ue_golomb_long (gb);
  }
}

static void _hvcc_update_ptl (SHEVCDecoderConfigurationRecord *record, SHVCCProfileTierLevel *ptl)
{
  record->general_profile_space = ptl->profile_space;

  if (record->general_tier_flag < ptl->tier_flag) {
    record->general_level_idc = ptl->level_idc;

  } else {
    record->general_level_idc = DMAX (record->general_level_idc, ptl->level_idc);
  }

  record->general_tier_flag = DMAX (record->general_tier_flag, ptl->tier_flag);
  record->general_profile_idc = DMAX (record->general_profile_idc, ptl->profile_idc);
  record->general_profile_compatibility_flags &= ptl->profile_compatibility_flags;
  record->general_constraint_indicator_flags &= ptl->constraint_indicator_flags;
}

static void _hvcc_parse_ptl (GetbitsContext *gb, SHEVCDecoderConfigurationRecord *record, guint max_sub_layers_minus1)
{
  guint i;
  SHVCCProfileTierLevel general_ptl;
  guchar sub_layer_profile_present_flag[MAX_SUB_LAYERS];
  guchar sub_layer_level_present_flag[MAX_SUB_LAYERS];

  general_ptl.profile_space               = _get_bits (gb, 2);
  general_ptl.tier_flag                   = _get_bits1 (gb);
  general_ptl.profile_idc                 = _get_bits (gb, 5);
  general_ptl.profile_compatibility_flags = _get_bits_long (gb, 32);
  general_ptl.constraint_indicator_flags  = _get_bits64 (gb, 48);
  general_ptl.level_idc                   = _get_bits (gb, 8);
  _hvcc_update_ptl (record, &general_ptl);

  for (i = 0; i < max_sub_layers_minus1; i++) {
    sub_layer_profile_present_flag[i] = _get_bits1 (gb);
    sub_layer_level_present_flag[i]   = _get_bits1 (gb);
  }

  if (max_sub_layers_minus1 > 0) {
    for (i = max_sub_layers_minus1; i < 8; i++) {
      _skip_bits (gb, 2);
    }
  }

  for (i = 0; i < max_sub_layers_minus1; i++) {
    if (sub_layer_profile_present_flag[i]) {
      _skip_bits_long (gb, 32);
      _skip_bits_long (gb, 32);
      _skip_bits (gb, 24);
    }

    if (sub_layer_level_present_flag[i]) {
      _skip_bits (gb, 8);
    }
  }
}

static gint _parse_rps (GetbitsContext *gb, guint rps_idx, guint num_rps, guint num_delta_pocs[MAX_SHORT_TERM_RPS_COUNT])
{
  guint i;

  if (rps_idx && _get_bits1 (gb) ) {
    if (rps_idx >= num_rps) {
      GST_ERROR ("_parse_rps, rps_idx > num_rps\n");
      return (-1);
    }

    _skip_bits1 (gb);
    _get_ue_golomb_long (gb);

    num_delta_pocs[rps_idx] = 0;

    for (i = 0; i < num_delta_pocs[rps_idx - 1]; i++) {
      guchar use_delta_flag = 0;
      guchar used_by_curr_pic_flag = _get_bits1 (gb);

      if (!used_by_curr_pic_flag) {
        use_delta_flag = _get_bits1 (gb);
      }

      if (used_by_curr_pic_flag || use_delta_flag) {
        num_delta_pocs[rps_idx]++;
      }
    }

  } else {
    guint num_negative_pics = _get_ue_golomb_long (gb);
    guint num_positive_pics = _get_ue_golomb_long (gb);

    num_delta_pocs[rps_idx] = num_negative_pics + num_positive_pics;

    for (i = 0; i < num_negative_pics; i++) {
      _get_ue_golomb_long (gb);
      _skip_bits1 (gb);
    }

    for (i = 0; i < num_positive_pics; i++) {
      _get_ue_golomb_long (gb);
      _skip_bits1 (gb);
    }
  }

  return 0;
}

static void _hvcc_parse_vps (SHEVCDecoderConfigurationRecord *record, guchar *p_data, guint data_size)
{
  GetbitsContext gb;
  gb.buffer = p_data;
  gb.buffer_end = p_data + data_size;
  gb.index = 0;
  gb.size_in_bits = 8 * data_size;

  guint vps_max_sub_layers_minus1;
  _skip_bits (&gb, 12);
  vps_max_sub_layers_minus1 = _get_bits (&gb, 3);
  record->numTemporalLayers = DMAX (record->numTemporalLayers, vps_max_sub_layers_minus1 + 1);
  _skip_bits (&gb, 17);

  _hvcc_parse_ptl (&gb, record, vps_max_sub_layers_minus1);
}

static void _hvcc_parse_sps (SHEVCDecoderConfigurationRecord *record, guchar *p_data, guint data_size, guint &pic_width, guint &pic_height)
{
  GetbitsContext gb;
  gb.buffer = p_data;
  gb.buffer_end = p_data + data_size;
  gb.index = 0;
  gb.size_in_bits = 8 * data_size;

  guint i, sps_max_sub_layers_minus1, log2_max_pic_order_cnt_lsb_minus4;
  guint num_short_term_ref_pic_sets, num_delta_pocs[MAX_SHORT_TERM_RPS_COUNT];

  _skip_bits (&gb, 4);
  sps_max_sub_layers_minus1 = _get_bits (&gb, 3);
  record->numTemporalLayers = DMAX (record->numTemporalLayers, sps_max_sub_layers_minus1 + 1);
  record->temporalIdNested = _get_bits1 (&gb);
  _hvcc_parse_ptl (&gb, record, sps_max_sub_layers_minus1);
  _get_ue_golomb_long (&gb);
  record->chromaFormat = _get_ue_golomb_long (&gb);

  if (record->chromaFormat == 3) {
    _skip_bits1 (&gb);
  }

  pic_width = _get_ue_golomb_long (&gb);
  pic_height = _get_ue_golomb_long (&gb);

  if (_get_bits1 (&gb) ) {
    _get_ue_golomb_long (&gb);
    _get_ue_golomb_long (&gb);
    _get_ue_golomb_long (&gb);
    _get_ue_golomb_long (&gb);
  }

  record->bitDepthLumaMinus8 = _get_ue_golomb_long (&gb);
  record->bitDepthChromaMinus8 = _get_ue_golomb_long (&gb);
  log2_max_pic_order_cnt_lsb_minus4 = _get_ue_golomb_long (&gb);

  i = _get_bits1 (&gb) ? 0 : sps_max_sub_layers_minus1;

  for (; i <= sps_max_sub_layers_minus1; i++) {
    _skip_sub_layer_ordering_info (&gb);
  }

  _get_ue_golomb_long (&gb);
  _get_ue_golomb_long (&gb);
  _get_ue_golomb_long (&gb);
  _get_ue_golomb_long (&gb);
  _get_ue_golomb_long (&gb);
  _get_ue_golomb_long (&gb);

  if (_get_bits1 (&gb) && _get_bits1 (&gb) ) {
    _skip_scaling_list_data (&gb);
  }

  _skip_bits1 (&gb);
  _skip_bits1 (&gb);

  if (_get_bits1 (&gb) ) {
    _skip_bits (&gb, 4);
    _skip_bits (&gb, 4);
    _get_ue_golomb_long (&gb);
    _get_ue_golomb_long (&gb);
    _skip_bits1 (&gb);
  }

  num_short_term_ref_pic_sets = _get_ue_golomb_long (&gb);

  if (G_UNLIKELY (num_short_term_ref_pic_sets > MAX_SHORT_TERM_RPS_COUNT) ) {
    GST_ERROR ("_hvcc_parse_sps fail, num_short_term_ref_pic_sets(%d) exceed max value\n", num_short_term_ref_pic_sets);
    return;
  }

  for (i = 0; i < num_short_term_ref_pic_sets; i++) {
    gint ret = _parse_rps (&gb, i, num_short_term_ref_pic_sets, num_delta_pocs);

    if (G_UNLIKELY (ret < 0) ) {
      GST_ERROR ("_parse_rps fail, ret %d\n", ret);
      return;
    }
  }

  if (_get_bits1 (&gb) ) {
    for (i = 0; i < _get_ue_golomb_long (&gb); i++) {
      gint len = DMIN (log2_max_pic_order_cnt_lsb_minus4 + 4, 16);
      _skip_bits (&gb, len);
      _skip_bits1 (&gb);
    }
  }

  _skip_bits1 (&gb);
  _skip_bits1 (&gb);

  if (_get_bits1 (&gb) ) {
    _hvcc_parse_vui (&gb, record, sps_max_sub_layers_minus1);
  }
}

static void _hvcc_parse_pps (SHEVCDecoderConfigurationRecord *record, guchar *p_data, guint data_size)
{
  GetbitsContext gb;
  gb.buffer = p_data;
  gb.buffer_end = p_data + data_size;
  gb.index = 0;
  gb.size_in_bits = 8 * data_size;

  guchar tiles_enabled_flag, entropy_coding_sync_enabled_flag;

  _get_ue_golomb_long (&gb);
  _get_ue_golomb_long (&gb);
  _skip_bits (&gb, 7);

  _get_ue_golomb_long (&gb);
  _get_ue_golomb_long (&gb);
  _get_se_golomb_long (&gb);
  _skip_bits (&gb, 2);

  if (_get_bits1 (&gb) ) {
    _get_ue_golomb_long (&gb);
  }

  _get_se_golomb_long (&gb);
  _get_se_golomb_long (&gb);
  _skip_bits (&gb, 3);

  tiles_enabled_flag = _get_bits1 (&gb);
  entropy_coding_sync_enabled_flag = _get_bits1 (&gb);

  if (entropy_coding_sync_enabled_flag && tiles_enabled_flag) {
    record->parallelismType = 0;

  } else if (entropy_coding_sync_enabled_flag) {
    record->parallelismType = 3;

  } else if (tiles_enabled_flag) {
    record->parallelismType = 2;

  } else {
    record->parallelismType = 1;
  }
}

static void _parse_ptl (GetbitsContext *gb, guint max_sub_layers_minus1, SHVCCProfileTierLevel *general_ptl)
{
  guint i = 0;
  guchar sub_layer_profile_present_flag[MAX_SUB_LAYERS];
  guchar sub_layer_level_present_flag[MAX_SUB_LAYERS];

  general_ptl->profile_space               = _get_bits (gb, 2);
  general_ptl->tier_flag                   = _get_bits1 (gb);
  general_ptl->profile_idc                 = _get_bits (gb, 5);
  general_ptl->profile_compatibility_flags = _get_bits_long (gb, 32);
  general_ptl->constraint_indicator_flags  = _get_bits64 (gb, 48);
  general_ptl->level_idc                   = _get_bits (gb, 8);

  for (i = 0; i < max_sub_layers_minus1; i++) {
    sub_layer_profile_present_flag[i] = _get_bits1 (gb);
    sub_layer_level_present_flag[i]   = _get_bits1 (gb);
  }

  if (max_sub_layers_minus1 > 0) {
    for (i = max_sub_layers_minus1; i < 8; i++) {
      _skip_bits (gb, 2);
    }
  }

  for (i = 0; i < max_sub_layers_minus1; i++) {
    if (sub_layer_profile_present_flag[i]) {
      _skip_bits_long (gb, 32);
      _skip_bits_long (gb, 32);
      _skip_bits (gb, 24);
    }

    if (sub_layer_level_present_flag[i]) {
      _skip_bits (gb, 8);
    }
  }
}

int gstGetH265SizeFromSPS (guchar *p_data, guint data_size, guint &pic_width, guint &pic_height)
{
  if (G_UNLIKELY (!p_data || !data_size) ) {
    GST_ERROR ("NULL params in gstGetH265SizeFromSPS()\n");
    return COM_ECODE_BAD_PARAMS;
  }

  guint rbsp_len = 0;
  guchar *p_rbsp = _rbsp_from_nalu (p_data, data_size, &rbsp_len);

  if (G_UNLIKELY (!p_data || !data_size) ) {
    GST_ERROR ("no memory when parse sps\n");
    return int_NoMemory;
  }

  GetbitsContext gb;
  gb.buffer = p_rbsp;
  gb.buffer_end = p_rbsp + rbsp_len;
  gb.index = 0;
  gb.size_in_bits = 8 * rbsp_len;

  guint sps_max_sub_layers_minus1;
  SHVCCProfileTierLevel general_ptl;

  _skip_bits (&gb, 4);
  sps_max_sub_layers_minus1 = _get_bits (&gb, 3);
  _get_bits1 (&gb);
  _parse_ptl (&gb, sps_max_sub_layers_minus1, &general_ptl);
  _get_ue_golomb_long (&gb);
  guchar chomaformat = _get_ue_golomb_long (&gb);

  if (chomaformat == 3) {
    _skip_bits1 (&gb);
  }

  pic_width = _get_ue_golomb_long (&gb);
  pic_height = _get_ue_golomb_long (&gb);

  free (p_rbsp, "RBSP");

  return COM_ECODE_OK;
}

int gstGetH265SizeFromExtradata (guchar *p_data, guint data_size, guint &pic_width, guint &pic_height)
{
  guchar *p_vps = NULL, *p_sps = NULL, *p_pps = NULL;
  guint vps_size = 0, sps_size = 0, pps_size = 0;

  int err = gstGetH265VPSSPSPPS (p_data, (guint) data_size, p_vps, vps_size, p_sps, sps_size, p_pps, pps_size);

  if (DLikely (COM_ECODE_OK == err) ) {
    gstGetH265SizeFromSPS (p_sps + 6, sps_size - 6, pic_width, pic_height);

  } else {
    GST_ERROR ("do not get extra data?\n");
    return int_DataCorruption;
  }

  return COM_ECODE_OK;
}

int gstGenerateHEVCDecoderConfigurationRecord (SHEVCDecoderConfigurationRecord *record, guchar *vps, guint vps_length, guchar *sps, guint sps_length, guchar *pps, guint pps_length, guint &pic_width, guint &pic_height)
{
  if (!record || !vps || !vps_length || !sps || !sps_length || !pps || !pps_length) {
    GST_ERROR ("NULL params in gstGenerateHEVCDecoderConfigurationRecord()\n");
    return COM_ECODE_BAD_PARAMS;
  }

  memset (record, 0, sizeof (SHEVCDecoderConfigurationRecord) );
  record->configurationVersion = 1;
  record->lengthSizeMinusOne   = 3;
  record->general_profile_compatibility_flags = 0xffffffff;
  record->general_constraint_indicator_flags  = (gulong) 0xffffffffffffLL;
  record->min_spatial_segmentation_idc = MAX_SPATIAL_SEGMENTATION + 1;

  guint rbsp_len = 0;
  guchar *p_rbsp = _rbsp_from_nalu (vps + 2, vps_length - 2, &rbsp_len);

  if (p_rbsp) {
    _hvcc_parse_vps (record, p_rbsp, rbsp_len);
    free (p_rbsp, "RBSP");

  } else {
    GST_ERROR ("no memory when parse vps?\n");
  }

  p_rbsp = _rbsp_from_nalu (sps + 2, sps_length - 2, &rbsp_len);

  if (p_rbsp) {
    _hvcc_parse_sps (record, p_rbsp, rbsp_len, pic_width, pic_height);
    free (p_rbsp, "RBSP");

  } else {
    GST_ERROR ("no memory when parse sps?\n");
  }

  p_rbsp = _rbsp_from_nalu (pps + 2, pps_length - 2, &rbsp_len);

  if (p_rbsp) {
    _hvcc_parse_pps (record, p_rbsp, rbsp_len);
    free (p_rbsp, "RBSP");

  } else {
    GST_ERROR ("no memory when parse sps?\n");
  }

  return COM_ECODE_OK;
}

int gstParseHEVCSPS (SHEVCDecoderConfigurationRecord *record, guchar *sps, guint sps_length, guint &pic_width, guint &pic_height)
{
  if (!record || !sps || !sps_length) {
    GST_ERROR ("NULL params in gstParseHEVCSPS()\n");
    return COM_ECODE_BAD_PARAMS;
  }

  memset (record, 0, sizeof (SHEVCDecoderConfigurationRecord) );
  record->configurationVersion = 1;
  record->lengthSizeMinusOne   = 3;
  record->general_profile_compatibility_flags = 0xffffffff;
  record->general_constraint_indicator_flags  = (gulong) 0xffffffffffffLL;
  record->min_spatial_segmentation_idc = MAX_SPATIAL_SEGMENTATION + 1;

  guint rbsp_len = 0;
  guchar *p_rbsp = _rbsp_from_nalu (sps + 2, sps_length - 2, &rbsp_len);

  if (p_rbsp) {
    _hvcc_parse_sps (record, p_rbsp, rbsp_len, pic_width, pic_height);
    free (p_rbsp, "RBSP");

  } else {
    GST_ERROR ("no memory when parse sps?\n");
    return int_NoMemory;
  }

  return COM_ECODE_OK;
}


