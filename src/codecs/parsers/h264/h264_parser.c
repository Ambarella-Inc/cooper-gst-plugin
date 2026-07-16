/*
 * h264_parser.c
 *
 * History:
 *    11/18/2013 - [Zhi He] created file
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

typedef struct {
  const guchar *buffer, *buffer_end;
  guint index;
  guint size_in_bits;
} GetbitsContext;

typedef struct {
  gint num; ///< numerator
  gint den; ///< denominator
} _AVRational;

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

#define BIT_OPEN_READER(name, gb)   \
  guint name##_index = (gb)->index;   \
  guint name##_cache  =   0

#define BITS_CLOSE_READER(name, gb) (gb)->index = name##_index
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

//log2
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

const gchar simple_se_golomb_vlc_code[512] = {
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

static const _AVRational pixel_aspect[17] = {
  {0, 1},
  {1, 1},
  {12, 11},
  {10, 11},
  {16, 11},
  {40, 33},
  {24, 11},
  {20, 11},
  {32, 11},
  {80, 33},
  {18, 11},
  {15, 11},
  {64, 33},
  {160, 99},
  {4, 3},
  {3, 2},
  {2, 1},
};


static const guchar default_scaling4[2][16] = {
  {
    6, 13, 20, 28,
    13, 20, 28, 32,
    20, 28, 32, 37,
    28, 32, 37, 42
  }, {
    10, 14, 20, 24,
    14, 20, 24, 27,
    20, 24, 27, 30,
    24, 27, 30, 34
  }
};

static const guchar default_scaling8[2][64] = {
  {
    6, 10, 13, 16, 18, 23, 25, 27,
    10, 11, 16, 18, 23, 25, 27, 29,
    13, 16, 18, 23, 25, 27, 29, 31,
    16, 18, 23, 25, 27, 29, 31, 33,
    18, 23, 25, 27, 29, 31, 33, 36,
    23, 25, 27, 29, 31, 33, 36, 38,
    25, 27, 29, 31, 33, 36, 38, 40,
    27, 29, 31, 33, 36, 38, 40, 42
  }, {
    9, 13, 15, 17, 19, 21, 22, 24,
    13, 13, 17, 19, 21, 22, 24, 25,
    15, 17, 19, 21, 22, 24, 25, 27,
    17, 19, 21, 22, 24, 25, 27, 28,
    19, 21, 22, 24, 25, 27, 28, 30,
    21, 22, 24, 25, 27, 28, 30, 32,
    22, 24, 25, 27, 28, 30, 32, 33,
    24, 25, 27, 28, 30, 32, 33, 35
  }
};

static const guchar simple_zigzag_scan[16] = {
  0 + 0 * 4, 1 + 0 * 4, 0 + 1 * 4, 0 + 2 * 4,
  1 + 1 * 4, 2 + 0 * 4, 3 + 0 * 4, 2 + 1 * 4,
  1 + 2 * 4, 0 + 3 * 4, 1 + 3 * 4, 2 + 2 * 4,
  3 + 1 * 4, 3 + 2 * 4, 2 + 3 * 4, 3 + 3 * 4,
};

const guchar simple_zigzag_direct[64] = {
  0,   1,  8, 16,  9,  2,  3, 10,
  17, 24, 32, 25, 18, 11,  4,  5,
  12, 19, 26, 33, 40, 48, 41, 34,
  27, 20, 13,  6,  7, 14, 21, 28,
  35, 42, 49, 56, 57, 50, 43, 36,
  29, 22, 15, 23, 30, 37, 44, 51,
  58, 59, 52, 45, 38, 31, 39, 46,
  53, 60, 61, 54, 47, 55, 62, 63
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

  BIT_OPEN_READER (re, gb);
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

  BIT_OPEN_READER (re, gb);
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

  BIT_OPEN_READER (re, gb);

  BITS_UPDATE_CACHE_BE (re, gb);
  buf = BITS_GET_CACHE (re, gb);

  buf >>= 32 - 9;

  BITS_LAST_SKIP_BITS (re, gb, simple_golomb_vlc_len[buf]);
  BITS_CLOSE_READER (re, gb);

  return simple_ue_golomb_vlc_code[buf];
}

static inline guint _show_bits (GetbitsContext *s, int n)
{
  gint tmp;
  BIT_OPEN_READER (re, s);
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
  BIT_OPEN_READER (re, s);
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

static void _decode_scaling_list (GetbitsContext *gb, guchar *factors, gint size,
                                  const guchar *jvt_list, const guchar *fallback_list)
{
  gint i, last = 8, next = 8;
  const guchar *scan = (size == 16) ? simple_zigzag_scan : simple_zigzag_direct;

  if (!_get_bits1 (gb) ) { /* matrix not written, we use the predicted one */
    memcpy (factors, fallback_list, size * sizeof (guchar) );

  } else {
    for (i = 0; i < size; i++) {
      if (next) {
        next = (last + _get_se_golomb (gb) ) & 0xff;
      }

      if (!i && !next) { /* matrix not written, we use the preset one */
        memcpy (factors, jvt_list, size * sizeof (guchar) );
        break;
      }

      last = factors[scan[i]] = next ? next : last;
    }
  }
}

