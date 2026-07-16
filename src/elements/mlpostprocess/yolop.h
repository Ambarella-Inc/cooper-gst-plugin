/*
 * yolop.h
 *
 * History:
 *    4/23/2026 - [pxduan] created file
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
 * Shared YOLOP det tensor selection for yolop_builtin and eazyai yolop (same 3 fmaps + order).
 */

#ifndef YOLOP_MLPP_H
#define YOLOP_MLPP_H

#include "ml_postprocess_if.h"

/**
 * Picks 3 det tensor indices: exclude drive_area_seg & lane_line_seg (by name or
 * 5-tensor nameless fallback: skip 3,4), then sort by H*W ascending (match tensordec).
 * @return 0 on success, -1 if not exactly 3 det tensors
 */
int mlpp_yolop_collect_det_tensor_indices(const ml_postproc_ctx_t *ctx, int det_idx[3]);

/**
 * nnctrl sometimes reports seg output (1,2,H,W) as W:H:C = 2:640:640 (C:H:W in fields).
 * Same rule as eazyai yolo_seg_proto_effective_dims: treat as C=2, H x W = 640x640.
 */
void mlpp_yolop_seg_effective_dims(int w, int h, int d, int *eff_w, int *eff_h, int *eff_d);

#endif
