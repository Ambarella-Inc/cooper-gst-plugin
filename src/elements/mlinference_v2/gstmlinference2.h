/*
 * gstmlinference2.h
 *
 * History:
 *    1/6/2026 - [pxduan] created file
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
 * SECTION:element-mlinference2
 * @title: mlinference2
 * @see_also: mlpostprocess, amba_draw_data_gen
 *
 * Runs ML inference with cvflow (no built-in preprocess/postprocess).
 * Preprocess: amba_videoscale/amba_img_cvt or videoscale/videoconvert. Postprocess: mlpostprocess.
 * Output: application/x-amba-ml-tensors. Downstream: mlpostprocess.
 */

#ifndef __GST_AMBA_ML_INFERENCE2_H__
#define __GST_AMBA_ML_INFERENCE2_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideofilter.h>

#include "cavalry_ioctl.h"
#include "vproc.h"
#include "nnctrl.h"
#include "cv_vproc.h"
#include "element_common.h"
#include "ml_tensors_caps.h"

G_BEGIN_DECLS

#define GST_TYPE_MLINFERENCE2 (gst_ml_inference2_get_type())
#define GST_MLINFERENCE2(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MLINFERENCE2,GstMlInference2))
#define GST_MLINFERENCE2_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MLINFERENCE2,GstMlInference2Class))
#define GST_MLINFERENCE2_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_MLINFERENCE2,GstMlInference2Class))
#define GST_IS_MLINFERENCE2(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MLINFERENCE2))
#define GST_IS_MLINFERENCE2_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MLINFERENCE2))

#ifndef ENABLE_CACHE_ON_NET_MEM
#define ENABLE_CACHE_ON_NET_MEM 1
#endif

#ifndef ALIGN_PITCH
#define ALIGN_PITCH(x) (((x) + (CAVALRY_PORT_PITCH_ALIGN) - 1) & ~((CAVALRY_PORT_PITCH_ALIGN) - 1))
#endif

#ifndef CHECK_PITCH_ALIGN
#define CHECK_PITCH_ALIGN(x) ((x) & ((CAVALRY_PORT_PITCH_ALIGN) - 1))
#endif

typedef struct {
  GstVideoFormat format;
  guint width;
  guint height;

  gint n_planes;
  guint stride[GST_VIDEO_MAX_PLANES];

  gpointer data[GST_VIDEO_MAX_PLANES];
} img_data_info2_t;

typedef struct _GstMlInference2 GstMlInference2;
typedef struct _GstMlInference2Class GstMlInference2Class;

typedef struct
{
  /*! When TRUE (default), use nnctrl MFD APIs; otherwise legacy phys-addr APIs. */
  gboolean use_mfd;
  gboolean nn_freed;

  int cache_en;

  cv_vproc_ctx_t *cv_ctx;
  // intermediate buffer for data type conversion (uint8 <-> float32)
  uint8_t *conv_buf_virt;
  unsigned long conv_buf_size;
  int conv_buf_fd;
  // allocated input memory (for copy mode, separate from in_desc which may point to upstream)
  uint8_t *input_mem_virt;
  unsigned long input_mem_size;
  int input_mem_fd;
  unsigned long input_mem_phys;
  // intermediate buffer phys (legacy non-mfd path)
  unsigned long conv_buf_phys;

  char model_file[DMAX_FILE_NAME_LENGTH + 64];


  unsigned int input_num;
  unsigned int output_num;

  char input_name[MIN(MAX_IO_NUM, AMBA_ML_MAX_TENSORS)][DMAX_FILE_NAME_LENGTH + 64];
  char output_name[MIN(MAX_IO_NUM, AMBA_ML_MAX_TENSORS)][DMAX_FILE_NAME_LENGTH + 64];

  /* Source data format (sign.datasize.exp_offset.exp_bits). Conversion applied when differs from model */
  char in_data_fmt[64];

  //nnctrl
  int id;
  int verbose_print;          /*!< The flag to enable verbose print in nnctrl lib, set before calling ::ea_net_load(). */
  int split_num;              /*!< The part number to split the large network so that the small network can get a chance to run, set before calling ::ea_net_forward(). */
  int abort_if_preempted;     /*!< The flag to abort the network without auto resume if other high-priority net preempts, set before calling ::ea_net_forward(). */
  int priority;               /*!< The priority of the network, set before calling ::ea_net_forward(). The range is from 0 to 31. 0:lowest(default), 31:highest. */
  int print_time;             /*!< The flag to print vp_time and arm_time, set before calling ::ea_net_load(). */
  float vp_time_us;           /*!< [out] The time that the network spends in the Vector Processor (VP), updated after calling ::ea_net_forward(). */

  /*! After a zero-copy dmabuf frame, next non-ZC frame must push model row pitch again. */
  gboolean nn_prev_was_zero_copy_dmabuf;

  struct net_cfg cfg;
  struct net_input_mfd_cfg input_cfg;
  struct net_output_mfd_cfg output_cfg;
  struct net_run_cfg run_cfg;
  struct net_result result;
  struct cavalry_mfd_desc mem;

  /* Legacy nnctrl IO (when use_mfd is FALSE) */
  struct net_input_cfg io_input_cfg;
  struct net_output_cfg io_output_cfg;
  struct net_mem io_mem;
  /* Per-frame legacy run output (avoid ~32KiB struct on stack; MAX_IO_NUM=128). */
  struct net_output_cfg legacy_run_out;
} priv_ml_infer_ctx2_t ;

struct _GstMlInference2
{
  GstBaseTransform parent;

  priv_ml_infer_ctx2_t *priv_ctx;
  /*!< Output tensor buffer pool (cavalry phys or mfd); size matches ml2_output_alloc_size. */
  GstBufferPool *out_pool;
  gsize out_pool_size;
  /*!< One-shot stderr (g_printerr): first NN input path diag (copy vs zero-copy vs bad fd). */
  gboolean logged_input_path_once;
  /*!< One-shot stderr (g_printerr): first NN output path diag (zero-copy vs copy / bad fd). */
  gboolean logged_output_path_once;
};

struct _GstMlInference2Class
{
  GstBaseTransformClass parent_class;
};

GType gst_ml_inference2_get_type (void);

G_END_DECLS

#endif /* __GST_ML_INFERENCE_H__ */
