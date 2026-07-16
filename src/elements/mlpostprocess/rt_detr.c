/*
 * rt_detr.c
 *
 * History:
 *    5/8/2026 - [pxduan] created file
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
 * RT-DETR post-processing (pred_logits + pred_boxes; not registered by default).
 */

#include "common_err_code_c.h"

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"
#include "debug_log.h"
#include "yolo_common.h"
#include "ml_postprocess_if.h"

/* 1 = per-class sigmoid + argmax (legacy); 0 = class-dim softmax (typical ONNX/PP) */
#ifndef MLPP_RTDET_USE_SIGMOID
#define MLPP_RTDET_USE_SIGMOID 0
#endif

#ifndef SIGMOID
#define SIGMOID(x) (1.0f / (1.0f + expf(-(x))))
#endif

typedef struct {
  float x0, y0, x1, y1;
  float score;
  int cls;
} mlpp_rtd_cand_t;

static void rtd_class_box_indices(const ml_postproc_ctx_t *pp, int *idx_cls, int *idx_box)
{
  int ic = -1, ib = -1, i;
  ic = ml_find_tensor_by_name(pp, "pred_logits");
  if (ic < 0)
    ic = ml_find_tensor_by_name(pp, "pred_logit");
  ib = ml_find_tensor_by_name(pp, "pred_boxes");
  if (ib < 0)
    ib = ml_find_tensor_by_name(pp, "pred_box");
  if (ic < 0 || ib < 0) {
    for (i = 0; i < 2 && i < pp->num_tensors; i++) {
      const char *nm = pp->tensors[i].name;
      if (nm && nm[0]) {
        if (ic < 0 && strstr(nm, "logit") != NULL)
          ic = i;
        if (ib < 0 && strstr(nm, "box") != NULL)
          ib = i;
      }
    }
  }
  if (ic < 0)
    ic = 0;
  if (ib < 0)
    ib = 1;
  *idx_cls = ic;
  *idx_box = ib;
}

static float rtd_iou_01(const mlpp_rtd_cand_t *a, const mlpp_rtd_cand_t *b)
{
  float xx0 = DCAL_MAX(a->x0, b->x0);
  float yy0 = DCAL_MAX(a->y0, b->y0);
  float xx1 = DCAL_MIN(a->x1, b->x1);
  float yy1 = DCAL_MIN(a->y1, b->y1);
  float w = (xx1 - xx0) > 0 ? (xx1 - xx0) : 0.f;
  float h = (yy1 - yy0) > 0 ? (yy1 - yy0) : 0.f;
  float inter = w * h;
  float a1 = (a->x1 - a->x0) * (a->y1 - a->y0);
  float a2 = (b->x1 - b->x0) * (b->y1 - b->y0);
  if (a1 <= 0.f || a2 <= 0.f)
    return 0.f;
  return inter / (a1 + a2 - inter);
}

static int rtd_cand_sort_desc(const void *a, const void *b)
{
  const mlpp_rtd_cand_t *p = (const mlpp_rtd_cand_t *)a;
  const mlpp_rtd_cand_t *q = (const mlpp_rtd_cand_t *)b;
  if (p->score < q->score)
    return 1;
  if (p->score > q->score)
    return -1;
  return 0;
}

/* Multi-class: stable softmax, class=argmax(logit), score=softmax prob of that class */
static void rtd_best_class_softmax(const float *log_row, int ncls, int *out_c, float *out_s)
{
  int c, ac;
  float msub = log_row[0], sum, e, bestl;
  for (c = 1; c < ncls; c++) {
    if (log_row[c] > msub)
      msub = log_row[c];
  }
  sum = 0.f;
  for (c = 0; c < ncls; c++) {
    e = fmaxf(fminf(log_row[c] - msub, 30.f), -30.f);
    sum += expf(e);
  }
  if (sum < 1e-20f) {
    *out_c = 0;
    *out_s = 0.f;
    return;
  }
  ac = 0;
  bestl = log_row[0];
  for (c = 1; c < ncls; c++) {
    if (log_row[c] > bestl) {
      bestl = log_row[c];
      ac = c;
    }
  }
  e = fmaxf(fminf(log_row[ac] - msub, 30.f), -30.f);
  *out_s = expf(e) / sum;
  *out_c = ac;
}

