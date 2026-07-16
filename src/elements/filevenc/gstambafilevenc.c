/*
 * gstambafilevenc.c
 *
 * History:
 *    6/28/2025 - [Cheng Chen] created file
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
 * SECTION: element-amba_file_venc
 * @title: amba_file_venc
 *
 * amba_file_venc can be used to do NV12 frame transform bitstream with Amba DSP HW.
 *
 * ## Example pipeline
 * [[
 * gst-launch-1.0 -e -v filesrc location=yuv_canvas0_1920x1080_NV12.yuv blocksize=3110400 ! "video/x-raw, format=NV12,width=1920,height=1080,framerate=1/1" ! amba_fil
e_venc enc=stream_id:0,canvas-id:16,type:h264,stream-output:0.0.1920.1080,start:1 ! filesink location=/tmp/video.h264
 * ]]
 *  Feed YUV frames to DSP and saves to video file.
 */

#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <gst/video/video.h>
#include <gst/gst.h>
#include "debug_log.h"
#include "iav_ctx.h"
#include "gstambafilevenc.h"
#include "internal.h"
#include "common_err_code_c.h"
#include "amba_private_data.h"


GST_DEBUG_CATEGORY_STATIC (gst_amba_file_venc_debug);
#define GST_CAT_DEFAULT gst_amba_file_venc_debug

enum
{
  PROP_0,
  PROP_ENCODE_SET,
};

typedef struct {
  unsigned int is_read;
  iav_ctx_t * iav_ctx;
  amba_dsp_release_bitstream_t release_bs;
  unsigned char * p_data_sim;
} amba_release_bits_t;

#define AMBAFILEVENC_PIXEL_FORMAT       "NV12"
#define gst_amba_filevenc_parent_class parent_class
G_DEFINE_TYPE (GstAmbaFileVenc, gst_amba_filevenc, GST_TYPE_BASE_TRANSFORM);

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (AMBAFILEVENC_PIXEL_FORMAT)));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264, "
    "stream-format=(string) { byte-stream, avc, avc3 }, "
    "framerate = " GST_VIDEO_FPS_RANGE ", "
    "alignment=(string) { nal };"
    "video/x-h265, "
    "stream-format=(string) { byte-stream, hvc1, hev1 }, "
    "framerate = " GST_VIDEO_FPS_RANGE ", "
    "alignment=(string) { nal };"
    "image/jpeg, "
    "framerate = (fraction) [ 0/1, MAX ], "
    "parsed=TRUE")
  );

#define AMBAVENCCAP_DEFAULT_ENC_FORMAT "stream_id:0"
#define AMBAVENCCAP_DEFAULT_STREAM_TYPE "avc"
#define DEFAULT_SELF_PTS TRUE
#define DEFAULT_FPS 30
#define MAX_CANVAS_FPS  120
#define PTS_CLK			(90000U)

static void gst_amba_filevenc_set_property (GObject *object, guint prop_id,
    const GValue *value, GParamSpec *pspec);
static void gst_amba_filevenc_get_property (GObject *object, guint prop_id,
    GValue *value, GParamSpec *pspec);
