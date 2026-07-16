/*
 * gstmlpostprocess.c
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
 */

/**
 * SECTION:element-mlpostprocess
 * @title: mlpostprocess
 * @see_also: mlinference2, amba_draw_data_gen
 *
 * Post-processes ML tensors from mlinference2 (NMS, bbox, segmentation ...etc).
 * Output caps depend on type: application/x-amba-ml-decoded (bbox and more) or video/x-raw GRAY8 (image-only).
 *
 * Input: application/x-amba-ml-tensors.
 * Output: application/x-amba-ml-decoded (struct/mixed) or video/x-raw,format=GRAY8 (image-only).
 * Downstream: amba_draw_data_gen (decoded) or videoconvert (video-only).
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 amba_camsrc buf-id=0 ! queue ! videoconvert ! videoscale ! video/x-raw,format=RGBP,width=416,height=416 ! \
 * mlinference2 in_name=images out_name=1037+1017+997 model=/tmp/nn/model/cv75/onnx_yolov5s_cavalry.bin ! queue ! \
 * mlpostprocess type=yolov5 label=/tmp/nn/in/coco_class_names.txt coord_res=1920x1080 conf_threshold=0.25 nms=0.45 top_k=100 ! \
 * amba_draw_data_gen ! amba_overlay_draw stream_id=0 sync_pts=1 osd_offset=0 osd_size=4163584
 * ]|
 * </refsect2>
 */
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include <gst/video/gstvideometa.h>

#include "internal.h"
#include "debug_log.h"
#include "gstmlpostprocess.h"
#include "ml_postprocess_if.h"
#include "clip_image.h"
#include "ml_tensors_caps.h"
#include "amba_ml_decoded_result.h"
#include "amba_private_data.h"
#include "yolop.h"

GST_DEBUG_CATEGORY_STATIC(gst_ml_postprocess_debug);
#define GST_CAT_DEFAULT gst_ml_postprocess_debug

enum {
  PROP_0,
  PROP_TYPE,
  PROP_LABEL,
  PROP_CONF_THRESHOLD,
  PROP_NMS,
  PROP_TOP_K,
  PROP_USE_MULTI_CLS,
  PROP_COORD_RESOLUTION,
  PROP_REFERENCE_EMBEDDING,
  PROP_REFERENCE_LABEL,
};

static GstStaticPadTemplate sink_tensor_factory = GST_STATIC_PAD_TEMPLATE("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS(GST_AMBA_ML_TENSORS_CAPS ", "
                   "num_tensors=(int)[1," G_STRINGIFY(AMBA_ML_MAX_TENSORS) "]")
    );

/* Single buffer: decoded for struct/mixed, video/x-raw for image-only (e.g. pure segmentation) */
static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS(
        GST_AMBA_ML_DECODED_CAPS "; "
        "video/x-raw, format=(string)GRAY8, width=(int)[1,4096], height=(int)[1,4096]"
    )
    );

#define gst_ml_postprocess_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE(GstMlPostprocess, gst_ml_postprocess, GST_TYPE_ELEMENT,
    GST_DEBUG_CATEGORY_INIT(gst_ml_postprocess_debug, "mlpostprocess", 0, "mlpostprocess"));

static void gst_ml_postprocess_finalize(GObject *object);
static void gst_ml_postprocess_set_property(GObject *object, guint prop_id,
    const GValue *value, GParamSpec *pspec);
static void gst_ml_postprocess_get_property(GObject *object, guint prop_id,
    GValue *value, GParamSpec *pspec);
static GstStateChangeReturn gst_ml_postprocess_change_state(GstElement *element, GstStateChange transition);

static GstFlowReturn gst_ml_postprocess_chain(GstPad *pad, GstObject *parent, GstBuffer *buffer);
static gboolean gst_ml_postprocess_sink_event(GstPad *pad, GstObject *parent, GstEvent *event);

/* Set output caps via gst_pad_set_caps (peer + sticky state), not only GST_EVENT_CAPS. */
static void
mlpp_set_src_pad_caps (GstObject *o, GstPad *src, GstCaps *caps)
{
  g_return_if_fail (src != NULL && caps != NULL);
  if (!gst_pad_set_caps (src, caps)) {
    GST_WARNING_OBJECT (o, "Failed to set caps on src pad (peer rejected or wrong state?)");
  }
}

static void parse_coord_resolution(const char *s, int *w, int *h)
{
  int a = 0, b = 0;
  if (s && s[0] && sscanf(s, "%dx%d", &a, &b) >= 2 && a > 0 && a <= 65535 && b > 0 && b <= 65535) {
    *w = a;
    *h = b;
  } else {
    *w = 0;
    *h = 0;
  }
}

/* File line buffer: ImageNet-style 'synset, species', lines can exceed DMAX_LABEL_LEN; must hold full line for closing '\'' */
#define MLPP_LABEL_FILE_LINE_MAX 512

static void
mlpp_label_str_rtrim_comma_sp(char *s)
{
  size_t n;

  if (!s)
    return;
  n = strlen(s);
  while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == ',')) {
    s[n - 1] = '\0';
    n--;
  }
}

/**
 * Label file lines may be:
 *   - Quoted tuple: 'tench, Tinca tinca',
 *   - id: text (COCO)
 *   - one plain label per line (ImageNet text lists)
 */
