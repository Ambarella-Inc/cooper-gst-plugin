/*
 * eazyai_postprocess.c
 *
 * History:
 *    3/10/2026 - [pxduan] created file
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
 * libeazyai_postprocess wrapper for mlpostprocess.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <glib.h>

#include "internal.h"
#include "debug_log.h"
#include "gstmlpostprocess.h"
#include "ml_postprocess_if.h"
#include "element_common.h"
#include "ea_postproc_common.h"
#include "ea_postproc_yolov8_det.h"
#include "ea_postproc_yolov10.h"
#include "ea_postproc_yolov6.h"
#include "ea_postproc_yolo11_det.h"
#include "ea_postproc_yolov12_det.h"
#include "ea_postproc_yolo26_det.h"
#include "ea_postproc_yolox.h"
#include "ea_postproc_yolo_det_common.h"
#include "ea_postproc_yolop_seg.h"
#include "ea_postproc_centernet.h"
#include "ea_postproc_rtmdet.h"
#include "ea_postproc_retinaface.h"
#include "ea_postproc_fgfd.h"
#include "ea_postproc_lffd.h"
#include "ea_postproc_yolov8_seg.h"
#include "ea_postproc_yolo11_seg.h"
#include "yolop.h"
#include "ea_postproc_yolov12_seg.h"
#include "ea_postproc_yolo26_seg.h"

#define EA_P_SINGLE_DET_LOCAL_NUMBER 4

/* YOLOv8-seg box row: 4 bbox + nc + nm (mask coeffs). Default nm matches yolov8n-seg / nn_arm mask_num. */
#define MLPP_YOLO_SEG_MASK_COEFF_DEFAULT 32

/* YOLOv5 anchors: 18 values, flattened [w,h] per anchor.
 * Order [small, medium, large] to match eazyai reorder_feature_map (ordered[0]=52x52, [1]=26x26, [2]=13x13). */
static const float s_yolov5_anchors[18] = {
  10, 13, 16, 30, 33, 23,
  30, 61, 62, 45, 59, 119,
  116, 90, 156, 198, 373, 326
};

/* YOLOP anchors: order [small, medium, large] to match eazyai yolo_det_common reorder_feature_map. */
static const float s_yolop_anchors[18] = {
  3, 9, 5, 11, 4, 20,
  7, 18, 6, 39, 12, 31,
  19, 50, 38, 81, 68, 157
};

/* RetinaFace anchors (ref nn_arm_task/face/retinaface: stride 32/16/8 x 2 anchors x 4) */
static const float s_retinaface_anchors[3][2][4] = {
  {{-248.f, -248.f, 263.f, 263.f}, {-120.f, -120.f, 135.f, 135.f}},
  {{-56.f, -56.f, 71.f, 71.f}, {-24.f, -24.f, 39.f, 39.f}},
  {{-8.f, -8.f, 23.f, 23.f}, {0.f, 0.f, 15.f, 15.f}}
};

/* Copy ea detection output to mlpostprocess result.
 * eazyai with output_normalize=1 outputs bbox in 0~1 range; keep as-is.
 * gstmlpostprocess scales with x*mw (output_coords_normalized=TRUE). */
static void copy_ea_bbox_to_result(ea_postproc_detection_bbox_t *out_boxes,
    uint32_t valid_num, ml_postproc_ctx_t *pp_ctx)
{
  bounding_boxes_t *result = pp_ctx->result;
  result->det_num = 0;
  unsigned int k;
  for (k = 0; k < valid_num && k < D_MAX_DET_NUM; k++) {
    det_object_t *d = &result->detections[k];
    d->score = out_boxes[k].score;
    d->id = out_boxes[k].id;
    d->x_start = out_boxes[k].box.x_start;
    d->y_start = out_boxes[k].box.y_start;
    d->x_end = out_boxes[k].box.x_end;
    d->y_end = out_boxes[k].box.y_end;
    if (d->id >= 0 && d->id < (int)pp_ctx->valid_label_count && pp_ctx->labels[d->id][0])
      (void)snprintf(d->label, (size_t)DMAX_LABEL_LEN, "%s", pp_ctx->labels[d->id]);
    else
      d->label[0] = '\0';
    result->det_num++;
  }
}

/* Reused per-element buffer: ea_postproc_detection_bbox_t[top_k] (grows; freed in mlpostprocess finalize). */
static int mlpp_ensure_ea_det_bbox_buf(mlpp_priv_ctx_t *priv, int top_k)
{
  int n;
  gsize need;
  gpointer nb;

  if (!priv)
    return -1;
  n = top_k > 0 ? top_k : 1;
  if (priv->ea_det_bbox_cap >= n && priv->ea_det_bbox_buf)
    return 0;
  need = (gsize)n * sizeof(ea_postproc_detection_bbox_t);
  nb = g_try_realloc(priv->ea_det_bbox_buf, need);
  if (!nb)
    return -1;
  priv->ea_det_bbox_buf = nb;
  priv->ea_det_bbox_cap = n;
  return 0;
}

/* Sort indices by spatial size (H*W) ascending. Used only by RTMDet which expects
 * p_tensor[0]=stride8, [1]=stride16, [2]=stride32 (smallest FM first). */
static void sort_indices_by_spatial_asc(ml_postproc_ctx_t *pp_ctx, int n, int *idx)
{
  int i, j, tmp;
  for (i = 0; i < n; i++) {
    idx[i] = i;
  }
  for (i = 0; i < n - 1; i++) {
    for (j = i + 1; j < n; j++) {
      int sz_i = pp_ctx->tensors[idx[i]].height * pp_ctx->tensors[idx[i]].width;
      int sz_j = pp_ctx->tensors[idx[j]].height * pp_ctx->tensors[idx[j]].width;
      if (sz_j < sz_i) {
        tmp = idx[i]; idx[i] = idx[j]; idx[j] = tmp;
      }
    }
  }
}

/* Build ea_tensor from first ml tensor. Gst caps dimensions are width:height:channels
 * (see mlinference2), aligned with EazyAI NCHW. */
static int build_single_tensor(ea_postproc_tensor_t *ea_tensor, ml_postproc_ctx_t *pp_ctx,
    int *out_class_num, int *out_box_num)
{
  int c0;
  if (!pp_ctx || pp_ctx->num_tensors < 1)
    return -1;
  memset(ea_tensor, 0, sizeof(*ea_tensor));
  ea_tensor->p_buffer = pp_ctx->tensors[0].data;
  ea_tensor->shape[EA_P_N] = 1;
  c0 = pp_ctx->tensors[0].depth;
  ea_tensor->shape[EA_P_C] = c0 > 0 ? c0 : 1;
  ea_tensor->shape[EA_P_H] = pp_ctx->tensors[0].height;
  ea_tensor->shape[EA_P_W] = pp_ctx->tensors[0].width;
  ea_tensor->pitch = (uint32_t)pp_ctx->tensors[0].pitch;
  ea_tensor->data_format = EA_P_F32;
  ea_tensor->p_name = pp_ctx->tensors[0].name[0] ? pp_ctx->tensors[0].name : "output0";

  *out_class_num = (int)ea_tensor->shape[EA_P_W] - EA_P_SINGLE_DET_LOCAL_NUMBER;
  *out_box_num = (int)ea_tensor->shape[EA_P_H];
  if (*out_class_num <= 0 || *out_box_num <= 0)
    return -1;
  return 0;
}

/* Build config for single-tensor init */
static void build_single_det_config(ea_postproc_detection_config_t *config,
    ea_postproc_nn_input_info_t *nn_info, ea_postproc_tensor_t *ea_tensor,
    ml_postproc_ctx_t *pp_ctx, int class_num)
{
  memset(config, 0, sizeof(*config));
  memset(nn_info, 0, sizeof(*nn_info));
  config->postp_input.p_tensor = ea_tensor;
  config->postp_input.num = 1;
  config->p_nn_orig_input_info = nn_info;
  config->nn_orig_input_info_num = 1;
  nn_info->shape[EA_P_N] = 1;
  nn_info->shape[EA_P_C] = 3;
  nn_info->shape[EA_P_W] = pp_ctx->nn_input_width > 0 ? pp_ctx->nn_input_width : 640;
  nn_info->shape[EA_P_H] = pp_ctx->nn_input_height > 0 ? pp_ctx->nn_input_height : 640;
  nn_info->p_in_port_name = NULL;
  config->conf_threshold = pp_ctx->conf_threshold;
  config->nms_threshold = pp_ctx->nms_threshold;
  config->nms_topk = (uint32_t)pp_ctx->top_k;
  config->topk = (uint32_t)pp_ctx->top_k;
  config->class_num = (uint32_t)class_num;
  config->output_normalize = 1;
  config->log_level = 0;
}

static int mlpp_ensure_yolo_tpose_f32(mlpp_priv_ctx_t *priv, gsize nfloat)
{
  gsize need;
  gpointer nb;

  if (!priv)
    return -1;
  need = nfloat * sizeof(float);
  if (priv->yolo_tpose_f32 && priv->yolo_tpose_f32_bytes >= need)
    return 0;
  nb = g_try_realloc(priv->yolo_tpose_f32, need);
  if (!nb)
    return -1;
  priv->yolo_tpose_f32 = (float *)nb;
  priv->yolo_tpose_f32_bytes = need;
  return 0;
}

/* S is h rows x w columns (row c = src + c*row_stride_bytes); build T = S^T with w rows x h columns. */
static void yolo_transpose_hrows_wcols(const float *src, int w, int h, gsize row_stride_bytes,
    float *dst)
{
  int r, c;
  const guchar *base = (const guchar *)src;
  for (r = 0; r < w; r++) {
    for (c = 0; c < h; c++) {
      dst[(gsize)r * (gsize)h + c] = ((const float *)(base + (gsize)c * row_stride_bytes))[r];
    }
  }
}

static int build_single_yolov12_ea_tensor(ea_postproc_tensor_t *ea_tensor, ml_postproc_ctx_t *pp_ctx,
    mlpp_priv_ctx_t *priv, int *out_class_num, int *out_box_num)
{
  const ml_postproc_tensor_desc_t *t = &pp_ctx->tensors[0];
  int w = t->width, h = t->height, c = t->depth;
  gsize row_stride = (t->pitch > 0) ? (gsize)t->pitch : (gsize)w * sizeof(float);

  if (c != 1)
    return build_single_tensor(ea_tensor, pp_ctx, out_class_num, out_box_num);

  /* Cavalry (1,1,8400,84) -> nnctrl W=84,H=8400 -> caps "84:8400:1": same row layout as YOLOv8. */
  if (w >= 5 && w <= 1024 && h > w && (w - 4) > 0 && (w - 4) <= 16000)
    return build_single_tensor(ea_tensor, pp_ctx, out_class_num, out_box_num);

  /* Caps "8400:84:1": N x (4+nc) in memory — transpose to (4+nc) x N for EazyAI. */
  if (h >= 5 && h <= 1024 && w > h && (h - 4) > 0 && (h - 4) <= 16000) {
    if (mlpp_ensure_yolo_tpose_f32(priv, (gsize)w * (gsize)h) < 0)
      return -1;
    yolo_transpose_hrows_wcols(t->data, w, h, row_stride, priv->yolo_tpose_f32);
    memset(ea_tensor, 0, sizeof(*ea_tensor));
    ea_tensor->p_buffer = priv->yolo_tpose_f32;
    ea_tensor->shape[EA_P_N] = 1;
    ea_tensor->shape[EA_P_C] = 1;
    ea_tensor->shape[EA_P_H] = w;
    ea_tensor->shape[EA_P_W] = h;
    ea_tensor->pitch = (uint32_t)(h * (int)sizeof(float));
    ea_tensor->data_format = EA_P_F32;
    ea_tensor->p_name = pp_ctx->tensors[0].name[0] ? pp_ctx->tensors[0].name : "output0";
    *out_class_num = h - 4;
    *out_box_num = w;
    if (*out_class_num <= 0 || *out_box_num <= 0)
      return -1;
    return 0;
  }

  return build_single_tensor(ea_tensor, pp_ctx, out_class_num, out_box_num);
}

/* -------- Single-tensor detection (YOLOv8, YOLOv10, YOLOv6, YOLO11, YOLOv12, YOLO26) -------- */
typedef void* (*ea_single_init_fn)(ea_postproc_detection_config_t *config);
typedef int (*ea_single_process_fn)(void *ctx, ea_postproc_input_t *in, uint32_t detect_num,
    ea_postproc_detection_bbox_t *out, uint32_t *valid_num);
typedef void (*ea_single_deinit_fn)(void *ctx);

