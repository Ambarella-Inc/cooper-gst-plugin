/*
 * gstambahwvdecv2.c
 *
 * History:
 *    4/6/2026 - [Dashun Pei] created file
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
 * SECTION:element-gstambahwvdecv2
 * @title: gstambahwvdecv2
 *
 * Use amba hardware decoding, then query yuv from iav canvas
 * GstVideoDecoder -> video/x-raw NV12 Cavalry pool (IAV BSB + canvas + GDMA dmabuf)
 * Output: video/x-raw (NV12).
 *
 * <refsect2>
 * <title>Example launch line</title>
 * gst-launch-1.0 -e \
 *   filesrc location="$H264_PATH" ! h264parse ! queue ! \
 *   amba_hwvdecv2 dec-id=0 num-decoders=1 \
 *     dump-nv12-dir="$DUMP_DIR/dec0" dump-nv12-frame-id=0 dump-nv12-num-frames=10 \
 *     ! video/x-raw,format=NV12 ! queue ! fakesink sync=false
 *
 * Default output pool matches cooper_1: Cavalry mfd (dmabuf) for IAV GDMA.
 * Optional: cavalry-phys-alloc=true for PHY/DDS experiments (GDMA may need extra work).
 * </refsect2>
 */

#include <string.h>
#include <stdio.h>

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideodecoder.h>

#include "internal.h"
#include "gstamshmemcommonslot.h"
#include "gst_amba_cavalry_allocator.h"
#include "gst_amba_pitch_align.h"
#include "gst_amba_cavalry_bufferpool.h"
#include "gstambahwvdecv2.h"
#include "gstambahwvdecv2_iav.h"

GST_DEBUG_CATEGORY_STATIC (gst_amba_hwvdecv2_debug);
#define GST_CAT_DEFAULT gst_amba_hwvdecv2_debug

#define DEFAULT_DEC_ID 0
#define DEFAULT_SLAVE_ID 0
#define DEFAULT_NUM_DECODERS 1
#define DEFAULT_PHYS_BASE G_GUINT64_CONSTANT (0x80000000)

#define DEFAULT_DUMP_NV12_FIRST_FRAME 0
#define DEFAULT_DUMP_NV12_NUM_FRAMES 5
#define DUMP_NV12_NUM_FRAMES_MAX 256

/* Property upper bound: user asked up to 16; DSP header may define a lower cap. */
#define HWVDECV2_NUM_DECODERS_MAX \
  ((guint) ((DAMBADSP_MAX_DECODER_NUMBER) < 16 ? DAMBADSP_MAX_DECODER_NUMBER : 16))

enum
{
  PROP_0,
  PROP_DEC_ID,
  PROP_SLAVE_ID,
  PROP_NUM_DECODERS,
  PROP_PHYS_BASE,
  PROP_VERBOSE,
  PROP_DUMP_NV12_DIR,
  PROP_DUMP_NV12_FRAME_ID,
  PROP_DUMP_NV12_NUM_FRAMES,
  PROP_CAVALRY_PHYS_ALLOC,
  PROP_CONTIGUOUS_POOL,
  PROP_ALLOC_NV12_WIDTH,
  PROP_ALLOC_NV12_HEIGHT,
  PROP_LAST
};

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264, "
        "stream-format=(string) { byte-stream, avc, avc3 }, "
        "alignment=(string) { nal }"));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, format=(string)NV12, "
        "width=(int)[1,8192], height=(int)[1,8192]"));

#define gst_amba_hwvdecv2_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstAmbaHwvdecV2, gst_amba_hwvdecv2,
    GST_TYPE_VIDEO_DECODER,
    GST_DEBUG_CATEGORY_INIT (gst_amba_hwvdecv2_debug, "amba_hwvdecv2", 0,
        "Ambarella HW decoder v2 (NV12 Cavalry pool out)"));

static void gst_amba_hwvdecv2_finalize (GObject * object);
static void gst_amba_hwvdecv2_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_amba_hwvdecv2_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_amba_hwvdecv2_start (GstVideoDecoder * decoder);
static gboolean gst_amba_hwvdecv2_stop (GstVideoDecoder * decoder);
static gboolean gst_amba_hwvdecv2_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state);
static gboolean gst_amba_hwvdecv2_negotiate (GstVideoDecoder * decoder);
static gboolean gst_amba_hwvdecv2_flush (GstVideoDecoder * decoder);
static gboolean gst_amba_hwvdecv2_propose_allocation (GstVideoDecoder * decoder,
    GstQuery * query);
static gboolean gst_amba_hwvdecv2_decide_allocation (GstVideoDecoder * decoder,
    GstQuery * query);
static GstFlowReturn gst_amba_hwvdecv2_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame);
static GstBufferPool *gst_amba_hwvdecv2_create_cavalry_output_pool (GstAmbaHwvdecV2 *
    self, gsize * out_buf_size);
static gboolean gst_amba_hwvdecv2_ensure_out_pool (GstAmbaHwvdecV2 * self);
static void gst_amba_hwvdecv2_output_picture_dims (GstAmbaHwvdecV2 * self, guint * ow,
    guint * oh);
static gboolean gst_amba_hwvdecv2_try_update_src_caps_for_picture (GstVideoDecoder *
    decoder, guint pic_w, guint pic_h);

/* Byte-stream H.264 often has 0x0 in GstVideoInfo until SPS; IAV prepare fills
 * max_coded_* (see GST_AMBA_HWVDECV2_DEFAULT_CODED_*). Used for buffer height / BSB,
 * not necessarily for pad caps (see output_picture_dims). */
static void
gst_amba_hwvdecv2_effective_dims (GstAmbaHwvdecV2 * self, guint * ow, guint * oh)
{
  if (self->width >= 16 && self->height >= 16) {
    *ow = self->width;
    *oh = self->height;
    GST_DEBUG_OBJECT (self,
        "[v2] effective_dims: %ux%u from decoder state (width/height)", *ow, *oh);
    return;
  }
  if (self->max_coded_width >= 16 && self->max_coded_height >= 16) {
    *ow = self->max_coded_width;
    *oh = self->max_coded_height;
    GST_DEBUG_OBJECT (self,
        "[v2] effective_dims: %ux%u from max_coded_* (IAV prepare / caps)", *ow, *oh);
    return;
  }
  *ow = GST_AMBA_HWVDECV2_DEFAULT_CODED_WIDTH;
  *oh = GST_AMBA_HWVDECV2_DEFAULT_CODED_HEIGHT;
  GST_DEBUG_OBJECT (self,
      "[v2] effective_dims: %ux%u fallback (GST_AMBA_HWVDECV2_DEFAULT_CODED_*)", *ow,
      *oh);
}

