/*
 * amba_ml_decoded_result.h
 *
 * Generic ML decoded result definitions for Ambarella GStreamer ML pipeline.
 * Post-processed output (bbox, classification, etc.), NOT raw NN tensors.
 * Supports multiple result types: detection/bbox, classification, segmentation, etc.
 * Reference: nnstreamer tensor_decoder result format.
 *
 * Install path: ${prefix}/include/amba-gst-plugins-1.0/
 * Use: #include <amba_ml_decoded_result.h> with -I${prefix}/include/amba-gst-plugins-1.0
 *
 * Copyright (C) 2022 Ambarella International LP
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef __AMBA_ML_DECODED_RESULT_H__
#define __AMBA_ML_DECODED_RESULT_H__

#include <stdint.h>
#include <gst/gst.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * AMBA_ML_RESULT_API_VERSION - Version of this header / result format
 * Bump when structure layout or semantics change.
 * Consumer should check: AMBA_ML_RESULT_VERSION_OK(header) before use.
 */
#define AMBA_ML_RESULT_API_VERSION 1

/**
 * AMBA_ML_RESULT_VERSION_OK(header) - Check if buffer version matches header
 * Returns 1 if compatible, 0 otherwise.
 * Usage: if (!AMBA_ML_RESULT_VERSION_OK(result.header)) { ... }
 */
#define AMBA_ML_RESULT_VERSION_OK(header) \
  ((header).version == AMBA_ML_RESULT_API_VERSION)

/**
 * AMBA_ML_RESULT_MAGIC - Magic signature identifying NN post-process result buffer
 * Overlay and other consumers can check the first 4 bytes of a buffer:
 * if (*(uint32_t *)buf == AMBA_ML_RESULT_MAGIC) -> NN post-process result
 * Use AMBA_ML_RESULT_IS_VALID(header) for full validation.
 */
#define AMBA_ML_RESULT_MAGIC 0x414D4C52  /* "AMLR" in ASCII */

/**
 * AMBA_ML_RESULT_IS_VALID(header) - Check if buffer is valid NN post-process result
 * Returns 1 if magic and version match, 0 otherwise.
 * Usage: if (!AMBA_ML_RESULT_IS_VALID(result.header)) { ... }
 */
#define AMBA_ML_RESULT_IS_VALID(header) \
  ((header).magic == AMBA_ML_RESULT_MAGIC && AMBA_ML_RESULT_VERSION_OK(header))

/**
 * amba_ml_result_type_t - Result type discriminator
 * Different NN models produce different post-process outputs.
 */
typedef enum {
  AMBA_ML_RESULT_TYPE_DETECTION_BBOX = 0,  /* Object detection (YOLO, SSD, etc.) */
  AMBA_ML_RESULT_TYPE_SEGMENTATION = 1,    /* Decoded GRAY8 mask (width*height), coord_res space */
  AMBA_ML_RESULT_TYPE_CLASSIFICATION = 2,  /* Image classification (logits -> softmax top-k) */
  AMBA_ML_RESULT_TYPE_POSE = 3,            /* Pose estimation (RTMPose, etc.) */
  AMBA_ML_RESULT_TYPE_EMBEDDING = 4,       /* L2-normalized feature vector (CLIP image enc, etc.) */
  AMBA_ML_RESULT_TYPE_CUSTOM = 5,
} amba_ml_result_type_t;

/**
 * amba_ml_result_header_t - Common header for all result types
 * Buffer always starts with this; payload layout depends on type.
 * Overlay checks magic to distinguish NN post-process result from BMP, string, etc.
 */
typedef struct {
  uint32_t magic;          /* AMBA_ML_RESULT_MAGIC - identifies NN post-process result */
  uint32_t type;           /* amba_ml_result_type_t */
  uint32_t version; /* AMBA_ML_RESULT_API_VERSION when buffer was created */
} amba_ml_result_header_t;

/* ========== Detection / Bounding Box (type == AMBA_ML_RESULT_TYPE_DETECTION_BBOX) ========== */

#ifndef AMBA_ML_DETECTION_LABEL_LEN
#define AMBA_ML_DETECTION_LABEL_LEN 128
#endif

#ifndef AMBA_ML_DETECTION_MAX_NUM
#define AMBA_ML_DETECTION_MAX_NUM 200
#endif

#ifndef AMBA_ML_DETECTION_FLAG_HAS_LANDMARKS
/** When set in amba_ml_detection_t.flags: landmark_x/y[] are valid (RetinaFace order: L eye, R eye, nose, L mouth, R mouth). */
#define AMBA_ML_DETECTION_FLAG_HAS_LANDMARKS (1u << 0)
#endif