#define DEF_SINGLE_DET(NAME, INIT_FN, PROCESS_FN, DEINIT_FN) \
static int eazyai_##NAME##_process(void *ctx) { \
  ml_postproc_ctx_t *pp_ctx = (ml_postproc_ctx_t *)ctx; \
  mlpp_priv_ctx_t *priv = (mlpp_priv_ctx_t *)pp_ctx->user_data; \
  void *ea_ctx = priv ? priv->eazyai_postp_ctx : NULL; \
  int rval = -1, class_num, box_num; \
  ea_postproc_tensor_t ea_tensor; \
  if (!pp_ctx || pp_ctx->num_tensors < 1 || !pp_ctx->result) { \
    return -1; \
  } \
  if (build_single_tensor(&ea_tensor, pp_ctx, &class_num, &box_num) < 0) { \
    return -1; \
  } \
  if (!ea_ctx && priv) { \
    ea_postproc_detection_config_t config; \
    ea_postproc_nn_input_info_t nn_info; \
    build_single_det_config(&config, &nn_info, &ea_tensor, pp_ctx, class_num); \
    ea_ctx = INIT_FN(&config); \
    if (!ea_ctx) { \
      return -1; \
    } \
    priv->eazyai_postp_ctx = ea_ctx; \
  } \
  if (!ea_ctx) { \
    return -1; \
  } \
  if (!priv || mlpp_ensure_ea_det_bbox_buf(priv, pp_ctx->top_k) < 0) { \
    return -1; \
  } \
  ea_postproc_input_t postp_input = { .p_tensor = &ea_tensor, .num = 1 }; \
  { \
    ea_postproc_detection_bbox_t *out_boxes = (ea_postproc_detection_bbox_t *)priv->ea_det_bbox_buf; \
    uint32_t valid_num = 0; \
    rval = PROCESS_FN(ea_ctx, &postp_input, (uint32_t)pp_ctx->top_k, out_boxes, &valid_num); \
    if (rval == 0) { \
      copy_ea_bbox_to_result(out_boxes, valid_num, pp_ctx); \
    } \
  } \
  return (rval == 0) ? 0 : -1; \
} \
static void eazyai_##NAME##_deinit(void *priv) { \
  mlpp_priv_ctx_t *p = (mlpp_priv_ctx_t *)priv; \
  if (!p || !p->eazyai_postp_ctx) { \
    return; \
  } \
  DEINIT_FN(p->eazyai_postp_ctx); \
  p->eazyai_postp_ctx = NULL; \
}

DEF_SINGLE_DET(yolov8_det, ea_postp_yolov8_det_init, ea_postp_yolov8_det, ea_postp_yolov8_det_deinit)
DEF_SINGLE_DET(yolov10, ea_postp_yolov10_init, ea_postp_yolov10, ea_postp_yolov10_deinit)
DEF_SINGLE_DET(yolo11_det, ea_postp_yolo11_det_init, ea_postp_yolo11_det, ea_postp_yolo11_det_deinit)
DEF_SINGLE_DET(yolo26_det, ea_postp_yolo26_det_init, ea_postp_yolo26_det, ea_postp_yolo26_det_deinit)

static int eazyai_yolov12_det_process(void *ctx)
{
  ml_postproc_ctx_t *pp_ctx = (ml_postproc_ctx_t *)ctx;
  mlpp_priv_ctx_t *priv = (mlpp_priv_ctx_t *)pp_ctx->user_data;
  void *ea_ctx = priv ? priv->eazyai_postp_ctx : NULL;
  int rval = -1, class_num, box_num;
  ea_postproc_tensor_t ea_tensor;

  if (!pp_ctx || pp_ctx->num_tensors < 1 || !pp_ctx->result)
    return -1;
  if (build_single_yolov12_ea_tensor(&ea_tensor, pp_ctx, priv, &class_num, &box_num) < 0)
    return -1;
  if (!ea_ctx && priv) {
    ea_postproc_detection_config_t config;
    ea_postproc_nn_input_info_t nn_info;
    build_single_det_config(&config, &nn_info, &ea_tensor, pp_ctx, class_num);
    ea_ctx = ea_postp_yolov12_det_init(&config);
    if (!ea_ctx)
      return -1;
    priv->eazyai_postp_ctx = ea_ctx;
  }
  if (!ea_ctx)
    return -1;
  if (!priv || mlpp_ensure_ea_det_bbox_buf(priv, pp_ctx->top_k) < 0)
    return -1;
  ea_postproc_input_t postp_input = { .p_tensor = &ea_tensor, .num = 1 };
  {
    ea_postproc_detection_bbox_t *out_boxes = (ea_postproc_detection_bbox_t *)priv->ea_det_bbox_buf;
    uint32_t valid_num = 0;
    rval = ea_postp_yolov12_det(ea_ctx, &postp_input, (uint32_t)pp_ctx->top_k, out_boxes, &valid_num);
    if (rval == 0)
      copy_ea_bbox_to_result(out_boxes, valid_num, pp_ctx);
  }
  return (rval == 0) ? 0 : -1;
}

static void eazyai_yolov12_det_deinit(void *priv)
{
  mlpp_priv_ctx_t *p = (mlpp_priv_ctx_t *)priv;
  if (!p || !p->eazyai_postp_ctx)
    return;
  ea_postp_yolov12_det_deinit(p->eazyai_postp_ctx);
  p->eazyai_postp_ctx = NULL;
}

static int eazyai_yolov6_process(void *ctx)
{
  ml_postproc_ctx_t *pp_ctx = (ml_postproc_ctx_t *)ctx;
  mlpp_priv_ctx_t *priv = (mlpp_priv_ctx_t *)pp_ctx->user_data;
  void *ea_ctx = priv ? priv->eazyai_postp_ctx : NULL;
  int rval = -1, class_num, box_num;
  ea_postproc_tensor_t ea_tensor;

  if (!pp_ctx || pp_ctx->num_tensors < 1 || !pp_ctx->result)
    return -1;
  if (build_single_tensor(&ea_tensor, pp_ctx, &class_num, &box_num) < 0)
    return -1;
  class_num = pp_ctx->tensors[0].width - 5;
  if (class_num <= 0 || box_num <= 0)
    return -1;

  if (!ea_ctx && priv) {
    ea_postproc_detection_config_t config;
    ea_postproc_nn_input_info_t nn_info;
    build_single_det_config(&config, &nn_info, &ea_tensor, pp_ctx, class_num);
    ea_ctx = ea_postp_yolov6_init(&config);
    if (!ea_ctx)
      return -1;
    priv->eazyai_postp_ctx = ea_ctx;
  }
  if (!ea_ctx)
    return -1;
  if (!priv || mlpp_ensure_ea_det_bbox_buf(priv, pp_ctx->top_k) < 0)
    return -1;

  ea_postproc_input_t postp_input = { .p_tensor = &ea_tensor, .num = 1 };
  {
    ea_postproc_detection_bbox_t *out_boxes = (ea_postproc_detection_bbox_t *)priv->ea_det_bbox_buf;
    uint32_t valid_num = 0;
    rval = ea_postp_yolov6(ea_ctx, &postp_input, (uint32_t)pp_ctx->top_k, out_boxes, &valid_num);
    if (rval == 0)
      copy_ea_bbox_to_result(out_boxes, valid_num, pp_ctx);
  }
  return (rval == 0) ? 0 : -1;
}

static void eazyai_yolov6_deinit(void *priv)
{
  mlpp_priv_ctx_t *p = (mlpp_priv_ctx_t *)priv;
  if (!p || !p->eazyai_postp_ctx)
    return;
  ea_postp_yolov6_deinit(p->eazyai_postp_ctx);
  p->eazyai_postp_ctx = NULL;
}

/* -------- RTMDet (3 tensors, multi-scale stride 8/16/32, ref nn_arm_task/rtmdet) -------- */
static int eazyai_rtmdet_process(void *ctx)
{
  ml_postproc_ctx_t *pp_ctx = (ml_postproc_ctx_t *)ctx;
  mlpp_priv_ctx_t *priv = (mlpp_priv_ctx_t *)pp_ctx->user_data;
  void *ea_ctx = priv ? priv->eazyai_postp_ctx : NULL;
  int rval = -1, i, n = pp_ctx->num_tensors;
  int idx[AMBA_ML_MAX_TENSORS];

  if (!pp_ctx || n < 3 || !pp_ctx->result) {
    return -1;
  }
  if (n > AMBA_ML_MAX_TENSORS) {
    n = AMBA_ML_MAX_TENSORS;
  }

  sort_indices_by_spatial_asc(pp_ctx, n, idx);

  ea_postproc_tensor_t ea_tensors[AMBA_ML_MAX_TENSORS];
  memset(ea_tensors, 0, sizeof(ea_tensors));
  for (i = 0; i < n; i++) {
    int ti = idx[i];
    ea_tensors[i].p_buffer = pp_ctx->tensors[ti].data;
    ea_tensors[i].shape[EA_P_N] = 1;
    ea_tensors[i].shape[EA_P_C] = pp_ctx->tensors[ti].depth;
    ea_tensors[i].shape[EA_P_H] = pp_ctx->tensors[ti].height;
    ea_tensors[i].shape[EA_P_W] = pp_ctx->tensors[ti].width;
    ea_tensors[i].pitch = (uint32_t)pp_ctx->tensors[ti].pitch;
    ea_tensors[i].data_format = EA_P_F32;
    ea_tensors[i].p_name = pp_ctx->tensors[ti].name[0] ? pp_ctx->tensors[ti].name : "output";
  }

  if (!ea_ctx && priv) {
    int class_num = (int)pp_ctx->tensors[idx[0]].depth - 4;
    if (class_num <= 0) {
      class_num = 80;
    }
    ea_postproc_detection_config_t config;
    ea_postproc_nn_input_info_t nn_info;
    memset(&config, 0, sizeof(config));
    memset(&nn_info, 0, sizeof(nn_info));
    config.postp_input.p_tensor = ea_tensors;
    config.postp_input.num = (uint32_t)n;
    config.p_nn_orig_input_info = &nn_info;
    config.nn_orig_input_info_num = 1;
    nn_info.shape[EA_P_W] = pp_ctx->nn_input_width > 0 ? pp_ctx->nn_input_width : 640;
    nn_info.shape[EA_P_H] = pp_ctx->nn_input_height > 0 ? pp_ctx->nn_input_height : 640;
    nn_info.p_in_port_name = "images";
    config.conf_threshold = pp_ctx->conf_threshold;
    config.nms_threshold = pp_ctx->nms_threshold;
    config.nms_topk = (uint32_t)pp_ctx->top_k;
    config.topk = (uint32_t)pp_ctx->top_k;
    config.class_num = (uint32_t)class_num;
    config.output_normalize = 1;
    ea_ctx = ea_postp_rtmdet_init(&config);
    if (!ea_ctx) {
      return -1;
    }
    priv->eazyai_postp_ctx = ea_ctx;
  }
  if (!ea_ctx) {
    return -1;
  }
  if (!priv || mlpp_ensure_ea_det_bbox_buf(priv, pp_ctx->top_k) < 0) {
    return -1;
  }

  ea_postproc_input_t postp_input = { .p_tensor = ea_tensors, .num = (uint32_t)n };
  {
    ea_postproc_detection_bbox_t *out_boxes = (ea_postproc_detection_bbox_t *)priv->ea_det_bbox_buf;
    uint32_t valid_num = 0;
    rval = ea_postp_rtmdet(ea_ctx, &postp_input, (uint32_t)pp_ctx->top_k, out_boxes, &valid_num);
    if (rval == 0) {
      copy_ea_bbox_to_result(out_boxes, valid_num, pp_ctx);
    }
  }
  return (rval == 0) ? 0 : -1;
}

static void eazyai_rtmdet_deinit(void *priv)
{
  mlpp_priv_ctx_t *p = (mlpp_priv_ctx_t *)priv;
  if (!p || !p->eazyai_postp_ctx)
    return;
  ea_postp_rtmdet_deinit(p->eazyai_postp_ctx);
  p->eazyai_postp_ctx = NULL;
}

