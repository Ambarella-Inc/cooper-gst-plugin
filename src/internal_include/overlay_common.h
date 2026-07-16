/*
 * overlay_common.h
 *
 * History:
 *    4/2/2024 - [pxduan] created file
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

#ifndef __OVERLAY_COMMON_H__
#define __OVERLAY_COMMON_H__

#include "iav_al.h"
#include "osd_draw_types.h"


#define OSD_DEFAULT_ENCODE_STREAM_NUM (1)
#define OSD_DEFAULT_UPDATE_INTERVALS (30)
#define OSD_DEFAULT_AREA_NUM (1)

#define DOUBLE_BUFFER_MIN_UPDATE_INTERVALS (5)
#define OVERLAY_CLUT_MAX_NUM (OVERLAY_CLUT_SIZE / sizeof(amba_draw_clut_t))
#define AMBA_DRAW_CLUT_ENTRY_BACKGROUND   (OVERLAY_CLUT_MAX_NUM - 1) //!< it defines background color

typedef struct {
  unsigned char *buf;
  unsigned int size;
} bitmap_buffer_t;

typedef struct {
  unsigned char enable; //!< It determines whether to enable the area
  unsigned char rotate; //!< if set to 0, osd area will not auto flip and rotate when encode stream is flip and rotate state. AM_OVERLAY_AREA_ALL_ROTATE;
                       //!< if set to 1, osd area will auto flip and rotate when encode stream is flip and rotate state
                       //!< if set to 2, osd area will auto flip and rotate when encode stream is flip and rotate state,but rectangle's string will not auto flip or rotate.
  unsigned char stream_rotate; //!< stream is flip or rotate state.
  unsigned char reserved;
  int buf_num; //!< buffer number for each area, annimation or frequently update manipulate may use double buffer. 1
  int keep_n;// keep drawing on overlay for n intervals, default -1 for always.
  unsigned int update_intervals;
  amba_rect_t rect; //!< area size and offset in stream
  amba_draw_clut_t bg_color; //!< color in full area as background
} amba_overlay_area_attr_t;

typedef struct {
  unsigned char attr_change_flag;
  unsigned char data_change_flag;
  unsigned char reserved[2];
  //int num; //!< It is the total number of data contained on this area

  amba_overlay_area_attr_t attr; //!< It defines area attribute, please refer to @ref AMOverlayAreaAttr
  amba_draw_params_t data; //!< It contains all data param which are added on this area, please refer to @ref AMDrawingParams
} amba_overlay_area_param_t;


typedef struct {
  unsigned char area_num;

  //unsigned char always_insert; //!< To specify whether to always insert
  //unsigned char sync_with_pts; //!< To specify use frame sync method, it is usefull to do overlay for a specify frame
  //unsigned char stream_rotate;
  unsigned char refresh;
  unsigned char draw_format;
  unsigned char reserved;

  unsigned char attr_change_flag[MAX_OVERLAY_AREA_NUM];
  unsigned char data_change_flag[MAX_OVERLAY_AREA_NUM];

  amba_overlay_area_attr_t attr[MAX_OVERLAY_AREA_NUM];
} osd_header_param_t;

/* Per-area self-describing block: one area per GstMemory, [header][CLUT 1024][pixels].
 * No global header. area_count = gst_buffer_n_memory(buffer). Merge = append memories.
 * Layout: [0, CLUT_OFFSET)=header, [CLUT_OFFSET, PIXEL_OFFSET)=CLUT, [PIXEL_OFFSET, block_size)=pixels. */
#define OSD_AREA_BLOCK_MAGIC 0x414D  /* "AM" - Ambarella draw data, for format validation */
#define OSD_AREA_BLOCK_MAX_SIZE      (16 * 1024 * 1024)  /* 16MB, sanity limit */
/* area_id: overlay hardware slot 0..MAX_OVERLAY_AREA_NUM-1; USE_INDEX = use GstMemory order (legacy). */
#define OSD_AREA_BLOCK_AREA_ID_USE_INDEX 0xFFU
typedef struct {
  unsigned short magic;  /* OSD_AREA_BLOCK_MAGIC */
  unsigned char enable;
  unsigned char draw_format;  /* AMBA_DRAW_FORMAT_8BIT_CLUT etc */
  unsigned char area_id;  /* OSD area slot; USE_INDEX = consumer maps by buffer index */
  unsigned char reserved[3];  /* align after draw_format+area_id */
  amba_rect_t rect;
  /* Background color: use CLUT[AMBA_DRAW_CLUT_ENTRY_BACKGROUND] only (not duplicated in header). */
  unsigned int block_size;   /* total bytes = header + CLUT + pixels, for bounds check */
} osd_area_block_header_t;

/* Overlay hardware slot: explicit area_id in block, or buffer memory index when USE_INDEX/invalid. */
static inline unsigned int
osd_area_block_resolve_slot(const osd_area_block_header_t *h, unsigned int buffer_memory_index)
{
  if (!h || h->magic != OSD_AREA_BLOCK_MAGIC)
    return buffer_memory_index;
  if (h->area_id == OSD_AREA_BLOCK_AREA_ID_USE_INDEX || h->area_id >= MAX_OVERLAY_AREA_NUM)
    return buffer_memory_index;
  return (unsigned int)h->area_id;
}

#define OSD_AREA_BLOCK_HEADER_SIZE   (sizeof(osd_area_block_header_t))
#define OSD_AREA_BLOCK_CLUT_OFFSET   (OSD_AREA_BLOCK_HEADER_SIZE)
#define OSD_AREA_BLOCK_PIXEL_OFFSET  (OSD_AREA_BLOCK_CLUT_OFFSET + OVERLAY_CLUT_SIZE)

int get_clut_color_index(const amba_draw_clut_t *clut,
    amba_draw_clut_t *cluts,
    unsigned char *index);



#endif

