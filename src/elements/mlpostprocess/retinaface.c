/*
 * retinaface.c
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
 * RetinaFace post-processing (merged box/conf/landmark, PriorBox cfg_re50 + NMS).
 * Register as type=retinaface; 9-output eazyai path uses type=retinaface_9out.
 */

#include "common_err_code_c.h"

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"
#include "debug_log.h"
#include "element_common.h"
#include "ml_postprocess_if.h"
#include "yolo_common.h"

#ifndef DCAL_MIN
#define DCAL_MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef DCAL_MAX
#define DCAL_MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

#define RF_VAR0           0.1f
#define RF_VAR1           0.2f
#define RF_MAX_PRIORS     32768
#define RF_STRIDE_CNT     3

static const int s_rf_steps[RF_STRIDE_CNT] = { 8, 16, 32 };

static const float s_rf_min_sizes[RF_STRIDE_CNT][2] = {
  { 16.f, 32.f },
  { 64.f, 128.f },
  { 256.f, 512.f },
};

static int rf_ceil_div(int a, int b)
{
  if (b <= 0)
    return 0;
  return (a + b - 1) / b;
}

/* Flat priors: prior i = cx, cy, s_kx, s_ky (normalized), same order as PyTorch RetinaFace PriorBox. */
static int rf_gen_priors_res50(int ih, int iw, float *priors /* [n][4] */)
{
  int k, i, j, m;
  int idx = 0;

  if (ih <= 0 || iw <= 0 || !priors)
    return -1;

  for (k = 0; k < RF_STRIDE_CNT; k++) {
    int fh = rf_ceil_div(ih, s_rf_steps[k]);
    int fw = rf_ceil_div(iw, s_rf_steps[k]);
    for (i = 0; i < fh; i++) {
      for (j = 0; j < fw; j++) {
        for (m = 0; m < 2; m++) {
          float s_kx = s_rf_min_sizes[k][m] / (float)iw;
          float s_ky = s_rf_min_sizes[k][m] / (float)ih;
          float cx = (j + 0.5f) / (float)fw;
          float cy = (i + 0.5f) / (float)fh;
          if (idx >= RF_MAX_PRIORS)
            return -1;
          priors[idx * 4 + 0] = cx;
          priors[idx * 4 + 1] = cy;
          priors[idx * 4 + 2] = s_kx;
          priors[idx * 4 + 3] = s_ky;
          idx++;
        }
      }
    }
  }
  return idx;
}

static float rf_row_col_f32(const ml_postproc_tensor_desc_t *t, int row, int col)
{
  const uint8_t *base = (const uint8_t *)t->data;
  int pitch = t->pitch > 0 ? t->pitch : (t->width * (int)sizeof(float));
  const float *rp = (const float *)(base + (size_t)row * (size_t)pitch);
  return rp[col];
}

/**
 * Read channel c for prior index p. Supports common caps layouts from mlinference2:
 *   [N, C]: height=N width=C depth=1 (row-major)
 *   Flattened [1,1,1,N*C] / single-axis contiguous: depth=1, H*W=N*C, prior-major [p][c] -> linear p*C+c
 *   [N, C]: width=1 height=N depth=C (channel planes)
 *   [C, N]: height=C width=N depth=1 (stride along width)
 */
static float rf_get_prior_chan(const ml_postproc_tensor_desc_t *t, int num_priors,
    int ch_per_prior, int p, int c)
{
  int w = t->width > 0 ? t->width : 1;
  int h = t->height > 0 ? t->height : 1;
  int d = t->depth > 0 ? t->depth : 1;

  if (p < 0 || p >= num_priors || c < 0 || c >= ch_per_prior)
    return 0.f;

  /* Prior-major interleaved on a 2D slab (depth==1): matches ONNX concat on last dim, e.g. (1,1,1,67200). */
  if (d == 1 && h * w == num_priors * ch_per_prior) {
    int lin = p * ch_per_prior + c;
    int row = lin / w;
    int col = lin % w;
    if (row >= 0 && row < h && col >= 0 && col < w)
      return rf_row_col_f32(t, row, col);
  }

  /* Single-axis buffer W=H=1, all elements in depth (some caps parsers). */
  if (w == 1 && h == 1 && d == num_priors * ch_per_prior) {
    const float *bp = (const float *)t->data;
    return bp[p * ch_per_prior + c];
  }

  if (d == 1 && h == num_priors && w == ch_per_prior)
    return rf_row_col_f32(t, p, c);

  if (d > 1 && h == num_priors && w == 1)
    return rf_row_col_f32(t, p + c * h, 0);

  if (d == 1 && w == num_priors && h == ch_per_prior)
    return rf_row_col_f32(t, c, p);

  if (h * w * d == num_priors * ch_per_prior) {
    /* Fallback: channel-major (matches f16->f32 converter: c varies slowest) */
    int hw = h * w;
    int idx = c * hw + p;
    int row = idx / w;
    int col = idx % w;
    if (row >= 0 && row < h && col >= 0 && col < w)
      return rf_row_col_f32(t, row, col);
  }

  return 0.f;
}