/* -------- LFFD (8 tensors: prob/bbox for stride 4/8/16/32, ref nn_arm_task/face/lffd) -------- */
static int eazyai_lffd_process(void *ctx)
{
  ml_postproc_ctx_t *pp_ctx = (ml_postproc_ctx_t *)ctx;
  mlpp_priv_ctx_t *priv = (mlpp_priv_ctx_t *)pp_ctx->user_data;
  void *ea_ctx = priv ? priv->eazyai_postp_ctx : NULL;
  int rval = -1, i, n = pp_ctx->num_tensors;

  if (!pp_ctx || n < 8 || !pp_ctx->result)
    return -1;
  /* LFFD requires exactly 8 tensors; ignore extras if caps list more. */
  if (n > 8)
    n = 8;

  /* eazyai LFFD uses shape-based lookup; input order not required. */
  ea_postproc_tensor_t ea_tensors[8];
  memset(ea_tensors, 0, sizeof(ea_tensors));
  for (i = 0; i < 8; i++) {
    ea_tensors[i].p_buffer = pp_ctx->tensors[i].data;
    ea_tensors[i].shape[EA_P_N] = 1;
    ea_tensors[i].shape[EA_P_C] = pp_ctx->tensors[i].depth;
    ea_tensors[i].shape[EA_P_H] = pp_ctx->tensors[i].height;
    ea_tensors[i].shape[EA_P_W] = pp_ctx->tensors[i].width;
    ea_tensors[i].pitch = (uint32_t)pp_ctx->tensors[i].pitch;
    ea_tensors[i].data_format = EA_P_F32;
    ea_tensors[i].p_name = pp_ctx->tensors[i].name[0] ? pp_ctx->tensors[i].name : "output";
  }

  if (!ea_ctx && priv) {
    ea_postproc_detection_config_t config;
    ea_postproc_nn_input_info_t nn_info;
    memset(&config, 0, sizeof(config));
    memset(&nn_info, 0, sizeof(nn_info));
    config.postp_input.p_tensor = ea_tensors;
    config.postp_input.num = 8;
    config.p_nn_orig_input_info = &nn_info;
    config.nn_orig_input_info_num = 1;
    nn_info.shape[EA_P_W] = pp_ctx->nn_input_width > 0 ? pp_ctx->nn_input_width : 640;
    nn_info.shape[EA_P_H] = pp_ctx->nn_input_height > 0 ? pp_ctx->nn_input_height : 640;
    nn_info.p_in_port_name = "images";
    config.conf_threshold = pp_ctx->conf_threshold;
    config.nms_threshold = pp_ctx->nms_threshold;
    config.nms_topk = (uint32_t)pp_ctx->top_k;
    config.topk = (uint32_t)pp_ctx->top_k;
    config.class_num = 1;
    config.output_normalize = 1;
    ea_ctx = ea_postp_lffd_init(&config);
    if (!ea_ctx)
      return -1;
    priv->eazyai_postp_ctx = ea_ctx;
  }
  if (!ea_ctx)
    return -1;
  if (!priv || mlpp_ensure_ea_det_bbox_buf(priv, pp_ctx->top_k) < 0)
    return -1;

  ea_postproc_input_t postp_input = { .p_tensor = ea_tensors, .num = 8 };
  {
    ea_postproc_detection_bbox_t *out_boxes = (ea_postproc_detection_bbox_t *)priv->ea_det_bbox_buf;
    uint32_t valid_num = 0;
    rval = ea_postp_lffd(ea_ctx, &postp_input, (uint32_t)pp_ctx->top_k, out_boxes, &valid_num);
    if (rval == 0)
      copy_ea_bbox_to_result(out_boxes, valid_num, pp_ctx);
  }
  return (rval == 0) ? 0 : -1;
}

static void eazyai_lffd_deinit(void *priv)
{
  mlpp_priv_ctx_t *p = (mlpp_priv_ctx_t *)priv;
  if (!p || !p->eazyai_postp_ctx)
    return;
  ea_postp_lffd_deinit(p->eazyai_postp_ctx);
  p->eazyai_postp_ctx = NULL;
}

/* -------- YOLOv8/11/12/26 seg (2 tensors: box + proto, ref nn_arm_task/yolov8/yolov8_seg) -------- */

static void yolo_seg_free_det_stash(mlpp_priv_ctx_t *priv)
{
  int i;
  ea_postproc_detection_bbox_t *buf;

  if (!priv || !priv->yolo_seg_det_buf)
    return;
  buf = (ea_postproc_detection_bbox_t *)priv->yolo_seg_det_buf;
  for (i = 0; i < priv->yolo_seg_buf_slots; i++) {
    if (buf[i].map.p_buffer) {
      free(buf[i].map.p_buffer);
      buf[i].map.p_buffer = NULL;
    }
  }
  free(buf);
  priv->yolo_seg_det_buf = NULL;
  priv->yolo_seg_buf_slots = 0;
  priv->yolo_seg_mask_w = 0;
  priv->yolo_seg_mask_h = 0;
}

/* Replacing stash while EA ctx is still live: we own det_buf only (masks still ours). */
static void yolo_seg_free_det_buf_array_only(mlpp_priv_ctx_t *priv)
{
  if (!priv || !priv->yolo_seg_det_buf)
    return;
  free(priv->yolo_seg_det_buf);
  priv->yolo_seg_det_buf = NULL;
  priv->yolo_seg_buf_slots = 0;
  priv->yolo_seg_mask_w = 0;
  priv->yolo_seg_mask_h = 0;
}

/* After ea_postp_*_seg_deinit / lib ea deinit: lib may free det_buf and/or map.p_buffer — do not free again. */
static void yolo_seg_detach_det_stash(mlpp_priv_ctx_t *priv)
{
  if (!priv)
    return;
  priv->yolo_seg_det_buf = NULL;
  priv->yolo_seg_buf_slots = 0;
  priv->yolo_seg_mask_w = 0;
  priv->yolo_seg_mask_h = 0;
}

typedef struct {
  ea_postproc_detection_config_t config;
  ea_postproc_nn_input_info_t nn_info;
  ea_postproc_tensor_t init_tensors[2];
} mlpp_yolo_seg_ea_cfg_t;

static mlpp_yolo_seg_ea_cfg_t *
yolo_seg_ensure_ea_cfg_store(mlpp_priv_ctx_t *priv)
{
  if (!priv)
    return NULL;
  if (!priv->yolo_seg_ea_cfg_store)
    priv->yolo_seg_ea_cfg_store = g_malloc0(sizeof(mlpp_yolo_seg_ea_cfg_t));
  return (mlpp_yolo_seg_ea_cfg_t *)priv->yolo_seg_ea_cfg_store;
}

static void
yolo_seg_free_ea_cfg_store(mlpp_priv_ctx_t *priv)
{
  if (!priv)
    return;
  g_free(priv->yolo_seg_ea_cfg_store);
  priv->yolo_seg_ea_cfg_store = NULL;
}

static void
yolo_seg_build_ea_init_config(mlpp_yolo_seg_ea_cfg_t *store,
    const ea_postproc_tensor_t *ea_tensors, ml_postproc_ctx_t *pp_ctx, int class_num)
{
  memcpy(store->init_tensors, ea_tensors, sizeof(store->init_tensors));
  build_single_det_config(&store->config, &store->nn_info, &store->init_tensors[0],
      pp_ctx, class_num);
  store->config.postp_input.p_tensor = store->init_tensors;
  store->config.postp_input.num = 2;
}

/**
 * nnctrl lists proto as "32:104:104" (C:H:W). f16 unpack and tensors[1].data layout match that.
 * YOLO mask plane is spatial H×W = 104×104 — use this only to size per-detection U8 masks in stash,
 * not for ea_postp tensor shape (those must match the F32 buffer layout).
 */
static void yolo_seg_proto_effective_dims(const ml_postproc_tensor_desc_t *t,
    int *eff_w, int *eff_h, int *eff_c)
{
  *eff_w = t->width;
  *eff_h = t->height;
  *eff_c = t->depth;
  if (t->height == t->depth && t->width > 0 && t->width <= 128 && t->width < t->height) {
    *eff_c = t->width;
    *eff_h = t->height;
    *eff_w = t->depth;
  }
}

/* Proto tensor NCHW (1,32,104,104): mask plane is spatial H×W = eff_h×eff_w (e.g. 104×104). */
static int yolo_seg_ensure_det_stash(mlpp_priv_ctx_t *priv, ml_postproc_ctx_t *pp_ctx)
{
  int mask_h, mask_w, eff_w, eff_h, eff_c, slots, i;
  ea_postproc_detection_bbox_t *buf;

  if (!priv || !pp_ctx || pp_ctx->num_tensors < 2)
    return -1;
  yolo_seg_proto_effective_dims(&pp_ctx->tensors[1], &eff_w, &eff_h, &eff_c);
  mask_h = eff_h;
  mask_w = eff_w;
  if (mask_h <= 0 || mask_w <= 0)
    return -1;

  slots = pp_ctx->top_k > 0 ? pp_ctx->top_k : 32;
  if (slots > D_MAX_DET_NUM)
    slots = D_MAX_DET_NUM;

  if (priv->yolo_seg_det_buf &&
      (priv->yolo_seg_mask_w != mask_w || priv->yolo_seg_mask_h != mask_h ||
          priv->yolo_seg_buf_slots != slots)) {
    if (priv->eazyai_postp_ctx)
      yolo_seg_free_det_buf_array_only(priv);
    else
      yolo_seg_free_det_stash(priv);
  }
  if (priv->yolo_seg_det_buf)
    return 0;

  buf = (ea_postproc_detection_bbox_t *)calloc((size_t)slots, sizeof(ea_postproc_detection_bbox_t));
  if (!buf)
    return -1;
  for (i = 0; i < slots; i++) {
    buf[i].map.p_buffer = (uint8_t *)malloc((size_t)mask_h * (size_t)mask_w);
    if (!buf[i].map.p_buffer) {
      int j;
      for (j = 0; j < i; j++) {
        free(buf[j].map.p_buffer);
        buf[j].map.p_buffer = NULL;
      }
      free(buf);
      return -1;
    }
    memset(buf[i].map.p_buffer, 0, (size_t)mask_h * (size_t)mask_w);
    buf[i].map.shape[EA_P_N] = 1;
    buf[i].map.shape[EA_P_C] = 1;
    buf[i].map.shape[EA_P_H] = mask_h;
    buf[i].map.shape[EA_P_W] = mask_w;
    buf[i].map.pitch = (uint32_t)mask_w;
    buf[i].map.data_format = EA_P_U8;
  }
  priv->yolo_seg_det_buf = buf;
  priv->yolo_seg_buf_slots = slots;
  priv->yolo_seg_mask_w = mask_w;
  priv->yolo_seg_mask_h = mask_h;
  return 0;
}

/* Merge all instance U8 masks into one SEGMENTATION buffer (0=bg, pixel = class_id+1). One GstMemory. */
static void yolo_seg_merge_instance_maps(mlpp_priv_ctx_t *priv, ml_postproc_ctx_t *pp_ctx,
    ea_postproc_detection_bbox_t *results, uint32_t valid_num)
{
  int mh, mw;
  size_t sz;
  uint8_t *merged;
  uint32_t i;
  int h, w;

  if (!priv || !pp_ctx || !results)
    return;
  mh = priv->yolo_seg_mask_h;
  mw = priv->yolo_seg_mask_w;
  if (mh <= 0 || mw <= 0)
    return;
  sz = (size_t)mh * (size_t)mw;
  if (pp_ctx->seg_outputs[0].mask) {
    free(pp_ctx->seg_outputs[0].mask);
    pp_ctx->seg_outputs[0].mask = NULL;
  }
  merged = (uint8_t *)malloc(sz);
  if (!merged)
    return;
  memset(merged, 0, sz);
  for (i = 0; i < valid_num; i++) {
    uint32_t class_idx = (uint32_t)results[i].id + 1;
    uint8_t *mb = results[i].map.p_buffer;
    if (class_idx > 255u)
      class_idx = 255u;
    if (!mb)
      continue;
    for (h = 0; h < mh; h++) {
      for (w = 0; w < mw; w++) {
        if (mb[h * mw + w] > 0)
          merged[h * mw + w] = (uint8_t)class_idx;
      }
    }
  }
  pp_ctx->seg_outputs[0].mask = merged;
  pp_ctx->seg_outputs[0].width = mw;
  pp_ctx->seg_outputs[0].height = mh;
}

typedef void *(*yolo_seg_init_fn)(ea_postproc_detection_config_t *, uint32_t);
typedef int (*yolo_seg_run_fn)(void *, ea_postproc_input_t *, uint32_t, ea_postproc_detection_bbox_t *, uint32_t *);

/**
 * Gst caps use width:height:channels from Cavalry (gstmlinference2). For det output
 * NCHW (1,1,3549,116), caps are often "3549:116:1" — first number is H (anchors), second
 * is W (4+nc+nm). EazyAI expects EA_P_H=3549, EA_P_W=116; naive mapping sets EA_H=116, EA_W=3549
 * and class_num=3545, which breaks ea_postp_yolov8_seg.
 */
static void yolo_seg_fill_det_tensor_ea(ea_postproc_tensor_t *ea, const ml_postproc_tensor_desc_t *t)
{
  int w = t->width, h = t->height, c = t->depth;

  ea->p_buffer = (uint8_t *)t->data;
  ea->shape[EA_P_N] = 1;
  ea->data_format = EA_P_F32;
  ea->pitch = (uint32_t)t->pitch;
  if (c <= 0)
    c = 1;
  ea->shape[EA_P_C] = c;
  /* (1,1,H,W) with H>>W: caps sometimes swap so "width" is the anchor count */
  if (c == 1 && w > h && w >= 256 && h >= 8 && h <= 1024 && (w / h) >= 4) {
    ea->shape[EA_P_H] = w;
    ea->shape[EA_P_W] = h;
  } else {
    ea->shape[EA_P_H] = h;
    ea->shape[EA_P_W] = w;
  }
}

