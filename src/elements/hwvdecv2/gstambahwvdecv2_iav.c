/*
 * gstambahwvdecv2_iav.c
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


#include "string.h"
#include "stdlib.h"
#include "errno.h"

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideodecoder.h>

#include <unistd.h>
#include <sys/ioctl.h>
#include <pthread.h>

#include "internal.h"
#include "platform_al.h"
#include "debug_log.h"
#include "iav_al.h"
#include "iav_ctx.h"
#include "iav_ioctl.h"
#include "codec_parser.h"
#include "utils.h"

#include "gstambahwvdecv2.h"
#include "gstambahwvdecv2_iav.h"
#include "gstambahwclock.h"
#include "gst_amba_cavalry_bufferpool.h"
#include "gst_amba_pitch_align.h"

#define HWVDECV2_BSB_ALIGN ((guint) (1u << 13))

/* Serialize f_enter_mode + decode_mode_entered across all amba_hwvdecv2 in one process. */
static pthread_mutex_t hwvdecv2_dec_mode_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
  guint size;
  guint offset;
  guint sc_offset;
  gboolean valid;
  guint8 start_code[4];
  guchar type;
  guchar sc_length;
  guchar reserved[2];
  guint8 *data;
} HwvdecV2H264Nalu;

static unsigned char *
nalu_find_first_avc_nal_type (unsigned char *p, unsigned int len,
    unsigned char *out_nal_type)
{
  if (!p)
    return NULL;
  while (len > 5) {
    if (*p == 0x00) {
      if (*(p + 1) == 0x00) {
        if (*(p + 2) == 0x00) {
          if (*(p + 3) == 0x01) {
            *out_nal_type = ((*(p + 4)) & 0x1F);
            return p;
          }
        } else if (*(p + 2) == 0x01) {
          *out_nal_type = ((*(p + 3)) & 0x1F);
          return p;
        }
      }
    }
    ++p;
    len--;
  }
  return NULL;
}

static gint
identify_nalu_avc (const guchar * data, guint size, guint nal_length_size,
    HwvdecV2H264Nalu * nalu)
{
  guint nbits = nal_length_size * 8;
  guint bytes = 0, bits = 0;

  if (nal_length_size > G_MAXUINT32)
    return -1;
  if (size < nal_length_size)
    return -1;

  nalu->size = 0;
  while (nbits > 0) {
    guint toread = MIN (nbits, 8 - bits);
    nalu->size <<= toread;
    nalu->size |= (data[bytes] & (0xff >> bits)) >> (8 - toread - bits);
    bits += toread;
    if (bits >= 8) {
      bytes++;
      bits = 0;
    }
    nbits -= toread;
  }

  if (nalu->size < 1)
    return -1;
  nalu->sc_offset = 0;
  nalu->offset = nal_length_size;

  if ((nalu->size + nal_length_size > G_MAXUINT32) ||
      (nalu->size + nal_length_size > size))
    return -1;

  nalu->data = (guint8 *) data;
  nalu->sc_length = 4;
  nalu->start_code[3] = 0x01;
  nalu->valid = TRUE;
  nalu->type = (data[nalu->offset] & 0x1f);
  return 0;
}

static guchar *
copy_data_to_bsb (GstAmbaHwvdecV2 * self, guchar * ptr, guchar * buffer, guint size)
{
  if (!ptr || !buffer || !self->mpBitSreamBufferStart || !self->mpBitSreamBufferEnd)
    return ptr;
  if (size == 0)
    return ptr;
  if (ptr < self->mpBitSreamBufferStart || ptr > self->mpBitSreamBufferEnd)
    return ptr;

  if (ptr + size <= self->mpBitSreamBufferEnd) {
    memcpy ((void *) ptr, (const void *) buffer, size);
    return ptr + size;
  } else {
    int room = (int) (self->mpBitSreamBufferEnd - ptr);
    guchar *ptr2;
    memcpy ((void *) ptr, (const void *) buffer, room);
    ptr2 = buffer + room;
    size -= room;
    memcpy ((void *) self->mpBitSreamBufferStart, (const void *) ptr2, size);
    return self->mpBitSreamBufferStart + size;
  }
}

static int
setup_bsb_offsets (GstAmbaHwvdecV2 * self)
{
  iav_ctx_t *ctx = self->iav_ctx;
  guint num_dec = (guint) ctx->dec_mode.mModeConfig.num_decoder;
  guint dec_id = self->dec_id;
  gssize bsb_size = ctx->map_dec_bsb.size;
  gssize bsb_per;
  guchar *base_addr;

  /* If driver leaves num_decoder at 0, dec_id >= num_dec becomes 0 >= 0 and we fail
   * on every subsequent set_format (attach path). Fall back to element property. */
  if (num_dec == 0) {
    num_dec = self->num_decoders ? self->num_decoders : 1u;
    GST_DEBUG_OBJECT (self,
        "[v2] BSB: mModeConfig.num_decoder was 0, using num_dec=%u from property",
        num_dec);
  }

  if (dec_id >= num_dec) {
    GST_ERROR_OBJECT (self, "dec-id %u >= num_decoder %u", dec_id, num_dec);
    return -1;
  }

  memset (&self->mDecoderInfo, 0, sizeof (self->mDecoderInfo));
  self->mDecoderInfo.decoder_id = (unsigned char) dec_id;
  self->mDecoderInfo.decoder_type = EAMDSP_VIDEO_CODEC_TYPE_H264;
  self->mDecoderInfo.num_vout = 0;

  if (!self->mbAutoMapBSB) {
    base_addr = (guchar *) ctx->map_dec_bsb.base;
    bsb_per =
        (gssize) ROUND_DOWN ((guint64) bsb_size / (guint64) num_dec,
        (guint64) HWVDECV2_BSB_ALIGN);
    if (num_dec == 1) {
      self->mDecoderInfo.bsb_start_offset = 0;
      self->mDecoderInfo.bsb_size = (unsigned int) bsb_size;
    } else if (dec_id < num_dec - 1) {
      self->mDecoderInfo.bsb_start_offset =
          (unsigned int) (dec_id * bsb_per);
      self->mDecoderInfo.bsb_size = (unsigned int) bsb_per;
    } else {
      self->mDecoderInfo.bsb_start_offset =
          (unsigned int) (dec_id * bsb_per);
      self->mDecoderInfo.bsb_size =
          (unsigned int) (bsb_size - self->mDecoderInfo.bsb_start_offset);
    }
    self->mDecoderInfo.b_use_addr = 0;
    self->mpBitStreamBufferCurPtr = self->mpBitSreamBufferStart =
        base_addr + self->mDecoderInfo.bsb_start_offset;
    self->mpBitSreamBufferEnd =
        self->mpBitSreamBufferStart + self->mDecoderInfo.bsb_size;
  } else {
    /* Automap: f_create_decoder returns absolute BSB in bsb_start_offset (b_use_addr = 0). */
    self->mDecoderInfo.b_use_addr = 0;
    self->mpBitStreamBufferCurPtr = self->mpBitSreamBufferStart = NULL;
    self->mpBitSreamBufferEnd = NULL;
  }

  return 0;
}

