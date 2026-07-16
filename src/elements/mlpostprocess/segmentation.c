/*
 * segmentation.c
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
 * Generic segmentation post-processing for mlpostprocess. Outputs GRAY8 mask.
 * type=depthanythingv2 matches nn_arm_task depth_anything draw_256_colors_depth:
 *   minMax, convertTo(8U, 234/(max-min), -min), then +1 (256-color display index).
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>

#include "internal.h"
#include "debug_log.h"
#include "yolo_common.h"
#include "ml_postprocess_if.h"

#ifndef MLPP_DEPTH_ANYTHING_LUT_SCALE
#define MLPP_DEPTH_ANYTHING_LUT_SCALE 234.0f
#endif

static int
mlpp_seg_alloc_mask(ml_postproc_ctx_t *pp, int w, int h)
{
  pp->seg_outputs[0].width = w;
  pp->seg_outputs[0].height = h;
  pp->seg_outputs[0].mask = (uint8_t *)malloc((size_t)w * h);
  if (!pp->seg_outputs[0].mask) {
    DPRINT_ERROR("segmentation: no memory\n");
    return -1;
  }
  return 0;
}

/**
 * DepthAnythingV2 (eazyai nn_arm_task/depth_anything): pitch-aware min/max and
 * 8-bit map for 256-color overlay (GRAY8 index; not the LUT itself).
 */
static int
mlpp_depth_anything_post_process(void *ctx)
{
  ml_postproc_ctx_t *pp = (ml_postproc_ctx_t *)ctx;
  const ml_postproc_tensor_desc_t *t;
  int w, h, y, x;
  int pitch_bytes;
  float minv, maxv, scale;
  uint8_t *mask;

  if (!pp || pp->num_tensors < 1)
    return -1;

  t = &pp->tensors[0];
  w = t->width;
  h = t->height;
  if (!t->data || w <= 0 || h <= 0)
    return -1;

  pitch_bytes = t->pitch > 0 ? t->pitch : (w * (int)sizeof(float));
  if (pitch_bytes < w * (int)sizeof(float)) {
    DPRINT_ERROR("depthanythingv2: invalid pitch %d for width %d\n", pitch_bytes, w);
    return -1;
  }

  minv = FLT_MAX;
  maxv = -FLT_MAX;
  for (y = 0; y < h; y++) {
    const float *row = (const float *)((const uint8_t *)t->data + (size_t)y * (size_t)pitch_bytes);
    for (x = 0; x < w; x++) {
      float v = row[x];
      if (v < minv)
        minv = v;
      if (v > maxv)
        maxv = v;
    }
  }

  if (mlpp_seg_alloc_mask(pp, w, h) < 0)
    return -1;

  mask = pp->seg_outputs[0].mask;
  scale = (maxv > minv) ? (MLPP_DEPTH_ANYTHING_LUT_SCALE / (maxv - minv)) : 0.0f;

  for (y = 0; y < h; y++) {
    const float *row = (const float *)((const uint8_t *)t->data + (size_t)y * (size_t)pitch_bytes);
    for (x = 0; x < w; x++) {
      float v;

      if (maxv > minv)
        v = (row[x] - minv) * scale + 1.0f;
      else
        v = 1.0f;

      if (v < 0.0f)
        v = 0.0f;
      if (v > 255.0f)
        v = 255.0f;
      mask[y * w + x] = (uint8_t)v;
    }
  }

  return 0;
}

static int mlpp_segmentation_post_process(void *ctx)
{
  ml_postproc_ctx_t *pp = (ml_postproc_ctx_t *)ctx;
  int w = 0, h = 0, ch = 0;
  float *data = NULL;

  if (!pp || pp->num_tensors < 1)
    return -1;

  /* Index-based: use first tensor */
  data = pp->tensors[0].data;
  w = pp->tensors[0].width;
  h = pp->tensors[0].height;
  ch = pp->tensors[0].depth;

  if (!data || w <= 0 || h <= 0)
    return -1;

  if (ch <= 0)
    ch = 1;

  if (mlpp_seg_alloc_mask(pp, w, h) < 0)
    return -1;

  if (ch == 1) {
    /* Single channel: scale to 0-255 */
    float minv = FLT_MAX, maxv = -FLT_MAX;
    int i, n = w * h;
    for (i = 0; i < n; i++) {
      if (data[i] < minv) {
        minv = data[i];
      }
      if (data[i] > maxv) {
        maxv = data[i];
      }
    }
    float range = (maxv > minv) ? (maxv - minv) : 1.0f;
    for (i = 0; i < n; i++) {
      float v = (data[i] - minv) / range * 255.0f;
      if (v < 0) {
        v = 0;
      }
      if (v > 255) {
        v = 255;
      }
      pp->seg_outputs[0].mask[i] = (uint8_t)v;
    }
  } else {
    /* Multi-channel: argmax on C, layout [y][c][x] -> data[y*ch*w + c*w + x] */
    int y, x, c;
    for (y = 0; y < h; y++) {
      for (x = 0; x < w; x++) {
        int best = 0;
        float bestv = data[y * ch * w + 0 * w + x];
        for (c = 1; c < ch; c++) {
          float v = data[y * ch * w + c * w + x];
          if (v > bestv) {
            bestv = v;
            best = c;
          }
        }
        pp->seg_outputs[0].mask[y * w + x] = (ch > 1) ?
            (uint8_t)(best * 255 / (ch - 1)) : (uint8_t)best;
      }
    }
  }

  return 0;
}

static const char *segmentation_result_types(void) { return "segmentation"; }

static const ml_postproc_output_pad_spec_t segmentation_output_pads[] = {
  { .name = "mask", .kind = ML_POSTPROC_PAD_VIDEO_GRAY8 },
};
static const ml_postproc_output_pad_spec_t *segmentation_get_output_pads(int *count) {
  *count = 1;
  return segmentation_output_pads;
}

static const ml_postproc_output_layout_t s_segmentation_layout = {
  .n_entries = 1,
  .entries = { { .type = AMBA_ML_RESULT_TYPE_SEGMENTATION, .seg_idx = 0 } }
};
static const ml_postproc_output_layout_t *segmentation_get_layout(void) { return &s_segmentation_layout; }

static const ml_postproc_ops_t segmentation_ops = {
  .name = "segmentation",
  .description = "Generic segmentation (single GRAY8 mask output)",
  .process = mlpp_segmentation_post_process,
  .get_result_types = segmentation_result_types,
  .get_output_pads = segmentation_get_output_pads,
  .get_output_layout = segmentation_get_layout,
};

static const ml_postproc_ops_t depthanythingv2_ops = {
  .name = "depthanythingv2",
  .description = "DepthAnythingV2: minMax + 234/(max-min) + 1 (nn_arm_task depth_anything)",
  .process = mlpp_depth_anything_post_process,
  .get_result_types = segmentation_result_types,
  .get_output_pads = segmentation_get_output_pads,
  .get_output_layout = segmentation_get_layout,
};

void ml_register_segmentation(void)
{
  ml_register_postproc("segmentation", &segmentation_ops);
  ml_register_postproc("deeplab", &segmentation_ops);
  ml_register_postproc("unet", &segmentation_ops);
  ml_register_postproc("depthanythingv2", &depthanythingv2_ops);
}