/* Pad caps and pool GstVideoInfo width/height: logical picture size (IAV canvas),
 * not coded (SPS) WxH and not Y row pitch. Stride stays in GstVideoMeta on buffers. */
static void
gst_amba_hwvdecv2_output_picture_dims (GstAmbaHwvdecV2 * self, guint * ow, guint * oh)
{
  if (self->canvas_out_valid && self->canvas_out_w >= 16 && self->canvas_out_h >= 16) {
    *ow = self->canvas_out_w;
    *oh = self->canvas_out_h;
    GST_DEBUG_OBJECT (self,
        "[v2] output_picture_dims: %ux%u from IAV canvas (pad caps / pool WxH)", *ow,
        *oh);
    return;
  }
  gst_amba_hwvdecv2_effective_dims (self, ow, oh);
}

void
gst_amba_hwvdecv2_refresh_nv12_size (GstAmbaHwvdecV2 * self)
{
  guint w, h, pitch_w = 0, hbuf = 0, al_h;
  gboolean tight_alloc = FALSE;

  gst_amba_hwvdecv2_effective_dims (self, &w, &h);
  /* Pool NV12 bytes = nv12_y_pitch * al_h * 3/2. Priority:
   * 1) alloc-buf-width/height: picture width (e.g. 768) rounded with
   *    AMBA_ROUND_GSIZE_ODD_DSP_PITCH_UNITS -> Y stride (e.g. 896); + height.
   * 2) alloc_preset_* from IAV_IOC_QUERY_DESC in prepare (canvas WxH; width rounded).
   * 3) Live canvas_out_* after first decoded picture (same stride bump as before).
   * 4) Legacy coded / max_coded sizing (may reserve 1080p-class memory). */
  if (self->alloc_nv12_width >= 16 && self->alloc_nv12_height >= 16) {
    pitch_w =
        (guint) AMBA_ROUND_GSIZE_ODD_DSP_PITCH_UNITS ((gsize) self->alloc_nv12_width);
    hbuf = self->alloc_nv12_height;
    tight_alloc = TRUE;
  } else if (self->alloc_preset_valid && self->alloc_preset_w >= 16
      && self->alloc_preset_h >= 16) {
    pitch_w = self->alloc_preset_w;
    hbuf = self->alloc_preset_h;
    tight_alloc = TRUE;
  } else if (self->canvas_out_valid && self->canvas_out_w >= 16) {
    pitch_w = self->canvas_out_w;
    if (self->last_canvas_src_pitch > 0 && pitch_w < self->last_canvas_src_pitch) {
      GST_DEBUG_OBJECT (self,
          "[v2] refresh_nv12_size: canvas_w=%u raised to IAV line pitch %u for GDMA",
          self->canvas_out_w, self->last_canvas_src_pitch);
      pitch_w = self->last_canvas_src_pitch;
    }
    if (self->canvas_out_h >= 16)
      hbuf = self->canvas_out_h;
    else
      hbuf = h;
    tight_alloc = TRUE;
  } else {
    if (w >= 16)
      pitch_w = w;
    else if (self->max_coded_width >= 16)
      pitch_w = self->max_coded_width;
    else
      pitch_w = GST_AMBA_HWVDECV2_DEFAULT_CODED_WIDTH;
    {
      guint tight = G_MAXUINT;

      if (self->canvas_cap_valid && self->canvas_cap_w >= 16)
        tight = MIN (tight, self->canvas_cap_w);
      if (self->canvas_out_valid && self->canvas_out_w >= 16)
        tight = MIN (tight, self->canvas_out_w);
      if (tight != G_MAXUINT && tight < pitch_w)
        pitch_w = tight;
    }
    {
      guint floor_pw = 0;

      if (w >= 16)
        floor_pw = w;
      if (self->max_coded_width >= 16 && self->max_coded_width > floor_pw)
        floor_pw = self->max_coded_width;
      if (floor_pw >= 16 && pitch_w < floor_pw) {
        GST_DEBUG_OBJECT (self,
            "[v2] refresh_nv12_size: clamp pitch_w %u -> %u (canvas cap/out tight "
            "ignored vs coded width)",
            pitch_w, floor_pw);
        pitch_w = floor_pw;
      }
    }
    hbuf = h;
    if (self->max_coded_height >= 16 && self->max_coded_height > hbuf)
      hbuf = self->max_coded_height;
  }

  if (tight_alloc && self->last_canvas_src_pitch > 0
      && pitch_w < self->last_canvas_src_pitch) {
    GST_DEBUG_OBJECT (self,
        "[v2] refresh_nv12_size: tight alloc pitch_w=%u raised to src pitch %u",
        pitch_w, self->last_canvas_src_pitch);
    pitch_w = self->last_canvas_src_pitch;
  }

  self->nv12_y_pitch = AMBA_ROUND_GSIZE_ODD_DSP_PITCH_UNITS (pitch_w);
  al_h = (hbuf + 15) & ~15u;
  self->nv12_buffer_size =
      (gsize) self->nv12_y_pitch * (gsize) al_h * 3 / 2;
  GST_DEBUG_OBJECT (self,
      "[v2] refresh_nv12_size: %" G_GUINT32_FORMAT "x%" G_GUINT32_FORMAT
      " pitch_w=%u nv12_y_pitch=%u hbuf=%u al_h=%u canvas_out=%d preset=%d props=%d "
      "tight=%d src_pitch=%u alloc_buf_w=%u -> nv12_buffer_size=%" G_GSIZE_FORMAT,
      w, h, pitch_w, self->nv12_y_pitch, hbuf, al_h, self->canvas_out_valid ? 1 : 0,
      self->alloc_preset_valid ? 1 : 0,
      (self->alloc_nv12_width >= 16 && self->alloc_nv12_height >= 16) ? 1 : 0,
      tight_alloc ? 1 : 0, self->last_canvas_src_pitch,
      (self->alloc_nv12_width >= 16 && self->alloc_nv12_height >= 16)
          ? self->alloc_nv12_width : 0u,
      self->nv12_buffer_size);
}