static int
enter_or_attach_decode_mode (GstAmbaHwvdecV2 * self)
{
  iav_ctx_t *ctx = self->iav_ctx;
  iav_al_t *al = &ctx->iav_al;
  amba_dsp_query_decode_config_t dec_config;
  amba_dsp_decode_mode_config_t *cfg = &ctx->dec_mode.mModeConfig;
  guint num_dec;
  guint dec_id = self->dec_id;
  guint configured;
  int ret;
  guint i;

  pthread_mutex_lock (&hwvdecv2_dec_mode_mutex);
  GST_DEBUG_OBJECT (self, "[v2] decode_mode: mutex locked");

  memset (&dec_config, 0, sizeof (dec_config));
  al->f_query_decode_config (ctx->iav_fd, &dec_config);
  self->mbAutoMapBSB = dec_config.auto_map_bsb;
  GST_DEBUG_OBJECT (self, "[v2] decode_mode: query_decode_config auto_map_bsb=%d",
      (int) self->mbAutoMapBSB);

  if (ctx->decode_mode_entered) {
    configured = (guint) ctx->dec_mode.mModeConfig.num_decoder;
    if (configured == 0) {
      configured = self->num_decoders ? self->num_decoders : 1u;
      GST_DEBUG_OBJECT (self,
          "[v2] decode_mode: attach: ctx num_decoder was 0, using configured=%u",
          configured);
    }
    /* After enter-decode-mode, mModeConfig.num_decoder often reflects the driver
     * maximum (dmesg e.g. 16): how many decoder IDs / canvases can decode YUV at
     * once. Property num-decoders is how many channels this pipeline requested;
     * num-decoders <= that maximum (e.g. 1 <= 16) is valid only error if we ask
     * for more logical channels than the mode allows. */
    if (self->num_decoders > configured) {
      GST_ERROR_OBJECT (self,
          "num-decoders=%u exceeds driver mode num_decoder=%u", self->num_decoders,
          configured);
      ret = -1;
      goto unlock;
    }
    if (self->num_decoders < configured) {
      GST_DEBUG_OBJECT (self,
          "[v2] decode_mode: attach: driver num_decoder=%u (property num-decoders=%u)",
          configured, self->num_decoders);
    }
    if (dec_id >= configured) {
      GST_ERROR_OBJECT (self, "dec-id %u >= configured num_decoder %u", dec_id,
          configured);
      ret = -1;
      goto unlock;
    }
    GST_DEBUG_OBJECT (self,
        "[v2] decode_mode: attach existing num_decoder=%u", configured);
    ret = setup_bsb_offsets (self);
    if (ret == 0) {
      GST_DEBUG_OBJECT (self,
          "[v2] decode_mode: attach BSB start=%p end=%p", (gpointer)
          self->mpBitSreamBufferStart, (gpointer) self->mpBitSreamBufferEnd);
    }
    goto unlock;
  }

  num_dec = self->num_decoders;
  if (num_dec < 1)
    num_dec = 1;
  if (num_dec > (guint) DAMBADSP_MAX_DECODER_NUMBER) {
    GST_ERROR_OBJECT (self, "num-decoders %u exceeds DAMBADSP_MAX_DECODER_NUMBER %u",
        num_dec, (guint) DAMBADSP_MAX_DECODER_NUMBER);
    ret = -1;
    goto unlock;
  }
  if (dec_id >= num_dec) {
    GST_ERROR_OBJECT (self, "dec-id %u >= num-decoders %u", dec_id, num_dec);
    ret = -1;
    goto unlock;
  }

  GST_DEBUG_OBJECT (self,
      "[v2] decode_mode: first enter num_dec=%u max %ux%u", num_dec,
      self->max_coded_width, self->max_coded_height);

  memset (cfg, 0, sizeof (*cfg));
  cfg->num_decoder = (unsigned char) num_dec;
  cfg->num_vout = 0;
  cfg->vout_mask = 0x01;
  cfg->b_support_ff_fb_bw = 0;
  cfg->max_gop_size = 0;
  cfg->max_vout0_width = self->max_coded_width;
  cfg->max_vout0_height = self->max_coded_height;

  for (i = 0; i < num_dec; i++) {
    amba_dsp_decode_chan_config_t *ch = &cfg->multi_chan_configs[i];
    ch->chan_id = (unsigned char) i;
    ch->decoder_type = EAMDSP_VIDEO_CODEC_TYPE_H264;
    ch->enable_vout = 0;
    ch->max_frm_width = self->max_coded_width;
    ch->max_frm_height = self->max_coded_height;
    ch->layers_map = 0;
    ch->scale_type = EDSP_PYRAMID_SCALE_SQRT2;
    memset (&ch->crop_win, 0, sizeof (ch->crop_win));
    ch->layer1_width = 0;
    ch->layer1_height = 0;
    cfg->decoder_type[i] = EAMDSP_VIDEO_CODEC_TYPE_H264;
    cfg->enable_vout[i] = 0;
    cfg->max_frm_width[i] = self->max_coded_width;
    cfg->max_frm_height[i] = self->max_coded_height;
  }

  GST_DEBUG_OBJECT (self, "[v2] decode_mode: calling f_enter_mode");
  ret = al->f_enter_mode (ctx->iav_fd, cfg);
  if (ret < 0) {
    GST_ERROR_OBJECT (self, "f_enter_mode failed: %d", ret);
    goto unlock;
  }
  GST_DEBUG_OBJECT (self, "[v2] decode_mode: f_enter_mode ok");
  ctx->decode_mode_entered = 1;
  ret = setup_bsb_offsets (self);
  if (ret == 0) {
    GST_DEBUG_OBJECT (self,
        "[v2] decode_mode: BSB start=%p end=%p (dec_bsb map base=%p size=%u)",
        (gpointer) self->mpBitSreamBufferStart,
        (gpointer) self->mpBitSreamBufferEnd, (gpointer) ctx->map_dec_bsb.base,
        ctx->map_dec_bsb.size);
  }

unlock:
  pthread_mutex_unlock (&hwvdecv2_dec_mode_mutex);
  GST_DEBUG_OBJECT (self, "[v2] decode_mode: mutex unlocked ret=%d", ret);
  return ret;
}

