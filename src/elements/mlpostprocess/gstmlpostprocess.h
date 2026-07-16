/*
 * gstmlpostprocess.h
 *
 * History:
 *    2/6/2026 - [pxduan] created file
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
 */

/**
 * SECTION:element-mlpostprocess
 * @title: mlpostprocess
 * @see_also: mlinference2, amba_draw_data_gen
 *
 * Post-processes ML tensors from mlinference2 (NMS, bbox, segmentation).
 * Input: application/x-amba-ml-tensors. Output: application/x-amba-ml-decoded or video/x-raw GRAY8.
 * Downstream: amba_draw_data_gen or videoconvert.
 */

#ifndef __GST_AMBA_ML_POSTPROCESS_H__
#define __GST_AMBA_ML_POSTPROCESS_H__

#include <gst/gst.h>

#include "internal.h"
#include "ml_postprocess_if.h"
#include "element_common.h"

G_BEGIN_DECLS

#define GST_TYPE_ML_POSTPROCESS (gst_ml_postprocess_get_type())
#define GST_ML_POSTPROCESS(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_ML_POSTPROCESS, GstMlPostprocess))
#define GST_ML_POSTPROCESS_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_ML_POSTPROCESS, GstMlPostprocessClass))
#define GST_IS_ML_POSTPROCESS(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_ML_POSTPROCESS))
#define GST_IS_ML_POSTPROCESS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_ML_POSTPROCESS))

/* Same meta names as mlinference for compatibility with amba_venc_overlay */
#define GST_ML_POSTPROCESS_META_NAME "amba_cvflow-object_detector"
#define GST_ML_POSTPROCESS_META_PARAM_NAME "extra-data"
#define GST_ML_POSTPROCESS_META_FIELD_ID "id"
#define GST_ML_POSTPROCESS_META_FIELD_LABEL "label"
#define GST_ML_POSTPROCESS_META_FIELD_SCORE "score"
#define GST_ML_POSTPROCESS_META_FIELD_X_START "x_start"
#define GST_ML_POSTPROCESS_META_FIELD_Y_START "y_start"
#define GST_ML_POSTPROCESS_META_FIELD_X_END "x_end"
#define GST_ML_POSTPROCESS_META_FIELD_Y_END "y_end"

#ifndef DMAX_LABEL_NUM
#define DMAX_LABEL_NUM 1024
#endif

#if (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L) \
    || defined(__aarch64__) \
    || defined(__ARM_FP16_FORMAT_IEEE)
#define MLPP_FP16_USE_NATIVE_HALF 1
#endif

typedef int (*mlpp_post_process_fn)(void *ctx);

typedef struct _GstMlPostprocess GstMlPostprocess;
typedef struct _GstMlPostprocessClass GstMlPostprocessClass;

typedef struct {
  float conf_threshold;
  float nms_threshold;
  int top_k;
  int use_multi_cls;

  char label_file[DMAX_FILE_NAME_LENGTH + 64];
  char labels[DMAX_LABEL_NUM][DMAX_LABEL_LEN];
  unsigned int valid_label_count;

  char model_type_str[256];
  mlpp_post_process_fn func_post_process;  /* Set from postprocess_ops->process */
  const ml_postproc_ops_t *postprocess_ops;  /* Current registered ops, or NULL */

  bounding_boxes_t bboxs;

  /* Resolution for bbox coordinates. Format "WIDTHxHEIGHT", e.g. "1920x1080". */
  char coord_res[32];
  int map_width;
  int map_height;

  /* Dynamic output pads: synced from postprocess_ops->get_output_pads when type is set */
  int output_pad_count;
  struct {
    const ml_postproc_output_pad_spec_t *spec;
    GstPad *pad;
  } output_pads[ML_POSTPROC_MAX_OUTPUT_PADS];

  /** eazyai_postp_ctx: for eazyai-backed types (yolov8_det, etc.), init on first buffer */
  void *eazyai_postp_ctx;
  /** eazyai_postp_ctx_det: for YOLOP only, cached yolo_det_common context */
  void *eazyai_postp_ctx_det;

  /** YOLO inst-seg (yolov8_seg, …): ea_postproc_detection_bbox_t array + per-slot U8 masks (see nn_arm yolov8_seg) */
  void *yolo_seg_det_buf;
  int yolo_seg_buf_slots;
  int yolo_seg_mask_w;
  int yolo_seg_mask_h;
  /** Cached layout for yolov8_seg et al.: ea det H×W + proto dims; mismatch forces ea deinit+reinit */
  guint64 yolo_seg_cfg_sig;
  /** Persistent ea_postproc_detection_config_t + nn_info + init tensor stubs (yolo_seg init). */
  gpointer yolo_seg_ea_cfg_store;
  /** Tensor caps changed vs cached copy; teardown eazyai/stash at next chain start (not in CAPS callback). */
  gboolean tensor_layout_dirty;

  /** Reused scratch for FP16->FP32 pack; size f32_convert_buf_bytes; freed in finalize. */
  float *f32_convert_buf;
  gsize f32_convert_buf_bytes;

  /** Reused buffer: ea_postproc_detection_bbox_t[ea_det_bbox_cap] for eazyai_postprocess det outputs. */
  void *ea_det_bbox_buf;
  int ea_det_bbox_cap;

  /** YOLOv12 det: if caps report N along W and 4+nc along H, transpose to EazyAI [W'=4+nc][H'=N] layout. */
  float *yolo_tpose_f32;
  gsize yolo_tpose_f32_bytes;

  /* Cached tensor caps parse result (avoids re-parsing on every buffer) */
  struct {
    GstCaps *caps;
    int num_tensors;
    int nn_input_w, nn_input_h;
    gsize total_f32_convert;
    struct {
      int w, h, ch;
      gboolean is_float16;
      guint pitch_bytes;
      gsize raw_offset;
      gsize f32_offset;
      char name[ML_POSTPROC_TENSOR_NAME_LEN];
    } desc[AMBA_ML_MAX_TENSORS];
  } tensor_cache;

  char reference_embedding_path[DMAX_FILE_NAME_LENGTH + 64];
  char reference_label[AMBA_ML_CLASSIFICATION_LABEL_LEN];
  float clip_ref_feature[AMBA_ML_EMBEDDING_MAX_DIM];
  uint32_t clip_ref_dim;
  unsigned char clip_ref_valid;
} mlpp_priv_ctx_t;

struct _GstMlPostprocess {
  GstElement parent;

  GstPad *sink_tensor;
  GstPad *src;  /* Single output pad (single-buffer design); NULL when using multi-pad backup */
  mlpp_priv_ctx_t *priv;
};

struct _GstMlPostprocessClass {
  GstElementClass parent_class;
};

GType gst_ml_postprocess_get_type(void);

/** Generic: look up post-processor by type (yolov5, yolox, etc.) and set on context */
int setup_ml_postproc_factory(mlpp_priv_ctx_t *thiz, const char *nn_type);

G_END_DECLS

#endif /* __GST_AMBA_ML_POSTPROCESS_H__ */
