/*
 * gstambavideoscale.c
 *
 * History:
 *    4/28/2025 - [Scott(Shou-Wen) Yu] created file
 *
 * Copyright (C) 2025 Ambarella International LP
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
 * SECTION: element-amba_videoscale
 * @title: amba_videoscale
 *
 * amba_videoscale can be used to do NV12 frame scaling with Amba image scaler HW.
 *
 * ## Example pipeline
 * [[
 * gst-launch-1.0 -v amba_camsrc buf-id=0 ! queue ! videoconvert ! amba_videoscale ! video/x-raw,width=640,height=360 ! filesink location=/tmp/640x360_nv12.yuv
 * ]]
 *  Capture canvas 0's frames and saves to file. Video scaling is performed by amba_videoscale when resize is necessary.
 */

#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <gst/video/video.h>
#include "debug_log.h"
#include "iav_ctx.h"
#include "gstambavideoscale.h"
#include "gst_amba_pitch_align.h"
#include "gst_amba_cavalry_allocator.h"
#include "amba_private_data.h"

#ifndef UNUSED
#define UNUSED(x) ((void)(x))
#endif

#define PROPOASE_BUF_POOL_ALIGNMENT  (0)

#define AMBAVIDEOSCALE_PIXEL_FORMAT       "NV12"

#ifdef BUILD_DSP_AMBA_V6
#define IMG_SCALE_H_ALIGN                 (IAV_IMG_SCALE_H_ALIGN)
#define IMG_SCALE_V_ALIGN                 (IAV_IMG_SCALE_V_ALIGN)
#else
#define IMG_SCALE_H_ALIGN                 (4)
#define IMG_SCALE_V_ALIGN                 (4)
#endif

/* NV12 row bytes: multiple of both IAV_DSP_BUF_PITCH_ALIGN and CAVALRY_PORT_PITCH_ALIGN */
#define IMG_SCALE_PITCH_ALIGN             ((guint) gst_amba_pitch_lcm_dsp_and_cavalry_step ())

GST_DEBUG_CATEGORY_STATIC (gst_amba_videoscale_debug);
#define GST_CAT_DEFAULT gst_amba_videoscale_debug