static int rf_infer_num_priors_box(const ml_postproc_tensor_desc_t *t, int *ch_out)
{
  int w = t->width > 0 ? t->width : 1;
  int h = t->height > 0 ? t->height : 1;
  int d = t->depth > 0 ? t->depth : 1;
  int elems = w * h * d;
  if (elems <= 0 || (elems % 4) != 0)
    return -1;
  *ch_out = 4;
  return elems / 4;
}

static int rf_infer_num_priors_conf(const ml_postproc_tensor_desc_t *t, int *ch_out)
{
  int w = t->width > 0 ? t->width : 1;
  int h = t->height > 0 ? t->height : 1;
  int d = t->depth > 0 ? t->depth : 1;
  int elems = w * h * d;
  if (elems <= 0 || (elems % 2) != 0)
    return -1;
  *ch_out = 2;
  return elems / 2;
}

static void rf_decode_bbox_px(const float *prior, const float *loc,
    int iw, int ih, float *x1, float *y1, float *x2, float *y2)
{
  float cx = prior[0] + loc[0] * RF_VAR0 * prior[2];
  float cy = prior[1] + loc[1] * RF_VAR0 * prior[3];
  float bw = prior[2] * expf(loc[2] * RF_VAR1);
  float bh = prior[3] * expf(loc[3] * RF_VAR1);
  float x1n = cx - bw * 0.5f;
  float y1n = cy - bh * 0.5f;
  float x2n = x1n + bw;
  float y2n = y1n + bh;
  *x1 = x1n * (float)iw;
  *y1 = y1n * (float)ih;
  *x2 = x2n * (float)iw;
  *y2 = y2n * (float)ih;
}

static void rf_decode_landm_px(const float *prior4, const float *pre10,
    int iw, int ih, float *out10 /* x0,y0,...,x4,y4 nn_input px */)
{
  int k;
  for (k = 0; k < 5; k++) {
    float px = prior4[0] + pre10[k * 2 + 0] * RF_VAR0 * prior4[2];
    float py = prior4[1] + pre10[k * 2 + 1] * RF_VAR0 * prior4[3];
    out10[k * 2 + 0] = px * (float)iw;
    out10[k * 2 + 1] = py * (float)ih;
  }
}

static float rf_face_score_softmax(const ml_postproc_tensor_desc_t *conf,
    int num_priors, int p)
{
  float bg = rf_get_prior_chan(conf, num_priors, 2, p, 0);
  float fg = rf_get_prior_chan(conf, num_priors, 2, p, 1);
  float m = DCAL_MAX(bg, fg);
  float eb = expf(bg - m);
  float ef = expf(fg - m);
  return ef / (eb + ef + 1e-8f);
}

static int rf_find_tensor(const ml_postproc_ctx_t *ctx, const char *a, const char *b, const char *c)
{
  int i;
  for (i = 0; i < ctx->num_tensors; i++) {
    const char *n = ctx->tensors[i].name;
    if (!n || !n[0])
      continue;
    if (a && strcmp(n, a) == 0)
      return i;
    if (b && strcmp(n, b) == 0)
      return i;
    if (c && strcmp(n, c) == 0)
      return i;
  }
  return -1;
}

static int rf_find_tensor_substr(const ml_postproc_ctx_t *ctx, const char *sub)
{
  int i;
  if (!sub)
    return -1;
  for (i = 0; i < ctx->num_tensors; i++) {
    const char *n = ctx->tensors[i].name;
    if (n && strstr(n, sub))
      return i;
  }
  return -1;
}

typedef struct {
  float score;
  int idx;
} rf_sort_pair_t;

static int rf_sort_pair_desc(const void *va, const void *vb)
{
  const rf_sort_pair_t *a = (const rf_sort_pair_t *)va;
  const rf_sort_pair_t *b = (const rf_sort_pair_t *)vb;
  if (a->score < b->score)
    return 1;
  if (a->score > b->score)
    return -1;
  return 0;
}

