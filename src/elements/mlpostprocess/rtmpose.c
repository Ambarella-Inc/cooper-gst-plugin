/*
 * rtmpose.c
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
 * RTMPose SIMCC post-processing (simcc_x / simcc_y). Register as type=rtmpose.
 */

#include <float.h>
#include <math.h>
#include <string.h>

#include "debug_log.h"
#include "gstmlpostprocess.h"
#include "ml_postprocess_if.h"

#define RTMPOSE_KP_NUM AMBA_ML_POSE_KEYPOINT_NUM

static float rtmpose_tensor_at(const ml_postproc_tensor_desc_t *t, int kp, int bin,
    int kp_axis, int simcc_dim)
{
  int pitch = t->pitch > 0 ? t->pitch : (t->width * (int)sizeof(float));
  const uint8_t *base = (const uint8_t *)t->data;

  if (!t->data || kp < 0 || kp >= RTMPOSE_KP_NUM || bin < 0 || bin >= simcc_dim)
    return -FLT_MAX;

  /* keypoints along height (rows), simcc bins along width */
  if (kp_axis == 0) {
    const float *row = (const float *)(base + (size_t)kp * (size_t)pitch);
    return row[bin];
  }

  /* keypoints along width (cols), simcc bins along height */
  if (kp_axis == 1) {
    const float *row = (const float *)(base + (size_t)bin * (size_t)pitch);
    return row[kp];
  }

  /* keypoints along depth (channel), simcc bins along width (h=1) */
  if (kp_axis == 2) {
    int w = t->width > 0 ? t->width : 1;
    int h = t->height > 0 ? t->height : 1;
    const float *plane = (const float *)(base + (size_t)kp * (size_t)h * (size_t)pitch);
    (void)w;
    return plane[bin];
  }

  return -FLT_MAX;
}

static int rtmpose_resolve_layout(const ml_postproc_tensor_desc_t *t,
    int *kp_axis, int *simcc_dim)
{
  int w = t->width > 0 ? t->width : 1;
  int h = t->height > 0 ? t->height : 1;
  int d = t->depth > 0 ? t->depth : 1;

  if (h == RTMPOSE_KP_NUM) {
    *kp_axis = 0;
    *simcc_dim = w;
    return 0;
  }
  if (w == RTMPOSE_KP_NUM) {
    *kp_axis = 1;
    *simcc_dim = h;
    return 0;
  }
  if (d == RTMPOSE_KP_NUM && w > 1) {
    *kp_axis = 2;
    *simcc_dim = w;
    return 0;
  }

  DPRINT_ERROR("rtmpose: expected %d keypoints on H, W or C, got w=%d h=%d c=%d\n",
      RTMPOSE_KP_NUM, t->width, t->height, t->depth);
  return -1;
}

static int rtmpose_decode_simcc(const ml_postproc_tensor_desc_t *simcc_x,
    const ml_postproc_tensor_desc_t *simcc_y, float x1, float y1, float x2, float y2,
    int mw, int mh, float score_threshold, amba_ml_pose_body_t *out)
{
  int kp_axis_x = 0, dim_x = 0;
  int kp_axis_y = 0, dim_y = 0;
  int i, j;

  if (!simcc_x || !simcc_y || !out)
    return -1;

  if (rtmpose_resolve_layout(simcc_x, &kp_axis_x, &dim_x) < 0)
    return -1;
  if (rtmpose_resolve_layout(simcc_y, &kp_axis_y, &dim_y) < 0)
    return -1;

  if (kp_axis_x != kp_axis_y) {
    DPRINT_ERROR("rtmpose: simcc_x/simcc_y keypoint axis mismatch\n");
    return -1;
  }

  memset(out, 0, sizeof(*out));

  for (i = 0; i < RTMPOSE_KP_NUM; i++) {
    float max_x_value = -FLT_MAX;
    float max_y_value = -FLT_MAX;
    int max_x_index = 0;
    int max_y_index = 0;
    float nx, ny;
    float score;

    for (j = 0; j < dim_x; j++) {
      float v = rtmpose_tensor_at(simcc_x, i, j, kp_axis_x, dim_x);
      if (v > max_x_value) {
        max_x_value = v;
        max_x_index = j;
      }
    }
    for (j = 0; j < dim_y; j++) {
      float v = rtmpose_tensor_at(simcc_y, i, j, kp_axis_y, dim_y);
      if (v > max_y_value) {
        max_y_value = v;
        max_y_index = j;
      }
    }

    score = (max_x_value + max_y_value) / 2.0f;
    nx = ((float)max_x_index / (float)dim_x) * (x2 - x1) + x1;
    ny = ((float)max_y_index / (float)dim_y) * (y2 - y1) + y1;

    out->keypoints[i].score = (score >= score_threshold) ? score : 0.0f;
    out->keypoints[i].x = (int32_t)(nx * (float)mw);
    out->keypoints[i].y = (int32_t)(ny * (float)mh);
  }

  return 0;
}

