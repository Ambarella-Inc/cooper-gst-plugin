/* GStreamer split muxer bin
 * Copyright (C) 2014-2019 Jan Schmidt <jan@centricular.com>
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

#ifndef __GST_MSPLITMUXSINK_H__
#define __GST_MSPLITMUXSINK_H__

#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>
#include <gst/base/base.h>

G_BEGIN_DECLS
#define GST_TYPE_MSPLITMUX_SINK               (gst_msplitmux_sink_get_type())
#define GST_MSPLITMUX_SINK(obj)               (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MSPLITMUX_SINK,GstMSplitMuxSink))
#define GST_MSPLITMUX_SINK_CLASS(klass)       (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MSPLITMUX_SINK,GstMSplitMuxSinkClass))
#define GST_IS_MSPLITMUX_SINK(obj)            (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MSPLITMUX_SINK))
#define GST_IS_MSPLITMUX_SINK_CLASS(klass)    (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MSPLITMUX_SINK))
typedef struct _GstMSplitMuxSink GstMSplitMuxSink;
typedef struct _GstMSplitMuxSinkClass GstMSplitMuxSinkClass;

GType gst_msplitmux_sink_get_type (void);

typedef enum _MSplitMuxInputState
{
  MSPLITMUX_INPUT_STATE_STOPPED,
  MSPLITMUX_INPUT_STATE_COLLECTING_GOP_START,    /* Waiting for the next ref ctx keyframe */
  MSPLITMUX_INPUT_STATE_WAITING_GOP_COLLECT,     /* Waiting for all streams to collect GOP */
  MSPLITMUX_INPUT_STATE_FINISHING_UP             /* Got EOS from reference ctx, send everything */
} MSplitMuxInputState;

typedef enum _MSplitMuxOutputState
{
  MSPLITMUX_OUTPUT_STATE_STOPPED,
  MSPLITMUX_OUTPUT_STATE_AWAITING_COMMAND,       /* Waiting first command packet from input */
  MSPLITMUX_OUTPUT_STATE_OUTPUT_GOP,     /* Outputting a collected GOP */
  MSPLITMUX_OUTPUT_STATE_ENDING_FILE,    /* Finishing the current fragment */
  MSPLITMUX_OUTPUT_STATE_ENDING_STREAM,  /* Finishing up the entire stream due to input EOS */
  MSPLITMUX_OUTPUT_STATE_START_NEXT_FILE /* Restarting after ENDING_FILE */
} MSplitMuxOutputState;

typedef enum _MSplitMuxOutputCommandType
{
  MSPLITMUX_OUTPUT_COMMAND_FINISH_FRAGMENT,
  MSPLITMUX_OUTPUT_COMMAND_RELEASE_GOP,
} MSplitMuxOutputCommandType;

typedef struct _MSplitMuxOutputCommand
{
  MSplitMuxOutputCommandType cmd_type;
  struct
  {
    GstClockTimeDiff max_output_ts;   /* Set the limit to stop GOP output */
  } release_gop;
} MSplitMuxOutputCommand;

typedef struct
{
  guint fragment_id;
  GstClockTime last_running_time;

  GstClockTime fragment_offset;
  GstClockTime fragment_duration;
} OutputFragmentInfo;

typedef struct _MqStreamBuf
{
  gboolean keyframe;
  GstClockTimeDiff run_ts;
  guint64 buf_size;
  GstClockTime duration;
} MqStreamBuf;

typedef struct {
  /* For the very first GOP if it was created from a GAP event */
  gboolean from_gap;

  /* Minimum start time (PTS or DTS) of the GOP */
  GstClockTimeDiff start_time;
  /* Start time (PTS) of the GOP */
  GstClockTimeDiff start_time_pts;
  /* Minimum start timecode of the GOP */
  GstVideoTimeCode *start_tc;

  /* Number of bytes we've collected into the GOP */
  guint64 total_bytes;
  /* Number of bytes from the reference context
   * that we've collected into the GOP */
  guint64 reference_bytes;

  gboolean sent_fku;
} InputGop;