/* VFL / per-class: max sigmoid */
static void rtd_best_class_sigmoid(const float *log_row, int ncls, int *out_c, float *out_s)
{
  int c, best = 0;
  float b = SIGMOID(log_row[0]);
  for (c = 1; c < ncls; c++) {
    float s = SIGMOID(log_row[c]);
    if (s > b) {
      b = s;
      best = c;
    }
  }
  *out_c = best;
  *out_s = b;
}

/* cxcywh -> 0~1 corners; if pixel coords (any comp > 1.5) divide by nn w/h first */
static void rtd_box_cxcywh_01(float cx, float cy, float bw, float bh, int nnw, int nnh,
    float *x0, float *y0, float *x1, float *y1)
{
  if (nnw <= 0)
    nnw = 416;
  if (nnh <= 0)
    nnh = 416;
  if (cx > 1.5f || cy > 1.5f || bw > 1.5f || bh > 1.5f) {
    cx /= (float)nnw;
    cy /= (float)nnh;
    bw /= (float)nnw;
    bh /= (float)nnh;
  }
  *x0 = fmaxf(0.f, fminf(1.f, cx - bw * 0.5f));
  *y0 = fmaxf(0.f, fminf(1.f, cy - bh * 0.5f));
  *x1 = fmaxf(0.f, fminf(1.f, cx + bw * 0.5f));
  *y1 = fmaxf(0.f, fminf(1.f, cy + bh * 0.5f));
}