typedef void (*yolo_seg_lib_ea_deinit_fn)(void *ea_ctx);

static guint64 yolo_seg_cfg_sig(const ea_postproc_tensor_t *det,
    int proto_w, int proto_h, int proto_d, int slots)
{
  guint64 s = ((guint64)det->shape[EA_P_H] << 48) | ((guint64)det->shape[EA_P_W] << 32)
      | ((guint64)(guint32)proto_d << 24)
      | ((guint64)(guint32)proto_h << 12) | ((guint64)(guint32)proto_w & 0xfffu);
  s ^= (guint64)(guint32)slots * 1315423911u;
  return s;
}

static int eazyai_yolo_seg_process_impl(ml_postproc_ctx_t *pp_ctx, yolo_seg_init_fn init_fn,
    yolo_seg_run_fn run_fn, yolo_seg_lib_ea_deinit_fn lib_ea_deinit)
{
  mlpp_priv_ctx_t *priv = pp_ctx ? (mlpp_priv_ctx_t *)pp_ctx->user_data : NULL;
  void *ea_ctx = priv ? priv->eazyai_postp_ctx : NULL;
  int rval = -1, class_num;
  ea_postproc_tensor_t ea_tensors[2];
  ea_postproc_detection_bbox_t *det_buf;
  uint32_t valid_num = 0;
  int slots;
  guint64 sig;
  int feat_w;

  if (!pp_ctx || pp_ctx->num_tensors < 2 || !pp_ctx->result || !priv)
    return -1;

  memset(ea_tensors, 0, sizeof(ea_tensors));
  yolo_seg_fill_det_tensor_ea(&ea_tensors[0], &pp_ctx->tensors[0]);
  /* Must match gstmlpostprocess f16→f32 layout (nnctrl dim order), not semantic W:H:C remap. */
  ea_tensors[1].p_buffer = (uint8_t *)pp_ctx->tensors[1].data;
  ea_tensors[1].shape[EA_P_N] = 1;
  ea_tensors[1].shape[EA_P_C] = pp_ctx->tensors[1].depth;
  ea_tensors[1].shape[EA_P_H] = pp_ctx->tensors[1].height;
  ea_tensors[1].shape[EA_P_W] = pp_ctx->tensors[1].width;
  ea_tensors[1].pitch = (uint32_t)pp_ctx->tensors[1].pitch;
  ea_tensors[1].data_format = EA_P_F32;

  feat_w = (int)ea_tensors[0].shape[EA_P_W];
  /* nn_arm: class_num = W - mask_num - 4; do not use (W-4) alone (that equals nc+nm). */
  class_num = feat_w - EA_P_SINGLE_DET_LOCAL_NUMBER - MLPP_YOLO_SEG_MASK_COEFF_DEFAULT;
  if (class_num <= 0)
    class_num = 80;

  slots = pp_ctx->top_k > 0 ? pp_ctx->top_k : 32;
  if (slots > D_MAX_DET_NUM)
    slots = D_MAX_DET_NUM;

  sig = yolo_seg_cfg_sig(&ea_tensors[0], pp_ctx->tensors[1].width, pp_ctx->tensors[1].height,
      pp_ctx->tensors[1].depth, slots);
  if (ea_ctx && priv->yolo_seg_cfg_sig != sig && lib_ea_deinit) {
    lib_ea_deinit(ea_ctx);
    priv->eazyai_postp_ctx = NULL;
    priv->yolo_seg_cfg_sig = 0;
    ea_ctx = NULL;
    yolo_seg_detach_det_stash(priv);
    yolo_seg_free_ea_cfg_store(priv);
  }

  if (yolo_seg_ensure_det_stash(priv, pp_ctx) < 0)
    return -1;
  det_buf = (ea_postproc_detection_bbox_t *)priv->yolo_seg_det_buf;
  slots = priv->yolo_seg_buf_slots;

  if (!ea_ctx) {
    mlpp_yolo_seg_ea_cfg_t *store = yolo_seg_ensure_ea_cfg_store(priv);

    if (!store)
      return -1;
    yolo_seg_build_ea_init_config(store, ea_tensors, pp_ctx, class_num);
    ea_ctx = init_fn(&store->config, 0u);
    if (!ea_ctx)
      return -1;
    priv->eazyai_postp_ctx = ea_ctx;
    priv->yolo_seg_cfg_sig = sig;
  }

  {
    ea_postproc_input_t postp_input = { .p_tensor = ea_tensors, .num = 2 };
    rval = run_fn(ea_ctx, &postp_input, (uint32_t)slots, det_buf, &valid_num);
  }
  if (rval == 0) {
    copy_ea_bbox_to_result(det_buf, valid_num, pp_ctx);
    yolo_seg_merge_instance_maps(priv, pp_ctx, det_buf, valid_num);
  }
  return (rval == 0) ? 0 : -1;
}

static int eazyai_yolov8_seg_process(void *ctx)
{
  return eazyai_yolo_seg_process_impl((ml_postproc_ctx_t *)ctx,
      ea_postp_yolov8_seg_init, ea_postp_yolov8_seg, ea_postp_yolov8_seg_deinit);
}

static void eazyai_yolov8_seg_deinit(void *priv)
{
  mlpp_priv_ctx_t *p = (mlpp_priv_ctx_t *)priv;
  if (!p)
    return;
  if (p->eazyai_postp_ctx) {
    ea_postp_yolov8_seg_deinit(p->eazyai_postp_ctx);
    p->eazyai_postp_ctx = NULL;
    yolo_seg_detach_det_stash(p);
  } else {
    yolo_seg_free_det_stash(p);
  }
  yolo_seg_free_ea_cfg_store(p);
  p->yolo_seg_cfg_sig = 0;
}

static int eazyai_yolo11_seg_process(void *ctx)
{
  return eazyai_yolo_seg_process_impl((ml_postproc_ctx_t *)ctx,
      ea_postp_yolo11_seg_init, ea_postp_yolo11_seg, ea_postp_yolo11_seg_deinit);
}

static void eazyai_yolo11_seg_deinit(void *priv)
{
  mlpp_priv_ctx_t *p = (mlpp_priv_ctx_t *)priv;
  if (!p)
    return;
  if (p->eazyai_postp_ctx) {
    ea_postp_yolo11_seg_deinit(p->eazyai_postp_ctx);
    p->eazyai_postp_ctx = NULL;
    yolo_seg_detach_det_stash(p);
  } else {
    yolo_seg_free_det_stash(p);
  }
  yolo_seg_free_ea_cfg_store(p);
  p->yolo_seg_cfg_sig = 0;
}

static int eazyai_yolov12_seg_process(void *ctx)
{
  return eazyai_yolo_seg_process_impl((ml_postproc_ctx_t *)ctx,
      ea_postp_yolov12_seg_init, ea_postp_yolov12_seg, ea_postp_yolov12_seg_deinit);
}

static void eazyai_yolov12_seg_deinit(void *priv)
{
  mlpp_priv_ctx_t *p = (mlpp_priv_ctx_t *)priv;
  if (!p)
    return;
  if (p->eazyai_postp_ctx) {
    ea_postp_yolov12_seg_deinit(p->eazyai_postp_ctx);
    p->eazyai_postp_ctx = NULL;
    yolo_seg_detach_det_stash(p);
  } else {
    yolo_seg_free_det_stash(p);
  }
  yolo_seg_free_ea_cfg_store(p);
  p->yolo_seg_cfg_sig = 0;
}

static int eazyai_yolo26_seg_process(void *ctx)
{
  return eazyai_yolo_seg_process_impl((ml_postproc_ctx_t *)ctx,
      ea_postp_yolo26_seg_init, ea_postp_yolo26_seg, ea_postp_yolo26_seg_deinit);
}

static void eazyai_yolo26_seg_deinit(void *priv)
{
  mlpp_priv_ctx_t *p = (mlpp_priv_ctx_t *)priv;
  if (!p)
    return;
  if (p->eazyai_postp_ctx) {
    ea_postp_yolo26_seg_deinit(p->eazyai_postp_ctx);
    p->eazyai_postp_ctx = NULL;
    yolo_seg_detach_det_stash(p);
  } else {
    yolo_seg_free_det_stash(p);
  }
  yolo_seg_free_ea_cfg_store(p);
  p->yolo_seg_cfg_sig = 0;
}

/* -------- YOLOX (needs class_agnostic) -------- */
static int eazyai_yolox_process(void *ctx)
{
  ml_postproc_ctx_t *pp_ctx = (ml_postproc_ctx_t *)ctx;
  mlpp_priv_ctx_t *priv = (mlpp_priv_ctx_t *)pp_ctx->user_data;
  void *ea_ctx = priv ? priv->eazyai_postp_ctx : NULL;
  int rval = -1, class_num, box_num;
  ea_postproc_tensor_t ea_tensor;

  if (!pp_ctx || pp_ctx->num_tensors < 1 || !pp_ctx->result)
    return -1;
  if (build_single_tensor(&ea_tensor, pp_ctx, &class_num, &box_num) < 0)
    return -1;

  /* ea_postp_yolox_init uses class_num = shape[EA_P_W] - 5 (4 bbox + 1 obj + classes). */
  class_num = pp_ctx->tensors[0].width - 5;
  if (class_num <= 0 || box_num <= 0)
    return -1;

  if (!ea_ctx && priv) {
    ea_postproc_detection_config_t config;
    ea_postproc_nn_input_info_t nn_info;
    build_single_det_config(&config, &nn_info, &ea_tensor, pp_ctx, class_num);
    ea_ctx = ea_postp_yolox_init(&config, 0); /* class_agnostic=0 */
    if (!ea_ctx)
      return -1;
    priv->eazyai_postp_ctx = ea_ctx;
  }
  if (!ea_ctx)
    return -1;
  if (!priv || mlpp_ensure_ea_det_bbox_buf(priv, pp_ctx->top_k) < 0)
    return -1;

  ea_postproc_input_t postp_input = { .p_tensor = &ea_tensor, .num = 1 };
  {
    ea_postproc_detection_bbox_t *out_boxes = (ea_postproc_detection_bbox_t *)priv->ea_det_bbox_buf;
    uint32_t valid_num = 0;
    rval = ea_postp_yolox(ea_ctx, &postp_input, (uint32_t)pp_ctx->top_k, out_boxes, &valid_num);
    if (rval == 0)
      copy_ea_bbox_to_result(out_boxes, valid_num, pp_ctx);
  }
  return (rval == 0) ? 0 : -1;
}

static void eazyai_yolox_deinit(void *priv)
{
  mlpp_priv_ctx_t *p = (mlpp_priv_ctx_t *)priv;
  if (!p || !p->eazyai_postp_ctx)
    return;
  ea_postp_yolox_deinit(p->eazyai_postp_ctx);
  p->eazyai_postp_ctx = NULL;
}