typedef struct _MqStreamCtx
{
  GstMSplitMuxSink *splitmux;
  guint ctx_id;

  guint q_overrun_id;
  guint sink_pad_block_id;
  guint src_pad_block_id;
  gulong fragment_block_id;

  gboolean is_reference;

  gboolean flushing;
  gboolean in_eos;
  gboolean out_eos;
  gboolean out_eos_async_done;
  gboolean need_unblock;
  gboolean caps_change;

  GstSegment in_segment;
  GstSegment out_segment;
  GstClockTimeDiff out_fragment_start_runts;

  GstClockTimeDiff in_running_time;

  GstClockTimeDiff out_running_time;
  GstClockTimeDiff out_running_time_end; /* max run ts + durations */

  GstElement *q;
  GQueue queued_bufs;

  GstPad *sinkpad;
  GstPad *srcpad;

  GstBuffer *cur_out_buffer;
  GstEvent *pending_gap;

} MqStreamCtx;

struct _GstMSplitMuxSink
{
  GstBin parent;

  GMutex state_lock;
  gboolean shutdown;

  GMutex lock;

  GCond input_cond;
  GCond output_cond;

  gdouble mux_overhead;

  GstClockTime threshold_time;
  guint64 threshold_bytes;
  guint max_files;
  GQueue old_files;              /* Queue of old file names for max_files management */
  gboolean send_keyframe_requests;
  gchar *threshold_timecode_str;
  /* created from threshold_timecode_str */
  GstVideoTimeCodeInterval *tc_interval;
  GstClockTime alignment_threshold;
  /* expected running time of next force keyframe unit event */
  GstClockTime next_fku_time;

  gboolean reset_muxer;

  GstElement *muxer;
  GstElement *sink;

  GstElement *provided_muxer;

  GstElement *provided_sink;
  GstElement *active_sink;

  gboolean ready_for_output;

  gchar *location;
  guint cur_fragment_id;

  guint next_fragment_id;
  guint start_index;
  GList *contexts;

  MSplitMuxInputState input_state;
  GstClockTimeDiff max_in_running_time;
  GstClockTimeDiff max_in_running_time_dts;

  /* Number of bytes sent to the
   * current fragment */
  guint64 fragment_total_bytes;
  /* Number of bytes for the reference
   * stream in this fragment */
  guint64 fragment_reference_bytes;

  /* Minimum start time (PTS or DTS) of the current fragment (reference stream, input side) */
  GstClockTimeDiff fragment_start_time;
  /* Start time (PTS) of the current fragment (reference stream, input side) */
  GstClockTimeDiff fragment_start_time_pts;
  /* Minimum start timecode of the current fragment (reference stream, input side) */
  GstVideoTimeCode *fragment_start_tc;

  /* Oldest GOP at head, newest GOP at tail */
  GQueue pending_input_gops;

  /* expected running time of next fragment in timecode mode */
  GstClockTime next_fragment_start_tc_time;

  GQueue out_cmd_q;             /* Queue of commands for output thread */

  MSplitMuxOutputState output_state;
  GstClockTimeDiff max_out_running_time;
  OutputFragmentInfo out_fragment_info;

  /* Track the earliest running time (across all inputs) for the first fragment */
  GstClockTimeDiff out_start_runts;
  /* Track the earliest running time (across all inputs) for the *current* fragment */
  GstClockTimeDiff out_fragment_start_runts;

  guint64 muxed_out_bytes;

  MqStreamCtx *reference_ctx;
  /* Count of queued keyframes in the reference ctx */
  guint queued_keyframes;

  gboolean switching_fragment;

  gboolean have_video;

  gboolean need_async_start;
  gboolean async_pending;

  gboolean use_robust_muxing;
  gboolean muxer_has_reserved_props;

  gboolean split_requested;
  gboolean do_split_next_gop;
  GstQueueArray *times_to_split;

  /* Async finalize options */
  gboolean async_finalize;
  gchar *muxer_factory;
  gchar *muxer_preset;
  GstStructure *muxer_properties;
  gchar *sink_factory;
  gchar *sink_preset;
  GstStructure *sink_properties;

  GstStructure *muxerpad_map;
};

struct _GstMSplitMuxSinkClass
{
  GstBinClass parent_class;

  /* actions */
  void     (*split_now)   (GstMSplitMuxSink * splitmux);
  void     (*split_after) (GstMSplitMuxSink * splitmux);
  void     (*split_at_running_time)   (GstMSplitMuxSink * splitmux, GstClockTime split_time);
};

GST_ELEMENT_REGISTER_DECLARE (msplitmuxsink);

G_END_DECLS
#endif /* __GST_MSPLITMUXSINK_H__ */