static void
gst_amba_hwvdecv2_maybe_dump_nv12 (GstAmbaHwvdecV2 * self, GstBuffer * buf,
    guint out_w, guint out_h, guint pitch, guint32 frame_id)
{
  GstMapInfo map;
  FILE *fp;
  gchar *path;
  guint al_h;
  gsize y_size;
  guint row;

  if (!self->dump_nv12_dir || !self->dump_nv12_dir[0])
    return;
  if (self->dump_nv12_frame_id < 0 || self->dump_nv12_num_frames == 0)
    return;
  {
    guint64 lo = (guint64) (gint64) self->dump_nv12_frame_id;
    guint64 hi = lo + (guint64) self->dump_nv12_num_frames;

    if ((guint64) frame_id < lo || (guint64) frame_id >= hi)
      return;
  }
  if (!g_file_test (self->dump_nv12_dir, G_FILE_TEST_IS_DIR)) {
    GST_WARNING_OBJECT (self, "dump-nv12-dir is not a directory");
    return;
  }
  if (!gst_buffer_map (buf, &map, GST_MAP_READ)) {
    GST_WARNING_OBJECT (self, "dump-nv12: map failed");
    return;
  }

  al_h = (out_h + 15) & ~15u;
  y_size = (gsize) pitch * (gsize) al_h;
  path = g_strdup_printf ("%s" G_DIR_SEPARATOR_S "hwvdecv2_f%u.nv12",
      self->dump_nv12_dir, frame_id);
  fp = fopen (path, "wb");
  if (!fp) {
    GST_WARNING_OBJECT (self, "dump-nv12: fopen %s failed", path);
    g_free (path);
    gst_buffer_unmap (buf, &map);
    return;
  }

  for (row = 0; row < out_h; row++)
    fwrite (map.data + (gsize) row * pitch, 1, out_w, fp);
  for (row = 0; row < out_h / 2; row++)
    fwrite (map.data + y_size + (gsize) row * pitch, 1, out_w, fp);

  fclose (fp);
  gst_buffer_unmap (buf, &map);
  GST_INFO_OBJECT (self, "dump-nv12: frame_id=%" G_GUINT32_FORMAT " -> %s",
      frame_id, path);
  g_free (path);
}

static void
format_from_caps (GstAmbaHwvdecV2 * self, GstCaps * caps, StreamFormat * format)
{
  (void) self;
  *format = StreamFormat_Invalid;
  if (!caps || !gst_caps_is_fixed (caps) || gst_caps_get_size (caps) < 1)
    return;

  {
    GstStructure *s = gst_caps_get_structure (caps, 0);
    const gchar *str = gst_structure_get_string (s, "stream-format");

    if (str) {
      if (strcmp (str, "avc") == 0 || strcmp (str, "avc3") == 0)
        *format = StreamFormat_H264;
      else if (strcmp (str, "byte-stream") == 0)
        *format = StreamFormat_BYTE;
    }
    if (gst_structure_has_name (s, "video/x-h264") && *format == StreamFormat_BYTE)
      *format = StreamFormat_H264_BYTE;
  }
}

static guint
gst_amba_hwvdecv2_assign_slot (GstBuffer * out_buf, gpointer user_data)
{
  GstAmbaHwvdecV2 *self = GST_AMBA_HWVDECV2 (user_data);

  return gst_amshmem_buffer_get_or_assign_slot (out_buf, &self->pool_slot_counter);
}

