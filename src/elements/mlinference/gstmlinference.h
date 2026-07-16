/*
 * GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2020 Niels De Graef <niels.degraef@gmail.com>
 * Copyright (C) 2022 PengXue Duan <<pxduan@ambarella.com>>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GST_AMBA_ML_INFERENCE_H__
#define __GST_AMBA_ML_INFERENCE_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideofilter.h>

#include "cavalry_ioctl.h"
#include "vproc.h"
#include "nnctrl.h"
#include "cv_vproc.h"
#include "element_common.h"

G_BEGIN_DECLS

#define GST_TYPE_MLINFERENCE (gst_ml_inference_get_type())
#define GST_MLINFERENCE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MLINFERENCE,GstMlInference))
#define GST_MLINFERENCE_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MLINFERENCE,GstMlInferenceClass))
#define GST_MLINFERENCE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_MLINFERENCE,GstMlInferenceClass))
#define GST_IS_MLINFERENCE(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MLINFERENCE))
#define GST_IS_MLINFERENCE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MLINFERENCE))

#define GST_MLINFERENCE_META_NAME "amba_cvflow-object_detector"
#define GST_MLINFERENCE_META_PARAM_NAME "extra-data"
#define GST_MLINFERENCE_META_FIELD_ID "id"
#define GST_MLINFERENCE_META_FIELD_LABEL "label"
#define GST_MLINFERENCE_META_FIELD_SCORE "score"
#define GST_MLINFERENCE_META_FIELD_X_START "x_start"
#define GST_MLINFERENCE_META_FIELD_Y_START "y_start"
#define GST_MLINFERENCE_META_FIELD_X_END "x_end"
#define GST_MLINFERENCE_META_FIELD_Y_END "y_end"


#define DMAX_INPUT_NUM 8
#define DMAX_OUTPUT_NUM 8


#ifndef DMAX_DET_NUM
#define DMAX_DET_NUM   200
#endif

#ifndef DMAX_LABEL_NUM
#define DMAX_LABEL_NUM 183
#endif

#ifndef MAX_CVT_W_SIZE
#define MAX_CVT_W_SIZE 2048
#endif
#ifndef MAX_CVT_H_SIZE
#define MAX_CVT_H_SIZE 2160
#endif

#ifndef ENABLE_CACHE_ON_NET_MEM
#define ENABLE_CACHE_ON_NET_MEM 1
#endif

#ifndef SIGMOID
#define SIGMOID(x)  (1.0 / (1.0 + exp(-(x))))
#endif

#ifndef ALIGN_PITCH
#define ALIGN_PITCH(x) (((x) + (CAVALRY_PORT_PITCH_ALIGN) - 1) & ~((CAVALRY_PORT_PITCH_ALIGN) - 1))
#endif

#ifndef CHECK_PITCH_ALIGN
#define CHECK_PITCH_ALIGN(x) ((x) & ((CAVALRY_PORT_PITCH_ALIGN) - 1))
#endif


#define EDNNTypeName_YoloV5S "yolov5s"
#define EDNNTypeName_YoloV5M "yolov5m"
#define EDNNTypeName_YoloV5L "yolov5l"
#define EDNNTypeName_YoloV5X "yolov5x"

#define EDNNTypeName_YoloV3S "yolov3s"
#define EDNNTypeName_YoloV3M "yolov3m"
#define EDNNTypeName_YoloV3L "yolov3l"
#define EDNNTypeName_YoloV3X "yolov3x"


#define EColorSpaceName_RGB "rgb"
#define EColorSpaceName_BGR "bgr"
#define EColorSpaceName_RGB_ITL "rgb_itl"
#define EColorSpaceName_BGR_ITL "bgr_itl"
#define EColorSpaceName_NV12 "nv12"
#define EColorSpaceName_Y "y"
#define EColorSpaceName_ITL "uv_itl"

#define DEFAULT_IMG_MEM_NUM 2

enum {
    NMS_INIT = 0x0,
    NMS_CHOSEN = 0x1,
    NMS_DISCARD = 0x2,
};


enum EDNNType{
    EDNNType_Invalid = 0x00,

    EDNNType_YoloV5_S = 0x01,
    EDNNType_YoloV5_M = 0x02,
    EDNNType_YoloV5_L = 0x03,
    EDNNType_YoloV5_X = 0x04,

    EDNNType_YoloV3_S = 0x05,
    EDNNType_YoloV3_M = 0x06,
    EDNNType_YoloV3_L = 0x07,
    EDNNType_YoloV3_X = 0x08,

    EDNNType_Customed =  0xff,
};


typedef struct {
  GstVideoFormat format;
  guint width;
  guint height;

  gint n_planes;
  gint stride[GST_VIDEO_MAX_PLANES];

  gpointer data[GST_VIDEO_MAX_PLANES];
} img_data_info_t;

typedef int (*tf_post_process)(void *ctx);


typedef struct _GstMlInference GstMlInference;
typedef struct _GstMlInferenceClass GstMlInferenceClass;

typedef struct
{
  unsigned int channel;

  int sig_flag;

  int cache_en;

  cv_vproc_ctx_t *cv_ctx;
  //vproc memory
  uint8_t *vproc_mem_virt;
  unsigned long vproc_mem_phys;
  unsigned long vproc_mem_size;
  //resize/convert input memory
  uint8_t *img_mem_virt[DEFAULT_IMG_MEM_NUM];
  unsigned long img_mem_phys[DEFAULT_IMG_MEM_NUM];
  unsigned long img_mem_size[DEFAULT_IMG_MEM_NUM];
  uint8_t *nn_out_f32;

  uint32_t dsp_pts;
#ifndef D_OS_AMRTOS
  FILE *dump_fd;
#endif
  char model_file[DMAX_FILE_NAME_LENGTH + 64];
  char label_file[DMAX_FILE_NAME_LENGTH + 64];

  char dump_file[DMAX_FILE_NAME_LENGTH + 64];
  unsigned int dump_num;

  //params for post process
  float conf_threshold;
  float nms_threshold;
  int top_k;
  int use_multi_cls;

  enum EDNNType model_type;
  //color space for nn's input map
  color_space_t color_space; /*!< the color space of the vector */
  unsigned char reserved[2];

  gchar model_type_str[256];
  gchar color_space_str[128];

  unsigned int input_num;
  unsigned int output_num;

  char input_name[DMAX_INPUT_NUM][DMAX_FILE_NAME_LENGTH + 64];
  char output_name[DMAX_OUTPUT_NUM][DMAX_FILE_NAME_LENGTH + 64];

  char labels[DMAX_LABEL_NUM][DMAX_LABEL_LEN];
  unsigned int valid_label_count;

  //nnctrl
  int id;
  int verbose_print;          /*!< The flag to enable verbose print in nnctrl lib, set before calling ::ea_net_load(). */
  int split_num;              /*!< The part number to split the large network so that the small network can get a chance to run, set before calling ::ea_net_forward(). */
  int abort_if_preempted;     /*!< The flag to abort the network without auto resume if other high-priority net preempts, set before calling ::ea_net_forward(). */
  int priority;               /*!< The priority of the network, set before calling ::ea_net_forward(). The range is from 0 to 31. 0:lowest(default), 31:highest. */
  int print_time;             /*!< The flag to print vp_time and arm_time, set before calling ::ea_net_load(). */
  float vp_time_us;           /*!< [out] The time that the network spends in the Vector Processor (VP), updated after calling ::ea_net_forward(). */

  struct net_cfg cfg;
  struct net_input_cfg input_cfg;
  struct net_output_cfg output_cfg;
  struct net_run_cfg run_cfg;
  struct net_result result;
  struct net_mem mem;

  //post process for nn result
  tf_post_process func_post_process;

  bounding_boxes_t bboxs;

  roi_info_t roi;

} priv_ml_infer_ctx_t ;

struct _GstMlInference
{
  GstBaseTransform parent;

  priv_ml_infer_ctx_t *priv_ctx;
};

struct _GstMlInferenceClass
{
  GstBaseTransformClass parent_class;
};

int setup_customized_nn_process_factory(priv_ml_infer_ctx_t *thiz, const char *nn_type);

GType gst_ml_inference_get_type (void);

G_END_DECLS

#endif /* __GST_ML_INFERENCE_H__ */
