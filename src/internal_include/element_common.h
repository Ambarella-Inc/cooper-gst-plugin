/*
 * element_common.h
 *
 * History:
 *    5/18/2022 - [Zhi He] created file
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

#ifndef __ELEMENT_COMMON_H__
#define __ELEMENT_COMMON_H__

// flags between elements, in user data

#define D_MAX_DET_NUM 200

//ambavencap meta
#define GST_VENCCAP_META_NAME "GstAmbaVenccapMeta"
#define GST_VENCCAP_META_PARAM_NAME "stream_info"
#define GST_VENCCAP_META_FIELD_STREAM_ID "stream_idx"
#define GST_VENCCAP_META_FIELD_STREAM_FORMAT "stream_fmt"
#define GST_VENCCAP_META_FIELD_KEY_FRAME "key_frame"
#define GST_VENCCAP_META_FIELD_FRAME_START "is_frame_start"
#define GST_VENCCAP_META_FIELD_FORMAT_CHANGE "is_format_changed"
#define GST_VENCCAP_META_FIELD_EXTRADATA "with_extradata"
#define GST_VENCCAP_META_FIELD_FPS_N "fps_n"
#define GST_VENCCAP_META_FIELD_FPS_D "fps_d"


typedef struct {
  unsigned long stream_idx : 8;
  unsigned long stream_fmt : 8;

  unsigned long is_key_frame : 1;
  unsigned long with_extradata : 1; // vps/sps/pps
  unsigned long is_frame_start : 1;
  unsigned long is_format_changed : 1;

  unsigned long reserved0 : 44;
} stream_info_t;

typedef union {
  stream_info_t info_v;
  unsigned long ul_v;
} shared_stream_info_u;

typedef struct {
  float score;
  int id;
  char label[DMAX_LABEL_LEN];
  float x_start; // normalized value
  float y_start;
  float x_end;
  float y_end;
  /** RetinaFace 5 landmarks x,y in nn_input pixel space (see amba_ml_detection_t order); valid if has_landmarks */
  float landmark[10];
  unsigned char has_landmarks;

} det_object_t;

typedef struct {
  det_object_t detections[D_MAX_DET_NUM];
  unsigned int det_num;
} bounding_boxes_t;

typedef struct {
  unsigned int dsp_pts;
  unsigned long mono_pts;
} timestamp_info_t;

typedef struct {
  unsigned int height;
  unsigned int width;
  unsigned int pitch;
  unsigned int yuv_fmt;

  unsigned int dsp_pts;
  unsigned long mono_pts;
} yuv_info_t;

typedef struct {
  unsigned int x;
  unsigned int y;
  unsigned int w;
  unsigned int h;
} roi_info_t;

typedef struct stream_param_s {
  int stream_type;
  int stream_state;
  int enc_src_id;
  int encode_width;
  int encode_height;
} stream_param_t;

#endif

