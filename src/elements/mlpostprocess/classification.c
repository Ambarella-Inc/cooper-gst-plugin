/*
 * classification.c
 *
 * History:
 *    5/9/2026 - [pxduan] created file
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
 * Image classification post-processing for mlpostprocess (ea_postp_classification
 * with CPU softmax top-k fallback). Register as type=classification.
 */

#include <math.h>
#include <stddef.h>
#include <string.h>
#include <stdint.h>

#include "debug_log.h"
#include "ea_postproc_common.h"
#include "ml_postprocess_if.h"

#ifndef ML_PP_CLASSIFICATION_MAX_LOGITS
#define ML_PP_CLASSIFICATION_MAX_LOGITS 8192
#endif

static void
mlpp_cls_fill_ea_tensor(const ml_postproc_tensor_desc_t *t, ea_postproc_tensor_t *ea)
{
  int c0;

  memset(ea, 0, sizeof(*ea));
  ea->p_buffer = (uint8_t *)t->data;
  ea->shape[EA_P_N] = 1;
  c0 = t->depth;
  ea->shape[EA_P_C] = c0 > 0 ? c0 : 1;
  ea->shape[EA_P_H] = t->height;
  ea->shape[EA_P_W] = t->width;
  ea->pitch = (uint32_t)t->pitch;
  ea->data_format = EA_P_F32;
  ea->p_name = (t->name[0] != '\0') ? t->name : (char *)"output";
}

static int
mlpp_cls_tensor_class_count(const ml_postproc_tensor_desc_t *t)
{
  int w = t->width;
  int h = t->height;
  int d = t->depth;
  int n = w * h * d;

  if (w <= 0 || h <= 0 || d <= 0 || !t->data || t->pitch < (int)(w * sizeof(float)))
    return -1;
  if (n > ML_PP_CLASSIFICATION_MAX_LOGITS)
    return -1;
  return n;
}

static void
mlpp_cls_linearize_logits(const ml_postproc_tensor_desc_t *t, float *out, int max_n, int *out_n)
{
  int w = t->width;
  int h = t->height;
  int d = t->depth;
  int pitch = t->pitch; /* bytes per row */
  int n = w * h * d;
  int c, j, k;
  const unsigned char *base = (const unsigned char *)t->data;

  if (w <= 0 || h <= 0 || d <= 0 || !t->data || pitch < (int)(w * sizeof(float))) {
    *out_n = 0;
    return;
  }

  if (n > max_n) {
    *out_n = 0;
    return;
  }

  {
    float *dst = out;
    for (c = 0; c < d; c++) {
      for (j = 0; j < h; j++) {
        const float *row = (const float *)(base + (size_t)(c * h + j) * (size_t)pitch);
        for (k = 0; k < w; k++)
          *dst++ = row[k];
      }
    }
  }
  *out_n = n;
}

static void
mlpp_cls_softmax_inplace(float *v, int n)
{
  float m = v[0];
  int i;
  double sum;
  for (i = 1; i < n; i++) {
    if (v[i] > m)
      m = v[i];
  }
  sum = 0.0;
  for (i = 0; i < n; i++) {
    double e = exp((double)(v[i] - m));
    v[i] = (float)e;
    sum += e;
  }
  if (sum <= 0.0) {
    return;
  }
  for (i = 0; i < n; i++) {
    v[i] = (float)((double)v[i] / sum);
  }
}

static void
mlpp_cls_fill_label(ml_postproc_ctx_t *pp, int class_id, char *dst, size_t dstsz)
{
  if (!dst || dstsz == 0)
    return;
  dst[0] = '\0';
  if (class_id >= 0 && (unsigned int)class_id < pp->valid_label_count) {
    strncpy(dst, pp->labels[class_id], dstsz - 1);
    dst[dstsz - 1] = '\0';
  }
}

/**
 * EazyAI path: matches model_garden apps/classification (ea_postp_classification).
 * Returns 0 on success, -1 if library call failed (caller may fall back).
 */
static int
mlpp_classification_try_eazyai(ml_postproc_ctx_t *pp, const ml_postproc_tensor_desc_t *t,
    int n_class, int want_k)
{
  ea_postproc_tensor_t ea_tensor;
  ea_postproc_input_t postp_in;
  ea_postproc_classification_out_t ea_out[AMBA_ML_CLASSIFICATION_TOPK_MAX];
  int rv;
  int out_i, src_i;

  memset(ea_out, 0, sizeof(ea_out));
  mlpp_cls_fill_ea_tensor(t, &ea_tensor);
  postp_in.num = 1;
  postp_in.p_tensor = &ea_tensor;

  rv = ea_postp_classification(&postp_in, want_k, ea_out);
  if (rv != 0)
    return -1;

  pp->classification.num_classes = (uint32_t)n_class;
  out_i = 0;
  for (src_i = 0; src_i < want_k && out_i < (int)AMBA_ML_CLASSIFICATION_TOPK_MAX; src_i++) {
    if (ea_out[src_i].score < pp->conf_threshold)
      continue;

    pp->classification.ranked[out_i].class_id = ea_out[src_i].id;
    pp->classification.ranked[out_i].score = ea_out[src_i].score;
    mlpp_cls_fill_label(pp, (int)ea_out[src_i].id, pp->classification.ranked[out_i].label,
        sizeof(pp->classification.ranked[out_i].label));
    out_i++;
  }
  pp->classification.top_k_out = (uint32_t)out_i;
  return 0;
}