/* Greedy IoU NMS; optionally copies face landmarks [num][10] with same keep order. */
static int rf_nms_xyxy(float *xyxy_score /* [n][5] */, int num, float nms_thresh,
    int top_k, float *out_xyxy_score, int *out_num,
    const float *lm_in /* [num][10] or NULL */, float *lm_out /* [*out][10] or NULL */,
    int has_lm)
{
  float *area = NULL;
  int *sort = NULL;
  int *status = NULL;
  int i, k, temp, high_ind;
  float area_high, area_i, max_x, max_y, min_x, min_y;
  float iou_width, iou_height, iou_area, iou_ratio;
  int chosen_count = 0;

  *out_num = 0;
  if (num <= 0 || !xyxy_score || !out_xyxy_score)
    return 0;

  area = (float *)malloc(sizeof(float) * (size_t)num);
  sort = (int *)malloc(sizeof(int) * (size_t)num);
  status = (int *)malloc(sizeof(int) * (size_t)num);
  if (!area || !sort || !status) {
    free(area);
    free(sort);
    free(status);
    return -1;
  }

  for (i = 0; i < num; i++) {
    float w = xyxy_score[i * 5 + 2] - xyxy_score[i * 5 + 0];
    float h = xyxy_score[i * 5 + 3] - xyxy_score[i * 5 + 1];
    area[i] = DCAL_MAX(0.f, w * h);
  }

  for (i = 0; i < num; i++)
    sort[i] = i;

  for (i = 0; i < num - 1; i++) {
    for (k = i + 1; k < num; k++) {
      if (xyxy_score[sort[i] * 5 + 4] < xyxy_score[sort[k] * 5 + 4]) {
        temp = sort[i];
        sort[i] = sort[k];
        sort[k] = temp;
      }
    }
  }

  for (i = 0; i < num; i++)
    status[i] = NMS_INIT;

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
        max_x = DCAL_MAX(xyxy_score[i * 5 + 0], xyxy_score[high_ind * 5 + 0]);
        max_y = DCAL_MAX(xyxy_score[i * 5 + 1], xyxy_score[high_ind * 5 + 1]);
        min_x = DCAL_MIN(xyxy_score[i * 5 + 2], xyxy_score[high_ind * 5 + 2]);
        min_y = DCAL_MIN(xyxy_score[i * 5 + 3], xyxy_score[high_ind * 5 + 3]);
        iou_width = DCAL_MAX(0.f, min_x - max_x);
        iou_height = DCAL_MAX(0.f, min_y - max_y);
        iou_area = iou_width * iou_height;
        iou_ratio = iou_area / DCAL_MAX(1e-8f, area_high + area_i - iou_area);
        if (iou_ratio > nms_thresh)
          status[i] = NMS_DISCARD;
      }
    }
  }

  *out_num = 0;
  for (i = 0; i < num; i++) {
    if (status[sort[i]] == NMS_CHOSEN) {
      int src = sort[i];
      memcpy(&out_xyxy_score[(*out_num) * 5], &xyxy_score[src * 5], sizeof(float) * 5);
      if (has_lm && lm_in && lm_out)
        memcpy(&lm_out[(*out_num) * 10], &lm_in[src * 10], sizeof(float) * 10);
      (*out_num)++;
    }
  }

  free(area);
  free(sort);
  free(status);
  return 0;
}

static int rf_resolve_indices(const ml_postproc_ctx_t *ctx, int *ibox, int *iconf, int *ilm)
{
  *ibox = rf_find_tensor(ctx, "box", "bbox", "loc");
  if (*ibox < 0)
    *ibox = rf_find_tensor_substr(ctx, "box");
  if (*ibox < 0)
    *ibox = rf_find_tensor_substr(ctx, "loc");

  *iconf = rf_find_tensor(ctx, "conf", "cls", "prob");
  if (*iconf < 0)
    *iconf = rf_find_tensor_substr(ctx, "conf");

  *ilm = rf_find_tensor(ctx, "face_point", "landmark", NULL);
  if (*ilm < 0)
    *ilm = rf_find_tensor_substr(ctx, "landm");
  if (*ilm < 0)
    *ilm = rf_find_tensor_substr(ctx, "face_point");

  if (*ibox < 0 || *iconf < 0)
    return -1;
  /* Landmark tensor optional for bbox-only pipeline */
  return 0;
}