static int get_label_from_file(const char *label_file, char (*labels)[DMAX_LABEL_LEN], guint *label_num)
{
  int rval = 0;
  guint label_count = 0;
  gint len = 0, file_len = 0;
  FILE *fp_label = NULL;
  char *label_line_start = NULL;
  char *label_line_end = NULL;
  char label_line[MLPP_LABEL_FILE_LINE_MAX];

  do {
    fp_label = fopen(label_file, "r");
    if (!fp_label) {
      DPRINT_ERROR("can't open label_file[%s]\n", label_file);
      rval = -1;
      break;
    }

    if (fseek(fp_label, 0L, SEEK_END) != 0) {
      rval = -1;
      break;
    }
    file_len = ftell(fp_label);
    if (file_len <= 0) {
      rval = -1;
      break;
    }
    if (fseek(fp_label, 0L, SEEK_SET) != 0) {
      rval = -1;
      break;
    }

    while (len != file_len) {
      memset(label_line, 0, sizeof(label_line));
      if (!fgets(label_line, (int)sizeof(label_line), fp_label)) {
        rval = -1;
        break;
      }
      len = ftell(fp_label);

      if (strlen(label_line) >= sizeof(label_line) - 1) {
        DPRINT_ERROR("label line too long (max %zu chars)\n", sizeof(label_line) - 1);
        rval = -1;
        break;
      }

      label_line_start = strchr(label_line, '\'');
      label_line_end = strrchr(label_line, '\'');
      if (label_line_start && label_line_end && (label_line_end > label_line_start + 1)) {
        *label_line_end = '\0';
        snprintf(labels[label_count], DMAX_LABEL_LEN, "%s", label_line_start + 1);
        mlpp_label_str_rtrim_comma_sp(labels[label_count]);
        if (labels[label_count][0] != '\0')
          label_count++;
      } else {
        label_line_start = strchr(label_line, ':');
        label_line_end = strrchr(label_line, '\r');
        if (!label_line_end) {
          label_line_end = strrchr(label_line, '\n');
        }
        if (label_line_start && label_line_end && (label_line_end > label_line_start + 1)) {
          *label_line_end = '\0';
          snprintf(labels[label_count], DMAX_LABEL_LEN, "%s", label_line_start + 1);
          mlpp_label_str_rtrim_comma_sp(labels[label_count]);
          if (labels[label_count][0] != '\0')
            label_count++;
        } else {
          /* Plain one-label-per-line (ImageNet 1000, etc.): no quotes or id: prefix */
          char *p = label_line;
          char *e;
          while (*p == ' ' || *p == '\t')
            p++;
          e = p + strlen(p);
          while (e > p && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t'))
            e--;
          *e = '\0';
          mlpp_label_str_rtrim_comma_sp(p);
          if (p[0] != '\0' && p[0] != '#') {
            snprintf(labels[label_count], DMAX_LABEL_LEN, "%s", p);
            label_count++;
          }
        }
      }

      if (label_count >= DMAX_LABEL_NUM && len != file_len) {
        DPRINT_ERROR("too many labels\n");
        rval = -1;
        break;
      }
    }

    *label_num = label_count;
  } while (0);

  if (fp_label) {
    fclose(fp_label);
  }
  return rval;
}

static void gst_ml_postprocess_class_init(GstMlPostprocessClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *)klass;
  GstElementClass *gstelement_class = (GstElementClass *)klass;

  gobject_class->finalize = gst_ml_postprocess_finalize;
  gobject_class->set_property = gst_ml_postprocess_set_property;
  gobject_class->get_property = gst_ml_postprocess_get_property;
  gstelement_class->change_state = gst_ml_postprocess_change_state;

  g_object_class_install_property(gobject_class, PROP_TYPE,
      g_param_spec_string("type", "ModelType",
          "Post-process type: yolov5, yolox, rtmpose, clip_image, depthanythingv2, classification, … (see registry)",
          NULL, G_PARAM_READWRITE));
  g_object_class_install_property(gobject_class, PROP_LABEL,
      g_param_spec_string("label", "LabelFile",
          "Label file path (optional for YOLOP: uses 'car' when unset)",
          NULL, G_PARAM_READWRITE));
  g_object_class_install_property(gobject_class, PROP_CONF_THRESHOLD,
      g_param_spec_float("conf_threshold", "ConfThreshold", "Confidence threshold",
          0, 1, 0.25, G_PARAM_READWRITE));
  g_object_class_install_property(gobject_class, PROP_NMS,
      g_param_spec_float("nms", "NMS", "NMS threshold",
          0, 1, 0.45, G_PARAM_READWRITE));
  g_object_class_install_property(gobject_class, PROP_TOP_K,
      g_param_spec_int("top_k", "TopK", "Max detections for detectors; top-k ranked classes for type=classification (max 16)",
          0, INT_MAX, 100, G_PARAM_READWRITE));
  g_object_class_install_property(gobject_class, PROP_USE_MULTI_CLS,
      g_param_spec_int("use_multi_cls", "UseMultiCls", "Use multiple classes per anchor",
          0, 1, 0, G_PARAM_READWRITE));
  g_object_class_install_property(gobject_class, PROP_COORD_RESOLUTION,
      g_param_spec_string("coord_res", "CoordRes",
          "Resolution for bbox coordinates (required), format WIDTHxHEIGHT (e.g. 1920x1080)",
          "", G_PARAM_READWRITE));
  g_object_class_install_property(gobject_class, PROP_REFERENCE_EMBEDDING,
      g_param_spec_string("reference-embedding", "ReferenceEmbedding",
          "CLIP reference embedding file (raw float32 dim*4 or amba_ml_embedding_result_t). "
          "Used with type=clip_image to fill match_score; logs cosine at INFO "
          "(GST_DEBUG=mlpostprocess:4).",
          "", G_PARAM_READWRITE));
  g_object_class_install_property(gobject_class, PROP_REFERENCE_LABEL,
      g_param_spec_string("reference-label", "ReferenceLabel",
          "Optional label shown with match_score (type=clip_image + drawdatagen type:clip_score).",
          "", G_PARAM_READWRITE));
  gst_element_class_set_static_metadata(gstelement_class,
      "Amba ML Post-Process",
      "Filter/Video",
      "Post-process for mlinference2 (YOLO, SSD, etc.) - single buffer multi-memory output",
      "PengXue Duan <<pxduan@ambarella.com>>");

  gst_element_class_add_static_pad_template(gstelement_class, &sink_tensor_factory);
  gst_element_class_add_static_pad_template(gstelement_class, &src_factory);
}

static void gst_ml_postprocess_init(GstMlPostprocess *self)
{
  self->priv = g_malloc0(sizeof(mlpp_priv_ctx_t));
  self->priv->conf_threshold = 0.25f;
  self->priv->nms_threshold = 0.45f;
  self->priv->top_k = 100;
  self->priv->use_multi_cls = 0;
  self->priv->coord_res[0] = '\0';
  self->priv->reference_embedding_path[0] = '\0';
  self->priv->reference_label[0] = '\0';
  self->priv->clip_ref_dim = 0;
  self->priv->clip_ref_valid = 0;
  self->priv->map_width = 0;
  self->priv->map_height = 0;
  self->priv->output_pad_count = 0;

  self->sink_tensor = gst_pad_new_from_static_template(&sink_tensor_factory, "sink");
  gst_pad_set_chain_function(self->sink_tensor, gst_ml_postprocess_chain);
  gst_pad_set_event_function(self->sink_tensor, gst_ml_postprocess_sink_event);
  gst_element_add_pad(GST_ELEMENT(self), self->sink_tensor);

  self->src = gst_pad_new_from_static_template(&src_factory, "src");
  gst_element_add_pad(GST_ELEMENT(self), self->src);
}