static int
create_decoder_slot (GstAmbaHwvdecV2 * self, guint width, guint height)
{
  iav_ctx_t *ctx = self->iav_ctx;
  int ret;

  if (self->is_decoder_created)
    return 0;

  GST_DEBUG_OBJECT (self, "[v2] create_decoder: %ux%u (dec_id=%u)", width, height,
      (guint) self->dec_id);

  self->mDecoderInfo.width = width;
  self->mDecoderInfo.height = height;

  if (!self->mbAutoMapBSB) {
    if (!self->mpBitSreamBufferStart || !self->mpBitSreamBufferEnd) {
      GST_ERROR_OBJECT (self, "BSB pointers not initialized");
      return -1;
    }
  }

  GST_DEBUG_OBJECT (self, "[v2] create_decoder: f_create_decoder ...");
  ret = ctx->iav_al.f_create_decoder (ctx->iav_fd, &self->mDecoderInfo);
  if (ret < 0) {
    GST_ERROR_OBJECT (self, "f_create_decoder failed: %d", ret);
    return ret;
  }
  GST_DEBUG_OBJECT (self, "[v2] create_decoder: f_create_decoder ok");

  if (self->mbAutoMapBSB) {
    if (!self->mDecoderInfo.b_use_addr) {
      self->mpBitStreamBufferCurPtr = self->mpBitSreamBufferStart =
          (guchar *) (unsigned long) self->mDecoderInfo.bsb_start_offset;
      self->mpBitSreamBufferEnd =
          self->mpBitSreamBufferStart + self->mDecoderInfo.bsb_size;
    } else {
      self->mpBitStreamBufferCurPtr = self->mpBitSreamBufferStart =
          (guchar *) ctx->map_dec_bsb.base + self->mDecoderInfo.bsb_start_offset;
      self->mpBitSreamBufferEnd =
          self->mpBitSreamBufferStart + self->mDecoderInfo.bsb_size;
    }
  }

  GST_DEBUG_OBJECT (self, "[v2] create_decoder: f_start + f_speed");
  ctx->iav_al.f_start (ctx->iav_fd, self->dec_id);
  ctx->iav_al.f_speed (ctx->iav_fd, self->dec_id, 0x100,
      EAMDSP_PB_SCAN_MODE_ALL_FRAMES, EAMDSP_PB_DIRECTION_FW);

  self->is_decoder_created = TRUE;
  self->b_1st_frame = TRUE;
  GST_DEBUG_OBJECT (self, "[v2] create_decoder: done");
  return 0;
}

gboolean
gst_ambahwvdecv2_iav_init_ctx (GstAmbaHwvdecV2 * self)
{
  GST_DEBUG_OBJECT (self, "[v2] iav: acquire_iav_ctx(1) ...");
  self->iav_ctx = acquire_iav_ctx (1);
  if (!self->iav_ctx) {
    GST_ERROR_OBJECT (self, "acquire_iav_ctx failed");
    return FALSE;
  }
  self->iav_ctx_acquired = TRUE;
  self->hwtimer_outfreq = (guint) gst_amba_hwtimer_get_outfreq ();
  GST_DEBUG_OBJECT (self, "[v2] iav: ctx acquired iav_fd=%d hwtimer_outfreq=%u",
      self->iav_ctx->iav_fd, self->hwtimer_outfreq);
  return TRUE;
}

void
gst_ambahwvdecv2_iav_release_ctx (GstAmbaHwvdecV2 * self)
{
  g_clear_pointer (&self->query_desc, g_free);
  if (self->iav_ctx_acquired) {
    release_iav_ctx (1);
    self->iav_ctx = NULL;
    self->iav_ctx_acquired = FALSE;
  }
}

void
gst_ambahwvdecv2_iav_shutdown_decoder (GstAmbaHwvdecV2 * self)
{
  if (!self->iav_ctx || !self->is_decoder_created)
    goto reset;

  self->iav_ctx->iav_al.f_stop (self->iav_ctx->iav_fd, self->dec_id, 1);
  self->iav_ctx->iav_al.f_destroy_decoder (self->iav_ctx->iav_fd, self->dec_id);

reset:
  self->is_decoder_created = FALSE;
  self->b_1st_frame = TRUE;
  self->mFrameCount = 0;
  self->extradata_size = 0;
  self->sps_size = 0;
  self->pps_size = 0;
  self->p_cur_extradata = NULL;
  self->last_canvas_seq = 0xFFFFFFFFu;
  self->canvas_init_done = FALSE;
  self->canvas_yuv_disabled = FALSE;
  self->canvas_cap_valid = FALSE;
  self->canvas_out_valid = FALSE;
}

gboolean
gst_ambahwvdecv2_iav_flush_decoder (GstAmbaHwvdecV2 * self)
{
  if (!self->iav_ctx || !self->is_decoder_created)
    return TRUE;

  if (self->iav_ctx->iav_al.f_stop (self->iav_ctx->iav_fd, self->dec_id, 1) < 0) {
    GST_ERROR_OBJECT (self, "f_stop failed");
    return FALSE;
  }
  self->mpBitStreamBufferCurPtr = self->mpBitSreamBufferStart;
  self->b_1st_frame = TRUE;

  if (self->iav_ctx->iav_al.f_start (self->iav_ctx->iav_fd, self->dec_id) < 0) {
    GST_ERROR_OBJECT (self, "f_start failed");
    return FALSE;
  }
  return TRUE;
}

/* One-shot IAV canvas query after decode mode is ready: fills alloc_preset_* for
 * NV12 pool sizing without touching last_canvas_seq (decode path owns seq). */
static void
gst_ambahwvdecv2_iav_probe_canvas_preset_dims (GstAmbaHwvdecV2 * self)
{
  int ret;
  struct iav_yuv_cap *yuv;

  self->alloc_preset_valid = FALSE;
  self->alloc_preset_w = 0;
  self->alloc_preset_h = 0;
  if (!self->iav_ctx || !self->query_desc)
    return;

  self->query_desc->arg.canvas.canvas_id = self->canvas_id;
  ret = ioctl (self->iav_ctx->iav_fd, IAV_IOC_QUERY_DESC, self->query_desc);
  if (ret < 0) {
    GST_DEBUG_OBJECT (self,
        "[v2] prepare: probe IAV_IOC_QUERY_DESC failed: %s", g_strerror (errno));
    return;
  }

  yuv = &self->query_desc->arg.canvas.yuv;
  if (yuv->width < 16 || yuv->height < 16) {
    GST_DEBUG_OBJECT (self,
        "[v2] prepare: probe canvas empty (wxh=%ux%u); pool uses coded dims unless "
        "alloc-buf-width/height set",
        (guint) yuv->width, (guint) yuv->height);
    return;
  }

  self->alloc_preset_w =
      (guint) AMBA_ROUND_GSIZE_ODD_DSP_PITCH_UNITS ((gsize) yuv->width);
  if (yuv->pitch >= 16 && (guint) yuv->pitch > self->alloc_preset_w)
    self->alloc_preset_w = (guint) yuv->pitch;
  self->alloc_preset_h = (guint) yuv->height;
  self->alloc_preset_valid = TRUE;
  GST_DEBUG_OBJECT (self,
      "[v2] prepare: probe preset NV12 alloc stride x h = %ux%u (yuv %ux%u pitch=%u)",
      self->alloc_preset_w, self->alloc_preset_h, (guint) yuv->width, (guint) yuv->height,
      (guint) yuv->pitch);
}