static void gst_amba_filevenc_finalize (GObject *gobject);
static gboolean gst_amba_filevenc_start (GstBaseTransform * bsrc);
static gboolean gst_amba_filevenc_stop (GstBaseTransform * bsrc);
static GstCaps *gst_amba_filevenc_transform_caps (GstBaseTransform * trans,
  GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static gboolean gst_amba_filevenc_set_caps (GstBaseTransform *trans,
    GstCaps *incaps, GstCaps *outcaps);
static GstFlowReturn gst_amba_filevenc_transform(GstBaseTransform *trans,
    GstBuffer *inbuf, GstBuffer *outbuf);
static GstFlowReturn gst_amba_filevenc_prepare_output_buffer (GstBaseTransform * base,
    GstBuffer * inbuf, GstBuffer ** outbuf);
static gboolean gst_amba_filevenc_sink_event(GstBaseTransform *trans, GstEvent *event);

static gboolean gst_amba_prepare_yuv_params(GstAmbaFileVenc *self)
{
#define PIXEL_IN_MB		(16)

  iav_al_t * iav_al = &self->iav_ctx->iav_al;
  iav_efm_stream_cfg_t stream_cfg = {0};
  struct iav_window *yuv_size = NULL;
  guint stream_id = self->stream_id;
  gint frame_height_aligned = 0;

  memset(&stream_cfg, 0, sizeof(iav_efm_stream_cfg_t));
  stream_cfg.stream_id = stream_id;
  if (iav_al->f_efm_get_stream_cfg(&stream_cfg) < 0) {
    GST_ERROR ("Get efm stream config failed");
    return FALSE;
  }

  self->stream_type = stream_cfg.stream_type;
  self->use_me0 = (self->stream_type == IAV_STREAM_TYPE_H265);

  frame_height_aligned = ROUND_UP(self->height, PIXEL_IN_MB);
  yuv_size = &stream_cfg.pool_info.yuv_size;

  if (self->width > (gint)yuv_size->width ||
    frame_height_aligned > (gint)yuv_size->height) {
    GST_ERROR ("Aligned Frame size is %dx%d, can not larger than %dx%d!",
      self->width, frame_height_aligned, yuv_size->width, yuv_size->height);
    return FALSE;
  }

  return TRUE;
}

static void generate_me1_buf(GstBuffer *luma_buf, GstAmbaFileVenc *self)
{
  GstMapInfo map;
  guint width = self->width;
  guint height = self->height;
  guint me1_size = 0, width_out = 0, i = 0, j = 0, result = 0;
  gint width_me1 = 0, height_me1 = 0;
  gint k = 0, l = 0;
  guint8 *addr = NULL;
  guint8 *start = NULL;

  // Calculate actual ME1 dimensions
  width_me1 = (ROUND_UP(width, 4) >> 2);
  height_me1 = (ROUND_UP(height, 4) >> 2);
  me1_size = width_me1 * height_me1;  // Use actual data size
  width_out = ROUND_UP(width, 4) >> 2;

  // Reallocate ME1 buffer only if size changed
  if (!self->me1_buf || self->me1_size != me1_size) {
    if (self->me1_buf) {
      free(self->me1_buf);
    }
    self->me1_buf = (u8 *)malloc(me1_size);
    if (!self->me1_buf) {
      GST_ERROR_OBJECT(self, "Failed to allocate ME1 buffer with size %u", me1_size);
      return;
    }
    self->me1_size = me1_size;
    GST_DEBUG_OBJECT(self, "Allocated ME1 buffer with size %u", me1_size);
  }

  GstMemory *mem = gst_buffer_peek_memory(luma_buf, 0);
  if (gst_memory_map(mem, &map, GST_MAP_READ)) {
    addr = map.data;
  }

  for (i = 0; i < height / 4; i++) {
    for (j = 0; j < width_out; j++) {
      result = 0;
      start = addr + i * 4 * width + j * 4;
      for (k = 0; k < 4; k++) {
        for (l = 0; l < 4; l++) {
          result += start[k * width + l];
        }
      }
      self->me1_buf[i * width_out + j] = (result + 8) >> 4;
    }
  }

  gst_memory_unmap(mem, &map);
  return;
}

static void generate_me0_buf(GstAmbaFileVenc *self)
{
  guint width_in = 0, height_in = 0, width_out = 0, result = 0;
  guint me0_width = 0, me0_height = 0, me0_size = 0;
  guint i = 0, j = 0;
  gint k = 0, l = 0;
  guint8 *start = NULL;

  me0_width = ROUND_UP(self->width, 8) >> 3;
  me0_height = ROUND_UP(self->height, 8) >> 3;
  me0_size = me0_width * me0_height;

  // Reallocate ME0 buffer only if size changed
  if (!self->me0_buf || self->me0_size != me0_size) {
    if (self->me0_buf) {
      free(self->me0_buf);
    }
    self->me0_buf = (u8 *)malloc(me0_size);
    if (!self->me0_buf) {
      GST_ERROR_OBJECT(self, "Failed to allocate ME0 buffer with size %u", me0_size);
      return;
    }
    self->me0_size = me0_size;
    GST_DEBUG_OBJECT(self, "Allocated ME0 buffer with size %u", me0_size);
  }

  width_in = ROUND_UP(self->width, 4) >> 2;
  height_in = ROUND_UP(self->height, 4) >> 2;
  width_out = ROUND_UP(self->width, 8) >> 3;

  for (i = 0; i < height_in / 2; i++) {
    for (j = 0; j < width_out; j++) {
      result = 0;
      start = self->me1_buf + i * 2 * width_in + j * 2;
      for (k = 0; k < 2; k++) {
        for (l = 0; l < 2; l++) {
          result += start[k * width_in + l];
        }
      }
      self->me0_buf[i * width_out + j] = (result + 2) >> 2;
    }
  }

  return;
}

/* Extract ME data from amba_filemuxer output buffer */
static gboolean extract_me_data_from_buffer(GstBuffer *buffer, GstAmbaFileVenc *self)
{
  AmbaPrivateDataMeta *meta = NULL;
  GstMapInfo map_info = {0};
  gboolean found_me0 = FALSE;
  gboolean found_me1 = FALSE;
  gboolean success = FALSE;

  meta = amba_buffer_get_private_data_meta(buffer);
  if (!meta) {
    GST_DEBUG_OBJECT(self, "No AmbaPrivateDataMeta found in input buffer");
    return FALSE;
  }

  GST_DEBUG_OBJECT(self, "Found AmbaPrivateDataMeta with %u private data entries",
                   meta->private_data_count);

  // Map buffer for reading
  if (!gst_buffer_map(buffer, &map_info, GST_MAP_READ)) {
    GST_ERROR_OBJECT(self, "Failed to map buffer for reading");
    return FALSE;
  }

  for (guint i = 0; i < meta->private_data_count; i++) {
    AmbaPrivateDataEntry *entry = &meta->private_data[i];

    if (entry->format == AMBA_PRIVATE_FORMAT_ME1) {
      GST_DEBUG_OBJECT(self, "Found ME1 data: size=%u, width=%u, height=%u, offset=%u",
                      entry->size, entry->width, entry->height, entry->offset);

      // Reallocate ME1 buffer only if size changed
      if (!self->me1_buf || self->me1_size != entry->size) {
        if (self->me1_buf) {
          free(self->me1_buf);
        }
        self->me1_buf = (u8 *)malloc(entry->size);
        if (!self->me1_buf) {
          GST_ERROR_OBJECT(self, "Failed to allocate ME1 buffer");
          goto cleanup;
        }
        self->me1_size = entry->size;
        GST_DEBUG_OBJECT(self, "Reallocated ME1 buffer with size %u", entry->size);
      }

      // Copy ME1 data from buffer
      if (entry->index == 0) {
        // Packed mode: data is in single memory
        memcpy(self->me1_buf, map_info.data + entry->offset, entry->size);
      } else {
        // Multi-memory mode: data is in separate memory
        GstMemory *mem = gst_buffer_peek_memory(buffer, entry->index);
        if (!mem) {
          GST_ERROR_OBJECT(self, "Failed to get memory for ME1 data");
          goto cleanup;
        }

        GstMapInfo mem_map;
        if (!gst_memory_map(mem, &mem_map, GST_MAP_READ)) {
          GST_ERROR_OBJECT(self, "Failed to map memory for ME1 data");
          goto cleanup;
        }

        memcpy(self->me1_buf, mem_map.data + entry->offset, entry->size);
        gst_memory_unmap(mem, &mem_map);
      }

      found_me1 = TRUE;
      GST_DEBUG_OBJECT(self, "Successfully extracted ME1 data");
    }
    else if (entry->format == AMBA_PRIVATE_FORMAT_ME0 && self->use_me0) {
      GST_DEBUG_OBJECT(self, "Found ME0 data: size=%u, width=%u, height=%u, offset=%u",
                      entry->size, entry->width, entry->height, entry->offset);

      // Reallocate ME0 buffer only if size changed
      if (!self->me0_buf || self->me0_size != entry->size) {
        if (self->me0_buf) {
          free(self->me0_buf);
        }
        self->me0_buf = (u8 *)malloc(entry->size);
        if (!self->me0_buf) {
          GST_ERROR_OBJECT(self, "Failed to allocate ME0 buffer");
          goto cleanup;
        }
        self->me0_size = entry->size;
        GST_DEBUG_OBJECT(self, "Reallocated ME0 buffer with size %u", entry->size);
      }

      // Copy ME0 data from buffer
      if (entry->index == 0) {
        // Packed mode: data is in single memory
        memcpy(self->me0_buf, map_info.data + entry->offset, entry->size);
      } else {
        // Multi-memory mode: data is in separate memory
        GstMemory *mem = gst_buffer_peek_memory(buffer, entry->index);
        if (!mem) {
          GST_ERROR_OBJECT(self, "Failed to get memory for ME0 data");
          goto cleanup;
        }

        GstMapInfo mem_map;
        if (!gst_memory_map(mem, &mem_map, GST_MAP_READ)) {
          GST_ERROR_OBJECT(self, "Failed to map memory for ME0 data");
          goto cleanup;
        }

        memcpy(self->me0_buf, mem_map.data + entry->offset, entry->size);
        gst_memory_unmap(mem, &mem_map);
      }

      found_me0 = TRUE;
      GST_DEBUG_OBJECT(self, "Successfully extracted ME0 data");
    }
  }

  // Check if we found the required ME data
  if (!found_me1) {
    GST_WARNING_OBJECT(self, "ME1 data required but not found in input buffer");
    goto cleanup;
  }

  if (self->use_me0 && !found_me0) {
    GST_WARNING_OBJECT(self, "ME0 data required but not found in input buffer");
    goto cleanup;
  }

  GST_DEBUG_OBJECT(self, "Successfully extracted ME data: ME0=%s, ME1=%s",
                   found_me0 ? "YES" : "NO", found_me1 ? "YES" : "NO");
  success = TRUE;

cleanup:
  gst_buffer_unmap(buffer, &map_info);

  return success;
}

static int fill_yuv_frame(iav_efm_buf_info_t *efm_buf, unsigned long *pts,
  GstBuffer *luma_buf, GstAmbaFileVenc *self)
{
  GstMapInfo src_map;
  gint i = 0, height_align = 0;
  gint width = self->width;
  gint height = self->height;
  guint8 *dest = NULL;
  guint8 *last_line = NULL;
  GstVideoMeta *vmeta = NULL;
  guint8 *y_src = NULL;
  guint8 *uv_src = NULL;
  gint y_stride = 0;
  gint uv_stride = 0;

  if (!gst_buffer_map(luma_buf, &src_map, GST_MAP_READ)) {
    GST_ERROR("Failed to map source buffer");
    return -1;
  }

  /* Try to get GstVideoMeta from buffer */
  vmeta = gst_buffer_get_video_meta(luma_buf);
  if (vmeta && vmeta->n_planes >= 2) {
    /* Use GstVideoMeta to get correct offsets and strides */
    y_src = src_map.data + vmeta->offset[0];
    uv_src = src_map.data + vmeta->offset[1];
    y_stride = vmeta->stride[0];
    uv_stride = vmeta->stride[1];
    GST_DEBUG_OBJECT(self, "Using GstVideoMeta: y_offset=%zu, y_stride=%d, uv_offset=%zu, uv_stride=%d",
                    vmeta->offset[0], y_stride, vmeta->offset[1], uv_stride);
  } else {
    /* Fallback: assume Y and UV are contiguous */
    y_src = src_map.data;
    uv_src = src_map.data + width * height;
    y_stride = width;
    uv_stride = width;
    GST_DEBUG_OBJECT(self, "No GstVideoMeta found, assuming contiguous Y/UV layout");
  }

  height_align = ROUND_UP(height, 16);
  /* fill YUV luma */
  dest = (u8 *)efm_buf->yuv_luma_vir_addr;
  for (i = 0; i < height; i++) {
    memcpy(dest + i * efm_buf->yuv_pitch, y_src + i * y_stride, width);
  }

  /* repeat lines for aligned part */
  last_line = dest + (height - 1) * efm_buf->yuv_pitch;
  for (i = height; i < height_align; i++) {
    memcpy(dest + i * efm_buf->yuv_pitch, last_line, width);
  }

  /* fill YUV chroma */
  dest = (u8 *)efm_buf->yuv_chroma_vir_addr;
  for (i = 0; i < height / 2; i++) {
    memcpy(dest + i * efm_buf->yuv_pitch, uv_src + i * uv_stride, width);
  }

  /* repeat lines for aligned part */
  last_line = dest + (height / 2 - 1) * efm_buf->yuv_pitch;
  for (i = height / 2; i < height_align / 2; i++) {
    memcpy(dest + i * efm_buf->yuv_pitch, last_line, width);
  }

  if (self->stream_type != IAV_STREAM_TYPE_MJPEG) {
    // fill me1
    dest = (u8 *)efm_buf->me1_vir_addr;
    memcpy(dest, self->me1_buf, self->me1_size);
  }

  if (self->use_me0) {
    // fill me0
    dest = (u8 *)efm_buf->me0_vir_addr;
    memcpy(dest, self->me0_buf, self->me0_size);
  }

  *pts = self->dsp_pts_counter;
  self->dsp_pts_counter += PTS_CLK / self->frame_rate;

  gst_buffer_unmap(luma_buf, &src_map);
  return 0;
}

static int gst_amba_feed_yuv_loop(GstBuffer *luma_buf, GstAmbaFileVenc *self)
{
  iav_efm_buf_info_t req_buf;
  iav_efm_feed_cfg_t feed_cfg;
  struct timeval pre = {0, 0}, curr = {0, 0};
  unsigned long delta_us = 0;
  guint stream_id = self->stream_id;
  gint frame_rate = self->frame_rate;
  gulong frame_pts = 0;
  gint count = 0;
  gint rval = 0;

  GST_INFO ("START feeding YUV frames:\n");
  do {
    GST_INFO ("  Feed frame %8u:\t", count);
    memset(&req_buf, 0, sizeof(iav_efm_buf_info_t));
    req_buf.stream_id = stream_id;
    rval = self->iav_ctx->iav_al.f_efm_get_buf(&req_buf);
    if (rval < 0) {
      GST_ERROR ("Get efm buf failed\n");
      rval = -1;
      return rval;
    }

    fill_yuv_frame(&req_buf, &frame_pts, luma_buf, self);
    self->gst_pts = frame_pts * (GST_SECOND / PTS_CLK);

    memset(&feed_cfg, 0, sizeof(iav_efm_feed_cfg_t));
    feed_cfg.pts = frame_pts;
#ifdef BUILD_DSP_AMBA_V6
    feed_cfg.is_last_frame = 0;
#endif

    gettimeofday(&curr, NULL);
    delta_us = (curr.tv_sec - pre.tv_sec) * 1000000UL + curr.tv_usec - pre.tv_usec;
    if (delta_us < 1000000UL / frame_rate) {
      delta_us = 1000000UL / frame_rate - delta_us;
      usleep(delta_us);
    }

    rval = self->iav_ctx->iav_al.f_efm_feed_buf(&req_buf, &feed_cfg);
    if (rval < 0) {
      GST_ERROR ("Feed efm buf failed\n");
      rval = -1;
      return rval;
    }

    gettimeofday(&pre, NULL);
    GST_INFO ("  Handshake frame %3u, done!\n", count);
  } while(0);

  return rval;
}

static unsigned char * nalu_find_first_avc_slice_header_type (
  unsigned char * p, unsigned int len, unsigned char * out_nal_type)
{
  unsigned char nal_type = 0;

  if (!p) {
    return NULL;
  }

  while (len > 5) {
    if (*p == 0x00) {
      if (* (p + 1) == 0x00) {
        if (* (p + 2) == 0x00) {
          if (* (p + 3) == 0x01) {
            nal_type = (* (p + 4) ) & 0x1F;
            *out_nal_type = nal_type;
            if (nal_type <= ENalType_IDR) {
              return p;
            }
          }

        } else if (* (p + 2) == 0x01) {
          nal_type = (* (p + 3) ) & 0x1F;
          *out_nal_type = nal_type;
          if (nal_type <= ENalType_IDR) {
            return p;
          }
        }
      }
    }

    ++ p;
    len --;
  }

  return NULL;
}

static unsigned char * nalu_find_first_hevc_slice_header_type (
  unsigned char * p, unsigned int len, unsigned char * out_nal_type, unsigned char * is_first_slice)
{
  unsigned char nal_type = 0;

  if (!p) {
    return NULL;
  }

  *is_first_slice = 0;

  while (len > 5) {
    if (*p == 0x00) {
      if (* (p + 1) == 0x00) {
        if (* (p + 2) == 0x00) {
          if (* (p + 3) == 0x01) {
            nal_type = ( ( (* (p + 4) ) >> 1) & 0x3F);
            *out_nal_type = nal_type;
            if (nal_type < EHEVCNalType_VPS) {
              if (p[6] & 0x80) {
                *is_first_slice = 1;
              } else {
                *is_first_slice = 0;
              }

              return p;
            }
          }

        } else if (* (p + 2) == 0x01) {
          nal_type = ( ( (* (p + 3) ) >> 1) & 0x3F);
          *out_nal_type = nal_type;

          if (nal_type < EHEVCNalType_VPS) {
            if (p[5] & 0x80) {
              *is_first_slice = 1;
            } else {
              *is_first_slice = 0;
            }

            return p;
          }
        }
      }
    }

    ++ p;
    len --;
  }

  return NULL;
}

static int save_bitstream_to_gst_buf(GstBaseTransform *trans, GstBuffer *outbuf)
{
#define DEFAULT_TIMEOUT_MS  (0)

  GstAmbaFileVenc *self = GST_AMBA_FILEVENC(trans);
  iav_ctx_t * iav_ctx = self->iav_ctx;
  iav_al_t *iav_al = &iav_ctx->iav_al;
  amba_dsp_read_bitstream_t read_bs = {0};
  amba_release_bits_t release_param[IAV_STREAM_MAX_NUM_ALL] = {0};
  video_bs_state_t * cur_bs_state;
  guchar *p_cur, *p_tmp;
  GstBuffer *p_out_buf = NULL;
  GstMemory *mem;
  GstMapInfo output_map;
  gssize cur_size;
  unsigned char nal_type;
  unsigned char is_first_slice;
  guint stream_idx = 0;
  guint ret = 0;
  guchar h265_tile_cnt = 0;

  while (1) {
    memset(&read_bs, 0x0, sizeof(read_bs));
    read_bs.stream_idx = self->stream_id;
    read_bs.timeout_ms = DEFAULT_TIMEOUT_MS;
    ret = iav_ctx->iav_al.f_read_bitstream(iav_ctx->iav_fd, &read_bs);
    if (ret == COM_ECODE_BAD_STATE) {
      DPRINT_ERROR("read_bitstream failed, ret 0x%08x\n", ret);
      ret = -1;
      goto SAVE_BITSTREAM_TO_GST_BUF_EXIT;
    } else if (COM_ECODE_TRY_AGAIN == ret) {
      GST_DEBUG_OBJECT (self, "try to read bitstream again.");
      goto SAVE_BITSTREAM_TO_GST_BUF_EXIT;
    } else {
      /* do nothing */
    }

    // check the stream_id
    if (read_bs.stream_idx >= IAV_STREAM_MAX_NUM_ALL) {
      DPRINT_ERROR("read_bs.stream_idx %d exceed max %d.\n",
        read_bs.stream_idx, IAV_STREAM_MAX_NUM_ALL);
      ret = -1;
      goto SAVE_BITSTREAM_TO_GST_BUF_EXIT;
    }

    // stream indexyes
    stream_idx = read_bs.stream_idx;
    release_param[stream_idx].is_read = 1;
    release_param[stream_idx].iav_ctx = iav_ctx;
    release_param[stream_idx].release_bs.stream_idx = read_bs.stream_idx;
    release_param[stream_idx].release_bs.framedesc = read_bs.framedesc;
    release_param[stream_idx].p_data_sim = read_bs.p_data_sim;

    // state and src pad for this stream id
    cur_bs_state = &self->bs_states[stream_idx];
    cur_bs_state->stream_id = stream_idx;

    if (COM_ECODE_COMPLETE == ret) {  //stream enc
      DPRINT_NOTICE("eos comes\n");
      if (!read_bs.p_data_sim) {
        iav_al->f_release_bitstream(iav_ctx->iav_fd, &release_param[stream_idx].release_bs);
        release_param[stream_idx].is_read = 0;
      }
     // reset this stream's setting
      cur_bs_state->codec_format = StreamFormat_Invalid;
      cur_bs_state->key_frame_comes = 0;
      goto SAVE_BITSTREAM_TO_GST_BUF_EXIT;
    } else if (COM_ECODE_OK != ret) {
      DPRINT_WARNING("ret 0x%08x here?\n", ret);
      goto SAVE_BITSTREAM_TO_GST_BUF_EXIT;
    }

    // bitstream
    if (read_bs.p_data_sim) {
      p_cur = read_bs.p_data_sim;
    } else {
      p_cur = iav_ctx->map_bsb.base + read_bs.offset;
    }
    cur_size = read_bs.size;

    // check stream format
    if (StreamFormat_Invalid == cur_bs_state->codec_format) {
      cur_bs_state->codec_format = read_bs.stream_format;
    } else {
      if (read_bs.stream_format != cur_bs_state->codec_format) {
        DPRINT_ERROR("stream_format[%d] not match: 0x%02x, 0x%02x\n",
            stream_idx, read_bs.stream_format, cur_bs_state->codec_format);
        ret = -1;
        goto SAVE_BITSTREAM_TO_GST_BUF_EXIT;
      }
    }

    // check avc, hevc, and mjpeg
    if (StreamFormat_H264 == read_bs.stream_format) {
    // check slice type
      p_tmp = nalu_find_first_avc_slice_header_type(p_cur, cur_size, &nal_type);
      if (!p_tmp) {// not find I frame
        GST_WARNING_OBJECT(self, "not found h264 slice header on stream_idx %d, skip.\n", stream_idx);
        if (!read_bs.p_data_sim) {
          iav_al->f_release_bitstream(iav_ctx->iav_fd, &release_param[stream_idx].release_bs);
          release_param[stream_idx].is_read = 0;
        }
        goto SAVE_BITSTREAM_TO_GST_BUF_EXIT;
      }

      if (!cur_bs_state->key_frame_comes) {
        if (ENalType_IDR != nal_type) {
          if (!read_bs.p_data_sim) {
            iav_al->f_release_bitstream(iav_ctx->iav_fd, &release_param[stream_idx].release_bs);
            release_param[stream_idx].is_read = 0;
          }
          GST_WARNING_OBJECT(self, "not found h264 IDR on stream_idx %d, skip.\n", stream_idx);
          goto SAVE_BITSTREAM_TO_GST_BUF_EXIT;
        }
        DPRINT_NOTICE("h264 stream [%d] key frame comes\n", stream_idx);
        cur_bs_state->key_frame_comes = 1;
        cur_bs_state->slice_num_per_frame = read_bs.slice_num;
        cur_bs_state->tile_num_per_frame = read_bs.tile_num;
      }

      mem = gst_allocator_alloc(NULL, cur_size, NULL);
      if (!mem) {
        GST_ERROR("can not allocate memory, size = 0x%lx", cur_size);
        ret = -1;
        goto SAVE_BITSTREAM_TO_GST_BUF_EXIT;
      }
      gst_buffer_append_memory(outbuf, mem);

      gst_buffer_map(outbuf, &output_map, GST_MAP_WRITE);
      GST_BUFFER_PTS(outbuf) = self->gst_pts;
      memcpy(output_map.data, p_cur, cur_size);
      gst_buffer_unmap(outbuf, &output_map);

      GST_INFO_OBJECT(self,"stream%d: frame idx[%d] has been saved\n", stream_idx, self->cur_frame_cnt);
      self->cur_frame_cnt++;
      goto SAVE_BITSTREAM_TO_GST_BUF_EXIT;
    } else if (StreamFormat_H265 == read_bs.stream_format) {
      // check slice type
      p_tmp = nalu_find_first_hevc_slice_header_type(p_cur, cur_size,
        &nal_type, &is_first_slice);
      if (!p_tmp) {
        GST_WARNING_OBJECT(self, "not found h265 slice header on stream_idx %d, skip.", stream_idx);
        if (!read_bs.p_data_sim) {
          iav_al->f_release_bitstream(iav_ctx->iav_fd, &release_param[stream_idx].release_bs);
          release_param[stream_idx].is_read = 0;
        }
        goto SAVE_BITSTREAM_TO_GST_BUF_EXIT;
      }
      // wait key frame
      if (!cur_bs_state->key_frame_comes) {
        if ( (EHEVCNalType_IDR_W_RADL != nal_type) && (EHEVCNalType_IDR_N_LP != nal_type) ) {
          if (!read_bs.p_data_sim) {
            iav_al->f_release_bitstream(iav_ctx->iav_fd, &release_param[stream_idx].release_bs);
            release_param[stream_idx].is_read = 0;
          }
          GST_WARNING_OBJECT(self, "not found h265 IDR on stream_idx %d, skip.\n", stream_idx);
          goto SAVE_BITSTREAM_TO_GST_BUF_EXIT;
        }
        DPRINT_NOTICE("h265 stream [%d] key frame comes\n", stream_idx);
        cur_bs_state->key_frame_comes = 1;
        cur_bs_state->slice_num_per_frame = read_bs.slice_num;
        cur_bs_state->tile_num_per_frame = read_bs.tile_num;
      }

      if (self->is_last_h265_frame) {
        p_out_buf = gst_buffer_new_allocate (NULL, cur_size, NULL);
        ret = gst_buffer_fill (p_out_buf, 0, p_cur, cur_size);
	    h265_tile_cnt++;
        gst_pad_push (trans->srcpad, p_out_buf);
        if (h265_tile_cnt == read_bs.tile_num) {
          GST_INFO_OBJECT(self,"stream%d: frame idx[%d] has been saved\n", stream_idx, self->cur_frame_cnt);
          self->is_last_h265_frame = 0;
          goto SAVE_BITSTREAM_TO_GST_BUF_EXIT;
        }
        continue;
      }
      mem = gst_allocator_alloc(NULL, cur_size, NULL);
      if (!mem) {
	    GST_ERROR("can not allocate memory, size = 0x%lx", cur_size);
	    ret = -1;
	    goto SAVE_BITSTREAM_TO_GST_BUF_EXIT;
      }

      gst_buffer_append_memory(outbuf, mem);
      gst_memory_map(mem, &output_map, GST_MAP_WRITE);
      GST_BUFFER_PTS(outbuf) = self->gst_pts;
      memcpy(output_map.data, p_cur, cur_size);
      gst_memory_unmap(mem, &output_map);
      h265_tile_cnt++;
      if (h265_tile_cnt == read_bs.tile_num) {
        GST_INFO_OBJECT(self,"stream%d: frame idx[%d] has been saved\n", stream_idx, self->cur_frame_cnt);
        self->cur_frame_cnt++;
        goto SAVE_BITSTREAM_TO_GST_BUF_EXIT;
      }
      continue;
    } else if (StreamFormat_JPEG == read_bs.stream_format) {
      if ((EJPEG_MarkerPrefix != p_cur[0]) || (EJPEG_SOI != p_cur[1]) || (EJPEG_MarkerPrefix != p_cur[2]) || (128 > cur_size)) {
        GST_WARNING_OBJECT(self, "not find mjpeg header %x%x%x, or invalid data size %ld on stream %d, skip.",
          p_cur[0], p_cur[1], p_cur[2], cur_size, stream_idx);
        if (!read_bs.p_data_sim) {
          iav_al->f_release_bitstream(iav_ctx->iav_fd, &release_param[stream_idx].release_bs);
          release_param[stream_idx].is_read = 0;
        }
        goto SAVE_BITSTREAM_TO_GST_BUF_EXIT;
      }

      cur_bs_state->key_frame_comes = 1;
      cur_bs_state->slice_num_per_frame = read_bs.slice_num;
      cur_bs_state->tile_num_per_frame = read_bs.tile_num;

      mem = gst_allocator_alloc(NULL, cur_size, NULL);
      if (!mem) {
        GST_ERROR("can not allocate memory, size = 0x%lx", cur_size);
        ret = -1;
        goto SAVE_BITSTREAM_TO_GST_BUF_EXIT;
      }
      gst_buffer_append_memory(outbuf, mem);
      gst_buffer_map(outbuf, &output_map, GST_MAP_WRITE);
      GST_BUFFER_PTS(outbuf) = self->gst_pts;
      memcpy(output_map.data, p_cur, cur_size);
      gst_buffer_unmap(outbuf, &output_map);

      GST_INFO_OBJECT(self,"stream%d: frame idx[%d] has been saved\n", stream_idx, self->cur_frame_cnt);
      self->cur_frame_cnt++;
      goto SAVE_BITSTREAM_TO_GST_BUF_EXIT;
    } else {
      GST_ERROR_OBJECT(self, "not supported stream(%d) format %d, only support h264/h265/mjpeg.",
      stream_idx, read_bs.stream_format);
      ret = -1;
      break;
    }
  }

  iav_al->f_release_bitstream(iav_ctx->iav_fd, &release_param[stream_idx].release_bs);
  release_param[stream_idx].is_read = 0;

SAVE_BITSTREAM_TO_GST_BUF_EXIT:
  return ret;
}

/* initialize the amba_file_venc class */
static void gst_amba_filevenc_class_init (GstAmbaFileVencClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstBaseTransformClass *gsttrans_class = (GstBaseTransformClass *) klass;

  GST_DEBUG_CATEGORY_INIT (gst_amba_file_venc_debug, "amba_file_venc", 0,
      "Amba file venc");

  GST_INFO("gst_amba_filevenc_class_init");

  gobject_class->set_property = gst_amba_filevenc_set_property;
  gobject_class->get_property = gst_amba_filevenc_get_property;
  gobject_class->finalize = gst_amba_filevenc_finalize;

  g_object_class_install_property (gobject_class, PROP_ENCODE_SET,
    g_param_spec_string ("enc", "Enc",
    "encode setting", AMBAVENCCAP_DEFAULT_ENC_FORMAT,
    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (gstelement_class,
    "Amba File Venc",
    "Filter/File Venc",
    "Reads NV12 images transform to bitstream with Amba platform",
    "Cheng Chen <cchen@ambarella.com>");

  gst_element_class_add_static_pad_template (gstelement_class, &sink_template);
  gst_element_class_add_static_pad_template (gstelement_class, &src_template);

  gsttrans_class->start = GST_DEBUG_FUNCPTR (gst_amba_filevenc_start);
  gsttrans_class->stop = GST_DEBUG_FUNCPTR (gst_amba_filevenc_stop);
  gsttrans_class->transform = GST_DEBUG_FUNCPTR (gst_amba_filevenc_transform);
  gsttrans_class->prepare_output_buffer = GST_DEBUG_FUNCPTR (gst_amba_filevenc_prepare_output_buffer);
  gsttrans_class->transform_caps = GST_DEBUG_FUNCPTR (gst_amba_filevenc_transform_caps);
  gsttrans_class->set_caps = GST_DEBUG_FUNCPTR (gst_amba_filevenc_set_caps);
  gsttrans_class->sink_event = GST_DEBUG_FUNCPTR (gst_amba_filevenc_sink_event);

  return;
}

static void gst_amba_filevenc_init (GstAmbaFileVenc *thiz)
{
  GST_INFO ("gst_amba_filevenc_init");

  thiz->iav_ctx = acquire_iav_ctx (1);
  if (!thiz->iav_ctx) {
    GST_ERROR("acquire_iav_ctx failed\n");
    return;
  }

  thiz->stream_id = 0;
  thiz->stream_type = 0;
  thiz->use_me0 = 0;
  thiz->width = 0;
  thiz->height = 0;
  thiz->pitch = 0;
  thiz->frame_rate = 30;
  thiz->me1_buf = NULL;
  thiz->me0_buf = NULL;
  thiz->enc_info = NULL;
  thiz->start_flag = 0;
  thiz->cur_frame_cnt = 0;
  thiz->gst_pts = 0;
  thiz->dsp_pts_counter = 0;
  thiz->cfg.iav_fd = thiz->iav_ctx->iav_fd;

#ifdef BUILD_DSP_AMBA_V6
  thiz->cfg.stream_map = 0;
#else
  thiz->cfg.no_prefetch_stream_map = 0;
  memset(thiz->cfg.request_mode, 0, sizeof(thiz->cfg.request_mode));
#endif

  return;
}

static void gst_amba_filevenc_set_property (GObject *object, guint prop_id,
    const GValue *value, GParamSpec *pspec)
{
  GstAmbaFileVenc *thiz = GST_AMBA_FILEVENC (object);
  iav_ctx_t * iav_ctx = thiz->iav_ctx;
  int ret = 0;

  GST_INFO("set property");

  switch (prop_id) {
    case PROP_ENCODE_SET: {
        enc_config_t config;
        ret = parse_enc(iav_ctx->iav_fd, g_value_get_string (value), &config);
        if (ret) {
            DPRINT_ERROR("parse_enc (%s) failed\n",
            g_value_get_string (value));
            return;
        }
        thiz->stream_id = config.current_stream;
        thiz->stream_type = config.encode_fmt[config.current_stream].type;
#ifdef BUILD_DSP_AMBA_V6
        thiz->cfg.stream_map = (1U << thiz->stream_id);
#endif
        if (!iav_ctx->iav_fd_opened) {
            DPRINT_ERROR("iav not opened\n");
            return;
        }
        ret = update_enc (iav_ctx->iav_fd, &config);
        if (ret) {
            DPRINT_ERROR("update encode setting (%s) failed\n",
            g_value_get_string (value));
            return;
        }
        thiz->start_flag = 1;
    }
    break;
    default: {
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
    break;
  }

  return;
}

static void gst_amba_filevenc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAmbaFileVenc *thiz = GST_AMBA_FILEVENC (object);
  iav_ctx_t * iav_ctx = thiz->iav_ctx;

  switch (prop_id) {
      case PROP_ENCODE_SET: {
         if (!iav_ctx->iav_fd_opened) {
           DPRINT_ERROR("iav not opened\n");
           return;
         }
         if (thiz->enc_info) {
           g_free(thiz->enc_info);
           thiz->enc_info = NULL;
         }
         enc_config_t config;
         thiz->enc_info = get_enc_info_string (iav_ctx->iav_fd, &config);
         if (thiz->enc_info == NULL) {
           DPRINT_ERROR("convert encoding configure information to string failed\n");
           return;
         }
         g_value_set_string (value, thiz->enc_info);
       }
       break;
    default: {
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
       break;
  }

  return;
}

/* start -> set_caps */
/* set_property -> set_caps */
static gboolean gst_amba_filevenc_set_caps (GstBaseTransform *trans,
    GstCaps *incaps, GstCaps *outcaps)
{
  (void)outcaps;
  GstAmbaFileVenc *self = GST_AMBA_FILEVENC(trans);
  GstStructure *structure = gst_caps_get_structure (incaps, 0);
  const gchar *format = NULL;

  GST_INFO("set caps");

  g_print("Structure: %s\n", gst_structure_to_string(structure));

  format = gst_structure_get_string (structure, "format");
  if (!format || g_strcmp0 (format, "NV12") != 0) {
    GST_ERROR_OBJECT (trans, "Input format must be NV12");
    return FALSE;
  }

  gst_structure_get_int (structure, "width", &self->width);
  gst_structure_get_int (structure, "height", &self->height);

  self->pitch = ROUND_UP(self->width, LPDDR4_ALIGN);

  /* prepare YUV params, generate me0, me1 data */
  gst_amba_prepare_yuv_params(self);

  // Set output caps with input width, height and framerate
  GstStructure *out_structure = gst_caps_get_structure (outcaps, 0);
  if (out_structure) {
      gst_structure_set (out_structure, "width", G_TYPE_INT, self->width, NULL);
      gst_structure_set (out_structure, "height", G_TYPE_INT, self->height, NULL);

      // Copy framerate from input caps if available
      gint framerate_n, framerate_d;
      if (gst_structure_get_fraction (structure, "framerate", &framerate_n, &framerate_d)) {
        gst_structure_set (out_structure, "framerate", GST_TYPE_FRACTION, framerate_n, framerate_d, NULL);
      }
      gst_structure_set(out_structure, "stream-format", G_TYPE_STRING, "byte-stream", NULL);
  }

  return TRUE;
}

static gboolean gst_amba_filevenc_sink_event(GstBaseTransform *trans, GstEvent *event)
{
  GstAmbaFileVenc *self = GST_AMBA_FILEVENC(trans);
  enc_config_t config = {0};
  GstBuffer *outbuf = NULL;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      // filesrc send EOS
      g_print("Received EOS event\n");
      config.restart_stream_id = 0;
      config.stop_stream_id |= (1 << self->stream_id);
      DPRINT_NOTICE("stop stream%d\n", config.stop_stream_id);
      update_enc(self->iav_ctx->iav_fd, &config);

      if (self->stream_type == IAV_STREAM_TYPE_H265) {
        self->is_last_h265_frame = 1;
        if (save_bitstream_to_gst_buf(trans, outbuf) < 0) {
	      GST_ERROR("Save bitstream failed\n");
	      return GST_FLOW_ERROR;
        }
      }

      break;
    default:
      break;
  }

  return GST_BASE_TRANSFORM_CLASS(parent_class)->sink_event (trans, event);
}

static GstCaps *gst_amba_filevenc_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  (void)caps;
  GstAmbaFileVenc *self = GST_AMBA_FILEVENC(trans);
  GstCaps *result = NULL;

  if (direction == GST_PAD_SINK) {
    if (self->stream_type == IAV_STREAM_TYPE_H264) {
      result = gst_caps_from_string("video/x-h264");
    } else if (self->stream_type == IAV_STREAM_TYPE_H265) {
      result = gst_caps_from_string("video/x-h265");
    } else {
      result = gst_caps_from_string("image/jpeg");
    }
  } else {
    result = gst_caps_from_string("video/x-raw");
  }

  if (filter) {
    GstCaps *tmp = result;
    result = gst_caps_intersect (tmp, filter);
    gst_caps_unref (tmp);
  }

  GST_DEBUG_OBJECT(self, "Direction: %s, Result caps: %" GST_PTR_FORMAT,
    (direction == GST_PAD_SINK) ? "sink" : "src", result);
  return result;
}

static GstFlowReturn gst_amba_filevenc_prepare_output_buffer (GstBaseTransform * base,
    GstBuffer * inbuf, GstBuffer ** outbuf)
{
  (void)base;
  (void)inbuf;

  *outbuf = gst_buffer_new();
  if (!*outbuf) {
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}

static GstFlowReturn gst_amba_filevenc_transform(GstBaseTransform *trans,
    GstBuffer *inbuf, GstBuffer *outbuf)
{
  (void)outbuf;
  GstAmbaFileVenc *self = GST_AMBA_FILEVENC(trans);
  GstBuffer *luma_buf = inbuf;    //from filesrc NV12 buffer

  // Try to extract ME data from amba_filemuxer output buffer first
  gboolean me_data_extracted = extract_me_data_from_buffer(luma_buf, self);

  if (!me_data_extracted) {
    GST_DEBUG_OBJECT(self, "No ME data found in input buffer, calculating ME data from luma");

    // Fallback to original calculation method
    generate_me1_buf(luma_buf, self);

    if (self->use_me0) {
      generate_me0_buf(self);
    }
  }

  gst_amba_feed_yuv_loop(luma_buf, self);

  if (self->stream_type == IAV_STREAM_TYPE_H265) {
    if (self->start_flag) {
      self->start_flag = 0;
      DPRINT_NOTICE("After starting encode, save empty buffer\n");
      return GST_FLOW_OK;
    }
  }

  /* save bitstream to gst output buf */
  if (save_bitstream_to_gst_buf(trans, outbuf) < 0) {
    GST_ERROR("Save bitstream failed\n");
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}

/* set_property-->start-->create */
static gboolean gst_amba_filevenc_start(GstBaseTransform *trans)
{
  GstAmbaFileVenc *thiz = GST_AMBA_FILEVENC(trans);
  iav_efm_usr_cfg_t cfg = thiz->cfg;

  GST_INFO("gst_amba_filevenc_start");
  thiz->dsp_pts_counter = 0;
  if (thiz->iav_ctx->iav_al.f_efm_lib_init(&cfg) < 0) {
    GST_ERROR("Init EFM Lib failed\n");
    return FALSE;
  }

  return TRUE;
}

static gboolean gst_amba_filevenc_stop (GstBaseTransform * bsrc)
{
  (void)bsrc;

  return TRUE;
}

static void gst_amba_filevenc_finalize(GObject * gobject)
{
  GstAmbaFileVenc *self = GST_AMBA_FILEVENC (gobject);

  self->iav_ctx->iav_al.f_efm_lib_deinit();

  if (self->iav_ctx) {
    release_iav_ctx (1);
    self->iav_ctx = NULL;
  }

  // Free ME buffers
  if (self->me1_buf) {
    free(self->me1_buf);
    self->me1_buf = NULL;
    self->me1_size = 0;
  }
  if (self->me0_buf) {
    free(self->me0_buf);
    self->me0_buf = NULL;
    self->me0_size = 0;
  }

  G_OBJECT_CLASS (parent_class)->finalize (gobject);

  return;
}
