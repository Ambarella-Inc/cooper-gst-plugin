/*
 * ml_postprocess_if.h
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
 * Extensible post-processor interface for mlpostprocess.
 */

#ifndef __ML_POSTPROC_IF_H__
#define __ML_POSTPROC_IF_H__

#include <stdint.h>
#include <string.h>

#include "internal.h"
#include "element_common.h"
#include "amba_ml_decoded_result.h"
#include "ml_tensors_caps.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef DMAX_LABEL_NUM
#define DMAX_LABEL_NUM 1024
#endif

#ifndef ML_POSTPROC_MAX_OUTPUT_PADS
#define ML_POSTPROC_MAX_OUTPUT_PADS 8
#endif

#define ML_POSTPROC_MAX_SEG_OUTPUTS GST_AMBA_ML_DECODED_META_MAX_ENTRIES

/**
 * ml_postproc_layout_entry_t - One output entry in the layout
 * Describes what to serialize and append to the output buffer.
 */
typedef struct {
  amba_ml_result_type_t type;
  int seg_idx;  /* For SEGMENTATION/CUSTOM: index into pp_ctx.seg_outputs[]; -1 for bbox/classification/pose */
} ml_postproc_layout_entry_t;

/**
 * ml_postproc_output_layout_t - Output layout for generic chain serialization
 * Order of entries matches output buffer memory order.
 */
typedef struct {
  int n_entries;
  ml_postproc_layout_entry_t entries[GST_AMBA_ML_DECODED_META_MAX_ENTRIES];
} ml_postproc_output_layout_t;

/**
 * ml_postproc_pad_kind_t - Output pad kind (determines caps template)
 */
typedef enum {
  ML_POSTPROC_PAD_BBOX = 0,       /* application/x-amba-ml-decoded, detection_bbox */
  ML_POSTPROC_PAD_VIDEO_GRAY8,    /* video/x-raw, format=GRAY8 (seg masks) */
  ML_POSTPROC_PAD_CLASSIFICATION, /* application/x-amba-ml-decoded, CLASSIFICATION payload */
  ML_POSTPROC_PAD_EMBEDDING,      /* application/x-amba-ml-decoded, EMBEDDING payload */
} ml_postproc_pad_kind_t;

/**
 * ml_postproc_output_pad_spec_t - Spec for one output pad
 * Order in get_output_pads() must match process output: [0]=bbox, [1]=first video, ...
 */
typedef struct {
  const char *name;               /* Pad name, e.g. "bbox", "drive_area" */
  ml_postproc_pad_kind_t kind;    /* Caps template to use */
} ml_postproc_output_pad_spec_t;

/* Tensor info for post-process (from GstBuffer) */
#define ML_POSTPROC_TENSOR_NAME_LEN 64
typedef struct {
  float *data;
  int width;
  int height;
  int depth;
  int pitch;  /* bytes per row */
  char name[ML_POSTPROC_TENSOR_NAME_LEN];  /* from caps "names", for lookup */
} ml_postproc_tensor_desc_t;

/* Segmentation output (e.g. yolop drive_area, lane_line) */
typedef struct {
  uint8_t *mask;   /* allocated by post_process, caller wraps in GstMemory */
  int width;
  int height;
} ml_postproc_seg_output_t;