gboolean
gst_ambahwvdecv2_iav_prepare (GstAmbaHwvdecV2 * self, GstVideoCodecState * state)
{
  GstVideoInfo *info;
  guint cw, ch;

  GST_DEBUG_OBJECT (self, "[v2] prepare: enter");

  if (!self->iav_ctx) {
    GST_ERROR_OBJECT (self, "no IAV context");
    return FALSE;
  }

  self->canvas_out_valid = FALSE;
  self->alloc_preset_valid = FALSE;
  self->alloc_preset_w = 0;
  self->alloc_preset_h = 0;

  g_clear_pointer (&self->query_desc, g_free);
  self->query_desc = g_malloc0 (sizeof (struct iav_querydesc));
  if (!self->query_desc)
    return FALSE;

  self->query_desc->qid = IAV_DESC_CANVAS;
  self->query_desc->arg.canvas.yuv_use_dma_buf_fd = 0;
  self->query_desc->arg.canvas.me_use_dma_buf_fd = 0;
  self->query_desc->arg.canvas.skip_cache_sync = 1;
  self->query_desc->arg.canvas.non_block_flag = 0;

  info = &state->info;
  cw = GST_VIDEO_INFO_WIDTH (info);
  ch = GST_VIDEO_INFO_HEIGHT (info);
  GST_DEBUG_OBJECT (self,
      "[v2] prepare: GstVideoInfo WxH from caps (before default-if-small) %ux%u", cw,
      ch);
  if (cw < 16)
    cw = GST_AMBA_HWVDECV2_DEFAULT_CODED_WIDTH;
  if (ch < 16)
    ch = GST_AMBA_HWVDECV2_DEFAULT_CODED_HEIGHT;

  self->max_coded_width = cw;
  self->max_coded_height = ch;
  self->canvas_id = self->dec_id;
  self->hw_decoder_id = self->dec_id;

  GST_DEBUG_OBJECT (self,
      "[v2] prepare: max_coded %ux%u canvas_id=%u", self->max_coded_width,
      self->max_coded_height, self->canvas_id);

  /* h264parse often negotiates twice (e.g. 0x0 then real SPS size). Re-running
   * shutdown + attach can fail on some BSPs; if decode mode is already ours, only
   * refresh max_coded_* and GOP. "configured" here is usually driver max decoder
   * slots (see dmesg); dec-id must be < configured; num-decoders only needs to be
   * <= configured (e.g. 1 <= 16). */
  if (self->iav_ctx->decode_mode_entered) {
    guint configured =
        (guint) self->iav_ctx->dec_mode.mModeConfig.num_decoder;
    if (configured == 0)
      configured = self->num_decoders ? self->num_decoders : 1u;
    if (self->num_decoders <= configured && (guint) self->dec_id < configured) {
      GST_DEBUG_OBJECT (self,
          "[v2] prepare: decode mode already active (driver num_decoder=%u), skip re-enter",
          configured);
      goto decode_mode_ready;
    }
  }

  gst_ambahwvdecv2_iav_shutdown_decoder (self);
  GST_DEBUG_OBJECT (self, "[v2] prepare: shutdown_decoder (if any) done");

  if (enter_or_attach_decode_mode (self) < 0)
    return FALSE;
  GST_DEBUG_OBJECT (self, "[v2] prepare: enter_or_attach_decode_mode ok");

decode_mode_ready:
  if (self->mSpecifiedTimeScale == 0 || self->mSpecifiedFrameTick == 0) {
    self->mSpecifiedTimeScale = 30;
    self->mSpecifiedFrameTick = 1;
  }
  gstFillAmbaH264GopHeader (self->mpAmbaGopHeader, self->mSpecifiedFrameTick,
      self->mSpecifiedTimeScale, 0, 0, 1);
  GST_DEBUG_OBJECT (self, "[v2] prepare: GOP header filled");

  {
    struct iav_canvas_cfg canvas_cfg;
    memset (&canvas_cfg, 0, sizeof (canvas_cfg));

#if defined (BUILD_DSP_AMBA_V5)
    struct iav_system_resource iav_sys_rsc;
    memset(&iav_sys_rsc, 0, sizeof(struct iav_system_resource));

    iav_sys_rsc.encode_mode = DSP_CURRENT_MODE;
    if (ioctl(self->iav_ctx->iav_fd, IAV_IOC_GET_SYSTEM_RESOURCE, &iav_sys_rsc) < 0) {
        perror("IAV_IOC_GET_SYSTEM_RESOURCE\n");
        return FALSE;
    }

    if ((self->canvas_id >= 0) && (self->canvas_id < iav_sys_rsc.canvas_num)) {
      canvas_cfg = iav_sys_rsc.canvas_cfg[self->canvas_id];
    } else {
      GST_ERROR_OBJECT(self, "canvas_id[%d] invalid, should in [0~%d]",
        self->canvas_id, iav_sys_rsc.canvas_num-1);
    }
#elif defined (BUILD_DSP_AMBA_V6)
    canvas_cfg.canvas_id = self->canvas_id;
    if (ioctl(self->iav_ctx->iav_fd, IAV_IOC_GET_CANVAS_CONFIG, &canvas_cfg) < 0) {
        perror("IAV_IOC_GET_CANVAS_CONFIG\n");
        return FALSE;
    }
#endif

    if ((canvas_cfg.max.width >= 16) && (canvas_cfg.max.height >= 16)) {
      self->canvas_cap_w = (guint) canvas_cfg.max.width;
      self->canvas_cap_h = (guint) canvas_cfg.max.height;
      self->canvas_cap_valid = TRUE;
      GST_DEBUG_OBJECT (self,
          "[v2] prepare: GET_CANVAS_CONFIG max canvas %ux%u (tighten NV12 pitch if < SPS)",
          self->canvas_cap_w, self->canvas_cap_h);
    } else {
      self->canvas_cap_valid = FALSE;
      GST_DEBUG_OBJECT (self,
          "[v2] prepare: GET_CANVAS_CONFIG missing or small max (pitch follows canvas query)");
    }
  }

  gst_ambahwvdecv2_iav_probe_canvas_preset_dims (self);

  self->iav_pipeline_ready = TRUE;
  self->last_canvas_seq = 0xFFFFFFFFu;
  GST_DEBUG_OBJECT (self, "[v2] prepare: done (pipeline_ready=TRUE)");
  return TRUE;
}

