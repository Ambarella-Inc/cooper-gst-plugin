/*
 * gstambafilemuxer.c
 *
 * History:
 *    08/01/2025 - [Yang Yu] created file
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

#include "gstambafilemuxer.h"
#include "gst_amba_cavalry_allocator.h"
#include "internal.h"

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/base/gstaggregator.h>
#include <string.h>

G_DEFINE_TYPE (GstAmbaFileMuxer, gst_amba_filemuxer, GST_TYPE_AGGREGATOR);

GST_DEBUG_CATEGORY_STATIC (amba_filemuxer_debug);
#define GST_CAT_DEFAULT amba_filemuxer_debug

/* Property IDs */
enum {
    PROP_0,
    PROP_PRIVATE_PAD_COUNT,
    PROP_PRIVATE_FORMATS,
    PROP_PACKED_MODE,
    PROP_DMA_OUTPUT_MODE,
    PROP_ALIGNMENT,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

/* Pad templates */
static GstStaticPadTemplate sink_video_template =
GST_STATIC_PAD_TEMPLATE ("sink_video",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw,"
                     "format=(string){NV12,NV16},"
                     "width=(int)[1,MAX],"
                     "height=(int)[1,MAX],"
                     "framerate=(fraction)[0/1,MAX]")
);

static GstStaticPadTemplate sink_private_template =
GST_STATIC_PAD_TEMPLATE ("sink_private_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("video/x-amba-private,"
                     "format=(string){ME0,ME1,CE},"
                     "width=(int)[1,MAX],"
                     "height=(int)[1,MAX]")
);

static GstStaticPadTemplate src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw,"
                     "format=(string){NV12,NV16},"
                     "width=(int)[1,MAX],"
                     "height=(int)[1,MAX],"
                     "framerate=(fraction)[0/1,MAX]")
);


/* Function declarations */
static GstAggregatorPad *gst_amba_filemuxer_create_new_pad (GstAggregator * agg, GstPadTemplate * templ,
    const gchar * name, const GstCaps * caps);
static GstFlowReturn gst_amba_filemuxer_aggregate (GstAggregator * agg, gboolean timeout);
static GstCaps *gst_amba_filemuxer_fixate_src_caps (GstAggregator * agg, GstCaps * caps);
static gboolean gst_amba_filemuxer_negotiate_caps (GstAmbaFileMuxer * self);
static GstCaps *gst_amba_filemuxer_create_private_caps (GstAmbaFileMuxer * self, guint pad_index);
static void gst_amba_filemuxer_set_private_pad_caps (GstAmbaFileMuxer * self);
static GstPadProbeReturn gst_amba_filemuxer_video_pad_probe (GstPad * pad, GstPadProbeInfo * info, gpointer user_data);
static gboolean gst_amba_filemuxer_start (GstAggregator * agg);

/* Helper function to copy video data with format-specific handling */
static void
copy_video_data_with_format (GstAmbaFileMuxer * self, guint8 * src_data, guint8 * dst_data)
{
    switch (self->video_format) {
        case GST_VIDEO_FORMAT_NV12: {
            memset(dst_data, 0, self->video_pitch * self->video_aligned_height * 3 / 2);
            // Copy Y plane (luma)
            for (gint y = 0; y < self->video_height; y++) {
                memcpy(dst_data + y * self->video_pitch,
                       src_data + y * self->video_width,
                       self->video_width);
            }
            // Copy UV plane (chroma) - for NV12 format
            guint uv_offset = self->video_pitch * self->video_aligned_height;
            guint src_uv_offset = self->video_width * self->video_height;
            for (gint y = 0; y < self->video_height / 2; y++) {
                memcpy(dst_data + uv_offset + y * self->video_pitch,
                       src_data + src_uv_offset + y * self->video_width,
                       self->video_width);
            }
            break;
        }
        case GST_VIDEO_FORMAT_NV16: {
            memset(dst_data, 0, self->video_pitch * self->video_aligned_height * 2);
            // Copy Y plane (luma)
            for (gint y = 0; y < self->video_height; y++) {
                memcpy(dst_data + y * self->video_pitch,
                       src_data + y * self->video_width,
                       self->video_width);
            }
            // Copy UV plane (chroma) - for NV16 format (full height)
            guint uv_offset = self->video_pitch * self->video_aligned_height;
            guint src_uv_offset = self->video_width * self->video_height;
            for (gint y = 0; y < self->video_height; y++) {
                memcpy(dst_data + uv_offset + y * self->video_pitch,
                       src_data + src_uv_offset + y * self->video_width,
                       self->video_width);
            }
            break;
        }
        default:
            GST_ERROR_OBJECT(self, "Unsupported video format for copy operation: %d", self->video_format);
            break;
    }
}

/* Helper function to clean up temporary private buffers */
static void
cleanup_temp_private_buffers (GstAmbaFileMuxer * self)
{
    for (guint i = 0; i < self->private_pad_count; i++) {
        if (self->private_pads[i].temp_buffer) {
            gst_buffer_unref(self->private_pads[i].temp_buffer);
            self->private_pads[i].temp_buffer = NULL;
        }
    }
}

/* Start method - perform complete configuration validation */
static gboolean
gst_amba_filemuxer_start (GstAggregator * agg)
{
    GstAmbaFileMuxer *self = GST_AMBA_FILEMUXER (agg);
    GST_DEBUG_OBJECT(self, "Starting amba_filemuxer element");

    // Validate format configuration
    if (self->private_pad_count > 0) {
        guint valid_formats = 0;
        for (guint i = 0; i < self->private_pad_count; i++) {
            if (self->private_pads[i].format != AMBA_PRIVATE_FORMAT_NONE) {
                const gchar *format_str = amba_private_format_to_string(self->private_pads[i].format);
                GST_DEBUG_OBJECT(self, "Pad %u has valid format: %s", i, format_str);
                valid_formats++;
            } else {
                GST_DEBUG_OBJECT(self, "Pad %u has no format set", i);
            }
        }
        if (valid_formats != self->private_pad_count) {
            GST_ERROR_OBJECT(self, "Invalid format configuration: %u valid formats for %u private pads. "
                           "Please ensure private-formats count matches private-pad-count.",
                           valid_formats, self->private_pad_count);
            return FALSE;
        }
        GST_DEBUG_OBJECT(self, "Format configuration validation passed: %u/%u pads configured",
                        valid_formats, self->private_pad_count);
    }

    return TRUE;
}