/**
 * amba_ml_detection_t - Single detection object (bbox)
 * Coordinates are pixel values in the coordinate system defined by
 * caps field coord_res (WIDTHxHEIGHT, e.g. 1920x1080).
 */
typedef struct {
  int32_t class_id;
  float   score;
  /* Non-empty when mlpostprocess has a display string (e.g. from label file); empty => overlay skips text. */
  char    label[AMBA_ML_DETECTION_LABEL_LEN];
  int32_t x_start;
  int32_t y_start;
  int32_t x_end;
  int32_t y_end;
  /** Bitmask (AMBA_ML_DETECTION_FLAG_*). Optional landmarks when HAS_LANDMARKS set. */
  uint32_t flags;
  /** Facial landmarks in coord_res pixels (only if flags & AMBA_ML_DETECTION_FLAG_HAS_LANDMARKS). */
  int32_t landmark_x[5];
  int32_t landmark_y[5];
} amba_ml_detection_t;

/**
 * amba_ml_detection_result_t - Bbox detection result payload (no header)
 * Bbox coordinate resolution is in caps field coord_res (WIDTHxHEIGHT).
 */
typedef struct {
  amba_ml_detection_t detections[AMBA_ML_DETECTION_MAX_NUM];
  uint32_t det_num;
} amba_ml_detection_result_t;

/**
 * amba_ml_bbox_result_t - Full bbox result: header + payload
 * Buffer layout for type DETECTION_BBOX.
 */
typedef struct {
  amba_ml_result_header_t     header;
  amba_ml_detection_result_t detections;
} amba_ml_bbox_result_t;

/* ========== Classification (type == AMBA_ML_RESULT_TYPE_CLASSIFICATION) ========== */

#ifndef AMBA_ML_CLASSIFICATION_LABEL_LEN
#define AMBA_ML_CLASSIFICATION_LABEL_LEN  AMBA_ML_DETECTION_LABEL_LEN
#endif
#ifndef AMBA_ML_CLASSIFICATION_TOPK_MAX
#define AMBA_ML_CLASSIFICATION_TOPK_MAX    16
#endif

typedef struct {
  int32_t class_id;
  float   score;  /* softmax probability */
  char    label[AMBA_ML_CLASSIFICATION_LABEL_LEN];
} amba_ml_classification_rank_t;

/** Payload after header: num_classes equals logits length; top_k_out <= AMBA_ML_CLASSIFICATION_TOPK_MAX */
typedef struct {
  uint32_t num_classes;
  uint32_t top_k_out;
  amba_ml_classification_rank_t ranked[AMBA_ML_CLASSIFICATION_TOPK_MAX];
} amba_ml_classification_body_t;

typedef struct {
  amba_ml_result_header_t       header;
  amba_ml_classification_body_t body;
} amba_ml_classification_result_t;

/* ========== Pose (type == AMBA_ML_RESULT_TYPE_POSE) ========== */

/** COCO body keypoints (RTMPose-s / RTMPose-m, etc.) */
#ifndef AMBA_ML_POSE_KEYPOINT_NUM
#define AMBA_ML_POSE_KEYPOINT_NUM 17
#endif

typedef struct {
  float   score;
  int32_t x;  /* coord_res pixels */
  int32_t y;
} amba_ml_keypoint_t;

typedef struct {
  amba_ml_keypoint_t keypoints[AMBA_ML_POSE_KEYPOINT_NUM];
} amba_ml_pose_body_t;

typedef struct {
  amba_ml_result_header_t header;
  amba_ml_pose_body_t     body;
} amba_ml_pose_result_t;

/* ========== Embedding (type == AMBA_ML_RESULT_TYPE_EMBEDDING) ========== */

/** Max feature dim (CLIP-B 512, CLIP-L / LongCLIP-L 768). */
#ifndef AMBA_ML_EMBEDDING_MAX_DIM
#define AMBA_ML_EMBEDDING_MAX_DIM 768
#endif

typedef struct {
  uint32_t dim;  /* valid length in feature[] (L2-normalized) */
  float    feature[AMBA_ML_EMBEDDING_MAX_DIM];
  unsigned char match_valid;  /* 1 if reference-embedding was loaded and score computed */
  unsigned char reserved[3];
  float    match_score;       /* dot(cur, ref), cosine similarity when both L2-normalized */
  char     match_label[AMBA_ML_CLASSIFICATION_LABEL_LEN];  /* from reference-label property */
} amba_ml_embedding_body_t;

typedef struct {
  amba_ml_result_header_t  header;
  amba_ml_embedding_body_t body;
} amba_ml_embedding_result_t;