int mlpp_rtmpose_process(void *ctx)
{
  ml_postproc_ctx_t *pp = (ml_postproc_ctx_t *)ctx;
  mlpp_priv_ctx_t *priv;
  const ml_postproc_tensor_desc_t *simcc_x = NULL;
  const ml_postproc_tensor_desc_t *simcc_y = NULL;
  int ti_x, ti_y;
  int mw = 1, mh = 1;
  float x1 = 0.0f, y1 = 0.0f, x2 = 1.0f, y2 = 1.0f;

  if (!pp || pp->num_tensors < 2) {
    DPRINT_ERROR("rtmpose: need simcc_x and simcc_y tensors\n");
    return -1;
  }

  priv = (mlpp_priv_ctx_t *)pp->user_data;
  if (priv && priv->map_width > 0 && priv->map_height > 0) {
    mw = priv->map_width;
    mh = priv->map_height;
  }

  ti_x = ml_find_tensor_by_name(pp, "simcc_x");
  ti_y = ml_find_tensor_by_name(pp, "simcc_y");
  if (ti_x < 0)
    ti_x = 0;
  if (ti_y < 0)
    ti_y = (pp->num_tensors > 1) ? 1 : 0;
  if (ti_x == ti_y) {
    DPRINT_ERROR("rtmpose: simcc_x and simcc_y resolve to same tensor\n");
    return -1;
  }

  simcc_x = &pp->tensors[ti_x];
  simcc_y = &pp->tensors[ti_y];

  memset(&pp->pose, 0, sizeof(pp->pose));

  if (rtmpose_decode_simcc(simcc_x, simcc_y, x1, y1, x2, y2, mw, mh, pp->conf_threshold,
        &pp->pose) < 0) {
    return -1;
  }

  return 0;
}

static const char *rtmpose_get_result_types(void)
{
  return GST_AMBA_ML_RESULT_TYPE_POSE;
}

static const ml_postproc_output_pad_spec_t rtmpose_output_pads[] = {
  { .name = "pose", .kind = ML_POSTPROC_PAD_BBOX },
};

static const ml_postproc_output_pad_spec_t *rtmpose_get_output_pads(int *count)
{
  *count = 1;
  return rtmpose_output_pads;
}

static const ml_postproc_output_layout_t rtmpose_output_layout = {
  .n_entries = 1,
  .entries = {
    { .type = AMBA_ML_RESULT_TYPE_POSE, .seg_idx = -1 },
  },
};

static const ml_postproc_output_layout_t *rtmpose_get_output_layout(void)
{
  return &rtmpose_output_layout;
}

static const ml_postproc_ops_t mlpp_rtmpose_ops = {
  .name = "rtmpose",
  .description = "RTMPose SIMCC decode (simcc_x/simcc_y argmax, 17 COCO keypoints)",
  .process = mlpp_rtmpose_process,
  .get_result_types = rtmpose_get_result_types,
  .get_output_pads = rtmpose_get_output_pads,
  .get_output_layout = rtmpose_get_output_layout,
  .deinit_user_ctx = NULL,
  .output_coords_normalized = FALSE,
};

void ml_register_rtmpose_builtin(void)
{
  ml_register_postproc("rtmpose", &mlpp_rtmpose_ops);
}