/* Element implementation */
static void
gst_amba_filemuxer_dispose (GObject * object)
{
    GstAmbaFileMuxer *self = GST_AMBA_FILEMUXER (object);

    // Clean up private pad info
    for (guint i = 0; i < AMBA_FILEMUXER_MAX_PRIVATE_PADS; i++) {
        self->private_pads[i].format = AMBA_PRIVATE_FORMAT_NONE;
        if (self->private_pads[i].temp_buffer) {
            gst_buffer_unref(self->private_pads[i].temp_buffer);
            self->private_pads[i].temp_buffer = NULL;
        }
        // Clear pad pointer reference (no unref needed as element manages refs)
        self->private_pads[i].pad = NULL;
    }
    g_free(self->private_formats);
    self->private_formats = NULL;
    // Clean up DMA allocator reference
    self->cavalry_allocator = NULL;

    G_OBJECT_CLASS (gst_amba_filemuxer_parent_class)->dispose (object);
}

static void
gst_amba_filemuxer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
    GstAmbaFileMuxer *self = GST_AMBA_FILEMUXER (object);

    switch (prop_id) {
        case PROP_PRIVATE_PAD_COUNT: {
            guint new_count = g_value_get_uint (value);
            if (new_count != self->private_pad_count) {
                self->private_pad_count = new_count;
                // TODO: Recreate pads if element is in READY state
            }
            break;
        }
        case PROP_PRIVATE_FORMATS: {
            const gchar *formats = g_value_get_string (value);
            g_free (self->private_formats);
            self->private_formats = g_strdup (formats);

            // Check if memory allocation was successful
            if (formats && !self->private_formats) {
                GST_ERROR_OBJECT(self, "Failed to allocate memory for private-formats property");
                return;
            }

            // Parse formats when property is set
            if (formats) {
                gchar **tokens = g_strsplit(formats, ",", -1);
                guint count = 0;

                for (guint i = 0; tokens[i] && count < AMBA_FILEMUXER_MAX_PRIVATE_PADS; i++) {
                    gchar *token = g_strstrip(tokens[i]);
                    if (g_strcmp0(token, "") != 0) {
                        // Convert string to enum format
                        AmbaPrivateFormat format = AMBA_PRIVATE_FORMAT_NONE;
                        if (g_strcmp0(token, "ME0") == 0) {
                            format = AMBA_PRIVATE_FORMAT_ME0;
                        } else if (g_strcmp0(token, "ME1") == 0) {
                            format = AMBA_PRIVATE_FORMAT_ME1;
                        } else if (g_strcmp0(token, "CE") == 0) {
                            format = AMBA_PRIVATE_FORMAT_CE;
                        } else {
                            GST_ERROR_OBJECT(self, "Invalid format '%s' in private-formats property", token);
                            g_strfreev(tokens);
                            return; // Don't set the property if format is invalid
                        }
                        self->private_pads[count].format = format;
                        const gchar *format_str = amba_private_format_to_string(format);
                        GST_DEBUG_OBJECT(self, "Set pad %u format to %s", count, format_str);
                        count++;
                    }
                }

                g_strfreev(tokens);
                GST_DEBUG_OBJECT(self, "Parsed %u formats from property", count);

                // Validate that format count matches pad count
                if (self->private_pad_count > 0 && count != self->private_pad_count) {
                    GST_ERROR_OBJECT(self, "Format count mismatch: %u formats specified but %u private pads configured. "
                                   "The number of formats must match the private-pad-count.",
                                   count, self->private_pad_count);
                    // Clear the parsed formats since they don't match
                    for (guint i = 0; i < AMBA_FILEMUXER_MAX_PRIVATE_PADS; i++) {
                        self->private_pads[i].format = AMBA_PRIVATE_FORMAT_NONE;
                    }
                    return;
                }
            } else {
                // Clear parsed formats if property is NULL
                for (guint i = 0; i < AMBA_FILEMUXER_MAX_PRIVATE_PADS; i++) {
                    self->private_pads[i].format = AMBA_PRIVATE_FORMAT_NONE;
                }
            }
            break;
        }
        case PROP_PACKED_MODE:
            self->packed_mode = g_value_get_boolean (value);
            GST_DEBUG_OBJECT(self, "Set packed-mode to %s", self->packed_mode ? "TRUE" : "FALSE");
            break;
        case PROP_DMA_OUTPUT_MODE: {
            gboolean new_dma_mode = g_value_get_boolean (value);
            if (new_dma_mode != self->dma_output_mode) {
                self->dma_output_mode = new_dma_mode;
                GST_DEBUG_OBJECT(self, "Set dma-output to %s", self->dma_output_mode ? "TRUE" : "FALSE");

                if (self->dma_output_mode && !self->cavalry_allocator) {
                    GST_WARNING_OBJECT(self, "DMA mode enabled but DMA allocator not available, will use default allocator");
                }
            }
            break;
        }
        case PROP_ALIGNMENT: {
            guint new_alignment = g_value_get_uint (value);
            if (new_alignment != self->alignment) {
                self->alignment = new_alignment;
                GST_DEBUG_OBJECT(self, "Set alignment to %u", self->alignment);

                // Reset caps negotiation state when alignment changes
                if (self->caps_negotiated) {
                    GST_DEBUG_OBJECT(self, "Alignment changed, resetting caps negotiation state");
                    self->caps_negotiated = FALSE;
                    self->video_input_size = 0;
                    self->video_output_size = 0;
                    self->total_output_size = 0;
                    self->video_width = 0;
                    self->video_height = 0;
                    self->video_pitch = 0;
                    // Reset calculated sizes for private pads
                    for (guint i = 0; i < self->private_pad_count; i++) {
                        self->private_pads[i].input_size = 0;
                        self->private_pads[i].output_size = 0;
                        self->private_pads[i].width = 0;
                        self->private_pads[i].height = 0;
                        self->private_pads[i].pitch = 0;
                        self->private_pads[i].aligned_height = 0;
                    }
                }
            }
            break;
        }
        default:
            G_OBJECT_CLASS (gst_amba_filemuxer_parent_class)->set_property (object, prop_id, value, pspec);
            break;
    }
}