static void _decode_scaling_matrices (GetbitsContext *gb, SCodecVideoH264SPS *sps, SCodecVideoH264PPS *pps, gint is_sps,
                                      guchar (*scaling_matrix4) [16], guchar (*scaling_matrix8) [64])
{
  gint fallback_sps = !is_sps && sps->scaling_matrix_present;
  const guchar *fallback[4] = {
    fallback_sps ? sps->scaling_matrix4[0] : default_scaling4[0],
    fallback_sps ? sps->scaling_matrix4[3] : default_scaling4[1],
    fallback_sps ? sps->scaling_matrix8[0] : default_scaling8[0],
    fallback_sps ? sps->scaling_matrix8[3] : default_scaling8[1]
  };

  if (_get_bits1 (gb) ) {
    sps->scaling_matrix_present |= is_sps;
    _decode_scaling_list (gb, scaling_matrix4[0], 16, default_scaling4[0], fallback[0]); // Intra, Y
    _decode_scaling_list (gb, scaling_matrix4[1], 16, default_scaling4[0], scaling_matrix4[0]); // Intra, Cr
    _decode_scaling_list (gb, scaling_matrix4[2], 16, default_scaling4[0], scaling_matrix4[1]); // Intra, Cb
    _decode_scaling_list (gb, scaling_matrix4[3], 16, default_scaling4[1], fallback[1]); // Inter, Y
    _decode_scaling_list (gb, scaling_matrix4[4], 16, default_scaling4[1], scaling_matrix4[3]); // Inter, Cr
    _decode_scaling_list (gb, scaling_matrix4[5], 16, default_scaling4[1], scaling_matrix4[4]); // Inter, Cb

    if (is_sps || pps->transform_8x8_mode) {
      _decode_scaling_list (gb, scaling_matrix8[0], 64, default_scaling8[0], fallback[2]); // Intra, Y
      _decode_scaling_list (gb, scaling_matrix8[3], 64, default_scaling8[1], fallback[3]); // Inter, Y

      if (sps->chroma_format_idc == 3) {
        _decode_scaling_list (gb, scaling_matrix8[1], 64, default_scaling8[0], scaling_matrix8[0]); // Intra, Cr
        _decode_scaling_list (gb, scaling_matrix8[4], 64, default_scaling8[1], scaling_matrix8[3]); // Inter, Cr
        _decode_scaling_list (gb, scaling_matrix8[2], 64, default_scaling8[0], scaling_matrix8[1]); // Intra, Cb
        _decode_scaling_list (gb, scaling_matrix8[5], 64, default_scaling8[1], scaling_matrix8[4]); // Inter, Cb
      }
    }
  }
}

