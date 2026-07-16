/*
 * yolop.c
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
 * Built-in YOLOP post-processing for mlpostprocess. Supports 3 tensors (detection)
 * or 5 tensors (detection + drive_area_seg + lane_line_seg).
 */

#include "common_err_code_c.h"

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>

#include "internal.h"
#include "debug_log.h"
#include "yolo_common.h"
#include "ml_postprocess_if.h"
#include "yolop.h"

#ifndef YOLOP_FEATURE_MAP_NUM
#define YOLOP_FEATURE_MAP_NUM 3
#endif
#ifndef YOLOP_ANCHOR_NUM
#define YOLOP_ANCHOR_NUM 18
#endif
#ifndef YOLOP_ANCHOR_GROUP_NUM
#define YOLOP_ANCHOR_GROUP_NUM 3
#endif
#ifndef YOLOP_MIN_WH
#define YOLOP_MIN_WH 2
#endif
#ifndef YOLOP_MAX_WH
#define YOLOP_MAX_WH 4096
#endif
#ifndef DMAX_DET_NUM
#define DMAX_DET_NUM 200
#endif

#define YOLOP_DRIVE_TENSOR_NAME  "drive_area_seg"
#define YOLOP_LANE_TENSOR_NAME   "lane_line_seg"

/* tensordec-boundingbox: yolop_anchors flat array [large,medium,small]
 * m=0 (13x13): (19,50),(38,81),(68,157)
 * m=1 (26x26): (7,18),(6,39),(12,31)
 * m=2 (52x52): (3,9),(5,11),(4,20) */
static const float yolop_anchors[YOLOP_ANCHOR_NUM] = {
  19, 50, 38, 81, 68, 157,
  7, 18, 6, 39, 12, 31,
  3, 9, 5, 11, 4, 20
};

static int __nms(float *x1y1x2y2score, void *aux, size_t aux_element_size, int num,
    float threshold, int use_iou_min, int top_k,
    float *out_x1y1x2y2score, void *out_aux, int *out_num)
{
  int rval = 0;
  float *area = NULL;
  int *sort = NULL;
  int *status = NULL;
  int i, k, temp, high_ind;
  float area_high, area_i, max_x, max_y, min_x, min_y;
  float iou_width, iou_height, iou_area, iou_ratio;
  int chosen_count = 0;

  if (!x1y1x2y2score || !out_x1y1x2y2score || !out_num) {
    DPRINT_ERROR("invalid params\n");
    return -1;
  }

  *out_num = 0;
  if (num <= 0) {
    return 0;
  }

  area = (float *)malloc(sizeof(float) * num);
  if (!area) {
    DPRINT_ERROR("no memory\n");
    return -1;
  }

  for (i = 0; i < num; i++) {
    area[i] = (x1y1x2y2score[i * 5 + 2] - x1y1x2y2score[i * 5 + 0]) *
             (x1y1x2y2score[i * 5 + 3] - x1y1x2y2score[i * 5 + 1]);
    area[i] = DCAL_MAX(0, area[i]);
  }

  sort = (int *)malloc(sizeof(int) * num);
  if (!sort) {
    free(area);
    return -1;
  }
  for (i = 0; i < num; i++) {
    sort[i] = i;
  }

  for (i = 0; i < num - 1; i++) {
    for (k = i + 1; k < num; k++) {
      if (x1y1x2y2score[sort[i] * 5 + 4] < x1y1x2y2score[sort[k] * 5 + 4]) {
        temp = sort[i];
        sort[i] = sort[k];
        sort[k] = temp;
      }
    }
  }

  status = (int *)malloc(sizeof(int) * num);
  if (!status) {
    free(area);
    free(sort);
    return -1;
  }
  for (i = 0; i < num; i++) {
    status[i] = NMS_INIT;
  }

  while (1) {
    high_ind = -1;
    for (i = 0; i < num; i++) {
      if (status[sort[i]] == NMS_INIT) {
        high_ind = sort[i];
        break;
      }
    }
    if (high_ind == -1) {
      break;
    }

    status[high_ind] = NMS_CHOSEN;
    chosen_count++;
    if (top_k > 0 && chosen_count >= top_k) {
      break;
    }

    for (i = 0; i < num; i++) {
      if (status[i] == NMS_INIT) {
        area_high = area[high_ind];
        area_i = area[i];
        max_x = DCAL_MAX(x1y1x2y2score[i * 5 + 0], x1y1x2y2score[high_ind * 5 + 0]);
        max_y = DCAL_MAX(x1y1x2y2score[i * 5 + 1], x1y1x2y2score[high_ind * 5 + 1]);
        min_x = DCAL_MIN(x1y1x2y2score[i * 5 + 2], x1y1x2y2score[high_ind * 5 + 2]);
        min_y = DCAL_MIN(x1y1x2y2score[i * 5 + 3], x1y1x2y2score[high_ind * 5 + 3]);
        iou_width = ((min_x - max_x) > 0) ? (min_x - max_x) : 0;
        iou_height = ((min_y - max_y) > 0) ? (min_y - max_y) : 0;
        iou_area = iou_width * iou_height;
        iou_ratio = use_iou_min ? (iou_area / DCAL_MIN(area_high, area_i))
                               : (iou_area / (area_high + area_i - iou_area));
        if (iou_ratio > threshold) {
          status[i] = NMS_DISCARD;
        }
      }
    }
  }

  for (i = 0; i < num; i++) {
    if (status[sort[i]] == NMS_CHOSEN) {
      memcpy(&out_x1y1x2y2score[(*out_num) * 5], &x1y1x2y2score[sort[i] * 5], sizeof(float) * 5);
      if (aux && out_aux) {
        memcpy(&((uint8_t *)out_aux)[(*out_num) * aux_element_size],
            &((uint8_t *)aux)[sort[i] * aux_element_size], aux_element_size);
      }
      (*out_num)++;
    }
  }

  free(area);
  free(sort);
  free(status);
  return rval;
}