static int
mlpp_classification_process_builtin(ml_postproc_ctx_t *pp, const ml_postproc_tensor_desc_t *t, int n,
    int want_k)
{
  float logits[ML_PP_CLASSIFICATION_MAX_LOGITS];
  int kk, i;

  mlpp_cls_linearize_logits(t, logits, ML_PP_CLASSIFICATION_MAX_LOGITS, &n);
  if (n <= 0) {
    DPRINT_ERROR("classification: bad tensor or too many logits (max %d)\n",
        ML_PP_CLASSIFICATION_MAX_LOGITS);
    return -1;
  }

  mlpp_cls_softmax_inplace(logits, n);
  pp->classification.num_classes = (uint32_t)n;

  for (kk = 0; kk < want_k; kk++) {
    int best = -1;
    float bestv = -1.0f;
    for (i = 0; i < n; i++) {
      int taken = 0;
      int j;
      for (j = 0; j < kk; j++) {
        if (pp->classification.ranked[j].class_id == i) {
          taken = 1;
          break;
        }
      }
      if (taken)
        continue;
      if (best < 0 || logits[i] > bestv) {
        best = i;
        bestv = logits[i];
      }
    }
    if (best < 0)
      break;
    if (bestv < pp->conf_threshold)
      break;

    pp->classification.ranked[kk].class_id = (int32_t)best;
    pp->classification.ranked[kk].score = bestv;
    mlpp_cls_fill_label(pp, best, pp->classification.ranked[kk].label,
        sizeof(pp->classification.ranked[kk].label));
  }
  pp->classification.top_k_out = (uint32_t)kk;

  return 0;
}

int mlpp_classification_process(void *ctx)
{
  ml_postproc_ctx_t *pp = (ml_postproc_ctx_t *)ctx;
  const ml_postproc_tensor_desc_t *t;
  int n;
  int want_k;
  int ti;

  if (!pp || pp->num_tensors < 1) {
    return -1;
  }

  ti = ml_find_tensor_by_name(pp, "output");
  if (ti < 0)
    ti = 0;
  t = &pp->tensors[ti];

  memset(&pp->classification, 0, sizeof(pp->classification));

  n = mlpp_cls_tensor_class_count(t);
  if (n <= 0) {
    DPRINT_ERROR("classification: invalid logits tensor (shape/pitch) or too many classes\n");
    return -1;
  }

  if (pp->valid_label_count > 0 && (unsigned int)n != pp->valid_label_count) {
    DPRINT_WARNING("classification: label file has %u entries but output has %d classes "
        "(use imagenet_1000.txt or match model output)\n",
        pp->valid_label_count, n);
  }

  want_k = pp->top_k;
  if (want_k < 1)
    want_k = 1;
  if (want_k > (int)AMBA_ML_CLASSIFICATION_TOPK_MAX)
    want_k = (int)AMBA_ML_CLASSIFICATION_TOPK_MAX;

  if (mlpp_classification_try_eazyai(pp, t, n, want_k) == 0)
    return 0;

  DPRINT_NOTICE("classification: ea_postp_classification failed, using built-in softmax path\n");
  return mlpp_classification_process_builtin(pp, t, n, want_k);
}

static const char *classification_get_result_types(void)
{
  return GST_AMBA_ML_RESULT_TYPE_CLASSIFICATION;
}

static const ml_postproc_output_pad_spec_t classification_output_pads[] = {
  { .name = "classification", .kind = ML_POSTPROC_PAD_CLASSIFICATION },
};

static const ml_postproc_output_pad_spec_t *classification_get_output_pads(int *count)
{
  *count = 1;
  return classification_output_pads;
}

static const ml_postproc_output_layout_t classification_output_layout = {
  .n_entries = 1,
  .entries = {
    { .type = AMBA_ML_RESULT_TYPE_CLASSIFICATION, .seg_idx = -1 },
  },
};

static const ml_postproc_output_layout_t *classification_get_output_layout(void)
{
  return &classification_output_layout;
}

static const ml_postproc_ops_t mlpp_classification_ops = {
  .name = "classification",
  .description = "Image classification: ea_postp_classification (EazyAI) with softmax fallback",
  .process = mlpp_classification_process,
  .get_result_types = classification_get_result_types,
  .get_output_pads = classification_get_output_pads,
  .get_output_layout = classification_get_output_layout,
  .deinit_user_ctx = NULL,
  .output_coords_normalized = FALSE,
};

void ml_register_classification_builtin(void)
{
  ml_register_postproc("classification", &mlpp_classification_ops);
}