/* -------- YOLOv3/v5 via yolo_det_common (3 tensors + anchor) -------- */
static int eazyai_yolo_det_common_process_impl(void *ctx, int use_exp)
{
  ml_postproc_ctx_t *pp_ctx = (ml_postproc_ctx_t *)ctx;
  mlpp_priv_ctx_t *priv = (mlpp_priv_ctx_t *)pp_ctx->user_data;
  void *ea_ctx = priv ? priv->eazyai_postp_ctx : NULL;
  int rval = -1;
  const float *anchors = s_yolov5_anchors;

  if (!pp_ctx || pp_ctx->num_tensors < 3 || !pp_ctx->result)
    return -1;

  /* eazyai yolo_det_common has reorder_feature_map; input order not required. */
  ea_postproc_tensor_t ea_tensors[3];
  ea_postproc_tensor_t anchor_tensor;
  memset(ea_tensors, 0, sizeof(ea_tensors));
  memset(&anchor_tensor, 0, sizeof(anchor_tensor));
  anchor_tensor.p_buffer = (void *)anchors;
  anchor_tensor.shape[EA_P_N] = 1;
  anchor_tensor.shape[EA_P_C] = 1;
  anchor_tensor.shape[EA_P_H] = 1;
  anchor_tensor.shape[EA_P_W] = 18;
  anchor_tensor.data_format = EA_P_F32;

  int i;
  for (i = 0; i < 3; i++) {
    ea_tensors[i].p_buffer = pp_ctx->tensors[i].data;
    ea_tensors[i].shape[EA_P_N] = 1;
    ea_tensors[i].shape[EA_P_C] = pp_ctx->tensors[i].depth;
    ea_tensors[i].shape[EA_P_H] = pp_ctx->tensors[i].height;
    ea_tensors[i].shape[EA_P_W] = pp_ctx->tensors[i].width;
    ea_tensors[i].pitch = (uint32_t)pp_ctx->tensors[i].pitch;
    ea_tensors[i].data_format = EA_P_F32;
    ea_tensors[i].p_name = pp_ctx->tensors[i].name[0] ? pp_ctx->tensors[i].name : "output";
  }
  int class_num = pp_ctx->tensors[0].depth / 3 - 5;
  if (class_num <= 0)
    return -1;

  if (!ea_ctx && priv) {
    ea_postproc_detection_config_t config;
    ea_postproc_nn_input_info_t nn_info;
    memset(&config, 0, sizeof(config));
    memset(&nn_info, 0, sizeof(nn_info));
    config.postp_input.p_tensor = ea_tensors;
    config.postp_input.num = 3;
    config.p_nn_orig_input_info = &nn_info;
    config.nn_orig_input_info_num = 1;
    nn_info.shape[EA_P_W] = pp_ctx->nn_input_width > 0 ? pp_ctx->nn_input_width : 640;
    nn_info.shape[EA_P_H] = pp_ctx->nn_input_height > 0 ? pp_ctx->nn_input_height : 640;
    nn_info.p_in_port_name = "images";
    config.conf_threshold = pp_ctx->conf_threshold;
    config.nms_threshold = pp_ctx->nms_threshold;
    config.nms_topk = (uint32_t)pp_ctx->top_k;
    config.topk = (uint32_t)pp_ctx->top_k;
    config.class_num = (uint32_t)class_num;
    config.output_normalize = 1;
    config.log_level = 0;
    config.use_multi_cls = (uint32_t)pp_ctx->use_multi_cls;
    config.use_exp = (uint32_t)(use_exp ? 1 : 0);
    config.p_extra_data = &anchor_tensor;

    ea_ctx = ea_postp_yolo_det_common_init(&config);
    if (!ea_ctx)
      return -1;
    priv->eazyai_postp_ctx = ea_ctx;
  }
  if (!ea_ctx)
    return -1;
  if (!priv || mlpp_ensure_ea_det_bbox_buf(priv, pp_ctx->top_k) < 0)
    return -1;

  ea_postproc_input_t postp_input;
  postp_input.p_tensor = ea_tensors;
  postp_input.num = 3;
  {
    ea_postproc_detection_bbox_t *out_boxes = (ea_postproc_detection_bbox_t *)priv->ea_det_bbox_buf;
    uint32_t valid_num = 0;
    rval = ea_postp_yolo_det_common(ea_ctx, &postp_input, (uint32_t)pp_ctx->top_k, out_boxes, &valid_num);
    if (rval == 0)
      copy_ea_bbox_to_result(out_boxes, valid_num, pp_ctx);
  }
  return (rval == 0) ? 0 : -1;
}

static void eazyai_yolo_det_common_deinit(void *priv)
{
  mlpp_priv_ctx_t *p = (mlpp_priv_ctx_t *)priv;
  if (!p || !p->eazyai_postp_ctx)
    return;
  ea_postp_yolo_det_common_deinit(p->eazyai_postp_ctx);
  p->eazyai_postp_ctx = NULL;
}

/* Wrappers for ops interface (process takes void *ctx only) */
static int eazyai_yolo_det_common_process(void *ctx)
{
  return eazyai_yolo_det_common_process_impl(ctx, 0); /* YOLOv5: use_exp=0 */
}

static int eazyai_yolov3_process(void *ctx)
{
  return eazyai_yolo_det_common_process_impl(ctx, 1); /* YOLOv3: use_exp=1 */
}

/* -------- YOLOP (3 det tensors + 2 seg tensors, uses yolo_det_common + yolop_seg) -------- */
static int eazyai_yolop_process(void *ctx)
{
  ml_postproc_ctx_t *pp_ctx = (ml_postproc_ctx_t *)ctx;
  mlpp_priv_ctx_t *priv = (mlpp_priv_ctx_t *)pp_ctx->user_data;
  void *ea_ctx = priv ? priv->eazyai_postp_ctx : NULL;
  int rval = -1;

  /* YOLOP: 5 tensors — 3 det + 2 seg. Det: same as yolop_builtin (exclude seg by name, sort H*W asc). */
  if (!pp_ctx || !pp_ctx->result)
    return -1;

  if (pp_ctx->num_tensors >= 5) {
    int det_idx[3];
    int drive_idx = ml_find_tensor_by_name(pp_ctx, "drive_area_seg");
    int lane_idx = ml_find_tensor_by_name(pp_ctx, "lane_line_seg");
    if (drive_idx < 0 || lane_idx < 0) {
      /* Fallback: assume out_name order ...+drive_area_seg+lane_line_seg -> indices 3,4 */
      drive_idx = 3;
      lane_idx = 4;
    }
    if (mlpp_yolop_collect_det_tensor_indices(pp_ctx, det_idx) < 0)
      return -1;
    int i;

    ea_postproc_tensor_t ea_tensors[5];
    ea_postproc_tensor_t anchor_tensor;
    ea_postproc_input_map_t input_map[2];
    memset(ea_tensors, 0, sizeof(ea_tensors));
    memset(&anchor_tensor, 0, sizeof(anchor_tensor));
    anchor_tensor.p_buffer = (void *)s_yolop_anchors;
    anchor_tensor.shape[EA_P_N] = 1;
    anchor_tensor.shape[EA_P_C] = 1;
    anchor_tensor.shape[EA_P_H] = 1;
    anchor_tensor.shape[EA_P_W] = 18;
    anchor_tensor.data_format = EA_P_F32;

    for (i = 0; i < 3; i++) {
      const ml_postproc_tensor_desc_t *td = &pp_ctx->tensors[det_idx[i]];
      ea_tensors[i].p_buffer = td->data;
      ea_tensors[i].shape[EA_P_N] = 1;
      ea_tensors[i].shape[EA_P_C] = td->depth;
      ea_tensors[i].shape[EA_P_H] = td->height;
      ea_tensors[i].shape[EA_P_W] = td->width;
      ea_tensors[i].pitch = (uint32_t)td->pitch;
      ea_tensors[i].data_format = EA_P_F32;
      ea_tensors[i].p_name = td->name[0] ? td->name : "output";
    }
    int seg_h = pp_ctx->tensors[drive_idx].height, seg_w = pp_ctx->tensors[drive_idx].width;
    ea_tensors[3].p_buffer = pp_ctx->tensors[drive_idx].data;
    ea_tensors[3].shape[EA_P_N] = 1;
    ea_tensors[3].shape[EA_P_C] = pp_ctx->tensors[drive_idx].depth;
    ea_tensors[3].shape[EA_P_H] = pp_ctx->tensors[drive_idx].height;
    ea_tensors[3].shape[EA_P_W] = pp_ctx->tensors[drive_idx].width;
    ea_tensors[3].pitch = (uint32_t)pp_ctx->tensors[drive_idx].pitch;
    ea_tensors[3].data_format = EA_P_F32;
    ea_tensors[3].p_name = pp_ctx->tensors[drive_idx].name[0] ? pp_ctx->tensors[drive_idx].name : "drive_area";
    ea_tensors[4].p_buffer = pp_ctx->tensors[lane_idx].data;
    ea_tensors[4].shape[EA_P_N] = 1;
    ea_tensors[4].shape[EA_P_C] = pp_ctx->tensors[lane_idx].depth;
    ea_tensors[4].shape[EA_P_H] = pp_ctx->tensors[lane_idx].height;
    ea_tensors[4].shape[EA_P_W] = pp_ctx->tensors[lane_idx].width;
    ea_tensors[4].pitch = (uint32_t)pp_ctx->tensors[lane_idx].pitch;
    ea_tensors[4].data_format = EA_P_F32;
    ea_tensors[4].p_name = pp_ctx->tensors[lane_idx].name[0] ? pp_ctx->tensors[lane_idx].name : "lane_line";

    input_map[0].type = EA_P_YOLOP_SEG_INPUT_DRIVE_AREA;
    input_map[0].index = 0;
    input_map[1].type = EA_P_YOLOP_SEG_INPUT_LINE;
    input_map[1].index = 1;

    int class_num = pp_ctx->tensors[det_idx[0]].depth / 3 - 5;
    if (class_num <= 0)
      class_num = 1; /* YOLOP often 1 class (car) */

    if (!ea_ctx && priv) {
      ea_postproc_detection_config_t config;
      ea_postproc_nn_input_info_t nn_info;
      memset(&config, 0, sizeof(config));
      memset(&nn_info, 0, sizeof(nn_info));
      config.postp_input.p_tensor = ea_tensors;
      config.postp_input.num = 5;
      config.p_nn_orig_input_info = &nn_info;
      config.nn_orig_input_info_num = 1;
      nn_info.shape[EA_P_W] = pp_ctx->nn_input_width > 0 ? pp_ctx->nn_input_width : 640;
      nn_info.shape[EA_P_H] = pp_ctx->nn_input_height > 0 ? pp_ctx->nn_input_height : 640;
      nn_info.p_in_port_name = "images";
      config.conf_threshold = pp_ctx->conf_threshold;
      config.nms_threshold = pp_ctx->nms_threshold;
      config.topk = (uint32_t)pp_ctx->top_k;
      config.class_num = (uint32_t)class_num;
      config.output_normalize = 1;
      config.p_extra_data = &anchor_tensor;

      /* Use yolop_seg init - it only needs the 2 seg tensors in config */
      ea_postproc_input_t seg_input;
      seg_input.p_tensor = &ea_tensors[3];
      seg_input.num = 2;
      config.postp_input = seg_input;

      ea_ctx = ea_postp_yolop_seg_init(&config, input_map);
      if (!ea_ctx)
        return -1;
      priv->eazyai_postp_ctx = ea_ctx;
    }
    if (!ea_ctx)
      return -1;

    /* Run detection via yolo_det_common for tensors 0,1,2. Cache det_ctx to avoid per-frame init. */
    void *det_ctx = priv ? priv->eazyai_postp_ctx_det : NULL;
    if (!det_ctx && priv) {
      ea_postproc_input_t det_input_init = { .p_tensor = ea_tensors, .num = 3 };
      ea_postproc_detection_config_t det_cfg;
      ea_postproc_nn_input_info_t det_nn_info;
      memset(&det_cfg, 0, sizeof(det_cfg));
      memset(&det_nn_info, 0, sizeof(det_nn_info));
      det_cfg.postp_input = det_input_init;
      det_cfg.p_nn_orig_input_info = &det_nn_info;
      det_cfg.nn_orig_input_info_num = 1;
      det_nn_info.shape[EA_P_W] = pp_ctx->nn_input_width > 0 ? pp_ctx->nn_input_width : 640;
      det_nn_info.shape[EA_P_H] = pp_ctx->nn_input_height > 0 ? pp_ctx->nn_input_height : 640;
      det_nn_info.p_in_port_name = "images";
      det_cfg.conf_threshold = pp_ctx->conf_threshold;
      det_cfg.nms_threshold = pp_ctx->nms_threshold;
      det_cfg.nms_topk = (uint32_t)pp_ctx->top_k;
      det_cfg.topk = (uint32_t)pp_ctx->top_k;
      det_cfg.class_num = (uint32_t)class_num;
      det_cfg.output_normalize = 1;
      det_cfg.p_extra_data = &anchor_tensor;
      det_ctx = ea_postp_yolo_det_common_init(&det_cfg);
      if (det_ctx)
        priv->eazyai_postp_ctx_det = det_ctx;
    }
    if (!det_ctx) {
      pp_ctx->result->det_num = 0;
    } else if (!priv || mlpp_ensure_ea_det_bbox_buf(priv, pp_ctx->top_k) < 0) {
      pp_ctx->result->det_num = 0;
    } else {
      ea_postproc_input_t det_input = { .p_tensor = ea_tensors, .num = 3 };
      ea_postproc_detection_bbox_t *out_boxes = (ea_postproc_detection_bbox_t *)priv->ea_det_bbox_buf;
      uint32_t valid_num = 0;
      rval = ea_postp_yolo_det_common(det_ctx, &det_input, (uint32_t)pp_ctx->top_k, out_boxes, &valid_num);
      if (rval == 0)
        copy_ea_bbox_to_result(out_boxes, valid_num, pp_ctx);
    }

    /* Run yolop_seg for drive_area + lane_line */
    ea_postproc_input_t seg_postp_input;
    seg_postp_input.p_tensor = &ea_tensors[3];
    seg_postp_input.num = 2;
    ea_postproc_segmentation_map_t out_drive = {0}, out_lane = {0};
    uint8_t *drive_buf = NULL, *lane_buf = NULL;
    size_t seg_size = (size_t)seg_h * seg_w;

    drive_buf = (uint8_t *)malloc(seg_size);
    lane_buf = (uint8_t *)malloc(seg_size);
    if (drive_buf && lane_buf) {
      out_drive.map.p_buffer = drive_buf;
      out_drive.map.shape[EA_P_N] = 1;
      out_drive.map.shape[EA_P_C] = 1;
      out_drive.map.shape[EA_P_H] = seg_h;
      out_drive.map.shape[EA_P_W] = seg_w;
      out_drive.map.pitch = (uint32_t)seg_w;
      out_drive.map.data_format = EA_P_U8;
      out_lane.map = out_drive.map;
      out_lane.map.p_buffer = lane_buf;

      rval = ea_postp_yolop_seg(ea_ctx, &seg_postp_input, &out_drive, &out_lane);
      if (rval == 0) {
        pp_ctx->seg_outputs[0].mask = drive_buf;
        pp_ctx->seg_outputs[0].width = seg_w;
        pp_ctx->seg_outputs[0].height = seg_h;
        pp_ctx->seg_outputs[1].mask = lane_buf;
        pp_ctx->seg_outputs[1].width = seg_w;
        pp_ctx->seg_outputs[1].height = seg_h;
        drive_buf = NULL;
        lane_buf = NULL;
      }
    }
    free(drive_buf);
    free(lane_buf);
    return 0;
  }

  /* 3-tensor YOLOP (det only, no seg) - use yolo_det_common */
  return eazyai_yolo_det_common_process(ctx);
}

