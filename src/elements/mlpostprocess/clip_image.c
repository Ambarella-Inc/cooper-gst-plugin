/*
 * clip_image.c
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
 * CLIP / LongCLIP image-encoder post-process (L2 embedding + optional ref match).
 * Register as type=clip_image or longclip.
 */

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "debug_log.h"
#include "clip_image.h"
#include "ml_postprocess_if.h"

#ifndef ML_PP_CLIP_IMAGE_MAX_ELEMENTS
#define ML_PP_CLIP_IMAGE_MAX_ELEMENTS AMBA_ML_EMBEDDING_MAX_DIM
#endif

void
mlpp_clip_l2_normalize_inplace(float *v, int n)
{
  double sum = 0.0;
  float norm;
  int i;

  if (!v || n <= 0)
    return;

  for (i = 0; i < n; i++)
    sum += (double)v[i] * (double)v[i];

  norm = (float)sqrt(sum);
  if (norm < 1e-12f)
    return;

  for (i = 0; i < n; i++)
    v[i] /= norm;
}

float
mlpp_clip_dot(const float *a, const float *b, int n)
{
  double sum = 0.0;
  int i;

  if (!a || !b || n <= 0)
    return 0.0f;

  for (i = 0; i < n; i++)
    sum += (double)a[i] * (double)b[i];

  return (float)sum;
}

int
mlpp_clip_load_reference_embedding(const char *path,
    float *out_feature, uint32_t *out_dim, uint32_t max_dim)
{
  FILE *fp;
  long fsize;
  size_t nread;
  int n;

  if (!path || !path[0] || !out_feature || !out_dim || max_dim == 0)
    return -1;

  fp = fopen(path, "rb");
  if (!fp) {
    DPRINT_ERROR("clip_image: cannot open reference-embedding %s\n", path);
    return -1;
  }

  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return -1;
  }
  fsize = ftell(fp);
  if (fsize <= 0) {
    fclose(fp);
    return -1;
  }
  if (fseek(fp, 0, SEEK_SET) != 0) {
    fclose(fp);
    return -1;
  }

  memset(out_feature, 0, (size_t)max_dim * sizeof(float));
  *out_dim = 0;

  if ((size_t)fsize == sizeof(amba_ml_embedding_result_t)) {
    amba_ml_embedding_result_t res;
    nread = fread(&res, 1, sizeof(res), fp);
    fclose(fp);
    if (nread != sizeof(res) || !AMBA_ML_RESULT_IS_VALID(res.header) ||
        res.header.type != AMBA_ML_RESULT_TYPE_EMBEDDING) {
      DPRINT_ERROR("clip_image: invalid embedding result file %s\n", path);
      return -1;
    }
    n = (int)res.body.dim;
    if (n <= 0 || n > (int)max_dim)
      return -1;
    memcpy(out_feature, res.body.feature, (size_t)n * sizeof(float));
    *out_dim = (uint32_t)n;
  } else if ((fsize % (long)sizeof(float)) == 0) {
    n = (int)(fsize / (long)sizeof(float));
    if (n <= 0 || n > (int)max_dim) {
      fclose(fp);
      DPRINT_ERROR("clip_image: reference dim %d out of range (max %u)\n", n, max_dim);
      return -1;
    }
    nread = fread(out_feature, sizeof(float), (size_t)n, fp);
    fclose(fp);
    if ((int)nread != n)
      return -1;
    *out_dim = (uint32_t)n;
  } else {
    fclose(fp);
    DPRINT_ERROR("clip_image: reference file size %ld not float32 or embedding struct\n", fsize);
    return -1;
  }

  mlpp_clip_l2_normalize_inplace(out_feature, (int)*out_dim);
  return 0;
}

static void
mlpp_clip_linearize(const ml_postproc_tensor_desc_t *t, float *out, int max_n, int *out_n)
{
  int w = t->width;
  int h = t->height;
  int d = t->depth;
  int pitch = t->pitch;
  int c, j, k;
  const unsigned char *base = (const unsigned char *)t->data;
  float *dst = out;

  if (w <= 0 || h <= 0 || d <= 0 || !t->data || pitch < (int)(w * sizeof(float))) {
    *out_n = 0;
    return;
  }
  if (w * h * d > max_n) {
    *out_n = 0;
    return;
  }

  for (c = 0; c < d; c++) {
    for (j = 0; j < h; j++) {
      const float *row = (const float *)(base + (size_t)(c * h + j) * (size_t)pitch);
      for (k = 0; k < w; k++)
        *dst++ = row[k];
    }
  }
  *out_n = w * h * d;
}