int mlpp_retinaface_process(void *context)
{
  ml_postproc_ctx_t *ctx = (ml_postproc_ctx_t *)context;
  bounding_boxes_t *result;
  int ibox, iconf, ilm;
  int num_pb, num_pc, chb, chc;
  int np_expect;
  float priors[RF_MAX_PRIORS * 4];
  rf_sort_pair_t *pairs = NULL;
  float *pre_nms = NULL;
  float *post_nms = NULL;
  float *pre_nms_lm = NULL;
  float *post_nms_lm = NULL;
  int iw, ih;
  int n_cand, i, j, n_kept;
  int use_lm;
  float thresh = ctx ? ctx->conf_threshold : 0.02f;
  float nms_thresh = ctx ? ctx->nms_threshold : 0.4f;
  int top_k_pre = ctx && ctx->top_k > 0 ? ctx->top_k : 5000;

  if (!ctx || !ctx->result || ctx->num_tensors < 2)
    return -1;

  result = ctx->result;
  result->det_num = 0;

  if (rf_resolve_indices(ctx, &ibox, &iconf, &ilm) < 0) {
    DPRINT_ERROR("retinaface: need box + conf tensors\n");
    return -1;
  }

  num_pb = rf_infer_num_priors_box(&ctx->tensors[ibox], &chb);
  num_pc = rf_infer_num_priors_conf(&ctx->tensors[iconf], &chc);
  if (num_pb < 0 || num_pc < 0 || chb != 4 || chc != 2) {
    DPRINT_ERROR("retinaface: invalid box/conf shapes\n");
    return -1;
  }
  if (num_pb != num_pc) {
    DPRINT_ERROR("retinaface: box priors %d != conf %d\n", num_pb, num_pc);
    return -1;
  }

  use_lm = 0;
  if (ilm >= 0) {
    int wlm = ctx->tensors[ilm].width > 0 ? ctx->tensors[ilm].width : 1;
    int hlm = ctx->tensors[ilm].height > 0 ? ctx->tensors[ilm].height : 1;
    int dlm = ctx->tensors[ilm].depth > 0 ? ctx->tensors[ilm].depth : 1;
    int nl = wlm * hlm * dlm;
    if ((nl % 10) != 0 || (nl / 10) != num_pb) {
      DPRINT_WARNING("retinaface: landmark tensor size mismatch (ignored)\n");
      ilm = -1;
    } else {
      use_lm = 1;
    }
  }

  iw = ctx->nn_input_width > 0 ? ctx->nn_input_width : 640;
  ih = ctx->nn_input_height > 0 ? ctx->nn_input_height : 640;

  np_expect = rf_gen_priors_res50(ih, iw, priors);
  if (np_expect < 0 || np_expect != num_pb) {
    DPRINT_ERROR("retinaface: prior count %d vs tensor %d (nn_input=%dx%d)\n",
        np_expect, num_pb, iw, ih);
    return -1;
  }

  pairs = (rf_sort_pair_t *)malloc(sizeof(rf_sort_pair_t) * (size_t)num_pb);
  pre_nms = (float *)malloc(sizeof(float) * (size_t)num_pb * 5);
  post_nms = (float *)malloc(sizeof(float) * (size_t)num_pb * 5);
  if (use_lm) {
    pre_nms_lm = (float *)calloc((size_t)num_pb * 10, sizeof(float));
    post_nms_lm = (float *)calloc((size_t)num_pb * 10, sizeof(float));
  }
  if (!pairs || !pre_nms || !post_nms || (use_lm && (!pre_nms_lm || !post_nms_lm))) {
    free(pairs);
    free(pre_nms);
    free(post_nms);
    free(pre_nms_lm);
    free(post_nms_lm);
    return -1;
  }

  n_cand = 0;
  for (i = 0; i < num_pb; i++) {
    float loc[4];
    float prior[4];
    float x1, y1, x2, y2;
    float sc;

    for (j = 0; j < 4; j++) {
      prior[j] = priors[i * 4 + j];
      loc[j] = rf_get_prior_chan(&ctx->tensors[ibox], num_pb, 4, i, j);
    }
    rf_decode_bbox_px(prior, loc, iw, ih, &x1, &y1, &x2, &y2);
    sc = rf_face_score_softmax(&ctx->tensors[iconf], num_pc, i);

    if (sc <= thresh)
      continue;

    x1 = DCAL_MAX(0.f, DCAL_MIN((float)iw, x1));
    y1 = DCAL_MAX(0.f, DCAL_MIN((float)ih, y1));
    x2 = DCAL_MAX(x1, DCAL_MIN((float)iw, x2));
    y2 = DCAL_MAX(y1, DCAL_MIN((float)ih, y2));

    pre_nms[n_cand * 5 + 0] = x1;
    pre_nms[n_cand * 5 + 1] = y1;
    pre_nms[n_cand * 5 + 2] = x2;
    pre_nms[n_cand * 5 + 3] = y2;
    pre_nms[n_cand * 5 + 4] = sc;

    if (use_lm) {
      float pre10[10];
      float lmpx[10];
      for (j = 0; j < 10; j++)
        pre10[j] = rf_get_prior_chan(&ctx->tensors[ilm], num_pb, 10, i, j);
      rf_decode_landm_px(prior, pre10, iw, ih, lmpx);
      memcpy(&pre_nms_lm[n_cand * 10], lmpx, sizeof(float) * 10);
    }
    n_cand++;
  }

  if (n_cand <= 0) {
    free(pairs);
    free(pre_nms);
    free(post_nms);
    free(pre_nms_lm);
    free(post_nms_lm);
    return 0;
  }

  /* Sort by score and cap candidates before NMS */
  for (i = 0; i < n_cand; i++) {
    pairs[i].score = pre_nms[i * 5 + 4];
    pairs[i].idx = i;
  }
  qsort(pairs, (size_t)n_cand, sizeof(rf_sort_pair_t), rf_sort_pair_desc);

  {
    float *tmp = (float *)malloc(sizeof(float) * (size_t)n_cand * 5);
    float *tmp_lm = use_lm ? (float *)malloc(sizeof(float) * (size_t)n_cand * 10) : NULL;
    if (!tmp || (use_lm && !tmp_lm)) {
      free(tmp);
      free(tmp_lm);
      free(pairs);
      free(pre_nms);
      free(post_nms);
      free(pre_nms_lm);
      free(post_nms_lm);
      return -1;
    }
    j = 0;
    for (i = 0; i < n_cand && j < top_k_pre; i++) {
      int src_row = pairs[i].idx;
      memcpy(&tmp[j * 5], &pre_nms[src_row * 5], sizeof(float) * 5);
      if (use_lm)
        memcpy(&tmp_lm[j * 10], &pre_nms_lm[src_row * 10], sizeof(float) * 10);
      j++;
    }
    memcpy(pre_nms, tmp, sizeof(float) * (size_t)j * 5);
    if (use_lm)
      memcpy(pre_nms_lm, tmp_lm, sizeof(float) * (size_t)j * 10);
    free(tmp);
    free(tmp_lm);
    n_cand = j;
  }
  free(pairs);
  pairs = NULL;

  /* top_k=0: do not cap greedy NMS picks (same spirit as py_cpu_nms); trim when copying to result */
  if (rf_nms_xyxy(pre_nms, n_cand, nms_thresh, 0, post_nms, &n_kept,
          use_lm ? pre_nms_lm : NULL, use_lm ? post_nms_lm : NULL, use_lm) < 0) {
    free(pre_nms);
    free(post_nms);
    free(pre_nms_lm);
    free(post_nms_lm);
    return -1;
  }

  free(pre_nms);
  free(pre_nms_lm);
  pre_nms = NULL;
  pre_nms_lm = NULL;

  result->det_num = 0;
  for (i = 0; i < n_kept && result->det_num < D_MAX_DET_NUM; i++) {
    det_object_t *d = &result->detections[result->det_num];
    d->id = 0;
    d->score = post_nms[i * 5 + 4];
    d->x_start = post_nms[i * 5 + 0];
    d->y_start = post_nms[i * 5 + 1];
    d->x_end = post_nms[i * 5 + 2];
    d->y_end = post_nms[i * 5 + 3];
    d->has_landmarks = use_lm ? 1 : 0;
    if (use_lm)
      memcpy(d->landmark, &post_nms_lm[i * 10], sizeof(d->landmark));
    else
      memset(d->landmark, 0, sizeof(d->landmark));
    if ((unsigned int)ctx->valid_label_count > 0 && ctx->labels[0][0])
      (void)snprintf(d->label, sizeof(d->label), "%s", ctx->labels[0]);
    else
      (void)snprintf(d->label, sizeof(d->label), "face");
    result->det_num++;
  }

  free(post_nms);
  free(post_nms_lm);
  return 0;
}
