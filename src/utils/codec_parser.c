/*******************************************************************************
 * codec_parser.c
 *
 * History:
 *    2015/02/10 - [Zhi He] create file
 *
 * Copyright (c) 2016 Ambarella International LP
 *
 * This file and its contents ( "Software" ) are protected by intellectual
 * property rights including, without limitation, U.S. and/or foreign
 * copyrights. This Software is also the confidential and proprietary
 * information of Ambarella International LP and its licensors. You may not use, reproduce,
 * disclose, distribute, modify, or otherwise prepare derivative works of this
 * Software or any portion thereof except pursuant to a signed license agreement
 * or nondisclosure agreement with Ambarella International LP or its authorized affiliates.
 * In the absence of such an agreement, you agree to promptly notify and return
 * this Software to Ambarella International LP.
 *
 * This file includes sample code and is only for internal testing and evaluation.  If you
 * distribute this sample code (whether in source, object, or binary code form), it will be
 * without any warranty or indemnity protection from Ambarella International LP or its affiliates.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF NON-INFRINGEMENT,
 * MERCHANTABILITY, AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL AMBARELLA INTERNATIONAL LP OR ITS AFFILIATES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; COMPUTER FAILURE OR MALFUNCTION; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
******************************************************************************/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DQP_MAX_NUM (51 + 6*6)
#define DMAX_SUB_LAYERS 7
#define DMAX_SPS_COUNT          32
#define DMAX_PPS_COUNT         256
#define DMIN_LOG2_MAX_FRAME_NUM 4
#define DMAX_LOG2_MAX_FRAME_NUM 16
#define DMAX_PICTURE_COUNT 36
#define DEXTENDED_SAR          255
#define DMIN_CACHE_BITS 25
#define DARRAY_ELEMS(a) (sizeof(a) / sizeof((a)[0]))

//simple parse slice_type for h264
typedef struct {
    const unsigned char *buffer, *buffer_end;
    int index;
    int size_in_bits;
} GetbitsContext;

#define READ_LE_32(x)   \
    ((((const unsigned char*)(x))[3] << 24) |   \
     (((const unsigned char*)(x))[2] << 16) |    \
     (((const unsigned char*)(x))[1] <<  8) |     \
     ((const unsigned char*)(x))[0])

#define READ_BE_32(x)   \
    ((((const unsigned char*)(x))[0] << 24) |   \
     (((const unsigned char*)(x))[1] << 16) |    \
     (((const unsigned char*)(x))[2] <<  8) |     \
     ((const unsigned char*)(x))[3])

#define BITS_OPEN_READER(name, gb)   \
    unsigned int name##_index = (gb)->index;    \
    int name##_cache    = 0

#define BITS_CLOSE_READER(name, gb) (gb)->index = name##_index
#define BITS_UPDATE_CACHE_BE(name, gb) \
    name##_cache = READ_BE_32(((const unsigned char *)(gb)->buffer)+(name##_index>>3)) << (name##_index&0x07)
#define BITS_UPDATE_CACHE_LE(name, gb) \
    name##_cache = READ_LE_32(((const unsigned char *)(gb)->buffer)+(name##_index>>3)) >> (name##_index&0x07)

#define BITS_SKIP_CACHE(name, gb, num) name##_cache >>= (num)

#define BITS_SKIP_COUNTER(name, gb, num) name##_index += (num)

#define BITS_SKIP_BITS(name, gb, num) do {  \
        BITS_SKIP_CACHE(name, gb, num); \
        BITS_SKIP_COUNTER(name, gb, num);    \
    } while (0)

#define BITS_LAST_SKIP_BITS(name, gb, num) BITS_SKIP_COUNTER(name, gb, num)
#define BITS_LAST_SKIP_CACHE(name, gb, num)

#define NEG_USR32(a,s) (((unsigned int)(a))>>(32-(s)))
#define BITS_SHOW_UBITS(name, gb, num) NEG_USR32(name ## _cache, num)

#define BITS_GET_CACHE(name, gb) ((unsigned int)name##_cache)