/* Filter signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  PROP_0,
  PROP_ZERO_COPY,
};

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (AMBAVIDEOSCALE_PIXEL_FORMAT)));

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (AMBAVIDEOSCALE_PIXEL_FORMAT)));

#define gst_amba_videoscale_parent_class parent_class
G_DEFINE_TYPE (GstAmbaVideoScale, gst_amba_videoscale, GST_TYPE_VIDEO_FILTER);


static void gst_amba_videoscale_finalize (GObject *gobject);
static GstStateChangeReturn gst_amba_videoscale_change_state (GstElement *element,
  GstStateChange transition);
static GstCaps *gst_amba_videoscale_transform_caps (GstBaseTransform * trans,
  GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static gboolean gst_amba_videoscale_src_event (GstBaseTransform * trans, GstEvent * event);
static gboolean gst_amba_videoscale_filter_meta (GstBaseTransform * trans, GstQuery * query,
  GType api, const GstStructure * params);
static gboolean gst_amba_videoscale_transform_meta (GstBaseTransform * trans, GstBuffer * outbuf,
  GstMeta * meta, GstBuffer * inbuf);
static GstFlowReturn gst_amba_videoscale_transform_frame (GstVideoFilter * filter,
  GstVideoFrame * in_frame, GstVideoFrame * out_frame);
static GstFlowReturn gst_amba_videoscale_prepare_output_buffer (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer ** outbuf);
static gboolean gst_amba_videoscale_propose_allocation (GstBaseTransform * trans,
  GstQuery * decide_query, GstQuery * query);
static gboolean gst_amba_videoscale_decide_allocation (GstBaseTransform * trans, GstQuery * query);
static gboolean gst_amba_videoscale_query (GstBaseTransform * trans, GstPadDirection direction,
  GstQuery * query);
static gboolean gst_amba_videoscale_set_info (GstVideoFilter *filter, GstCaps *incaps,
  GstVideoInfo *in_info, GstCaps *outcaps, GstVideoInfo *out_info);
static gboolean gst_amba_videoscale_copy_buffer (GstMapInfo *map, GstVideoFrame *frame, u32 direction);
static void gst_amba_videoscale_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_amba_videoscale_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GQuark _size_quark;

/* initialize the amba_videoscale class */
static void gst_amba_videoscale_class_init (GstAmbaVideoScaleClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstBaseTransformClass *gsttrans_class = (GstBaseTransformClass *) klass;
  GstVideoFilterClass *gstfilter_class = (GstVideoFilterClass *) klass;

  GST_DEBUG_CATEGORY_INIT (gst_amba_videoscale_debug, "amba_videoscale", 0,
      "Amba video scaler");

  GST_DEBUG ("gst_amba_videoscale_class_init");

  _size_quark = g_quark_from_static_string (GST_META_TAG_VIDEO_SIZE_STR);
  gobject_class->finalize = gst_amba_videoscale_finalize;
  gobject_class->set_property = gst_amba_videoscale_set_property;
  gobject_class->get_property = gst_amba_videoscale_get_property;

  g_object_class_install_property (gobject_class, PROP_ZERO_COPY,
      g_param_spec_boolean ("zero-copy", "Zero Copy",
          "Use zero-copy mode when possible (TRUE) or always allocate internal buffers (FALSE)",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_amba_videoscale_change_state);
  gst_element_class_set_static_metadata (gstelement_class,
    "Amba video scaler",
    "Filter/Video/Scaler",
    "Resizes NV12 images with Amba image scaler hardware",
    "Scott(Shou-Wen) Yu <swyu@ambarella.com>");

  gst_element_class_add_static_pad_template (gstelement_class, &sink_factory);
  gst_element_class_add_static_pad_template (gstelement_class, &src_factory);

  gsttrans_class->transform_caps = GST_DEBUG_FUNCPTR (gst_amba_videoscale_transform_caps);
  gsttrans_class->src_event = GST_DEBUG_FUNCPTR (gst_amba_videoscale_src_event);
  gsttrans_class->filter_meta = GST_DEBUG_FUNCPTR (gst_amba_videoscale_filter_meta);
  gsttrans_class->transform_meta = GST_DEBUG_FUNCPTR (gst_amba_videoscale_transform_meta);
  gsttrans_class->propose_allocation = GST_DEBUG_FUNCPTR (gst_amba_videoscale_propose_allocation);
  gsttrans_class->decide_allocation = GST_DEBUG_FUNCPTR (gst_amba_videoscale_decide_allocation);
  gsttrans_class->query = GST_DEBUG_FUNCPTR (gst_amba_videoscale_query);
  gsttrans_class->prepare_output_buffer =
      GST_DEBUG_FUNCPTR (gst_amba_videoscale_prepare_output_buffer);

  gstfilter_class->transform_frame = GST_DEBUG_FUNCPTR (gst_amba_videoscale_transform_frame);
  gstfilter_class->set_info = GST_DEBUG_FUNCPTR (gst_amba_videoscale_set_info);

  return;
}

static void gst_amba_videoscale_init (GstAmbaVideoScale *self)
{
  GST_DEBUG ("gst_amba_videoscale_init");

  gst_amba_cavalry_allocator_init_once();

  self->iav_ctx = acquire_iav_ctx (1);
  if (!self->iav_ctx) {
    GST_ERROR("acquire_iav_ctx failed\n");
    return;
  }
  self->cavalry_allocator = gst_amba_cavalry_allocator_get();
  self->input_mem = NULL;
  self->output_mem = NULL;
  self->incaps = NULL;
  self->outcaps = NULL;
  self->decide_query = NULL;
  memset(&self->input_map, 0, sizeof(self->input_map));
  memset(&self->output_map, 0, sizeof(self->output_map));
  memset(&self->in_info, 0, sizeof(self->in_info));
  memset(&self->out_info, 0, sizeof(self->out_info));

  /* Initialize properties */
  self->zero_copy = TRUE;
  self->logged_zero_copy_path = FALSE;

  /* disable QoS*/
  gst_base_transform_set_qos_enabled (GST_BASE_TRANSFORM (self), FALSE);

  return;
}

static void
gst_amba_videoscale_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAmbaVideoScale *self = GST_AMBA_VIDEOSCALE (object);

  switch (prop_id) {
    case PROP_ZERO_COPY:
      self->zero_copy = g_value_get_boolean (value);
      self->logged_zero_copy_path = FALSE;
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_amba_videoscale_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAmbaVideoScale *self = GST_AMBA_VIDEOSCALE (object);

  switch (prop_id) {
    case PROP_ZERO_COPY:
      g_value_set_boolean (value, self->zero_copy);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void gst_amba_videoscale_finalize (GObject *gobject)
{
  GstAmbaVideoScale * self = GST_AMBA_VIDEOSCALE (gobject);

  GST_DEBUG ("gst_amba_videoscale_finalize");

  if (self->iav_ctx) {
    release_iav_ctx (1);
    self->iav_ctx = NULL;
  }

  if (self->cavalry_allocator) {
    gst_object_unref(self->cavalry_allocator);
    self->cavalry_allocator = NULL;
  }

  if (self->incaps) {
    gst_caps_unref(self->incaps);
    self->incaps = NULL;
  }

  if (self->outcaps) {
    gst_caps_unref(self->outcaps);
    self->outcaps = NULL;
  }

  if (self->input_map.data) {
    gst_memory_unmap(self->input_mem, &self->input_map);
    self->input_map.data = NULL;
  }
  if (self->input_mem) {
    gst_memory_unref(self->input_mem);
    self->input_mem = NULL;
  }

  if (self->output_map.data) {
    gst_memory_unmap(self->output_mem, &self->output_map);
    self->output_map.data = NULL;
  }
  if (self->output_mem) {
    gst_memory_unref(self->output_mem);
    self->output_mem = NULL;
  }

  if (self->decide_query) {
    gst_query_unref(self->decide_query);
    self->decide_query = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (gobject);

  return;
}

static GstStateChangeReturn
gst_amba_videoscale_change_state (GstElement *element, GstStateChange transition)
{
  GstAmbaVideoScale * self = GST_AMBA_VIDEOSCALE (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      GST_DEBUG_OBJECT(self, "GST_STATE_CHANGE_NULL_TO_READY\n");
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_DEBUG_OBJECT(self, "GST_STATE_CHANGE_READY_TO_PAUSED\n");
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      GST_DEBUG_OBJECT(self, "GST_STATE_CHANGE_PAUSED_TO_PLAYING\n");
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      GST_DEBUG_OBJECT(self, "GST_STATE_CHANGE_PLAYING_TO_PAUSED\n");
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_DEBUG_OBJECT(self, "GST_STATE_CHANGE_PAUSED_TO_READY\n");
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      GST_DEBUG_OBJECT(self, "GST_STATE_CHANGE_READY_TO_NULL\n");
      break;
    default:
      break;
  }
  return ret;
}

static GstCaps *
gst_amba_videoscale_caps_rangify_size_info (GstAmbaVideoScale * self, GstPadDirection direction,
  GstCaps * caps)
{
  GstCaps *ret;
  GstStructure *structure;
  GstCapsFeatures *features;
  iav_al_t * iav_al = &self->iav_ctx->iav_al;
  amba_resource_info_t resource = {0};
  gint i, n, width = 0, height = 0;
  gint max_input_width = 0, max_input_height = 0, max_output_width = 0, max_output_height = 0;

  ret = gst_caps_new_empty ();

  if (!iav_al->f_get_resource_info ||
    iav_al->f_get_resource_info(self->iav_ctx->iav_fd, &resource) < 0) {
    DPRINT_ERROR("f_get_resource_info failed\n");
    return ret;
  }

  if (!resource.img_scale_enable) {
    GST_DEBUG_OBJECT(self, "img scale not enabled, go passthrough\n");
    return ret;
  }

  max_input_width = resource.img_scale_max_input_width;
  max_input_height = resource.img_scale_max_input_height;
  max_output_width = resource.img_scale_max_output_width;
  max_output_height = resource.img_scale_max_output_height;

  n = gst_caps_get_size (caps);
  for (i = 0; i < n; i++) {
    structure = gst_caps_get_structure (caps, i);
    features = gst_caps_get_features (caps, i);

    if (!gst_structure_has_field_typed (structure, "width", G_TYPE_INT)) {
      structure = gst_structure_copy (structure);
      gst_caps_append_structure_full (ret, structure, gst_caps_features_copy (features));
      continue;
    } else {
      gst_structure_get (structure, "width", G_TYPE_INT, &width, "height", G_TYPE_INT, &height, NULL);
    }

    /* If this is already expressed by the existing caps
     * skip this structure */
    if (i > 0 && gst_caps_is_subset_structure_full (ret, structure, features))
      continue;

    structure = gst_structure_copy (structure);
    if (direction == GST_PAD_SINK) {
      if (width > max_input_width || height > max_input_height) {
        GST_ERROR_OBJECT(self, "input size %dx%d is too large, max is %dx%d\n",
          width, height, max_input_width, max_input_height);
        gst_structure_set (structure, "width", G_TYPE_INT, 0,
         "height", G_TYPE_INT, 0, NULL);
      } else {
        gst_structure_set (structure, "width", GST_TYPE_INT_RANGE, 1,
          max_output_width, "height", GST_TYPE_INT_RANGE, 1,
          max_output_height, NULL);
      }
    } else {
      if (width > max_output_width || height > max_output_height) {
        GST_ERROR_OBJECT(self, "output size %dx%d is too large, max is %dx%d\n",
          width, height, max_output_width, max_output_height);
        gst_structure_set (structure, "width", G_TYPE_INT, 0,
         "height", G_TYPE_INT, 0, NULL);
      } else {
        gst_structure_set (structure, "width", GST_TYPE_INT_RANGE, 1,
          max_input_width, "height", GST_TYPE_INT_RANGE, 1,
          max_input_height, NULL);
      }
    }

    gst_caps_append_structure_full (ret, structure, gst_caps_features_copy (features));
  }

  return ret;
}

static GstCaps *
gst_amba_videoscale_transform_caps (GstBaseTransform * trans,
  GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstAmbaVideoScale *self = GST_AMBA_VIDEOSCALE (trans);
  GstCaps *ret;

  GST_DEBUG_OBJECT (trans,
    "Transforming caps %" GST_PTR_FORMAT " in direction %s", caps,
    (direction == GST_PAD_SINK) ? "sink" : "src");

  ret = gst_amba_videoscale_caps_rangify_size_info (self, direction, caps);
  GST_DEBUG_OBJECT (trans,
    "Transforming caps ret %" GST_PTR_FORMAT " in direction %s", ret,
    (direction == GST_PAD_SINK) ? "sink" : "src");

  if (filter) {
    GstCaps *intersection;

    intersection =
      gst_caps_intersect_full (filter, ret, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (ret);
    ret = intersection;
  }

  GST_DEBUG_OBJECT (trans,
    "Transforming caps result %" GST_PTR_FORMAT " in direction %s", ret,
    (direction == GST_PAD_SINK) ? "sink" : "src");

  return ret;
}

static gboolean
gst_amba_videoscale_src_event (GstBaseTransform * trans, GstEvent * event)
{
  GstAmbaVideoScale *self = GST_AMBA_VIDEOSCALE_CAST (trans);
  GstVideoFilter *filter = GST_VIDEO_FILTER_CAST (trans);
  GstStructure *structure;
  gboolean ret;
  gdouble x, y;

  GST_DEBUG_OBJECT (self, "handling %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NAVIGATION:
      if (filter->in_info.width != filter->out_info.width ||
          filter->in_info.height != filter->out_info.height) {
        event = gst_event_make_writable (event);

        structure = (GstStructure *) gst_event_get_structure (event);
        if (gst_structure_get_double (structure, "pointer_x", &x)) {
          gst_structure_set (structure, "pointer_x", G_TYPE_DOUBLE,
            x * filter->in_info.width / filter->out_info.width, NULL);
        }
        if (gst_structure_get_double (structure, "pointer_y", &y)) {
          gst_structure_set (structure, "pointer_y", G_TYPE_DOUBLE,
            y * filter->in_info.height / filter->out_info.height, NULL);
        }
      }
      break;
    default:
      break;
  }

  ret = GST_BASE_TRANSFORM_CLASS (parent_class)->src_event (trans, event);

  return ret;
}

static gboolean
gst_amba_videoscale_filter_meta (GstBaseTransform * trans, GstQuery * query,
    GType api, const GstStructure * params)
{
  UNUSED(params);
  UNUSED(query);
  UNUSED(trans);
  /* This element cannot passthrough the crop meta, because it would convert the
   * wrong sub-region of the image, and worst, our output image may not be large
   * enough for the crop to be applied later */
  if (api == GST_VIDEO_CROP_META_API_TYPE)
    return FALSE;

  /* propose all other metadata upstream */
  return TRUE;
}

static gboolean
gst_amba_videoscale_transform_meta (GstBaseTransform * trans,
    GstBuffer * outbuf, GstMeta * meta, GstBuffer * inbuf)
{
  GstVideoFilter *videofilter = GST_VIDEO_FILTER (trans);
  const GstMetaInfo *info = meta->info;
  const gchar *const *tags;
  const gchar *const *curr = NULL;
  gboolean should_copy = TRUE;
  const gchar *const valid_tags[] = {
    GST_META_TAG_VIDEO_SIZE_STR,
    NULL
  };

  tags = gst_meta_api_type_get_tags (info->api);
  /* No specific tags, we are good to copy */
  if (!tags) {
    return TRUE;
  }

  /* We are only changing size, we can preserve other metas tagged as
     orientation and colorspace */
  for (curr = tags; *curr; ++curr) {
    /* We dont handle any other tag */
    if (!g_strv_contains (valid_tags, *curr)) {
      should_copy = FALSE;
      break;
    }
  }
  /* Cant handle the tags in this meta, let the parent class handle it */
  if (!should_copy) {
    return GST_BASE_TRANSFORM_CLASS (parent_class)->transform_meta (trans,
        outbuf, meta, inbuf);
  }

  /* This meta is size sensitive, try to transform it accordingly */
  if (gst_meta_api_type_has_tag (info->api, _size_quark)) {
    GstVideoMetaTransform trans =
        { &videofilter->in_info, &videofilter->out_info };
    if (info->transform_func)
      info->transform_func (outbuf, meta, inbuf, _size_quark, &trans);
    return FALSE;
  }

  /* No need to transform, we can safely copy this meta */
  return TRUE;
}

static gboolean
gst_amba_videoscale_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query)
{
  GstAmbaVideoScale *self = GST_AMBA_VIDEOSCALE_CAST (trans);
  GstVideoFilter *filter = GST_VIDEO_FILTER (trans);
  GstVideoInfo *in_info = &self->in_info;
  GstCaps *incaps = self->incaps;
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  GstAllocationParams params;
  gboolean ret = FALSE;
  guint size, min, max;

  if (decide_query && gst_query_get_n_allocation_params (decide_query) > 0) {
    gst_query_parse_nth_allocation_param (decide_query, 0, &allocator, &params);
  } else {
    /* If no allocator proposed by downstream, use cavalry allocator */
    allocator = gst_amba_cavalry_allocator_get ();
    if (!allocator) {
      GST_ERROR_OBJECT (self, "Failed to get cavalry allocator!\n");
      ret = FALSE;
      goto no_allocator;
    }
    gst_allocation_params_init (&params);
  }
  if ((gst_query_get_n_allocation_params (query) <= 0) && (allocator)) {
    gst_query_add_allocation_param (query, allocator, &params);
  } else {
    GST_DEBUG_OBJECT (self, "query already has allocator, don't propose cavalry allocator!\n");
  }

  if (decide_query == NULL) {
    GST_DEBUG_OBJECT (self, "decide_query is NULL.\n");
    goto done;
  }
  if (!filter->negotiated) {
    GST_ERROR_OBJECT(self, "query allocation when not negotiated.\n");
    goto done;
  }

  GST_DEBUG_OBJECT(self, "in_info:%dx%d, out_info:%dx%d\n",
    in_info->width, in_info->height,
    self->out_info.width, self->out_info.height);

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    if (pool) {
      GST_DEBUG_OBJECT (self, "query already has pool, don't propose pool!\n");
    }
  }

  if (!pool) {
    pool = gst_buffer_pool_new ();
    #if PROPOASE_BUF_POOL_ALIGNMENT
    {
      gsize raw = (gsize) GST_ROUND_UP_N (GST_VIDEO_INFO_WIDTH (in_info), IMG_SCALE_PITCH_ALIGN) *
          (gsize) GST_ROUND_UP_N (GST_VIDEO_INFO_HEIGHT (in_info), IMG_SCALE_V_ALIGN) * 3 / 2;
      size = (guint) AMBA_ROUND_GSIZE_ODD_DSP_PITCH_UNITS (raw);
    }
    #else
    size = (guint) AMBA_ROUND_GSIZE_ODD_DSP_PITCH_UNITS ((gsize) in_info->size);
    #endif
    min = 2;
    max = 0;

    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_set_params (config, incaps, size, min, max);
    gst_buffer_pool_config_set_allocator (config, allocator, &params);
    gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

    #if PROPOASE_BUF_POOL_ALIGNMENT
    /* Set video alignment in config */
    GstVideoAlignment align;
    gst_video_alignment_reset (&align);
    align.padding_right = GST_ROUND_UP_N (GST_VIDEO_INFO_WIDTH (in_info),
                                         IMG_SCALE_PITCH_ALIGN) - GST_VIDEO_INFO_WIDTH (in_info);
    align.stride_align[0] = IMG_SCALE_PITCH_ALIGN - 1;  /* Y plane */
    align.stride_align[1] = IMG_SCALE_PITCH_ALIGN - 1;  /* UV plane */
    gst_buffer_pool_config_set_video_alignment (config, &align);
    gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
    #endif

    /* Apply the configuration */
    if (!gst_buffer_pool_set_config (pool, config)) {
      GST_WARNING_OBJECT (self, "Failed to set buffer pool config");
      ret = FALSE;
      goto config_failed;
    }
    gst_query_add_allocation_pool (query, pool, size, 0, 0);
 }

  ret = GST_BASE_TRANSFORM_CLASS (parent_class)->propose_allocation (trans, decide_query, query);
  if (!ret) {
    GST_DEBUG_OBJECT(self, "propose_allocation failed.\n");
  }
  goto done;

no_allocator:
  {
    GST_ERROR_OBJECT (self, "No allocator available");
    return ret;
  }
config_failed:
  {
    GST_ERROR_OBJECT (self, "Failed to set config on pool");
    if (allocator)
      gst_object_unref (allocator);
    if (pool)
      gst_object_unref (pool);
    return ret;
  }
done:
  if (allocator)
      gst_object_unref (allocator);
  if (pool)
    gst_object_unref (pool);

  return ret;
}

static gboolean
gst_amba_videoscale_decide_allocation (GstBaseTransform * trans, GstQuery * query)
{
  GstAmbaVideoScale *self = GST_AMBA_VIDEOSCALE_CAST (trans);

  gboolean ret = FALSE;
  GstQuery *old_query = self->decide_query;

  /* If no allocator proposed by downstream, use cavalry allocator */
  if (gst_query_get_n_allocation_params (query) <= 0) {
    GstAllocator *allocator = gst_amba_cavalry_allocator_get ();
    GstAllocationParams params = {0};
    if (allocator) {
      gst_allocation_params_init (&params);
      gst_query_add_allocation_param (query, allocator, &params);
      gst_object_unref (allocator);
    }
  }

  ret = GST_BASE_TRANSFORM_CLASS (parent_class)->decide_allocation (trans,
      query);
  self->decide_query = gst_query_copy(query);
  if (old_query) {
    gst_query_unref(old_query);
  }
  return ret;
}

/*
 * Allocate NV12 output here with GstVideoMeta strides aligned to IMG_SCALE_PITCH_ALIGN
 * (parent pool from decide_allocation may still use width as stride).
 */
static GstFlowReturn
gst_amba_videoscale_prepare_output_buffer (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer ** outbuf)
{
  GstAmbaVideoScale *self = GST_AMBA_VIDEOSCALE_CAST (trans);
  GstVideoFilter *vf = GST_VIDEO_FILTER (trans);
  GstVideoInfo oi;
  gint w, h, pitch;
  gsize raw, size;

  g_return_val_if_fail (outbuf != NULL, GST_FLOW_ERROR);

  *outbuf = NULL;

  if (gst_base_transform_is_passthrough (trans) || !vf->negotiated)
    return GST_BASE_TRANSFORM_CLASS (parent_class)->prepare_output_buffer (trans,
        inbuf, outbuf);

  if (!self->cavalry_allocator)
    return GST_BASE_TRANSFORM_CLASS (parent_class)->prepare_output_buffer (trans,
        inbuf, outbuf);

  GST_OBJECT_LOCK (self);
  if (GST_VIDEO_INFO_FORMAT (&self->out_info) != GST_VIDEO_FORMAT_NV12) {
    GST_OBJECT_UNLOCK (self);
    return GST_BASE_TRANSFORM_CLASS (parent_class)->prepare_output_buffer (trans,
        inbuf, outbuf);
  }
  oi = self->out_info;
  GST_OBJECT_UNLOCK (self);

  w = GST_VIDEO_INFO_WIDTH (&oi);
  h = GST_VIDEO_INFO_HEIGHT (&oi);
  if (w <= 0 || h <= 0)
    return GST_BASE_TRANSFORM_CLASS (parent_class)->prepare_output_buffer (trans,
        inbuf, outbuf);

  pitch = (gint) GST_ROUND_UP_N ((guint) w, IMG_SCALE_PITCH_ALIGN);
  raw = (gsize) pitch * (gsize) GST_ROUND_UP_N ((guint) h, IMG_SCALE_V_ALIGN) * 3 / 2;
  size = AMBA_ROUND_GSIZE_ODD_DSP_PITCH_UNITS (raw);

  *outbuf = gst_buffer_new_allocate (self->cavalry_allocator, size, NULL);
  if (!*outbuf) {
    GST_ERROR_OBJECT (self,
        "prepare_output_buffer: failed to allocate size=%" G_GSIZE_FORMAT, size);
    return GST_FLOW_ERROR;
  }

  {
    gsize offset[4] = { 0, };
    gint stride[4] = { 0, };

    offset[0] = 0;
    offset[1] = (gsize) pitch * h;
    stride[0] = pitch;
    stride[1] = pitch;
    gst_buffer_add_video_meta_full (*outbuf, GST_VIDEO_FRAME_FLAG_NONE,
        GST_VIDEO_FORMAT_NV12, w, h, 2, offset, stride);
  }

  if (inbuf) {
    if (GST_BUFFER_PTS_IS_VALID (inbuf))
      GST_BUFFER_PTS (*outbuf) = GST_BUFFER_PTS (inbuf);
    if (GST_BUFFER_DTS_IS_VALID (inbuf))
      GST_BUFFER_DTS (*outbuf) = GST_BUFFER_DTS (inbuf);
    if (GST_BUFFER_DURATION_IS_VALID (inbuf))
      GST_BUFFER_DURATION (*outbuf) = GST_BUFFER_DURATION (inbuf);
    if (GST_BUFFER_OFFSET_IS_VALID (inbuf))
      GST_BUFFER_OFFSET (*outbuf) = GST_BUFFER_OFFSET (inbuf);
    if (GST_BUFFER_OFFSET_END_IS_VALID (inbuf))
      GST_BUFFER_OFFSET_END (*outbuf) = GST_BUFFER_OFFSET_END (inbuf);
    GST_BUFFER_FLAGS (*outbuf) = GST_BUFFER_FLAGS (inbuf);
  }

  return GST_FLOW_OK;
}

static gboolean
gst_amba_videoscale_alloc_internal_buf (GstAmbaVideoScale * self, gboolean is_input)
{
  GstVideoInfo *info = is_input ? &self->in_info : &self->out_info;
  GstMemory **mem = is_input ? &self->input_mem : &self->output_mem;
  GstMapInfo *map = is_input ? &self->input_map : &self->output_map;
  u32 size = 0;

  if (map->data) {
    gst_memory_unmap(*mem, map);
    map->data = NULL;
  }
  if (*mem) {
    gst_memory_unref(*mem);
    *mem = NULL;
  }

  {
    gsize raw = (gsize) GST_ROUND_UP_N (GST_VIDEO_INFO_WIDTH (info), IMG_SCALE_PITCH_ALIGN) *
        (gsize) GST_ROUND_UP_N (GST_VIDEO_INFO_HEIGHT (info), IMG_SCALE_V_ALIGN) * 3 / 2;
    size = (guint) AMBA_ROUND_GSIZE_ODD_DSP_PITCH_UNITS (raw);
  }

  *mem = gst_allocator_alloc(self->cavalry_allocator, size, NULL);
  if (!*mem) {
    GST_ERROR_OBJECT(self, "Failed to allocate buffer");
    return FALSE;
  }

  g_printerr (
      "[amba_videoscale]: using internal %s buffer (copy path), strides=%d/%d, size=%u mem_fd=%d "
      "(need Cavalry FD + pitch-aligned stride for zero-copy)\n",
      is_input ? "input" : "output", info->stride[0], info->stride[1], (unsigned) size,
      gst_amba_cavalry_memory_get_fd (*mem));

  if (!gst_memory_map(*mem, map, GST_MAP_WRITE)) {
    GST_ERROR_OBJECT(self, "Failed to map memory");
    gst_memory_unref(*mem);
    *mem = NULL;
    map->data = NULL;
    return FALSE;
  }

  return TRUE;
}

/*
 * @direction: 0 for input: copy from frame to map
 * @direction: 1 for output: copy from map to frame
 */
static gboolean
gst_amba_videoscale_copy_buffer (GstMapInfo *map, GstVideoFrame *frame, u32 direction)
{
  u32 buf_offset = 0, frame_offset = 0;
  u32 stride = GST_ROUND_UP_N(frame->info.width, IMG_SCALE_PITCH_ALIGN);

  if (direction == 0) {
    for (int i = 0; i < frame->info.height; i++) {
      memcpy(map->data + buf_offset, frame->data[0] + frame_offset, frame->info.width);
      buf_offset += stride;
      frame_offset += frame->info.stride[0];
    }
    frame_offset = 0;
    for (int i = 0; i < frame->info.height / 2; i++) {
      memcpy(map->data + buf_offset, frame->data[1] + frame_offset, frame->info.width);
      buf_offset += stride;
      frame_offset += frame->info.stride[1];
    }
  } else {
    for (int i = 0; i < frame->info.height; i++) {
      memcpy(frame->data[0] + frame_offset, map->data + buf_offset, frame->info.width);
      buf_offset += stride;
      frame_offset += frame->info.stride[0];
    }
    frame_offset = 0;
    for (int i = 0; i < frame->info.height / 2; i++) {
      memcpy(frame->data[1] + frame_offset, map->data + buf_offset, frame->info.width);
      buf_offset += stride;
      frame_offset += frame->info.stride[1];
    }
  }
  return TRUE;
}

static GstFlowReturn
gst_amba_videoscale_transform_frame (GstVideoFilter * filter,
    GstVideoFrame * in_frame, GstVideoFrame * out_frame)
{
  GstAmbaVideoScale *self = GST_AMBA_VIDEOSCALE_CAST (filter);
  GstFlowReturn ret = GST_FLOW_OK;
  iav_al_t * iav_al = &self->iav_ctx->iav_al;
  img_scale_cfg_t img_scale_cfg = {0};
  GstMemory *mem = NULL;
  gboolean input_copy = FALSE, output_copy = FALSE;

  if (!filter->negotiated) {
    GST_ERROR_OBJECT(self, "Not negotiated");
    return GST_FLOW_NOT_NEGOTIATED;
  }

  /* input copy when allocator isn't cavalry allocator,
   * or input frame stride isn't aligned to IMG_SCALE_PITCH_ALIGN.
   */
  mem = gst_buffer_peek_memory(in_frame->buffer, 0);
  if (!self->zero_copy ||
      !gst_is_amba_cavalry_memory(mem) ||
      in_frame->info.stride[0] % IMG_SCALE_PITCH_ALIGN != 0 ||
      in_frame->info.stride[1] % IMG_SCALE_PITCH_ALIGN != 0) {
    input_copy = TRUE;
  }

  /* output copy when allocator isn't cavalry allocator,
   * or output frame stride isn't aligned to IMG_SCALE_PITCH_ALIGN.
   */
  mem = gst_buffer_peek_memory(out_frame->buffer, 0);
  if (!self->zero_copy ||
      !gst_is_amba_cavalry_memory(mem) ||
      out_frame->info.stride[0] % IMG_SCALE_PITCH_ALIGN != 0 ||
      out_frame->info.stride[1] % IMG_SCALE_PITCH_ALIGN != 0) {
    output_copy = TRUE;
  }

  if (!self->logged_zero_copy_path) {
    GstMemory *inm = gst_buffer_peek_memory (in_frame->buffer, 0);
    GstMemory *outm = gst_buffer_peek_memory (out_frame->buffer, 0);
    gint in_mfd = -1, out_mfd = -1;

    if (gst_is_amba_cavalry_memory (inm))
      in_mfd = gst_amba_cavalry_memory_get_fd (inm);
    else if (gst_is_fd_memory (inm))
      in_mfd = gst_fd_memory_get_fd (inm);
    if (gst_is_amba_cavalry_memory (outm))
      out_mfd = gst_amba_cavalry_memory_get_fd (outm);
    else if (gst_is_fd_memory (outm))
      out_mfd = gst_fd_memory_get_fd (outm);
    if (!self->zero_copy) {
      g_printerr (
          "[amba_videoscale]: path zero-copy disabled by property (in_mem_fd=%d out_mem_fd=%d)\n",
          in_mfd, out_mfd);
    } else if (input_copy || output_copy) {
      g_printerr ("[amba_videoscale]: copy in=%d out=%d (zc_prop=%d in_cav=%d out_cav=%d "
          "in_stride=%d/%d out_stride=%d/%d in_mem_fd=%d out_mem_fd=%d)\n",
          input_copy, output_copy, self->zero_copy,
          gst_is_amba_cavalry_memory (inm), gst_is_amba_cavalry_memory (outm),
          in_frame->info.stride[0], in_frame->info.stride[1],
          out_frame->info.stride[0], out_frame->info.stride[1],
          in_mfd, out_mfd);
    } else {
      g_printerr (
          "[amba_videoscale]: zero-copy (input+output Cavalry, in_stride=%d/%d out_stride=%d/%d pitch-aligned, "
          "in_mem_fd=%d out_mem_fd=%d)\n",
          in_frame->info.stride[0], in_frame->info.stride[1],
          out_frame->info.stride[0], out_frame->info.stride[1],
          in_mfd, out_mfd);
    }
    self->logged_zero_copy_path = TRUE;
  }

  // allocate buffer
  if (input_copy && self->input_mem == NULL) {
    gst_amba_videoscale_alloc_internal_buf(self, TRUE);
  }
  if (output_copy && self->output_mem == NULL) {
    gst_amba_videoscale_alloc_internal_buf(self, FALSE);
  }

  // copy input buffer
  if (input_copy) {
    gst_amba_videoscale_copy_buffer(&self->input_map, in_frame, 0);
  }

  // scale
  img_scale_cfg.input.x = 0;
  img_scale_cfg.input.y = 0;
  img_scale_cfg.input.w = in_frame->info.width;
  img_scale_cfg.input.h = in_frame->info.height;
  img_scale_cfg.output.x = 0;
  img_scale_cfg.output.y = 0;
  img_scale_cfg.output.w = out_frame->info.width;
  img_scale_cfg.output.h = out_frame->info.height;

  if (input_copy) {
    img_scale_cfg.input_buf_pid = gst_fd_memory_get_fd(self->input_mem);
    /* Internal buffer from gst_amba_videoscale_copy_buffer: pitch = ROUND_UP(width). */
    img_scale_cfg.input_buf_picth = GST_ROUND_UP_N(in_frame->info.width, IMG_SCALE_PITCH_ALIGN);
  } else {
    img_scale_cfg.input_buf_pid = gst_fd_memory_get_fd(gst_buffer_peek_memory(in_frame->buffer, 0));
    /* Zero-copy DMA: use GstVideoMeta stride (IAV/GDMA pitch can exceed ROUND_UP(width), e.g. 896 vs 768). */
    img_scale_cfg.input_buf_picth = (unsigned int) in_frame->info.stride[0];
  }
  img_scale_cfg.input_buf_height = in_frame->info.height;

  if (output_copy) {
    img_scale_cfg.output_buf_pid = gst_fd_memory_get_fd(self->output_mem);
    img_scale_cfg.output_buf_picth = GST_ROUND_UP_N(out_frame->info.width, IMG_SCALE_PITCH_ALIGN);
  } else {
    img_scale_cfg.output_buf_pid = gst_fd_memory_get_fd(gst_buffer_peek_memory(out_frame->buffer, 0));
    img_scale_cfg.output_buf_picth = (unsigned int) out_frame->info.stride[0];
  }
  img_scale_cfg.output_buf_height = out_frame->info.height;

  if (!iav_al->f_set_img_scale ||
    iav_al->f_set_img_scale(self->iav_ctx->iav_fd, &img_scale_cfg) < 0) {
    DPRINT_ERROR("f_set_img_scale failed\n");
    return GST_FLOW_ERROR;
  }

  // copy output buffer
  if (output_copy) {
    gst_amba_videoscale_copy_buffer(&self->output_map, out_frame, 1);
  }

  if (!amba_buffer_get_private_data_meta(out_frame->buffer) &&
      amba_buffer_get_private_data_meta(in_frame->buffer)) {
    amba_buffer_copy_private_data_meta(out_frame->buffer, in_frame->buffer);
  }

  return ret;
}

static gboolean
gst_amba_videoscale_query (GstBaseTransform * trans,
    GstPadDirection direction, GstQuery * query)
{
  GstAmbaVideoScale *self = GST_AMBA_VIDEOSCALE_CAST (trans);
  gboolean ret = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_ALLOCATION:
    {
      /* can only be done on the sinkpad */
      if (direction != GST_PAD_SINK)
        goto done;

      GST_OBJECT_LOCK (self);
      gst_amba_videoscale_propose_allocation(trans, self->decide_query, query);
      GST_OBJECT_UNLOCK (self);

      ret = GST_BASE_TRANSFORM_CLASS (parent_class)->query (trans, direction, query);
      break;
    }
    default:
      ret = GST_BASE_TRANSFORM_CLASS (parent_class)->query (trans, direction, query);
      break;
  }

done:
  return ret;
}

static gboolean
gst_amba_videoscale_set_info (GstVideoFilter *filter,
    GstCaps *incaps, GstVideoInfo *in_info,
    GstCaps *outcaps, GstVideoInfo *out_info)
{
  GstAmbaVideoScale *self = GST_AMBA_VIDEOSCALE_CAST (filter);
  iav_al_t * iav_al = &self->iav_ctx->iav_al;
  amba_resource_info_t resource = {0};

  GST_DEBUG_OBJECT(self, "get new info. in_info:%dx%d [size:%" G_GSIZE_FORMAT "], "
      "out_info:%dx%d [size:%" G_GSIZE_FORMAT "]. incaps:%" GST_PTR_FORMAT ", outcaps:%" GST_PTR_FORMAT,
      in_info->width, in_info->height, in_info->size,
      out_info->width, out_info->height, out_info->size,
      incaps, outcaps);

  GST_OBJECT_LOCK (self);

  if (self->incaps) {
    gst_caps_unref(self->incaps);
    self->incaps = NULL;
  }
  if (self->outcaps) {
    gst_caps_unref(self->outcaps);
    self->outcaps = NULL;
  }

  self->incaps = gst_caps_copy(incaps);
  self->in_info = *in_info;
  self->outcaps = gst_caps_copy(outcaps);
  self->out_info = *out_info;

  self->logged_zero_copy_path = FALSE;
  if (self->input_map.data) {
    gst_memory_unmap(self->input_mem, &self->input_map);
    self->input_map.data = NULL;
  }
  if (self->input_mem) {
    gst_memory_unref(self->input_mem);
    self->input_mem = NULL;
  }
  if (self->output_map.data) {
    gst_memory_unmap(self->output_mem, &self->output_map);
    self->output_map.data = NULL;
  }
  if (self->output_mem) {
    gst_memory_unref(self->output_mem);
    self->output_mem = NULL;
  }

  GST_OBJECT_UNLOCK (self);

  if (!iav_al->f_get_resource_info ||
    iav_al->f_get_resource_info(self->iav_ctx->iav_fd, &resource) < 0) {
    DPRINT_ERROR("f_get_resource_info failed\n");
    return FALSE;
  }
  if (!resource.img_scale_enable) {
    GST_DEBUG_OBJECT(self, "img scale not enabled, go passthrough\n");
    gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (filter), TRUE);
  }

  return TRUE;
}