static void gst_ml_postprocess_finalize(GObject *object)
{
  GstMlPostprocess *self = GST_ML_POSTPROCESS(object);

  if (self->priv) {
    if (self->priv->postprocess_ops && self->priv->postprocess_ops->deinit_user_ctx) {
      self->priv->postprocess_ops->deinit_user_ctx(self->priv);
    }
    if (self->priv->tensor_cache.caps) {
      gst_caps_unref(self->priv->tensor_cache.caps);
    }
    g_free(self->priv->f32_convert_buf);
    g_free(self->priv->ea_det_bbox_buf);
    g_free(self->priv->yolo_tpose_f32);
    g_free(self->priv);
    self->priv = NULL;
  }

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
mlpp_reload_clip_reference(GstMlPostprocess *self)
{
  mlpp_priv_ctx_t *priv = self->priv;

  priv->clip_ref_valid = 0;
  priv->clip_ref_dim = 0;
  if (!priv->reference_embedding_path[0])
    return;

  if (mlpp_clip_load_reference_embedding(priv->reference_embedding_path,
          priv->clip_ref_feature, &priv->clip_ref_dim,
          AMBA_ML_EMBEDDING_MAX_DIM) == 0) {
    priv->clip_ref_valid = 1;
    GST_INFO_OBJECT(self, "Loaded reference embedding dim=%u from %s",
        priv->clip_ref_dim, priv->reference_embedding_path);
  } else {
    GST_WARNING_OBJECT(self, "Failed to load reference-embedding %s",
        priv->reference_embedding_path);
  }
}

static void gst_ml_postprocess_set_property(GObject *object, guint prop_id,
    const GValue *value, GParamSpec *pspec)
{
  GstMlPostprocess *self = GST_ML_POSTPROCESS(object);
  mlpp_priv_ctx_t *priv = self->priv;

  switch (prop_id) {
    case PROP_TYPE: {
      const gchar *s = g_value_get_string(value);
      strncpy(priv->model_type_str, s ? s : "", sizeof(priv->model_type_str) - 1);
      priv->model_type_str[sizeof(priv->model_type_str) - 1] = '\0';
      setup_ml_postproc_factory(priv, priv->model_type_str);
      if (priv->func_post_process && strlen(priv->label_file) > 0) {
        if (get_label_from_file(priv->label_file, priv->labels, &priv->valid_label_count) < 0) {
          GST_WARNING_OBJECT(self, "Failed to load label file %s", priv->label_file);
        }
      }
    } break;
    case PROP_LABEL: {
      const gchar *s = g_value_get_string(value);
      strncpy(priv->label_file, s ? s : "", sizeof(priv->label_file) - 1);
      priv->label_file[sizeof(priv->label_file) - 1] = '\0';
      if (priv->func_post_process && strlen(priv->label_file) > 0) {
        if (get_label_from_file(priv->label_file, priv->labels, &priv->valid_label_count) < 0) {
          GST_WARNING_OBJECT(self, "Failed to load label file %s", priv->label_file);
        }
      }
    } break;
    case PROP_CONF_THRESHOLD:
      priv->conf_threshold = g_value_get_float(value);
      break;
    case PROP_NMS:
      priv->nms_threshold = g_value_get_float(value);
      break;
    case PROP_TOP_K:
      priv->top_k = g_value_get_int(value);
      break;
    case PROP_USE_MULTI_CLS:
      priv->use_multi_cls = g_value_get_int(value);
      break;
    case PROP_COORD_RESOLUTION: {
      const gchar *s = g_value_get_string(value);
      strncpy(priv->coord_res, s ? s : "", sizeof(priv->coord_res) - 1);
      priv->coord_res[sizeof(priv->coord_res) - 1] = '\0';
      parse_coord_resolution(priv->coord_res, &priv->map_width, &priv->map_height);
      if ((priv->map_width <= 0 || priv->map_height <= 0) && priv->coord_res[0] != '\0') {
        GST_WARNING_OBJECT(self, "coord_res '%s' invalid; expect WIDTHxHEIGHT e.g. 1920x1080", priv->coord_res);
      }
    } break;
    case PROP_REFERENCE_EMBEDDING: {
      const gchar *s = g_value_get_string(value);
      strncpy(priv->reference_embedding_path, s ? s : "",
          sizeof(priv->reference_embedding_path) - 1);
      priv->reference_embedding_path[sizeof(priv->reference_embedding_path) - 1] = '\0';
      mlpp_reload_clip_reference(self);
    } break;
    case PROP_REFERENCE_LABEL: {
      const gchar *s = g_value_get_string(value);
      strncpy(priv->reference_label, s ? s : "",
          sizeof(priv->reference_label) - 1);
      priv->reference_label[sizeof(priv->reference_label) - 1] = '\0';
    } break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
      break;
  }
}

static void gst_ml_postprocess_get_property(GObject *object, guint prop_id,
    GValue *value, GParamSpec *pspec)
{
  GstMlPostprocess *self = GST_ML_POSTPROCESS(object);
  mlpp_priv_ctx_t *priv = self->priv;

  switch (prop_id) {
    case PROP_TYPE:
      g_value_set_string(value, priv->model_type_str);
      break;
    case PROP_LABEL:
      g_value_set_string(value, priv->label_file);
      break;
    case PROP_CONF_THRESHOLD:
      g_value_set_float(value, priv->conf_threshold);
      break;
    case PROP_NMS:
      g_value_set_float(value, priv->nms_threshold);
      break;
    case PROP_TOP_K:
      g_value_set_int(value, priv->top_k);
      break;
    case PROP_USE_MULTI_CLS:
      g_value_set_int(value, priv->use_multi_cls);
      break;
    case PROP_COORD_RESOLUTION:
      g_value_set_string(value, priv->coord_res);
      break;
    case PROP_REFERENCE_EMBEDDING:
      g_value_set_string(value, priv->reference_embedding_path);
      break;
    case PROP_REFERENCE_LABEL:
      g_value_set_string(value, priv->reference_label);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
      break;
  }
}

static void
mlpp_log_clip_match_score(GstMlPostprocess *self, const amba_ml_embedding_body_t *body)
{
  mlpp_priv_ctx_t *priv = self->priv;

  if (!body)
    return;

  if (body->match_valid) {
    if (body->match_label[0]) {
      GST_INFO_OBJECT(self, "match_score=%.6f label=%s dim=%u",
          body->match_score, body->match_label, (unsigned) body->dim);
    } else {
      GST_INFO_OBJECT(self, "match_score=%.6f dim=%u",
          body->match_score, (unsigned) body->dim);
    }
    return;
  }

  if (priv->clip_ref_valid && body->dim > 0 && priv->clip_ref_dim != body->dim) {
    GST_WARNING_OBJECT(self,
        "match_score unavailable: reference dim %u != embedding dim %u",
        priv->clip_ref_dim, body->dim);
  } else if (priv->reference_embedding_path[0] && !priv->clip_ref_valid) {
    GST_WARNING_OBJECT(self,
        "match_score unavailable: failed to load reference-embedding %s",
        priv->reference_embedding_path);
  }
}

static GstStateChangeReturn gst_ml_postprocess_change_state(GstElement *element, GstStateChange transition)
{
  GstMlPostprocess *self = GST_ML_POSTPROCESS(element);
  mlpp_priv_ctx_t *priv = self->priv;
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED: {
      if (self->src && gst_pad_is_linked(self->src) &&
          (priv->map_width <= 0 || priv->map_height <= 0)) {
        GST_ELEMENT_ERROR(element, CORE, NEGOTIATION,
            ("coord_res is required when src pad is linked"),
            ("Set coord_res property, e.g. coord_res=1920x1080"));
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    }
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    return ret;
  }

  return ret;
}

/* dimensions: three ints + optional batch; physical layout fields from caps (see ml_tensors_caps.h). */
static gboolean parse_tensor_dimensions(const char *dim_str, int *width, int *height, int *channels)
{
  int w, h, c, d;
  if (sscanf(dim_str, "%d:%d:%d:%d", &w, &h, &c, &d) >= 3) {
    *width = w;
    *height = h;
    *channels = c;
    return TRUE;
  }
  return FALSE;
}

#define PITCH_ALIGN 16

/* YOLOP seg: same C:H:W vs W:H:C nnctrl quirk as yolov8 proto (see gstmlinference2.c comment). */
static gboolean
mlpp_tensor_is_yolop_seg_caps(const mlpp_priv_ctx_t *priv, int t, int num_tensors, const char *name)
{
  const char *m;

  m = priv->model_type_str;
  if (!m || m[0] == '\0') {
    return FALSE;
  }
  if (strncmp(m, "yolop", 5) != 0) {
    return FALSE;
  }
  /* 5-tensor: last two are seg. Must run before name checks: numeric out_name (e.g. 1871) is not
   * drive_area_seg, and would incorrectly return FALSE. */
  if (num_tensors == 5 && (t == 3 || t == 4)) {
    return TRUE;
  }
  if (name && name[0] &&
      (strcmp(name, "drive_area_seg") == 0 || strcmp(name, "lane_line_seg") == 0)) {
    return TRUE;
  }
  return FALSE;
}

static gboolean parse_tensor_caps_to_cache(GstMlPostprocess *self, GstCaps *tensor_caps)
{
  mlpp_priv_ctx_t *priv = self->priv;
  GstStructure *st = gst_caps_get_structure(tensor_caps, 0);
  const char *dim_str = gst_structure_get_string(st, "dimensions");
  const char *types_str = gst_structure_get_string(st, "types");
  const char *pitches_str = gst_structure_get_string(st, "pitches");
  const char *names_str = gst_structure_get_string(st, "names");
  const char *nn_res_str = gst_structure_get_string(st, "nn_input_res");
  gint num_tensors = 0;
  gst_structure_get_int(st, "num_tensors", &num_tensors);

  if (!dim_str || !types_str || num_tensors <= 0) {
    return FALSE;
  }

  gchar **dim_list = g_strsplit(dim_str, ",", -1);
  gchar **types_list = g_strsplit(types_str, ",", -1);
  gchar **pitches_list = pitches_str ? g_strsplit(pitches_str, ",", -1) : NULL;
  gchar **names_list = names_str ? g_strsplit(names_str, ",", -1) : NULL;
  if (!dim_list || !types_list) {
    g_strfreev(dim_list);
    g_strfreev(types_list);
    g_strfreev(pitches_list);
    g_strfreev(names_list);
    return FALSE;
  }

  int nn_w = 416, nn_h = 416;
  if (nn_res_str && nn_res_str[0]) {
    parse_coord_resolution(nn_res_str, &nn_w, &nn_h);
  }
  if (nn_w <= 0 || nn_h <= 0) {
    nn_w = 416, nn_h = 416;
  }
  /* nn_input is typically 320/416/512/640; if we got coord_res (e.g. 1920x1080)
   * by mistake, scaling would be x*mw/nw = x*1920/1920 = x, leaving 0..416
   * coords unscaled -> tiny boxes in top-left. Reject display-like values. */
  if (nn_w >= 1000 || nn_h >= 1000) {
    GST_WARNING_OBJECT(self, "nn_input_res %dx%d looks like coord_res, using 416x416",
        nn_w, nn_h);
    nn_w = 416;
    nn_h = 416;
  }

  /* Defer deinit_user_ctx to chain (see tensor_layout_dirty) — sync deinit here races with in-flight postproc. */
  if (priv->tensor_cache.caps) {
    if (!gst_caps_is_equal(priv->tensor_cache.caps, tensor_caps)) {
      priv->tensor_layout_dirty = TRUE;
    }
    gst_caps_unref(priv->tensor_cache.caps);
  }
  priv->tensor_cache.caps = NULL;
  priv->tensor_cache.num_tensors = num_tensors;
  memset(priv->tensor_cache.desc, 0, sizeof(priv->tensor_cache.desc));

  GST_DEBUG_OBJECT(self,
      "caps fields: num_tensors=%d types=\"%s\" dimensions=\"%s\" pitches=\"%s\" names=\"%s\" nn_input_res=\"%s\"",
      num_tensors, types_str, dim_str,
      pitches_str ? pitches_str : "",
      names_str ? names_str : "",
      nn_res_str ? nn_res_str : "");

  gsize raw_offset = 0, f32_offset = 0;
  guint t;
  for (t = 0; t < (guint)num_tensors && t < AMBA_ML_MAX_TENSORS && dim_list[t] && types_list[t]; t++) {
    int w = 0, h = 0, ch = 0;
    if (!parse_tensor_dimensions(dim_list[t], &w, &h, &ch)) {
      break;
    }

    if (mlpp_tensor_is_yolop_seg_caps(priv, (int)t, num_tensors, names_list ? names_list[t] : NULL)) {
      int ew, eh, ed;
      mlpp_yolop_seg_effective_dims(w, h, ch, &ew, &eh, &ed);
      w = ew;
      h = eh;
      ch = ed;
    }

    gboolean is_f16 = (g_strcmp0(types_list[t], "float16") == 0);
    guint elem = is_f16 ? 2u : 4u;
    guint pitch;
    if (pitches_list && pitches_list[t]) {
      pitch = (guint)atoi(pitches_list[t]);
      if (pitch == 0) {
        pitch = (w * elem + PITCH_ALIGN - 1) & ~(PITCH_ALIGN - 1);
      }
    } else {
      pitch = (w * elem + PITCH_ALIGN - 1) & ~(PITCH_ALIGN - 1);
    }

    priv->tensor_cache.desc[t].w = w;
    priv->tensor_cache.desc[t].h = h;
    priv->tensor_cache.desc[t].ch = ch;
    priv->tensor_cache.desc[t].is_float16 = is_f16;
    priv->tensor_cache.desc[t].pitch_bytes = pitch;
    priv->tensor_cache.desc[t].raw_offset = raw_offset;
    if (is_f16) {
      priv->tensor_cache.desc[t].f32_offset = f32_offset;
      f32_offset += (gsize)w * h * ch * sizeof(float);
    }
    if (names_list && names_list[t] && names_list[t][0]) {
      g_strlcpy(priv->tensor_cache.desc[t].name, names_list[t], ML_POSTPROC_TENSOR_NAME_LEN);
    } else {
      priv->tensor_cache.desc[t].name[0] = '\0';
    }

    GST_DEBUG_OBJECT(self,
        "tensor[%u] parsed: w:h:c=%d:%d:%d type=%s pitch_bytes=%u float16=%d name=\"%s\"",
        t, w, h, ch, types_list[t], pitch, is_f16 ? 1 : 0,
        priv->tensor_cache.desc[t].name);

    raw_offset += (gsize)h * ch * pitch;
  }
  priv->tensor_cache.total_f32_convert = f32_offset;

  /* Fallback: derive nn_input from YOLOv5-style tensor dims when caps had wrong/missing nn_input_res */
  if (nn_w >= 1000 || nn_h >= 1000 || nn_w <= 0 || nn_h <= 0) {
    int max_wh = 0;
    for (t = 0; t < (guint)num_tensors && t < AMBA_ML_MAX_TENSORS && dim_list[t]; t++) {
      int w = 0, h = 0, ch = 0;
      if (parse_tensor_dimensions(dim_list[t], &w, &h, &ch) && w > 0 && h > 0) {
        int s = (w > h) ? w : h;
        if (s > max_wh) {
          max_wh = s;
        }
      }
    }
    if (max_wh > 0 && max_wh <= 80) {
      int inferred = (max_wh == 52) ? 416 : (max_wh == 80) ? 640 : max_wh * 8;
      nn_w = inferred;
      nn_h = inferred;
      GST_INFO_OBJECT(self, "Inferred nn_input=%dx%d from tensor dim (max=%d)",
          nn_w, nn_h, max_wh);
    } else {
      nn_w = 416;
      nn_h = 416;
    }
  }

  priv->tensor_cache.nn_input_w = nn_w;
  priv->tensor_cache.nn_input_h = nn_h;

  g_strfreev(dim_list);
  g_strfreev(types_list);
  g_strfreev(pitches_list);
  g_strfreev(names_list);

  priv->tensor_cache.caps = gst_caps_ref(tensor_caps);
  GST_DEBUG_OBJECT(self, "Parsed caps: num_tensors=%d nn_input=%dx%d",
      num_tensors, priv->tensor_cache.nn_input_w, priv->tensor_cache.nn_input_h);
  return TRUE;
}

static gboolean gst_ml_postprocess_sink_event(GstPad *pad, GstObject *parent, GstEvent *event)
{
  GstMlPostprocess *self = GST_ML_POSTPROCESS(parent);
  mlpp_priv_ctx_t *priv = self->priv;

  if (GST_EVENT_TYPE(event) == GST_EVENT_CAPS) {
    GstCaps *caps;
    gst_event_parse_caps(event, &caps);
    if (caps && parse_tensor_caps_to_cache(self, caps)) {
      GST_DEBUG_OBJECT(self, "Parsed and cached tensor caps from caps event");
    }

    /* Push output caps before segment - sticky order: caps then segment.
     * Determine output type from post-processor so negotiation completes early. */
    if (priv->map_width > 0 && priv->map_height > 0 && self->src && gst_pad_is_linked(self->src) &&
        !gst_pad_has_current_caps(self->src)) {
      const ml_postproc_output_pad_spec_t *specs = NULL;
      int pad_count = 0;
      if (priv->postprocess_ops && priv->postprocess_ops->get_output_pads) {
        specs = priv->postprocess_ops->get_output_pads(&pad_count);
      }

      gboolean video_only = (specs && pad_count == 1 && specs[0].kind == ML_POSTPROC_PAD_VIDEO_GRAY8);

      if (video_only) {
        GstCaps *ocaps = gst_caps_new_simple("video/x-raw",
            "format", G_TYPE_STRING, "GRAY8",
            "width", G_TYPE_INT, priv->map_width,
            "height", G_TYPE_INT, priv->map_height,
            NULL);
        mlpp_set_src_pad_caps (GST_OBJECT (self), self->src, ocaps);
        gst_caps_unref(ocaps);
      } else {
        gchar coord_res_str[32];
        g_snprintf(coord_res_str, sizeof(coord_res_str), "%dx%d", priv->map_width, priv->map_height);
        GstCaps *ocaps = gst_caps_new_simple(GST_AMBA_ML_DECODED_CAPS,
            "coord_res", G_TYPE_STRING, coord_res_str,
            NULL);
        mlpp_set_src_pad_caps (GST_OBJECT (self), self->src, ocaps);
        gst_caps_unref(ocaps);
      }
    }
  }

  return gst_pad_event_default(pad, parent, event);
}

/* Resolve output layout: from ops->get_output_layout(); default bbox-only when absent */
static void resolve_output_layout(const ml_postproc_ops_t *ops,
    ml_postproc_output_layout_t *out_layout)
{
  if (ops && ops->get_output_layout) {
    const ml_postproc_output_layout_t *layout = ops->get_output_layout();
    if (layout && layout->n_entries > 0) {
      *out_layout = *layout;
      return;
    }
  }
  out_layout->entries[0].type = AMBA_ML_RESULT_TYPE_DETECTION_BBOX;
  out_layout->entries[0].seg_idx = -1;
  out_layout->n_entries = 1;
}

static gboolean
layout_needs_bbox_coord_scale(const ml_postproc_output_layout_t *layout)
{
  guint i;

  if (!layout)
    return TRUE;
  for (i = 0; i < (guint)layout->n_entries; i++) {
    if (layout->entries[i].type == AMBA_ML_RESULT_TYPE_DETECTION_BBOX)
      return TRUE;
  }
  return FALSE;
}

/* Build single buffer with multiple GstMemory using layout-driven generic serialization.
   * Attach GstAmbaMlDecodedMeta to describe each memory. */
static GstFlowReturn gst_ml_postprocess_chain(GstPad *pad, GstObject *parent, GstBuffer *tensor_buf)
{
  (void)pad;
  GstMlPostprocess *self = GST_ML_POSTPROCESS(parent);
  mlpp_priv_ctx_t *priv = self->priv;
  GstFlowReturn ret = GST_FLOW_OK;
  guint i;
  GstCaps *tensor_caps = NULL;
  ml_postproc_ctx_t pp_ctx;
  GstMapInfo map_info[AMBA_ML_MAX_TENSORS];
  int mw, mh, nw, nh;
  int post_ok;

  GST_DEBUG_OBJECT(self, "chain: received tensor buffer, pts=%" GST_TIME_FORMAT,
      GST_TIME_ARGS(GST_BUFFER_PTS(tensor_buf)));

  if (priv->map_width <= 0 || priv->map_height <= 0) {
    GST_ERROR_OBJECT(self, "coord_res is required and must be valid (e.g. 1920x1080)");
    gst_buffer_unref(tensor_buf);
    return GST_FLOW_ERROR;
  }

  if (!self->src || !gst_pad_is_linked(self->src)) {
    GST_DEBUG_OBJECT(self, "src pad not linked, dropping");
    gst_buffer_unref(tensor_buf);
    return GST_FLOW_OK;
  }

  if (!priv->func_post_process) {
    GST_DEBUG_OBJECT(self, "No post-process configured, output empty detection");
    GstBuffer *out_buf = gst_buffer_new_allocate(NULL, sizeof(amba_ml_bbox_result_t), NULL);
    if (out_buf) {
      GstMapInfo omap;
      if (gst_buffer_map(out_buf, &omap, GST_MAP_WRITE)) {
        amba_ml_bbox_result_t *empty_res = (amba_ml_bbox_result_t *)omap.data;
        memset(empty_res, 0, sizeof(amba_ml_bbox_result_t));
        empty_res->header.magic = AMBA_ML_RESULT_MAGIC;
        empty_res->header.type = AMBA_ML_RESULT_TYPE_DETECTION_BBOX;
        empty_res->header.version = AMBA_ML_RESULT_API_VERSION;
        gst_buffer_unmap(out_buf, &omap);
      }
      GST_BUFFER_PTS(out_buf) = GST_BUFFER_PTS(tensor_buf);
      GST_BUFFER_DTS(out_buf) = GST_BUFFER_DTS(tensor_buf);
      amba_buffer_copy_private_data_meta(out_buf, tensor_buf);

      GstAmbaMlDecodedMeta *meta = (GstAmbaMlDecodedMeta *)gst_buffer_add_meta(out_buf,
          GST_AMBA_ML_DECODED_META_INFO, NULL);
      if (meta) {
        meta->n_entries = 1;
        meta->entries[0].type = AMBA_ML_RESULT_TYPE_DETECTION_BBOX;
        meta->entries[0].offset = 0;
        meta->entries[0].size = sizeof(amba_ml_bbox_result_t);
        meta->entries[0].width = 0;
        meta->entries[0].height = 0;
      }

      if (!gst_pad_has_current_caps(self->src)) {
        gchar coord_res_str[32];
        g_snprintf(coord_res_str, sizeof(coord_res_str), "%dx%d", priv->map_width, priv->map_height);
        GstCaps *caps = gst_caps_new_simple(GST_AMBA_ML_DECODED_CAPS,
            "coord_res", G_TYPE_STRING, coord_res_str,
            NULL);
        mlpp_set_src_pad_caps (GST_OBJECT (self), self->src, caps);
        gst_caps_unref(caps);
      }
      ret = gst_pad_push(self->src, out_buf);
    } else {
      GST_ERROR_OBJECT (self, "Failed to allocate output buffer (empty result path)");
      ret = GST_FLOW_ERROR;
    }
    gst_buffer_unref(tensor_buf);
    return ret;
  }

  if (priv->tensor_layout_dirty) {
    if (priv->postprocess_ops && priv->postprocess_ops->deinit_user_ctx) {
      priv->postprocess_ops->deinit_user_ctx(priv);
    }
    priv->tensor_layout_dirty = FALSE;
  }

  if (!priv->tensor_cache.caps) {
    tensor_caps = gst_pad_get_current_caps(self->sink_tensor);
    if (!tensor_caps) {
      GST_ERROR_OBJECT(self, "No caps on tensor pad");
      gst_buffer_unref(tensor_buf);
      return GST_FLOW_ERROR;
    }
    if (!parse_tensor_caps_to_cache(self, tensor_caps)) {
      gst_caps_unref(tensor_caps);
      GST_ERROR_OBJECT(self, "Failed to parse tensor caps");
      gst_buffer_unref(tensor_buf);
      return GST_FLOW_ERROR;
    }
    gst_caps_unref(tensor_caps);
  }

  memset(&pp_ctx, 0, sizeof(pp_ctx));
  pp_ctx.nn_input_width = priv->tensor_cache.nn_input_w;
  pp_ctx.nn_input_height = priv->tensor_cache.nn_input_h;
  pp_ctx.conf_threshold = priv->conf_threshold;
  pp_ctx.nms_threshold = priv->nms_threshold;
  pp_ctx.top_k = priv->top_k;
  pp_ctx.use_multi_cls = priv->use_multi_cls;
  pp_ctx.valid_label_count = priv->valid_label_count;
  memcpy(pp_ctx.labels, priv->labels, sizeof(pp_ctx.labels));
  pp_ctx.result = &priv->bboxs;
  pp_ctx.user_data = priv;
  pp_ctx.clip_ref_feature = priv->clip_ref_feature;
  pp_ctx.clip_ref_dim = priv->clip_ref_dim;
  pp_ctx.clip_ref_valid = priv->clip_ref_valid;
  pp_ctx.clip_ref_label = priv->reference_label[0] ? priv->reference_label : NULL;

  GstMemory *tensor_mem = gst_buffer_peek_memory(tensor_buf, 0);
  if (!tensor_mem || !gst_memory_map(tensor_mem, &map_info[0], GST_MAP_READ)) {
    GST_ERROR_OBJECT(self, "Failed to map tensor buffer");
    gst_buffer_unref(tensor_buf);
    return GST_FLOW_ERROR;
  }

  float *f32_convert_buf = NULL;
  {
    unsigned char *base = map_info[0].data;
    const int num_t = priv->tensor_cache.num_tensors;
    const gsize need_f32 = priv->tensor_cache.total_f32_convert;

    if (need_f32 > 0) {
      if (need_f32 > priv->f32_convert_buf_bytes) {
        gpointer nb = g_try_realloc(priv->f32_convert_buf, need_f32);
        if (!nb) {
          GST_ERROR_OBJECT(self, "Failed to allocate f16 conversion buffer (%" G_GSIZE_FORMAT " bytes)",
              need_f32);
          gst_memory_unmap(tensor_mem, &map_info[0]);
          gst_buffer_unref(tensor_buf);
          return GST_FLOW_ERROR;
        }
        priv->f32_convert_buf = (float *)nb;
        priv->f32_convert_buf_bytes = need_f32;
      }
      f32_convert_buf = priv->f32_convert_buf;
    }

    for (i = 0; i < (guint)num_t && i < AMBA_ML_MAX_TENSORS; i++) {
      const int w = priv->tensor_cache.desc[i].w;
      const int h = priv->tensor_cache.desc[i].h;
      const int ch = priv->tensor_cache.desc[i].ch;
      const gboolean is_float16 = priv->tensor_cache.desc[i].is_float16;
      const guint pitch_bytes = priv->tensor_cache.desc[i].pitch_bytes;
      const gsize offset = priv->tensor_cache.desc[i].raw_offset;
      const gsize tensor_raw_size = (gsize)h * ch * pitch_bytes;

      if (base + offset + tensor_raw_size > map_info[0].data + map_info[0].size) {
        GST_WARNING_OBJECT(self, "Tensor %u exceeds buffer bounds", i);
        break;
      }

      if (is_float16) {
        gushort *f16_src = (gushort *)(base + offset);
        float *f32_dst = f32_convert_buf + priv->tensor_cache.desc[i].f32_offset / sizeof(float);
        int c, j, k;
        for (c = 0; c < ch; c++) {
          for (j = 0; j < h; j++) {
            for (k = 0; k < w; k++) {
#if defined(MLPP_FP16_USE_NATIVE_HALF)
              f32_dst[k] = (float)(*(_Float16 *)(f16_src + k));
#else
              gushort hbits = f16_src[k];
              guint sign = (hbits >> 15) & 1;
              gint exp = (hbits >> 10) & 0x1f;
              guint mant = hbits & 0x3ff;
              float f;
              if (exp == 0) {
                f = (mant == 0) ? 0.0f : (mant / 1024.0f) * 0.00006103515625f;
              } else if (exp == 31) {
                f = (mant == 0) ? (sign ? -INFINITY : INFINITY) : NAN;
              } else {
                f = (1.0f + mant / 1024.0f) * powf(2.0f, (float)(exp - 15));
              }
              f32_dst[k] = sign ? -f : f;
#endif
            }
            f16_src = (gushort *)((guchar *)f16_src + pitch_bytes);
            f32_dst += w;
          }
        }
        pp_ctx.tensors[i].data = f32_convert_buf + priv->tensor_cache.desc[i].f32_offset / sizeof(float);
      } else {
        pp_ctx.tensors[i].data = (float *)(base + offset);
      }
      pp_ctx.tensors[i].width = w;
      pp_ctx.tensors[i].height = h;
      pp_ctx.tensors[i].depth = ch;
      /* Use actual pitch from caps (model may use aligned stride); f16-converted uses w*4 */
      pp_ctx.tensors[i].pitch = is_float16 ? (w * (int)sizeof(float)) : (int)pitch_bytes;
      g_strlcpy(pp_ctx.tensors[i].name, priv->tensor_cache.desc[i].name, ML_POSTPROC_TENSOR_NAME_LEN);
      pp_ctx.num_tensors++;
    }
  }

  mw = priv->map_width;
  mh = priv->map_height;
  nw = priv->tensor_cache.nn_input_w;
  nh = priv->tensor_cache.nn_input_h;
  memset(&priv->bboxs, 0, sizeof(priv->bboxs));
  post_ok = priv->func_post_process(&pp_ctx);

  /* Video-only: single image output (e.g. pure segmentation) -> video/x-raw directly */
  {
    const ml_postproc_output_pad_spec_t *specs = NULL;
    int pad_count = 0;
    if (priv->postprocess_ops && priv->postprocess_ops->get_output_pads) {
      specs = priv->postprocess_ops->get_output_pads(&pad_count);
    }

    gboolean video_only = (specs && pad_count == 1 && specs[0].kind == ML_POSTPROC_PAD_VIDEO_GRAY8);
    ml_postproc_seg_output_t *seg = &pp_ctx.seg_outputs[0];

    if (video_only && post_ok >= 0 && seg->mask && seg->width > 0 && seg->height > 0) {
      int sw = seg->width, sh = seg->height;
      gsize dst_size = (gsize)mw * mh;
      guint8 *vbuf = (guint8 *)g_malloc(dst_size);
      if (vbuf) {
        int dy, dx;
        for (dy = 0; dy < mh; dy++) {
          int sy = (mh > 1 && sh > 0) ? (dy * (sh - 1) / (mh - 1)) : 0;
          if (sy >= sh) {
            sy = sh - 1;
          }
          for (dx = 0; dx < mw; dx++) {
            int sx = (mw > 1 && sw > 0) ? (dx * (sw - 1) / (mw - 1)) : 0;
            if (sx >= sw) {
              sx = sw - 1;
            }
            vbuf[dy * mw + dx] = seg->mask[sy * sw + sx];
          }
        }
        free(seg->mask);
        seg->mask = NULL;

        GstBuffer *vid_buf = gst_buffer_new_wrapped_full(0, vbuf, dst_size, 0, dst_size, vbuf, g_free);
        GST_BUFFER_PTS(vid_buf) = GST_BUFFER_PTS(tensor_buf);
        GST_BUFFER_DTS(vid_buf) = GST_BUFFER_DTS(tensor_buf);
        amba_buffer_copy_private_data_meta(vid_buf, tensor_buf);
        gst_buffer_add_video_meta(vid_buf, GST_VIDEO_FRAME_FLAG_NONE,
            GST_VIDEO_FORMAT_GRAY8, mw, mh);

        if (!gst_pad_has_current_caps(self->src)) {
          GstCaps *vcaps = gst_caps_new_simple("video/x-raw",
              "format", G_TYPE_STRING, "GRAY8",
              "width", G_TYPE_INT, mw,
              "height", G_TYPE_INT, mh,
              NULL);
          mlpp_set_src_pad_caps (GST_OBJECT (self), self->src, vcaps);
          gst_caps_unref(vcaps);
        }

        gst_memory_unmap(tensor_mem, &map_info[0]);
        gst_buffer_unref(tensor_buf);
        ret = gst_pad_push(self->src, vid_buf);
        return ret;
      }
      free(seg->mask);
      seg->mask = NULL;
    }
  }

  /* Resolve layout and serialize generically */
  ml_postproc_output_layout_t layout;
  resolve_output_layout(priv->postprocess_ops, &layout);

  if (nw <= 0) {
    nw = 1;
  }
  if (nh <= 0) {
    nh = 1;
  }
  /* Bbox pixel scaling only; embedding/classification/pose ignore nw/nh here. */
  if (layout_needs_bbox_coord_scale(&layout) && (nw >= mw || nh >= mh)) {
    GST_WARNING_OBJECT(self, "nn_input %dx%d >= coord_res %dx%d, using 416 for scaling",
        nw, nh, mw, mh);
    nw = 416;
    nh = 416;
  }

  GstBuffer *out_buf = gst_buffer_new();
  GstAmbaMlDecodedMeta *meta = (GstAmbaMlDecodedMeta *)gst_buffer_add_meta(out_buf,
      GST_AMBA_ML_DECODED_META_INFO, NULL);
  gsize offset_acc = 0;

  for (i = 0; i < (guint)layout.n_entries && meta && meta->n_entries < GST_AMBA_ML_DECODED_META_MAX_ENTRIES; i++) {
    const ml_postproc_layout_entry_t *ent = &layout.entries[i];

    if (ent->type == AMBA_ML_RESULT_TYPE_DETECTION_BBOX) {
      amba_ml_bbox_result_t result;
      memset(&result, 0, sizeof(result));
      result.header.magic = AMBA_ML_RESULT_MAGIC;
      result.header.type = AMBA_ML_RESULT_TYPE_DETECTION_BBOX;
      result.header.version = AMBA_ML_RESULT_API_VERSION;

      if (post_ok >= 0) {
        bounding_boxes_t *boxes = &priv->bboxs;
        unsigned int k;
        result.detections.det_num = boxes->det_num;
        if (boxes->det_num > 0) {
          GST_INFO_OBJECT(self, "det_num=%u (nn=%dx%d)", boxes->det_num, nw, nh);
        }
        if (result.detections.det_num > AMBA_ML_DETECTION_MAX_NUM) {
          result.detections.det_num = AMBA_ML_DETECTION_MAX_NUM;
        }
        for (k = 0; k < result.detections.det_num; k++) {
          det_object_t *b = &boxes->detections[k];
          int32_t xs, ys, xe, ye;
          result.detections.detections[k].class_id = b->id;
          result.detections.detections[k].score = b->score;
          strncpy(result.detections.detections[k].label, b->label, AMBA_ML_DETECTION_LABEL_LEN - 1);
          result.detections.detections[k].label[AMBA_ML_DETECTION_LABEL_LEN - 1] = '\0';
          if (priv->postprocess_ops && priv->postprocess_ops->output_coords_normalized) {
            xs = (int32_t)(b->x_start * mw);
            ys = (int32_t)(b->y_start * mh);
            xe = (int32_t)(b->x_end * mw);
            ye = (int32_t)(b->y_end * mh);
          } else {
            xs = (int32_t)(b->x_start * mw / nw);
            ys = (int32_t)(b->y_start * mh / nh);
            xe = (int32_t)(b->x_end * mw / nw);
            ye = (int32_t)(b->y_end * mh / nh);
          }
          if (xs > xe) {
            int32_t t = xs;
            xs = xe;
            xe = t;
          }
          if (ys > ye) {
            int32_t t = ys;
            ys = ye;
            ye = t;
          }
          result.detections.detections[k].x_start = xs;
          result.detections.detections[k].y_start = ys;
          result.detections.detections[k].x_end = xe;
          result.detections.detections[k].y_end = ye;
          result.detections.detections[k].flags = 0;
          memset(result.detections.detections[k].landmark_x, 0,
              sizeof(result.detections.detections[k].landmark_x));
          memset(result.detections.detections[k].landmark_y, 0,
              sizeof(result.detections.detections[k].landmark_y));
          if (b->has_landmarks) {
            int pi;
            result.detections.detections[k].flags |= AMBA_ML_DETECTION_FLAG_HAS_LANDMARKS;
            for (pi = 0; pi < 5; pi++) {
              float lx = b->landmark[pi * 2];
              float ly = b->landmark[pi * 2 + 1];
              if (priv->postprocess_ops && priv->postprocess_ops->output_coords_normalized) {
                result.detections.detections[k].landmark_x[pi] = (int32_t)(lx * mw);
                result.detections.detections[k].landmark_y[pi] = (int32_t)(ly * mh);
              } else {
                result.detections.detections[k].landmark_x[pi] = (int32_t)(lx * mw / nw);
                result.detections.detections[k].landmark_y[pi] = (int32_t)(ly * mh / nh);
              }
            }
          }
        }
      } else {
        GST_WARNING_OBJECT(self, "Post-process returned error (%d), no detections", post_ok);
        GST_DEBUG_OBJECT(self, "tensor_cache: num=%d nn=%dx%d", priv->tensor_cache.num_tensors,
            priv->tensor_cache.nn_input_w, priv->tensor_cache.nn_input_h);
      }

      GstMemory *mem = gst_allocator_alloc(NULL, sizeof(result), NULL);
      if (mem) {
        GstMapInfo omap;
        if (gst_memory_map(mem, &omap, GST_MAP_WRITE)) {
          memcpy(omap.data, &result, sizeof(result));
          gst_memory_unmap(mem, &omap);
        }
        gst_buffer_append_memory(out_buf, mem);
        if (meta) {
          meta->entries[meta->n_entries].type = AMBA_ML_RESULT_TYPE_DETECTION_BBOX;
          meta->entries[meta->n_entries].offset = offset_acc;
          meta->entries[meta->n_entries].size = sizeof(result);
          meta->entries[meta->n_entries].width = 0;
          meta->entries[meta->n_entries].height = 0;
          meta->n_entries++;
        }
        offset_acc += sizeof(result);
      }
    } else if (ent->type == AMBA_ML_RESULT_TYPE_CLASSIFICATION) {
      amba_ml_classification_result_t cls_res;
      memset(&cls_res, 0, sizeof(cls_res));
      cls_res.header.magic = AMBA_ML_RESULT_MAGIC;
      cls_res.header.type = AMBA_ML_RESULT_TYPE_CLASSIFICATION;
      cls_res.header.version = AMBA_ML_RESULT_API_VERSION;
      if (post_ok >= 0) {
        cls_res.body = pp_ctx.classification;
      } else {
        GST_WARNING_OBJECT(self, "Post-process returned error (%d), empty classification", post_ok);
      }

      {
        GstMemory *mem = gst_allocator_alloc(NULL, sizeof(cls_res), NULL);
        if (mem) {
          GstMapInfo omap;
          if (gst_memory_map(mem, &omap, GST_MAP_WRITE)) {
            memcpy(omap.data, &cls_res, sizeof(cls_res));
            gst_memory_unmap(mem, &omap);
          }
          gst_buffer_append_memory(out_buf, mem);
          if (meta) {
            meta->entries[meta->n_entries].type = AMBA_ML_RESULT_TYPE_CLASSIFICATION;
            meta->entries[meta->n_entries].offset = offset_acc;
            meta->entries[meta->n_entries].size = sizeof(cls_res);
            meta->entries[meta->n_entries].width = 0;
            meta->entries[meta->n_entries].height = 0;
            meta->n_entries++;
          }
          offset_acc += sizeof(cls_res);
        }
      }
    } else if (ent->type == AMBA_ML_RESULT_TYPE_POSE) {
      amba_ml_pose_result_t pose_res;
      memset(&pose_res, 0, sizeof(pose_res));
      pose_res.header.magic = AMBA_ML_RESULT_MAGIC;
      pose_res.header.type = AMBA_ML_RESULT_TYPE_POSE;
      pose_res.header.version = AMBA_ML_RESULT_API_VERSION;
      if (post_ok >= 0) {
        pose_res.body = pp_ctx.pose;
      } else {
        GST_WARNING_OBJECT(self, "Post-process returned error (%d), empty pose", post_ok);
      }

      {
        GstMemory *mem = gst_allocator_alloc(NULL, sizeof(pose_res), NULL);
        if (mem) {
          GstMapInfo omap;
          if (gst_memory_map(mem, &omap, GST_MAP_WRITE)) {
            memcpy(omap.data, &pose_res, sizeof(pose_res));
            gst_memory_unmap(mem, &omap);
          }
          gst_buffer_append_memory(out_buf, mem);
          if (meta) {
            meta->entries[meta->n_entries].type = AMBA_ML_RESULT_TYPE_POSE;
            meta->entries[meta->n_entries].offset = offset_acc;
            meta->entries[meta->n_entries].size = sizeof(pose_res);
            meta->entries[meta->n_entries].width = 0;
            meta->entries[meta->n_entries].height = 0;
            meta->n_entries++;
          }
          offset_acc += sizeof(pose_res);
        }
      }
    } else if (ent->type == AMBA_ML_RESULT_TYPE_EMBEDDING) {
      amba_ml_embedding_result_t emb_res;
      memset(&emb_res, 0, sizeof(emb_res));
      emb_res.header.magic = AMBA_ML_RESULT_MAGIC;
      emb_res.header.type = AMBA_ML_RESULT_TYPE_EMBEDDING;
      emb_res.header.version = AMBA_ML_RESULT_API_VERSION;
      if (post_ok >= 0) {
        emb_res.body = pp_ctx.embedding;
        mlpp_log_clip_match_score(self, &emb_res.body);
      } else {
        GST_WARNING_OBJECT(self, "Post-process returned error (%d), empty embedding", post_ok);
      }

      {
        GstMemory *mem = gst_allocator_alloc(NULL, sizeof(emb_res), NULL);
        if (mem) {
          GstMapInfo omap;
          if (gst_memory_map(mem, &omap, GST_MAP_WRITE)) {
            memcpy(omap.data, &emb_res, sizeof(emb_res));
            gst_memory_unmap(mem, &omap);
          }
          gst_buffer_append_memory(out_buf, mem);
          if (meta) {
            meta->entries[meta->n_entries].type = AMBA_ML_RESULT_TYPE_EMBEDDING;
            meta->entries[meta->n_entries].offset = offset_acc;
            meta->entries[meta->n_entries].size = sizeof(emb_res);
            meta->entries[meta->n_entries].width = 0;
            meta->entries[meta->n_entries].height = 0;
            meta->n_entries++;
          }
          offset_acc += sizeof(emb_res);
        }
      }
    } else if ((ent->type == AMBA_ML_RESULT_TYPE_SEGMENTATION ||
        ent->type == AMBA_ML_RESULT_TYPE_CUSTOM) &&
        ent->seg_idx >= 0 && ent->seg_idx < ML_POSTPROC_MAX_SEG_OUTPUTS && post_ok >= 0) {
      ml_postproc_seg_output_t *seg = &pp_ctx.seg_outputs[ent->seg_idx];
      if (seg->mask && seg->width > 0 && seg->height > 0) {
        int sw = seg->width, sh = seg->height;
        gsize dst_size = (gsize)mw * mh;
        guint8 *vbuf = (guint8 *)g_malloc(dst_size);
        if (vbuf) {
          int dy, dx;
          for (dy = 0; dy < mh; dy++) {
            int sy = (mh > 1 && sh > 0) ? (dy * (sh - 1) / (mh - 1)) : 0;
            if (sy >= sh) {
              sy = sh - 1;
            }
            for (dx = 0; dx < mw; dx++) {
              int sx = (mw > 1 && sw > 0) ? (dx * (sw - 1) / (mw - 1)) : 0;
              if (sx >= sw) {
                sx = sw - 1;
              }
              vbuf[dy * mw + dx] = seg->mask[sy * sw + sx];
            }
          }
          free(seg->mask);
          seg->mask = NULL;

          GstMemory *vid_mem = gst_memory_new_wrapped(0, vbuf, dst_size, 0, dst_size, vbuf, g_free);
          gst_buffer_append_memory(out_buf, vid_mem);
          if (meta) {
            meta->entries[meta->n_entries].type = ent->type;
            meta->entries[meta->n_entries].offset = offset_acc;
            meta->entries[meta->n_entries].size = dst_size;
            meta->entries[meta->n_entries].width = (guint)mw;
            meta->entries[meta->n_entries].height = (guint)mh;
            meta->n_entries++;
          }
          offset_acc += dst_size;
        } else {
          free(seg->mask);
          seg->mask = NULL;
        }
      }
    }
  }

  /* Free any remaining seg masks */
  for (i = 0; i < ML_POSTPROC_MAX_SEG_OUTPUTS; i++) {
    if (pp_ctx.seg_outputs[i].mask) {
      free(pp_ctx.seg_outputs[i].mask);
      pp_ctx.seg_outputs[i].mask = NULL;
    }
  }

  GST_BUFFER_PTS(out_buf) = GST_BUFFER_PTS(tensor_buf);
  GST_BUFFER_DTS(out_buf) = GST_BUFFER_DTS(tensor_buf);
  amba_buffer_copy_private_data_meta(out_buf, tensor_buf);

  if (!gst_pad_has_current_caps(self->src)) {
    gchar coord_res_str[32];
    g_snprintf(coord_res_str, sizeof(coord_res_str), "%dx%d", mw, mh);
    GstCaps *caps = gst_caps_new_simple(GST_AMBA_ML_DECODED_CAPS,
        "coord_res", G_TYPE_STRING, coord_res_str,
        NULL);
    if (caps) {
      mlpp_set_src_pad_caps (GST_OBJECT (self), self->src, caps);
      gst_caps_unref(caps);
    }
  }

  if (gst_buffer_n_memory (out_buf) == 0) {
    GST_ERROR_OBJECT (self, "Output buffer has no memory (allocation failed for all layout entries?)");
    gst_buffer_unref (out_buf);
    gst_memory_unmap(tensor_mem, &map_info[0]);
    gst_buffer_unref(tensor_buf);
    return GST_FLOW_ERROR;
  }

  gst_memory_unmap(tensor_mem, &map_info[0]);
  gst_buffer_unref(tensor_buf);

  ret = gst_pad_push(self->src, out_buf);

  return ret;
}