static gboolean
query_canvas_new_picture (GstAmbaHwvdecV2 * self, struct iav_yuv_cap **out_yuv)
{
  int ret;
  struct iav_canvasdesc *canvas;
  struct iav_yuv_cap *yuv;
  guint32 seq;

  self->query_desc->arg.canvas.canvas_id = self->canvas_id;
  canvas = &self->query_desc->arg.canvas;
  yuv = &canvas->yuv;

  ret = ioctl (self->iav_ctx->iav_fd, IAV_IOC_QUERY_DESC, self->query_desc);

  if (ret < 0) {
    if (errno == EAGAIN || errno == EINTR)
      return FALSE;
    GST_ERROR_OBJECT (self, "IAV_IOC_QUERY_DESC failed: %s", g_strerror (errno));
    return FALSE;
  }

  if (yuv->width == 0 || yuv->height == 0) {
    GST_DEBUG_OBJECT (self,
        "[v2] canvas query: empty picture (canvas_id=%d wxh=%ux%u)",
        (int) self->canvas_id, (guint) yuv->width, (guint) yuv->height);
    return FALSE;
  }

  seq = yuv->seq_num;
  if (seq == self->last_canvas_seq && self->last_canvas_seq != 0xFFFFFFFFu) {
    GST_DEBUG_OBJECT (self,
        "[v2] canvas query: same seq=%u as last (no new picture)", (guint) seq);
    return FALSE;
  }
  self->last_canvas_seq = seq;

  GST_DEBUG_OBJECT (self,
      "[v2] canvas query: new picture canvas_id=%d seq=%u %ux%u pitch=%u fmt=%d "
      "y_off=0x%x uv_off=0x%x",
      (int) self->canvas_id, (guint) seq, (guint) yuv->width, (guint) yuv->height,
      (guint) yuv->pitch, (gint) yuv->format, (guint) yuv->y_addr_offset,
      (guint) yuv->uv_addr_offset);

  *out_yuv = yuv;
  return TRUE;
}

/* Destination: IAV GDMA with dst_use_phys=1 and absolute dst_addr (see iav_ioctl.h
 * struct iav_gdma_copy). @p dst_y_pa is the physical address of the first byte of
 * NV12 Y in the output buffer (Cavalry meta slab_phys_base is already per-buffer). */
static gboolean
gdma_nv12_to_cavalry_phys (GstAmbaHwvdecV2 * self, struct iav_yuv_cap *yuv,
    guint canvas_w, guint canvas_h, guint src_pitch, guint dst_pitch,
    guint64 dst_y_pa, gsize nv12_buffer_size)
{
  struct iav_gdma_copy gdma = { 0 };
  guint al_h = (canvas_h + 15) & ~15u;
  gsize need = (gsize) dst_pitch * (gsize) al_h * 3 / 2;

  if (dst_y_pa == 0) {
    GST_ERROR_OBJECT (self, "GDMA: invalid dst phys (0)");
    return FALSE;
  }

  if (src_pitch > dst_pitch) {
    GST_ERROR_OBJECT (self,
        "GDMA: canvas src_pitch %u > Cavalry dst_pitch %u (increase coded width / pool)",
        src_pitch, dst_pitch);
    return FALSE;
  }

  if (need > nv12_buffer_size) {
    GST_ERROR_OBJECT (self,
        "NV12 need %"
        G_GSIZE_FORMAT " bytes but output buffer has %" G_GSIZE_FORMAT, need,
        nv12_buffer_size);
    return FALSE;
  }

  gdma.src_skip_cache_sync = 1;
  gdma.src_mmap_type = IAV_PART_DSP;
  gdma.src_use_phys = 0;
  gdma.src_use_dma_buf_fd = 0;
  gdma.src_offset = yuv->y_addr_offset;
  gdma.dst_use_phys = 1;
  gdma.dst_use_dma_buf_fd = 0;
  gdma.dst_addr = (unsigned long) dst_y_pa;
  gdma.src_pitch = src_pitch;
  gdma.dst_pitch = dst_pitch;
  gdma.width = canvas_w;
  gdma.height = canvas_h;

  if (ioctl (self->iav_ctx->iav_fd, IAV_IOC_GDMA_COPY, &gdma) < 0) {
    GST_ERROR_OBJECT (self, "GDMA Y (dst phys) failed: %s", g_strerror (errno));
    return FALSE;
  }

  if (yuv->format != IAV_YUV_FORMAT_YUV400) {
    gdma.src_offset = yuv->uv_addr_offset;
    gdma.dst_addr =
        (unsigned long) (dst_y_pa + (guint64) dst_pitch * (guint64) al_h);
    gdma.height = canvas_h / 2;
    if (ioctl (self->iav_ctx->iav_fd, IAV_IOC_GDMA_COPY, &gdma) < 0) {
      GST_ERROR_OBJECT (self, "GDMA UV (dst phys) failed: %s", g_strerror (errno));
      return FALSE;
    }
  }

  GST_DEBUG_OBJECT (self,
      "[v2] decode: GDMA ok dst=phys PA=0x%" G_GINT64_MODIFIER "x canvas %ux%u "
      "src_pitch=%u dst_pitch=%u",
      (gint64) dst_y_pa, canvas_w, canvas_h, src_pitch, dst_pitch);
  return TRUE;
}

static gboolean
gdma_nv12_to_cavalry_dmabuf (GstAmbaHwvdecV2 * self, struct iav_yuv_cap *yuv,
    guint canvas_w, guint canvas_h, guint src_pitch, guint dst_pitch,
    gint dst_fd, gsize dst_block_offset, gsize nv12_buffer_size)
{
  struct iav_gdma_copy gdma = { 0 };
  guint al_h = (canvas_h + 15) & ~15u;
  gsize need = (gsize) dst_pitch * (gsize) al_h * 3 / 2;

  if (dst_fd < 0) {
    GST_ERROR_OBJECT (self, "GDMA: invalid cavalry dmabuf fd");
    return FALSE;
  }

  if (src_pitch > dst_pitch) {
    GST_ERROR_OBJECT (self,
        "GDMA: canvas src_pitch %u > Cavalry dst_pitch %u (increase coded width / pool)",
        src_pitch, dst_pitch);
    return FALSE;
  }

  if (need > nv12_buffer_size) {
    GST_ERROR_OBJECT (self,
        "NV12 need %"
        G_GSIZE_FORMAT " bytes but output buffer has %" G_GSIZE_FORMAT, need,
        nv12_buffer_size);
    return FALSE;
  }

  gdma.src_skip_cache_sync = 1;
  gdma.src_mmap_type = IAV_PART_DSP;
  gdma.src_use_phys = 0;
  gdma.src_use_dma_buf_fd = 0;
  gdma.src_offset = yuv->y_addr_offset;
  gdma.dst_use_phys = 0;
  gdma.dst_use_dma_buf_fd = 1;
  gdma.dst_dma_buf_fd = dst_fd;
  gdma.dst_offset = (unsigned int) dst_block_offset;
  gdma.src_pitch = src_pitch;
  gdma.dst_pitch = dst_pitch;
  gdma.width = canvas_w;
  gdma.height = canvas_h;

  if (ioctl (self->iav_ctx->iav_fd, IAV_IOC_GDMA_COPY, &gdma) < 0) {
    GST_ERROR_OBJECT (self, "GDMA Y (dmabuf) failed: %s", g_strerror (errno));
    return FALSE;
  }

  if (yuv->format != IAV_YUV_FORMAT_YUV400) {
    gdma.src_offset = yuv->uv_addr_offset;
    gdma.dst_offset =
        (unsigned int) (dst_block_offset + (gsize) dst_pitch * (gsize) al_h);
    gdma.height = canvas_h / 2;
    if (ioctl (self->iav_ctx->iav_fd, IAV_IOC_GDMA_COPY, &gdma) < 0) {
      GST_ERROR_OBJECT (self, "GDMA UV (dmabuf) failed: %s", g_strerror (errno));
      return FALSE;
    }
  }

  GST_DEBUG_OBJECT (self,
      "[v2] decode: GDMA ok dst=dmabuf cav_fd=%d dst_off=%" G_GSIZE_FORMAT " canvas %ux%u "
      "src_pitch=%u dst_pitch=%u",
      dst_fd, dst_block_offset, canvas_w, canvas_h, src_pitch, dst_pitch);
  return TRUE;
}

