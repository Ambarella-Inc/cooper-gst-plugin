/*
 * GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
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

/**
 * SECTION:element-ml_inference
 *
 * prepare on EVK
 * // initialize
 * init.sh --imx274_mipi
 * test_aaa_service -a &
 * test_encode --hdmi 1080p --resource-cfg /root/dualvin_sixstreams.lua
 * // set cavalry
 * modprobe cavalry
 * cavalry_load -f /lib/firmware/cavalry.bin -r
 *
 *
 * FIXME:Describe ml_inference here.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! ml_inference ! fakesink silent=TRUE
 * ]|
 * yolov5
 * |[
 * gst-launch-1.0 amba_camsrc buf-id = 0 ! mlinference in_name = images out_name = 1037 out_name = 1017 out_name = 997 \
 * label = ./win/nn/in/coco_class_names.txt model = ./win/nn/model/onnx_yolov5s_cavalry.bin \
 * type = yolov5s conf_threshold = 0.25 nms = 0.45 top_k = 100 ! queue ! amba_venc_overlay stream_id = 0 alpha = 0 \
 * font = arial.ttf clut_start = 22 clut_end = 70 color_number = 300 score_lmt = 0.25
 * ]|
 * tiny yolov3
 * |[
 * gst-launch-1.0 amba_camsrc buf-id = 0 ! mlinference in_name = data out_name = layer115-conv out_name = layer125-conv \
 * label = ./win/nn/in/live_voc_labels.txt model = ./win/nn/model/yolov3_fastest_xl_cavalry.bin \
 * type = yolov3x conf_threshold = 0.8 nms = 0.3 top_k = 100 ! queue ! amba_venc_overlay stream_id = 0 alpha = 0 \
 * font = arial.ttf clut_start = 22 clut_end = 70 color_number = 300 score_lmt = 0.8
 *
 * below just running forward without label file or postprocess now
 * |[
 * gst-launch-1.0 amba_camsrc buf-id = 0 ! mlinference in_name = input out_name = 672 \
 * label = ./win/cavalry_model/nn/in/coco_ids.yaml model = ./win/cavalry_model/nn/model/efficientnet_cavalry.bin \
 * type = efficientnet conf_threshold = 0.2 nms = 0.5 top_k = 100 ! autovideosink
 * ]|
 * |[
 * gst-launch-1.0 amba_camsrc buf-id = 0 ! mlinference in_name = image_arrays out_name = concat_1 out_name = Sigmoid \
 * label = ./win/cavalry_model/nn/in/coco_ids.yaml model = ./win/cavalry_model/nn/model/cavalry_efficientdet_lite0.bin \
 * type = efficientdet_lite0 conf_threshold = 0.2 nms = 0.5 top_k = 100 ! autovideosink
 * ]|
 * |[
 * gst-launch-1.0 amba_camsrc buf-id = 0 ! mlinference in_name = serving_default_input out_name = StatefulPartitionedCall \
 * label = ./win/cavalry_model/nn/in/coco_ids.yaml model = ./win/cavalry_model/nn/model/cavalry_movenet_lightning.bin \
 * type = movenet_lightning conf_threshold = 0.2 nms = 0.5 top_k = 100 ! autovideosink
 * ]|
 * |[
 * gst-launch-1.0 amba_camsrc buf-id = 0 ! mlinference in_name = serving_default_input out_name = StatefulPartitionedCall \
 * label = ./win/cavalry_model/nn/in/coco_ids.yaml model = ./win/cavalry_model/nn/model/cavalry_movenet_thunder.bin \
 * type = movenet_thunder conf_threshold = 0.2 nms = 0.5 top_k = 100 ! autovideosink
 * ]|
 * |[
 * gst-launch-1.0 amba_camsrc buf-id = 0 ! mlinference in_name = input.1 out_name = 424 \
 * label = ./win/cavalry_model/nn/in/coco_ids.yaml model = ./win/cavalry_model/nn/model/onnx_fast_depth_cavalry.bin \
 * type = onnx_fast_depth conf_threshold = 0.2 nms = 0.5 top_k = 100 ! autovideosink
 * ]|
 * </refsect2>
 */

#include <gst/video/gstvideometa.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <stdint.h>
#include <stdio.h>
#include <ctype.h>
#include <wchar.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <linux/fb.h>
#include <fcntl.h>
#include <dirent.h>
#include <pthread.h>
#include <math.h>
#include <float.h>

#include "cavalry_mem.h"
#include "nnctrl.h"
#include "vproc.h"

#include "internal.h"
#include "debug_log.h"
#include "gstmlinference.h"

GST_DEBUG_CATEGORY_STATIC (gst_ml_inference_debug);
#define GST_CAT_DEFAULT gst_ml_inference_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_IN_NAME,
  PROP_OUT_NAME,
  PROP_LABEL,
  PROP_DUMP_FILE,
  PROP_MODEL,
  PROP_TYPE,
  PROP_CONF_THRESHOLD,
  PROP_NMS,
  PROP_TOP_K,
  PROP_USE_MULTI_CLS,
  PROP_COLOR_SPACE,
  PROP_ROI
};

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ RGBP, NV12, RGB, RGBP, BGR, BGRP }"))
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ RGBP, NV12, RGB, BGR, BGRP }"))
    );

#define gst_ml_inference_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstMlInference, gst_ml_inference, GST_TYPE_BASE_TRANSFORM,
  GST_DEBUG_CATEGORY_INIT(gst_ml_inference_debug, "mlinference", 0,
  "mlinference"));

static void gst_ml_inference_finalize (GObject * object);
static void gst_ml_inference_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_ml_inference_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static gboolean gst_ml_inference_start (GstBaseTransform *trans);
static gboolean gst_ml_inference_stop (GstBaseTransform *trans);
static GstFlowReturn gst_ml_inference_transform_ip (GstBaseTransform *
    trans, GstBuffer * buf);
static gboolean gst_ml_inference_create_session (GstBaseTransform * trans);
static gboolean gst_ml_inference_process (GstBaseTransform * trans,
    GstBuffer * buf);

static void __net_io_config(priv_ml_infer_ctx_t *params)
{
  for (unsigned int i = 0; i < params->input_num; i++) {
    params->input_cfg.in_desc[i].name = params->input_name[i];
    params->input_cfg.in_desc[i].no_mem = 1; // allocate memory for input outside nnctrl lib
  }
  params->input_cfg.in_num = params->input_num;

  for (unsigned int i = 0; i < params->output_num; i++) {
    params->output_cfg.out_desc[i].name = params->output_name[i];
    params->output_cfg.out_desc[i].no_mem = 1; // allocate memory for input outside nnctrl lib
  }
  params->output_cfg.out_num = params->output_num;
}