static int
mlpp_clip_image_process(void *ctx)
{
  ml_postproc_ctx_t *pp = (ml_postproc_ctx_t *)ctx;
  ml_postproc_tensor_desc_t *t = NULL;
  float tmp[ML_PP_CLIP_IMAGE_MAX_ELEMENTS];
  int n = 0;
  int i;

  if (!pp || pp->num_tensors < 1)
    return -1;

  memset(&pp->embedding, 0, sizeof(pp->embedding));
  t = &pp->tensors[0];

  mlpp_clip_linearize(t, tmp, ML_PP_CLIP_IMAGE_MAX_ELEMENTS, &n);
  if (n <= 0) {
    DPRINT_ERROR("clip_image: invalid tensor shape or empty output\n");
    return -1;
  }
  if (n > (int)AMBA_ML_EMBEDDING_MAX_DIM) {
    DPRINT_ERROR("clip_image: dim %d > max %d\n", n, AMBA_ML_EMBEDDING_MAX_DIM);
    return -1;
  }

  mlpp_clip_l2_normalize_inplace(tmp, n);
  pp->embedding.dim = (uint32_t)n;
  for (i = 0; i < n; i++)
    pp->embedding.feature[i] = tmp[i];

  if (pp->clip_ref_label && pp->clip_ref_label[0]) {
    size_t llen = strlen(pp->clip_ref_label);
    if (llen >= AMBA_ML_CLASSIFICATION_LABEL_LEN)
      llen = AMBA_ML_CLASSIFICATION_LABEL_LEN - 1;
    memcpy(pp->embedding.match_label, pp->clip_ref_label, llen);
    pp->embedding.match_label[llen] = '\0';
  }

  if (pp->clip_ref_valid && pp->clip_ref_feature && pp->clip_ref_dim > 0) {
    if (pp->clip_ref_dim == pp->embedding.dim) {
      pp->embedding.match_score = mlpp_clip_dot(pp->embedding.feature,
          pp->clip_ref_feature, (int)pp->embedding.dim);
      pp->embedding.match_valid = 1;
    } else {
      DPRINT_ERROR("clip_image: ref dim %u != current dim %u\n",
          pp->clip_ref_dim, pp->embedding.dim);
    }
  }

  return 0;
}

static const char *clip_image_get_result_types(void)
{
  return GST_AMBA_ML_RESULT_TYPE_EMBEDDING;
}

static const ml_postproc_output_pad_spec_t clip_image_output_pads[] = {
  { .name = "embedding", .kind = ML_POSTPROC_PAD_EMBEDDING },
};

static const ml_postproc_output_pad_spec_t *clip_image_get_output_pads(int *count)
{
  *count = 1;
  return clip_image_output_pads;
}

static const ml_postproc_output_layout_t clip_image_output_layout = {
  .n_entries = 1,
  .entries = {
    { .type = AMBA_ML_RESULT_TYPE_EMBEDDING, .seg_idx = -1 },
  },
};

static const ml_postproc_output_layout_t *clip_image_get_output_layout(void)
{
  return &clip_image_output_layout;
}

static const ml_postproc_ops_t mlpp_clip_image_ops = {
  .name = "clip_image",
  .description = "CLIP/LongCLIP image encoder: L2 embedding + optional ref match score",
  .process = mlpp_clip_image_process,
  .get_result_types = clip_image_get_result_types,
  .get_output_pads = clip_image_get_output_pads,
  .get_output_layout = clip_image_get_output_layout,
  .deinit_user_ctx = NULL,
  .output_coords_normalized = FALSE,
};

void ml_register_clip_image_builtin(void)
{
  ml_register_postproc("clip_image", &mlpp_clip_image_ops);
  ml_register_postproc("longclip", &mlpp_clip_image_ops);
}