int mlpp_rt_detr_post_process(void *context)
{
  ml_postproc_ctx_t *ctx = (ml_postproc_ctx_t *)context;
  bounding_boxes_t *result = ctx->result;
  int ic = 0, ib = 1;
  const ml_postproc_tensor_desc_t *t_cls, *t_box;
  int nq, ncls;
  mlpp_rtd_cand_t *cand = NULL, *kept;
  int ncand = 0, nout = 0, q;
  int pitch_l, pitch_b;
  int topk;
  int i, j, ki;

  if (!ctx || !result || ctx->num_tensors < 2) {
    DPRINT_ERROR("rt_detr: need 2 tensors\n");
    return -1;
  }

  rtd_class_box_indices(ctx, &ic, &ib);
  t_cls = &ctx->tensors[ic];
  t_box = &ctx->tensors[ib];

  nq = t_cls->height;
  ncls = t_cls->width;
  if (ncls < 1)
    ncls = 1;
  if (t_cls->height != t_box->height) {
    DPRINT_ERROR("rt_detr: logits H %d != boxes H %d\n", t_cls->height, t_box->height);
    return -1;
  }
  if (t_box->width < 4) {
    DPRINT_ERROR("rt_detr: box width < 4\n");
    return -1;
  }

  pitch_l = t_cls->pitch;
  pitch_b = t_box->pitch;
  if (pitch_l < (int)sizeof(float) * t_cls->width
      || pitch_b < (int)sizeof(float) * t_box->width) {
    DPRINT_ERROR("rt_detr: invalid pitch\n");
    return -1;
  }

  cand = (mlpp_rtd_cand_t *)malloc((size_t)nq * sizeof(mlpp_rtd_cand_t));
  kept = (mlpp_rtd_cand_t *)malloc((size_t)nq * sizeof(mlpp_rtd_cand_t));
  if (!cand || !kept) {
    if (cand)
      free(cand);
    if (kept)
      free(kept);
    DPRINT_ERROR("rt_detr: oom\n");
    return -1;
  }

  for (q = 0; q < nq; q++) {
    const float *log_row = (const float *)((const uint8_t *)t_cls->data + (size_t)q * (size_t)pitch_l);
    const float *box_row = (const float *)((const uint8_t *)t_box->data + (size_t)q * (size_t)pitch_b);
    float best_s;
    int best_c;
#if MLPP_RTDET_USE_SIGMOID
    rtd_best_class_sigmoid(log_row, ncls, &best_c, &best_s);
#else
    rtd_best_class_softmax(log_row, ncls, &best_c, &best_s);
#endif
    if (best_s < ctx->conf_threshold)
      continue;
    {
      mlpp_rtd_cand_t d;
      int nnw = ctx->nn_input_width > 0 ? ctx->nn_input_width : 416;
      int nnh = ctx->nn_input_height > 0 ? ctx->nn_input_height : 416;
      float cx = box_row[0], cy = box_row[1], bw = box_row[2], bh = box_row[3];
      d.score = best_s;
      d.cls = best_c;
      rtd_box_cxcywh_01(cx, cy, bw, bh, nnw, nnh, &d.x0, &d.y0, &d.x1, &d.y1);
      if (d.x1 <= d.x0 || d.y1 <= d.y0)
        continue;
      cand[ncand++] = d;
    }
  }

  qsort(cand, (size_t)ncand, sizeof(mlpp_rtd_cand_t), rtd_cand_sort_desc);

  topk = ctx->top_k;
  if (topk < 1)
    topk = 100;
  nout = 0;
  for (i = 0; i < ncand; i++) {
    int sup = 0;
    mlpp_rtd_cand_t *a = &cand[i];
    for (j = 0; j < nout; j++) {
      mlpp_rtd_cand_t *b = &kept[j];
      if (a->cls == b->cls
          && rtd_iou_01(a, b) > ctx->nms_threshold) {
        sup = 1;
        break;
      }
    }
    if (sup)
      continue;
    kept[nout++] = *a;
    if (nout >= topk)
      break;
  }

  result->det_num = 0;
  for (ki = 0; ki < nout && (unsigned)result->det_num < D_MAX_DET_NUM; ki++) {
    det_object_t *d = &result->detections[result->det_num];
    mlpp_rtd_cand_t *K = &kept[ki];
    d->id = K->cls;
    d->score = K->score;
    d->x_start = K->x0;
    d->y_start = K->y0;
    d->x_end = K->x1;
    d->y_end = K->y1;
    if (K->cls >= 0 && K->cls < (int)ctx->valid_label_count && ctx->labels[K->cls][0])
      strncpy(d->label, ctx->labels[K->cls], DMAX_LABEL_LEN - 1);
    else
      d->label[0] = '\0';
    d->label[DMAX_LABEL_LEN - 1] = '\0';
    result->det_num++;
  }

  free(cand);
  free(kept);
  return 0;
}

/* -------- Registration -------- */
static const ml_postproc_output_pad_spec_t s_rt_detr_bbox[] = {
  { .name = "bbox", .kind = ML_POSTPROC_PAD_BBOX },
};

static const ml_postproc_output_pad_spec_t *rt_detr_pads(int *count)
{
  *count = 1;
  return s_rt_detr_bbox;
}

/* Not registered in registry; call ml_register_postproc("rt_detr", &s_rt_detr_ops) to enable */
#if defined(__GNUC__) || defined(__clang__)
static const ml_postproc_ops_t s_rt_detr_ops __attribute__((unused)) = {
#else
static const ml_postproc_ops_t s_rt_detr_ops = {
#endif
  .name = "rt_detr",
  .description = "RT-DETR (native)",
  .process = mlpp_rt_detr_post_process,
  .get_result_types = NULL,
  .get_output_pads = rt_detr_pads,
  .get_output_layout = NULL,
  .deinit_user_ctx = NULL,
  .output_coords_normalized = TRUE
};
