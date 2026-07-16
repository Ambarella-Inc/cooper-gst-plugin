/*
 * drawdatagen_bbox.h
 *
 * History:
 *    3/3/2026 - [pxduan] created file
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
 *
 * Bounding box overlay for amba_draw_data_gen.
 */

#ifndef __DRAWDATAGEN_BBOX_H__
#define __DRAWDATAGEN_BBOX_H__

#include <stdint.h>
#include "overlay_common.h"

/** drawdatagen CLUT index layout (8bit CLUT). Must not conflict with AMBA_DRAW_CLUT_ENTRY_BACKGROUND.
 *  0,1: text (AMBA_TEXT_CLUT_ENTRY_BACKGOURND, AMBA_TEXT_CLUT_ENTRY_OUTLINE in amba_draw_data_string.h)
 *  2:   bbox (single+multi)
 *  3,4: YOLOP seg (lane_line, drive_area, multi only)
 *  5~13: text anti-aliasing gradient (init_text_info in amba_draw_data_string.c)
 *  14:  RetinaFace landmarks (mlpostprocess amba_ml_detection_t.flags HAS_LANDMARKS) */
#define DRAWDATAGEN_BBOX_CLUT_INDEX            2
#define DRAWDATAGEN_SEG_LANE_LINE_CLUT_INDEX   3
#define DRAWDATAGEN_SEG_DRIVE_AREA_CLUT_INDEX  4
#define DRAWDATAGEN_LANDMARK_CLUT_INDEX        14

/** Unified CLUT colors (BT.601 YUV). Single+multi use same values. */
#define DRAWDATAGEN_BBOX_CLUT_Y   81
#define DRAWDATAGEN_BBOX_CLUT_U   90
#define DRAWDATAGEN_BBOX_CLUT_V   240
#define DRAWDATAGEN_BBOX_CLUT_A   255
#define DRAWDATAGEN_SEG_LANE_LINE_CLUT_Y   225
#define DRAWDATAGEN_SEG_LANE_LINE_CLUT_U   0
#define DRAWDATAGEN_SEG_LANE_LINE_CLUT_V   148
#define DRAWDATAGEN_SEG_LANE_LINE_CLUT_A   255
/* drive_area: cyan (R=0,G=255,B=255) in BT.601 */
#define DRAWDATAGEN_SEG_DRIVE_AREA_CLUT_Y  170
#define DRAWDATAGEN_SEG_DRIVE_AREA_CLUT_U  166
#define DRAWDATAGEN_SEG_DRIVE_AREA_CLUT_V  16
#define DRAWDATAGEN_SEG_DRIVE_AREA_CLUT_A  255
/* Landmarks: green (BT.601), distinct from bbox / seg */
#define DRAWDATAGEN_LANDMARK_CLUT_Y  154
#define DRAWDATAGEN_LANDMARK_CLUT_U  48
#define DRAWDATAGEN_LANDMARK_CLUT_V  21
#define DRAWDATAGEN_LANDMARK_CLUT_A  255

#ifdef __cplusplus
extern "C" {
#endif

/**
 * drawdatagen_bbox_draw - Draw bounding boxes and labels to overlay area
 * @roi: area rect (width/height used)
 * @ml_result: detection result (nullable)
 * @det_num: number of detections
 * @color_idx: CLUT color index for box (default 15)
 * @thickness: line thickness (default 3)
 * @clut: CLUT buffer (filled by this function)
 * @data_buf: pixel buffer (drawn by this function)
 * @area_pitch: pitch of data_buf
 * @font_file: TTF path for labels (NULL = skip labels)
 * @font_size: font size for labels
 * @area_str: amba_overlay_area_param_t for text (when font_file set)
 * @bitmap: bitmap buffer for text render (when font_file set)
 * @draw_format: AMBA_DRAW_FORMAT_8BIT_CLUT etc
 * @append_mode: if non-zero, skip CLUT setup and fill_background (draw on existing buffer)
 * @bg_color_hex: 0=default transparent area background (same as osd bg_color). Non-zero: YUV formats v/u/y/a; RGB565 b/g/r/a (MSB..LSB).
 * @line_color_hex: 0=default box outline (BT.601 DRAWDATAGEN_BBOX_CLUT_* / fixed red 16-32bit). Non-zero: same hex layout as bg_color_hex.
 * Class/score text is drawn only when mlpostprocess fills a non-empty detection label (see amba_ml_detection_t.label).
 * When amba_ml_detection_t.flags & AMBA_ML_DETECTION_FLAG_HAS_LANDMARKS, draws 5 facial points
 * (landmark_x/y[] in coord_res) as small squares (RetinaFace order).
 * Returns: 0 on success
 */
int drawdatagen_bbox_draw(const amba_rect_t *roi,
    const void *ml_result,
    unsigned int det_num,
    unsigned char color_idx,
    int thickness,
    amba_draw_clut_t *clut,
    unsigned char *data_buf,
    unsigned int area_pitch,
    const char *font_file,
    int font_size,
    amba_overlay_area_param_t *area_str,
    bitmap_buffer_t *bitmap,
    int draw_format,
    int append_mode,
    uint32_t bg_color_hex,
    uint32_t line_color_hex);

#ifdef __cplusplus
}
#endif

#endif /* __DRAWDATAGEN_BBOX_H__ */