/* Context passed to post-processors - filled by mlpostprocess */
typedef struct {
  int num_tensors;
  ml_postproc_tensor_desc_t tensors[AMBA_ML_MAX_TENSORS];
  int nn_input_width;
  int nn_input_height;

  float conf_threshold;
  float nms_threshold;
  int top_k;
  int use_multi_cls;

  char labels[DMAX_LABEL_NUM][DMAX_LABEL_LEN];
  unsigned int valid_label_count;

  bounding_boxes_t *result;

  /** Generic seg outputs: YOLOP [0]/[1] drive+lane; generic / YOLO inst-seg merged mask [0] (GRAY8 index or label map). */
  ml_postproc_seg_output_t seg_outputs[ML_POSTPROC_MAX_SEG_OUTPUTS];

  /** Filled by classification post-processor from first (or named "output") logits tensor */
  amba_ml_classification_body_t classification;

  /** Filled by rtmpose post-processor (SIMCC decode); keypoints in coord_res pixels */
  amba_ml_pose_body_t pose;

  /** Filled by clip_image post-processor: L2-normalized image embedding */
  amba_ml_embedding_body_t embedding;

  /** CLIP reference (from mlpostprocess reference-embedding file); not owned by ctx */
  const float *clip_ref_feature;
  uint32_t clip_ref_dim;
  unsigned char clip_ref_valid;
  const char *clip_ref_label;

  /** user_data: for eazyai-backed types, element passes mlpp_priv_ctx_t */
  void *user_data;
} ml_postproc_ctx_t;

/**
 * ml_postproc_ops_t - Post-processor operations (vtable)
 * Each network type implements this and registers with ml_register_postproc.
 */
typedef struct ml_postproc_ops ml_postproc_ops_t;

struct ml_postproc_ops {
  /** Display name, e.g. "yolov5", "yolox" */
  const char *name;

  /** Short description for debug/log */
  const char *description;

  /**
   * process - Main entry point
   * @ctx: ml_postproc_ctx_t, filled by mlpostprocess
   * Returns: 0 on success, <0 on error
   */
  int (*process)(void *ctx);

  /**
   * get_result_types - Optional. Return result_types string for caps.
   * Set per network type in registry. For dynamic output (e.g. yolop 3 vs 5 tensors),
   * element determines from actual output; this is fallback for empty/no-output case.
   * If NULL, defaults to "detection_bbox".
   */
  const char *(*get_result_types)(void);

  /**
   * get_output_pads - Return output pad specs for this network.
   * Order must match process output: [0]=bbox, [1]=first video (e.g. drive_area), [2]=second video (e.g. lane_line).
   * If NULL, defaults to single "bbox" pad.
   * @count: output, number of pad specs
   * @return: array of specs (static, never freed)
   */
  const ml_postproc_output_pad_spec_t *(*get_output_pads)(int *count);

  /**
   * get_output_layout - Return structured output layout for generic chain serialization.
   * If NULL, layout is derived from get_result_types() for backward compatibility.
   * @return: static layout (never freed), or NULL to use get_result_types
   */
  const ml_postproc_output_layout_t *(*get_output_layout)(void);

  /**
   * deinit_user_ctx - Optional. For eazyai-backed types, frees priv->eazyai_postp_ctx.
   * @priv: mlpp_priv_ctx_t from user_data
   */
  void (*deinit_user_ctx)(void *priv);

  /** If true, bbox coords are 0~1 normalized; scale with x*mw. If false, coords are nn_input pixels; scale with x*mw/nw. */
  gboolean output_coords_normalized;
};

/**
 * ml_find_tensor_by_name - Look up tensor index by name (from caps "names")
 * @ctx: post-process context
 * @name: tensor name to find
 * Returns: tensor index (0..num_tensors-1), or -1 if not found
 */
static inline int ml_find_tensor_by_name(const ml_postproc_ctx_t *ctx, const char *name)
{
  int i;
  if (!ctx || !name || !name[0])
    return -1;
  for (i = 0; i < ctx->num_tensors; i++) {
    if (ctx->tensors[i].name[0] && strcmp(ctx->tensors[i].name, name) == 0)
      return i;
  }
  return -1;
}

/**
 * ml_register_postproc - Register a post-processor (call for each type name)
 */
void ml_register_postproc(const char *name, const ml_postproc_ops_t *ops);

/**
 * ml_find_postproc - Look up post-processor by type name
 */
const ml_postproc_ops_t *ml_find_postproc(const char *name);

/**
 * ml_unregister_all_postproc - Clear registry (e.g. for plugin unload)
 */
void ml_unregister_all_postproc(void);

#ifdef __cplusplus
}
#endif

#endif /* __ML_POSTPROC_IF_H__ */