/* tensordec-boundingbox seg_get_mask: from out_tensor(1x2xHxW) NCHW planar, extract seg mask
 * tensordec: ch0=bg, ch1=fg -> is_fg = (ch1 > ch0).
 * Model [fg,bg]: ch0=fg, ch1=bg -> is_fg = (ch0 > ch1). Use invert to swap channel role.
 * pitch_floats: float elements per row (from tensor.pitch/4); may be > w (row padding). */
static void seg_get_mask(float *input_data, int w, int h, int pitch_floats, uint8_t *mask,
    int invert, const char *name)
{
  int x, y;
  gsize plane_floats;
  float *base_x_0;
  float *base_x_1;

  (void)name;
  if (pitch_floats < w) {
    pitch_floats = w;
  }
  plane_floats = (gsize)h * (gsize)pitch_floats;
  for (x = 0; x < h; x++) {
    base_x_0 = input_data + (gsize)pitch_floats * (gsize)x;
    base_x_1 = input_data + plane_floats + (gsize)pitch_floats * (gsize)x;
    for (y = 0; y < w; y++) {
      /* invert: model [fg,bg] use ch0>ch1; !invert: tensordec [bg,fg] use ch1>ch0 */
      int is_fg = invert ? (base_x_0[y] > base_x_1[y]) : (base_x_1[y] > base_x_0[y]);
      *mask++ = is_fg ? 255 : 0;
    }
  }
}

int mlpp_yolop_collect_det_tensor_indices(const ml_postproc_ctx_t *ctx, int det_idx[3])
{
  int num_tensors, i, d;
  if (!ctx || !det_idx) {
    return -1;
  }
  num_tensors = ctx->num_tensors;
  d = 0;
  for (i = 0; i < num_tensors; i++) {
    if (ctx->tensors[i].name[0]) {
      if (strcmp(ctx->tensors[i].name, YOLOP_DRIVE_TENSOR_NAME) == 0 ||
          strcmp(ctx->tensors[i].name, YOLOP_LANE_TENSOR_NAME) == 0) {
        continue;
      }
    } else if (num_tensors == 5 && (i == 3 || i == 4)) {
      continue;
    }
    if (d < YOLOP_FEATURE_MAP_NUM) {
      det_idx[d++] = i;
    }
  }
  if (d != YOLOP_FEATURE_MAP_NUM) {
    return -1;
  }
  mlpp_sort_indices_by_spatial_asc(ctx, YOLOP_FEATURE_MAP_NUM, det_idx);
  return 0;
}

void
mlpp_yolop_seg_effective_dims(int w, int h, int d, int *eff_w, int *eff_h, int *eff_d)
{
  *eff_w = w;
  *eff_h = h;
  *eff_d = d;
  if (h == d && w > 0 && w <= 128 && w < h) {
    *eff_d = w;
    *eff_h = h;
    *eff_w = d;
  }
}