SCodecVideoH264 *gstGetVideoCodecH264Parser (guchar *p_data, TMemSize data_size, int &ret)
{
  GetbitsContext gb;
  gb.buffer = p_data;
  gb.buffer_end = p_data + data_size;
  gb.index = 0;
  gb.size_in_bits = 8 * data_size;

  SCodecVideoH264 *p_header = (SCodecVideoH264 *) malloc (sizeof (SCodecVideoH264), "G264");

  if (G_UNLIKELY (!p_header) ) {
    GST_ERROR ("No memory\n");
    ret = int_NoMemory;
    return NULL;
  }

  memset (p_header, 0x0, sizeof (SCodecVideoH264) );

  guint sps_id;
  gint i, log2_max_frame_num_minus4;

  p_header->sps.profile_idc = _get_bits (&gb, 8);
  p_header->sps.constraint_set_flags |= _get_bits1 (&gb) << 0;  //constraint_set0_flag
  p_header->sps.constraint_set_flags |= _get_bits1 (&gb) << 1;  //constraint_set1_flag
  p_header->sps.constraint_set_flags |= _get_bits1 (&gb) << 2;  //constraint_set2_flag
  p_header->sps.constraint_set_flags |= _get_bits1 (&gb) << 3;  //constraint_set3_flag
  p_header->sps.constraint_set_flags |= _get_bits1 (&gb) << 4;  //constraint_set4_flag
  p_header->sps.constraint_set_flags |= _get_bits1 (&gb) << 5;  //constraint_set5_flag
  _get_bits (&gb, 2);
  p_header->sps.level_idc = _get_bits (&gb, 8);
  sps_id = _get_ue_golomb_31 (&gb);

  p_header->common.profile_indicator = p_header->sps.profile_idc;
  p_header->common.level_indicator = p_header->sps.level_idc;

  if (sps_id >= DMAX_SPS_COUNT) {
    GST_ERROR ("sps_id (%d) out of range\n", sps_id);
    ret = COM_ECODE_BAD_PARAMS;
    goto gstGetVideoCodecH264Parser_failexit;
  }

  p_header->sps.time_offset_length = 24;
  p_header->sps.full_range = -1;

  memset (p_header->sps.scaling_matrix4, 16, sizeof (p_header->sps.scaling_matrix4) );
  memset (p_header->sps.scaling_matrix8, 16, sizeof (p_header->sps.scaling_matrix8) );
  p_header->sps.scaling_matrix_present = 0;
  p_header->sps.colorspace = EColorSpace_UNSPECIFIED;//  2; //AVCOL_SPC_UNSPECIFIED

  if (p_header->sps.profile_idc == 100 || p_header->sps.profile_idc == 110 ||
      p_header->sps.profile_idc == 122 || p_header->sps.profile_idc == 244 ||
      p_header->sps.profile_idc ==  44 || p_header->sps.profile_idc ==  83 ||
      p_header->sps.profile_idc ==  86 || p_header->sps.profile_idc == 118 ||
      p_header->sps.profile_idc == 128 || p_header->sps.profile_idc == 144) {
    p_header->sps.chroma_format_idc = _get_ue_golomb_31 (&gb);

    GST_LOG ("[parse sps]: chroma_format_idc %d\n", p_header->sps.chroma_format_idc);

    if ( (guint) p_header->sps.chroma_format_idc > 3U) {
      GST_ERROR ("chroma_format_idc %d is illegal\n", p_header->sps.chroma_format_idc);
      ret = COM_ECODE_BAD_PARAMS;
      goto gstGetVideoCodecH264Parser_failexit;

    } else if (p_header->sps.chroma_format_idc == 3) {
      p_header->sps.residual_color_transform_flag = _get_bits1 (&gb);
      GST_LOG ("[parse sps]: residual_color_transform_flag %d\n", p_header->sps.residual_color_transform_flag);

      if (p_header->sps.residual_color_transform_flag) {
        GST_ERROR ("separate color planes are not supported\n");
        ret = COM_ECODE_BAD_PARAMS;
        goto gstGetVideoCodecH264Parser_failexit;
      }
    }

    p_header->sps.bit_depth_luma   = _get_ue_golomb (&gb) + 8;
    p_header->sps.bit_depth_chroma = _get_ue_golomb (&gb) + 8;
    GST_LOG ("[parse sps]: bit_depth_luma %d, bit_depth_chroma %d\n", p_header->sps.bit_depth_luma, p_header->sps.bit_depth_chroma);

    if ( (guint) p_header->sps.bit_depth_luma > 14U || (guint) p_header->sps.bit_depth_chroma > 14U || p_header->sps.bit_depth_luma != p_header->sps.bit_depth_chroma) {
      GST_ERROR ("illegal bit depth value (%d, %d)\n", p_header->sps.bit_depth_luma, p_header->sps.bit_depth_chroma);
      ret = COM_ECODE_BAD_PARAMS;
      goto gstGetVideoCodecH264Parser_failexit;
    }

    p_header->sps.transform_bypass = _get_bits1 (&gb);
    GST_LOG ("[parse sps]: transform_bypass %d\n", p_header->sps.transform_bypass);
    _decode_scaling_matrices (&gb, &p_header->sps, NULL, 1, p_header->sps.scaling_matrix4, p_header->sps.scaling_matrix8);

  } else {
    p_header->sps.chroma_format_idc = 1;
    p_header->sps.bit_depth_luma   = 8;
    p_header->sps.bit_depth_chroma = 8;
  }

  log2_max_frame_num_minus4 = _get_ue_golomb (&gb);

  if (log2_max_frame_num_minus4 < (DMIN_LOG2_MAX_FRAME_NUM - 4) ||
      log2_max_frame_num_minus4 > (DMAX_LOG2_MAX_FRAME_NUM - 4) ) {
    GST_ERROR ("log2_max_frame_num_minus4 out of range (0-12): %d\n",
               log2_max_frame_num_minus4);
    ret = COM_ECODE_BAD_PARAMS;
    goto gstGetVideoCodecH264Parser_failexit;
  }

  p_header->sps.log2_max_frame_num = log2_max_frame_num_minus4 + 4;
  p_header->sps.poc_type = _get_ue_golomb_31 (&gb);

  GST_LOG ("[parse sps]: poc_type %d, log2_max_frame_num %d\n", p_header->sps.poc_type, p_header->sps.log2_max_frame_num);

  if (p_header->sps.poc_type == 0) {
    guint t = _get_ue_golomb (&gb);

    if (t > 12) {
      GST_ERROR ("log2_max_poc_lsb (%d) is out of range\n", t);
      ret = COM_ECODE_BAD_PARAMS;
      goto gstGetVideoCodecH264Parser_failexit;
    }

    p_header->sps.log2_max_poc_lsb = t + 4;

  } else if (p_header->sps.poc_type == 1) {
    p_header->sps.delta_pic_order_always_zero_flag = _get_bits1 (&gb);
    p_header->sps.offset_for_non_ref_pic = _get_se_golomb (&gb);
    p_header->sps.offset_for_top_to_bottom_field = _get_se_golomb (&gb);
    p_header->sps.poc_cycle_length                = _get_ue_golomb (&gb);

    if ( (guint) p_header->sps.poc_cycle_length >= DARRAY_ELEMS (p_header->sps.offset_for_ref_frame) ) {
      GST_ERROR ("poc_cycle_length overflow %u\n", p_header->sps.poc_cycle_length);
      ret = COM_ECODE_BAD_PARAMS;
      goto gstGetVideoCodecH264Parser_failexit;
    }

    for (i = 0; i < p_header->sps.poc_cycle_length; i++) {
      p_header->sps.offset_for_ref_frame[i] = _get_se_golomb (&gb);
    }

  } else if (p_header->sps.poc_type != 2) {
    GST_ERROR ("illegal POC type %d\n", p_header->sps.poc_type);
    ret = COM_ECODE_BAD_PARAMS;
    goto gstGetVideoCodecH264Parser_failexit;
  }

  p_header->sps.ref_frame_count = _get_ue_golomb_31 (&gb);

  if ( (p_header->sps.ref_frame_count > (DMAX_PICTURE_COUNT - 2) ) || ( (guint) p_header->sps.ref_frame_count > 16U) ) {
    GST_ERROR ("too many reference frames, %d\n", p_header->sps.ref_frame_count);
    ret = COM_ECODE_BAD_PARAMS;
    goto gstGetVideoCodecH264Parser_failexit;
  }

  GST_LOG ("[parse sps]: ref_frame_count %d\n", p_header->sps.ref_frame_count);

  p_header->sps.gaps_in_frame_num_allowed_flag = _get_bits1 (&gb);
  p_header->sps.mb_width = _get_ue_golomb (&gb) + 1;
  p_header->sps.mb_height = _get_ue_golomb (&gb) + 1;

  p_header->common.max_width = 16 * p_header->sps.mb_width;
  p_header->common.max_height = 16 * p_header->sps.mb_height;
  GST_LOG ("[parse sps]: picture width %d, height %d\n", p_header->common.max_width, p_header->common.max_height);
  return p_header;

gstGetVideoCodecH264Parser_failexit:

  if (G_LIKELY (p_header) ) {
    free (p_header, "G264");
  }

  return NULL;
}

guchar gstGetH264SilceType (guchar *pdata)
{
  guchar slice_type = 0, first_mb_in_slice = 0;
  GetbitsContext gb;

  gb.buffer = pdata;
  gb.buffer_end = pdata + 16;
  gb.index = 0;
  gb.size_in_bits = 16 * 8;

  first_mb_in_slice = _get_ue_golomb (&gb);
  slice_type = _get_ue_golomb_31 (&gb);

  if (G_UNLIKELY (EH264SliceType_MaxValue < slice_type) ) {
    GST_ERROR ("BAD slice_type %d, first_mb_in_slice %d\n", slice_type, first_mb_in_slice);
    return 0;
  }

  if (slice_type >= EH264SliceType_FieldOffset) {
    slice_type -= EH264SliceType_FieldOffset;
  }

  return slice_type;
}