static void
gst_amba_filemuxer_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
    GstAmbaFileMuxer *self = GST_AMBA_FILEMUXER (object);

    switch (prop_id) {
        case PROP_PRIVATE_PAD_COUNT:
            g_value_set_uint (value, self->private_pad_count);
            break;
        case PROP_PRIVATE_FORMATS:
            g_value_set_string (value, self->private_formats);
            break;
        case PROP_PACKED_MODE:
            g_value_set_boolean (value, self->packed_mode);
            break;
        case PROP_DMA_OUTPUT_MODE:
            g_value_set_boolean (value, self->dma_output_mode);
            break;
        case PROP_ALIGNMENT:
            g_value_set_uint (value, self->alignment);
            break;
        default:
            G_OBJECT_CLASS (gst_amba_filemuxer_parent_class)->get_property (object, prop_id, value, pspec);
            break;
    }
}

static void
gst_amba_filemuxer_class_init (GstAmbaFileMuxerClass * klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
    GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
    GstAggregatorClass *agg_class = GST_AGGREGATOR_CLASS (klass);

    gobject_class->dispose = gst_amba_filemuxer_dispose;
    gobject_class->set_property = gst_amba_filemuxer_set_property;
    gobject_class->get_property = gst_amba_filemuxer_get_property;

    properties[PROP_PRIVATE_PAD_COUNT] =
        g_param_spec_uint ("private-pad-count", "Private Pad Count",
                          /* cppcheck-suppress unknownMacro */
                          "Number of private data pads (0-" G_STRINGIFY(AMBA_FILEMUXER_MAX_PRIVATE_PADS) ")",
                          0, AMBA_FILEMUXER_MAX_PRIVATE_PADS, 0,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_PRIVATE_FORMATS] =
        g_param_spec_string ("private-formats", "Private Formats",
                           "Comma-separated list of private formats (ME0,ME1,CE)",
                           NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_PACKED_MODE] =
        g_param_spec_boolean ("packed-mode", "Packed Mode",
                            "TRUE: pack all data into single memory, FALSE: use separate memory for each input",
                            TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_DMA_OUTPUT_MODE] =
        g_param_spec_boolean ("dma-output", "DMA Output Mode",
                            "TRUE: use DMA allocator for output buffers, FALSE: use default allocator",
                            TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_ALIGNMENT] =
        g_param_spec_uint ("alignment", "Memory Alignment",
                          "Memory alignment requirement for output buffers (in bytes)",
                          1, 1024, 16, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (gobject_class, N_PROPERTIES, properties);

    gst_element_class_set_static_metadata (gstelement_class,
        "Amba File Muxer",
        "Muxer/Aggregator",
        "Combines standard video data with Ambarella private data into a single buffer with custom metadata",
        "Yang Yu <yyua@ambarella.com>");

    gst_element_class_add_static_pad_template (gstelement_class, &sink_video_template);
    gst_element_class_add_static_pad_template (gstelement_class, &sink_private_template);
    gst_element_class_add_static_pad_template (gstelement_class, &src_template);

    /* Override aggregator methods */
    agg_class->create_new_pad = gst_amba_filemuxer_create_new_pad;
    agg_class->aggregate = gst_amba_filemuxer_aggregate;
    agg_class->fixate_src_caps = gst_amba_filemuxer_fixate_src_caps;
    agg_class->start = gst_amba_filemuxer_start;

    GST_DEBUG_CATEGORY_INIT (amba_filemuxer_debug, "amba_filemuxer", 0, "Amba File Muxer");
}

static void
gst_amba_filemuxer_init (GstAmbaFileMuxer * self)
{
    self->private_pad_count = 0;
    self->private_formats = NULL;
    self->packed_mode = TRUE;       /* Default to packed mode for backward compatibility */
    self->dma_output_mode = TRUE;   /* Default to DMA allocator for better performance */
    self->caps_negotiated = FALSE;
    self->alignment = 16;           /* Default alignment is 16 bytes */
    self->video_input_size = 0;
    self->video_output_size = 0;
    self->total_output_size = 0;
    self->video_width = 0;
    self->video_height = 0;
    self->video_pitch = 0;
    self->video_aligned_height = 0;
    self->video_format = GST_VIDEO_FORMAT_UNKNOWN;

    // Initialize DMA allocator early
    gst_amba_cavalry_allocator_init_once();
    self->cavalry_allocator = gst_amba_cavalry_allocator_get();
    if (self->cavalry_allocator) {
        GST_DEBUG_OBJECT(self, "Initialized DMA allocator during element init");
    } else {
        GST_DEBUG_OBJECT(self, "DMA allocator not available during init, will use default allocator");
    }

    // Initialize aggregation state
    self->active_private_count = 0;
    self->has_private_data = FALSE;

    // Initialize private pad info
    for (guint i = 0; i < AMBA_FILEMUXER_MAX_PRIVATE_PADS; i++) {
        self->private_pads[i].pad = NULL;
        self->private_pads[i].format = AMBA_PRIVATE_FORMAT_NONE;
        self->private_pads[i].width = 0;
        self->private_pads[i].height = 0;
        self->private_pads[i].pitch = 0;
        self->private_pads[i].aligned_height = 0;
        self->private_pads[i].input_size = 0;
        self->private_pads[i].output_size = 0;
        self->private_pads[i].temp_buffer = NULL;
        self->private_pads[i].has_data = FALSE;
    }

    self->video_sink_pad = NULL;

    // Create the sink_video pad manually since it's an ALWAYS pad
    GstPadTemplate *sink_video_templ = gst_element_class_get_pad_template(GST_ELEMENT_CLASS(G_OBJECT_GET_CLASS(self)), "sink_video");
    if (sink_video_templ) {
        GstAggregatorPad *agg_pad = gst_amba_filemuxer_create_new_pad(GST_AGGREGATOR(self), sink_video_templ, "sink_video", NULL);
        if (agg_pad) {
            gst_element_add_pad(GST_ELEMENT(self), GST_PAD(agg_pad));
        }
    }
}

/* Pad creation */
static GstAggregatorPad *
gst_amba_filemuxer_create_new_pad (GstAggregator * agg, GstPadTemplate * templ,
    const gchar * name, const GstCaps * caps)
{
    DUNUSED(caps);
    GstAmbaFileMuxer *self = GST_AMBA_FILEMUXER (agg);
    GstAggregatorPad *agg_pad;

    GST_DEBUG_OBJECT(self, "Creating new pad: %s", name);

    // Create the aggregator pad
    agg_pad = g_object_new(GST_TYPE_AGGREGATOR_PAD,
        "name", name, "direction", GST_PAD_SINK, "template", templ, NULL);

    if (g_strcmp0(name, "sink_video") == 0) {
        self->video_sink_pad = GST_PAD(agg_pad);
        GST_DEBUG_OBJECT(self, "Created video sink pad: %s", name);

        // Add probe to set private pad caps when video pad gets caps
        gst_pad_add_probe(self->video_sink_pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
                         gst_amba_filemuxer_video_pad_probe, self, NULL);
    } else {
        // Parse pad index from name (e.g., "sink_private_0" -> index 0)
        guint pad_index = 0;
        if (g_str_has_prefix(name, "sink_private_")) {
            const gchar *index_str = name + strlen("sink_private_");
            pad_index = g_ascii_strtoull(index_str, NULL, 10);
        }

        // Validate pad index
        if (pad_index >= self->private_pad_count) {
            if (self->private_pad_count == 0) {
                GST_ERROR_OBJECT(self, "Invalid private pad index %u (no private pads configured)", pad_index);
            } else {
                GST_ERROR_OBJECT(self, "Invalid private pad index %u (max: %u)", pad_index, self->private_pad_count - 1);
            }
            g_object_unref(agg_pad);
            return NULL;
        }

        // Store pad in the correct slot
        if (self->private_pads[pad_index].pad == NULL) {
            self->private_pads[pad_index].pad = GST_PAD(agg_pad);
            GST_DEBUG_OBJECT(self, "Created private sink pad %u: %s", pad_index, name);
        } else {
            GST_WARNING_OBJECT(self, "Private pad %u already exists, replacing", pad_index);
            gst_object_unref(self->private_pads[pad_index].pad);
            self->private_pads[pad_index].pad = GST_PAD(agg_pad);
        }
    }

    return agg_pad;
}

/* Fixate src caps to match video input caps */
static GstCaps *
gst_amba_filemuxer_fixate_src_caps (GstAggregator * agg, GstCaps * caps)
{
    GstAmbaFileMuxer *self = GST_AMBA_FILEMUXER (agg);
    GstCaps *video_caps = NULL;
    GstCaps *result = NULL;

    // Get caps from video sink pad
    if (self->video_sink_pad) {
        video_caps = gst_pad_get_current_caps(self->video_sink_pad);
        if (video_caps) {
            result = gst_caps_copy(video_caps);
            gst_caps_unref(video_caps);
        }
    }

    if (!result) {
        result = gst_caps_copy(caps);
    }

    return result;
}

/* Aggregation logic */
static GstFlowReturn
gst_amba_filemuxer_aggregate (GstAggregator * agg, gboolean timeout)
{
    DUNUSED(timeout);
    GstAmbaFileMuxer *self = GST_AMBA_FILEMUXER (agg);
    GstBuffer *output_buffer = NULL;
    GstMapInfo map_info;
    guint offset = 0;

    // Reset pad state tracking
    self->active_private_count = 0;
    self->has_private_data = FALSE;
    for (guint i = 0; i < self->private_pad_count; i++) {
        self->private_pads[i].has_data = FALSE;
    }
    // Check if video pad exists (should always exist)
    if (self->video_sink_pad == NULL) {
        GST_ERROR_OBJECT(self, "Video sink pad not found");
        return GST_FLOW_ERROR;
    }
    // Get video buffer - base class guarantees it exists
    GstAggregatorPad *video_pad = GST_AGGREGATOR_PAD(self->video_sink_pad);
    // Check video pad EOS status
    if (gst_aggregator_pad_is_eos(video_pad)) {
        GST_DEBUG_OBJECT(self, "Video pad has EOS, returning EOS");
        return GST_FLOW_EOS;
    }
    GstBuffer *video_buffer = gst_aggregator_pad_pop_buffer(video_pad);
    if (!video_buffer) {
        GST_ERROR_OBJECT(self, "Failed to get video buffer despite base class guarantee");
        return GST_FLOW_ERROR;
    }

    // Process private pads - base class guarantees all pads have buffers or EOS
    for (guint i = 0; i < self->private_pad_count; i++) {
        GstAggregatorPad *private_pad = GST_AGGREGATOR_PAD(self->private_pads[i].pad);
        // Check EOS status
        if (gst_aggregator_pad_is_eos(private_pad)) {
            GST_DEBUG_OBJECT(self, "Private pad %u has EOS - will continue with available data", i);
            self->private_pads[i].temp_buffer = NULL;
            self->private_pads[i].has_data = FALSE;
            continue;
        }
        // Get private buffer - base class guarantees it exists
        GstBuffer *private_buffer = gst_aggregator_pad_pop_buffer(private_pad);
        if (!private_buffer) {
            GST_ERROR_OBJECT(self, "Failed to get private buffer from pad %u despite base class guarantee", i);
            gst_buffer_unref(video_buffer);
            return GST_FLOW_ERROR;
        }
        // Check if the buffer has actual data (not empty)
        gsize buffer_size = gst_buffer_get_size(private_buffer);
        if (buffer_size == 0) {
            GST_DEBUG_OBJECT(self, "Private pad %u has empty buffer, treating as no data", i);
            gst_buffer_unref(private_buffer);
            self->private_pads[i].temp_buffer = NULL;
            self->private_pads[i].has_data = FALSE;
            continue;
        }
        // Store the buffer for later processing
        self->private_pads[i].temp_buffer = private_buffer;
        self->private_pads[i].has_data = TRUE;
        self->active_private_count++;
        self->has_private_data = TRUE;
        GST_DEBUG_OBJECT(self, "Private pad %u has buffer with %zu bytes, active count: %u", i, buffer_size, self->active_private_count);
    }

    // Calculate sizes if not done yet
    if (!self->caps_negotiated) {
        if (!gst_amba_filemuxer_negotiate_caps(self)) {
            goto error;
        }
    }

    // Create output buffer based on packed mode
    if (self->packed_mode) {
        // Packed mode: single memory for all data
        if (self->total_output_size == 0) {
            GST_ERROR_OBJECT(self, "total_output_size is 0, cannot create output buffer");
            goto error;
        }

        // Get appropriate allocator based on DMA output mode
        GstAllocator *allocator = (self->dma_output_mode && self->cavalry_allocator) ? self->cavalry_allocator : NULL;
        GST_DEBUG_OBJECT(self, "Allocating packed mode buffer with size %u using %s allocator",
                        self->total_output_size, allocator ? "DMA" : "default");
        output_buffer = gst_buffer_new_allocate(allocator, self->total_output_size, NULL);
        if (!output_buffer) {
            GST_ERROR_OBJECT(self, "Failed to allocate output buffer with size %u using %s allocator",
                           self->total_output_size, allocator ? "DMA" : "default");
            goto error;
        }
        GST_DEBUG_OBJECT(self, "Successfully created packed mode output buffer with size %u using %s allocator",
                        self->total_output_size, allocator ? "DMA" : "default");
    } else {
        // Multi-memory mode: separate memory for each input
        output_buffer = gst_buffer_new();
        if (!output_buffer) {
            GST_ERROR_OBJECT(self, "Failed to create output buffer for multi-memory mode");
            goto error;
        }
        GST_DEBUG_OBJECT(self, "Created multi-memory mode output buffer");
    }

    // Copy video data
    if (gst_buffer_map(video_buffer, &map_info, GST_MAP_READ)) {
        if (map_info.size != self->video_input_size) {
            GST_ERROR_OBJECT(self, "Video buffer size (%zu) != expected video_input_size (%u). "
                           "This indicates a mismatch between the actual data format and the declared caps format.",
                           map_info.size, self->video_input_size);
            gst_buffer_unmap(video_buffer, &map_info);
            goto error;
        }

        if (self->packed_mode) {
            // Packed mode: copy to single memory
            if (gst_buffer_get_size(output_buffer) < self->video_output_size) {
                GST_ERROR_OBJECT(self, "Output buffer size (%" G_GSIZE_FORMAT ") < video_output_size (%u)", gst_buffer_get_size(output_buffer), self->video_output_size);
                gst_buffer_unmap(video_buffer, &map_info);
                goto error;
            }

            // Copy video data with pitch alignment
            if (self->video_width == self->video_pitch && self->video_height == self->video_aligned_height) {
                // Width equals pitch, can copy directly
                gst_buffer_fill(output_buffer, 0, map_info.data, self->video_input_size);
            } else {
                // Width differs from pitch, need to copy line by line with format-specific handling
                guint8 *src_data = map_info.data;
                guint8 *dst_data;
                GstMapInfo dst_map_info;
                if (gst_buffer_map(output_buffer, &dst_map_info, GST_MAP_WRITE)) {
                    dst_data = dst_map_info.data;
                    copy_video_data_with_format(self, src_data, dst_data);
                    gst_buffer_unmap(output_buffer, &dst_map_info);
                } else {
                    GST_ERROR_OBJECT(self, "Failed to map output buffer for video data writing");
                    gst_buffer_unmap(video_buffer, &map_info);
                    goto error;
                }
            }
            offset = self->video_output_size;
        } else {
            // Multi-memory mode: create new memory for video data
            GstAllocator *allocator = (self->dma_output_mode && self->cavalry_allocator) ? self->cavalry_allocator : NULL;
            GstMemory *video_memory = gst_allocator_alloc(allocator, self->video_output_size, NULL);
            if (!video_memory) {
                GST_ERROR_OBJECT(self, "Failed to allocate video memory for multi-memory mode using %s allocator",
                               allocator ? "DMA" : "default");
                gst_buffer_unmap(video_buffer, &map_info);
                goto error;
            }

            // Copy video data to new memory with pitch alignment
            GstMapInfo new_map_info;
            if (gst_memory_map(video_memory, &new_map_info, GST_MAP_WRITE)) {
                if (self->video_width == self->video_pitch && self->video_height == self->video_aligned_height) {
                    // Width equals pitch, can copy directly
                    memcpy(new_map_info.data, map_info.data, self->video_input_size);
                } else {
                    // Width differs from pitch, need to copy line by line with format-specific handling
                    guint8 *src_data = map_info.data;
                    guint8 *dst_data = new_map_info.data;
                    copy_video_data_with_format(self, src_data, dst_data);
                }
                gst_memory_unmap(video_memory, &new_map_info);
                gst_buffer_append_memory(output_buffer, video_memory);
            } else {
                GST_ERROR_OBJECT(self, "Failed to map video memory for writing");
                gst_memory_unref(video_memory);
                gst_buffer_unmap(video_buffer, &map_info);
                goto error;
            }
            offset = 0;  // Not used in multi-memory mode
        }

        gst_buffer_unmap(video_buffer, &map_info);

        // Copy timestamp information from input buffer to output buffer
        GST_BUFFER_PTS(output_buffer) = GST_BUFFER_PTS(video_buffer);
        GST_BUFFER_DTS(output_buffer) = GST_BUFFER_DTS(video_buffer);
        GST_BUFFER_DURATION(output_buffer) = GST_BUFFER_DURATION(video_buffer);
        GST_BUFFER_OFFSET(output_buffer) = GST_BUFFER_OFFSET(video_buffer);
        GST_BUFFER_OFFSET_END(output_buffer) = GST_BUFFER_OFFSET_END(video_buffer);
        // Copy buffer flags (important for sync=false mode)
        GST_BUFFER_FLAGS(output_buffer) = GST_BUFFER_FLAGS(video_buffer);

        // Notify aggregator about selected samples
        GstClockTime pts = GST_BUFFER_PTS(video_buffer);
        GstClockTime dts = GST_BUFFER_DTS(video_buffer);
        GstClockTime duration = GST_BUFFER_DURATION(video_buffer);
        gst_aggregator_selected_samples(agg, pts, dts, duration, NULL);
    } else {
        GST_ERROR_OBJECT(self, "Failed to map video buffer for reading");
        goto error;
    }
    gst_buffer_unref(video_buffer);
    video_buffer = NULL;

    // Copy private data - only for pads that have data
    GST_DEBUG_OBJECT(self, "Processing %u private data pads (active: %u)", self->private_pad_count, self->active_private_count);
    for (guint i = 0; i < self->private_pad_count; i++) {
        GstBuffer *private_buffer = self->private_pads[i].temp_buffer;
        guint input_size = self->private_pads[i].input_size;
        guint output_size = self->private_pads[i].output_size;

        if (private_buffer && gst_buffer_map(private_buffer, &map_info, GST_MAP_READ)) {
            GST_DEBUG_OBJECT(self, "Copying private data from pad %u, size: %zu, input_size: %u", i, map_info.size, input_size);

            // Check if actual data size matches calculated size
            if (map_info.size != input_size) {
                GST_ERROR_OBJECT(self, "Private data buffer size (%zu) != input size (%u) for pad %u. "
                               "This indicates a mismatch between the actual data and the expected format.",
                               map_info.size, input_size, i);
                gst_buffer_unmap(private_buffer, &map_info);
                goto error;
            }

            if (self->packed_mode) {
                // Packed mode: copy to single memory
                // Check if we have enough space in output buffer
                if (offset + output_size > gst_buffer_get_size(output_buffer)) {
                    GST_ERROR_OBJECT(self, "Not enough space in output buffer for private data %u", i);
                    gst_buffer_unmap(private_buffer, &map_info);
                    goto error;
                }

                // Copy private data with pitch alignment
                if (self->private_pads[i].width == self->private_pads[i].pitch) {
                    // Width equals pitch, can copy directly
                    gst_buffer_fill(output_buffer, offset, map_info.data, input_size);
                } else {
                    // Width differs from pitch, need to copy line by line
                    guint8 *src_data = map_info.data;
                    guint8 *dst_data;
                    GstMapInfo dst_map_info;
                    if (gst_buffer_map(output_buffer, &dst_map_info, GST_MAP_WRITE)) {
                        dst_data = dst_map_info.data + offset;

                        // Copy private data line by line
                        for (gint y = 0; y < self->private_pads[i].height; y++) {
                            memcpy(dst_data + y * self->private_pads[i].pitch,
                                   src_data + y * self->private_pads[i].width,
                                   self->private_pads[i].width);
                        }

                        gst_buffer_unmap(output_buffer, &dst_map_info);
                    } else {
                        GST_ERROR_OBJECT(self, "Failed to map output buffer for private data writing");
                        gst_buffer_unmap(private_buffer, &map_info);
                        goto error;
                    }
                }
                /*FIXME: when aligned_height > height, now we leave the rest lines without filling*/
                offset += output_size;
            } else {
                // Multi-memory mode: create new memory for private data
                GstAllocator *allocator = (self->dma_output_mode && self->cavalry_allocator) ? self->cavalry_allocator : NULL;
                GstMemory *private_memory = gst_allocator_alloc(allocator, output_size, NULL);
                if (!private_memory) {
                    GST_ERROR_OBJECT(self, "Failed to allocate private memory for multi-memory mode using %s allocator",
                                   allocator ? "DMA" : "default");
                    gst_buffer_unmap(private_buffer, &map_info);
                    goto error;
                }

                // Copy private data to new memory with pitch alignment
                GstMapInfo new_map_info;
                if (gst_memory_map(private_memory, &new_map_info, GST_MAP_WRITE)) {
                    if (self->private_pads[i].width == self->private_pads[i].pitch) {
                        // Width equals pitch, can copy directly
                        memcpy(new_map_info.data, map_info.data, input_size);
                    } else {
                        // Width differs from pitch, need to copy line by line
                        guint8 *src_data = map_info.data;
                        guint8 *dst_data = new_map_info.data;

                        // Copy private data line by line
                        for (gint y = 0; y < self->private_pads[i].height; y++) {
                            memcpy(dst_data + y * self->private_pads[i].pitch,
                                   src_data + y * self->private_pads[i].width,
                                   self->private_pads[i].width);
                        }
                    }
                    gst_memory_unmap(private_memory, &new_map_info);
                    gst_buffer_append_memory(output_buffer, private_memory);
                } else {
                    GST_ERROR_OBJECT(self, "Failed to map private memory for writing");
                    gst_memory_unref(private_memory);
                    gst_buffer_unmap(private_buffer, &map_info);
                    goto error;
                }
            }

            gst_buffer_unmap(private_buffer, &map_info);
        } else {
            GST_DEBUG_OBJECT(self, "No private buffer available for pad %u (EOS or no data), expected input_size: %u", i, input_size);
            // For pads without data, we still need to reserve space in packed mode
            if (self->packed_mode) {
                offset += output_size;
            }
        }
        if (private_buffer) {
            gst_buffer_unref(private_buffer);
            self->private_pads[i].temp_buffer = NULL;
        }
    }

    // Add custom metadata only if there are active private data pads
    if (self->has_private_data) {
        GST_DEBUG_OBJECT(self, "Adding custom meta for %u active private data pads", self->active_private_count);

        // Use the predefined macro for adding meta
        AmbaPrivateDataMeta *meta = amba_buffer_add_private_data_meta(output_buffer);

        if (meta) {
            meta->private_data_count = self->active_private_count;

            guint meta_index = 0;
            for (guint i = 0; i < self->private_pad_count && meta_index < self->active_private_count; i++) {
                // Only add meta for pads that actually had data
                if (self->private_pads[i].has_data) {
                    // Use pre-parsed format information - no fallback
                    if (self->private_pads[i].format == AMBA_PRIVATE_FORMAT_NONE) {
                        GST_ERROR_OBJECT(self, "Invalid or missing format for pad %u (format=%u)",
                                       i, self->private_pads[i].format);
                        goto error;
                    }

                    if (self->packed_mode) {
                        // Packed mode: calculate offset based on previous pads' sizes
                        guint meta_offset = self->video_output_size;
                        for (guint j = 0; j < i; j++) {
                            meta_offset += self->private_pads[j].output_size;
                        }
                        meta->private_data[meta_index].index = 0;  // Single memory
                        meta->private_data[meta_index].offset = meta_offset;
                    } else {
                        // Multi-memory mode: offset is 0, use memory index
                        meta->private_data[meta_index].index = 1 + meta_index;  // Memory index (0=video, 1+=private)
                        meta->private_data[meta_index].offset = 0;
                    }
                    meta->private_data[meta_index].size = self->private_pads[i].output_size;
                    meta->private_data[meta_index].format = self->private_pads[i].format;
                    meta->private_data[meta_index].width = self->private_pads[i].width;
                    meta->private_data[meta_index].height = self->private_pads[i].height;
                    meta->private_data[meta_index].pitch = self->private_pads[i].pitch;

                    const gchar *format_str = amba_private_format_to_string(meta->private_data[meta_index].format);
                    GST_DEBUG_OBJECT(self, "Meta entry %u (pad %u): index=%u, offset=%u, size=%u, format=%s, width=%u, height=%u, pitch=%u",
                                   meta_index, i, meta->private_data[meta_index].index,
                                   meta->private_data[meta_index].offset,
                                   meta->private_data[meta_index].size,
                                   format_str,
                                   meta->private_data[meta_index].width,
                                   meta->private_data[meta_index].height,
                                   meta->private_data[meta_index].pitch);
                    meta_index++;
                }
            }
            GST_DEBUG_OBJECT(self, "Successfully added custom meta with %u private data entries", self->active_private_count);
        } else {
            GST_ERROR_OBJECT(self, "Failed to add custom meta to buffer - meta is NULL");
        }
    } else {
        GST_DEBUG_OBJECT(self, "No active private data pads, skipping meta addition");
    }

    // Add standard GstVideoMeta for downstream elements
    GstVideoMeta *video_meta = gst_buffer_add_video_meta(output_buffer, GST_VIDEO_FRAME_FLAG_NONE,
                                                        self->video_format, self->video_width, self->video_height);
    if (video_meta) {
        video_meta->offset[0] = 0;
        video_meta->stride[0] = self->video_pitch;
        video_meta->offset[1] = self->video_pitch * self->video_aligned_height;
        video_meta->stride[1] = self->video_pitch;
        GST_DEBUG_OBJECT(self, "Added GstVideoMeta: width=%d, height=%d, stride[0]=%d, stride[1]=%d",
                        self->video_width, self->video_height, video_meta->stride[0], video_meta->stride[1]);
    } else {
        GST_WARNING_OBJECT(self, "Failed to add GstVideoMeta to buffer");
    }

    // Push output buffer
    GST_DEBUG_OBJECT(self, "Pushing output buffer with final size: %" G_GSIZE_FORMAT, gst_buffer_get_size(output_buffer));
    GstFlowReturn ret = gst_aggregator_finish_buffer(agg, output_buffer);

    GST_DEBUG_OBJECT(self, "Buffer pushed with result: %s", gst_flow_get_name(ret));
    return ret;

error:
    // Centralized error handling
    if (video_buffer) {
        gst_buffer_unref(video_buffer);
    }
    if (output_buffer) {
        gst_buffer_unref(output_buffer);
    }
    cleanup_temp_private_buffers(self);
    return GST_FLOW_ERROR;
}

/* Negotiate caps and calculate sizes for video and private pads */
static gboolean
gst_amba_filemuxer_negotiate_caps (GstAmbaFileMuxer * self)
{
    gboolean success = FALSE;

    GstCaps *video_caps = NULL;
    GstCaps *private_caps = NULL;
    gint width = 0, height = 0;
    GstStructure *str = NULL;
    const gchar *format_str = NULL;

    video_caps = gst_pad_get_current_caps(self->video_sink_pad);
    if (video_caps) {
        str = gst_caps_get_structure(video_caps, 0);
        if (gst_structure_get_int(str, "width", &width) &&
            gst_structure_get_int(str, "height", &height) &&
            (format_str = gst_structure_get_string(str, "format"))) {
            // Store video dimensions and format for GstVideoMeta
            self->video_width = width;
            self->video_height = height;
            self->video_pitch = GST_ROUND_UP_N(width, self->alignment);
            // video output height is rounded up to 16
            self->video_aligned_height = GST_ROUND_UP_N(self->video_height, 16);
            // Detect video format
            if (g_strcmp0(format_str, "NV12") == 0) {
                self->video_format = GST_VIDEO_FORMAT_NV12;
                self->video_output_size = self->video_pitch * self->video_aligned_height * 3 / 2;
            } else if (g_strcmp0(format_str, "NV16") == 0) {
                self->video_format = GST_VIDEO_FORMAT_NV16;
                self->video_output_size = self->video_pitch * self->video_aligned_height * 2;
            } else {
                GST_ERROR_OBJECT(self, "Unsupported video format: %s", format_str);
                goto cleanup;
            }
            // Calculate video input size using unified API
            self->video_input_size = amba_get_data_size_from_caps(video_caps);
            if (self->video_input_size == 0) {
                GST_ERROR_OBJECT(self, "Failed to calculate video size for format %s", format_str);
                goto cleanup;
            }
            GST_DEBUG_OBJECT(self, "get video pitch: %u, aligned height: %u, video output size: %u", self->video_pitch, self->video_aligned_height, self->video_output_size);
            self->total_output_size = self->video_output_size;

            // Calculate private data sizes using the new unified API
            for (guint i = 0; i < self->private_pad_count; i++) {
                // Get private pad caps for size calculation
                if (self->private_pads[i].pad) {
                    private_caps = gst_pad_get_current_caps(self->private_pads[i].pad);
                }

                if (!private_caps) {
                    // Fallback: create private caps based on video caps and format
                    private_caps = gst_amba_filemuxer_create_private_caps(self, i);
                }

                if (private_caps) {
                    str = gst_caps_get_structure(private_caps, 0);
                    if (gst_structure_get_int(str, "width", &width) &&
                        gst_structure_get_int(str, "height", &height) &&
                        (format_str = gst_structure_get_string(str, "format"))) {
                        self->private_pads[i].width = width;
                        self->private_pads[i].height = height;
                        self->private_pads[i].pitch = GST_ROUND_UP_N(width, self->alignment);
                        // caps make guarantees height is rounded up to 2, so aligned_height round up to 4 is enough
                        self->private_pads[i].aligned_height = GST_ROUND_UP_N(height, 4);
                        self->private_pads[i].input_size = amba_get_data_size_from_caps(private_caps);
                        if (self->private_pads[i].input_size == 0) {
                            GST_ERROR_OBJECT(self, "Failed to calculate input size for private pad %u: unsupported format or invalid caps", i);
                            goto cleanup;
                        }
                        self->private_pads[i].output_size = self->private_pads[i].pitch * self->private_pads[i].aligned_height;
                        self->total_output_size += self->private_pads[i].output_size;
                        GST_DEBUG_OBJECT(self, "private pad %u: width=%d, height=%d, pitch=%d, aligned_height=%d, format=%s, input_size=%u, output_size=%u",
                            i, width, height, self->private_pads[i].pitch, self->private_pads[i].aligned_height, format_str, self->private_pads[i].input_size, self->private_pads[i].output_size);
                    }
                } else {
                    GST_ERROR_OBJECT(self, "Failed to get caps for private pad %u", i);
                    goto cleanup;
                }
            }
            GST_DEBUG_OBJECT(self, "Caps negotiated: video_input_size=%u, total_output_size=%u",
                self->video_input_size, self->total_output_size);
            self->caps_negotiated = TRUE;
            success = TRUE;
        } else {
            GST_ERROR_OBJECT(self, "Failed to get width/height/format from caps");
            goto cleanup;
        }
    } else {
        GST_ERROR_OBJECT(self, "Failed to get current caps from video sink pad");
        goto cleanup;
    }

cleanup:
    if (video_caps) {
        gst_caps_unref(video_caps);
    }
    if (private_caps) {
        gst_caps_unref(private_caps);
    }
    return success;
}

/* Create caps for private pads based on video caps and format configuration */
static GstCaps *
gst_amba_filemuxer_create_private_caps (GstAmbaFileMuxer * self, guint pad_index)
{
    if (pad_index >= self->private_pad_count) {
        GST_ERROR_OBJECT(self, "Invalid pad index %u (max: %u)", pad_index, self->private_pad_count - 1);
        return NULL;
    }

    if (self->private_pads[pad_index].format == AMBA_PRIVATE_FORMAT_NONE) {
        GST_ERROR_OBJECT(self, "No format configured for private pad %u", pad_index);
        return NULL;
    }

    // Get video caps to extract width/height
    GstCaps *video_caps = NULL;
    if (self->video_sink_pad) {
        video_caps = gst_pad_get_current_caps(self->video_sink_pad);
    }

    if (!video_caps) {
        GST_ERROR_OBJECT(self, "No video caps available for private pad negotiation");
        return NULL;
    }

    GstStructure *video_str = gst_caps_get_structure(video_caps, 0);
    gint video_width, video_height;

    if (!gst_structure_get_int(video_str, "width", &video_width) ||
        !gst_structure_get_int(video_str, "height", &video_height)) {
        GST_ERROR_OBJECT(self, "Failed to get width/height from video caps");
        gst_caps_unref(video_caps);
        return NULL;
    }

    // Create private caps based on format
    GstCaps *private_caps = NULL;
    switch (self->private_pads[pad_index].format) {
        case AMBA_PRIVATE_FORMAT_ME0:
            private_caps = amba_create_me0_caps_from_video(video_width, video_height);
            break;
        case AMBA_PRIVATE_FORMAT_ME1:
            private_caps = amba_create_me1_caps_from_video(video_width, video_height);
            break;
        case AMBA_PRIVATE_FORMAT_CE:
            private_caps = amba_create_ce_caps_from_video(video_width, video_height);
            break;
        case AMBA_PRIVATE_FORMAT_NONE:
        default: {
            const gchar *format_str = amba_private_format_to_string(self->private_pads[pad_index].format);
            GST_ERROR_OBJECT(self, "Unsupported format '%s' (%u) for private pad %u",
                            format_str, self->private_pads[pad_index].format, pad_index);
            gst_caps_unref(video_caps);
            return NULL;
        }
    }

    gst_caps_unref(video_caps);

    if (!private_caps) {
        GST_ERROR_OBJECT(self, "Failed to create private caps for pad %u", pad_index);
        return NULL;
    }

    GST_DEBUG_OBJECT(self, "Negotiated private caps for pad %u: %s", pad_index,
                    gst_caps_to_string(private_caps));

    return private_caps;
}

/* Set caps for all private pads when video pad has caps */
static void
gst_amba_filemuxer_set_private_pad_caps (GstAmbaFileMuxer * self)
{
    if (!self->video_sink_pad) {
        GST_DEBUG_OBJECT(self, "No video sink pad available");
        return;
    }

    GstCaps *video_caps = gst_pad_get_current_caps(self->video_sink_pad);
    if (!video_caps) {
        GST_DEBUG_OBJECT(self, "No video caps available yet");
        return;
    }

    GST_DEBUG_OBJECT(self, "Setting caps for all private pads");

    for (guint i = 0; i < self->private_pad_count; i++) {
        if (self->private_pads[i].pad && !gst_pad_get_current_caps(self->private_pads[i].pad)) {
            GstCaps *private_caps = gst_amba_filemuxer_create_private_caps(self, i);
            if (private_caps) {
                gst_pad_set_caps(self->private_pads[i].pad, private_caps);
                gst_caps_unref(private_caps);
                GST_DEBUG_OBJECT(self, "Set caps for private pad %u (delayed)", i);
            } else {
                GST_ERROR_OBJECT(self, "Failed to negotiate caps for private pad %u", i);
            }
        }
    }

    gst_caps_unref(video_caps);
}

/* Video pad probe to set private pad caps when video pad gets caps */
static GstPadProbeReturn
gst_amba_filemuxer_video_pad_probe (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
    DUNUSED(pad);
    GstAmbaFileMuxer *self = GST_AMBA_FILEMUXER(user_data);

    if (info->type & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM) {
        GstEvent *event = info->data;
        if (GST_EVENT_TYPE(event) == GST_EVENT_CAPS) {
            // Reset caps negotiation state when video caps change
            if (self->caps_negotiated) {
                GST_DEBUG_OBJECT(self, "Video caps changed, resetting caps negotiation state");
                self->caps_negotiated = FALSE;
                self->video_input_size = 0;
                self->video_output_size = 0;
                self->total_output_size = 0;
                self->video_width = 0;
                self->video_height = 0;
                self->video_pitch = 0;
                self->video_aligned_height = 0;
                self->video_format = GST_VIDEO_FORMAT_UNKNOWN;
                // Reset calculated sizes for private pads
                for (guint i = 0; i < self->private_pad_count; i++) {
                    self->private_pads[i].input_size = 0;
                    self->private_pads[i].output_size = 0;
                    self->private_pads[i].width = 0;
                    self->private_pads[i].height = 0;
                    self->private_pads[i].pitch = 0;
                    self->private_pads[i].aligned_height = 0;
                }
            }
            gst_amba_filemuxer_set_private_pad_caps(self);
        }
    }

    return GST_PAD_PROBE_OK;
}