static void eazyai_yolop_deinit(void *priv)
{
  mlpp_priv_ctx_t *p = (mlpp_priv_ctx_t *)priv;
  if (!p)
    return;
  if (p->eazyai_postp_ctx) {
    ea_postp_yolop_seg_deinit(p->eazyai_postp_ctx);
    p->eazyai_postp_ctx = NULL;
  }
  if (p->eazyai_postp_ctx_det) {
    ea_postp_yolo_det_common_deinit(p->eazyai_postp_ctx_det);
    p->eazyai_postp_ctx_det = NULL;
  }
}

/* -------- CenterNet (4 tensors: hm, hmax, wh, reg) -------- */
static int eazyai_centernet_process(void *ctx)
{
  ml_postproc_ctx_t *pp_ctx = (ml_postproc_ctx_t *)ctx;
  mlpp_priv_ctx_t *priv = (mlpp_priv_ctx_t *)pp_ctx->user_data;
  void *ea_ctx = priv ? priv->eazyai_postp_ctx : NULL;
  int rval = -1, i;

  if (!pp_ctx || pp_ctx->num_tensors < 4 || !pp_ctx->result)
    return -1;

  /* Index-based: hm[0], hm_max[1], wh[2], reg[3]. User must set out_name order. */
  ea_postproc_tensor_t ea_tensors[4];
  memset(ea_tensors, 0, sizeof(ea_tensors));
  for (i = 0; i < 4; i++) {
    int ti = i;
    ea_tensors[i].p_buffer = pp_ctx->tensors[ti].data;
    ea_tensors[i].shape[EA_P_N] = 1;
    ea_tensors[i].shape[EA_P_C] = pp_ctx->tensors[ti].depth;
    ea_tensors[i].shape[EA_P_H] = pp_ctx->tensors[ti].height;
    ea_tensors[i].shape[EA_P_W] = pp_ctx->tensors[ti].width;
    ea_tensors[i].pitch = (uint32_t)pp_ctx->tensors[ti].pitch;
    ea_tensors[i].data_format = EA_P_F32;
    ea_tensors[i].p_name = pp_ctx->tensors[ti].name[0] ? pp_ctx->tensors[ti].name : "output";
  }

  if (!ea_ctx && priv) {
    ea_postproc_detection_config_t config;
    ea_postproc_nn_input_info_t nn_info;
    memset(&config, 0, sizeof(config));
    memset(&nn_info, 0, sizeof(nn_info));
    config.postp_input.p_tensor = ea_tensors;
    config.postp_input.num = 4;
    config.p_nn_orig_input_info = &nn_info;
    config.nn_orig_input_info_num = 1;
    nn_info.shape[EA_P_W] = pp_ctx->nn_input_width > 0 ? pp_ctx->nn_input_width : 512;
    nn_info.shape[EA_P_H] = pp_ctx->nn_input_height > 0 ? pp_ctx->nn_input_height : 512;
    nn_info.p_in_port_name = "images";
    config.conf_threshold = pp_ctx->conf_threshold;
    config.nms_threshold = pp_ctx->nms_threshold;
    config.nms_topk = (uint32_t)pp_ctx->top_k;
    config.topk = (uint32_t)pp_ctx->top_k;
    config.class_num = pp_ctx->tensors[0].depth > 0 ? (uint32_t)pp_ctx->tensors[0].depth : 80;
    config.output_normalize = 1;
    ea_ctx = ea_postp_centernet_init(&config);
    if (!ea_ctx)
      return -1;
    priv->eazyai_postp_ctx = ea_ctx;
  }
  if (!ea_ctx)
    return -1;
  if (!priv || mlpp_ensure_ea_det_bbox_buf(priv, pp_ctx->top_k) < 0)
    return -1;

  ea_postproc_input_t postp_input = { .p_tensor = ea_tensors, .num = 4 };
  {
    ea_postproc_detection_bbox_t *out_boxes = (ea_postproc_detection_bbox_t *)priv->ea_det_bbox_buf;
    uint32_t valid_num = 0;
    rval = ea_postp_centernet(ea_ctx, &postp_input, (uint32_t)pp_ctx->top_k, out_boxes, &valid_num);
    if (rval == 0)
      copy_ea_bbox_to_result(out_boxes, valid_num, pp_ctx);
  }
  return (rval == 0) ? 0 : -1;
}

static void eazyai_centernet_deinit(void *priv)
{
  mlpp_priv_ctx_t *p = (mlpp_priv_ctx_t *)priv;
  if (!p || !p->eazyai_postp_ctx)
    return;
  ea_postp_centernet_deinit(p->eazyai_postp_ctx);
  p->eazyai_postp_ctx = NULL;
}

/* ea_postp_retinaface_init matches nn_arm tensor names; strip _reshape (common in ONNX). */
static void retinaface_normalize_name(const char *src, char *dst, size_t dstsz)
{
  if (!dst || dstsz == 0)
    return;
  if (!src || !src[0]) {
    dst[0] = '\0';
    return;
  }
  (void)snprintf(dst, dstsz, "%s", src);
  for (;;) {
    char *p = strstr(dst, "_reshape");
    if (!p)
      break;
    memmove(p, p + 8, strlen(p + 8) + 1);
  }
}

static int retinaface_stride_from_name(const char *name)
{
  if (!name)
    return 0;
  if (strstr(name, "stride32"))
    return 32;
  if (strstr(name, "stride16"))
    return 16;
  if (strstr(name, "stride8"))
    return 8;
  return 0;
}

/* 0=cls, 1=bbox, 2=landmark — match ea_postproc_retinaface slot order per scale. */
static int retinaface_kind_from_name(const char *name)
{
  if (!name)
    return -1;
  if (strstr(name, "landmark") || strstr(name, "landm"))
    return 2;
  if (strstr(name, "bbox"))
    return 1;
  if (strstr(name, "cls") || strstr(name, "prob"))
    return 0;
  return -1;
}

/* Match s_retinaface_anchors[0..2]: [0] stride 32, [1] stride 16, [2] stride 8. */
static int retinaface_scale_to_idx(int stride)
{
  if (stride == 32)
    return 0;
  if (stride == 16)
    return 1;
  if (stride == 8)
    return 2;
  return -1;
}

/*
 * mlinference2 cap dimensions are W:H:C = dim.width:dim.height:dim.depth = NCHW (W,H,C).
 * Some exports list (C,H,W) in the same three numbers; then first≠second, second=third, first≤32
 * and first looks like a channel count (2/4/8/20…): interpret as (C,H,W) = (a,b,c3).
 * Do NOT override landmark C: buffers are still C channels; faking C=20 while data is 4/8ch breaks
 * ea_postp_retinaface (e.g. output[j].point_num == 5).
 */
static void retinaface_set_ea_shape_nchw(const ml_postproc_tensor_desc_t *t, ea_postproc_tensor_t *ea)
{
  int a, b, c3, c, h, w;

  if (!t || !ea)
    return;
  a = t->width;
  b = t->height;
  c3 = t->depth;

  if (a > 0 && a <= 32 && a != b && b == c3 && b >= 8) {
    c = a;
    h = b;
    w = c3;
  } else {
    c = c3;
    h = b;
    w = a;
  }
  ea->shape[EA_P_N] = 1;
  ea->shape[EA_P_C] = c;
  ea->shape[EA_P_H] = h;
  ea->shape[EA_P_W] = w;
}

/*
 * ea_postp_retinaface asserts cls/bbox/landmark inputs are EA_P_F32 (packed f16->f32 in gstmlpostprocess).
 */
static void retinaface_apply_ea_buffer(const ml_postproc_tensor_desc_t *t, ea_postproc_tensor_t *ea)
{
  ea->p_buffer = t->data;
  ea->pitch = (uint32_t)t->pitch;
  ea->data_format = EA_P_F32;
}

/* Build [cls32,bbox32,lm32, cls16,bbox16,lm16, cls8,bbox8,lm8] for ea_postp_retinaface. */
static int retinaface_fill_ordered_tensors(ml_postproc_ctx_t *pp_ctx, int n,
    ea_postproc_tensor_t *ea_out, char name_buf[9][ML_POSTPROC_TENSOR_NAME_LEN])
{
  int src_for_slot[9];
  int i, slot;

  if (n < 9)
    return -1;
  for (slot = 0; slot < 9; slot++) {
    src_for_slot[slot] = -1;
  }
  for (i = 0; i < n; i++) {
    char norm[ML_POSTPROC_TENSOR_NAME_LEN];
    int stride, kind, six;

    retinaface_normalize_name(pp_ctx->tensors[i].name, norm, sizeof(norm));
    stride = retinaface_stride_from_name(norm);
    kind = retinaface_kind_from_name(norm);
    six = retinaface_scale_to_idx(stride);
    if (six < 0 || kind < 0)
      return -1;
    slot = six * 3 + kind;
    if (src_for_slot[slot] >= 0)
      return -1;
    src_for_slot[slot] = i;
  }
  for (slot = 0; slot < 9; slot++) {
    if (src_for_slot[slot] < 0)
      return -1;
  }
  for (slot = 0; slot < 9; slot++) {
    int ti = src_for_slot[slot];
    ml_postproc_tensor_desc_t *t = &pp_ctx->tensors[ti];

    retinaface_set_ea_shape_nchw(t, &ea_out[slot]);
    retinaface_apply_ea_buffer(t, &ea_out[slot]);
    retinaface_normalize_name(t->name, name_buf[slot], ML_POSTPROC_TENSOR_NAME_LEN);
    ea_out[slot].p_name = name_buf[slot][0] ? name_buf[slot] : "output";
  }
  return 0;
}

