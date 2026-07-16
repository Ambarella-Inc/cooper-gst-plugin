/*
 * yolov5.c
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
 * YOLOv5 post-processing for mlpostprocess.
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

#ifndef YOLOV5_FEATURE_MAP_NUM
#define YOLOV5_FEATURE_MAP_NUM 3
#endif
#ifndef YOLOV5_ANCHOR_NUM
#define YOLOV5_ANCHOR_NUM 3
#endif
#ifndef YOLOV5_MIN_WH
#define YOLOV5_MIN_WH 2
#endif
#ifndef YOLOV5_MAX_WH
#define YOLOV5_MAX_WH 4096
#endif
#ifndef DMAX_DET_NUM
#define DMAX_DET_NUM 200
#endif
static float const yolov5_anchors[YOLOV5_FEATURE_MAP_NUM][YOLOV5_ANCHOR_NUM][2] = {
    {{116, 90}, {156, 198}, {373, 326}},
    {{30, 61}, {62, 45}, {59, 119}},
    {{10, 13}, {16, 30}, {33, 23}}
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
  if (num <= 0)
    return 0;

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
    if (high_ind == -1)
      break;

    status[high_ind] = NMS_CHOSEN;
    chosen_count++;
    if (top_k > 0 && chosen_count >= top_k)
      break;

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
        if (iou_ratio > threshold)
          status[i] = NMS_DISCARD;
      }
    }
  }

  for (i = 0; i < num; i++) {
    if (status[sort[i]] == NMS_CHOSEN) {
      memcpy(&out_x1y1x2y2score[(*out_num) * 5], &x1y1x2y2score[sort[i] * 5], sizeof(float) * 5);
      if (aux && out_aux)
        memcpy(&((uint8_t *)out_aux)[(*out_num) * aux_element_size],
               &((uint8_t *)aux)[sort[i] * aux_element_size], aux_element_size);
      (*out_num)++;
    }
  }

  free(area);
  free(sort);
  free(status);
  return rval;
}

int mlpp_yolov5_post_process(void *context)
{
  int rval = 0;
  ml_postproc_ctx_t *ctx = (ml_postproc_ctx_t *)context;
  bounding_boxes_t *result = ctx->result;
  int feature_map_num = ctx->num_tensors;
  const float *yolo_anchors = &yolov5_anchors[0][0][0];
  int det_idx[YOLOV5_FEATURE_MAP_NUM];
  int class_num;
  int height[YOLOV5_FEATURE_MAP_NUM];
  int width[YOLOV5_FEATURE_MAP_NUM];
  int max_det_num_in_class = 0;
  float *feature_map_data;
  float *feature_map_data_in_anchor;
  float *x_data, *y_data, *w_data, *h_data, *box_conf_data, *cls_conf_data;
  int shape_h;
  int pitch_elems;
  int stride_w, stride_h;
  float xywhscore[5];
  float cls_conf;
  float **x1y1x2y2score_in_class = NULL;
  int *valid_count_in_class = NULL;
  float **nms_x1y1x2y2score_in_class = NULL;
  int *nms_valid_count_in_class = NULL;
  int i, m, a, c, h, w;
  int best_cls;
  float best_cls_conf;

  if (feature_map_num != YOLOV5_FEATURE_MAP_NUM) {
    DPRINT_ERROR("yolov5 feature map num should be 3\n");
    return -1;
  }

  /* Index-based; sort descending (anchors[0]=52x52 P3, [1]=26x26 P4, [2]=13x13 P5) */
  mlpp_sort_indices_by_spatial_desc(ctx, YOLOV5_FEATURE_MAP_NUM, det_idx);
  class_num = ctx->tensors[det_idx[0]].depth / YOLOV5_ANCHOR_NUM - 5;

  do {
    for (m = 0; m < feature_map_num; m++) {
      height[m] = ctx->tensors[det_idx[m]].height;
      width[m] = ctx->tensors[det_idx[m]].width;
      max_det_num_in_class += height[m] * width[m] * YOLOV5_ANCHOR_NUM;
    }

    x1y1x2y2score_in_class = (float **)malloc(class_num * sizeof(float *));
    if (!x1y1x2y2score_in_class) {
      rval = -1;
      break;
    }
    for (c = 0; c < class_num; c++) {
      x1y1x2y2score_in_class[c] = (float *)malloc(max_det_num_in_class * 5 * sizeof(float));
      if (!x1y1x2y2score_in_class[c]) {
        for (i = 0; i < c; i++) {
          free(x1y1x2y2score_in_class[i]);
        }
        free(x1y1x2y2score_in_class);
        rval = -1;
        break;
      }
    }
    if (rval < 0)
      break;

    valid_count_in_class = (int *)malloc(class_num * sizeof(int));
    if (!valid_count_in_class) {
      for (c = 0; c < class_num; c++) {
        free(x1y1x2y2score_in_class[c]);
      }
      free(x1y1x2y2score_in_class);
      rval = -1;
      break;
    }
    memset(valid_count_in_class, 0, class_num * sizeof(int));

    for (m = 0; m < feature_map_num; m++) {
      stride_w = ctx->nn_input_width / width[m];
      stride_h = ctx->nn_input_height / height[m];
      feature_map_data = ctx->tensors[det_idx[m]].data;
      shape_h = ctx->tensors[det_idx[m]].height;
      pitch_elems = ctx->tensors[det_idx[m]].pitch / sizeof(float);

      for (a = 0; a < YOLOV5_ANCHOR_NUM; a++, yolo_anchors += 2) {
        feature_map_data_in_anchor = feature_map_data + shape_h * pitch_elems * (class_num + 5) * a;
        x_data = feature_map_data_in_anchor;
        y_data = x_data + shape_h * pitch_elems;
        w_data = y_data + shape_h * pitch_elems;
        h_data = w_data + shape_h * pitch_elems;
        box_conf_data = h_data + shape_h * pitch_elems;

        for (h = 0; h < height[m]; h++) {
          for (w = 0; w < width[m]; w++) {
            xywhscore[4] = SIGMOID(box_conf_data[h * pitch_elems + w]);
            if (xywhscore[4] <= ctx->conf_threshold)
              continue;

            xywhscore[2] = SIGMOID(w_data[h * pitch_elems + w]);
            xywhscore[2] = pow(xywhscore[2] * 2.0, 2.0) * yolo_anchors[0];
            if (xywhscore[2] <= YOLOV5_MIN_WH || xywhscore[2] >= YOLOV5_MAX_WH)
              continue;

            xywhscore[3] = SIGMOID(h_data[h * pitch_elems + w]);
            xywhscore[3] = pow(xywhscore[3] * 2.0, 2.0) * yolo_anchors[1];
            if (xywhscore[3] <= YOLOV5_MIN_WH || xywhscore[3] >= YOLOV5_MAX_WH)
              continue;

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

            x1y1x2y2score_in_class[c][valid_count_in_class[c] * 5 + 0] = xywhscore[0] - xywhscore[2] / 2.0;
            x1y1x2y2score_in_class[c][valid_count_in_class[c] * 5 + 1] = xywhscore[1] - xywhscore[3] / 2.0;
            x1y1x2y2score_in_class[c][valid_count_in_class[c] * 5 + 2] = xywhscore[0] + xywhscore[2] / 2.0;
            x1y1x2y2score_in_class[c][valid_count_in_class[c] * 5 + 3] = xywhscore[1] + xywhscore[3] / 2.0;
            x1y1x2y2score_in_class[c][valid_count_in_class[c] * 5 + 4] = xywhscore[4];
            valid_count_in_class[c]++;
          }
        }
      }
    }

    nms_x1y1x2y2score_in_class = (float **)malloc(class_num * sizeof(float *));
    if (!nms_x1y1x2y2score_in_class) {
      rval = -1;
      break;
    }
    for (c = 0; c < class_num; c++) {
      nms_x1y1x2y2score_in_class[c] = (float *)malloc(valid_count_in_class[c] * 5 * sizeof(float));
      if (!nms_x1y1x2y2score_in_class[c]) {
        for (i = 0; i < c; i++) {
          free(nms_x1y1x2y2score_in_class[i]);
        }
        free(nms_x1y1x2y2score_in_class);
        rval = -1;
        break;
      }
    }
    if (rval < 0)
      break;

    nms_valid_count_in_class = (int *)malloc(class_num * sizeof(int));
    if (!nms_valid_count_in_class) {
      for (c = 0; c < class_num; c++) {
        free(nms_x1y1x2y2score_in_class[c]);
      }
      free(nms_x1y1x2y2score_in_class);
      rval = -1;
      break;
    }

    for (c = 0; c < class_num; c++) {
      __nms(x1y1x2y2score_in_class[c], NULL, 0, valid_count_in_class[c],
            ctx->nms_threshold, 0, ctx->top_k,
            nms_x1y1x2y2score_in_class[c], NULL, &nms_valid_count_in_class[c]);
    }

    result->det_num = 0;
    for (c = 0; c < class_num; c++) {
      for (i = 0; i < nms_valid_count_in_class[c]; i++) {
        if (nms_x1y1x2y2score_in_class[c][i * 5 + 4] > ctx->conf_threshold) {
          result->detections[result->det_num].id = c;
          result->detections[result->det_num].score = nms_x1y1x2y2score_in_class[c][i * 5 + 4];

          {
            float x0 = nms_x1y1x2y2score_in_class[c][i * 5 + 0];
            float y0 = nms_x1y1x2y2score_in_class[c][i * 5 + 1];
            float x1 = nms_x1y1x2y2score_in_class[c][i * 5 + 2];
            float y1 = nms_x1y1x2y2score_in_class[c][i * 5 + 3];
            x0 = DCAL_MAX(0.0f, DCAL_MIN((float)ctx->nn_input_width, x0));
            y0 = DCAL_MAX(0.0f, DCAL_MIN((float)ctx->nn_input_height, y0));
            x1 = DCAL_MAX(x0, DCAL_MIN((float)ctx->nn_input_width, x1));
            y1 = DCAL_MAX(y0, DCAL_MIN((float)ctx->nn_input_height, y1));
            result->detections[result->det_num].x_start = x0;
            result->detections[result->det_num].y_start = y0;
            result->detections[result->det_num].x_end = x1;
            result->detections[result->det_num].y_end = y1;
          }

          if (c < (int)ctx->valid_label_count)
            strncpy(result->detections[result->det_num].label, ctx->labels[c], DMAX_LABEL_LEN - 1);
          else
            result->detections[result->det_num].label[0] = '\0';
          result->detections[result->det_num].label[DMAX_LABEL_LEN - 1] = '\0';

          result->det_num++;
          if (result->det_num >= DMAX_DET_NUM)
            break;
        }
      }
      if (result->det_num >= DMAX_DET_NUM)
        break;
    }
  } while (0);

  if (x1y1x2y2score_in_class) {
    for (c = 0; c < class_num; c++) {
      free(x1y1x2y2score_in_class[c]);
    }
    free(x1y1x2y2score_in_class);
  }
  if (valid_count_in_class)
    free(valid_count_in_class);
  if (nms_x1y1x2y2score_in_class) {
    for (c = 0; c < class_num; c++) {
      free(nms_x1y1x2y2score_in_class[c]);
    }
    free(nms_x1y1x2y2score_in_class);
  }
  if (nms_valid_count_in_class)
    free(nms_valid_count_in_class);

  return rval;
}

/* Built-in YOLOv5 ops for cavalry model (1037,1017,997). Use type=yolov5_builtin when eazyai fails. */
static const ml_postproc_output_pad_spec_t s_yolov5_builtin_pads[] = {
  { .name = "bbox", .kind = ML_POSTPROC_PAD_BBOX },
};
static const ml_postproc_output_pad_spec_t *yolov5_builtin_get_pads(int *count) {
  *count = 1;
  return s_yolov5_builtin_pads;
}
static const ml_postproc_ops_t mlpp_yolov5_builtin_ops = {
  .name = "yolov5_builtin",
  .description = "YOLOv5 built-in (cavalry 1037/1017/997)",
  .process = mlpp_yolov5_post_process,
  .get_result_types = NULL,
  .get_output_pads = yolov5_builtin_get_pads,
  .get_output_layout = NULL,
  .deinit_user_ctx = NULL,
  .output_coords_normalized = FALSE,
};
void ml_register_yolov5_builtin(void)
{
  ml_register_postproc("yolov5_builtin", &mlpp_yolov5_builtin_ops);
}