static void
gst_amba_hwvdecv2_class_init (GstAmbaHwvdecV2Class * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoDecoderClass *vdec_class = GST_VIDEO_DECODER_CLASS (klass);

  gobject_class->finalize = gst_amba_hwvdecv2_finalize;
  gobject_class->set_property = gst_amba_hwvdecv2_set_property;
  gobject_class->get_property = gst_amba_hwvdecv2_get_property;

  g_object_class_install_property (gobject_class, PROP_DEC_ID,
      g_param_spec_uint ("dec-id", "Decoder ID", "AmShMem_Msg.dec_id / IAV decoder index", 0, 255,
          DEFAULT_DEC_ID, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_SLAVE_ID,
      g_param_spec_uint ("slave-id", "Slave ID", "AmShMem_Msg.slave_id", 0, 255,
          DEFAULT_SLAVE_ID, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_NUM_DECODERS,
      g_param_spec_uint ("num-decoders", "Number of decoders",
          "Logical decode channels passed to IAV enter-decode-mode for this process "
          "(same value on every amba_hwvdecv2; first prepare calls f_enter_mode once). "
          "The driver may advertise a higher maximum (e.g. dmesg num_decoder=16 for 16 "
          "decoder IDs / canvases); using num-decoders=1 with dec-id=0 is valid whenever "
          "num-decoders is within that driver limit.", 1, HWVDECV2_NUM_DECODERS_MAX,
          DEFAULT_NUM_DECODERS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PHYS_BASE,
      g_param_spec_uint64 ("phys-base", "Physical base",
          "Deprecated (no longer used for GDMA). Kept for API compatibility.", 0,
          G_MAXUINT64, DEFAULT_PHYS_BASE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_VERBOSE,
      g_param_spec_boolean ("verbose", "Verbose", "Extra logging", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_DUMP_NV12_DIR,
      g_param_spec_string ("dump-nv12-dir", "Dump NV12 directory",
          "If set, map output buffer and write NV12 files for a range of frame ids "
          "(see dump-nv12-frame-id, dump-nv12-num-frames)",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_DUMP_NV12_FRAME_ID,
      g_param_spec_int ("dump-nv12-frame-id", "First dump NV12 frame id",
          "First frame id (inclusive) to dump; -1 disables dump. Default with dir set: "
          "dump frames [id, id+num) e.g. 0..4 when num-frames=5",
          -1, G_MAXINT, DEFAULT_DUMP_NV12_FIRST_FRAME,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_DUMP_NV12_NUM_FRAMES,
      g_param_spec_uint ("dump-nv12-num-frames", "Number of NV12 frames to dump",
          "Consecutive frame count starting at dump-nv12-frame-id; 0 disables", 0,
          DUMP_NV12_NUM_FRAMES_MAX, DEFAULT_DUMP_NV12_NUM_FRAMES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_CAVALRY_PHYS_ALLOC,
      g_param_spec_boolean ("cavalry-phys-alloc", "Cavalry phys allocator",
          "FALSE (default): same as cooper_1 — Cavalry mfd pool + IAV GDMA into dmabuf. "
          "TRUE: phys/PA pool for PHY/DDS experiments; current GDMA path still expects "
          "a dmabuf fd on output buffers unless extended.",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_CONTIGUOUS_POOL,
      g_param_spec_boolean ("alloc-contiguous-pool", "Contiguous Cavalry output pool",
          "TRUE (default): one CMA/Cavalry slab at pool start (max_buffers * aligned slot). "
          "FALSE: per-buffer allocator alloc (smaller single allocations; may help when "
          "shared Cavalry memory is fragmented).",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_ALLOC_NV12_WIDTH,
      g_param_spec_uint ("alloc-buf-width", "Alloc NV12 picture width (pixels)",
          "If set with alloc-buf-height (both >= 16), pool Y stride is "
          "AMBA_ROUND_GSIZE_ODD_DSP_PITCH_UNITS(width) (e.g. 768 -> 896). "
          "If IAV canvas line pitch (after first picture) is larger, it still wins. "
          "0 = use prepare-time canvas probe or live canvas/coded heuristics.",
          0, 8192, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_ALLOC_NV12_HEIGHT,
      g_param_spec_uint ("alloc-buf-height", "Alloc NV12 picture height",
          "If set with alloc-buf-width (both >= 16), NV12 pool height uses this value "
          "(16-pixel aligned internally) instead of max_coded_height. 0 = auto.",
          0, 8192, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);
  gst_element_class_set_static_metadata (element_class,
        "Ambarella hardware video decoder v2 (query canvas NV12)",
      "Codec/Decoder/Video",
      "Decodes H.264 via IAV; GDMA canvas YUV into Cavalry NV12 pool (16 buffers)",
      "Dashun Pei <<dspei@ambarella.com>>");


  vdec_class->start = GST_DEBUG_FUNCPTR (gst_amba_hwvdecv2_start);
  vdec_class->stop = GST_DEBUG_FUNCPTR (gst_amba_hwvdecv2_stop);
  vdec_class->set_format = GST_DEBUG_FUNCPTR (gst_amba_hwvdecv2_set_format);
  vdec_class->negotiate = GST_DEBUG_FUNCPTR (gst_amba_hwvdecv2_negotiate);
  vdec_class->flush = GST_DEBUG_FUNCPTR (gst_amba_hwvdecv2_flush);
  vdec_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_amba_hwvdecv2_propose_allocation);
  vdec_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_amba_hwvdecv2_decide_allocation);
  vdec_class->handle_frame = GST_DEBUG_FUNCPTR (gst_amba_hwvdecv2_handle_frame);
}

static void
gst_amba_hwvdecv2_init (GstAmbaHwvdecV2 * self)
{
  gst_video_decoder_set_packetized (GST_VIDEO_DECODER (self), TRUE);
  gst_video_decoder_set_needs_format (GST_VIDEO_DECODER (self), TRUE);

  self->dec_id = (guint8) DEFAULT_DEC_ID;
  self->slave_id = (guint8) DEFAULT_SLAVE_ID;
  self->num_decoders = DEFAULT_NUM_DECODERS;
  self->cavalry_phys_alloc = FALSE;
  self->phys_base = DEFAULT_PHYS_BASE;
  self->contiguous_pool = TRUE;
  self->alloc_nv12_width = 0;
  self->alloc_nv12_height = 0;
  self->alloc_preset_valid = FALSE;
  self->alloc_preset_w = 0;
  self->alloc_preset_h = 0;
  self->verbose = FALSE;

  self->codec_format = StreamFormat_H264_BYTE;
  self->nal_length_size = 4;
  self->mbAddAmbaGopHeader = TRUE;
  self->mCurGopSize = 0;
  self->mSpecifiedTimeScale = 0;
  self->mSpecifiedFrameTick = 0;
  self->iav_ctx_acquired = FALSE;
  self->iav_pipeline_ready = FALSE;
  self->nv12_buffer_size = 0;
  self->dump_nv12_dir = NULL;
  self->dump_nv12_frame_id = DEFAULT_DUMP_NV12_FIRST_FRAME;
  self->dump_nv12_num_frames = DEFAULT_DUMP_NV12_NUM_FRAMES;

  self->canvas_cap_valid = FALSE;
  self->canvas_out_valid = FALSE;
  self->last_canvas_src_pitch = 0;
}

static void
gst_amba_hwvdecv2_finalize (GObject * object)
{
  GstAmbaHwvdecV2 *self = GST_AMBA_HWVDECV2 (object);

  if (self->out_pool) {
    gst_buffer_pool_set_active (self->out_pool, FALSE);
    gst_object_unref (self->out_pool);
    self->out_pool = NULL;
  }

  gst_ambahwvdecv2_iav_shutdown_decoder (self);
  gst_ambahwvdecv2_iav_release_ctx (self);

  g_free (self->dump_nv12_dir);
  self->dump_nv12_dir = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_amba_hwvdecv2_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAmbaHwvdecV2 *self = GST_AMBA_HWVDECV2 (object);

  switch (prop_id) {
    case PROP_DEC_ID:
      self->dec_id = (guint8) g_value_get_uint (value);
      break;
    case PROP_SLAVE_ID:
      self->slave_id = (guint8) g_value_get_uint (value);
      break;
    case PROP_NUM_DECODERS:
      self->num_decoders = g_value_get_uint (value);
      break;
    case PROP_PHYS_BASE:
      self->phys_base = g_value_get_uint64 (value);
      break;
    case PROP_VERBOSE:
      self->verbose = g_value_get_boolean (value);
      break;
    case PROP_DUMP_NV12_DIR: {
      const gchar *s = g_value_get_string (value);

      g_free (self->dump_nv12_dir);
      self->dump_nv12_dir = g_strdup (s);
      break;
    }
    case PROP_DUMP_NV12_FRAME_ID:
      self->dump_nv12_frame_id = g_value_get_int (value);
      break;
    case PROP_DUMP_NV12_NUM_FRAMES:
      self->dump_nv12_num_frames = g_value_get_uint (value);
      break;
    case PROP_CAVALRY_PHYS_ALLOC:
      self->cavalry_phys_alloc = g_value_get_boolean (value);
      break;
    case PROP_CONTIGUOUS_POOL:
      self->contiguous_pool = g_value_get_boolean (value);
      break;
    case PROP_ALLOC_NV12_WIDTH:
      self->alloc_nv12_width = g_value_get_uint (value);
      break;
    case PROP_ALLOC_NV12_HEIGHT:
      self->alloc_nv12_height = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_amba_hwvdecv2_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstAmbaHwvdecV2 *self = GST_AMBA_HWVDECV2 (object);

  switch (prop_id) {
    case PROP_DEC_ID:
      g_value_set_uint (value, self->dec_id);
      break;
    case PROP_SLAVE_ID:
      g_value_set_uint (value, self->slave_id);
      break;
    case PROP_NUM_DECODERS:
      g_value_set_uint (value, self->num_decoders);
      break;
    case PROP_PHYS_BASE:
      g_value_set_uint64 (value, self->phys_base);
      break;
    case PROP_VERBOSE:
      g_value_set_boolean (value, self->verbose);
      break;
    case PROP_DUMP_NV12_DIR:
      g_value_set_string (value, self->dump_nv12_dir);
      break;
    case PROP_DUMP_NV12_FRAME_ID:
      g_value_set_int (value, self->dump_nv12_frame_id);
      break;
    case PROP_DUMP_NV12_NUM_FRAMES:
      g_value_set_uint (value, self->dump_nv12_num_frames);
      break;
    case PROP_CAVALRY_PHYS_ALLOC:
      g_value_set_boolean (value, self->cavalry_phys_alloc);
      break;
    case PROP_CONTIGUOUS_POOL:
      g_value_set_boolean (value, self->contiguous_pool);
      break;
    case PROP_ALLOC_NV12_WIDTH:
      g_value_set_uint (value, self->alloc_nv12_width);
      break;
    case PROP_ALLOC_NV12_HEIGHT:
      g_value_set_uint (value, self->alloc_nv12_height);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_amba_hwvdecv2_start (GstVideoDecoder * decoder)
{
  GstAmbaHwvdecV2 *self = GST_AMBA_HWVDECV2 (decoder);

  GST_DEBUG_OBJECT (self,
      "[v2] start: enter (dec-id=%u num-decoders=%u)", (guint) self->dec_id,
      self->num_decoders);

  /* Do not chain GstVideoDecoderClass::start — same as gst_amba_hwvdec_start (v1).
   * Default gst_video_decoder_start() has crashed here on target (SIGSEGV right
   * after entering start); v1 never calls it and relies on base state as-is. */
  GST_DEBUG_OBJECT (self, "[v2] start: skip parent VideoDecoder::start (v1-style)");

  if (!gst_ambahwvdecv2_iav_init_ctx (self)) {
    GST_DEBUG_OBJECT (self, "[v2] start: IAV ctx init failed");
    return FALSE;
  }

  GST_DEBUG_OBJECT (self, "[v2] start: IAV ctx ok (iav_fd=%d)",
      self->iav_ctx ? self->iav_ctx->iav_fd : -1);

  self->frame_id_seq = 0;
  self->pool_slot_counter = 0;

  GST_DEBUG_OBJECT (self, "[v2] start: done");
  return TRUE;
}

static gboolean
gst_amba_hwvdecv2_stop (GstVideoDecoder * decoder)
{
  GstAmbaHwvdecV2 *self = GST_AMBA_HWVDECV2 (decoder);

  GST_DEBUG_OBJECT (self, "[v2] stop: enter");
  gst_ambahwvdecv2_iav_shutdown_decoder (self);

  if (self->out_pool) {
    gst_buffer_pool_set_active (self->out_pool, FALSE);
    gst_object_unref (self->out_pool);
    self->out_pool = NULL;
  }

  /* Match v1: gst_amba_hwvdec_stop does not chain parent VideoDecoder::stop. */
  GST_DEBUG_OBJECT (self, "[v2] stop: done (skip parent VideoDecoder::stop)");
  return TRUE;
}

static gboolean
gst_amba_hwvdecv2_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state)
{
  GstAmbaHwvdecV2 *self = GST_AMBA_HWVDECV2 (decoder);
  StreamFormat format = StreamFormat_Invalid;

  GST_DEBUG_OBJECT (self, "[v2] set_format: enter");

  self->width = GST_VIDEO_INFO_WIDTH (&state->info);
  self->height = GST_VIDEO_INFO_HEIGHT (&state->info);

  if (state->caps)
    format_from_caps (self, state->caps, &format);

  if (format == StreamFormat_Invalid) {
    if (state->codec_data)
      format = StreamFormat_H264;
    else
      format = StreamFormat_H264_BYTE;
  }
  self->codec_format = format;

  if (format == StreamFormat_H264 && state->codec_data) {
    GstMapInfo cmap;
    if (gst_buffer_map (state->codec_data, &cmap, GST_MAP_READ)) {
      if (cmap.size >= 5)
        self->nal_length_size = (guint) ((cmap.data[4] & 0x03) + 1);
      else
        self->nal_length_size = 4;
      gst_buffer_unmap (state->codec_data, &cmap);
    } else {
      self->nal_length_size = 4;
    }
  } else {
    self->nal_length_size = 4;
  }

  self->mFrameRateNum = 0;
  self->mFrameRateDen = 0;
  if (state->caps && gst_caps_is_fixed (state->caps)
      && gst_caps_get_size (state->caps) > 0) {
    GstStructure *s = gst_caps_get_structure (state->caps, 0);
    const GValue *fps_value = gst_structure_get_value (s, "framerate");

    if (fps_value && GST_VALUE_HOLDS_FRACTION (fps_value)) {
      gint n = gst_value_get_fraction_numerator (fps_value);
      gint d = gst_value_get_fraction_denominator (fps_value);
      if (n > 0 && d > 0) {
        self->mFrameRateNum = (guint) n;
        self->mFrameRateDen = (guint) d;
      }
    }
  }
  if (GST_VIDEO_INFO_FPS_N (&state->info) > 0
      && GST_VIDEO_INFO_FPS_D (&state->info) > 0) {
    if (self->mFrameRateNum == 0 || self->mFrameRateDen == 0) {
      self->mFrameRateNum = GST_VIDEO_INFO_FPS_N (&state->info);
      self->mFrameRateDen = GST_VIDEO_INFO_FPS_D (&state->info);
    }
  }
  self->mSpecifiedTimeScale = self->mFrameRateNum ? self->mFrameRateNum : 30;
  self->mSpecifiedFrameTick = self->mFrameRateDen ? self->mFrameRateDen : 1;

  GST_DEBUG_OBJECT (self,
      "[v2] set_format: caps wxh=%ux%u dec-id=%u num-decoders=%u codec_format=%d "
      "cavalry-phys-alloc=%d",
      self->width, self->height, (guint) self->dec_id, self->num_decoders,
      (int) self->codec_format, self->cavalry_phys_alloc ? 1 : 0);

  GST_DEBUG_OBJECT (self, "[v2] phase set_format/iav_prepare begin");
  if (!gst_ambahwvdecv2_iav_prepare (self, state)) {
    GST_ERROR_OBJECT (self, "IAV prepare failed");
    return FALSE;
  }
  GST_DEBUG_OBJECT (self, "[v2] phase set_format/iav_prepare done");

  gst_amba_hwvdecv2_refresh_nv12_size (self);
  GST_DEBUG_OBJECT (self,
      "[v2] set_format: after prepare max_coded=%ux%u nv12_buffer_size=%" G_GSIZE_FORMAT,
      self->max_coded_width, self->max_coded_height, self->nv12_buffer_size);

  GST_DEBUG_OBJECT (self, "[v2] phase set_format/src_negotiate begin");
  if (!gst_amba_hwvdecv2_negotiate (decoder)) {
    GST_ERROR_OBJECT (self, "downstream rejected NV12 caps");
    return FALSE;
  }

  GST_DEBUG_OBJECT (self, "[v2] phase set_format/src_negotiate done");
  GST_DEBUG_OBJECT (self, "[v2] set_format: done (src caps set)");
  return TRUE;
}

static gboolean
gst_amba_hwvdecv2_try_update_src_caps_for_picture (GstVideoDecoder * decoder,
    guint pic_w, guint pic_h)
{
  GstAmbaHwvdecV2 *self = GST_AMBA_HWVDECV2 (decoder);
  GstPad *pad = GST_VIDEO_DECODER_SRC_PAD (decoder);
  GstCaps *cur;
  gboolean need_renego = FALSE;

  if (pic_w < 16 || pic_h < 16)
    return TRUE;

  cur = gst_pad_get_current_caps (pad);
  if (!cur) {
    need_renego = TRUE;
  } else {
    GstStructure *st = gst_caps_get_structure (cur, 0);
    gint cw = 0, ch = 0;

    if (!st || !gst_structure_get_int (st, "width", &cw)
        || !gst_structure_get_int (st, "height", &ch)) {
      need_renego = TRUE;
    } else if ((guint) cw != pic_w || (guint) ch != pic_h) {
      need_renego = TRUE;
    }
    gst_caps_unref (cur);
  }

  if (!need_renego)
    return TRUE;

  GST_DEBUG_OBJECT (self,
      "[v2] src caps -> picture %ux%u (stride/pitch only on GstVideoMeta)", pic_w,
      pic_h);

  if (!gst_video_decoder_negotiate (decoder)) {
    GST_WARNING_OBJECT (self,
        "[v2] gst_video_decoder_negotiate failed for picture %ux%u", pic_w, pic_h);
    return FALSE;
  }
  gst_pad_push_event (pad, gst_event_new_reconfigure ());
  return TRUE;
}

static gboolean
gst_amba_hwvdecv2_negotiate (GstVideoDecoder * decoder)
{
  GstAmbaHwvdecV2 *self = GST_AMBA_HWVDECV2 (decoder);
  GstPad *pad = GST_VIDEO_DECODER_SRC_PAD (decoder);
  GstVideoInfo vi;
  GstCaps *caps;
  gboolean ret;
  guint w, h;

  gst_amba_hwvdecv2_output_picture_dims (self, &w, &h);
  gst_video_info_init (&vi);
  gst_video_info_set_format (&vi, GST_VIDEO_FORMAT_NV12, w, h);
  if (self->mFrameRateNum > 0 && self->mFrameRateDen > 0) {
    vi.fps_n = (gint) self->mFrameRateNum;
    vi.fps_d = (gint) self->mFrameRateDen;
  }
  caps = gst_video_info_to_caps (&vi);
  if (!caps)
    return FALSE;

  GST_DEBUG_OBJECT (self,
      "[v2] negotiate: NV12 %" G_GUINT32_FORMAT "x%" G_GUINT32_FORMAT
      " (pad WxH = picture; row stride in GstVideoMeta)", w, h);
  ret = gst_pad_set_caps (pad, caps);
  gst_caps_unref (caps);
  GST_DEBUG_OBJECT (self, "[v2] negotiate: gst_pad_set_caps -> %d", ret);
  return ret;
}

static gboolean
gst_amba_hwvdecv2_flush (GstVideoDecoder * decoder)
{
  GstAmbaHwvdecV2 *self = GST_AMBA_HWVDECV2 (decoder);

  self->pool_slot_counter = 0;
  self->canvas_out_valid = FALSE;
  self->last_canvas_src_pitch = 0;
  gst_ambahwvdecv2_iav_flush_decoder (self);

  return GST_VIDEO_DECODER_CLASS (parent_class)->flush (decoder);
}

/* Shared by propose_allocation and decide_allocation fallback. Sinks like
 * fakesink often drop the proposed pool from the allocation query answer. */
static GstBufferPool *
gst_amba_hwvdecv2_create_cavalry_output_pool (GstAmbaHwvdecV2 * self,
    gsize * out_buf_size)
{
  GstCaps *pool_caps;
  GstBufferPool *pool;
  GstStructure *config;
  gsize size;
  GstAllocator *alloc;
  GstVideoInfo vi;
  guint w, h;

  if (out_buf_size)
    *out_buf_size = 0;

  gst_amba_hwvdecv2_output_picture_dims (self, &w, &h);
  if (self->nv12_buffer_size == 0)
    gst_amba_hwvdecv2_refresh_nv12_size (self);
  size = self->nv12_buffer_size;
  gst_video_info_init (&vi);
  gst_video_info_set_format (&vi, GST_VIDEO_FORMAT_NV12, w, h);
  pool_caps = gst_video_info_to_caps (&vi);
  if (!pool_caps)
    return NULL;

  pool = gst_amba_cavalry_buffer_pool_new ();
  gst_amba_cavalry_buffer_pool_set_contiguous_memory (
      GST_AMBA_CAVALRY_BUFFER_POOL (pool), self->contiguous_pool);

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, pool_caps, size,
      GST_AMSHMEM_POOL_MAX_BUFFERS, GST_AMSHMEM_POOL_MAX_BUFFERS);
  gst_caps_unref (pool_caps);

  gst_amba_cavalry_allocator_init_once ();
  if (self->cavalry_phys_alloc) {
    gst_amba_cavalry_phys_allocator_init_once ();
    alloc = gst_amba_cavalry_phys_allocator_get ();
    if (!alloc) {
      GST_ERROR_OBJECT (self, "gst_amba_cavalry_phys_allocator_get failed");
      gst_object_unref (pool);
      return NULL;
    }
  } else {
    alloc = gst_amba_cavalry_allocator_get ();
    if (!alloc) {
      GST_ERROR_OBJECT (self, "gst_amba_cavalry_allocator_get failed");
      gst_object_unref (pool);
      return NULL;
    }
  }
  gst_buffer_pool_config_set_allocator (config, alloc, NULL);
  gst_object_unref (alloc);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (self, "cavalry buffer pool set_config failed");
    gst_object_unref (pool);
    return NULL;
  }

  GST_DEBUG_OBJECT (self,
      "[v2] phase pool/create allocator=%s",
      self->cavalry_phys_alloc ? "amba_cavalry_phys(PA)" : "amba_cavalry(mfd)");

  GST_DEBUG_OBJECT (self,
      "[v2] create_cavalry_output_pool: contiguous=%d eff_NV12=%" G_GUINT32_FORMAT
      "x%" G_GUINT32_FORMAT " gst_video_info_size=%" G_GSIZE_FORMAT
      " pool_buffers=%d (%s)",
      self->contiguous_pool ? 1 : 0, w, h, size,
      GST_AMSHMEM_POOL_MAX_BUFFERS,
      self->contiguous_pool
          ? "start: one cma slab ~ max_buffers*aligned_slot"
          : "per-buffer Cavalry alloc in pool (no contiguous slab)");

  if (out_buf_size)
    *out_buf_size = size;
  return pool;
}

/* GstVideoDecoder may not call decide_allocation before the first handle_frame
 * (e.g. queue ! fakesink); acquisition must still work. */
static gboolean
gst_amba_hwvdecv2_ensure_out_pool (GstAmbaHwvdecV2 * self)
{
  gsize psize = 0;

  if (self->out_pool)
    return TRUE;

  if (self->nv12_buffer_size == 0)
    gst_amba_hwvdecv2_refresh_nv12_size (self);

  self->out_pool = gst_amba_hwvdecv2_create_cavalry_output_pool (self, &psize);
  if (!self->out_pool) {
    GST_ERROR_OBJECT (self, "failed to create Cavalry output pool");
    return FALSE;
  }
  GST_DEBUG_OBJECT (self,
      "ensure_out_pool: created Cavalry pool (size %" G_GSIZE_FORMAT ")", psize);
  return TRUE;
}

static gboolean
gst_amba_hwvdecv2_propose_allocation (GstVideoDecoder * decoder,
    GstQuery * query)
{
  GstAmbaHwvdecV2 *self = GST_AMBA_HWVDECV2 (decoder);
  GstCaps *qcaps = NULL;
  GstBufferPool *pool;
  gsize size;

  GST_DEBUG_OBJECT (self, "[v2] phase propose_allocation begin");
  gst_amba_hwvdecv2_refresh_nv12_size (self);

  // parse the query to get the caps and the allocation pools
  gst_query_parse_allocation (query, &qcaps, NULL);
  if (qcaps) {
    gst_caps_unref (qcaps);
  }

  // create a new Cavalry output pool, so that the downstream can use it
  pool = gst_amba_hwvdecv2_create_cavalry_output_pool (self, &size);
  if (!pool || size == 0) {
    return FALSE;
  }

  // add the Cavalry output pool to the query, so that the downstream can use it
  gst_query_add_allocation_pool (query, pool, size, GST_AMSHMEM_POOL_MAX_BUFFERS,
      GST_AMSHMEM_POOL_MAX_BUFFERS);
  gst_object_unref (pool);

  return TRUE;
}

static gboolean
gst_amba_hwvdecv2_decide_allocation (GstVideoDecoder * decoder, GstQuery * query)
{
  GstAmbaHwvdecV2 *self = GST_AMBA_HWVDECV2 (decoder);
  guint i, n;

  gst_amba_hwvdecv2_refresh_nv12_size (self);

  // release the existing output pool
  if (self->out_pool) {
    gst_buffer_pool_set_active (self->out_pool, FALSE);
    gst_object_unref (self->out_pool);
    self->out_pool = NULL;
  }

  // query the number of GstBUfferPool offered by downstream
  n = gst_query_get_n_allocation_pools (query);
  GST_DEBUG_OBJECT (self,
      "[v2] decide_allocation: query has %u pool(s), expect psize=%" G_GSIZE_FORMAT
      " for Cavalry match", n, self->nv12_buffer_size);
  for (i = 0; i < n; i++) {
    GstBufferPool *pool = NULL;
    guint psize = 0, min = 0, max = 0;

    //get the info of the i-th GstBufferPool offered by downstream
    gst_query_parse_nth_allocation_pool (query, i, &pool, &psize, &min, &max);
    // check if the GstBufferPool is a Cavalry buffer pool and the size is the same as the nv12 buffer size
    if (pool && GST_IS_AMBA_CAVALRY_BUFFER_POOL (pool)
        && psize == self->nv12_buffer_size) {
      GST_DEBUG_OBJECT (self,
          "[v2] decide_allocation: using pool[%u] from query (Cavalry, psize=%u)", i,
          psize);
      self->out_pool = gst_object_ref (pool);
      gst_object_unref (pool);
      break;
    }
    if (pool) {
      GST_DEBUG_OBJECT (self,
          "[v2] decide_allocation: skip pool[%u] (cavalry=%d psize=%u want %"
          G_GSIZE_FORMAT ")", i, pool ? GST_IS_AMBA_CAVALRY_BUFFER_POOL (pool) : 0,
          psize, self->nv12_buffer_size);
    }
    // gst_query_parse_nth_allocation_pool will inrelease reference-counted
    if (pool) {
      gst_object_unref (pool);
    }
  }

  // if the output pool is not set, create a new one
  if (!self->out_pool) {
    if (!gst_amba_hwvdecv2_ensure_out_pool (self)) {
      return FALSE;
    }
    GST_DEBUG_OBJECT (self,
        "decide_allocation: using locally created Cavalry pool (e.g. fakesink dropped proposed pool)");
  }

  return TRUE;
}

static GstFlowReturn
gst_amba_hwvdecv2_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame)
{
  GstAmbaHwvdecV2 *self = GST_AMBA_HWVDECV2 (decoder);
  GstBuffer *outbuf = NULL;
  GstFlowReturn fret;
  GstFlowReturn pushret;
  GstBuffer *in_buf = frame->input_buffer;
  gboolean produced = FALSE;
  guint ow = 0, oh = 0, op = 0, oslot = 0;
  gsize nv12_sz;
  guint32 frame_id_before;
  gsize off[GST_VIDEO_MAX_PLANES] = { 0, };
  gint str[GST_VIDEO_MAX_PLANES] = { 0, };
  guint al_h;

  // skip null frame
  if (!in_buf) {
    gst_video_decoder_drop_frame (decoder, frame);
    return GST_FLOW_OK;
  }

  gst_amba_hwvdecv2_refresh_nv12_size (self);
  if (self->out_pool) {
    GstStructure *cfg = gst_buffer_pool_get_config (self->out_pool);
    guint pool_buf_size = 0;

    if (cfg) {
      gst_buffer_pool_config_get_params (cfg, NULL, &pool_buf_size, NULL, NULL);
      gst_structure_free (cfg);
    }
    if ((gsize) pool_buf_size != self->nv12_buffer_size) {
      GST_DEBUG_OBJECT (self,
          "[v2] handle_frame: pool buffer size %u != nv12_buffer_size %" G_GSIZE_FORMAT
          ", dropping pool", pool_buf_size, self->nv12_buffer_size);
      gst_buffer_pool_set_active (self->out_pool, FALSE);
      gst_object_unref (self->out_pool);
      self->out_pool = NULL;
    }
  }

  if (!gst_amba_hwvdecv2_ensure_out_pool (self)) {
    gst_video_decoder_drop_frame (decoder, frame);
    return GST_FLOW_ERROR;
  }

  GST_DEBUG_OBJECT (self,
      "[v2] handle_frame: out_pool active=%d nv12_buffer_size=%" G_GSIZE_FORMAT
      " (set_active TRUE -> pool start / buffer alloc per contiguous=%d)",
      gst_buffer_pool_is_active (self->out_pool) ? 1 : 0, self->nv12_buffer_size,
      self->contiguous_pool ? 1 : 0);

  GST_DEBUG_OBJECT (self, "[v2] phase handle_frame/acquire begin");
  if (!gst_buffer_pool_is_active (self->out_pool) &&
      !gst_buffer_pool_set_active (self->out_pool, TRUE)) {
    GST_ERROR_OBJECT (self, "failed to activate buffer pool");
    gst_video_decoder_drop_frame (decoder, frame);
    return GST_FLOW_ERROR;
  }

  fret = gst_buffer_pool_acquire_buffer (self->out_pool, &outbuf, NULL);
  if (fret != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (self, "acquire_buffer: %s", gst_flow_get_name (fret));
    gst_video_decoder_drop_frame (decoder, frame);
    return fret;
  }

  nv12_sz = gst_buffer_get_size (outbuf);
  frame_id_before = self->frame_id_seq;

  if (gst_buffer_n_memory (outbuf) > 0) {
    GstMemory *m0 = gst_buffer_peek_memory (outbuf, 0);
    guint64 slab = gst_amba_cavalry_buffer_get_slab_phys (outbuf);
    guint64 pa_mem = gst_amba_cavalry_memory_get_phys_base (m0);

    GST_DEBUG_OBJECT (self,
        "[v2] phase handle_frame/acquired outbuf_sz=%" G_GSIZE_FORMAT
        " mem0_PA=0x%" G_GINT64_MODIFIER "x meta_slab_phys=0x%" G_GINT64_MODIFIER "x",
        nv12_sz, (gint64) pa_mem, (gint64) slab);
  }

  GST_DEBUG_OBJECT (self,
      "[v2] phase handle_frame/iav_fill begin (in_sz=%" G_GSIZE_FORMAT ")",
      gst_buffer_get_size (in_buf));

  // BSB hwdec + GDMA to outbuf
  fret = gst_ambahwvdecv2_iav_fill_output (self, decoder, frame, in_buf, outbuf,
      nv12_sz, gst_amba_hwvdecv2_assign_slot, self, &self->frame_id_seq, &ow, &oh,
      &op, &oslot, &produced);

  // check if the output buffer is valid
  if (fret != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (self,
        "[v2] phase handle_frame/iav_fill failed flow=%s in_sz=%" G_GSIZE_FORMAT
        " out_sz=%" G_GSIZE_FORMAT " produced=%d",
        gst_flow_get_name (fret), gst_buffer_get_size (in_buf), nv12_sz,
        produced ? 1 : 0);
    gst_buffer_unref (outbuf);
    gst_video_decoder_drop_frame (decoder, frame);
    return fret;
  }

  if (!produced) {
    GST_DEBUG_OBJECT (self, "[v2] phase handle_frame/iav_fill no picture (drop silent)");
    gst_buffer_unref (outbuf);
    gst_video_decoder_drop_frame (decoder, frame);
    return GST_FLOW_OK;
  }

  GST_DEBUG_OBJECT (self,
      "[v2] phase handle_frame/iav_fill ok %ux%u pitch=%u slot=%u frame_id=%"
      G_GUINT32_FORMAT, ow, oh, op, oslot,
      self->frame_id_seq > 0 ? self->frame_id_seq - 1 : 0);

  if (!gst_amba_hwvdecv2_try_update_src_caps_for_picture (decoder, ow, oh)) {
    gst_buffer_unref (outbuf);
    gst_video_decoder_drop_frame (decoder, frame);
    return GST_FLOW_NOT_NEGOTIATED;
  }

  // add video meta to the output buffer
  if (!gst_buffer_get_video_meta (outbuf)) {
    al_h = (oh + 15) & ~15u;
    off[0] = 0;
    off[1] = (gsize) op * (gsize) al_h;
    str[0] = (gint) op;
    str[1] = (gint) op;
    gst_buffer_add_video_meta_full (outbuf, GST_VIDEO_FRAME_FLAG_NONE,
        GST_VIDEO_FORMAT_NV12, ow, oh, 2, off, str);
  }

// GstVideoMeta debug print
//  {
//    GstVideoMeta *vm = gst_buffer_get_video_meta (outbuf);

//    if (vm) {
//      gboolean row_padded =
//         (vm->stride[0] > 0 && (guint) vm->stride[0] > vm->width);
//      g_print ("[amba_hwvdecv2 v2] GstVideoMeta (pushed): dec-id=%u %s wxh=%ux%u "
//           "stride0=%d stride1=%d offset0=%" G_GSIZE_FORMAT " offset1=%" G_GSIZE_FORMAT
//           " stride0_gt_width=%d (1 => meta width < row pitch, e.g. 768 on 1920)\n",
//           (guint) self->dec_id, gst_video_format_to_string (vm->format), vm->width,
//           vm->height, vm->stride[0], vm->stride[1], vm->offset[0], vm->offset[1],
//           row_padded ? 1 : 0);
//    } else {
//      g_print ("[amba_hwvdecv2 v2] no GstVideoMeta on output buffer\n");
//    }
//  }

  // set pts to the output buffer: copy from the input buffer
  if (GST_BUFFER_PTS_IS_VALID (in_buf))
    GST_BUFFER_PTS (outbuf) = GST_BUFFER_PTS (in_buf);
  else
    GST_BUFFER_PTS (outbuf) = GST_CLOCK_TIME_NONE;

  if (self->verbose) {
    GST_INFO_OBJECT (self,
        "push frame_id=%" G_GUINT32_FORMAT " slot=%u %ux%u pitch=%u",
        self->frame_id_seq > 0 ? self->frame_id_seq - 1 : 0, oslot, ow, oh, op);
  }

  // dump nv12 for debug
  if (self->frame_id_seq > frame_id_before) {
    guint32 fid = self->frame_id_seq - 1;
    gst_amba_hwvdecv2_maybe_dump_nv12 (self, outbuf, ow, oh, op, fid);
  }

  GST_DEBUG_OBJECT (self, "[v2] phase handle_frame/pad_push begin");
  // push the output buffer to the downstream
  pushret = gst_pad_push (GST_VIDEO_DECODER_SRC_PAD (decoder), outbuf);
  if (pushret != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (self, "pad_push returned %s", gst_flow_get_name (pushret));
    gst_buffer_unref (outbuf);  // release the output buffer reference-counted
    gst_video_decoder_drop_frame (decoder, frame);  // drop the frame
    return pushret;
  }

  gst_video_decoder_drop_frame (decoder, frame);
  return GST_FLOW_OK;
}