/* Prefer dmabuf when fd is valid; otherwise use dst phys (cavalry-phys pool). */
static gboolean
gdma_nv12_to_cavalry_output (GstAmbaHwvdecV2 * self, struct iav_yuv_cap *yuv,
    guint canvas_w, guint canvas_h, guint src_pitch, guint dst_pitch,
    gint cav_fd, gsize block_off, guint64 dst_buf_pa, gsize nv12_buffer_size)
{
  if (cav_fd >= 0)
    return gdma_nv12_to_cavalry_dmabuf (self, yuv, canvas_w, canvas_h, src_pitch,
        dst_pitch, cav_fd, block_off, nv12_buffer_size);
  if (dst_buf_pa != 0)
    return gdma_nv12_to_cavalry_phys (self, yuv, canvas_w, canvas_h, src_pitch,
        dst_pitch, dst_buf_pa, nv12_buffer_size);

  GST_ERROR_OBJECT (self,
      "GDMA: no dst dmabuf fd and no dst phys (fd=%d slab_phys=0x%" G_GINT64_MODIFIER "x)",
      cav_fd, (gint64) dst_buf_pa);
  return FALSE;
}

static GstFlowReturn
decode_h264_and_try_output (GstAmbaHwvdecV2 * self, GstBuffer * in_buf,
    GstVideoCodecFrame * frame, gsize nv12_buffer_size,
    guint (*assign_slot) (GstBuffer *, gpointer), gpointer slot_user,
    GstBuffer * out_buf, guint32 * frame_id_seq, guint * out_w, guint * out_h,
    guint * out_pitch, guint * out_slot, gboolean * produced)
{
  GstMapInfo map;
  guchar *p_data;
  guint size;
  HwvdecV2H264Nalu nalu;
  unsigned char first_nal_type = 0;
  unsigned char *p_check = NULL;
  int append_start_code = 0;
  int ret;
  GstFlowReturn flow = GST_FLOW_OK;
  unsigned int pts_90k = 0;
  GstClockTime provided_pts = GST_CLOCK_TIME_NONE;

  *produced = FALSE;

  if (!gst_buffer_map (in_buf, &map, GST_MAP_READ)) {
    GST_DEBUG_OBJECT (self, "[v2] decode: gst_buffer_map(in_buf) failed");
    return GST_FLOW_ERROR;
  }

  p_data = map.data;
  size = map.size;
  memset (&nalu, 0, sizeof (nalu));

  if (self->codec_format == StreamFormat_H264) {
    if (identify_nalu_avc (p_data, size, self->nal_length_size, &nalu) < 0) {
      gst_buffer_unmap (in_buf, &map);
      return GST_FLOW_OK;
    }
    p_data = nalu.data + nalu.offset;
    size = nalu.size;
    first_nal_type = nalu.type;
    append_start_code = 1;
  } else {
    p_check = nalu_find_first_avc_nal_type (p_data, size, &first_nal_type);
    if (!p_check) {
      gst_buffer_unmap (in_buf, &map);
      return GST_FLOW_OK;
    }
    append_start_code = 0;
  }

  if (first_nal_type == ENalType_SEI || first_nal_type == ENalType_AUD) {
    gst_buffer_unmap (in_buf, &map);
    return GST_FLOW_OK;
  }

  if (first_nal_type == ENalType_SPS) {
    self->p_cur_extradata = self->extradata_buf;
    self->extradata_size = 0;
    if (append_start_code) {
      self->sps_size = size + nalu.sc_length;
      memcpy (self->p_cur_extradata, nalu.start_code, nalu.sc_length);
      memcpy (self->p_cur_extradata + self->sps_size - size, p_data, size);
    } else {
      self->sps_size = size;
      memcpy (self->p_cur_extradata, p_data, size);
    }

    if (self->b_1st_frame) {
      guchar *sps_data = p_data;
      guint sps_data_size = size;
      guint skip_bytes = 0;
      guint sps_width = 0, sps_height = 0;
      gint parse_ret;

      if (size >= 4 && p_data[0] == 0x00 && p_data[1] == 0x00 &&
          p_data[2] == 0x00 && p_data[3] == 0x01)
        skip_bytes = 4;
      else if (size >= 3 && p_data[0] == 0x00 && p_data[1] == 0x00
          && p_data[2] == 0x01)
        skip_bytes = 3;

      if (skip_bytes < size) {
        sps_data = p_data + skip_bytes;
        sps_data_size = size - skip_bytes;
        if (sps_data_size > 1 && (sps_data[0] & 0x1F) == 7) {
          sps_data++;
          sps_data_size--;
          parse_ret =
              get_h264_reso_from_sps (sps_data, sps_data_size, &sps_width,
              &sps_height);
          if (parse_ret == 0 && sps_width > 0 && sps_height > 0) {
            if (create_decoder_slot (self, sps_width, sps_height) < 0) {
              GST_DEBUG_OBJECT (self,
                  "[v2] decode: create_decoder_slot(SPS %ux%u) failed", sps_width,
                  sps_height);
              flow = GST_FLOW_ERROR;
              goto done_map;
            }
            self->width = sps_width;
            self->height = sps_height;
            goto done_map;
          }
        }
      }
      if (create_decoder_slot (self, self->max_coded_width,
              self->max_coded_height) < 0) {
        GST_DEBUG_OBJECT (self,
            "[v2] decode: create_decoder_slot(max_coded %ux%u) failed",
            self->max_coded_width, self->max_coded_height);
        flow = GST_FLOW_ERROR;
        goto done_map;
      }
      self->width = self->max_coded_width;
      self->height = self->max_coded_height;
    }
    goto done_map;
  }

  if (first_nal_type == ENalType_PPS) {
    if (append_start_code) {
      self->pps_size = size + nalu.sc_length;
      memcpy (self->p_cur_extradata + self->sps_size, nalu.start_code,
          nalu.sc_length);
      memcpy (self->p_cur_extradata + self->sps_size + nalu.sc_length, p_data,
          size);
    } else {
      self->pps_size = size;
      memcpy (self->p_cur_extradata + self->sps_size, p_data, size);
    }
    self->extradata_size = self->sps_size + self->pps_size;
    goto done_map;
  }

  if (!self->is_decoder_created || !self->mpBitSreamBufferStart
      || !self->mpBitSreamBufferEnd) {
    flow = GST_FLOW_OK;
    goto done_map;
  }

  if (self->mpBitStreamBufferCurPtr == self->mpBitSreamBufferEnd)
    self->mpBitStreamBufferCurPtr = self->mpBitSreamBufferStart;

  if (frame->pts != GST_CLOCK_TIME_NONE)
    provided_pts = frame->pts;
  else if (GST_BUFFER_PTS_IS_VALID (in_buf))
    provided_pts = GST_BUFFER_PTS (in_buf);

  if (provided_pts != GST_CLOCK_TIME_NONE) {
    pts_90k = (unsigned int) gst_util_uint64_scale (provided_pts,
        self->hwtimer_outfreq, GST_SECOND);
  } else if (self->mFrameRateNum > 0 && self->mFrameRateDen > 0) {
    GstClockTime t = gst_util_uint64_scale (self->mFrameCount,
        GST_SECOND * self->mFrameRateDen, self->mFrameRateNum);
    pts_90k = (unsigned int) gst_util_uint64_scale (t, self->hwtimer_outfreq,
        GST_SECOND);
  } else {
    GST_ERROR_OBJECT (self, "no PTS and no framerate");
    flow = GST_FLOW_ERROR;
    goto done_map;
  }

  self->mDecCmdCtx.decoder_id = self->dec_id;
  self->mDecCmdCtx.num_frames = 1;
  if (!self->mbAutoMapBSB) {
    self->mDecCmdCtx.start_ptr_offset =
        (guint) (gulong) (self->mpBitStreamBufferCurPtr -
        self->mpBitSreamBufferStart);
  } else {
    self->mDecCmdCtx.start_ptr_offset =
        (guint) (gulong) (self->mpBitStreamBufferCurPtr);
  }
  self->mDecCmdCtx.first_frame_display = 0;

  ret = self->iav_ctx->iav_al.f_request_bsb (self->iav_ctx->iav_fd,
    self->dec_id, size + 1024, self->mpBitStreamBufferCurPtr);
  if (ret == DDECODER_STOPPED) {
    flow = GST_FLOW_OK;
    goto done_map;
  } else if (ret < 0) {
    GST_ERROR_OBJECT (self, "f_request_bsb failed: %d", ret);
    flow = GST_FLOW_ERROR;
    goto done_map;
  }

  GST_DEBUG_OBJECT (self,
      "[v2] decode: f_request_bsb ok ret=%d dec_id=%u nal_type=%u bsb_need=%u "
      "cur_ptr=%p",
      ret, (guint) self->dec_id, (guint) first_nal_type,
      (guint) (size + 1024), (gpointer) self->mpBitStreamBufferCurPtr);

  if (first_nal_type == ENalType_IDR) {
    if (self->mbAddAmbaGopHeader) {
      gstUpdateAmbaH264GopHeader (self->mpAmbaGopHeader, pts_90k,
          (unsigned char) self->mCurGopSize);
      self->mpBitStreamBufferCurPtr =
          copy_data_to_bsb (self, self->mpBitStreamBufferCurPtr,
          self->mpAmbaGopHeader, DAMBA_H264_GOP_HEADER_LENGTH);
    }
    self->mpBitStreamBufferCurPtr =
        copy_data_to_bsb (self, self->mpBitStreamBufferCurPtr,
        self->extradata_buf, self->extradata_size);
  }

  if (append_start_code) {
    self->mpBitStreamBufferCurPtr =
        copy_data_to_bsb (self, self->mpBitStreamBufferCurPtr, nalu.start_code,
        nalu.sc_length);
  }
  self->mpBitStreamBufferCurPtr =
      copy_data_to_bsb (self, self->mpBitStreamBufferCurPtr, p_data, size);

  if (!self->mbAutoMapBSB) {
    self->mDecCmdCtx.end_ptr_offset =
        (guint) (gulong) (self->mpBitStreamBufferCurPtr -
        self->mpBitSreamBufferStart);
  } else {
    self->mDecCmdCtx.end_ptr_offset =
        (guint) (gulong) (self->mpBitStreamBufferCurPtr);
  }
  self->mDecCmdCtx.first_frame_display = pts_90k;

  ret =
      self->iav_ctx->iav_al.f_decode (self->iav_ctx->iav_fd, &self->mDecCmdCtx);
  if (ret == DDECODER_STOPPED) {
    flow = GST_FLOW_OK;
    goto done_map;
  } else if (ret < 0) {
    GST_ERROR_OBJECT (self, "f_decode failed: %d", ret);
    flow = GST_FLOW_ERROR;
    goto done_map;
  }

  GST_DEBUG_OBJECT (self,
      "[v2] decode: f_decode ok ret=%d dec_id=%u pts_90k=%u start_off=%u end_off=%u",
      ret, (guint) self->dec_id, pts_90k,
      (guint) self->mDecCmdCtx.start_ptr_offset,
      (guint) self->mDecCmdCtx.end_ptr_offset);

  self->mFrameCount++;
  self->b_1st_frame = FALSE;

  if (!self->canvas_init_done) {
    struct iav_canvas_cfg canvas_cfg;
    memset (&canvas_cfg, 0, sizeof (canvas_cfg));

#if defined (BUILD_DSP_AMBA_V5)
    struct iav_system_resource iav_sys_rsc;
    memset(&iav_sys_rsc, 0, sizeof(struct iav_system_resource));

    iav_sys_rsc.encode_mode = DSP_CURRENT_MODE;
    if (ioctl(self->iav_ctx->iav_fd, IAV_IOC_GET_SYSTEM_RESOURCE, &iav_sys_rsc) < 0) {
        GST_ERROR_OBJECT (self, "IAV_IOC_GET_SYSTEM_RESOURCE failed");
        flow = GST_FLOW_ERROR;
        goto done_map;
    }

    if ((self->canvas_id >= 0) && (self->canvas_id < iav_sys_rsc.canvas_num)) {
      canvas_cfg = iav_sys_rsc.canvas_cfg[self->canvas_id];
    } else {
      GST_ERROR_OBJECT(self, "canvas_id[%d] invalid, should in [0~%d].\n",
        self->canvas_id, iav_sys_rsc.canvas_num-1);
    }
#elif defined (BUILD_DSP_AMBA_V6)
    canvas_cfg.canvas_id = self->canvas_id;
    if (ioctl(self->iav_ctx->iav_fd, IAV_IOC_GET_CANVAS_CONFIG, &canvas_cfg) < 0) {
      GST_ERROR_OBJECT (self, "IAV_IOC_GET_CANVAS_CONFIG failed");
      flow = GST_FLOW_ERROR;
      goto done_map;
    }
#endif

    self->canvas_yuv_disabled = canvas_cfg.disable_yuv_dram;
    self->canvas_init_done = TRUE;
    if (self->canvas_yuv_disabled) {
      GST_ERROR_OBJECT (self, "canvas YUV disabled");
      flow = GST_FLOW_ERROR;
      goto done_map;
    }
  }

  {
    int state = 0;
    struct iav_yuv_cap *yuv = NULL;
    guint slot;
    guint cw, ch, cp;
    gint cav_fd;
    gsize block_off;

    if (ioctl (self->iav_ctx->iav_fd, IAV_IOC_GET_IAV_STATE, &state) < 0) {
      GST_DEBUG_OBJECT (self,
          "[v2] decode: IAV_IOC_GET_IAV_STATE failed: %s", g_strerror (errno));
      flow = GST_FLOW_ERROR;
      goto done_map;
    }
    if (state != IAV_STATE_PREVIEW && state != IAV_STATE_ENCODING) {
      GST_ERROR_OBJECT (self, "IAV state %d, need preview/encoding", state);
      GST_DEBUG_OBJECT (self, "[v2] decode: IAV_IOC_GET_IAV_STATE -> %d", state);
      flow = GST_FLOW_ERROR;
      goto done_map;
    }

    if (!query_canvas_new_picture (self, &yuv)) {
      gst_buffer_unmap (in_buf, &map);
      return GST_FLOW_OK;
    }

    cw = yuv->width;
    ch = yuv->height;
    cp = yuv->pitch;

    self->canvas_out_w = cw;
    self->canvas_out_h = ch;
    self->canvas_out_valid = TRUE;
    self->last_canvas_src_pitch = cp;
    gst_amba_hwvdecv2_refresh_nv12_size (self);

    // g_print ("[amba_hwvdecv2 v2] verify canvas->GDMA dec-id=%u canvas wxh=%ux%u "
    //     "pitch(src)=%u seq=%u format=%d y_off=0x%x uv_off=0x%x | max_coded=%ux%u "
    //     "nv12_y_pitch(dst)=%u gdma_al_h=%u nv12_buffer_size=%" G_GSIZE_FORMAT "\n",
    //     (guint) self->dec_id, cw, ch, cp, (guint) yuv->seq_num, (gint) yuv->format,
    //     (guint) yuv->y_addr_offset, (guint) yuv->uv_addr_offset,
    //     self->max_coded_width, self->max_coded_height, self->nv12_y_pitch,
    //     (ch + 15u) & ~15u, nv12_buffer_size);

    {
      gint cbi = gst_amba_cavalry_buffer_get_block_index (out_buf);

      if (cbi >= 0)
        slot = (guint) cbi;
      else
        slot = assign_slot (out_buf, slot_user);
    }

    cav_fd = gst_amba_cavalry_buffer_get_fd (out_buf);
    block_off = gst_amba_cavalry_buffer_get_block_offset (out_buf);
    {
      guint64 dst_pa = gst_amba_cavalry_buffer_get_slab_phys (out_buf);

      if (cav_fd < 0 && dst_pa == 0) {
        GST_ERROR_OBJECT (self,
            "output buffer has no cavalry dmabuf fd and no slab phys for GDMA dst");
        GST_DEBUG_OBJECT (self,
            "[v2] decode: cav_fd=%d slab_phys=0x%" G_GINT64_MODIFIER "x",
            cav_fd, (gint64) dst_pa);
        flow = GST_FLOW_ERROR;
        goto done_map;
      }
      if (cav_fd < 0) {
        GST_DEBUG_OBJECT (self,
            "[v2] decode: GDMA dst=phys (cav_fd=%d) dst_buf_PA=0x%" G_GINT64_MODIFIER "x",
            cav_fd, (gint64) dst_pa);
      }

      if (gst_buffer_get_size (out_buf) < nv12_buffer_size) {
        GST_ERROR_OBJECT (self, "output buffer too small for NV12");
        flow = GST_FLOW_ERROR;
        goto done_map;
      }

      if (!gdma_nv12_to_cavalry_output (self, yuv, cw, ch, cp, self->nv12_y_pitch,
              cav_fd, block_off, dst_pa, nv12_buffer_size)) {
        GST_DEBUG_OBJECT (self,
            "[v2] decode: GDMA failed (cav_fd=%d dst_PA=0x%" G_GINT64_MODIFIER "x canvas %ux%u "
            "pitch=%u dst_pitch=%u block_off=%" G_GSIZE_FORMAT ")",
            cav_fd, (gint64) dst_pa, cw, ch, cp, self->nv12_y_pitch, block_off);
        flow = GST_FLOW_ERROR;
        goto done_map;
      }
    }

    *out_w = cw;
    *out_h = ch;
    *out_pitch = self->nv12_y_pitch;
    *out_slot = slot;
    (*frame_id_seq)++;
    *produced = TRUE;
  }

done_map:
  gst_buffer_unmap (in_buf, &map);
  return flow;
}