#define BITS_OPEN_READER_SKIP(name, gb)   \
    unsigned int name##_index = (gb)->index

#define BITS_CLOSE_READER_SKIP(name, gb) \
    (gb)->index = name##_index

//log2
const unsigned char simple_log2_table[256] = {
    0, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7
};

const unsigned char simple_golomb_vlc_len[512] = {
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

const unsigned char simple_ue_golomb_vlc_code[512] = {
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

const char simple_se_golomb_vlc_code[512] = {
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

static const unsigned char default_scaling4[2][16] = {
    {
        6, 13, 20, 28,
        13, 20, 28, 32,
        20, 28, 32, 37,
        28, 32, 37, 42
    },

    {
        10, 14, 20, 24,
        14, 20, 24, 27,
        20, 24, 27, 30,
        24, 27, 30, 34
    }
};

static const unsigned char default_scaling8[2][64] = {
    {
        6, 10, 13, 16, 18, 23, 25, 27,
        10, 11, 16, 18, 23, 25, 27, 29,
        13, 16, 18, 23, 25, 27, 29, 31,
        16, 18, 23, 25, 27, 29, 31, 33,
        18, 23, 25, 27, 29, 31, 33, 36,
        23, 25, 27, 29, 31, 33, 36, 38,
        25, 27, 29, 31, 33, 36, 38, 40,
        27, 29, 31, 33, 36, 38, 40, 42
    },
    {
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

static const unsigned char simple_zigzag_scan[16] = {
    0 + 0 * 4, 1 + 0 * 4, 0 + 1 * 4, 0 + 2 * 4,
    1 + 1 * 4, 2 + 0 * 4, 3 + 0 * 4, 2 + 1 * 4,
    1 + 2 * 4, 0 + 3 * 4, 1 + 3 * 4, 2 + 2 * 4,
    3 + 1 * 4, 3 + 2 * 4, 2 + 3 * 4, 3 + 3 * 4,
};

const unsigned char simple_zigzag_direct[64] = {
    0,   1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

static int simple_log2_c(unsigned int v)
{
    int n = 0;
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

static inline int _get_ue_golomb(GetbitsContext *gb)
{
    unsigned int buf;
    int log;

    // Check if we have enough bits
    if (gb->index >= gb->size_in_bits - 32) {
        printf("UE Golomb: not enough bits remaining (%d/%d)\n", gb->index, gb->size_in_bits);
        return 0;
    }

    BITS_OPEN_READER(re, gb);
    BITS_UPDATE_CACHE_BE(re, gb);
    buf = BITS_GET_CACHE(re, gb);
    if (buf >= (1 << 27)) {
        buf >>= 32 - 9;

        // Validate array access
        if (buf >= 512) {
            printf("UE Golomb: buffer index out of range (%u)\n", buf);
            BITS_CLOSE_READER(re, gb);
            return 0;
        }

        BITS_LAST_SKIP_BITS(re, gb, simple_golomb_vlc_len[buf]);
        BITS_CLOSE_READER(re, gb);
        return simple_ue_golomb_vlc_code[buf];
    } else {
        if (buf == 0) {
            printf("UE Golomb: invalid zero buffer\n");
            BITS_CLOSE_READER(re, gb);
            return 0;
        }

        log = 2 * simple_log2_c(buf) - 31;

        // Check if we have enough bits for the operation
        if (gb->index + (32 - log) > gb->size_in_bits) {
            printf("UE Golomb: not enough bits for operation\n");
            BITS_CLOSE_READER(re, gb);
            return 0;
        }

        buf >>= log;
        buf--;
        BITS_LAST_SKIP_BITS(re, gb, 32 - log);
        BITS_CLOSE_READER(re, gb);
        return buf;
    }
}

static inline int _get_ue_golomb_31(GetbitsContext *gb)
{
    unsigned int buf;

    // Check if we have enough bits
    if (gb->index >= gb->size_in_bits - 32) {
        printf("UE Golomb 31: not enough bits remaining (%d/%d)\n", gb->index, gb->size_in_bits);
        return 0;
    }

    BITS_OPEN_READER(re, gb);
    BITS_UPDATE_CACHE_BE(re, gb);
    buf = BITS_GET_CACHE(re, gb);
    buf >>= 32 - 9;

    // Validate array access
    if (buf >= 512) {
        printf("UE Golomb 31: buffer index out of range (%u)\n", buf);
        BITS_CLOSE_READER(re, gb);
        return 0;
    }

    BITS_LAST_SKIP_BITS(re, gb, simple_golomb_vlc_len[buf]);
    BITS_CLOSE_READER(re, gb);

    return simple_ue_golomb_vlc_code[buf];
}

static inline int _get_se_golomb(GetbitsContext *gb)
{
    unsigned int buf;
    int log;

    BITS_OPEN_READER(re, gb);
    BITS_UPDATE_CACHE_BE(re, gb);
    buf = BITS_GET_CACHE(re, gb);

    if (buf >= (1 << 27)) {
        buf >>= 32 - 9;
        BITS_LAST_SKIP_BITS(re, gb, simple_golomb_vlc_len[buf]);
        BITS_CLOSE_READER(re, gb);

        return simple_se_golomb_vlc_code[buf];
    } else {
        log = simple_log2_c(buf);
        BITS_LAST_SKIP_BITS(re, gb, 31 - log);
        BITS_UPDATE_CACHE_BE(re, gb);
        buf = BITS_GET_CACHE(re, gb);
        buf >>= log;

        BITS_LAST_SKIP_BITS(re, gb, 32 - log);
        BITS_CLOSE_READER(re, gb);

        if (buf & 1) {
            buf = -(buf >> 1);
        } else {
            buf = (buf >> 1);
        }
        return buf;
    }
}

static inline unsigned int _get_bits(GetbitsContext *s, int n)
{
    int tmp;

    // Check if we have enough bits
    if (s->index + n > s->size_in_bits) {
        printf("Get bits: not enough bits remaining (%d + %d > %d)\n", s->index, n, s->size_in_bits);
        return 0;
    }

    BITS_OPEN_READER(re, s);
    BITS_UPDATE_CACHE_BE(re, s);
    tmp = BITS_SHOW_UBITS(re, s, n);
    BITS_LAST_SKIP_BITS(re, s, n);
    BITS_CLOSE_READER(re, s);
    return tmp;
}

static inline unsigned int _get_bits1(GetbitsContext *s)
{
    unsigned int index = s->index;
    unsigned char result  = s->buffer[index >> 3];

    result <<= index & 7;
    result >>= 8 - 1;

    index++;
    s->index = index;

    return result;
}

static inline unsigned int _get_bits_long(GetbitsContext *s, int n)
{
    if (!n) {
        return 0;
    } else if (n <= DMIN_CACHE_BITS) {
        return _get_bits(s, n);
    } else {
        unsigned ret = _get_bits(s, 16) << (n - 16);
        return ret | _get_bits(s, n - 16);
    }
}

static unsigned long long _get_bits64(GetbitsContext *s, int n)
{
    if (n <= 32) {
        return _get_bits_long(s, n);
    } else {
        unsigned long long ret = (unsigned long long) _get_bits_long(s, n - 32) << 32;
        return ret | _get_bits_long(s, 32);
    }
}

static inline unsigned int _show_bits(GetbitsContext *s, int n)
{
    register int tmp;
    BITS_OPEN_READER(re, s);
    BITS_UPDATE_CACHE_BE(re, s);
    tmp = BITS_SHOW_UBITS(re, s, n);
    return tmp;
}

static inline unsigned int _show_bits1(GetbitsContext *s)
{
    return _show_bits(s, 1);
}

unsigned char get_h264_slice_type_le(unsigned char *pdata, unsigned char *first_mb_in_slice)
{
    unsigned char slice_type = 0;
    GetbitsContext gb;

    gb.buffer = pdata;
    gb.buffer_end = pdata + 8;//hard code here
    gb.index = 0;
    gb.size_in_bits = 8 * 8;

    *first_mb_in_slice = _get_ue_golomb(&gb);
    slice_type = _get_ue_golomb_31(&gb);

    if (slice_type > 9) {
        printf("BAD slice_type %d, first_mb_in_slice %d\n", slice_type, *first_mb_in_slice);
        return 0;
    }

    if (slice_type > 4) {
        slice_type -= 5;
    }

    return slice_type;//0 I P B S SI SP BI
}

typedef struct {
    unsigned int sps_id;
    unsigned int profile_idc;
    unsigned int level_idc;
    unsigned int chroma_idc;
    unsigned int residual_color_transform_flag;
    unsigned int bit_depth_luma;
    unsigned int bit_depth_chroma;
    unsigned int log2_max_frame_num_minus4;

    unsigned int log2_max_frame_num;
    unsigned int poc_type;
    unsigned int log2_max_poc_lsb;
    unsigned int delta_pic_order_always_zero_flag;
    unsigned int offset_for_non_ref_pic;
    unsigned int offset_for_top_to_bottom_field;
    unsigned int poc_cycle_length;
    unsigned int ref_frame_count;

    unsigned short offset_for_ref_frame[256];

    unsigned char scaling_matrix4[6][16];
    unsigned char scaling_matrix8[6][64];
} s_h264_sps;

static void _decode_scaling_list(GetbitsContext *gb, unsigned char *factors, int size,
                                 const unsigned char *jvt_list, const unsigned char *fallback_list)
{
    int i, last = 8, next = 8;
    const unsigned char *scan = (size == 16) ? simple_zigzag_scan : simple_zigzag_direct;
    if (!_get_bits1(gb)) {
        memcpy(factors, fallback_list, size * sizeof(unsigned char));
    } else {
        for (i = 0; i < size; i++) {
            if (next) {
                next = (last + _get_se_golomb(gb)) & 0xff;
            }
            if (!i && !next) {
                memcpy(factors, jvt_list, size * sizeof(unsigned char));
                break;
            }
            last = factors[scan[i]] = next ? next : last;
        }
    }
}

static void _decode_scaling_matrices(GetbitsContext *gb, s_h264_sps *sps, unsigned char(*scaling_matrix4)[16], unsigned char(*scaling_matrix8)[64])
{
    const unsigned char *fallback[4] = {
        default_scaling4[0],
        default_scaling4[1],
        default_scaling8[0],
        default_scaling8[1]
    };

    if (_get_bits1(gb)) {
        _decode_scaling_list(gb, scaling_matrix4[0], 16, default_scaling4[0], fallback[0]); // Intra, Y
        _decode_scaling_list(gb, scaling_matrix4[1], 16, default_scaling4[0], scaling_matrix4[0]); // Intra, Cr
        _decode_scaling_list(gb, scaling_matrix4[2], 16, default_scaling4[0], scaling_matrix4[1]); // Intra, Cb
        _decode_scaling_list(gb, scaling_matrix4[3], 16, default_scaling4[1], fallback[1]); // Inter, Y
        _decode_scaling_list(gb, scaling_matrix4[4], 16, default_scaling4[1], scaling_matrix4[3]); // Inter, Cr
        _decode_scaling_list(gb, scaling_matrix4[5], 16, default_scaling4[1], scaling_matrix4[4]); // Inter, Cb

        _decode_scaling_list(gb, scaling_matrix8[0], 64, default_scaling8[0], fallback[2]); // Intra, Y
        _decode_scaling_list(gb, scaling_matrix8[3], 64, default_scaling8[1], fallback[3]); // Inter, Y
        if (sps->chroma_idc == 3) {
            _decode_scaling_list(gb, scaling_matrix8[1], 64, default_scaling8[0], scaling_matrix8[0]); // Intra, Cr
            _decode_scaling_list(gb, scaling_matrix8[4], 64, default_scaling8[1], scaling_matrix8[3]); // Inter, Cr
            _decode_scaling_list(gb, scaling_matrix8[2], 64, default_scaling8[0], scaling_matrix8[1]); // Intra, Cb
            _decode_scaling_list(gb, scaling_matrix8[5], 64, default_scaling8[1], scaling_matrix8[4]); // Inter, Cb
        }
    }
}

int get_h264_reso_from_sps(unsigned char *p_data, unsigned int data_size, unsigned int *width, unsigned int *height)
{
    s_h264_sps sps;
    GetbitsContext gb;
    unsigned int i;

    if (!p_data || !data_size || !width || !height) {
        printf("H264 SPS: NULL params\n");
        return (-1);
    }

    // Initialize output parameters
    *width = 0;
    *height = 0;

    memset(&sps, 0x0, sizeof(sps));

    gb.buffer = p_data;
    gb.buffer_end = p_data + data_size;
    gb.index = 0;
    gb.size_in_bits = 8 * data_size;

    printf("H264 SPS: data_size=%u, size_in_bits=%d\n", data_size, gb.size_in_bits);

    // Print hex dump of first 16 bytes (or all bytes if less than 16)
    printf("H264 SPS hex dump: ");
    for (unsigned int i = 0; i < (data_size < 16 ? data_size : 16); i++) {
        printf("%02x ", p_data[i]);
    }
    printf("\n");

    // Check minimum SPS size
    if (data_size < 4) {
        printf("H264 SPS too short: %u bytes\n", data_size);
        return (-2);
    }

    // Check if we have enough bits for basic parsing
    if (gb.size_in_bits < 32) {
        printf("H264 SPS insufficient bits: %d\n", gb.size_in_bits);
        return (-3);
    }

        sps.profile_idc = _get_bits(&gb, 8);
    _get_bits(&gb, 8);  // constraint flags
    sps.level_idc = _get_bits(&gb, 8);

    printf("H264 SPS: profile_idc=%u, level_idc=%u\n", sps.profile_idc, sps.level_idc);

    // Sanity check for common H264 profiles
    if (sps.profile_idc != 66 && sps.profile_idc != 77 && sps.profile_idc != 88 &&
        sps.profile_idc != 100 && sps.profile_idc != 110 && sps.profile_idc != 122 &&
        sps.profile_idc != 244 && sps.profile_idc != 44 && sps.profile_idc != 83 &&
        sps.profile_idc != 86 && sps.profile_idc != 118 && sps.profile_idc != 128 &&
        sps.profile_idc != 144) {
        printf("H264 SPS: Warning - unusual profile_idc=%u, data may be misaligned\n", sps.profile_idc);
        // Continue parsing anyway, but this indicates a potential issue
    }

    // Check if we have enough bits for sps_id parsing
    if (gb.index >= gb.size_in_bits - 8) {
        printf("H264 SPS: not enough bits for sps_id\n");
        return (-4);
    }

    sps.sps_id = _get_ue_golomb_31(&gb);

    printf("H264 SPS: sps_id=%u (max allowed: %d)\n", sps.sps_id, DMAX_SPS_COUNT - 1);

    if (sps.sps_id >= DMAX_SPS_COUNT) {
        printf("sps_id (%d) out of range\n", sps.sps_id);
        return (-5);
    }

    printf("H264 SPS: checking profile_idc=%u for high profile features, bits_consumed=%d\n", sps.profile_idc, gb.index);

    if (sps.profile_idc == 100 || sps.profile_idc == 110 ||
            sps.profile_idc == 122 || sps.profile_idc == 244 ||
            sps.profile_idc ==  44 || sps.profile_idc ==  83 ||
            sps.profile_idc ==  86 || sps.profile_idc == 118 ||
            sps.profile_idc == 128 || sps.profile_idc == 144) {
        printf("H264 SPS: high profile detected, parsing extended features\n");
        sps.chroma_idc = _get_ue_golomb_31(&gb);
        printf("H264 SPS: chroma_idc=%u, bits_consumed=%d\n", sps.chroma_idc, gb.index);

        if (sps.chroma_idc > 3) {
            printf("chroma_idc %d is illegal\n", sps.chroma_idc);
            return (-2);
        } else if (sps.chroma_idc == 3) {
            sps.residual_color_transform_flag = _get_bits1(&gb);
            if (sps.residual_color_transform_flag) {
                printf("separate color planes are not supported\n");
                return (-3);
            }
        }
        sps.bit_depth_luma   = _get_ue_golomb(&gb) + 8;
        sps.bit_depth_chroma = _get_ue_golomb(&gb) + 8;
        printf("H264 SPS: bit_depth_luma=%u, bit_depth_chroma=%u, bits_consumed=%d\n",
               sps.bit_depth_luma, sps.bit_depth_chroma, gb.index);
        if (sps.bit_depth_luma > 14 || sps.bit_depth_chroma > 14 || sps.bit_depth_luma != sps.bit_depth_chroma) {
            printf("illegal bit depth value (%d, %d)\n", sps.bit_depth_luma, sps.bit_depth_chroma);
            return (-4);
        }
        _get_bits1(&gb);
        _decode_scaling_matrices(&gb, &sps, sps.scaling_matrix4, sps.scaling_matrix8);
        printf("H264 SPS: completed high profile parsing, bits_consumed=%d\n", gb.index);
    } else {
        printf("H264 SPS: baseline/main profile, using default values\n");
        sps.chroma_idc = 1;
        sps.bit_depth_luma   = 8;
        sps.bit_depth_chroma = 8;
    }

    sps.log2_max_frame_num_minus4 = _get_ue_golomb(&gb);
    printf("H264 SPS: log2_max_frame_num_minus4=%u, bits_consumed=%d\n", sps.log2_max_frame_num_minus4, gb.index);

    if ((sps.log2_max_frame_num_minus4 + 4) < DMIN_LOG2_MAX_FRAME_NUM ||
            sps.log2_max_frame_num_minus4 > (DMAX_LOG2_MAX_FRAME_NUM - 4)) {
        printf("log2_max_frame_num_minus4 out of range (0-12): %d\n", sps.log2_max_frame_num_minus4);
        return (-5);
    }
    sps.log2_max_frame_num = sps.log2_max_frame_num_minus4 + 4;
    sps.poc_type = _get_ue_golomb_31(&gb);
    printf("H264 SPS: poc_type=%u, bits_consumed=%d\n", sps.poc_type, gb.index);

    if (sps.poc_type == 0) {
        unsigned int t = _get_ue_golomb(&gb);
        printf("H264 SPS: log2_max_poc_lsb_minus4=%u, bits_consumed=%d\n", t, gb.index);
        if (t > 12) {
            printf("log2_max_poc_lsb (%d) is out of range\n", t);
            return (-6);
        }
        sps.log2_max_poc_lsb = t + 4;
    } else if (sps.poc_type == 1) {
        sps.delta_pic_order_always_zero_flag = _get_bits1(&gb);
        sps.offset_for_non_ref_pic = _get_se_golomb(&gb);
        sps.offset_for_top_to_bottom_field = _get_se_golomb(&gb);
        sps.poc_cycle_length                = _get_ue_golomb(&gb);
        printf("H264 SPS: poc_type=1, poc_cycle_length=%u, bits_consumed=%d\n", sps.poc_cycle_length, gb.index);

        if (sps.poc_cycle_length >= DARRAY_ELEMS(sps.offset_for_ref_frame)) {
            printf("poc_cycle_length overflow %u\n", sps.poc_cycle_length);
            return (-7);
        }

        for (i = 0; i < sps.poc_cycle_length; i++) {
            sps.offset_for_ref_frame[i] = _get_se_golomb(&gb);
        }
    } else if (sps.poc_type != 2) {
        printf("illegal POC type %d\n", sps.poc_type);
        return (-8);
    }

    sps.ref_frame_count = _get_ue_golomb_31(&gb);
    printf("H264 SPS: ref_frame_count=%u, bits_consumed=%d\n", sps.ref_frame_count, gb.index);

    if ((sps.ref_frame_count > (DMAX_PICTURE_COUNT - 2)) || (sps.ref_frame_count > 16)) {
        printf("too many reference frames, %d\n", sps.ref_frame_count);
        return (-9);
    }

    _get_bits1(&gb);

    // Check if we have enough bits for width/height
    if (gb.index >= gb.size_in_bits - 16) {
        printf("H264 SPS: not enough bits for width/height\n");
        return (-10);
    }

    *width = (_get_ue_golomb(&gb) + 1) * 16;
    *height = (_get_ue_golomb(&gb) + 1) * 16;

    printf("H264 SPS parsed: width=%u, height=%u, remaining_bits=%d\n",
           *width, *height, gb.size_in_bits - gb.index);

    // Validate dimensions
    if (*width == 0 || *height == 0 || *width > 8192 || *height > 8192) {
        printf("H264 SPS: invalid dimensions %ux%u\n", *width, *height);
        *width = 0;
        *height = 0;
        return (-11);
    }

    return 0;
}

static inline void _skip_bits(GetbitsContext *s, int n)
{
    BITS_OPEN_READER_SKIP(re, s);
    BITS_LAST_SKIP_BITS(re, s, n);
    BITS_CLOSE_READER_SKIP(re, s);
}

static inline void _skip_bits1(GetbitsContext *s)
{
    _skip_bits(s, 1);
}

static inline unsigned int _show_bits_long(GetbitsContext *s, int n)
{
    if (n <= DMIN_CACHE_BITS) {
        return _show_bits(s, n);
    } else {
        GetbitsContext gb = *s;
        return _get_bits_long(&gb, n);
    }
}

static inline void _skip_bits_long(GetbitsContext *s, int n)
{
    s->index += n;
}

static inline unsigned _get_ue_golomb_long(GetbitsContext *gb)
{
    unsigned buf, log;

    // Check if we have enough bits
    if (gb->index >= gb->size_in_bits - 32) {
        printf("UE Golomb: not enough bits remaining (%d/%d)\n", gb->index, gb->size_in_bits);
        return 0;
    }

    buf = _show_bits_long(gb, 32);
    if (buf == 0) {
        printf("UE Golomb: invalid zero buffer\n");
        return 0;
    }

    log = 31 - simple_log2_c(buf);

    // Check if we have enough bits for the full value
    if ((int) (gb->index + log + (log + 1)) > gb->size_in_bits) {
        printf("UE Golomb: not enough bits for value (need %d, have %d)\n",
               log + (log + 1), gb->size_in_bits - gb->index);
        return 0;
    }

    _skip_bits_long(gb, log);

    return _get_bits_long(gb, log + 1) - 1;
}

static inline int _get_se_golomb_long(GetbitsContext *gb)
{
    unsigned int buf = _get_ue_golomb_long(gb);

    if (buf & 1)
    { buf = (buf + 1) >> 1; }
    else
    { buf = -(buf >> 1); }

    return buf;
}

typedef struct SHVCCProfileTierLevel {
    unsigned char  profile_space;
    unsigned char  tier_flag;
    unsigned char  profile_idc;
    unsigned int profile_compatibility_flags;
    unsigned long long constraint_indicator_flags;
    unsigned char  level_idc;
} SHVCCProfileTierLevel;

static unsigned char *_rbsp_from_nalu(const unsigned char *src, unsigned int src_len, unsigned int *dst_len)
{
    unsigned char *dst;
    unsigned int i, len;

    dst = (unsigned char *) malloc(src_len);
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

static void _parse_ptl(GetbitsContext *gb, unsigned int max_sub_layers_minus1, SHVCCProfileTierLevel *general_ptl)
{
    unsigned int i = 0;
    unsigned char sub_layer_profile_present_flag[DMAX_SUB_LAYERS];
    unsigned char sub_layer_level_present_flag[DMAX_SUB_LAYERS];

    general_ptl->profile_space               = _get_bits(gb, 2);
    general_ptl->tier_flag                   = _get_bits1(gb);
    general_ptl->profile_idc                 = _get_bits(gb, 5);
    general_ptl->profile_compatibility_flags = _get_bits_long(gb, 32);
    general_ptl->constraint_indicator_flags  = _get_bits64(gb, 48);
    general_ptl->level_idc                   = _get_bits(gb, 8);

    for (i = 0; i < max_sub_layers_minus1; i++) {
        sub_layer_profile_present_flag[i] = _get_bits1(gb);
        sub_layer_level_present_flag[i]   = _get_bits1(gb);
    }

    if (max_sub_layers_minus1 > 0) {
        for (i = max_sub_layers_minus1; i < 8; i++) {
            _skip_bits(gb, 2);
        }
    }

    for (i = 0; i < max_sub_layers_minus1; i++) {
        if (sub_layer_profile_present_flag[i]) {
            _skip_bits_long(gb, 32);
            _skip_bits_long(gb, 32);
            _skip_bits(gb, 24);
        }

        if (sub_layer_level_present_flag[i])
        { _skip_bits(gb, 8); }
    }
}

int get_h265_reso_from_sps(unsigned char *p_data, unsigned int data_size, unsigned int *pic_width, unsigned int *pic_height)
{
    if (!p_data || !data_size || !pic_width || !pic_height) {
        printf("NULL params in get_h265_reso_from_sps()\n");
        return (-1);
    }

    // Initialize output parameters
    *pic_width = 0;
    *pic_height = 0;

    unsigned int rbsp_len = 0;
    unsigned char *p_rbsp = _rbsp_from_nalu(p_data, data_size, &rbsp_len);
    if (!p_rbsp || !rbsp_len) {
        printf("no memory when parse sps, rbsp_len=%u\n", rbsp_len);
        return (-2);
    }

    printf("H265 SPS: data_size=%u, rbsp_len=%u\n", data_size, rbsp_len);

    GetbitsContext gb;
    gb.buffer = p_rbsp;
    gb.buffer_end = p_rbsp + rbsp_len;
    gb.index = 0;
    gb.size_in_bits = 8 * rbsp_len;

    // Check minimum SPS size
    if (rbsp_len < 10) {
        printf("H265 SPS too short: %u bytes\n", rbsp_len);
        free(p_rbsp);
        return (-3);
    }

    unsigned int sps_max_sub_layers_minus1;
    SHVCCProfileTierLevel general_ptl;

    // Check if we have enough bits for basic parsing
    if (gb.size_in_bits < 64) {
        printf("H265 SPS insufficient bits: %d\n", gb.size_in_bits);
        free(p_rbsp);
        return (-4);
    }

    _skip_bits(&gb, 4);
    sps_max_sub_layers_minus1 = _get_bits(&gb, 3);
    _get_bits1(&gb);

    printf("H265 SPS: sps_max_sub_layers_minus1=%u\n", sps_max_sub_layers_minus1);

    if (sps_max_sub_layers_minus1 >= DMAX_SUB_LAYERS) {
        printf("H265 SPS: invalid sps_max_sub_layers_minus1=%u\n", sps_max_sub_layers_minus1);
        free(p_rbsp);
        return (-5);
    }

    _parse_ptl(&gb, sps_max_sub_layers_minus1, &general_ptl);

    // Check if we still have enough bits
    if (gb.index >= gb.size_in_bits - 32) {
        printf("H265 SPS: not enough bits remaining after PTL parsing\n");
        free(p_rbsp);
        return (-6);
    }

    _get_ue_golomb_long(&gb);
    unsigned char chomaformat = _get_ue_golomb_long(&gb);

    printf("H265 SPS: chroma_format=%u\n", chomaformat);

    if (chomaformat == 3) {
        _skip_bits1(&gb);
    }

    // Check if we have enough bits for width/height
    if (gb.index >= gb.size_in_bits - 16) {
        printf("H265 SPS: not enough bits for width/height\n");
        free(p_rbsp);
        return (-7);
    }

    *pic_width = _get_ue_golomb_long(&gb);
    *pic_height = _get_ue_golomb_long(&gb);

    printf("H265 SPS parsed: width=%u, height=%u, remaining_bits=%d\n",
           *pic_width, *pic_height, gb.size_in_bits - gb.index);

    // Validate dimensions
    if (*pic_width == 0 || *pic_height == 0 || *pic_width > 8192 || *pic_height > 8192) {
        printf("H265 SPS: invalid dimensions %ux%u\n", *pic_width, *pic_height);
        *pic_width = 0;
        *pic_height = 0;
        free(p_rbsp);
        return (-8);
    }

    free(p_rbsp);

    return 0;
}