/* -------- RetinaFace (multi-tensor, typically 3 scales) -------- */
static int eazyai_retinaface_process(void *ctx)
{
  ml_postproc_ctx_t *pp_ctx = (ml_postproc_ctx_t *)ctx;
  mlpp_priv_ctx_t *priv = (mlpp_priv_ctx_t *)pp_ctx->user_data;
  void *ea_ctx = priv ? priv->eazyai_postp_ctx : NULL;
  int rval = -1, n = pp_ctx->num_tensors;
  ea_postproc_tensor_t ea_tensors[AMBA_ML_MAX_TENSORS];
  char rf_names[9][ML_POSTPROC_TENSOR_NAME_LEN];
  char rf_fb_names[AMBA_ML_MAX_TENSORS][ML_POSTPROC_TENSOR_NAME_LEN];

  if (!pp_ctx || n < 1 || !pp_ctx->result)
    return -1;
  if (n > AMBA_ML_MAX_TENSORS)
    n = AMBA_ML_MAX_TENSORS;
  if (n < 9)
    return -1;

  memset(ea_tensors, 0, sizeof(ea_tensors));
  if (retinaface_fill_ordered_tensors(pp_ctx, n, ea_tensors, rf_names) == 0) {
    n = 9;
  } else {
    int i;
    /* Fallback: caps order matches ONNX (9 tensors); names normalized for ea_postproc. */
    if (n != 9)
      return -1;
    for (i = 0; i < n; i++) {
      retinaface_set_ea_shape_nchw(&pp_ctx->tensors[i], &ea_tensors[i]);
      retinaface_apply_ea_buffer(&pp_ctx->tensors[i], &ea_tensors[i]);
      retinaface_normalize_name(pp_ctx->tensors[i].name, rf_fb_names[i],
          ML_POSTPROC_TENSOR_NAME_LEN);
      ea_tensors[i].p_name = rf_fb_names[i][0] ? rf_fb_names[i] :
          (pp_ctx->tensors[i].name[0] ? pp_ctx->tensors[i].name : "output");
    }
  }

  if (!ea_ctx && priv) {
    ea_postproc_tensor_t anchor_tensor;
    ea_postproc_detection_config_t config;
    ea_postproc_nn_input_info_t nn_info;
    memset(&config, 0, sizeof(config));
    memset(&nn_info, 0, sizeof(nn_info));
    memset(&anchor_tensor, 0, sizeof(anchor_tensor));
    anchor_tensor.p_buffer = (void *)s_retinaface_anchors;
    anchor_tensor.data_format = EA_P_F32;
    anchor_tensor.shape[EA_P_N] = 1;
    anchor_tensor.shape[EA_P_C] = 3;
    anchor_tensor.shape[EA_P_H] = 2;
    anchor_tensor.shape[EA_P_W] = 4;
    config.postp_input.p_tensor = ea_tensors;
    config.postp_input.num = (uint32_t)n;
    config.p_nn_orig_input_info = &nn_info;
    config.nn_orig_input_info_num = 1;
    config.p_extra_data = &anchor_tensor;
    nn_info.shape[EA_P_W] = pp_ctx->nn_input_width > 0 ? pp_ctx->nn_input_width : 640;
    nn_info.shape[EA_P_H] = pp_ctx->nn_input_height > 0 ? pp_ctx->nn_input_height : 640;
    nn_info.p_in_port_name = "images";
    config.conf_threshold = pp_ctx->conf_threshold;
    config.nms_threshold = pp_ctx->nms_threshold;
    config.nms_topk = (uint32_t)pp_ctx->top_k;
    config.topk = (uint32_t)pp_ctx->top_k;
    config.class_num = 1;
    config.output_normalize = 1;
    ea_ctx = ea_postp_retinaface_init(&config);
    if (!ea_ctx)
      return -1;
    priv->eazyai_postp_ctx = ea_ctx;
  }
  if (!ea_ctx)
    return -1;
  if (!priv || mlpp_ensure_ea_det_bbox_buf(priv, pp_ctx->top_k) < 0)
    return -1;

  ea_postproc_input_t postp_input = { .p_tensor = ea_tensors, .num = (uint32_t)n };
  {
    ea_postproc_detection_bbox_t *out_boxes = (ea_postproc_detection_bbox_t *)priv->ea_det_bbox_buf;
    uint32_t valid_num = 0;
    rval = ea_postp_retinaface(ea_ctx, &postp_input, (uint32_t)pp_ctx->top_k, out_boxes, &valid_num);
    if (rval == 0)
      copy_ea_bbox_to_result(out_boxes, valid_num, pp_ctx);
  }
  return (rval == 0) ? 0 : -1;
}

static void eazyai_retinaface_deinit(void *priv)
{
  mlpp_priv_ctx_t *p = (mlpp_priv_ctx_t *)priv;
  if (!p || !p->eazyai_postp_ctx)
    return;
  ea_postp_retinaface_deinit(p->eazyai_postp_ctx);
  p->eazyai_postp_ctx = NULL;
}

/* ---- FGFD ONNX/flat: [scores] + [boxes]. Skips ea_postp_fgfd_init when shapes (e.g. [1,1,1,36]) do not match.
 * Score: sigmoid(s1-s0) with a minimum logit margin.
 * Box: always xyxy (top-left + bottom-right, [0,1]); on failure, fall back to center+size (cx,cy,w,h) as +/- half extents. */

static int ml_tensor_elem_count(const ml_postproc_tensor_desc_t *t)
{
  int w = t->width, h = t->height, d = t->depth;
  if (w < 1) w = 1;
  if (h < 1) h = 1;
  if (d < 1) d = 1;
  return w * h * d;
}

static float ml_tensor_f32_at_whn1(const ml_postproc_tensor_desc_t *t, int linear)
{
  int w = t->width, h = t->height;
  if (w < 1) w = 1;
  if (h < 1) h = 1;
  if (linear < 0)
    return 0.f;
  {
    int r = linear / w, c = linear % w;
    if (r < 0 || r >= h || c < 0 || c >= w)
      return 0.f;
    return *(const float *) ((const uint8_t *) t->data + (size_t)r * t->pitch
        + (size_t)c * sizeof(float));
  }
}

typedef struct {
  float x0, y0, x1, y1, s;
} fgfd_cand_t;

static int fgfd_cand_sort_desc(const void *a, const void *b)
{
  const fgfd_cand_t *p = (const fgfd_cand_t *)a;
  const fgfd_cand_t *q = (const fgfd_cand_t *)b;
  if (p->s < q->s)
    return 1;
  if (p->s > q->s)
    return -1;
  return 0;
}

static float fgfd_iou_01(const fgfd_cand_t *a, const fgfd_cand_t *b)
{
  float x0, y0, x1, y1, iw, ih, in_, u;
  x0 = a->x0 > b->x0 ? a->x0 : b->x0;
  y0 = a->y0 > b->y0 ? a->y0 : b->y0;
  x1 = a->x1 < b->x1 ? a->x1 : b->x1;
  y1 = a->y1 < b->y1 ? a->y1 : b->y1;
  iw = (x1 - x0) > 0.f ? (x1 - x0) : 0.f;
  ih = (y1 - y0) > 0.f ? (y1 - y0) : 0.f;
  in_ = iw * ih;
  u = (a->x1 - a->x0) * (a->y1 - a->y0) + (b->x1 - b->x0) * (b->y1 - b->y0) - in_;
  if (u <= 1e-8f)
    return 0.f;
  return in_ / u;
}

static int fgfd_native_onnx2_decode(ml_postproc_ctx_t *pp_ctx, int is, int ib)
{
  int ts = ml_tensor_elem_count(&pp_ctx->tensors[is]);
  int tb = ml_tensor_elem_count(&pp_ctx->tensors[ib]);
  int n, nnw, nnh, k, a, b, mkept, nms_cap, kept_i;
  fgfd_cand_t *cand = NULL, *kept;
  int nc, nk;
  bounding_boxes_t *result = pp_ctx->result;
  if (pp_ctx->result == NULL)
    return 1;
  if (ts < 256)
    return 1;
  if (tb != 2 * ts || (ts % 2) != 0)
    return 1;
  if ((tb % 4) != 0)
    return 1;
  n = ts / 2;
  if (4 * n != tb)
    return 1;
  cand = (fgfd_cand_t *)malloc((size_t)n * sizeof(fgfd_cand_t));
  if (!cand)
    return -1;
  nc = 0;
  nnw = pp_ctx->nn_input_width > 0 ? pp_ctx->nn_input_width : 320;
  nnh = pp_ctx->nn_input_height > 0 ? pp_ctx->nn_input_height : 240;
  for (k = 0; k < n; k++) {
    float s0, s1, margin, sc, v0, v1, v2, v3, x0, y0, x1, y1, ar;
    s0 = ml_tensor_f32_at_whn1(&pp_ctx->tensors[is], 2 * k);
    s1 = ml_tensor_f32_at_whn1(&pp_ctx->tensors[is], 2 * k + 1);
    margin = s1 - s0;
    /* Binary log-odds; avoids all scores ~0.5 when logits are too close (softmax). */
    sc = 1.0f / (1.0f + expf(-fminf(fmaxf(margin, -12.f), 12.f)));
    if (sc < pp_ctx->conf_threshold)
      continue;
    if (margin < 0.25f)
      continue;
    v0 = ml_tensor_f32_at_whn1(&pp_ctx->tensors[ib], 4 * k);
    v1 = ml_tensor_f32_at_whn1(&pp_ctx->tensors[ib], 4 * k + 1);
    v2 = ml_tensor_f32_at_whn1(&pp_ctx->tensors[ib], 4 * k + 2);
    v3 = ml_tensor_f32_at_whn1(&pp_ctx->tensors[ib], 4 * k + 3);
    {
      float n0, n1, n2, n3, cx, cyy, bww, bhh;
      n0 = v0; n1 = v1; n2 = v2; n3 = v3;
      if (n0 > 1.5f || n1 > 1.5f || n2 > 1.5f || n3 > 1.5f) {
        n0 /= (float)nnw;
        n1 /= (float)nnh;
        n2 /= (float)nnw;
        n3 /= (float)nnh;
      }
      x0 = fmaxf(0.f, fminf(1.f, n0));
      y0 = fmaxf(0.f, fminf(1.f, n1));
      x1 = fmaxf(0.f, fminf(1.f, n2));
      y1 = fmaxf(0.f, fminf(1.f, n3));
      if (x0 > x1) { float t = x0; x0 = x1; x1 = t; }
      if (y0 > y1) { float t = y0; y0 = y1; y1 = t; }
      ar = (x1 - x0) * (y1 - y0);
      if (n2 < 0.f || n3 < 0.f || ar < 1e-5f || ar > 0.7f) {
        cx = n0; cyy = n1; bww = n2; bhh = n3;
        x0 = fmaxf(0.f, fminf(1.f, cx - bww * 0.5f));
        y0 = fmaxf(0.f, fminf(1.f, cyy - bhh * 0.5f));
        x1 = fmaxf(0.f, fminf(1.f, cx + bww * 0.5f));
        y1 = fmaxf(0.f, fminf(1.f, cyy + bhh * 0.5f));
        if (x1 <= x0 || y1 <= y0)
          continue;
        ar = (x1 - x0) * (y1 - y0);
      }
    }
    if (x1 <= x0 || y1 <= y0)
      continue;
    if (ar < 1e-5f || ar > 0.6f)
      continue;
    cand[nc].x0 = x0;
    cand[nc].y0 = y0;
    cand[nc].x1 = x1;
    cand[nc].y1 = y1;
    cand[nc].s = sc;
    nc++;
  }
  qsort(cand, (size_t)nc, sizeof(fgfd_cand_t), fgfd_cand_sort_desc);
  mkept = pp_ctx->top_k > 0 ? pp_ctx->top_k : 100;
  nms_cap = mkept * 4;
  if (nms_cap < 200)
    nms_cap = 200;
  if (nms_cap > 600)
    nms_cap = 600;
  if (nc > nms_cap)
    nc = nms_cap;
  kept = (fgfd_cand_t *)malloc((size_t)nc * sizeof(fgfd_cand_t));
  if (!kept) {
    free(cand);
    return -1;
  }
  nk = 0;
  for (a = 0; a < nc; a++) {
    int supp = 0;
    for (b = 0; b < nk; b++) {
      if (fgfd_iou_01(&cand[a], &kept[b]) > pp_ctx->nms_threshold) {
        supp = 1;
        break;
      }
    }
    if (supp)
      continue;
    kept[nk++] = cand[a];
    if (nk >= mkept)
      break;
  }
  free(cand);
  result->det_num = 0;
  for (kept_i = 0; kept_i < nk && (unsigned)result->det_num < D_MAX_DET_NUM; kept_i++) {
    det_object_t *d = &result->detections[result->det_num];
    d->id = 0;
    d->score = kept[kept_i].s;
    d->x_start = kept[kept_i].x0;
    d->y_start = kept[kept_i].y0;
    d->x_end = kept[kept_i].x1;
    d->y_end = kept[kept_i].y1;
    if (pp_ctx->valid_label_count > 0 && pp_ctx->labels[0][0]) {
      (void)snprintf(d->label, (size_t)DMAX_LABEL_LEN, "%s", pp_ctx->labels[0]);
    } else
      d->label[0] = '\0';
    result->det_num++;
  }
  free(kept);
  return 0;
}