GstFlowReturn
gst_ambahwvdecv2_iav_fill_output (GstAmbaHwvdecV2 * self,
    GstVideoDecoder * decoder, GstVideoCodecFrame * frame, GstBuffer * in_buf,
    GstBuffer * out_buf, gsize nv12_buffer_size,
    guint (*assign_slot) (GstBuffer *, gpointer), gpointer slot_user,
    guint32 * frame_id_seq, guint * out_w, guint * out_h, guint * out_pitch,
    guint * out_slot, gboolean * produced)
{
  g_return_val_if_fail (self && in_buf && out_buf && frame_id_seq && out_w
      && out_h && out_pitch && out_slot && produced && assign_slot,
      GST_FLOW_ERROR);

  *produced = FALSE;
  *out_w = 0;
  *out_h = 0;
  *out_pitch = 0;
  *out_slot = 0;
  (void) decoder;

  if (!self->iav_ctx || !self->iav_pipeline_ready) {
    GST_ERROR_OBJECT (self, "IAV not ready");
    GST_DEBUG_OBJECT (self,
        "[v2] fill_output: IAV not ready (ctx=%p pipeline_ready=%d)",
        (gpointer) self->iav_ctx, self->iav_pipeline_ready ? 1 : 0);
    return GST_FLOW_ERROR;
  }

  if (self->verbose) {
    GST_DEBUG_OBJECT (self, "[v2] fill_output: decode path (NV12, GDMA dst dmabuf or phys)");
  }

  return decode_h264_and_try_output (self, in_buf, frame, nv12_buffer_size,
      assign_slot, slot_user, out_buf, frame_id_seq, out_w, out_h, out_pitch,
      out_slot, produced);
}