static void __net_free(priv_ml_infer_ctx_t *params)
{
  unsigned long size;
  unsigned long phys_addr;
  unsigned int i;

  for (i = 0; i < params->input_cfg.in_num; i++) {
    if (params->input_cfg.in_desc[i].virt) {
      cavalry_mem_free(params->input_cfg.in_desc[i].size, params->input_cfg.in_desc[i].addr,
          params->input_cfg.in_desc[i].virt);
      params->input_cfg.in_desc[i].virt = NULL;
      params->input_cfg.in_desc[i].size = 0;
    }
  }
  for (i = 0; i < params->output_cfg.out_num; i++) {
    if (params->output_cfg.out_desc[i].virt) {
      cavalry_mem_free(params->output_cfg.out_desc[i].size, params->output_cfg.out_desc[i].addr,
          params->output_cfg.out_desc[i].virt);
      params->output_cfg.out_desc[i].virt = NULL;
      params->output_cfg.out_desc[i].size = 0;
    }
  }

  if (params->mem.mem_size > 0) {
    size = params->mem.mem_size;
    phys_addr = params->mem.phy_addr;
    cavalry_mem_free(size, phys_addr, params->mem.virt_addr);
    params->mem.virt_addr = NULL;
    params->mem.mem_size = 0;
  }

  for (i = 0; i < DEFAULT_IMG_MEM_NUM; i++) {
    if (params->img_mem_size[i] > 0) {
      cavalry_mem_free(params->img_mem_size[i], params->img_mem_phys[i], params->img_mem_virt[i]);
      params->img_mem_virt[i] = NULL;
      params->img_mem_size[i] = 0;
    }
  }

  if (params->nn_out_f32) {
      g_free(params->nn_out_f32);
      params->nn_out_f32 = NULL;
  }

  if (params->id >= 0) {
    nnctrl_exit_net(params->id);
    params->id = -1;
  }

}


static int __net_load(priv_ml_infer_ctx_t *params, int max_batch)
{
  int rval = 0;
  unsigned long size = 0, max_size = 0;
  unsigned long phys_addr = 0;
  size_t shape[4];
  size_t pitch = 0;
  unsigned int i = 0;

  do {
    params->cfg.verbose = params->verbose_print;
    params->cfg.print_time = params->print_time;
    params->cfg.net_file = params->model_file;

    params->cfg.reuse_mem = 1;

    params->cfg.net_loop_cnt = max_batch;
    params->id = -1;
    params->id = nnctrl_init_net(&params->cfg, &params->input_cfg, &params->output_cfg);
    if (params->id < 0) {
        DPRINT_ERROR("nnctrl_init_net failed on %s\n", params->cfg.net_file);
        rval = -1;
        break;
    }

    // allocate memory for network
    size = params->cfg.net_mem_total;
#if ENABLE_CACHE_ON_NET_MEM
    rval = cavalry_mem_alloc(&size, &phys_addr,
        (void **) & (params->mem.virt_addr), params->cache_en);
#else
    rval = cavalry_mem_alloc(&size, &phys_addr, // &size and &phy_addr should be unsigned long *
        (void **) & (params->mem.virt_addr), 0);
#endif
    if (rval < 0) {
      DPRINT_ERROR("cavalry_mem_alloc_retry error\n");
      rval = -1;
      break;
    } else {
      if (params->mem.virt_addr == NULL || size < params->cfg.net_mem_total) {
        DPRINT_ERROR("alloc cv mem is NULL\n");
        rval = -1;
        break;
      }
    }

    params->mem.mem_size = size;
    params->mem.phy_addr = phys_addr;

    DPRINT_INFO("net use cavalry memory total 0x%lX bytes\n", (unsigned long)params->mem.mem_size);

    for (i = 0; i < params->input_cfg.in_num; i++) {

      shape[0] = params->input_cfg.in_desc[i].dim.plane * max_batch;
      if (params->input_cfg.in_desc[i].dim.dram_fmt == 1) {
        shape[1] = 1;
        shape[2] = params->input_cfg.in_desc[i].dim.height;
        shape[3] = params->input_cfg.in_desc[i].dim.width * params->input_cfg.in_desc[i].dim.depth;
      } else {
        shape[1] = params->input_cfg.in_desc[i].dim.depth;
        shape[2] = params->input_cfg.in_desc[i].dim.height;
        shape[3] = params->input_cfg.in_desc[i].dim.width;
      }
      pitch = params->input_cfg.in_desc[i].dim.pitch;
      if (pitch == 0) {
        if (params->input_cfg.in_desc[i].data_fmt.size == 0) {
          pitch = DROUND_UP(shape[3] * sizeof(unsigned char), CAVALRY_PORT_PITCH_ALIGN);
        } else if (params->input_cfg.in_desc[i].data_fmt.size == 2) {
          pitch = DROUND_UP(shape[3] * sizeof(float), CAVALRY_PORT_PITCH_ALIGN);
        } else {
          DPRINT_ERROR("net input only supports 8-bits and 32-bits data by now\n");
          rval = -1;
          break;
        }
        params->input_cfg.in_desc[i].dim.pitch = pitch;
      }
      size = shape[0] * shape[1] * shape[2] * pitch;
      if (cavalry_mem_alloc(&size, &(phys_addr), (void **) & (params->input_cfg.in_desc[i].virt), params->cache_en) < 0) {
        DPRINT_ERROR("input memory error\n");
        rval = -1;
        break;
      }
      params->input_cfg.in_desc[i].size = size;
      params->input_cfg.in_desc[i].addr = phys_addr;//ea_tensor_mfd_for_read(params->input_data_nodes[i].tensor, EA_VP);
      DPRINT_INFO("net input %u use cavalry mem total 0x%lX bytes\n", i, size);
    }

    for (i = 0; i < params->output_cfg.out_num; i++) {
      shape[0] = params->output_cfg.out_desc[i].dim.plane * max_batch;
      shape[1] = params->output_cfg.out_desc[i].dim.depth;
      shape[2] = params->output_cfg.out_desc[i].dim.height;
      shape[3] = params->output_cfg.out_desc[i].dim.width;
      pitch = params->output_cfg.out_desc[i].dim.pitch;
      if (pitch == 0) {
        if (params->output_cfg.out_desc[i].data_fmt.size == 1) {
          pitch = DROUND_UP(shape[3] * sizeof(unsigned short), CAVALRY_PORT_PITCH_ALIGN);
        } else if (params->output_cfg.out_desc[i].data_fmt.size == 2) {
          pitch = DROUND_UP(shape[3] * sizeof(float), CAVALRY_PORT_PITCH_ALIGN);
        } else {
          DPRINT_ERROR("net output only supports 16-bits and 32-bits data by now\n");
          rval = -1;
          break;
        }
        params->output_cfg.out_desc[i].dim.pitch = pitch;
      }

      size = shape[0] * shape[1] * shape[2] * pitch;
      if (cavalry_mem_alloc(&size, &(phys_addr), (void **) & (params->output_cfg.out_desc[i].virt), params->cache_en) < 0) {
        DPRINT_ERROR("input memory error\n");
        rval = -1;
        break;
      }
      params->output_cfg.out_desc[i].size = size;
      params->output_cfg.out_desc[i].addr = phys_addr;
      DPRINT_INFO("net output %u use cavalry mem total 0x%lX bytes\n", i, size);

      size = shape[0] * shape[1] * shape[2] * shape[3] * sizeof(float);
      if (max_size < size) {
          max_size = size;
      }
    }

    if (params->output_cfg.out_desc[0].data_fmt.size == 1) {
        params->nn_out_f32 = g_malloc(max_size);
        if (params->nn_out_f32 == NULL) {
            DPRINT_ERROR("nn_out_f32 memory error\n");
            rval = -1;
            break;
        }
    }

    // load network to memory
    if (nnctrl_load_net(params->id, &params->mem, &params->input_cfg, &params->output_cfg) < 0) {
      printf("nnctrl_load_net failed\n");
      rval = -1;
      break;
    }
#if ENABLE_CACHE_ON_NET_MEM
    if (params->cache_en) {
      cavalry_mem_sync_cache(params->mem.mem_size, params->mem.phy_addr, 1, 0); // clean data in CPU cache after load
    }
#endif
  } while (0);

  if (rval < 0) {
    __net_free(params);
  }

  return rval;
}