int mlpp_yolop_builtin_post_process(void *context)
{
  int rval = 0;
  ml_postproc_ctx_t *ctx = (ml_postproc_ctx_t *)context;
  bounding_boxes_t *result = ctx->result;
  int num_tensors = ctx->num_tensors;
  int det_idx[YOLOP_FEATURE_MAP_NUM];
  int class_num;
  int height[YOLOP_FEATURE_MAP_NUM];
  int width[YOLOP_FEATURE_MAP_NUM];
  int max_det_num_in_class = 0;
  float *feature_map_data;
  float *feature_map_data_in_anchor;
  float *x_data, *y_data, *w_data, *h_data, *box_conf_data, *cls_conf_data;
  int shape_h;
  int pitch_elems;
  int stride_w, stride_h;
  float xywhscore[5];
  float cls_conf;
  float *x1y1x2y2score_one_class = NULL;
  int valid_count_one_class = 0;
  float *nms_x1y1x2y2score_one_class = NULL;
  int nms_valid_count_one_class = 0;
  int i, m, a, c, h, w;
  int best_cls;
  float best_cls_conf;
  const float *yolov5_anchors = NULL;

  /* 3 tensors: det only. 5 tensors: det + drive_area + lane_line */
  if (num_tensors != YOLOP_FEATURE_MAP_NUM && num_tensors != 5) {
    DPRINT_ERROR("yolop_builtin: need 3 (det) or 5 (det+seg) tensors, got %d\n", num_tensors);
    return -1;
  }

  if (mlpp_yolop_collect_det_tensor_indices(ctx, det_idx) < 0) {
    DPRINT_ERROR("yolop_builtin: expected 3 det tensors (after exclude seg + sort), got <3\n");
    return -1;
  }

  class_num = (ctx->tensors[det_idx[0]].depth / YOLOP_FEATURE_MAP_NUM) - 5;
  if (class_num <= 0) {
    class_num = 1;
  }
  if (class_num != 1) {
    DPRINT_ERROR("yolop_builtin: detection class should be 1 (CAR), got %d\n", class_num);
    return -1;
  }

  do {
    for (m = 0; m < YOLOP_FEATURE_MAP_NUM; m++) {
      height[m] = ctx->tensors[det_idx[m]].height;
      width[m] = ctx->tensors[det_idx[m]].width;
      max_det_num_in_class += height[m] * width[m] * YOLOP_ANCHOR_GROUP_NUM;
    }

    x1y1x2y2score_one_class = (float *)malloc(max_det_num_in_class * 5 * sizeof(float));
    if (!x1y1x2y2score_one_class) {
      rval = -1;
      break;
    }

    for (m = 0; m < YOLOP_FEATURE_MAP_NUM; m++) {
      yolov5_anchors = &yolop_anchors[0] + m * (YOLOP_ANCHOR_GROUP_NUM << 1);
      feature_map_data = ctx->tensors[det_idx[m]].data;
      shape_h = ctx->tensors[det_idx[m]].height;
      pitch_elems = ctx->tensors[det_idx[m]].pitch / (int)sizeof(float);
      stride_w = ctx->nn_input_width / width[m];
      stride_h = ctx->nn_input_height / height[m];

      for (a = 0; a < YOLOP_ANCHOR_GROUP_NUM; a++) {
        feature_map_data_in_anchor = feature_map_data + shape_h * pitch_elems * (class_num + 5) * a;
        x_data = feature_map_data_in_anchor;
        y_data = x_data + shape_h * pitch_elems;
        w_data = y_data + shape_h * pitch_elems;
        h_data = w_data + shape_h * pitch_elems;
        box_conf_data = h_data + shape_h * pitch_elems;

        for (h = 0; h < height[m]; h++) {
          for (w = 0; w < width[m]; w++) {
            xywhscore[4] = SIGMOID(box_conf_data[h * pitch_elems + w]);
            if (xywhscore[4] <= ctx->conf_threshold) {
              continue;
            }

            xywhscore[2] = SIGMOID(w_data[h * pitch_elems + w]);
            xywhscore[2] = pow(xywhscore[2] * 2.0, 2.0) * yolov5_anchors[a * 2];
            if (xywhscore[2] <= YOLOP_MIN_WH || xywhscore[2] >= YOLOP_MAX_WH) {
              continue;
            }

            xywhscore[3] = SIGMOID(h_data[h * pitch_elems + w]);
            xywhscore[3] = pow(xywhscore[3] * 2.0, 2.0) * yolov5_anchors[a * 2 + 1];
            if (xywhscore[3] <= YOLOP_MIN_WH || xywhscore[3] >= YOLOP_MAX_WH) {
              continue;
            }

            best_cls = 0;
            best_cls_conf = -FLT_MAX;
            for (c = 0; c < class_num; c++) {
              cls_conf_data = box_conf_data + shape_h * pitch_elems + c * shape_h * pitch_elems;
              cls_conf = cls_conf_data[h * pitch_elems + w];
              if (best_cls_conf < cls_conf) {
                best_cls_conf = cls_conf;
                best_cls = c;
              }
            }
            c = best_cls;
            cls_conf = SIGMOID(best_cls_conf);
            xywhscore[4] = xywhscore[4] * cls_conf;

            xywhscore[0] = SIGMOID(x_data[h * pitch_elems + w]);
            xywhscore[0] = (xywhscore[0] * 2.0 - 0.5 + w) * stride_w;
            xywhscore[1] = SIGMOID(y_data[h * pitch_elems + w]);
            xywhscore[1] = (xywhscore[1] * 2.0 - 0.5 + h) * stride_h;

            x1y1x2y2score_one_class[valid_count_one_class * 5 + 0] = xywhscore[0] - xywhscore[2] / 2.0;
            x1y1x2y2score_one_class[valid_count_one_class * 5 + 1] = xywhscore[1] - xywhscore[3] / 2.0;
            x1y1x2y2score_one_class[valid_count_one_class * 5 + 2] = xywhscore[0] + xywhscore[2] / 2.0;
            x1y1x2y2score_one_class[valid_count_one_class * 5 + 3] = xywhscore[1] + xywhscore[3] / 2.0;
            x1y1x2y2score_one_class[valid_count_one_class * 5 + 4] = xywhscore[4];
            valid_count_one_class++;
          }
        }
      }
    }

    nms_x1y1x2y2score_one_class = (float *)malloc(valid_count_one_class * 5 * sizeof(float));
    if (!nms_x1y1x2y2score_one_class) {
      rval = -1;
      break;
    }

    __nms(x1y1x2y2score_one_class, NULL, 0, valid_count_one_class,
          ctx->nms_threshold, 0, ctx->top_k,
          nms_x1y1x2y2score_one_class, NULL, &nms_valid_count_one_class);

    result->det_num = 0;
    for (i = 0; i < nms_valid_count_one_class; i++) {
      if (nms_x1y1x2y2score_one_class[i * 5 + 4] > ctx->conf_threshold) {
        float x0 = nms_x1y1x2y2score_one_class[i * 5 + 0];
        float y0 = nms_x1y1x2y2score_one_class[i * 5 + 1];
        float x1 = nms_x1y1x2y2score_one_class[i * 5 + 2];
        float y1 = nms_x1y1x2y2score_one_class[i * 5 + 3];
        x0 = DCAL_MAX(0.0f, DCAL_MIN((float)ctx->nn_input_width, x0));
        y0 = DCAL_MAX(0.0f, DCAL_MIN((float)ctx->nn_input_height, y0));
        x1 = DCAL_MAX(x0, DCAL_MIN((float)ctx->nn_input_width, x1));
        y1 = DCAL_MAX(y0, DCAL_MIN((float)ctx->nn_input_height, y1));

        result->detections[result->det_num].id = 0;
        result->detections[result->det_num].score = nms_x1y1x2y2score_one_class[i * 5 + 4];
        result->detections[result->det_num].x_start = x0;
        result->detections[result->det_num].y_start = y0;
        result->detections[result->det_num].x_end = x1;
        result->detections[result->det_num].y_end = y1;

        if (ctx->valid_label_count > 0 && ctx->labels[0][0]) {
          snprintf(result->detections[result->det_num].label, DMAX_LABEL_LEN, "%s", ctx->labels[0]);
        } else {
          result->detections[result->det_num].label[0] = '\0';
        }
        result->detections[result->det_num].label[DMAX_LABEL_LEN - 1] = '\0';

        result->det_num++;
        if (result->det_num >= DMAX_DET_NUM) {
          break;
        }
      }
    }
  } while (0);

  free(x1y1x2y2score_one_class);
  free(nms_x1y1x2y2score_one_class);

  if (rval < 0) {
    return rval;
  }

  /* Segmentation: tensordec seg_get_mask - 5 tensors */
  if (num_tensors >= 5) {
    int drive_idx = ml_find_tensor_by_name(ctx, YOLOP_DRIVE_TENSOR_NAME);
    int lane_idx = ml_find_tensor_by_name(ctx, YOLOP_LANE_TENSOR_NAME);
    if (drive_idx < 0 || lane_idx < 0) {
      drive_idx = 3;
      lane_idx = 4;
    }

    int seg_w, seg_h;
    {
      int ed_c;
      mlpp_yolop_seg_effective_dims(ctx->tensors[drive_idx].width, ctx->tensors[drive_idx].height,
          ctx->tensors[drive_idx].depth, &seg_w, &seg_h, &ed_c);
    }
    size_t seg_size = (size_t)seg_w * (size_t)seg_h;

    uint8_t *drive_buf = (uint8_t *)malloc(seg_size);
    uint8_t *lane_buf = (uint8_t *)malloc(seg_size);
    if (drive_buf && lane_buf) {
      int pitch_d = (ctx->tensors[drive_idx].pitch > 0) ?
          (int)(ctx->tensors[drive_idx].pitch / (int)sizeof(float)) : seg_w;
      int pitch_l = (ctx->tensors[lane_idx].pitch > 0) ?
          (int)(ctx->tensors[lane_idx].pitch / (int)sizeof(float)) : seg_w;

      seg_get_mask(ctx->tensors[drive_idx].data, seg_w, seg_h, pitch_d, drive_buf, 0,
          "drive_area");
      ctx->seg_outputs[0].mask = drive_buf;
      ctx->seg_outputs[0].width = seg_w;
      ctx->seg_outputs[0].height = seg_h;

      seg_get_mask(ctx->tensors[lane_idx].data, seg_w, seg_h, pitch_l, lane_buf, 0,
          "lane_line");
      ctx->seg_outputs[1].mask = lane_buf;
      ctx->seg_outputs[1].width = seg_w;
      ctx->seg_outputs[1].height = seg_h;
    } else {
      free(drive_buf);
      free(lane_buf);
    }
  }

  return 0;
}