/* ========== Segmentation (type == AMBA_ML_RESULT_TYPE_SEGMENTATION) ========== */

/**
 * SEGMENTATION: pure segmentation model output (standalone mask).
 * CUSTOM: model-specific auxiliary (e.g. YOLOP drive_area, lane_line - bundled with DETECTION_BBOX).
 * Both use raw GRAY8 (width*height bytes).
 */

/* ========== GstAmbaMlDecodedMeta - describes each memory in buffer ========== */

#ifndef GST_AMBA_ML_DECODED_META_MAX_ENTRIES
#define GST_AMBA_ML_DECODED_META_MAX_ENTRIES 8
#endif

/**
 * GstAmbaMlDecodedMetaEntry - descriptor for one memory in the buffer
 */
typedef struct {
  guint type;    /* amba_ml_result_type_t */
  gsize offset;  /* offset in buffer */
  gsize size;    /* size in bytes */
  guint width;   /* SEGMENTATION/CUSTOM: mask; 0 for bbox */
  guint height;
} GstAmbaMlDecodedMetaEntry;

/**
 * GstAmbaMlDecodedMeta - describes each memory for unified access
 * Attached to application/x-amba-ml-decoded buffers with multiple memories.
 */
typedef struct {
  GstMeta meta;
  guint n_entries;
  GstAmbaMlDecodedMetaEntry entries[GST_AMBA_ML_DECODED_META_MAX_ENTRIES];
} GstAmbaMlDecodedMeta;

GType gst_amba_ml_decoded_meta_api_get_type(void);
const GstMetaInfo *gst_amba_ml_decoded_meta_get_info(void);

#define GST_AMBA_ML_DECODED_META_API_TYPE (gst_amba_ml_decoded_meta_api_get_type())
#define GST_AMBA_ML_DECODED_META_INFO (gst_amba_ml_decoded_meta_get_info())
#define gst_buffer_get_amba_ml_decoded_meta(b) \
  ((GstAmbaMlDecodedMeta *)gst_buffer_get_meta((b), GST_AMBA_ML_DECODED_META_API_TYPE))

/* ========== GStreamer caps ========== */

/* Post-processed/decoded output (bbox, classification, etc.), NOT raw NN tensors */
#define GST_AMBA_ML_DECODED_CAPS "application/x-amba-ml-decoded"

/* Caps field: result_type (string) - "detection_bbox", "classification", etc. */
#define GST_AMBA_ML_RESULT_TYPE_DETECTION_BBOX "detection_bbox"
#define GST_AMBA_ML_RESULT_TYPE_CLASSIFICATION  "classification"
#define GST_AMBA_ML_RESULT_TYPE_SEGMENTATION   "segmentation"
#define GST_AMBA_ML_RESULT_TYPE_POSE           "pose"
#define GST_AMBA_ML_RESULT_TYPE_EMBEDDING      "embedding"
#define GST_AMBA_ML_RESULT_TYPE_CUSTOM        "custom"

/* Caps field: coord_res (string) - resolution for bbox coordinates, format WIDTHxHEIGHT (e.g. "1920x1080") */
/* Output types and seg resolution: use GstAmbaMlDecodedMeta entries[].type/width/height per memory */

/* Legacy aliases */
#define GST_AMBA_ML_RESULT_CAPS    GST_AMBA_ML_DECODED_CAPS

/**
 * Buffer layout (DETECTION_BBOX):
 * - amba_ml_result_header_t
 * - amba_ml_detection_result_t (detections[], det_num)
 * - Bbox coords are pixels in coord_res space (from caps)
 * - Optional: detections[].flags / landmark_x/y[] when HAS_LANDMARKS (e.g. nn_type retinaface).
 *
 * Buffer layout (CLASSIFICATION):
 * - amba_ml_classification_result_t (header.type == AMBA_ML_RESULT_TYPE_CLASSIFICATION)
 * - body.ranked[0 .. top_k_out-1] sorted by descending score
 *
 * Buffer layout (POSE):
 * - amba_ml_pose_result_t (header.type == AMBA_ML_RESULT_TYPE_POSE)
 * - body.keypoints[0 .. AMBA_ML_POSE_KEYPOINT_NUM-1] in coord_res pixels
 *
 * Buffer layout (EMBEDDING):
 * - amba_ml_embedding_result_t (header.type == AMBA_ML_RESULT_TYPE_EMBEDDING)
 * - body.dim + body.feature[0..dim-1] L2-normalized (dot product == cosine similarity)
 * - body.match_score / match_label when mlpostprocess reference-embedding is set
 */

#ifdef __cplusplus
}
#endif

#endif /* __AMBA_ML_DECODED_RESULT_H__ */