/* -------- FGFD (face detection, 2 tensors bbox+score, ref nn_arm_task/face/fd_blur/fgfd) -------- */
static int eazyai_fgfd_process(void *ctx)
{
  ml_postproc_ctx_t *pp_ctx = (ml_postproc_ctx_t *)ctx;
  mlpp_priv_ctx_t *priv = (mlpp_priv_ctx_t *)pp_ctx->user_data;
  void *ea_ctx = priv ? priv->eazyai_postp_ctx : NULL;
  int rval = -1, i, n = pp_ctx->num_tensors;
  int is, ib, nat;

  if (!pp_ctx || n < 2 || !pp_ctx->result)
    return -1;
  if (n > AMBA_ML_MAX_TENSORS)
    n = AMBA_ML_MAX_TENSORS;
  is = ml_find_tensor_by_name(pp_ctx, "scores");
  if (is < 0)
    is = 0;
  ib = ml_find_tensor_by_name(pp_ctx, "boxes");
  if (ib < 0)
    ib = 1;
  if (is >= 0 && is < n && ib >= 0 && ib < n && is != ib) {
    nat = fgfd_native_onnx2_decode(pp_ctx, is, ib);
    if (nat == 0)
      return 0;
    if (nat < 0)
      return -1;
  }

  /* eazyai FGFD uses shape-based lookup; input order not required. */
  ea_postproc_tensor_t ea_tensors[AMBA_ML_MAX_TENSORS];
  memset(ea_tensors, 0, sizeof(ea_tensors));
  for (i = 0; i < n; i++) {
    ea_tensors[i].p_buffer = pp_ctx->tensors[i].data;
    ea_tensors[i].shape[EA_P_N] = 1;
    ea_tensors[i].shape[EA_P_C] = pp_ctx->tensors[i].depth;
    ea_tensors[i].shape[EA_P_H] = pp_ctx->tensors[i].height;
    ea_tensors[i].shape[EA_P_W] = pp_ctx->tensors[i].width;
    ea_tensors[i].pitch = (uint32_t)pp_ctx->tensors[i].pitch;
    ea_tensors[i].data_format = EA_P_F32;
    ea_tensors[i].p_name = pp_ctx->tensors[i].name[0] ? pp_ctx->tensors[i].name : "output";
  }

  if (!ea_ctx && priv) {
    ea_postproc_detection_config_t config;
    ea_postproc_nn_input_info_t nn_info;
    memset(&config, 0, sizeof(config));
    memset(&nn_info, 0, sizeof(nn_info));
    config.postp_input.p_tensor = ea_tensors;
    config.postp_input.num = (uint32_t)n;
    config.p_nn_orig_input_info = &nn_info;
    config.nn_orig_input_info_num = 1;
    nn_info.shape[EA_P_W] = pp_ctx->nn_input_width > 0 ? pp_ctx->nn_input_width : 640;
    nn_info.shape[EA_P_H] = pp_ctx->nn_input_height > 0 ? pp_ctx->nn_input_height : 640;
    nn_info.p_in_port_name = "images";
    config.conf_threshold = pp_ctx->conf_threshold;
    config.nms_threshold = pp_ctx->nms_threshold;
    config.nms_topk = (uint32_t)pp_ctx->top_k;
    config.topk = (uint32_t)pp_ctx->top_k;
    config.class_num = 1;
    config.output_normalize = 1;
    ea_ctx = ea_postp_fgfd_init(&config, 9);
    if (!ea_ctx)
      return -1;
    priv->eazyai_postp_ctx = ea_ctx;
  }
  if (!ea_ctx)
    return -1;
  if (!priv || mlpp_ensure_ea_det_bbox_buf(priv, pp_ctx->top_k) < 0)
    return -1;

  ea_postproc_input_t postp_input = { .p_tensor = ea_tensors, .num = (uint32_t)n };
  {
    ea_postproc_detection_bbox_t *out_boxes = (ea_postproc_detection_bbox_t *)priv->ea_det_bbox_buf;
    uint32_t valid_num = 0;
    rval = ea_postp_fgfd(ea_ctx, &postp_input, (uint32_t)pp_ctx->top_k, out_boxes, &valid_num);
    if (rval == 0)
      copy_ea_bbox_to_result(out_boxes, valid_num, pp_ctx);
  }
  return (rval == 0) ? 0 : -1;
}

static void eazyai_fgfd_deinit(void *priv)
{
  mlpp_priv_ctx_t *p = (mlpp_priv_ctx_t *)priv;
  if (!p || !p->eazyai_postp_ctx)
    return;
  ea_postp_fgfd_deinit(p->eazyai_postp_ctx);
  p->eazyai_postp_ctx = NULL;
}

/* -------- Output pad specs -------- */
static const ml_postproc_output_pad_spec_t s_bbox_only_pads[] = {
  { .name = "bbox", .kind = ML_POSTPROC_PAD_BBOX },
};
static const ml_postproc_output_pad_spec_t s_yolop_pads[] = {
  { .name = "bbox", .kind = ML_POSTPROC_PAD_BBOX },
  { .name = "drive_area", .kind = ML_POSTPROC_PAD_VIDEO_GRAY8 },
  { .name = "lane_line", .kind = ML_POSTPROC_PAD_VIDEO_GRAY8 },
};

static const ml_postproc_output_pad_spec_t *get_bbox_only_pads(int *count)
{
  *count = 1;
  return s_bbox_only_pads;
}

static const ml_postproc_output_pad_spec_t *get_yolop_pads(int *count)
{
  *count = 3;
  return s_yolop_pads;
}

static const char *yolop_result_types(void)
{
  return "detection_bbox:drive_area_seg:lane_line_seg";
}

/* YOLOP output layout: bbox + drive_area + lane_line */
static const ml_postproc_output_layout_t s_yolop_layout = {
  .n_entries = 3,
  .entries = {
    { .type = AMBA_ML_RESULT_TYPE_DETECTION_BBOX, .seg_idx = -1 },
    { .type = AMBA_ML_RESULT_TYPE_SEGMENTATION, .seg_idx = 0 },
    { .type = AMBA_ML_RESULT_TYPE_SEGMENTATION, .seg_idx = 1 },
  }
};
static const ml_postproc_output_layout_t *get_yolop_layout(void)
{
  return &s_yolop_layout;
}

/* bbox + single merged SEGMENTATION (all instances composited into seg_outputs[0]) */
static const ml_postproc_output_layout_t s_yolo_inst_seg_layout = {
  .n_entries = 2,
  .entries = {
    { .type = AMBA_ML_RESULT_TYPE_DETECTION_BBOX, .seg_idx = -1 },
    { .type = AMBA_ML_RESULT_TYPE_SEGMENTATION, .seg_idx = 0 },
  }
};
static const ml_postproc_output_layout_t *get_yolo_inst_seg_layout(void)
{
  return &s_yolo_inst_seg_layout;
}

/* -------- Ops and registration -------- */
#define DEF_OPS(NAME, DESC, PROCESS, DEINIT, PADS, RESULT_TYPES) \
  DEF_OPS_(NAME, DESC, PROCESS, DEINIT, PADS, RESULT_TYPES, NULL)
#define DEF_OPS_LAYOUT(NAME, DESC, PROCESS, DEINIT, PADS, RESULT_TYPES, LAYOUT_FN) \
  DEF_OPS_(NAME, DESC, PROCESS, DEINIT, PADS, RESULT_TYPES, LAYOUT_FN)
#define DEF_OPS_(NAME, DESC, PROCESS, DEINIT, PADS, RESULT_TYPES, LAYOUT_FN) \
  static const ml_postproc_ops_t eazyai_##NAME##_ops = { \
    .name = #NAME, .description = DESC, .process = PROCESS, \
    .get_result_types = RESULT_TYPES, .get_output_pads = PADS, .get_output_layout = LAYOUT_FN, \
    .deinit_user_ctx = DEINIT, .output_coords_normalized = TRUE \
  }
DEF_OPS(yolov8_det, "YOLOv8 det (eazyai)", eazyai_yolov8_det_process, eazyai_yolov8_det_deinit, get_bbox_only_pads, NULL);
DEF_OPS(yolov10, "YOLOv10 (eazyai)", eazyai_yolov10_process, eazyai_yolov10_deinit, get_bbox_only_pads, NULL);
DEF_OPS(yolov6, "YOLOv6 (eazyai)", eazyai_yolov6_process, eazyai_yolov6_deinit, get_bbox_only_pads, NULL);
DEF_OPS(yolo11_det, "YOLO11 det (eazyai)", eazyai_yolo11_det_process, eazyai_yolo11_det_deinit, get_bbox_only_pads, NULL);
DEF_OPS(yolov12_det, "YOLOv12 det (eazyai)", eazyai_yolov12_det_process, eazyai_yolov12_det_deinit, get_bbox_only_pads, NULL);
DEF_OPS(yolo26_det, "YOLO26 det (eazyai)", eazyai_yolo26_det_process, eazyai_yolo26_det_deinit, get_bbox_only_pads, NULL);
DEF_OPS(yolox, "YOLOX (eazyai)", eazyai_yolox_process, eazyai_yolox_deinit, get_bbox_only_pads, NULL);
DEF_OPS(yolo_det_common, "YOLOv5 (eazyai)", eazyai_yolo_det_common_process, eazyai_yolo_det_common_deinit, get_bbox_only_pads, NULL);
DEF_OPS(yolov3, "YOLOv3 (eazyai)", eazyai_yolov3_process, eazyai_yolo_det_common_deinit, get_bbox_only_pads, NULL);
DEF_OPS_LAYOUT(yolop, "YOLOP (eazyai)", eazyai_yolop_process, eazyai_yolop_deinit, get_yolop_pads, yolop_result_types, get_yolop_layout);
DEF_OPS(rtmdet, "RTMDet (eazyai)", eazyai_rtmdet_process, eazyai_rtmdet_deinit, get_bbox_only_pads, NULL);
DEF_OPS(lffd, "LFFD (eazyai)", eazyai_lffd_process, eazyai_lffd_deinit, get_bbox_only_pads, NULL);
DEF_OPS_LAYOUT(yolov8_seg, "YOLOv8 seg (eazyai)", eazyai_yolov8_seg_process, eazyai_yolov8_seg_deinit, get_bbox_only_pads, NULL, get_yolo_inst_seg_layout);
DEF_OPS_LAYOUT(yolo11_seg, "YOLO11 seg (eazyai)", eazyai_yolo11_seg_process, eazyai_yolo11_seg_deinit, get_bbox_only_pads, NULL, get_yolo_inst_seg_layout);
DEF_OPS_LAYOUT(yolov12_seg, "YOLOv12 seg (eazyai)", eazyai_yolov12_seg_process, eazyai_yolov12_seg_deinit, get_bbox_only_pads, NULL, get_yolo_inst_seg_layout);
DEF_OPS_LAYOUT(yolo26_seg, "YOLO26 seg (eazyai)", eazyai_yolo26_seg_process, eazyai_yolo26_seg_deinit, get_bbox_only_pads, NULL, get_yolo_inst_seg_layout);
DEF_OPS(centernet, "CenterNet (eazyai)", eazyai_centernet_process, eazyai_centernet_deinit, get_bbox_only_pads, NULL);
DEF_OPS(retinaface_9out, "RetinaFace (eazyai, 9 outputs: cls+bbox+landm per stride 32/16/8)",
    eazyai_retinaface_process, eazyai_retinaface_deinit, get_bbox_only_pads, NULL);
DEF_OPS(fgfd, "FGFD (eazyai)", eazyai_fgfd_process, eazyai_fgfd_deinit, get_bbox_only_pads, NULL);

void ml_register_eazyai_postprocessors(void)
{
  /*
   * YOLO: one registered name per family / task (no n/s/m/l/x or other size aliases).
   * Det+seg families use *_det and *_seg; det-only use the base name (yolov3, yolov5, …).
   */

  /* YOLOv3 */
  ml_register_postproc("yolov3", &eazyai_yolov3_ops);

  /* YOLOv5 */
  ml_register_postproc("yolov5", &eazyai_yolo_det_common_ops);

  /* YOLOX */
  ml_register_postproc("yolox", &eazyai_yolox_ops);

  /* YOLOP */
  ml_register_postproc("yolop", &eazyai_yolop_ops);

  /* YOLOv6 */
  ml_register_postproc("yolov6", &eazyai_yolov6_ops);

  /* YOLOv8 */
  ml_register_postproc("yolov8_det", &eazyai_yolov8_det_ops);

  /* YOLOv10 */
  ml_register_postproc("yolov10", &eazyai_yolov10_ops);

  /* YOLO11 */
  ml_register_postproc("yolo11_det", &eazyai_yolo11_det_ops);

  /* YOLOv12 */
  ml_register_postproc("yolov12_det", &eazyai_yolov12_det_ops);

  /* YOLO26 */
  ml_register_postproc("yolo26_det", &eazyai_yolo26_det_ops);

  /* YOLO instance segmentation */
  ml_register_postproc("yolov8_seg", &eazyai_yolov8_seg_ops);
  ml_register_postproc("yolo11_seg", &eazyai_yolo11_seg_ops);
  ml_register_postproc("yolov12_seg", &eazyai_yolov12_seg_ops);
  ml_register_postproc("yolo26_seg", &eazyai_yolo26_seg_ops);

  /* Non-YOLO detectors */
  ml_register_postproc("rtmdet", &eazyai_rtmdet_ops);
  ml_register_postproc("lffd", &eazyai_lffd_ops);
  ml_register_postproc("centernet", &eazyai_centernet_ops);
  ml_register_postproc("retinaface_9out", &eazyai_retinaface_9out_ops);
  ml_register_postproc("fgfd", &eazyai_fgfd_ops);
}