static const ml_postproc_output_pad_spec_t s_yolop_builtin_pads[] = {
  { .name = "bbox", .kind = ML_POSTPROC_PAD_BBOX },
  { .name = "drive_area", .kind = ML_POSTPROC_PAD_VIDEO_GRAY8 },
  { .name = "lane_line", .kind = ML_POSTPROC_PAD_VIDEO_GRAY8 },
};
static const ml_postproc_output_pad_spec_t *yolop_builtin_get_pads(int *count) {
  *count = 3;
  return s_yolop_builtin_pads;
}
static const char *yolop_builtin_result_types(void) {
  return "detection_bbox:drive_area_seg:lane_line_seg";
}
static const ml_postproc_output_layout_t s_yolop_builtin_layout = {
  .n_entries = 3,
  .entries = {
    { .type = AMBA_ML_RESULT_TYPE_DETECTION_BBOX, .seg_idx = -1 },
    { .type = AMBA_ML_RESULT_TYPE_SEGMENTATION, .seg_idx = 0 },
    { .type = AMBA_ML_RESULT_TYPE_SEGMENTATION, .seg_idx = 1 },
  }
};
static const ml_postproc_output_layout_t *yolop_builtin_get_layout(void) {
  return &s_yolop_builtin_layout;
}

static const ml_postproc_ops_t mlpp_yolop_builtin_ops = {
  .name = "yolop_builtin",
  .description = "YOLOP built-in (ref tensordec-boundingbox amba_yolop)",
  .process = mlpp_yolop_builtin_post_process,
  .get_result_types = yolop_builtin_result_types,
  .get_output_pads = yolop_builtin_get_pads,
  .get_output_layout = yolop_builtin_get_layout,
  .deinit_user_ctx = NULL,
  .output_coords_normalized = FALSE,
};

void ml_register_yolop_builtin(void)
{
  ml_register_postproc("yolop_builtin", &mlpp_yolop_builtin_ops);
}