static int get_label_from_file(const char *label_file, char (*labels)[DMAX_LABEL_LEN], guint *label_num)
{
  int rval = 0;
  guint label_count = 0;
  gint len = 0, file_len = 0;
  FILE *fp_label = NULL;
  char *label_line_start = NULL;
  char *label_line_end = NULL;
  char label_line[DMAX_LABEL_LEN];

  do {
    // load label from file
    fp_label = fopen(label_file, "r");
    if (fp_label == NULL) {
      DPRINT_ERROR("can't open label_file[%s] !\n", label_file);
      rval = -1;
      break;
    }

    rval = fseek(fp_label, 0L, SEEK_END);
    if (rval != 0) {
      DPRINT_ERROR("fseek to end failed !\n");
      rval = -1;
      break;
    }
    file_len = ftell(fp_label);
    if (file_len <= 0) {
      DPRINT_ERROR("Error: get file size is [%d]!\n", file_len);
      rval = -1;
      break;
    }
    rval = fseek(fp_label, 0L, SEEK_SET);
    if (rval != 0) {
      DPRINT_ERROR("fseek to start failed !\n");
      rval = -1;
      break;
    }

    while (len != file_len) {
      memset(label_line, 0, DMAX_LABEL_LEN);
      if (fgets(label_line, DMAX_LABEL_LEN, fp_label) == NULL) { //Read a line
        DPRINT_ERROR("fgets error !\n");
        rval = -1;
        break;
      }
      len = ftell(fp_label);

      // YOLOV3_MAX_LABEL_LEN is too small, fp_label may be truncated.
      if (strlen(label_line) >= DMAX_LABEL_LEN - 1) {
        DPRINT_ERROR("DMAX_LABEL_LEN[%d] is too small, fp_label may be truncated!\n", DMAX_LABEL_LEN);
        rval = -1;
        break;
      }

      //remove '\''
      label_line_start = strchr(label_line, '\'');
      label_line_end = strrchr(label_line, '\'');
      if (label_line_start && label_line_end && (label_line_end > label_line_start + 1)) {
        *label_line_end = '\0';
        snprintf(labels[label_count], DMAX_LABEL_LEN, "%s", label_line_start + 1);
        label_count++;
      } else {
        label_line_start = strchr(label_line, ':');
        label_line_end = strrchr(label_line, '\r');
        if (label_line_end == NULL) {
          label_line_end = strrchr(label_line, '\n');
        }
        if (label_line_start && label_line_end
            && (label_line_end > label_line_start + 1)) {
          *label_line_end = '\0';
          snprintf(labels[label_count], DMAX_LABEL_LEN, "%s", label_line_start + 1);
          label_count++;
        } else {
          DPRINT_ERROR("[Warning] %s don't satisfy label's format, it will be ignored !\n", label_line);
          continue;
        }
      }

      if ((label_count >= DMAX_LABEL_NUM) && (len != file_len)) {
        DPRINT_ERROR("The number of label exceeds the maximum %d !\n",DMAX_LABEL_NUM);
        rval = -1;
        break;
      }
    }

    if (rval < 0) {
      break;
    }

    *label_num = label_count;
  } while (0);

  if (fp_label) {
    fclose(fp_label);
    fp_label = NULL;
  }

  return rval;
}

static int __net_init(priv_ml_infer_ctx_t *params)
{
  int rval = 0;

  if (params) {
    do {

      params->verbose_print = 0;
      params->split_num = 0;
      params->abort_if_preempted = 0;
      params->priority = 0;
      params->cache_en = 1;

      __net_io_config(params);
      __net_load(params, 1/*max_batch*/);

      // get label from file
      if (params->func_post_process) {
        if (strlen(params->label_file) != 0) {
          if (get_label_from_file(params->label_file, params->labels,
                &params->valid_label_count) < 0) {
            DPRINT_ERROR("get_label_from_file for nn failed! \n");
            rval = -1;
            break;
          }
        } else {
          DPRINT_ERROR("no label file\n");
          rval = -1;
          break;
        }
        DPRINT_INFO("label num: %d\n", params->valid_label_count);
      }

    } while (0);
  } else {
    DPRINT_ERROR("params error\n");
    rval = -1;
  }


  if (rval < 0) {
    if (params) {
      __net_free(params);
    }
  }

  return rval;
}

