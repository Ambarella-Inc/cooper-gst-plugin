/*
 * yolo_common.h
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
 * YOLO-specific post-processing (yolov3, yolov5 built-in).
 */

#ifndef __YOLO_COMMON_H__
#define __YOLO_COMMON_H__

#include "ml_postprocess_if.h"

enum {
  NMS_INIT = 0x0,
  NMS_CHOSEN = 0x1,
  NMS_DISCARD = 0x2,
};

#ifndef SIGMOID
#define SIGMOID(x) (1.0 / (1.0 + exp(-(x))))
#endif

#ifndef DCAL_MAX
#define DCAL_MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#ifndef DCAL_MIN
#define DCAL_MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

/* Sort indices 0..n-1 by tensor spatial size (H*W). Reduces pipeline misconfiguration.
 * asc=1: ascending (smallest first). Use when anchors[0]=P5/smallest FM (yolov3).
 * asc=0: descending (largest first). Use when anchors[0]=P3/largest FM (yolov5). */
static inline void mlpp_sort_indices_by_spatial(const ml_postproc_ctx_t *ctx, int n, int *idx, int asc)
{
  int i, j, tmp;
  for (i = 0; i < n; i++)
    idx[i] = i;
  for (i = 0; i < n - 1; i++) {
    for (j = i + 1; j < n; j++) {
      int sz_i = ctx->tensors[idx[i]].height * ctx->tensors[idx[i]].width;
      int sz_j = ctx->tensors[idx[j]].height * ctx->tensors[idx[j]].width;
      int swap = asc ? (sz_j < sz_i) : (sz_j > sz_i);
      if (swap) {
        tmp = idx[i]; idx[i] = idx[j]; idx[j] = tmp;
      }
    }
  }
}
#define mlpp_sort_indices_by_spatial_asc(ctx, n, idx) mlpp_sort_indices_by_spatial(ctx, n, idx, 1)
#define mlpp_sort_indices_by_spatial_desc(ctx, n, idx) mlpp_sort_indices_by_spatial(ctx, n, idx, 0)

int mlpp_yolov3_post_process(void *context);
int mlpp_yolov5_post_process(void *context);

#endif /* __YOLO_COMMON_H__ */
