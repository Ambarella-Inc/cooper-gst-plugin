/*
 * drawdatagen_pose.h
 *
 * History:
 *    5/25/2026 - [pxduan] created file
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
 * RTMPose overlay helpers for amba_draw_data_gen.
 */

#ifndef __DRAWDATAGEN_POSE_H__
#define __DRAWDATAGEN_POSE_H__

#include <stdint.h>
#include "overlay_common.h"
#include "amba_ml_decoded_result.h"

/** CLUT index for pose keypoints and skeleton lines (8bit CLUT). */
#define DRAWDATAGEN_POSE_CLUT_INDEX  15

#define DRAWDATAGEN_POSE_CLUT_Y   29
#define DRAWDATAGEN_POSE_CLUT_U   255
#define DRAWDATAGEN_POSE_CLUT_V   107
#define DRAWDATAGEN_POSE_CLUT_A   255

#ifdef __cplusplus
extern "C" {
#endif

/**
 * drawdatagen_pose_draw - Draw RTMPose keypoints and skeleton.
 * Skips keypoints with score==0 (set by mlpostprocess conf_threshold).
 */
int drawdatagen_pose_draw(const amba_rect_t *roi,
    const amba_ml_pose_body_t *pose,
    unsigned char color_idx,
    int line_thickness,
    int kp_radius,
    amba_draw_clut_t *clut,
    unsigned char *data_buf,
    unsigned int area_pitch,
    int draw_format,
    int append_mode,
    uint32_t bg_color_hex,
    uint32_t line_color_hex);

#ifdef __cplusplus
}
#endif

#endif /* __DRAWDATAGEN_POSE_H__ */