/* initialize the ml_inference's class */
static void
gst_ml_inference_class_init (GstMlInferenceClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstBaseTransformClass *basetransform_class = (GstBaseTransformClass *) klass;

  gobject_class->finalize = gst_ml_inference_finalize;
  gobject_class->set_property = gst_ml_inference_set_property;
  gobject_class->get_property = gst_ml_inference_get_property;

  basetransform_class->start = gst_ml_inference_start;
  basetransform_class->stop = gst_ml_inference_stop;
  basetransform_class->transform_ip =
      GST_DEBUG_FUNCPTR (gst_ml_inference_transform_ip);

  g_object_class_install_property (gobject_class, PROP_IN_NAME,
      g_param_spec_string ("in_name", "InName", "input feature name ?",
          NULL, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_OUT_NAME,
      g_param_spec_string ("out_name", "OutName", "output feature name ?",
          NULL, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_LABEL,
      g_param_spec_string ("label", "LabelFile", "label file name ?",
          NULL, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_DUMP_FILE,
      g_param_spec_string ("dump", "DumpFile", "dump file name ?",
          NULL, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_MODEL,
      g_param_spec_string ("model", "ModelFile", "model file name ?",
          NULL, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_TYPE,
      g_param_spec_string ("type", "ModelType", "model type ?",
          NULL, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_CONF_THRESHOLD,
      g_param_spec_float ("conf_threshold", "ConfThreshold", "conf threshold ?",
          0, 1, 0.2, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_NMS,
      g_param_spec_float ("nms", "NMS", "nms threshold ?",
          0, 1, 0.5, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_TOP_K,
      g_param_spec_int ("top_k", "TopK", "top k ?",
          0, INT_MAX, 100, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_USE_MULTI_CLS,
      g_param_spec_int ("use_multi_cls", "UseMultiCls", "use multiple classes ?",
          0, 1, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_COLOR_SPACE,
      g_param_spec_string ("color_space", "ColorSpace", "color space of input ?",
          "rgb", G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_ROI,
      g_param_spec_string ("roi", "ROI", "roi of input ?",
          NULL, G_PARAM_READWRITE));


  gst_element_class_set_static_metadata (gstelement_class,
      "Amba Machine Learning Inference",
      "mlinfernece",
      "Inference of cvflow for machine learning",
      "PengXue Duan <<pxduan@ambarella.com>>");

  gst_element_class_add_static_pad_template (gstelement_class, &src_factory);
  gst_element_class_add_static_pad_template (gstelement_class, &sink_factory);
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad callback functions
 * initialize instance structure
 */
static void
gst_ml_inference_init (GstMlInference * in_filter)
{
  priv_ml_infer_ctx_t * filter;
  filter = (priv_ml_infer_ctx_t *) malloc(sizeof(priv_ml_infer_ctx_t));
  memset(filter, 0x0, sizeof(priv_ml_infer_ctx_t));
  in_filter->priv_ctx = filter;

  filter->model_type = EDNNType_Invalid;
  filter->color_space = CS_RGB;
  filter->conf_threshold = 0.2;
  filter->nms_threshold = 0.3;
  filter->top_k = 200;
  filter->use_multi_cls = 0;
  filter->input_num = 0;
  filter->output_num = 0;

  filter->dump_num = 1;

  filter->cv_ctx = acquire_cv_vproc_ctx(1, 0);

}

static color_space_t get_color_space(const char *color_space)
{
  if (!strcmp(color_space, EColorSpaceName_RGB)) {
    return CS_RGB;
  } else if (!strcmp(color_space, EColorSpaceName_BGR)) {
    return CS_BGR;
  } else if (!strcmp(color_space, EColorSpaceName_RGB_ITL)) {
    return CS_RGB_ITL;
  } else if (!strcmp(color_space, EColorSpaceName_BGR_ITL)) {
    return CS_BGR_ITL;
  } else if (!strcmp(color_space, EColorSpaceName_NV12)) {
    return CS_NV12;
  } else if (!strcmp(color_space, EColorSpaceName_Y)) {
    return CS_Y;
  } else if (!strcmp(color_space, EColorSpaceName_ITL)) {
    return CS_ITL;
  } else {
    DPRINT_ERROR("unsupported color_space(%s)\n", color_space);
    return CS_VECT;
  }

}

static int parse_roi (const char *custom_properties, roi_info_t *roi)
{
  if (custom_properties) {
    char **options;
    unsigned int len = 0;

    options = g_strsplit (custom_properties, ".", -1);
    len = g_strv_length (options);

    if (len == 2) {
      roi->x = 0;
      roi->y = 0;
      roi->w = (guint) g_ascii_strtoll (options[0], NULL, 10);
      roi->h = (guint) g_ascii_strtoll (options[1], NULL, 10);
    } else if (len == 4) {
      roi->x = (guint) g_ascii_strtoll (options[0], NULL, 10);
      roi->y = (guint) g_ascii_strtoll (options[1], NULL, 10);
      roi->w = (guint) g_ascii_strtoll (options[2], NULL, 10);
      roi->h = (guint) g_ascii_strtoll (options[3], NULL, 10);
    } else {
      DPRINT_ERROR ("Invalid param, should be roi:offset_x.offset_y.width.height\n");
      return -1;
    }

    g_strfreev (options);

  } else {
    printf("no params\n");
    return -1;
  }

  return 0;
}


static void
gst_ml_inference_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMlInference *in_filter = GST_MLINFERENCE (object);
  priv_ml_infer_ctx_t * filter = in_filter->priv_ctx;

  switch (prop_id) {
    case PROP_IN_NAME:
      strncpy(filter->input_name[filter->input_num], g_value_get_string (value), DMAX_FILE_NAME_LENGTH);
      filter->input_num++;
      break;
    case PROP_OUT_NAME:
      strncpy(filter->output_name[filter->output_num], g_value_get_string (value), DMAX_FILE_NAME_LENGTH);
      filter->output_num++;
      break;
    case PROP_LABEL:
      strncpy(filter->label_file, g_value_get_string (value), DMAX_LABEL_LEN);
      break;
    case PROP_DUMP_FILE:
      strncpy(filter->dump_file, g_value_get_string (value), DMAX_FILE_NAME_LENGTH);
      break;
    case PROP_MODEL:
      strncpy(filter->model_file, g_value_get_string (value), DMAX_FILE_NAME_LENGTH);
      break;
    case PROP_TYPE:
      strncpy(filter->model_type_str, g_value_get_string (value), 255);
      setup_customized_nn_process_factory(filter, filter->model_type_str);
      break;
    case PROP_CONF_THRESHOLD:
      filter->conf_threshold = g_value_get_float (value);
      break;
    case PROP_NMS:
      filter->nms_threshold = g_value_get_float (value);
      break;
    case PROP_TOP_K:
      filter->top_k = g_value_get_int (value);
      break;
    case PROP_USE_MULTI_CLS:
      filter->use_multi_cls = g_value_get_int (value);
      break;
    case PROP_COLOR_SPACE:
      strncpy(filter->color_space_str, g_value_get_string (value), 127);
      filter->color_space = get_color_space(filter->color_space_str);
      break;
    case PROP_ROI: {
      if (parse_roi(g_value_get_string (value), &filter->roi) < 0) {
        DPRINT_ERROR("parse_roi (%s) failed\n", g_value_get_string (value));
        return;
      }
    }break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ml_inference_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMlInference *in_filter = GST_MLINFERENCE (object);
  priv_ml_infer_ctx_t * filter = in_filter->priv_ctx;

  switch (prop_id) {
    case PROP_IN_NAME:
      g_value_set_string (value, filter->input_name[0]);
      break;
    case PROP_OUT_NAME:
      g_value_set_string (value, filter->output_name[0]);
      break;
    case PROP_LABEL:
      g_value_set_string (value, filter->label_file);
      break;
    case PROP_DUMP_FILE:
      g_value_set_string (value, filter->dump_file);
      break;
    case PROP_MODEL:
      g_value_set_string (value, filter->model_file);
      break;
    case PROP_TYPE:
      g_value_set_string (value, filter->model_type_str);
      break;
    case PROP_CONF_THRESHOLD:
      g_value_set_float (value, filter->conf_threshold);
      break;
    case PROP_NMS:
      g_value_set_float (value, filter->nms_threshold);
      break;
    case PROP_TOP_K:
      g_value_set_int (value, filter->top_k);
      break;
    case PROP_USE_MULTI_CLS:
      g_value_set_int (value, filter->use_multi_cls);
      break;
    case PROP_COLOR_SPACE:
      g_value_set_string (value, filter->color_space_str);
      break;
    case PROP_ROI: {
      char roi_str[256] = {0};
      snprintf(roi_str, sizeof(roi_str) - 1, "%d.%d.%d.%d",
          filter->roi.x, filter->roi.y, filter->roi.w, filter->roi.h);
      g_value_set_string (value, roi_str);
    }break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}



static void
gst_ml_inference_finalize (GObject * object)
{
  GstMlInference *in_filter = GST_MLINFERENCE (object);
  priv_ml_infer_ctx_t * filter = in_filter->priv_ctx;

  __net_free(filter);

  release_cv_vproc_ctx(1);

  if (in_filter->priv_ctx) {
    free(in_filter->priv_ctx);
    in_filter->priv_ctx = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static color_space_t get_color_space_type(GstVideoFormat format, guint *channel, guchar *interleaved)
{
  switch (format) {
    case GST_VIDEO_FORMAT_RGB:
      *channel = 3;
      *interleaved = 1;
      return CS_RGB_ITL;
      break;
    case GST_VIDEO_FORMAT_BGR:
      *channel = 3;
      *interleaved = 1;
      return CS_BGR_ITL;
      break;
    case GST_VIDEO_FORMAT_RGBP:
      *channel = 3;
      *interleaved = 0;
      return CS_RGB;
      break;
    case GST_VIDEO_FORMAT_BGRP:
      *channel = 3;
      *interleaved = 0;
      return CS_BGR;
      break;
    case GST_VIDEO_FORMAT_NV12:
      *channel = 2;
      *interleaved = 0;
      return CS_NV12;
      break;
    case GST_VIDEO_FORMAT_GRAY8:
      *channel = 1;
      *interleaved = 0;
      return CS_Y;
    default:
      DPRINT_ERROR("not supported video format: %d\n", format);
      return CS_VECT;
      break;
  }
}

static int color_convert(priv_ml_infer_ctx_t *ctx,
  vect_desc_t *src, vect_desc_t *dst,
  unsigned char *src_data, unsigned char *dst_data,
  unsigned long dst_size, unsigned int inter_idx)
{
  int ret = 0;
  unsigned char hwc_2_chw = 0;
  unsigned char swap_flag = 0;

  if (src->roi.width > 0 && src->roi.height > 0
      && (dst->shape.w != src->roi.width || dst->shape.h != src->roi.height)) {
    DPRINT_ERROR("roi width/height not equal (%d, %d)->(%d, %d)\n",
        src->roi.width, src->roi.height,
        dst->shape.w, dst->shape.h);
    return -1;
  } else if (dst->shape.w != src->shape.w || dst->shape.h != src->shape.h) {
    DPRINT_ERROR("width/height not equal (%d, %d)->(%d, %d)\n",
        src->shape.w, src->shape.h,
        dst->shape.w, dst->shape.h);
    return -1;
  }

  switch (dst->color_space) {
    case CS_RGB: {
      unsigned int size = dst->shape.h * dst->pitch;
      unsigned char *srcPtr[3] = {NULL};
      vect_desc_t it_split, ot_split;
      cv_daddr_t data_addr = dst->data_addr;
      unsigned int data_size = dst_size;

      memcpy(&it_split, src, sizeof(vect_desc_t));
      memcpy(&ot_split, dst, sizeof(vect_desc_t));

      switch (src->color_space) {
        case CS_RGB: {
            memcpy(dst_data, src_data, dst->shape.p * dst->shape.d * dst->shape.h * dst->pitch);
#if ENABLE_CACHE_ON_NET_MEM
            if (ctx->cache_en) {
              cavalry_mem_sync_cache(dst_size, dst->data_addr, 1, 0);
            }
#endif

          }
          break;
        case CS_RGB_ITL:
          hwc_2_chw = 1;

          break;
        case CS_BGR: {
            swap_flag = 1;
            srcPtr[0] = src_data + 2 * size;
            srcPtr[1] = src_data + size;
            srcPtr[2] = src_data;
          }
          break;
        case CS_BGR_ITL: {
            hwc_2_chw = 1;
            swap_flag = 1;

            ot_split.color_space = CS_BGR;
            data_addr = ctx->img_mem_phys[inter_idx];
            data_size = ctx->img_mem_size[inter_idx];

            srcPtr[0] = ctx->img_mem_virt[inter_idx] + 2 * size;
            srcPtr[1] = ctx->img_mem_virt[inter_idx] + size;
            srcPtr[2] = ctx->img_mem_virt[inter_idx];
          }
          break;
        case CS_NV12: {
            vect_desc_t y, uv;
            yuv2rgb_mat_t yuv2rgb_mat;
            memset(&y, 0x0, sizeof(vect_desc_t));
            memset(&uv, 0x0, sizeof(vect_desc_t));
            memset(&yuv2rgb_mat, 0x0, sizeof(yuv2rgb_mat_t));
            memcpy(&y, src, sizeof(vect_desc_t));
            y.shape.d = 1;
            y.color_space = CS_NV12;

            memcpy(&uv, src, sizeof(vect_desc_t));
            uv.shape.w = (src->shape.w + 1)>>1;
            uv.shape.h = (src->shape.h + 1)>>1;
            uv.shape.d = 2;
            uv.color_space = CS_NV12;
            uv.data_addr = src->data_addr + y.shape.h * y.pitch;
            uv.roi.xoffset = (src->roi.xoffset + 1)>>1;
            uv.roi.yoffset = (src->roi.yoffset + 1)>>1;
            uv.roi.width = (src->roi.width + 1)>>1;
            uv.roi.height = (src->roi.height + 1)>>1;

            yuv2rgb_mat.yc = 1;
            yuv2rgb_mat.rv = 1.402;
            yuv2rgb_mat.gu = 0.344136;
            yuv2rgb_mat.gv = 0.714136;
            yuv2rgb_mat.bu = 1.772;
            yuv2rgb_mat.yb = 0;

            if (vproc_yuv2rgb_420(&y, &uv, dst, &yuv2rgb_mat) < 0) {
              DPRINT_ERROR("nv12 convert to rgb failed\n");
              ret = -1;
              break;
            }

#if ENABLE_CACHE_ON_NET_MEM
            if (ctx->cache_en) {
              cavalry_mem_sync_cache(dst_size, dst->data_addr, 0, 1);
            }
#endif
          }
          break;
        default:
          DPRINT_ERROR("not supported video color space: %d\n", src->color_space);
          return -1;
          break;
      }

      if (hwc_2_chw) {
        unsigned int batch_size = src->shape.w;
        cv_daddr_t it_addr_offset = 0, ot_addr_offset = 0;

        while (batch_size > MAX_CVT_W_SIZE) {
          it_split.data_addr = src->data_addr + it_addr_offset;
          it_split.shape.w = MAX_CVT_W_SIZE;
          ot_split.data_addr = data_addr + ot_addr_offset;
          ot_split.shape.w = MAX_CVT_W_SIZE;
          if (vproc_imcvt(&it_split, &ot_split) < 0) {
            DPRINT_ERROR("imcvt failure! it_split %dx%d\n", it_split.shape.w, it_split.shape.h);
            break;
          }

          it_addr_offset += it_split.shape.w * it_split.shape.d;
          ot_addr_offset += ot_split.shape.w;
          batch_size -= MAX_CVT_W_SIZE;
        }
        it_split.shape.w = batch_size;
        it_split.data_addr = src->data_addr + it_addr_offset;
        ot_split.shape.w = batch_size;
        ot_split.data_addr = data_addr + ot_addr_offset;

        if (vproc_imcvt(&it_split, &ot_split) < 0) {
          DPRINT_ERROR("imcvt failure! it_split %dx%d\n", it_split.shape.w, it_split.shape.h);
          break;
        }

#if ENABLE_CACHE_ON_NET_MEM
        if (ctx->cache_en) {
          cavalry_mem_sync_cache(data_size, data_addr, 0, 1);
        }
#endif

      }

      if (swap_flag) {
        memcpy(dst_data, srcPtr[0], size);
        memcpy(dst_data + size, srcPtr[1], size);
        memcpy(dst_data + size * 2, srcPtr[2], size);
#if ENABLE_CACHE_ON_NET_MEM
        if (ctx->cache_en) {
          cavalry_mem_sync_cache(dst_size, dst->data_addr, 1, 0);
        }
#endif
      }
    }
    break;
    default:
      DPRINT_ERROR("not supported nn input color space: %d\n", dst->color_space);
      return -1;
      break;
  }

  return ret;

}

static int __cvt_color_resize_vproc(priv_ml_infer_ctx_t *ctx,
  img_data_info_t *input_info)
{
  int rval = 0, i = 0;
  color_space_t cs = CS_VECT;
  guint channel = 0, src_pitch0 = 0;
  vect_desc_t src_desc;
  vect_desc_t dst_desc0, dst_desc1;
  resize_cfg_t rsz_cfg = {0};
  struct input_desc *dst = &ctx->input_cfg.in_desc[0];
  gsize size = 0, max_size = 0;
  guchar interleaved = 0;
  unsigned char *input_buf = NULL;
  unsigned int inter_index = 0;

  do {
    cs = get_color_space_type(input_info->format, &channel, &interleaved);

    if (CHECK_PITCH_ALIGN(input_info->stride[0])) {
      if (interleaved) {
        src_pitch0 = ALIGN_PITCH(input_info->width * channel);
      } else {
        src_pitch0 = ALIGN_PITCH(input_info->width);
      }
    } else {
      src_pitch0 = input_info->stride[0];
    }

    max_size = DMAX(channel, dst->dim.depth) *
      DMAX(input_info->height * ALIGN_PITCH(input_info->width), dst->dim.height * ALIGN_PITCH(dst->dim.width));

    if (ctx->img_mem_size[0] < max_size) {
      if (ctx->img_mem_virt[0]) {
        cavalry_mem_free(ctx->img_mem_size[0], ctx->img_mem_phys[0],
            ctx->img_mem_virt[0]);
      }
      ctx->img_mem_size[0] = max_size;
      if (cavalry_mem_alloc(&ctx->img_mem_size[0], &ctx->img_mem_phys[0],
          (void **) & (ctx->img_mem_virt[0]), ctx->cache_en) < 0) {
        DPRINT_ERROR("cavalry_mem_alloc(bgr) failed\n");
        return -2;
      }
    }

    if (ctx->img_mem_size[1] < max_size) {
      if (ctx->img_mem_virt[1]) {
        cavalry_mem_free(ctx->img_mem_size[1], ctx->img_mem_phys[1],
            ctx->img_mem_virt[1]);
      }
      ctx->img_mem_size[1] = max_size;
      if (cavalry_mem_alloc(&ctx->img_mem_size[1], &ctx->img_mem_phys[1],
          (void **) & (ctx->img_mem_virt[1]), ctx->cache_en) < 0) {
        DPRINT_ERROR("cavalry_mem_alloc(bgr) failed\n");
        return -2;
      }
    }

    if (cs == CS_NV12) {
      if (CHECK_PITCH_ALIGN(input_info->stride[0])) {
        unsigned char *s0 = input_info->data[0];
        unsigned char *s1 = input_info->data[1];
        unsigned char *p0 = ctx->img_mem_virt[0];
        for (unsigned int h = 0; h <input_info->height; h++) {
          memcpy(p0, s0, input_info->width);
          p0 += src_pitch0;
          s0 += input_info->stride[0];
        }

        for (unsigned int h = 0; h < (input_info->height + 1) / 2; h++) {
          memcpy(p0, s1, input_info->width);
          p0 += src_pitch0;
          s1 += input_info->stride[1];
        }
      } else {
        size = input_info->height * input_info->stride[0];
        memcpy(ctx->img_mem_virt[0], input_info->data[0], size);
        memcpy(ctx->img_mem_virt[0] + size, input_info->data[1], input_info->stride[1] * ((input_info->height + 1) / 2));
      }
    } else {
      input_buf = ctx->img_mem_virt[0];
      unsigned int stride = 0;
      if (interleaved) {
        stride = input_info->width * channel;
        for (i = 0; i < input_info->n_planes; i++) {
          if (CHECK_PITCH_ALIGN(input_info->stride[i])) {
            unsigned char *s0 = input_info->data[i];
            for (unsigned int h = 0; h < input_info->height; h++) {
              memcpy(input_buf, s0, stride);
              input_buf += src_pitch0;
              s0 += input_info->stride[i];
            }
          } else {
            gsize ss = input_info->height * input_info->stride[i];
            memcpy(input_buf, input_info->data[i], ss);
            input_buf += ss;
          }
        }

      } else {
        stride = input_info->width;
        if (input_info->n_planes != (gint) channel) {
          DPRINT_ERROR("plane num %d not equal to channel %d\n", input_info->n_planes, channel);
          rval = -3;
          break;
        }
        for (i = 0; i < input_info->n_planes; i++) {
          if (CHECK_PITCH_ALIGN(input_info->stride[i])) {
            unsigned char *s0 = input_info->data[i];
            for (unsigned int h = 0; h < input_info->height; h++) {
              memcpy(input_buf, s0, stride);
              input_buf += src_pitch0;
              s0 += input_info->stride[i];
            }
          } else {
            gsize ss = input_info->height * input_info->stride[i];
            memcpy(input_buf, input_info->data[i], ss);
            input_buf += ss;
          }
        }
      }
    }


#if ENABLE_CACHE_ON_NET_MEM
    if (ctx->cache_en) {
      cavalry_mem_sync_cache(ctx->img_mem_size[0], ctx->img_mem_phys[0], 1, 0);
    }
#endif

    if (input_info->height == dst->dim.height
        && input_info->width == dst->dim.width
        && ctx->roi.h == 0
        && ctx->roi.w == 0) {
      memset(&dst_desc0, 0, sizeof(dst_desc0));
      dst_desc0.shape.p = 1;
      dst_desc0.shape.h = input_info->height;
      dst_desc0.shape.w = input_info->width;
      dst_desc0.data_format.sign = 0;
      dst_desc0.data_format.datasize = 0;
      dst_desc0.data_format.exp_offset = 0;
      dst_desc0.data_format.exp_bits = 0;
      dst_desc0.pitch = src_pitch0;
      dst_desc0.data_addr = ctx->img_mem_phys[0];
      dst_desc0.shape.d = channel;
      dst_desc0.color_space = cs;

      input_buf = ctx->img_mem_virt[0];
      inter_index = 1;
    } else {
      memset(&src_desc, 0, sizeof(src_desc));
      src_desc.shape.p = 1;
      src_desc.shape.h = input_info->height;
      src_desc.shape.w = input_info->width;
      src_desc.data_format.sign = 0;
      src_desc.data_format.datasize = 0;
      src_desc.data_format.exp_offset = 0;
      src_desc.data_format.exp_bits = 0;
      src_desc.data_addr = ctx->img_mem_phys[0];
      src_desc.pitch = src_pitch0;//DROUND_UP(input_info->width * (1 << src_desc.data_format.datasize), CAVALRY_PORT_PITCH_ALIGN);
      src_desc.shape.d = channel;
      src_desc.color_space = cs;
      src_desc.roi.xoffset = ctx->roi.x;
      src_desc.roi.yoffset = ctx->roi.y;
      src_desc.roi.width = ctx->roi.w;
      src_desc.roi.height = ctx->roi.h;

      if (cs == CS_RGB_ITL || cs == CS_BGR_ITL) {
        DPRINT_ERROR("not support interleave data resize now\n");
        rval = -1;
        break;
      }

      memset(&dst_desc0, 0, sizeof(dst_desc0));
      dst_desc0.shape.p = dst->dim.plane;
      dst_desc0.shape.h = dst->dim.height;
      dst_desc0.shape.w = dst->dim.width;
      dst_desc0.data_format.sign = 0;
      dst_desc0.data_format.datasize = 0;
      dst_desc0.data_format.exp_offset = 0;
      dst_desc0.data_format.exp_bits = 0;
      dst_desc0.pitch = ALIGN_PITCH(dst->dim.width);
      dst_desc0.data_addr = ctx->img_mem_phys[1];
      dst_desc0.shape.d = channel;
      dst_desc0.color_space = cs;
#if defined (BUILD_DSP_AMBA_V5)
      rsz_cfg.method = RESIZE_MUL_STEPS;
#elif defined (BUILD_DSP_AMBA_V6)
      rsz_cfg.method = RESIZE_SIN_STEP;
#endif
      if (vproc_resize_ext(&src_desc, &dst_desc0, &rsz_cfg) < 0) {
        DPRINT_ERROR("vproc_resize_ext error\n");
        rval = -1;
        break;
      }

#if ENABLE_CACHE_ON_NET_MEM
      if (ctx->cache_en) {
        cavalry_mem_sync_cache(ctx->img_mem_size[1], ctx->img_mem_phys[1], 0, 1);
      }

#endif

      input_buf = ctx->img_mem_virt[1];
      inter_index = 0;
    }

    memcpy(&dst_desc1, &dst_desc0, sizeof(dst_desc1));
    dst_desc1.shape.d = dst->dim.depth;
    dst_desc1.pitch = dst->dim.pitch;
    dst_desc1.data_addr = dst->addr;
    dst_desc1.color_space = ctx->color_space;

    color_convert(ctx, &dst_desc0, &dst_desc1,
        input_buf,
        dst->virt,
        dst->size, inter_index);

  } while (0);

  return rval;
}

static int __nn_vp_forward(priv_ml_infer_ctx_t *net)
{
  int rval = 0;

  if (net) {
    do {
#if ENABLE_CACHE_ON_NET_MEM
      if (net->cache_en) {
        /*for (unsigned int i = 0; i < net->input_cfg.in_num; i++) {
          // data will be read by VP
          cavalry_mem_sync_cache(net->input_cfg.in_desc[i].size,
              net->input_cfg.in_desc[i].addr, 0, 1);
        }*/

        for (unsigned int i = 0; i < net->output_cfg.out_num; i++) {
          // data will be written by VP
          memset(net->output_cfg.out_desc[i].virt,
              0x0, net->output_cfg.out_desc[i].size);
          cavalry_mem_sync_cache(net->output_cfg.out_desc[i].size,
              net->output_cfg.out_desc[i].addr, 1, 0);
        }
      }
#endif
      net->run_cfg.net_loop_cnt = 1;//batch
      net->run_cfg.no_auto_resume = net->abort_if_preempted;
      net->run_cfg.priority = net->priority;
      net->run_cfg.split_num_run = net->split_num;
      if (nnctrl_run_net(net->id, &net->result,
          &net->run_cfg, &net->input_cfg, &net->output_cfg) < 0) {
        DPRINT_ERROR("nnctrl_run_net error\n");
        rval = -1;
      }
      net->vp_time_us = net->result.vp_time_us;
    } while (0);
  } else {
    DPRINT_ERROR("params error\n");
    rval = -1;
  }

  return rval;
}


static gboolean
gst_ml_inference_create_session (GstBaseTransform * trans)
{
  GstMlInference *self = GST_MLINFERENCE (trans);
  priv_ml_infer_ctx_t * filter = self->priv_ctx;
  gboolean nn_disabled = FALSE;
  int ret = 0;

  GST_OBJECT_LOCK (self);

  if (filter->model_file[0] != '\0') {
    ret = __net_init(filter);
    if (ret < 0) {
      DPRINT_ERROR("net init error\n");
      nn_disabled = TRUE;
    }

  } else {
    nn_disabled = TRUE;
  }
  GST_OBJECT_UNLOCK (self);
  if (nn_disabled){
    gst_base_transform_set_passthrough (trans, TRUE);
  }


#ifndef D_OS_AMRTOS

  if (filter->dump_file[0] != '\0') {
    filter->dump_fd = fopen(filter->dump_file, "wb");
    if (filter->dump_fd == NULL) {
      DPRINT_ERROR("fopen() failed on %s\n", filter->dump_file);
      return FALSE;
    }
  }

#endif

  return TRUE;
}

static gboolean
gst_ml_inference_start (GstBaseTransform * trans)
{
  if ( !gst_ml_inference_create_session (trans) ) {
    DPRINT_ERROR("ml_inference_create error\n");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_ml_inference_stop (GstBaseTransform * trans)
{
  GstMlInference *self = GST_MLINFERENCE (trans);

 __net_free(self->priv_ctx);

#ifndef D_OS_AMRTOS
  if (self->priv_ctx->dump_fd) {
    fclose(self->priv_ctx->dump_fd);
    self->priv_ctx->dump_fd = NULL;
  }

#endif

  return TRUE;
}

static gboolean
gst_ml_inference_process (GstBaseTransform * trans, GstBuffer * buf)
{
  gboolean ret = TRUE;
  GstMlInference *self = GST_MLINFERENCE (trans);
  gint i = 0;
  guint n = 0;
  img_data_info_t input_info;

  bounding_boxes_t *boxes = NULL;
  GstVideoMeta *vmeta = gst_buffer_get_video_meta (buf);
  GstMemory *memory = NULL;
  GstMapInfo map_info;

  if (!vmeta) {
    GST_ERROR_OBJECT (trans, "missing video meta");
    return FALSE;
  }

  if (vmeta->n_planes > GST_VIDEO_MAX_PLANES) {
    GST_ERROR_OBJECT (trans, "too many planes in video meta");
    return FALSE;
  }

  input_info.n_planes = vmeta->n_planes;
  input_info.format = vmeta->format;
  input_info.height = vmeta->height;
  input_info.width = vmeta->width;

  memory = gst_buffer_peek_memory(buf, 0);
  if (!gst_memory_map(memory, &map_info, GST_MAP_READ)) {
    GST_ERROR_OBJECT (trans, "failed to map memory");
    return FALSE;
  }

  for (i = 0; i < (gint) vmeta->n_planes; i++) {
    input_info.data[i] = map_info.data + vmeta->offset[i];
    input_info.stride[i] = vmeta->stride[i];
  }

  priv_ml_infer_ctx_t * filter = self->priv_ctx;

  if (__cvt_color_resize_vproc(filter, &input_info) < 0) {
    DPRINT_ERROR("__cvt_color_resize_vproc error\n");
    ret = FALSE;
    goto ML_END;
  }

  if (__nn_vp_forward(filter) < 0) {
    DPRINT_ERROR("nn_vp_forward error\n");
    ret = FALSE;
    goto ML_END;
  }
  if (filter->func_post_process) {
    if (filter->func_post_process(filter) < 0) {
      DPRINT_ERROR("post_process error\n");
      ret = FALSE;
      goto ML_END;
    }

    boxes = &filter->bboxs;
    for (n = 0; n < boxes->det_num; n++) {
      det_object_t *b = &boxes->detections[n];

      if (b->x_start < 0 || b->y_start < 0
        || (b->x_end - b->x_start) < 0 || (b->y_end - b->y_start) < 0) {
        DPRINT_ERROR("bbox params error\n");
        return -1;
      }
      if (b->x_start >= 1 || b->x_end > 1) {
        DPRINT_ERROR("bbox params error\n");
        return -1;
      }
      if (b->y_start >= 1 || b->y_end > 1) {
        DPRINT_ERROR("bbox params error\n");
        return -1;
      }
      guint sx = b->x_start * vmeta->width;
      guint sy = b->y_start * vmeta->height;
      guint w = (b->x_end - b->x_start) * vmeta->width;
      guint h = (b->y_end - b->y_start) * vmeta->height;
      GstVideoRegionOfInterestMeta *vroi_meta = gst_buffer_add_video_region_of_interest_meta (buf,
          GST_MLINFERENCE_META_NAME,
          sx, sy,
          w,
          h);
      if (!vroi_meta) {
        GST_ERROR_OBJECT (trans,
            "Unable to attach GstVideoRegionOfInterestMeta to buffer");
        ret = FALSE;
        goto ML_END;
      }

      GstStructure *s = gst_structure_new (GST_MLINFERENCE_META_PARAM_NAME,
          GST_MLINFERENCE_META_FIELD_ID,
          G_TYPE_INT,
          b->id,
          GST_MLINFERENCE_META_FIELD_LABEL,
          G_TYPE_STRING,
          b->label,
          GST_MLINFERENCE_META_FIELD_SCORE,
          G_TYPE_DOUBLE,
          b->score,
          GST_MLINFERENCE_META_FIELD_X_START,
          G_TYPE_DOUBLE,
          b->x_start,
          GST_MLINFERENCE_META_FIELD_Y_START,
          G_TYPE_DOUBLE,
          b->y_start,
          GST_MLINFERENCE_META_FIELD_X_END,
          G_TYPE_DOUBLE,
          b->x_end,
          GST_MLINFERENCE_META_FIELD_Y_END,
          G_TYPE_DOUBLE,
          b->y_end,
          NULL);

      gst_video_region_of_interest_meta_add_param (vroi_meta, s);
      GST_FIXME_OBJECT (self,
          "Object detected with label : %s, score: %f, bound box: (%d,%d,%f,%f) \n",
          b->label, b->score, sx, sy,
          b->x_end * vmeta->width, b->y_end * vmeta->height);

    }
  } else {
    //store outputs to file
    if (filter->dump_fd) {
      for (n = 0; n < filter->output_cfg.out_num; n++) {
        int ch = filter->output_cfg.out_desc[n].dim.depth;
        int height = filter->output_cfg.out_desc[n].dim.height;
        int pitch = filter->output_cfg.out_desc[n].dim.pitch;
        int offset = filter->output_cfg.out_desc[n].dim.width
            * (1 << filter->output_cfg.out_desc[n].data_fmt.size);
        unsigned char *nn_output = filter->output_cfg.out_desc[n].virt;

#if ENABLE_CACHE_ON_NET_MEM
        if (filter->cache_en) {
          cavalry_mem_sync_cache(filter->output_cfg.out_desc[n].size,
              filter->output_cfg.out_desc[n].addr, 0, 1);
        }
#endif
        for (int c = 0; c < ch; c++) {
          for (int j = 0; j < height; j++) {
            fwrite(nn_output, 1, offset, filter->dump_fd);
            nn_output += pitch;
          }
        }
      }
    }
  }

ML_END:
  gst_memory_unmap(memory, &map_info);

  return ret;
}

static GstFlowReturn
gst_ml_inference_transform_ip (GstBaseTransform * trans,
    GstBuffer * buf)
{
  GstClockTime start = GST_CLOCK_TIME_NONE, end = GST_CLOCK_TIME_NONE;

  start = gst_util_get_timestamp ();

  if (!gst_base_transform_is_passthrough (trans)
    && !gst_ml_inference_process (trans, buf)){
    GST_ELEMENT_WARNING (trans, STREAM, FAILED,
        ("ML Inference failed"), (NULL));
    return GST_FLOW_ERROR;
  }

  end = gst_util_get_timestamp ();

  GST_FIXME_OBJECT (trans, "ML running time: %" GST_TIME_FORMAT "\n", GST_TIME_ARGS (end - start));

  return GST_FLOW_OK;
}


