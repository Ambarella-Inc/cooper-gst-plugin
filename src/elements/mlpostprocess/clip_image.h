/*
 * clip_image.h
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
 * CLIP / LongCLIP image encoder post-process helpers for mlpostprocess.
 */

#ifndef __MLPP_CLIP_IMAGE_H__
#define __MLPP_CLIP_IMAGE_H__

#include <stdint.h>
#include "amba_ml_decoded_result.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Load reference embedding from raw float32 (dim*4) or amba_ml_embedding_result_t file. */
int mlpp_clip_load_reference_embedding(const char *path,
    float *out_feature, uint32_t *out_dim, uint32_t max_dim);

void mlpp_clip_l2_normalize_inplace(float *v, int n);

float mlpp_clip_dot(const float *a, const float *b, int n);

#ifdef __cplusplus
}
#endif

#endif /* __MLPP_CLIP_IMAGE_H__ */
