/*
 * iav_al_enc_params.c
 *
 * History:
 *    8/23/2022 - [Zhi He] created file
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

#include "stdlib.h"
#include "stdio.h"
#include "string.h"

#include "common_err_code_c.h"

#include "debug_log.h"
#include "internal.h"

#include "iav_al.h"
#include "mcl.h"

#include "iav_al_enc_params.h"

#include <glib.h>
#include <gst/gst.h>

#ifdef BUILD_MODULE_AMBA_DSP

#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <termios.h>


#include "errno.h"
#include "basetypes.h"
#include "iav_ioctl.h"
#include <sys/ioctl.h>

typedef enum {
  IAV_CBR = 0,
  IAV_VBR,
  IAV_CBR_QUALITY_KEEPING,
  IAV_VBR_QUALITY_KEEPING,
} iav_rate_control_mode;


#if defined (BUILD_DSP_AMBA_V5) || defined (BUILD_DSP_AMBA_V6)


int update_enc_resolution (int iav_fd, enc_resolution_t *reso)
{
  DUNUSED(iav_fd);
  DPRINT_NOTICE("[enc %d]: update resolution\n", reso->enc_index);
  return 0;
}

int update_enc_bitrate (int iav_fd, enc_bitrate_t *bitrate)
{
  DUNUSED(iav_fd);
  DPRINT_NOTICE("[enc %d]: update bitrate\n", bitrate->enc_index);
  return 0;
}

int update_enc_framerate (int iav_fd, enc_framerate_t *framerate)
{
  DUNUSED(iav_fd);
  DPRINT_NOTICE("[enc %d]: update framerate\n", framerate->enc_index);
  return 0;
}

int update_enc_bitrate_frameate
(int iav_fd, enc_bitrate_framerate_t *bitrate_framerate)
{
  DUNUSED(iav_fd);
  DPRINT_NOTICE("[enc %d]: update bitrate & framerate\n", bitrate_framerate->enc_index);
  return 0;
}

int update_enc_codec_type (int iav_fd, enc_codec_type_t *codec_type)
{
  DUNUSED(iav_fd);
  DPRINT_NOTICE("[enc %d]: update codec type\n", codec_type->enc_index);
  return 0;
}

int update_enc_gop_structure (int iav_fd, enc_gop_structure_t *gop_structure)
{
  DUNUSED(iav_fd);
  DPRINT_NOTICE("[enc %d]: update gop structure\n", gop_structure->enc_index);
  return 0;
}

int enc_force_idr (int iav_fd, enc_force_idr_t *force_idr)
{
  struct iav_stream_cfg stream_cfg;
  int ret = 0;

  DPRINT_NOTICE("[enc %d]: force idr\n", force_idr->enc_index);

  memset(&stream_cfg, 0, sizeof(stream_cfg));
  stream_cfg.id = force_idr->enc_index;
  if (force_idr->stream_type == IAV_STREAM_TYPE_H264) {
    stream_cfg.cid = IAV_H264_CFG_FORCE_IDR;
    stream_cfg.arg.h264_force_idr = 1;
  } else {
    stream_cfg.cid = IAV_H265_CFG_FORCE_IDR;
    stream_cfg.arg.h265_force_idr = 1;
  }
  ret = ioctl(iav_fd, IAV_IOC_SET_STREAM_CONFIG, &stream_cfg);
  if (ret) {
    DPRINT_ERROR("[enc %d]: set force idr failed\n", force_idr->enc_index);
  }

  return ret;
}

int sync_frame_force_idr (int iav_fd, enc_force_idr_t *force_idr)
{
  struct iav_stream_cfg stream_cfg;
  int ret = 0;

  DPRINT_NOTICE("[sync_frame %d]: force idr\n", force_idr->enc_index);

  memset(&stream_cfg, 0, sizeof(stream_cfg));
  stream_cfg.id = force_idr->enc_index;

  if (force_idr->stream_type == IAV_STREAM_TYPE_H264) {
    stream_cfg.cid = IAV_H264_CFG_FORCE_IDR;
    stream_cfg.arg.h264_force_idr = 1;
  } else {
    stream_cfg.cid = IAV_H265_CFG_FORCE_IDR;
    stream_cfg.arg.h265_force_idr = 1;
  }

  stream_cfg.strm_sync_type = IAV_FRAME_SYNC;
  stream_cfg.dsp_pts = force_idr->pts;
  ret = ioctl(iav_fd, IAV_IOC_CFG_FRAME_SYNC_PROC, &stream_cfg);
  if (ret) {
    DPRINT_ERROR("[sync_frame %d]: set force idr failed\n", force_idr->enc_index);
  }

  return ret;
}

static int set_encode_format(int iav_fd, enc_config_t *config)
{
  struct iav_stream_cfg stream_cfg;
  struct iav_stream_format *format = NULL;
  int i;

  for (i = 0; i < IAV_STREAM_MAX_NUM_ALL; i++) {
    if (!(config->encode_format_changed_id & (1 << i))) {
      continue;
    }

    memset(&stream_cfg, 0, sizeof(stream_cfg));
    stream_cfg.id = i;
    stream_cfg.cid = IAV_STMCFG_FORMAT;
    AM_IOCTL(iav_fd, IAV_IOC_GET_STREAM_CONFIG, &stream_cfg);

    format = &stream_cfg.arg.format;
    if (config->encode_fmt[i].type_changed_flag) {
      format->type = config->encode_fmt[i].type;
    }
    if (config->encode_fmt[i].resolution_changed_flag) {
      format->enc_win.width = config->encode_fmt[i].width;
      format->enc_win.height = config->encode_fmt[i].height;
    }
    if (config->encode_fmt[i].offset_changed_flag) {
      format->enc_win.x = config->encode_fmt[i].offset_x;
      format->enc_win.y = config->encode_fmt[i].offset_y;
    }
    if (config->encode_fmt[i].source_changed_flag) {
      if (config->encode_fmt[i].source_map_changed_flag) {
        printf("Warning: stream %c: source canvas[%d] and source canvas_map[0x%x] " \
            "cannot be specified at the same time, canvas_map will be ignored.\n",
            'A' + i, format->enc_src_id, format->enc_src_map);
      }
      format->enc_src_id = config->encode_fmt[i].source;
      format->use_enc_src_map = 0;
      format->enc_src_map = 0;
    } else if (config->encode_fmt[i].source_map_changed_flag) {
      format->enc_src_id = 0;
      format->use_enc_src_map = 1;
      format->enc_src_map = config->encode_fmt[i].source_map;
    }
    if (config->encode_fmt[i].duration_flag) {
      format->duration = config->encode_fmt[i].duration;
    }
    if (config->encode_fmt[i].hflip_flag) {
      format->hflip = config->encode_fmt[i].hflip;
    }
    if (config->encode_fmt[i].vflip_flag) {
      format->vflip = config->encode_fmt[i].vflip;
    }
    if (config->encode_fmt[i].rotate_flag) {
      format->rotate_cw = config->encode_fmt[i].rotate;
    }
    if (config->encode_fmt[i].efm_customize_fps_flag) {
      format->efm_customize_fps = config->encode_fmt[i].efm_customize_fps;
    }
    if (config->encode_fmt[i].session_id_flag) {
      format->session_id = config->encode_fmt[i].session_id;
    }
    if (config->encode_fmt[i].fake_avg_pts_flag) {
      format->fake_avg_pts = config->encode_fmt[i].fake_avg_pts;
    }

    AM_IOCTL(iav_fd, IAV_IOC_SET_STREAM_CONFIG, &stream_cfg);
  }
  return 0;
}

// rate control variables
static u32 rc_br_table[11] =
{
  256000,  512000,  768000,  1000000,	1500000, 2000000,
  3000000, 4000000, 6000000, 8000000, 10000000,
};

static u32 rc_reso_table[11] =
{
  1920*1080, 1280*1024, 1280*960, 1280*720, 1024*768,  800*600,
  720*576,  720*480, 640*480,   352*288,   320*240,
};

static u32 h264_rc_qp_for_vbr_lut[11][11] =
{
  {31, 29, 27, 27, 26, 23, 23, 22, 22, 17, 16},	/* 256 kbps */
  {30, 26, 25, 25, 24, 22, 21, 20, 20, 16, 15},	/* 512 kbps */
  {28, 25, 24, 24, 23, 21, 20, 19, 19, 15, 14},	/* 768 kbps */
  {27, 24, 23, 23, 22, 20, 19, 18, 18, 14, 13},	/* 1 Mbps */
  {26, 24, 22, 22, 21, 19, 18, 17, 17, 12, 11},	/* 1.5 Mbps */
  {25, 23, 22, 21, 19, 18, 17, 16, 16, 11, 10},	/* 2 Mbps */
  {24, 22, 21, 20, 19, 17, 16, 15, 15, 9,  8},	/* 3 Mbps */
  {23, 21, 20, 19, 18, 16, 15, 14, 14, 8,  7},	/* 4 Mbps */
  {22, 20, 19, 18, 17, 15, 14, 13, 12, 5,  1},	/* 6 Mbps */
  {21, 19, 18, 17, 16, 14, 13, 12, 11, 1,  1},	/* 8 Mbps */
  {21, 18, 17, 16, 15, 13, 12, 11, 10, 1,  1},	/* 10 Mbps */
};

static u32 h265_rc_qp_for_vbr_lut[11][11] =
{
  {34, 32, 30, 30, 29, 26, 26, 25, 25, 20, 19},	/* 256 kbps */
  {34, 29, 28, 28, 27, 25, 24, 23, 23, 19, 18},	/* 512 kbps */
  {31, 28, 27, 27, 26, 24, 23, 22, 22, 18, 17},	/* 768 kbps */
  {30, 27, 26, 26, 25, 23, 22, 21, 21, 17, 16},	/* 1 Mbps */
  {29, 27, 25, 25, 24, 22, 20, 20, 20, 15, 14},	/* 1.5 Mbps */
  {28, 26, 25, 24, 22, 21, 20, 19, 19, 14, 13},	/* 2 Mbps */
  {27, 25, 24, 23, 22, 20, 19, 18, 18, 12,  11},	/* 3 Mbps */
  {26, 24, 23, 22, 21, 19, 18, 17, 17, 11,  10},	/* 4 Mbps */
  {25, 23, 22, 21, 20, 18, 17, 16, 15, 8,  4},	/* 6 Mbps */
  {24, 22, 21, 20, 19, 17, 16, 15, 14, 4,  4},	/* 8 Mbps */
  {24, 21, 20, 19, 18, 16, 15, 14, 13, 4,  4},	/* 10 Mbps */
};

static int h26x_calc_target_qp(u32 codec, u32 bitrate, u32 resolution)
{
  size_t i = 0, j = 0;

  for (i = 0; i < ARRAY_SIZE(rc_br_table); i++) {
    if (bitrate <= rc_br_table[i])
      break;
  }

  if (i == ARRAY_SIZE(rc_br_table)) {
    printf("Invalid bitrate: %d\n", bitrate);
    return -1;
  }

  for (j = 0; j < ARRAY_SIZE(rc_reso_table); j++) {
    if (resolution >= rc_reso_table[j])
      break;
  }

  if (j == ARRAY_SIZE(rc_reso_table)){
    printf("Invalid resolution: %d\n", resolution);
    return -1;
  }

  if (codec == IAV_STREAM_TYPE_H264) {
    return h264_rc_qp_for_vbr_lut[i][j];
  } else {
    return h265_rc_qp_for_vbr_lut[i][j];
  }
}

static int set_h26x_encode_param(int stream, struct iav_stream_format *format, int fd_iav, enc_config_t *config)
{
  struct iav_h26x_cfg h264cfg;
  struct iav_stream_cfg cfg;
  struct iav_bitrate bitrate;
  struct iav_h26x_gop gop;
  struct iav_zmv_threshold_info *zmv_threshold = NULL;
  struct iav_h26x_pskip *h26x_pskip = NULL;
  h264_param_t *param = &config->encode_param[stream].h264_param;
  enum iav_stream_type type = format->type;
  u32 resolution = 0;
  int qp = 0;
  u16 i;

  memset(&h264cfg, 0, sizeof(h264cfg));
  h264cfg.id = stream;

  if (type == IAV_STREAM_TYPE_H264) {
    AM_IOCTL(fd_iav, IAV_IOC_GET_H264_CONFIG, &h264cfg);
  } else {
    AM_IOCTL(fd_iav, IAV_IOC_GET_H265_CONFIG, &h264cfg);
  }

  if (param->h264_M_flag) {
    h264cfg.M = param->h264_M;
  }

  if (param->h264_N_flag) {
    h264cfg.N = param->h264_N;
  }

  if (param->h264_idr_interval_flag) {
    h264cfg.idr_interval = param->h264_idr_interval;
  }

  if (param->h264_gop_model_flag) {
    h264cfg.gop_structure = param->h264_gop_model;
  }
#if defined (BUILD_DSP_AMBA_V5)
  if (param->h264_svc_extension_enable_flag) {
    h264cfg.svc_extension_enable = param->h264_svc_extension_enable;
  }
#endif
  if (param->wp_mode_flag) {
    h264cfg.wp_mode = param->wp_mode;
  }

  if (param->h264_profile_level_flag) {
    h264cfg.profile = param->h264_profile_level;
  }

#if defined (BUILD_DSP_AMBA_V5) //ndef SDK_VER_LESS_THAN_030011
  if (param->intra_refresh_cycle_flag) {
    h264cfg.intra_refresh_cycle = param->intra_refresh_cycle;
  }
#endif

  if (param->h264_chroma_format_flag) {
    h264cfg.chroma_format = param->h264_chroma_format;
  }

  if (param->h264_zmv_threshold_enable_flag) {
    h264cfg.zmv_enable_flag = param->h264_zmv_threshold_enable;
  }

  if (param->h264_zmv_threshold_qp_offset_flag) {
    h264cfg.zmv_threshold_qp_offset = param->h264_zmv_threshold_qp_offset;
  }

  if (param->h264_fast_seek_intvl_flag) {
    h264cfg.fast_seek_intvl = param->h264_fast_seek_intvl;
  }

  if (param->h264_user1_intra_bias_flag) {
    h264cfg.user1_intra_bias = param->h264_user1_intra_bias;
  }

  if (param->h264_user1_direct_bias_flag) {
    h264cfg.user1_direct_bias = param->h264_user1_direct_bias;
  }

  if (param->h264_user2_intra_bias_flag) {
    h264cfg.user2_intra_bias = param->h264_user2_intra_bias;
  }

  if (param->h264_user2_direct_bias_flag) {
    h264cfg.user2_direct_bias = param->h264_user2_direct_bias;
  }

  // panic mode settings
  if (param->panic_mode_flag) {
    h264cfg.cpb_buf_idc = param->cpb_buf_idc;
    h264cfg.cpb_cmp_idc = param->cpb_cmp_idc;
    h264cfg.en_panic_rc = param->en_panic_rc;
    h264cfg.fast_rc_idc = param->fast_rc_idc;
    h264cfg.cpb_user_size = param->cpb_user_size;
  }

  // h264 syntax settings
  if (param->au_type_flag) {
    h264cfg.au_type = param->au_type;
  }

  if (param->h264_deblocking_filter_alpha_flag) {
    h264cfg.deblocking_filter_alpha = param->h264_deblocking_filter_alpha;
  }

  if (param->h264_deblocking_filter_beta_flag) {
    h264cfg.deblocking_filter_beta = param->h264_deblocking_filter_beta;
  }

  if (param->h264_deblocking_filter_enable_flag) {
    h264cfg.deblocking_filter_enable = param->h264_deblocking_filter_enable;
  }

  if (param->h264_deblocking_filter_alpha_flag ||
      param->h264_deblocking_filter_beta_flag ||
      param->h264_deblocking_filter_enable_flag) {
    h264cfg.dblk_custom_flag = 1;
  }

  if (param->h264_long_start_code_flag) {
    h264cfg.long_start_code = param->h264_long_start_code;
  }

  if (param->h264_ltrs_type_flag) {
    h264cfg.ltrs_type = param->h264_ltrs_type;
  }

  if (param->h264_log2_num_ltrp_per_gop_flag) {
    h264cfg.log2_num_ltrp_per_gop = param->h264_log2_num_ltrp_per_gop;
  }

  if (param->h264_two_ltrs_mode_flag) {
    h264cfg.two_ltrs_mode = param->h264_two_ltrs_mode;
  }

  if (param->h264_two_str_flag) {
    h264cfg.two_str = param->h264_two_str;
  }

  if (param->h264_aqp_type_flag) {
    if (type == IAV_STREAM_TYPE_H265) {
      h264cfg.aqp_type = param->h264_aqp_type;
    } else {
      printf("The aqp type can only be set for HEVC!\n");
      return -1;
    }
  }

  if (param->h264_chroma_qp_offset_flag) {
    if (type == IAV_STREAM_TYPE_H265 || type == IAV_STREAM_TYPE_H264) {
      h264cfg.chroma_qp_offset = param->h264_chroma_qp_offset;
    } else {
      printf("chroma_qp_offset can only be set for H265/H264!\n");
      return -1;
    }
  }

  if (param->h264_one_frm_qp_offset_flag) {
    if (type == IAV_STREAM_TYPE_H265 || type == IAV_STREAM_TYPE_H264) {
      h264cfg.one_frm_qp_offset = param->h264_one_frm_qp_offset;
    } else {
      printf("one_frm_qp_offset can only be set for H265/H264!\n");
      return -1;
    }
  }
#if defined (BUILD_DSP_AMBA_V5)
  if (param->h264_qp_smooth_enable_flag) {
    if (type == IAV_STREAM_TYPE_H265 || type == IAV_STREAM_TYPE_H264) {
      h264cfg.qp_smooth_enable = param->h264_qp_smooth_enable;
    } else {
      printf("qp smooth can only be set for H265/H264!\n");
      return -1;
    }
  }
#endif
  if (type == IAV_STREAM_TYPE_H264) {
    AM_IOCTL(fd_iav, IAV_IOC_SET_H264_CONFIG, &h264cfg);
  } else {
    AM_IOCTL(fd_iav, IAV_IOC_SET_H265_CONFIG, &h264cfg);
  }

  if (param->h264_fast_seek_intvl_flag) {
    memset(&cfg, 0, sizeof(cfg));
    cfg.id = stream;
    if (type == IAV_STREAM_TYPE_H264) {
      cfg.cid = IAV_H264_CFG_FAST_SEEK_INTERVAL;
      AM_IOCTL(fd_iav, IAV_IOC_GET_STREAM_CONFIG, &cfg);
      cfg.arg.h264_fast_seek_interval = param->h264_fast_seek_intvl;
      AM_IOCTL(fd_iav, IAV_IOC_SET_STREAM_CONFIG, &cfg);
    } else {
      cfg.cid = IAV_H265_CFG_FAST_SEEK_INTERVAL;
      AM_IOCTL(fd_iav, IAV_IOC_GET_STREAM_CONFIG, &cfg);
      cfg.arg.h265_fast_seek_interval = param->h264_fast_seek_intvl;
      AM_IOCTL(fd_iav, IAV_IOC_SET_STREAM_CONFIG, &cfg);
    }
  }

  if (param->stream_dummy_latency_flag) {
    memset(&cfg, 0, sizeof(cfg));
    cfg.id = stream;
    cfg.cid = IAV_STMCFG_DUMMY_LATENCY;
    cfg.arg.stream_dummy_latency = param->stream_dummy_latency;
    AM_IOCTL(fd_iav, IAV_IOC_SET_STREAM_CONFIG, &cfg);
  }

  if (param->h264_abs_br_flag) {
    memset(&cfg, 0, sizeof(cfg));
    cfg.id = stream;
    if (type == IAV_STREAM_TYPE_H264) {
      cfg.cid = IAV_H264_CFG_RC_STRATEGY;
      AM_IOCTL(fd_iav, IAV_IOC_GET_STREAM_CONFIG, &cfg);
      cfg.arg.h264_rc_strategy.abs_br_flag = param->h264_abs_br;
      AM_IOCTL(fd_iav, IAV_IOC_SET_STREAM_CONFIG, &cfg);
    } else {
      cfg.cid = IAV_H265_CFG_RC_STRATEGY;
      AM_IOCTL(fd_iav, IAV_IOC_GET_STREAM_CONFIG, &cfg);
      cfg.arg.h265_rc_strategy.abs_br_flag = param->h264_abs_br;
      AM_IOCTL(fd_iav, IAV_IOC_SET_STREAM_CONFIG, &cfg);
    }
  }

  if (param->md_cat_lut_flag) {
    memset(&cfg, 0, sizeof(cfg));
    cfg.id = stream;
    if (type == IAV_STREAM_TYPE_H265) {
      cfg.cid = IAV_H265_CFG_MD_CAT_LUT;
      for (i = 0; i < MD_CAT_MAX_NUM; i++) {
        cfg.arg.h265_md_cat_lut[i] = (u8)param->md_cat_lut[i];
      }
      AM_IOCTL(fd_iav, IAV_IOC_SET_STREAM_CONFIG, &cfg);
    } else {
      printf("MD category type mapping can only be set for HEVC!\n");
      return -1;

    }
  }

  memset(&cfg, 0, sizeof(cfg));
  cfg.id = stream;
  if (type == IAV_STREAM_TYPE_H264) {
    cfg.cid = IAV_H264_CFG_BITRATE;
    AM_IOCTL(fd_iav, IAV_IOC_GET_STREAM_CONFIG, &cfg);
    bitrate = cfg.arg.h264_rc;
  } else {
    cfg.cid = IAV_H265_CFG_BITRATE;
    AM_IOCTL(fd_iav, IAV_IOC_GET_STREAM_CONFIG, &cfg);
    bitrate = cfg.arg.h265_rc;
  }

  resolution = format->enc_win.width * (format->enc_win.height);

  if (param->h264_bitrate_control_flag) {
    if (param->h264_bitrate_control == IAV_CBR ||
      param->h264_bitrate_control == IAV_CBR_QUALITY_KEEPING) {
      if (!param->h264_cbr_bitrate_flag) {
        printf("the cbr bitrate should be set!\n");
        return -1;
      }
    } else {
      if (!param->h264_vbr_bitrate_flag) {
        printf("the vbr bitrate should be set!\n");
        return -1;
      }
    }
  }

  if (param->h264_cbr_stable_br_adjust_flag) {
    if((param->h264_bitrate_control != IAV_CBR) &&
      (param->h264_bitrate_control != IAV_CBR_QUALITY_KEEPING)) {
      printf("cbr stable bitrate fluctuation only support CBR mode!\n");
      return -1;
    }

    if (!param->h264_cbr_bitrate_flag) {
      printf("the cbr bitrate should be set!\n");
      return -1;
    }
  }

  switch (param->h264_bitrate_control) {
  case IAV_CBR:
    bitrate.vbr_setting = IAV_BRC_SCBR;
    bitrate.average_bitrate = param->h264_cbr_avg_bitrate;
    bitrate.cbr_stable_br_adjust = param->h264_cbr_stable_br_adjust;
    bitrate.qp_min_on_I = 14;
    bitrate.qp_max_on_I = 51;
    bitrate.qp_min_on_P = 17;
    bitrate.qp_max_on_P = 51;
    bitrate.qp_min_on_B = 21;
    bitrate.qp_max_on_B = 51;
    bitrate.qp_min_on_Q = 15;
    bitrate.qp_max_on_Q = 51;
    bitrate.skip_flag = 0;
    break;
    case IAV_CBR_QUALITY_KEEPING:
        qp = h26x_calc_target_qp(type, param->h264_cbr_avg_bitrate, resolution);
        if (qp < 0)
            return -1;
        bitrate.vbr_setting = IAV_BRC_SCBR;
        bitrate.average_bitrate = param->h264_cbr_avg_bitrate;
        bitrate.cbr_stable_br_adjust = param->h264_cbr_stable_br_adjust;
        bitrate.qp_min_on_I = 1;
        bitrate.qp_max_on_I = qp * 6 / 5;
        bitrate.qp_min_on_P = 1;
        bitrate.qp_max_on_P = qp * 6 / 5;
        bitrate.qp_min_on_B = 1;
        bitrate.qp_max_on_B = qp * 6 / 5;
        bitrate.qp_min_on_Q = 1;
        bitrate.qp_max_on_Q = qp * 6 / 5;
        bitrate.skip_flag = H264_WITH_FRAME_DROP; // enable frame dropping
        break;
    case IAV_VBR:
        qp = h26x_calc_target_qp(type, param->h264_vbr_min_bitrate, resolution);
        if (qp < 0)
            return -1;
        bitrate.vbr_setting = IAV_BRC_SCBR;
        bitrate.average_bitrate = param->h264_vbr_max_bitrate;
        bitrate.cbr_stable_br_adjust = 0;
        bitrate.qp_min_on_I = qp;
        bitrate.qp_max_on_I = 51;
        bitrate.qp_min_on_P = qp;
        bitrate.qp_max_on_P = 51;
        bitrate.qp_min_on_B = qp;
        bitrate.qp_max_on_B = 51;
        bitrate.qp_min_on_Q = qp;
        bitrate.qp_max_on_Q = 51;
        bitrate.skip_flag = 0;
        break;
    case IAV_VBR_QUALITY_KEEPING:
        qp = h26x_calc_target_qp(type, param->h264_vbr_min_bitrate, resolution);
        if (qp < 0)
            return -1;
        bitrate.vbr_setting = IAV_BRC_SCBR;
        bitrate.average_bitrate = param->h264_vbr_max_bitrate;
        bitrate.cbr_stable_br_adjust = 0;
        bitrate.qp_min_on_I = qp;
        bitrate.qp_max_on_I = qp;
        bitrate.qp_min_on_P = qp;
        bitrate.qp_max_on_P = qp;
        bitrate.qp_min_on_B = qp;
        bitrate.qp_max_on_B = qp;
        bitrate.qp_min_on_Q = qp;
        bitrate.qp_max_on_Q = qp;
        bitrate.skip_flag = H264_WITH_FRAME_DROP; // enable frame dropping
        break;
  default:
    printf("Unknown rate control mode [%d] !\n", param->h264_bitrate_control);
    return -1;
  }

  memset(&cfg, 0, sizeof(cfg));
  cfg.id = stream;
  if (param->h264_bitrate_control_flag
      || param->h264_cbr_bitrate_flag
      || param->h264_vbr_bitrate_flag) {
    if (type == IAV_STREAM_TYPE_H264) {
      cfg.cid = IAV_H264_CFG_BITRATE;
      cfg.arg.h264_rc = bitrate;
    } else {
      cfg.cid = IAV_H265_CFG_BITRATE;
      cfg.arg.h265_rc = bitrate;
    }
    AM_IOCTL(fd_iav, IAV_IOC_SET_STREAM_CONFIG, &cfg);
  }

  memset(&cfg, 0, sizeof(cfg));
  cfg.id = stream;
  if (param->h264_N_flag || param->h264_idr_interval_flag ||
      param->h264_N_update_mode_flag) {
    memset(&gop, 0x0, sizeof(gop));
    gop.id = stream;
    gop.N = h264cfg.N;
    gop.idr_interval = h264cfg.idr_interval;
    gop.is_update_N_to_next_GOP = param->h264_N_update_mode;

    if (type == IAV_STREAM_TYPE_H264) {
      cfg.cid = IAV_H264_CFG_GOP;
      cfg.arg.h264_gop = gop;
    } else {
      cfg.cid = IAV_H265_CFG_GOP;
      cfg.arg.h265_gop = gop;
    }
    AM_IOCTL(fd_iav, IAV_IOC_SET_STREAM_CONFIG, &cfg);
  }

  memset(&cfg, 0, sizeof(cfg));
  cfg.id = stream;
  if (param->h264_slice_num_flag || param->h264_slices_per_info_flag) {
    if (type == IAV_STREAM_TYPE_H265) {
      cfg.cid = IAV_H265_CFG_SLICE;
      cfg.arg.h265_slice.slice_num = param->h264_slice_num;
      cfg.arg.h265_slice.slices_per_info = param->h264_slices_per_info;
    } else {
      cfg.cid = IAV_H264_CFG_SLICE;
      cfg.arg.h264_slice.slice_num = param->h264_slice_num;
      cfg.arg.h264_slice.slices_per_info = param->h264_slices_per_info;
    }
    AM_IOCTL(fd_iav, IAV_IOC_SET_STREAM_CONFIG, &cfg);
  }

  memset(&cfg, 0, sizeof(cfg));
  cfg.id = stream;
  if (param->sar_flag) {
    if (type == IAV_STREAM_TYPE_H265) {
      cfg.cid = IAV_H265_CFG_SAR;
      cfg.arg.h265_sar.sar_width = param->sar_width;
      cfg.arg.h265_sar.sar_height = param->sar_height;
    } else if (type == IAV_STREAM_TYPE_H264) {
      cfg.cid = IAV_H264_CFG_SAR;
      cfg.arg.h264_sar.sar_width = param->sar_width;
      cfg.arg.h264_sar.sar_height = param->sar_height;
    }
    AM_IOCTL(fd_iav, IAV_IOC_SET_STREAM_CONFIG, &cfg);
  }

  memset(&cfg, 0, sizeof(cfg));
  cfg.id = stream;
  zmv_threshold = &cfg.arg.h264_zmv_threshold;
  if (param->h264_zmv_threshold_enable_flag ||
      param->h264_zmv_threshold_qp_offset_flag) {
    if (type == IAV_STREAM_TYPE_H264) {
      cfg.cid = IAV_H264_CFG_ZMV_THRESHOLD_INFO;
      AM_IOCTL(fd_iav, IAV_IOC_GET_STREAM_CONFIG, &cfg);
      if (param->h264_zmv_threshold_enable_flag) {
        zmv_threshold->enable =
          h264cfg.zmv_enable_flag;
      }
      if (param->h264_zmv_threshold_qp_offset_flag) {
        zmv_threshold->qp_offset =
          h264cfg.zmv_threshold_qp_offset;
      }
      AM_IOCTL(fd_iav, IAV_IOC_SET_STREAM_CONFIG, &cfg);
    } else {
      printf("ZMV threshold only works for H.264.\n");
    }
  }

  memset(&cfg, 0, sizeof(cfg));
  cfg.id = stream;
  if (param->h264_chroma_format_flag) {
    cfg.cid = IAV_STMCFG_CHROMA;
    cfg.arg.chroma = h264cfg.chroma_format;
    AM_IOCTL(fd_iav, IAV_IOC_SET_STREAM_CONFIG, &cfg);
  }

  memset(&cfg, 0, sizeof(cfg));
  cfg.id = stream;
  if (param->h264_one_frm_qp_offset_flag) {
    if (type == IAV_STREAM_TYPE_H264) {
      cfg.cid = IAV_H264_CFG_ONE_FRM_QP_OFFSET;
      cfg.arg.h264_one_frm_qp_offset = param->h264_one_frm_qp_offset;
      AM_IOCTL(fd_iav, IAV_IOC_SET_STREAM_CONFIG, &cfg);
    } else {
      cfg.cid = IAV_H265_CFG_ONE_FRM_QP_OFFSET;
      cfg.arg.h265_one_frm_qp_offset = param->h264_one_frm_qp_offset;
      AM_IOCTL(fd_iav, IAV_IOC_SET_STREAM_CONFIG, &cfg);
    }
  }

  memset(&cfg, 0, sizeof(cfg));
  cfg.id = stream;
  if (param->h264_drop_frames_flag) {
    if (type == IAV_STREAM_TYPE_H264) {
      cfg.cid = IAV_H264_CFG_FRAME_DROP;
      cfg.arg.h264_drop_frame.repeat_enable =
        param->h264_frame_drop_repeat_enable;
      cfg.arg.h264_drop_frame.drop_num = param->h264_drop_frames;
    } else {
      cfg.cid = IAV_H265_CFG_FRAME_DROP;
      cfg.arg.h265_drop_frame.repeat_enable =
        param->h264_frame_drop_repeat_enable;
      cfg.arg.h265_drop_frame.drop_num = param->h264_drop_frames;
    }
    AM_IOCTL(fd_iav, IAV_IOC_SET_STREAM_CONFIG, &cfg);
  }

  memset(&cfg, 0, sizeof(cfg));
  cfg.id = stream;
  if (param->h264_user1_intra_bias_flag ||
      param->h264_user1_direct_bias_flag ||
      param->h264_user2_intra_bias_flag ||
      param->h264_user2_direct_bias_flag) {
    if (type == IAV_STREAM_TYPE_H264) {
      cfg.cid = IAV_H264_CFG_ENC_PARAM;
      cfg.arg.h264_enc.id = stream;
      cfg.arg.h264_enc.user1_intra_bias = h264cfg.user1_intra_bias;
      cfg.arg.h264_enc.user1_direct_bias = h264cfg.user1_direct_bias;
      cfg.arg.h264_enc.user2_intra_bias = h264cfg.user2_intra_bias;
      cfg.arg.h264_enc.user2_direct_bias = h264cfg.user2_direct_bias;
    } else {
      cfg.cid = IAV_H265_CFG_ENC_PARAM;
      cfg.arg.h265_enc.id = stream;
      cfg.arg.h265_enc.user1_intra_bias = h264cfg.user1_intra_bias;
      cfg.arg.h265_enc.user1_direct_bias = h264cfg.user1_direct_bias;
      cfg.arg.h265_enc.user2_intra_bias = h264cfg.user2_intra_bias;
      cfg.arg.h265_enc.user2_direct_bias = h264cfg.user2_direct_bias;
    }
    AM_IOCTL(fd_iav, IAV_IOC_SET_STREAM_CONFIG, &cfg);
  }

  memset(&cfg, 0, sizeof(cfg));
  cfg.id = stream;
  if (param->pskip_flag) {
    if (type == IAV_STREAM_TYPE_H264) {
      cfg.cid = IAV_H264_CFG_FORCE_PSKIP;
      h26x_pskip = &cfg.arg.h264_pskip;
    } else {
      cfg.cid = IAV_H265_CFG_FORCE_PSKIP;
      h26x_pskip = &cfg.arg.h265_pskip;
    }
    h26x_pskip->repeat_enable = param->pskip_repeat_enable;
    h26x_pskip->repeat_num =
      (param->pskip_repeat_enable ? param->pskip_repeat_num : 0);
    h26x_pskip->repeat_mode = (h26x_pskip->repeat_num ?
                               param->pskip_repeat_mode : 0);
    AM_IOCTL(fd_iav, IAV_IOC_SET_STREAM_CONFIG, &cfg);
  }

  memset(&cfg, 0, sizeof(cfg));
  cfg.id = stream;
  if (param->h264_skip_strength_flag) {
    if (type == IAV_STREAM_TYPE_H264) {
      printf("Skip strength only works for HEVC.\n");
    } else {
      cfg.cid = IAV_H265_CFG_SKIP_STRENGTH;
      cfg.arg.h265_skip_strength = param->h264_skip_strength;
    }
    AM_IOCTL(fd_iav, IAV_IOC_SET_STREAM_CONFIG, &cfg);
  }

  memset(&cfg, 0, sizeof(cfg));
  cfg.id = stream;
  if (param->h264_disable_cu16_flag ||
      param->h264_disable_cu8_flag ||
      param->h264_cu32_bias_level_flag ||
      param->h264_cu16_bias_level_flag ||
      param->h264_cu8_bias_level_flag) {
    if (type == IAV_STREAM_TYPE_H265) {
      cfg.cid = IAV_H265_CFG_CU_SPLIT;
      if (param->h264_disable_cu16_flag) {
        cfg.arg.h265_cu_split.disable_cu16 = param->h264_disable_cu16;
      }
      if (param->h264_disable_cu8_flag) {
        cfg.arg.h265_cu_split.disable_cu8 = param->h264_disable_cu8;
      }
      if (param->h264_cu32_bias_level_flag) {
        cfg.arg.h265_cu_split.cu32_bias_level = param->h264_cu32_bias_level;
      }
      if (param->h264_cu16_bias_level_flag) {
        cfg.arg.h265_cu_split.cu16_bias_level = param->h264_cu16_bias_level;
      }
      if (param->h264_cu8_bias_level_flag) {
        cfg.arg.h265_cu_split.cu8_bias_level = param->h264_cu8_bias_level;
      }
      AM_IOCTL(fd_iav, IAV_IOC_SET_STREAM_CONFIG, &cfg);
    } else {
      printf("Failed to configure CU split for stream %d, only support "
             "CU split cfg for HEVC stream!\n", stream);
    }
  }

  return 0;
}

static int get_h26x_encode_param(int stream, struct iav_stream_format *format, int fd_iav, enc_config_t *config)
{
  struct iav_h26x_cfg h264cfg;
  struct iav_stream_cfg cfg;
  struct iav_bitrate bitrate;
  h264_param_t *param = &config->encode_param[stream].h264_param;
  enum iav_stream_type type = format->type;
  u16 i;

  memset(&h264cfg, 0, sizeof(h264cfg));
  h264cfg.id = stream;

  if (type == IAV_STREAM_TYPE_H264) {
    AM_IOCTL(fd_iav, IAV_IOC_GET_H264_CONFIG, &h264cfg);
  } else {
    AM_IOCTL(fd_iav, IAV_IOC_GET_H265_CONFIG, &h264cfg);
  }

  param->h264_M= h264cfg.M;

  param->h264_N = h264cfg.N;

  param->h264_idr_interval = h264cfg.idr_interval;

  param->h264_gop_model = h264cfg.gop_structure;
#if defined (BUILD_DSP_AMBA_V5)
  param->h264_svc_extension_enable = h264cfg.svc_extension_enable;
#endif
  param->wp_mode = h264cfg.wp_mode;

  param->h264_profile_level = h264cfg.profile;

#if defined (BUILD_DSP_AMBA_V5)
#ifndef SDK_VER_LESS_THAN_030011
  param->intra_refresh_cycle = h264cfg.intra_refresh_cycle;
#endif
#endif

  param->h264_chroma_format = h264cfg.chroma_format;

  param->h264_zmv_threshold_enable = h264cfg.zmv_enable_flag;

  param->h264_zmv_threshold_qp_offset = h264cfg.zmv_threshold_qp_offset;

  param->h264_fast_seek_intvl = h264cfg.fast_seek_intvl;

  param->h264_user1_intra_bias = h264cfg.user1_intra_bias;

  param->h264_user1_direct_bias = h264cfg.user1_direct_bias;

  param->h264_user2_intra_bias = h264cfg.user2_intra_bias;

  param->h264_user2_direct_bias = h264cfg.user2_direct_bias;

  // panic mode settings
  param->cpb_buf_idc = h264cfg.cpb_buf_idc;
  param->cpb_cmp_idc = h264cfg.cpb_cmp_idc;
  param->en_panic_rc = h264cfg.en_panic_rc;
  param->fast_rc_idc = h264cfg.fast_rc_idc;
  param->cpb_user_size = h264cfg.cpb_user_size;

  // h264 syntax settings
  param->au_type = h264cfg.au_type;

  param->h264_deblocking_filter_alpha = h264cfg.deblocking_filter_alpha;

  param->h264_deblocking_filter_beta = h264cfg.deblocking_filter_beta;

  param->h264_deblocking_filter_enable = h264cfg.deblocking_filter_enable;


  param->h264_long_start_code = h264cfg.long_start_code;

  param->h264_ltrs_type = h264cfg.ltrs_type;

  param->h264_log2_num_ltrp_per_gop = h264cfg.log2_num_ltrp_per_gop;

  param->h264_two_ltrs_mode = h264cfg.two_ltrs_mode;

  param->h264_two_str = h264cfg.two_str;

  if (type == IAV_STREAM_TYPE_H265) {
    param->h264_aqp_type = h264cfg.aqp_type;
  }

  if (type == IAV_STREAM_TYPE_H265 || type == IAV_STREAM_TYPE_H264) {
    param->h264_chroma_qp_offset = h264cfg.chroma_qp_offset;
  }

  if (type == IAV_STREAM_TYPE_H265 || type == IAV_STREAM_TYPE_H264) {
    param->h264_one_frm_qp_offset = h264cfg.one_frm_qp_offset;
  }
#if defined (BUILD_DSP_AMBA_V5)
  if (type == IAV_STREAM_TYPE_H265 || type == IAV_STREAM_TYPE_H264) {
    param->h264_qp_smooth_enable = h264cfg.qp_smooth_enable;
  }
#endif

  memset(&cfg, 0, sizeof(cfg));
  cfg.id = stream;
  if (type == IAV_STREAM_TYPE_H264) {
    cfg.cid = IAV_H264_CFG_FAST_SEEK_INTERVAL;
    AM_IOCTL(fd_iav, IAV_IOC_GET_STREAM_CONFIG, &cfg);
    param->h264_fast_seek_intvl = cfg.arg.h264_fast_seek_interval;
  } else {
    cfg.cid = IAV_H265_CFG_FAST_SEEK_INTERVAL;
    AM_IOCTL(fd_iav, IAV_IOC_GET_STREAM_CONFIG, &cfg);
    param->h264_fast_seek_intvl = cfg.arg.h265_fast_seek_interval;
  }

  memset(&cfg, 0, sizeof(cfg));
  cfg.id = stream;
  cfg.cid = IAV_STMCFG_DUMMY_LATENCY;
  AM_IOCTL(fd_iav, IAV_IOC_GET_STREAM_CONFIG, &cfg);
  param->stream_dummy_latency = cfg.arg.stream_dummy_latency;

  memset(&cfg, 0, sizeof(cfg));
  cfg.id = stream;
  if (type == IAV_STREAM_TYPE_H264) {
    cfg.cid = IAV_H264_CFG_RC_STRATEGY;
    AM_IOCTL(fd_iav, IAV_IOC_GET_STREAM_CONFIG, &cfg);
    param->h264_abs_br = cfg.arg.h264_rc_strategy.abs_br_flag;
  } else {
    cfg.cid = IAV_H265_CFG_RC_STRATEGY;
    AM_IOCTL(fd_iav, IAV_IOC_GET_STREAM_CONFIG, &cfg);
    param->h264_abs_br = cfg.arg.h265_rc_strategy.abs_br_flag;
  }

  if (type == IAV_STREAM_TYPE_H265) {
    memset(&cfg, 0, sizeof(cfg));
    cfg.id = stream;
    cfg.cid = IAV_H265_CFG_MD_CAT_LUT;
    AM_IOCTL(fd_iav, IAV_IOC_GET_STREAM_CONFIG, &cfg);
    for (i = 0; i < MD_CAT_MAX_NUM; i++) {
      param->md_cat_lut[i] = (int) cfg.arg.h265_md_cat_lut[i];
    }
  }

  memset(&cfg, 0, sizeof(cfg));
  cfg.id = stream;
  if (type == IAV_STREAM_TYPE_H264) {
    cfg.cid = IAV_H264_CFG_BITRATE;
    AM_IOCTL(fd_iav, IAV_IOC_GET_STREAM_CONFIG, &cfg);
    bitrate = cfg.arg.h264_rc;
  } else {
    cfg.cid = IAV_H265_CFG_BITRATE;
    AM_IOCTL(fd_iav, IAV_IOC_GET_STREAM_CONFIG, &cfg);
    bitrate = cfg.arg.h265_rc;
  }

  param->h264_bitrate_control = bitrate.vbr_setting;

  switch (bitrate.vbr_setting) {
    case IAV_CBR:
    case IAV_CBR_QUALITY_KEEPING:
      param->h264_cbr_avg_bitrate = bitrate.average_bitrate;
      param->h264_cbr_stable_br_adjust = bitrate.cbr_stable_br_adjust;
      break;
    case IAV_VBR:
    case IAV_VBR_QUALITY_KEEPING:
      param->h264_vbr_max_bitrate = bitrate.average_bitrate;
      param->h264_cbr_stable_br_adjust = 0;
        break;
    default:
      param->h264_cbr_avg_bitrate = bitrate.average_bitrate;
      param->h264_cbr_stable_br_adjust = bitrate.cbr_stable_br_adjust;
      break;
  }

  memset(&cfg, 0, sizeof(cfg));
  cfg.id = stream;
  if (type == IAV_STREAM_TYPE_H265) {
    cfg.cid = IAV_H265_CFG_SLICE;
    AM_IOCTL(fd_iav, IAV_IOC_GET_STREAM_CONFIG, &cfg);
    param->h264_slice_num = cfg.arg.h265_slice.slice_num;
    param->h264_slices_per_info = cfg.arg.h265_slice.slices_per_info;
  } else {
    cfg.cid = IAV_H264_CFG_SLICE;
    AM_IOCTL(fd_iav, IAV_IOC_GET_STREAM_CONFIG, &cfg);
    param->h264_slice_num = cfg.arg.h264_slice.slice_num;
    param->h264_slices_per_info = cfg.arg.h264_slice.slices_per_info;
  }

  memset(&cfg, 0, sizeof(cfg));
  cfg.id = stream;
  if (type == IAV_STREAM_TYPE_H265) {
    cfg.cid = IAV_H265_CFG_SAR;
    AM_IOCTL(fd_iav, IAV_IOC_GET_STREAM_CONFIG, &cfg);
    param->sar_width = cfg.arg.h265_sar.sar_width;
    param->sar_height = cfg.arg.h265_sar.sar_height;
  } else if (type == IAV_STREAM_TYPE_H264) {
    cfg.cid = IAV_H264_CFG_SAR;
    AM_IOCTL(fd_iav, IAV_IOC_GET_STREAM_CONFIG, &cfg);
    param->sar_width = cfg.arg.h264_sar.sar_width;
    param->sar_height = cfg.arg.h264_sar.sar_height;
  }

  memset(&cfg, 0, sizeof(cfg));
  cfg.id = stream;
  if (type == IAV_STREAM_TYPE_H264) {
    cfg.cid = IAV_H264_CFG_ONE_FRM_QP_OFFSET;
    AM_IOCTL(fd_iav, IAV_IOC_GET_STREAM_CONFIG, &cfg);
    param->h264_one_frm_qp_offset = cfg.arg.h264_one_frm_qp_offset;
  } else {
    cfg.cid = IAV_H265_CFG_ONE_FRM_QP_OFFSET;
    AM_IOCTL(fd_iav, IAV_IOC_GET_STREAM_CONFIG, &cfg);
    param->h264_one_frm_qp_offset = cfg.arg.h265_one_frm_qp_offset;
  }

  memset(&cfg, 0, sizeof(cfg));
  cfg.id = stream;
  if (type == IAV_STREAM_TYPE_H264) {
    cfg.cid = IAV_H264_CFG_FRAME_DROP;
    AM_IOCTL(fd_iav, IAV_IOC_GET_STREAM_CONFIG, &cfg);
    param->h264_frame_drop_repeat_enable = cfg.arg.h264_drop_frame.repeat_enable;
    param->h264_drop_frames = cfg.arg.h264_drop_frame.drop_num;
  } else {
    cfg.cid = IAV_H265_CFG_FRAME_DROP;
    AM_IOCTL(fd_iav, IAV_IOC_GET_STREAM_CONFIG, &cfg);
    param->h264_frame_drop_repeat_enable = cfg.arg.h265_drop_frame.repeat_enable;
    param->h264_drop_frames = cfg.arg.h265_drop_frame.drop_num;
  }

  memset(&cfg, 0, sizeof(cfg));
  cfg.id = stream;
  if (type == IAV_STREAM_TYPE_H265) {
    cfg.cid = IAV_H265_CFG_SKIP_STRENGTH;
    AM_IOCTL(fd_iav, IAV_IOC_GET_STREAM_CONFIG, &cfg);
    param->h264_skip_strength = cfg.arg.h265_skip_strength;
  }

  memset(&cfg, 0, sizeof(cfg));
  cfg.id = stream;
  if (type == IAV_STREAM_TYPE_H265) {
    cfg.cid = IAV_H265_CFG_CU_SPLIT;
    AM_IOCTL(fd_iav, IAV_IOC_GET_STREAM_CONFIG, &cfg);
    param->h264_disable_cu16 = cfg.arg.h265_cu_split.disable_cu16;
    param->h264_disable_cu8 = cfg.arg.h265_cu_split.disable_cu8;
    param->h264_cu32_bias_level = cfg.arg.h265_cu_split.cu32_bias_level;
    param->h264_cu16_bias_level = cfg.arg.h265_cu_split.cu16_bias_level;
    param->h264_cu8_bias_level = cfg.arg.h265_cu_split.cu8_bias_level;
  }

  return 0;
}

static int set_mjpeg_encode_param(int stream, int fd_iav, enc_config_t *config)
{
  struct iav_mjpeg_cfg mjpeg_cfg;
  struct iav_queryinfo query_info;
  struct iav_stream_info *stream_info;
  struct iav_stream_cfg stream_cfg;
  jpeg_param_t * param = &config->encode_param[stream].jpeg_param;

  memset(&query_info, 0, sizeof(query_info));
  query_info.qid = IAV_INFO_STREAM;
  stream_info = &query_info.arg.stream;
  stream_info->id = stream;
  AM_IOCTL(fd_iav, IAV_IOC_QUERY_INFO, &query_info);

  if (stream_info->state != IAV_STREAM_STATE_ENCODING) {
    memset(&mjpeg_cfg, 0, sizeof(mjpeg_cfg));
    mjpeg_cfg.id = stream;
    AM_IOCTL(fd_iav, IAV_IOC_GET_MJPEG_CONFIG, &mjpeg_cfg);
    if (param->quality_changed_flag)
      mjpeg_cfg.quality = param->quality;
    if (param->jpeg_chroma_format_flag)
      mjpeg_cfg.chroma_format = param->jpeg_chroma_format;
    if (param->restart_interval_flag)
      mjpeg_cfg.restart_interval = param->restart_interval;
    if (param->jpeg_slice_num_flag)
      mjpeg_cfg.slice_num = param->jpeg_slice_num;
    AM_IOCTL(fd_iav, IAV_IOC_SET_MJPEG_CONFIG, &mjpeg_cfg);
  } else {
    if (param->quality_changed_flag) {
      memset(&stream_cfg, 0, sizeof(stream_cfg));
      stream_cfg.id = stream;
      stream_cfg.cid = IAV_MJPEG_CFG_QUALITY;
      stream_cfg.arg.mjpeg_quality = param->quality;
      AM_IOCTL(fd_iav, IAV_IOC_SET_STREAM_CONFIG, &stream_cfg);
    }
    if (param->jpeg_chroma_format_flag) {
      memset(&stream_cfg, 0, sizeof(stream_cfg));
      stream_cfg.id = stream;
      stream_cfg.cid = IAV_STMCFG_CHROMA;
      stream_cfg.arg.chroma = param->jpeg_chroma_format;
      AM_IOCTL(fd_iav, IAV_IOC_SET_STREAM_CONFIG, &stream_cfg);
    }
    if (param->jpeg_drop_frames_flag) {
      if (param->jpeg_frame_drop_repeat_enable) {
        printf("Jmpeg unsupport the frame drop repeat!\n");
        return -1;
      }
      memset(&stream_cfg, 0, sizeof(stream_cfg));
      stream_cfg.cid = IAV_MJPEG_CFG_FRAME_DROP;
      stream_cfg.id = stream;
      stream_cfg.arg.mjpeg_drop_frame.drop_num = param->jpeg_drop_frames;
      AM_IOCTL(fd_iav, IAV_IOC_SET_STREAM_CONFIG, &stream_cfg);
    }
  }

  return 0;
}

static int get_mjpeg_encode_param(int stream, int fd_iav, enc_config_t *config)
{
  struct iav_mjpeg_cfg mjpeg_cfg = {0};
  struct iav_stream_cfg stream_cfg = {0};
  struct iav_mjpeg_drop_frame mjpeg_drop_frame = {0};
  jpeg_param_t * param = &config->encode_param[stream].jpeg_param;

  mjpeg_cfg.id = stream;
  AM_IOCTL(fd_iav, IAV_IOC_GET_MJPEG_CONFIG, &mjpeg_cfg);

  param->quality = mjpeg_cfg.quality;
  param->jpeg_chroma_format = mjpeg_cfg.chroma_format;
  param->restart_interval = mjpeg_cfg.restart_interval;
  param->jpeg_slice_num = mjpeg_cfg.slice_num;

  stream_cfg.id = stream;
  stream_cfg.cid = IAV_MJPEG_CFG_FRAME_DROP;
  AM_IOCTL(fd_iav, IAV_IOC_GET_STREAM_CONFIG, &stream_cfg);
  mjpeg_drop_frame = stream_cfg.arg.mjpeg_drop_frame;

  param->jpeg_drop_frames = mjpeg_drop_frame.drop_num;
  param->jpeg_frame_drop_repeat_enable = mjpeg_drop_frame.repeat_enable;

  return 0;
}

static int set_encode_param(int iav_fd, enc_config_t *config)
{
  struct iav_stream_cfg cfg;
  struct iav_stream_format *format = NULL;
  int i;

  for (i = 0; i < IAV_STREAM_MAX_NUM_ALL; i++) {
    if (!(config->encode_param_changed_id & (1 << i))) {
      continue;
    }
    memset(&cfg, 0x0, sizeof(cfg));
    cfg.id = i;
    cfg.cid = IAV_STMCFG_FORMAT;
    AM_IOCTL(iav_fd, IAV_IOC_GET_STREAM_CONFIG, &cfg);
    format = &cfg.arg.format;
    switch (format->type) {
      case IAV_STREAM_TYPE_H265:
        /* TODO: Switch to H265 encode param */
        /* Fall through */
      case IAV_STREAM_TYPE_H264:
        if (set_h26x_encode_param(i, format, iav_fd, config) < 0) {
          printf("set_h26x_encode_param failed\n");
          return -1;
        }
        break;
      case IAV_STREAM_TYPE_MJPEG:
        if (set_mjpeg_encode_param(i, iav_fd, config) < 0) {
          printf("set_mjpeg_encode_param failed\n");
          return -1;
        }
        break;
      default:
        printf("Please specify stream type (H.264, H.265) first "
               "for stream %c!\n", 'A' + i);
        return -1;
        break;
    }
  }

  return 0;
}

static int start_encode(u32 stream_map, int fd_iav)
{
  struct iav_queryinfo query_info;
  struct iav_stream_info *stream_info = NULL;
  int i;

  for (i = 0; i < IAV_STREAM_MAX_NUM_ALL; i++) {
    if (stream_map & (1 << i)) {
      memset(&query_info, 0, sizeof(query_info));
      query_info.qid = IAV_INFO_STREAM;
      stream_info = &query_info.arg.stream;
      stream_info->id = i;
      AM_IOCTL(fd_iav, IAV_IOC_QUERY_INFO, &query_info);
      if (stream_info->state == IAV_STREAM_STATE_ENCODING) {
        stream_map &= ~(1 << i);
      }
    }
  }
  if (stream_map == 0) {
    printf("already in encoding, nothing to do \n");
    return 0;
  }

  AM_IOCTL(fd_iav, IAV_IOC_START_ENCODE, stream_map);

  printf("Start encoding for stream 0x%x successfully\n", stream_map);
  return 0;
}

/* this function will get encode state, if it's encoding, then stop it, otherwise, return 0 and do nothing */
int stop_encode(int fd_iav, u32 streamid)
{
  struct iav_queryinfo query_info;
  struct iav_stream_info *stream_info;
  u32 stop_streamid = streamid;
  u32 abort_streamid = streamid;
  int i;

  /* try to stop stream first */
  for (i = 0; i < IAV_STREAM_MAX_NUM_ALL; ++i) {
    if (stop_streamid & (1 << i)) {
      memset(&query_info, 0, sizeof(query_info));
      query_info.qid = IAV_INFO_STREAM;
      stream_info = &query_info.arg.stream;
      stream_info->id = i;
      AM_IOCTL(fd_iav, IAV_IOC_QUERY_INFO, &query_info);
      if (stream_info->state != IAV_STREAM_STATE_ENCODING) {
        stop_streamid &= ~(1 << i);
      }
    }
  }
  if (stop_streamid) {
    printf("Stop encoding for stream 0x%x \n", stop_streamid);
    AM_IOCTL(fd_iav, IAV_IOC_STOP_ENCODE, stop_streamid);
  }

  /* force abort stream if it is in stopping or starting status */
  for (i = 0; i < IAV_STREAM_MAX_NUM_ALL; ++i) {
    if (abort_streamid & (1 << i)) {
      memset(&query_info, 0, sizeof(query_info));
      query_info.qid = IAV_INFO_STREAM;
      stream_info = &query_info.arg.stream;
      stream_info->id = i;
      AM_IOCTL(fd_iav, IAV_IOC_QUERY_INFO, &query_info);
      if (stream_info->state != IAV_STREAM_STATE_STARTING &&
          stream_info->state != IAV_STREAM_STATE_STOPPING) {
        abort_streamid &= ~(1 << i);
      }
    }
  }
  if (abort_streamid) {
    printf("Abort encoding for stream 0x%x \n", abort_streamid);
    AM_IOCTL(fd_iav, IAV_IOC_ABORT_ENCODE, abort_streamid);
  }

  return 0;
}

static int abort_encode(u32 streamid, int fd_iav)
{
  printf("abort encoding for stream 0x%x \n", streamid);
  AM_IOCTL(fd_iav, IAV_IOC_ABORT_ENCODE, streamid);

  return 0;
}

static u32 get_canvas_fps(struct iav_canvas_cfg *canvas, u8 enable_hp_fps)
{
  return enable_hp_fps ? canvas->frame_rate_hp : canvas->frame_rate;
}

static int change_frame_rate(int fd_iav, enc_config_t *config)
{
  struct iav_stream_cfg streamcfg, cfg;
  struct iav_system_resource resource;
  struct iav_stream_format *format = NULL;
  int i, j, flag = 0;
  char str_fps[32] = {0};
  u32 fps = 0;
  u32 hp_factor = 1;

  memset(&resource, 0, sizeof(resource));
  resource.encode_mode = DSP_CURRENT_MODE;
  AM_IOCTL(fd_iav, IAV_IOC_GET_SYSTEM_RESOURCE, &resource);
  if (resource.enable_hp_fps) {
    hp_factor = IAV_HP_FPS_FACTOR;
  }

  for (i = 0; i < IAV_STREAM_MAX_NUM_ALL; i++) {

    if ((config->framerate_factor_changed_id & (1 << i))
      || ((config->stream_abs_fps_enabled_id & (1 << i)))
      || (config->frame_rate_update_mode_changed_id & (1 << i))
      || (config->stream_abs_fps_changed_id & (1 << i))) {
      memset(&streamcfg, 0, sizeof(streamcfg));
      streamcfg.id = i;
      streamcfg.cid = IAV_STMCFG_FPS;
      AM_IOCTL(fd_iav, IAV_IOC_GET_STREAM_CONFIG, &streamcfg);
    } else {
      continue;
    }
    if (config->framerate_factor_changed_id & (1 << i)) {
      if ((config->stream_abs_fps_enabled_id & (1 << i))) {
        if (config->stream_abs_fps_enable[i]) {
          printf("Error! cannot set frame factor for stream [%d] when abs fps enabled.\n", i);
          return -1;
        }
      } else if (streamcfg.arg.fps.abs_fps_enable) {
        printf("Error! cannot set frame factor for stream [%d] when abs fps enabled.\n", i);
        return -1;
      } else {
        /* do nothing */
      }
      streamcfg.arg.fps.fps_multi = config->framerate_factor[i][0];
      streamcfg.arg.fps.fps_div = config->framerate_factor[i][1];

      memset(&cfg, 0, sizeof(cfg));
      cfg.id = i;
      cfg.cid = IAV_STMCFG_FORMAT;
      AM_IOCTL(fd_iav, IAV_IOC_GET_STREAM_CONFIG, &cfg);

      format = &cfg.arg.format;
      fps = 0;

#if defined (BUILD_DSP_AMBA_V5)
      if (!format->use_enc_src_map) {
        fps = get_canvas_fps(&resource.canvas_cfg[format->enc_src_id],
                             resource.enable_hp_fps);
      } else {
        for (j = 0; j < resource.canvas_num; j++) {
          if (((1 << j) & format->enc_src_map) && (!resource.canvas_cfg[j].is_broken)) {
            fps += get_canvas_fps(&resource.canvas_cfg[j],
                resource.enable_hp_fps);
          }
        }
      }
#elif defined (BUILD_DSP_AMBA_V6)
      struct iav_canvas_cfg canvas_cfg;
      memset(&canvas_cfg, 0, sizeof(canvas_cfg));

      if (!format->use_enc_src_map) {
        canvas_cfg.canvas_id = format->enc_src_id;
        AM_IOCTL(fd_iav, IAV_IOC_GET_CANVAS_CONFIG, &canvas_cfg);
        fps = get_canvas_fps(&canvas_cfg,
                             resource.enable_hp_fps);
      } else {
        for (j = 0; j < resource.canvas_num; j++) {
          canvas_cfg.canvas_id = j;
          AM_IOCTL(fd_iav, IAV_IOC_GET_CANVAS_CONFIG, &canvas_cfg);
          if ((1 << j) & format->enc_src_map) {
            fps += get_canvas_fps(&canvas_cfg,
                resource.enable_hp_fps);
          }
        }
      }

#endif
      snprintf(str_fps, sizeof(str_fps), "%f", (float)fps / hp_factor);

      if (!format->use_enc_src_map) {
        printf("The current canvas[%d] fps is %s, stream[%d] encoding fps will be %s x %d/%d!\n",
               format->enc_src_id, str_fps, i, str_fps, config->framerate_factor[i][0], config->framerate_factor[i][1]);
      } else {
        printf("The current canvas_map[0x%x] total fps is %s, stream[%d] encoding fps will be %s x %d/%d!\n",
               format->enc_src_map, str_fps, i, str_fps, config->framerate_factor[i][0], config->framerate_factor[i][1]);
      }
      flag = 1;
    }
    if (config->frame_rate_update_mode_changed_id & (1 << i)) {
      streamcfg.arg.fps.is_update_frame_rate_to_next_GOP = config->frame_rate_update_mode[i];
      flag = 1;
    }

    if (config->stream_abs_fps_enabled_id & (1 << i)) {
      if (config->stream_abs_fps_enable[i] && config->stream_abs_fps[i] == 0) {
        printf("Error! abs fps isn't set when abs fps enabled for stream [%d].\n", i);
        return -1;
      }
      streamcfg.arg.fps.abs_fps_enable = config->stream_abs_fps_enable[i];
      flag = 1;
    }
    if (config->stream_abs_fps_changed_id & (1 << i)) {
      if (streamcfg.arg.fps.abs_fps_enable || config->stream_abs_fps_enable[i]) {
        if (resource.enable_hp_fps) {
          streamcfg.arg.fps.abs_fps_hp = config->stream_abs_fps[i] * hp_factor;
        } else {
          streamcfg.arg.fps.abs_fps = config->stream_abs_fps[i];
        }
        flag = 1;
      } /*else {
        printf("Error! cannot set abs fps when abs fps disabled for stream[%d].\n", i);
        return -1;
      }*/
    }

    if (flag == 1) {
      flag = 0;
      AM_IOCTL(fd_iav, IAV_IOC_SET_STREAM_CONFIG, &streamcfg);
    }
  }
  return 0;
}

int get_frame_rate(int fd_iav, enc_config_t *config, int i)
{
  struct iav_stream_cfg streamcfg, cfg;
  struct iav_system_resource resource;
  struct iav_stream_format *format = NULL;
  int j;
  u32 fps = 0;
  u32 hp_factor = 1;

  memset(&resource, 0, sizeof(resource));
  resource.encode_mode = DSP_CURRENT_MODE;
  AM_IOCTL(fd_iav, IAV_IOC_GET_SYSTEM_RESOURCE, &resource);
  if (resource.enable_hp_fps) {
    hp_factor = IAV_HP_FPS_FACTOR;
  }

  memset(&streamcfg, 0, sizeof(streamcfg));
  streamcfg.id = i;
  streamcfg.cid = IAV_STMCFG_FPS;
  AM_IOCTL(fd_iav, IAV_IOC_GET_STREAM_CONFIG, &streamcfg);

  config->framerate_factor[i][0] = streamcfg.arg.fps.fps_multi;
  config->framerate_factor[i][1] = streamcfg.arg.fps.fps_div;

  memset(&cfg, 0, sizeof(cfg));
  cfg.id = i;
  cfg.cid = IAV_STMCFG_FORMAT;
  AM_IOCTL(fd_iav, IAV_IOC_GET_STREAM_CONFIG, &cfg);

  format = &cfg.arg.format;
  fps = 0;
#if defined (BUILD_DSP_AMBA_V5)
  if (!format->use_enc_src_map) {
    fps = get_canvas_fps(&resource.canvas_cfg[format->enc_src_id],
                         resource.enable_hp_fps);
  } else {
    for (j = 0; j < resource.canvas_num; j++) {
      if (((1 << j) & format->enc_src_map) && (!resource.canvas_cfg[j].is_broken)) {
        fps += get_canvas_fps(&resource.canvas_cfg[j],
            resource.enable_hp_fps);
      }
    }
  }
#elif defined (BUILD_DSP_AMBA_V6)
  struct iav_canvas_cfg canvas_cfg;
  memset(&canvas_cfg, 0, sizeof(canvas_cfg));

  if (!format->use_enc_src_map) {
    canvas_cfg.canvas_id = format->enc_src_id;
    AM_IOCTL(fd_iav, IAV_IOC_GET_CANVAS_CONFIG, &canvas_cfg);
    fps = get_canvas_fps(&canvas_cfg,
                         resource.enable_hp_fps);
  } else {
    for (j = 0; j < resource.canvas_num; j++) {
      canvas_cfg.canvas_id = j;
      AM_IOCTL(fd_iav, IAV_IOC_GET_CANVAS_CONFIG, &canvas_cfg);
      if ((1 << j) & format->enc_src_map) {
        fps += get_canvas_fps(&canvas_cfg,
            resource.enable_hp_fps);
      }
    }
  }

#endif

  if (resource.enable_hp_fps) {
    config->stream_fps[i] = fps / hp_factor
      * streamcfg.arg.fps.fps_multi / streamcfg.arg.fps.fps_div;
  } else {
    config->stream_fps[i] = fps * streamcfg.arg.fps.fps_multi / streamcfg.arg.fps.fps_div;
  }

  config->frame_rate_update_mode[i] = streamcfg.arg.fps.is_update_frame_rate_to_next_GOP;
  config->stream_abs_fps_enable[i] = streamcfg.arg.fps.abs_fps_enable;
  if (streamcfg.arg.fps.abs_fps_enable) {
    if (resource.enable_hp_fps) {
      config->stream_abs_fps[i] = streamcfg.arg.fps.abs_fps_hp / hp_factor;
    } else {
      config->stream_abs_fps[i] = streamcfg.arg.fps.abs_fps;
    }
  }

  return 0;
}

static int sync_frame_rate(int fd_iav, enc_config_t *config)
{
  u8 i, canvas_id = 0, is_found = 0, strm_sync_type = IAV_FRAME_SYNC;
  struct iav_stream_cfg sync_frame;
  struct iav_apply_frame_sync apply;
  struct iav_stream_cfg stream_cfg;
  struct iav_querydesc query_desc;
  struct iav_yuv_cap *yuv_cap;
  u16 stream_map = 0;

  for (i = 0; i < IAV_STREAM_MAX_NUM_ALL; i++) {
    if (config->framerate_factor_sync_id & (1 << i)) {
      memset(&sync_frame, 0, sizeof(sync_frame));
      sync_frame.id = i;
      sync_frame.cid = IAV_STMCFG_FPS;
      sync_frame.strm_sync_type = strm_sync_type;
      sync_frame.arg.fps.fps_multi = config->framerate_factor[i][0];
      sync_frame.arg.fps.fps_div = config->framerate_factor[i][1];
      printf("Stream [%d] sync frame interval %d/%d \n", i,
          config->framerate_factor[i][0],
          config->framerate_factor[i][1]);
      AM_IOCTL(fd_iav, IAV_IOC_CFG_FRAME_SYNC_PROC, &sync_frame);
      stream_map |= (1 << i);

      if (!is_found) {
        memset(&stream_cfg, 0, sizeof(stream_cfg));
        stream_cfg.id = i;
        stream_cfg.cid = IAV_STMCFG_FORMAT;
        AM_IOCTL(fd_iav, IAV_IOC_GET_STREAM_CONFIG, &stream_cfg);
        canvas_id = stream_cfg.arg.format.enc_src_id;
        is_found = 1;
      }
    }
  }

  memset(&query_desc, 0, sizeof(query_desc));
  query_desc.qid = IAV_DESC_CANVAS;
  query_desc.arg.canvas.canvas_id = canvas_id;
  yuv_cap = &query_desc.arg.canvas.yuv;
  AM_IOCTL(fd_iav, IAV_IOC_QUERY_DESC, &query_desc);

  memset(&apply, 0, sizeof(apply));
  apply.force_update = 1;
  apply.strm_sync_type = strm_sync_type;
  apply.stream_updated_map = stream_map;
  apply.dsp_pts = yuv_cap->dsp_pts;
  AM_IOCTL(fd_iav, IAV_IOC_APPLY_FRAME_SYNC_PROC, &apply);

  return 0;
}

static int set_stream_trigger_frame(int fd_iav, enc_config_t *config)
{
  struct iav_stream_cfg cfg;
  int i = 0;

  for (i = 0; i < IAV_STREAM_MAX_NUM_ALL; i++) {
    if (config->trigger_frame_stream_id & (1 << i)) {
      memset(&cfg, 0, sizeof(cfg));
      cfg.id = i;
      cfg.cid = IAV_STMCFG_FRAME_TRIGGER;
      AM_IOCTL(fd_iav, IAV_IOC_GET_STREAM_CONFIG, &cfg);
      if (config->trigger_frame_enable_flag[i]) {
        cfg.arg.trigger_frame.trigger_frame_enable = config->trigger_frame_enable[i];
      }
      if (config->trigger_frame_repeat_flag[i]) {
        cfg.arg.trigger_frame.repeat_enable = config->trigger_frame_repeat[i];
      }
      if (config->trigger_frame_num_flag[i]) {
        cfg.arg.trigger_frame.trigger_num = config->trigger_frame_num[i];
      }
      AM_IOCTL(fd_iav, IAV_IOC_SET_STREAM_CONFIG, &cfg);
    }
  }

  return 0;
}

static int force_idr_insertion(int stream, int fd_iav)
{
  struct iav_stream_cfg stream_cfg;
  int stream_type = IAV_STREAM_TYPE_NONE;

  memset(&stream_cfg, 0, sizeof(stream_cfg));
  stream_cfg.id = stream;
  stream_cfg.cid = IAV_STMCFG_FORMAT;
  AM_IOCTL(fd_iav, IAV_IOC_GET_STREAM_CONFIG, &stream_cfg);
  stream_type = stream_cfg.arg.format.type;

  memset(&stream_cfg, 0, sizeof(stream_cfg));
  stream_cfg.id = stream;
  if (stream_type == IAV_STREAM_TYPE_H264) {
    stream_cfg.cid = IAV_H264_CFG_FORCE_IDR;
    stream_cfg.arg.h264_force_idr = 1;
  } else {
    stream_cfg.cid = IAV_H265_CFG_FORCE_IDR;
    stream_cfg.arg.h265_force_idr = 1;
  }
  AM_IOCTL(fd_iav, IAV_IOC_SET_STREAM_CONFIG, &stream_cfg);

  return 0;
}

static int set_force_idr(int iav_fd, int force_idr_id)
{
  int i;

  for (i = 0; i < IAV_STREAM_MAX_NUM_ALL; i++) {
    if (force_idr_id & (1 << i)) {
      if (force_idr_insertion(i, iav_fd) < 0) {
        return -1;
      }
    }
  }

  return 0;
}

static int set_vin_frame_rate(int fd_iav, enc_config_t *config)
{
  struct vindev_fps vsrc_fps = {0};
  for (int i = 0; i < VIN_CONTROLLER_NUM; i++) {
    if (config->vin_framerate_changed_id & (1 << i)) {
      memset(&vsrc_fps, 0, sizeof(vsrc_fps));
      vsrc_fps.vsrc_id = i;
      vsrc_fps.fps = config->vin_framerate[i];
      if (ioctl(fd_iav, IAV_IOC_VIN_SET_FPS, &vsrc_fps) < 0) {
        perror("IAV_IOC_VIN_SET_FPS");
        return -1;
      }
    }
  }

  return 0;
}

static char getch(void)
{
  struct termios tm, tm_old;
  int fd = 0;
  char ch;

  if (tcgetattr(fd, &tm) < 0) {
    return -1;
  }

  tm_old = tm;
  cfmakeraw(&tm);
  if (tcsetattr(fd, TCSANOW, &tm) < 0) {
    return -1;
  }

  ch = getchar();
  if (tcsetattr(fd, TCSANOW, &tm_old) < 0) {
    return -1;
  }

  return ch;
}

static int run_src_buf_interactive_change(int fd_iav, u8 chan_id, u8 pass_id,
	u8 src_buf_id, u8 is_input, u8 is_size)
{
  struct iav_video_proc vproc = {0};
  struct iav_dptz *dptz = &vproc.arg.dptz;
  struct iav_rect *rect = NULL;
  struct iav_apply_flag apply_flag[IAV_VIDEO_PROC_NUM];
  u32 *hor_param = NULL;
  u32 *ver_param = NULL;
  int step = 0, has_change = 0, is_hori = 0;
  char ch;

  rect = is_input ? &dptz->buf_cfg.input : &dptz->buf_cfg.output;
  hor_param = is_size ? &rect->width : &rect->x;
  ver_param = is_size ? &rect->height : &rect->y;

  memset(&vproc, 0, sizeof(vproc));
  memset(apply_flag, 0, sizeof(struct iav_apply_flag) * IAV_VIDEO_PROC_NUM);
  vproc.cid = IAV_VIDEO_PROC_DPTZ;
  dptz->channel_id = chan_id;
  dptz->pass_id = pass_id;
  dptz->buf_id = src_buf_id;
  apply_flag[IAV_VIDEO_PROC_DPTZ].apply = 1;
#if defined (BUILD_DSP_AMBA_V5)
  apply_flag[IAV_VIDEO_PROC_CANVAS_ORDER].apply = 1;
#endif

  printf("=========================================================\n");
  printf("Enter src_buf[%d][%d][%d] %s %s interactive change:\n\n", chan_id, pass_id,
  src_buf_id, is_input ? "input" : "output", is_size ? "size" : "offset");
  if (is_size) {
  printf("    \'s\': decrease width, \'f\': increase width\n");
  printf("    \'e\': decrease height, \'d\': increase height\n");
  } else {
  printf("    \'s\': left, \'f\': right\n");
  printf("    \'e\': up, \'d\': down\n");
  }
  printf("    \'q\': quit\n");
  printf("=========================================================\n");

  while(1) {
    ch = getch();
    has_change = 0;
    switch (ch) {
      case 's':
      case 'S':
        is_hori = 1;
        has_change = 1;//IAV_SUBBUF_INPUT_H_ALIGN
#if defined (BUILD_DSP_AMBA_V5)
        step = is_input ? -IAV_SRCBUF_INPUT_H_ALIGN :
          is_size ? -IAV_SRCBUF_OUTPUT_WIDTH_ALIGN : -IAV_SRCBUF_OUTPUT_X_ALIGN;
#elif defined (BUILD_DSP_AMBA_V6)
        step = is_input ? -IAV_SUBBUF_INPUT_H_ALIGN :
          is_size ? -IAV_SRCBUF_OUTPUT_WIDTH_ALIGN : -IAV_SRCBUF_OUTPUT_X_ALIGN;
#endif
        break;
      case 'f':
      case 'F':
        is_hori = 1;
        has_change = 1;

#if defined (BUILD_DSP_AMBA_V5)
        step = is_input ? IAV_SRCBUF_INPUT_H_ALIGN :
          is_size ? IAV_SRCBUF_OUTPUT_WIDTH_ALIGN : IAV_SRCBUF_OUTPUT_X_ALIGN;
#elif defined (BUILD_DSP_AMBA_V6)
        step = is_input ? IAV_SUBBUF_INPUT_H_ALIGN :
          is_size ? IAV_SRCBUF_OUTPUT_WIDTH_ALIGN : IAV_SRCBUF_OUTPUT_X_ALIGN;
#endif
        break;
      case 'd':
      case 'D':
        is_hori = 0;
        has_change = 1;
#if defined (BUILD_DSP_AMBA_V5)
        step = is_input ? IAV_SRCBUF_INPUT_V_ALIGN :
          is_size ? IAV_SRCBUF_OUTPUT_HEIGHT_ALIGN : IAV_SRCBUF_OUTPUT_Y_ALIGN;
#elif defined (BUILD_DSP_AMBA_V6)
        step = is_input ? IAV_SUBBUF_INPUT_V_ALIGN :
          is_size ? IAV_SRCBUF_OUTPUT_HEIGHT_ALIGN : IAV_SRCBUF_OUTPUT_Y_ALIGN;
#endif
        break;
      case 'e':
      case 'E':
        is_hori = 0;
        has_change = 1;
#if defined (BUILD_DSP_AMBA_V5)
        step = is_input ? -IAV_SRCBUF_INPUT_V_ALIGN :
          is_size ? -IAV_SRCBUF_OUTPUT_HEIGHT_ALIGN : -IAV_SRCBUF_OUTPUT_Y_ALIGN;
#elif defined (BUILD_DSP_AMBA_V6)
        step = is_input ? -IAV_SUBBUF_INPUT_V_ALIGN :
          is_size ? -IAV_SRCBUF_OUTPUT_HEIGHT_ALIGN : -IAV_SRCBUF_OUTPUT_Y_ALIGN;
#endif
        break;
      case 'q':
      case 'Q':
        printf("quit\n");
        return 0;
        default:
        break;
    }

    if (has_change) {
      AM_IOCTL(fd_iav, IAV_IOC_GET_VIDEO_PROC, &vproc);
      if (is_hori) {
        if ((step >= 0 ) || ((step < 0) && (*hor_param))) {
          *hor_param += step;
          step = (step > 0) ? step : -1 * step;
          if (*hor_param % step) {
            *hor_param = (*hor_param / step ) * step;
          }
        } else {
          continue;
        }
      } else {
        if ((step >= 0 ) || ((step < 0) && (*ver_param))) {
          *ver_param += step;
          step = (step > 0) ? step : -1 * step;
          if (*ver_param % step) {
            *ver_param = (*ver_param / step ) * step;
          }
        } else {
          continue;
        }
      }

      if(!ioctl(fd_iav, IAV_IOC_CFG_VIDEO_PROC, &vproc)) {
        apply_flag[IAV_VIDEO_PROC_DPTZ].param = (1 << chan_id);
        AM_IOCTL(fd_iav, IAV_IOC_APPLY_VIDEO_PROC, apply_flag);
        printf("src_buf[%d][%d] %s %s: (%d, %d)\n", chan_id, src_buf_id,
            is_input ? "input" : "output", is_size ? "size" : "offset",
            *hor_param, *ver_param);
      } else {
        printf("Error: invalid src_buf[%d][%d] %s %s: (%d, %d)\n",
            chan_id, src_buf_id,
            is_input ? "input" : "output", is_size ? "size" : "offset",
            *hor_param, *ver_param);
      }
    }
  }

  return 0;
}

static int has_interactive_change(enc_config_t *config)
{
  source_buffer_format_t *src_buf_fmt = NULL;
  unsigned int i, j, k;

  for (i = 0; i < config->res_info.channel_num; i++) {
    for (j = 0; j < IAV_MAX_PASS_NUM; j++) {
      for (k = IAV_SRCBUF_FIRST; k < IAV_SRCBUF_LAST; k++) {
        if (!((1 << i) & config->channel_id)) {
          continue;
        }

        src_buf_fmt = &config->multi_chan_dptz[i].source_buffer_format[j][k];
        if (src_buf_fmt->input_size_interactive_change_flag ||
            src_buf_fmt->input_offset_interactive_change_flag ||
            src_buf_fmt->output_size_interactive_change_flag ||
            src_buf_fmt->output_offset_interactive_change_flag) {
          return 1;
        }
      }
    }
  }

  return 0;
}

static int set_multi_chan_buf_cfg(int fd_iav, enc_config_t *config)
{
  struct iav_video_proc vproc = {0};
  struct iav_dptz *dptz = &vproc.arg.dptz;
  struct iav_canvas_fps *canvas_fps_cfg = &vproc.arg.canvas_fps;
  struct iav_buf_cfg *buf_cfg = NULL;
  struct mcl_multi_chan_cfg multi_channel_cfg = {0};
  struct mcl_source_buffer_cfg *source_buffer = NULL;
  //struct iav_system_resource resource = {0};
  struct iav_apply_flag apply_flag[IAV_VIDEO_PROC_NUM];
  source_buffer_format_t *src_buf_fmt = NULL;
  struct mcl_pyramid_cfg *pyramid = NULL;
  u32 hp_factor = 0;
  u32 i = 0, j = 0, k = 0;

  memset(&vproc, 0, sizeof(vproc));
  memset(apply_flag, 0, sizeof(struct iav_apply_flag) * IAV_VIDEO_PROC_NUM);

  /* If specify a lua script to do multi-chan dptz, here will ignore other params */
  if (config->multi_chan_lua_flag) {
    printf("do multi-chan dptz through lua: %s\n", config->multi_chan_cfg_file_name);
    if (mcl_get_multi_chan_cfg(config->multi_chan_cfg_file_name, &multi_channel_cfg)) {
      printf("get multi-channel config from file(%s) error\n", config->multi_chan_cfg_file_name);
      return -1;
    }
    hp_factor = multi_channel_cfg.enable_hp_fps ? IAV_HP_FPS_FACTOR : 1;

    /* Buffer DPTZ CFG */
    for (i = 0; i < multi_channel_cfg.chan_num; i++) {
      for (j = 0; j < multi_channel_cfg.channels[i].scale_pass_num; j++) {
        for (k = IAV_SRCBUF_FIRST; k < IAV_SRCBUF_LAST; k++) {
          source_buffer = &multi_channel_cfg.channels[i].source_buffer[j][k];
          memset(dptz, 0, sizeof(struct iav_dptz));
          dptz->channel_id = i;
          dptz->pass_id = j;
          dptz->buf_id = k;
          buf_cfg = &dptz->buf_cfg;
          pyramid = &multi_channel_cfg.channels[i].pyramid;
          if ((source_buffer->canvas_id != (u32) MCL_INVALID_ID) ||
              (j == 0 && pyramid->input_buf_id == k)) {
            buf_cfg->input = source_buffer->input;
            buf_cfg->output = source_buffer->output;
            buf_cfg->canvas_id = source_buffer->canvas_id;
          } else {
            buf_cfg->canvas_id = IAV_INVALID_CANVAS_ID;
          }

          vproc.cid = IAV_VIDEO_PROC_DPTZ;
          AM_IOCTL(fd_iav, IAV_IOC_CFG_VIDEO_PROC, &vproc);
          apply_flag[IAV_VIDEO_PROC_DPTZ].apply = 1;
          apply_flag[IAV_VIDEO_PROC_DPTZ].param |= (1 << i);
        }
      }
    }

  } else {
    hp_factor = config->res_info.enable_hp_fps ? IAV_HP_FPS_FACTOR : 1;

    /* Buffer DPTZ CFG */
    for (i = 0; i < config->res_info.channel_num; i++) {
      if (!((1 << i) & config->channel_id)) {
        continue;
      }
      for (j = 0; j < IAV_MAX_PASS_NUM; j++) {
        for (k = IAV_SRCBUF_FIRST; k < IAV_SRCBUF_LAST; k++) {
          src_buf_fmt = &config->multi_chan_dptz[i].source_buffer_format[j][k];
          if (!src_buf_fmt->input_size_changed_flag &&
              !src_buf_fmt->input_offset_changed_flag &&
              !src_buf_fmt->output_size_changed_flag &&
              !src_buf_fmt->output_offset_changed_flag) {
            continue;
          }

          vproc.cid = IAV_VIDEO_PROC_DPTZ;
          memset(dptz, 0, sizeof(struct iav_dptz));
          dptz->channel_id = i;
          dptz->pass_id = j;
          dptz->buf_id = k;
          AM_IOCTL(fd_iav, IAV_IOC_GET_VIDEO_PROC, &vproc);

          if (src_buf_fmt->input_size_changed_flag) {
            dptz->buf_cfg.input.width = src_buf_fmt->input_width;
            dptz->buf_cfg.input.height = src_buf_fmt->input_height;
          }
          if (src_buf_fmt->input_offset_changed_flag) {
            dptz->buf_cfg.input.x = src_buf_fmt->input_x;
            dptz->buf_cfg.input.y = src_buf_fmt->input_y;
          }
          if (src_buf_fmt->output_size_changed_flag) {
            dptz->buf_cfg.output.width = src_buf_fmt->output_width;
            dptz->buf_cfg.output.height = src_buf_fmt->output_height;
          }
          if (src_buf_fmt->output_offset_changed_flag) {
            dptz->buf_cfg.output.x = src_buf_fmt->output_x;
            dptz->buf_cfg.output.y = src_buf_fmt->output_y;
          }

          AM_IOCTL(fd_iav, IAV_IOC_CFG_VIDEO_PROC, &vproc);
          apply_flag[IAV_VIDEO_PROC_DPTZ].apply = 1;
          apply_flag[IAV_VIDEO_PROC_DPTZ].param |= (1 << i);
        }
      }
    }

    for (i = 0; i < config->res_info.canvas_num; i++) {
      if (!((1 << i) & config->canvas_fps_map)) {
        continue;
      }
      vproc.cid = IAV_VIDEO_PROC_CANVAS_FPS;
      memset(canvas_fps_cfg, 0, sizeof(struct iav_canvas_fps));
      canvas_fps_cfg->id = i;
      if (config->res_info.enable_hp_fps) {
        canvas_fps_cfg->frame_rate_hp = config->res_info.canvas_fps[i] * hp_factor;
      } else {
        canvas_fps_cfg->frame_rate = config->res_info.canvas_fps[i];
      }
      canvas_fps_cfg->zero_fps = config->zero_fps_flag[i];
      AM_IOCTL(fd_iav, IAV_IOC_CFG_VIDEO_PROC, &vproc);
      apply_flag[IAV_VIDEO_PROC_CANVAS_FPS].apply = 1;
      apply_flag[IAV_VIDEO_PROC_CANVAS_FPS].param |= (1 << i);
    }

    AM_IOCTL(fd_iav, IAV_IOC_APPLY_VIDEO_PROC, apply_flag);

    /* interactive Buffer DPTZ CFG */
    if (has_interactive_change(config)) {
      for (i = 0; i < config->res_info.channel_num; i++) {
        if (!((1 << i) & config->channel_id)) {
          continue;
        }
        for (j = 0; j < IAV_MAX_PASS_NUM; j++) {
          for (k = IAV_SRCBUF_FIRST; k < IAV_SRCBUF_LAST; k++) {
            src_buf_fmt = &config->multi_chan_dptz[i].source_buffer_format[j][k];
            if (src_buf_fmt->input_size_interactive_change_flag) {
              run_src_buf_interactive_change(fd_iav, i, j, k, 1, 1);
            }
            if (src_buf_fmt->input_offset_interactive_change_flag) {
              run_src_buf_interactive_change(fd_iav, i, j, k, 1, 0);
            }
            if (src_buf_fmt->output_size_interactive_change_flag) {
              run_src_buf_interactive_change(fd_iav, i, j, k, 0, 1);
            }
            if (src_buf_fmt->output_offset_interactive_change_flag) {
              run_src_buf_interactive_change(fd_iav, i, j, k, 0, 0);
            }
          }
        }
      }
    }
  }

  return 0;
}

static int do_real_time_change(int fd_iav, enc_config_t *config)
{
  if (config->multi_chan_cfg_changed_flag) {
    if (set_multi_chan_buf_cfg(fd_iav, config) < 0) {
      return -1;
    }
  }

  if (config->vin_framerate_changed_id) {
    if (set_vin_frame_rate(fd_iav, config) < 0)
      return -1;
  }

  if (config->framerate_factor_changed_id ||
      config->frame_rate_update_mode_changed_id ||
      config->stream_abs_fps_enabled_id ||
      config->stream_abs_fps_changed_id)  {
    if (change_frame_rate(fd_iav, config) < 0) {
      return -1;
    }
  }

  if (config->framerate_factor_sync_id)  {
    if (sync_frame_rate(fd_iav, config) < 0) {
      return -1;
    }
  }
  if (config->force_idr_id) {
    if (set_force_idr(fd_iav, config->force_idr_id) < 0) {
      return -1;
    }
  }

  if (config->trigger_frame_stream_id) {
    if (set_stream_trigger_frame(fd_iav, config) < 0) {
      return -1;
    }
  }
  return 0;
}

int update_enc (int iav_fd, enc_config_t *config)
{
  /* stop encoding flag */
  config->stop_stream_id |= config->restart_stream_id;
  if (config->stop_stream_id) {
    if (stop_encode(iav_fd, config->stop_stream_id) < 0) {
      return -1;
    }
  }
  if (config->abort_stream_id) {
    if (abort_encode(config->abort_stream_id, iav_fd) < 0) {
      return -1;
    }
  }

  /* set encode format flag */
  if (config->encode_format_changed_id) {
    if (set_encode_format(iav_fd, config) < 0) {
      return -1;
    }
  }

  /* set encode param flag */
  if (config->encode_param_changed_id) {
    if (set_encode_param(iav_fd, config) < 0) {
      return -1;
    }
  }

  /* real time change encoding parameter */

  if (config->vin_framerate_changed_id ||
    config->framerate_factor_changed_id ||
    config->framerate_factor_sync_id ||
    config->frame_rate_update_mode_changed_id ||
    config->stream_abs_fps_enabled_id ||
    config->stream_abs_fps_changed_id ||
    config->force_idr_id ||
    config->force_fast_seek_id ||
    config->qp_limit_changed_id ||
    config->intra_mb_rows_changed_id ||
    config->trigger_frame_stream_id ||
    config->multi_chan_cfg_changed_flag) {
    if (do_real_time_change(iav_fd, config) < 0) {
      return -1;
    }
  }

  /* encode start flag */
  config->start_stream_id |= config->restart_stream_id;
  if (config->start_stream_id) {
    if (start_encode(config->start_stream_id, iav_fd) < 0) {
      return -1;
    }
  }

  return 0;
}

static unsigned int get_vsrc_num(int iav_fd)
{
  struct vin_global_info vsrc_info;

  AM_IOCTL(iav_fd, IAV_IOC_VIN_GET_GLOBAL_INFO, &vsrc_info);

  return vsrc_info.total_vsrc_num;
}

static int get_encode_param(int iav_fd, enc_config_t *config, u32 stream_id, u32 vsrc_id)
{
  struct iav_stream_cfg stream_cfg;
  struct vindev_fps vsrc_fps = {0};
  struct iav_stream_format *format = NULL;
  unsigned int i = 0;
  unsigned int vsrc_num = 0;

  for (i = 0; i < IAV_STREAM_MAX_NUM_ALL; i++) {
    if (!(stream_id & (1 << i))) {
      continue;
    }

    memset(&stream_cfg, 0, sizeof(stream_cfg));
    stream_cfg.id = i;
    stream_cfg.cid = IAV_STMCFG_FORMAT;
    AM_IOCTL(iav_fd, IAV_IOC_GET_STREAM_CONFIG, &stream_cfg);

    format = &stream_cfg.arg.format;
    config->encode_fmt[i].type = format->type;
    config->encode_fmt[i].width = format->enc_win.width;
    config->encode_fmt[i].height = format->enc_win.height;
    config->encode_fmt[i].offset_x = format->enc_win.x;
    config->encode_fmt[i].offset_y = format->enc_win.y;
    config->encode_fmt[i].source = format->enc_src_id;
    config->encode_fmt[i].source_map = format->enc_src_map;
    config->encode_fmt[i].duration = format->duration;
    config->encode_fmt[i].hflip = format->hflip;
    config->encode_fmt[i].vflip = format->vflip;
    config->encode_fmt[i].rotate = format->rotate_cw;
    config->encode_fmt[i].efm_customize_fps = format->efm_customize_fps;
    config->encode_fmt[i].session_id = format->session_id;
    config->encode_fmt[i].fake_avg_pts = format->fake_avg_pts;

    switch (format->type) {
      case IAV_STREAM_TYPE_H265:
        /* TODO: Switch to H265 encode param */
        /* Fall through */
      case IAV_STREAM_TYPE_H264:
        if (get_h26x_encode_param(i, format, iav_fd, config) < 0) {
          return -1;
        }
        break;
      case IAV_STREAM_TYPE_MJPEG:
        if (get_mjpeg_encode_param(i, iav_fd, config) < 0) {
          return -1;
        }
        break;
      default:
        break;
    }

    if (get_frame_rate(iav_fd, config, i) < 0) {
      return -1;
    }


  }


  //get vin frame rate
  vsrc_num = get_vsrc_num(iav_fd);
  for (i = 0; i < vsrc_num; i++) {
    if (!(vsrc_id & (1 << i))) {
      continue;
    }

    memset(&vsrc_fps, 0, sizeof(vsrc_fps));
    vsrc_fps.vsrc_id = i;
    AM_IOCTL(iav_fd, IAV_IOC_VIN_GET_FPS, &vsrc_fps);

    switch (vsrc_fps.fps) {
      case AMBA_VIDEO_FPS_AUTO:
        config->vin_framerate[i] = 0;
        break;
      case AMBA_VIDEO_FPS_29_97:
        config->vin_framerate[i] = 0x10000;//29.97;
        break;
      case AMBA_VIDEO_FPS_59_94:
        config->vin_framerate[i] = 0x10001;//59.94;
        break;
      case AMBA_VIDEO_FPS_12_5:
        config->vin_framerate[i] = 0x10003;//12.5;
        break;
      case AMBA_VIDEO_FPS_7_5:
        config->vin_framerate[i] = 0x10006;//7.5;
        break;
      default:
        config->vin_framerate[i] = DIV_ROUND(512000000, vsrc_fps.fps);
        break;
    }
  }

  return 0;
}

int get_enc_info_config (int iav_fd, enc_config_t *config, u32 stream_id, u32 vsrc_id)
{
  if (config) {

    if (get_encode_param(iav_fd, config, stream_id, vsrc_id) < 0) {
      printf("get encoding params failed\n");
      return -1;
    }

  } else {
    printf("bad params\n");
    return -1;
  }

  return 0;
}

static int __get_enc_type (int enc_type, char * type_name)
{
  switch (enc_type) {

    case IAV_STREAM_TYPE_H264 :
      memcpy(type_name, "h264", sizeof("h264"));
      break;
    case IAV_STREAM_TYPE_H265 :
      memcpy(type_name, "h265", sizeof("h265"));
      break;
    case IAV_STREAM_TYPE_MJPEG:
      memcpy(type_name, "mjpeg", sizeof("mjpeg"));
      break;
    default :
      memcpy(type_name, "invalid", sizeof("invalid"));
      break;
  }
  return 0;
}

static int __get_vbr_setting (int vbr_type, char * type_name)
{
  switch (vbr_type) {
    case IAV_BRC_CBR :
      memcpy(type_name, "CBR", sizeof("CBR"));
      break;
    case IAV_BRC_PCBR :
      memcpy(type_name, "PCBR", sizeof("PCBR"));
      break;

    case IAV_BRC_VBR :
      memcpy(type_name, "VBR", sizeof("VBR"));
      break;

    case IAV_BRC_SCBR :
      memcpy(type_name, "SCBR", sizeof("SCBR"));
      break;

    case IAV_BRC_SVBR :
      memcpy(type_name, "SVBR", sizeof("SVBR"));
      break;
    default :
      memcpy(type_name, "INVALID", sizeof("INVALID"));
      break;
  }
  return 0;
}

static int get_multi_ch_info_config (int fd_iav, enc_config_t *config)
{
  if (config) {
    struct iav_video_proc vproc = {0};
    struct iav_dptz *dptz = &vproc.arg.dptz;
    source_buffer_format_t *src_buf_fmt = NULL;
    unsigned int i = 0, j = 0, k = 0;

    /* Buffer DPTZ CFG */
    for (i = 0; i < config->res_info.channel_num; i++) {
      for (j = 0; j < config->res_info.scale_pass_num[i]; j++) {
        for (k = IAV_SRCBUF_FIRST; k < IAV_SRCBUF_LAST_PMN; k++) {
          memset(&vproc, 0, sizeof(vproc));
          vproc.cid = IAV_VIDEO_PROC_DPTZ;
          dptz->channel_id = i;
          dptz->pass_id = j;
          dptz->buf_id = k;
          AM_IOCTL(fd_iav, IAV_IOC_GET_VIDEO_PROC, &vproc);

          src_buf_fmt = &config->multi_chan_dptz[i].source_buffer_format[j][k];

          src_buf_fmt->input_width = dptz->buf_cfg.input.width;
          src_buf_fmt->input_height = dptz->buf_cfg.input.height;
          src_buf_fmt->input_x = dptz->buf_cfg.input.x;
          src_buf_fmt->input_y = dptz->buf_cfg.input.y;

          src_buf_fmt->output_width = dptz->buf_cfg.output.width;
          src_buf_fmt->output_height = dptz->buf_cfg.output.height;
          src_buf_fmt->output_x = dptz->buf_cfg.output.x;
          src_buf_fmt->output_y = dptz->buf_cfg.output.y;
        }
      }
    }
  } else {
    printf("bad params\n");
    return -1;
  }
  return 0;
}

#if defined (BUILD_DSP_AMBA_V5)

static int get_resource_info(int iav_fd, amba_resource_info_t *info)
{
  struct iav_system_resource resource;
  struct iav_pyramid_cfg pyramid_cfg;
  //struct iav_video_proc vproc = {0};
  unsigned char i = 0, frame_rate = 0;
  //int active_overlap_num = 0;

  /* query system resource */
  memset(info, 0x0, sizeof(amba_resource_info_t));
  memset(&resource, 0, sizeof(struct iav_system_resource));
  resource.encode_mode = DSP_CURRENT_MODE;
  if (ioctl(iav_fd, IAV_IOC_GET_SYSTEM_RESOURCE, &resource) < 0) {
    perror("IAV_IOC_GET_SYSTEM_RESOURCE\n");
    return -1;
  }

  info->channel_num = resource.chan_num;
  info->canvas_num = resource.canvas_num;
  info->encode_mode = resource.encode_mode;
  info->max_stream_num = resource.max_stream_num;
  info->enable_hp_fps = resource.enable_hp_fps;

  for (i = 0; i < resource.chan_num; ++i) {
    info->vcap_mode_flag[i] = resource.chan_cfg[i].vcap_mode_flags;
    info->scale_pass_num[i] = resource.chan_cfg[i].pass_num;
  }

  /* get canvas & pyramid buffer info from IAV */
  for (i = 0; i < resource.canvas_num; i++) {
    /*if (resource.canvas_cfg[i].type != IAV_CANVAS_TYPE_OFF) {
    info->canvas_num++;
    }*/
    frame_rate = resource.canvas_cfg[i].frame_rate;
    if (frame_rate != 0) {
      info->pts_intval[i] = DAMBA_HWTIMER_OUTPUT_FREQ / frame_rate;
      info->canvas_fps[i] = frame_rate;
    }
    info->canvas_mf_enable[i] = resource.canvas_cfg[i].manual_feed;
    info->canvas_yuv_buffer_disable[i] = resource.canvas_cfg[i].disable_yuv_dram;
  }

  for (i = 0; i < info->channel_num; i++) {
    memset(&pyramid_cfg, 0, sizeof(struct iav_pyramid_cfg));
    pyramid_cfg.chan_id = i;
    if (ioctl(iav_fd, IAV_IOC_GET_PYRAMID_CFG, &pyramid_cfg) < 0) {
      perror("IAV_IOC_GET_PYRAMID_CFG\n");
      return -1;
    }
    info->pyramid_manual_feed[i] = pyramid_cfg.manual_feed;
  }

  /*memset(&vproc, 0, sizeof(vproc));
  vproc.cid = IAV_VIDEO_PROC_MULTI_BLEND;
  if (ioctl(iav_fd, IAV_IOC_GET_VIDEO_PROC, &vproc) < 0) {
    perror("IAV_IOC_GET_VIDEO_PROC\n");
    return -1;
  }
  active_overlap_num = vproc.arg.blend_overlap.active_overlap_num;

  if (active_overlap_num) {
    info->skip_chan_check_in_blend_case = 1;
  }*/

  return 0;
}

#elif defined (BUILD_DSP_AMBA_V6)
static int get_resource_info(int iav_fd, amba_resource_info_t *info)
{
  struct iav_system_resource resource;
  struct iav_canvas_cfg canvas_cfg;
  struct iav_chan_cfg chan_cfg;
  struct iav_pyramid_cfg pyramid_cfg;
  //struct iav_video_proc vproc = {0};
  unsigned char i = 0, frame_rate = 0;
  //int active_overlap_num = 0;

  /* query system resource */
  memset(info, 0x0, sizeof(amba_resource_info_t));
  memset(&resource, 0, sizeof(struct iav_system_resource));
  resource.encode_mode = DSP_CURRENT_MODE;
  if (ioctl(iav_fd, IAV_IOC_GET_SYSTEM_RESOURCE, &resource) < 0) {
    perror("IAV_IOC_GET_SYSTEM_RESOURCE\n");
    return -1;
  }

  info->channel_num = resource.chan_num;
  info->canvas_num = resource.canvas_num;
  info->encode_mode = resource.encode_mode;
  info->max_stream_num = resource.max_stream_num;
  info->enable_hp_fps = resource.enable_hp_fps;

  for (i = 0; i < resource.chan_num; ++i) {
    memset(&chan_cfg, 0, sizeof(chan_cfg));
    chan_cfg.chan_id = i;
    if (ioctl(iav_fd, IAV_IOC_GET_CHAN_CONFIG, &chan_cfg) < 0) {
      perror("IAV_IOC_GET_CANVAS_CONFIG\n");
      return -1;
    }

    info->vcap_mode_flag[i] = chan_cfg.vcap_mode_flags;
    info->scale_pass_num[i] = chan_cfg.pass_num;
  }

  /* get canvas & pyramid buffer info from IAV */

  for (i = 0; i < resource.canvas_num; i++) {
    memset(&canvas_cfg, 0, sizeof(canvas_cfg));
    canvas_cfg.canvas_id = i;
    if (ioctl(iav_fd, IAV_IOC_GET_CANVAS_CONFIG, &canvas_cfg) < 0) {
      perror("IAV_IOC_GET_CANVAS_CONFIG\n");
      return -1;
    }

    frame_rate = resource.enable_hp_fps ? canvas_cfg.frame_rate_hp : canvas_cfg.frame_rate;
    if (frame_rate != 0) {
      info->pts_intval[i] = DAMBA_HWTIMER_OUTPUT_FREQ / frame_rate;
      info->canvas_fps[i] = frame_rate;
    }
    info->canvas_mf_enable[i] = canvas_cfg.manual_feed;
    info->canvas_yuv_buffer_disable[i] = canvas_cfg.disable_yuv_dram;
  }

  for (i = 0; i < info->channel_num; i++) {
    memset(&pyramid_cfg, 0, sizeof(struct iav_pyramid_cfg));
    pyramid_cfg.chan_id = i;
    if (ioctl(iav_fd, IAV_IOC_GET_PYRAMID_CFG, &pyramid_cfg) < 0) {
      perror("IAV_IOC_GET_PYRAMID_CFG\n");
      return -1;
    }
    info->pyramid_manual_feed[i] = pyramid_cfg.manual_feed;
  }

  /*memset(&vproc, 0, sizeof(vproc));
  vproc.cid = IAV_VIDEO_PROC_MULTI_BLEND;
  if (ioctl(iav_fd, IAV_IOC_GET_VIDEO_PROC, &vproc) < 0) {
    perror("IAV_IOC_GET_VIDEO_PROC\n");
    return -1;
  }
  active_overlap_num = vproc.arg.blend_overlap.active_overlap_num;

  if (active_overlap_num) {
    info->skip_chan_check_in_blend_case = 1;
  }*/

  return 0;
}

#endif

char * get_enc_info_string(int iav_fd, enc_config_t *config)
{
  unsigned int i = 0, j = 0, k = 0, count = 0;
  GstStructure * s = NULL;
  char name[128] = {0};
  char type_name[32] = {0};
  char vbr_name[32] = {0};
  char dptz_name[32] = {0};
  char dptz_value[128] = {0};
  gchar *enc_info_list[IAV_STREAM_MAX_NUM_ALL + VIN_CONTROLLER_NUM + 1] = {NULL};
  gchar * enc_info_all = NULL;
  u32 stream_id = 0, vsrc_id = 0;
  multi_chan_dptz_t *dptz = NULL;

  if (iav_fd < 0 || config == NULL) {
    printf("bad params\n");
    return NULL;
  }

  memset(config, 0x0, sizeof(enc_config_t));

  get_resource_info(iav_fd, &config->res_info);

  for (i = 0; i < config->res_info.max_stream_num; i++) {
    stream_id |= (1 << i);
  }

  for (i = 0; i < config->res_info.channel_num; i++) {
    vsrc_id |= (1 << i);
  }

  if (get_multi_ch_info_config(iav_fd, config) < 0) {
    printf("get_multi_ch_info_config error\n");
    return NULL;
  }

  if (get_enc_info_config(iav_fd, config, stream_id, vsrc_id) < 0) {
    printf("get_enc_info_config failed\n");
    return NULL;
  }


  for (i = 0; i < config->res_info.max_stream_num; i++) {
    /*if (!(stream_id & (1 << i))) {
      continue;
    }*/
    snprintf(name, sizeof(name) - 1, "stream_id_%d", i);

    __get_enc_type(config->encode_fmt[i].type, type_name);

    if (config->encode_fmt[i].type == IAV_STREAM_TYPE_H264
        || config->encode_fmt[i].type == IAV_STREAM_TYPE_H265) {
      __get_vbr_setting(config->encode_param[i].h264_param.h264_bitrate_control, vbr_name);
      s = gst_structure_new (name,
          "stream.type", G_TYPE_STRING, type_name,
          "stream.width", G_TYPE_INT, config->encode_fmt[i].width,
          "stream.height", G_TYPE_INT, config->encode_fmt[i].height,
          "stream.offset_x", G_TYPE_INT, config->encode_fmt[i].offset_x,
          "stream.offset_y", G_TYPE_INT, config->encode_fmt[i].offset_y,
          "stream.canvas_id", G_TYPE_INT, config->encode_fmt[i].source,
          "stream.abs_br", G_TYPE_INT, config->encode_param[i].h264_param.h264_abs_br,
          "stream.vbr_setting", G_TYPE_STRING, vbr_name,
          "stream.cbr_avg_bitrate", G_TYPE_INT, config->encode_param[i].h264_param.h264_cbr_avg_bitrate,
          "stream.cbr_stable_br_adjust", G_TYPE_INT, config->encode_param[i].h264_param.h264_cbr_stable_br_adjust,
          "stream.vbr_max_bitrate", G_TYPE_INT, config->encode_param[i].h264_param.h264_vbr_max_bitrate,
          "stream.M", G_TYPE_INT, config->encode_param[i].h264_param.h264_M,
          "stream.N", G_TYPE_INT, config->encode_param[i].h264_param.h264_N,
          "stream.idr_interval", G_TYPE_INT, config->encode_param[i].h264_param.h264_idr_interval,
          "stream.stream_fps", G_TYPE_INT, config->stream_fps[i],
          "stream.fps_multi", G_TYPE_INT, config->framerate_factor[i][0],
          "stream.fps_div", G_TYPE_INT, config->framerate_factor[i][1],
          "stream.slice_num", G_TYPE_INT, config->encode_param[i].h264_param.h264_slice_num,
          NULL);
    } else if (config->encode_fmt[i].type == IAV_STREAM_TYPE_MJPEG) {
      s = gst_structure_new (name,
          "stream.type", G_TYPE_STRING, type_name,
          "stream.width", G_TYPE_INT, config->encode_fmt[i].width,
          "stream.height", G_TYPE_INT, config->encode_fmt[i].height,
          "stream.offset_x", G_TYPE_INT, config->encode_fmt[i].offset_x,
          "stream.offset_y", G_TYPE_INT, config->encode_fmt[i].offset_y,
          "stream.canvas_id", G_TYPE_INT, config->encode_fmt[i].source,
          "stream.stream_fps", G_TYPE_INT, config->stream_fps[i],
          "stream.fps_multi", G_TYPE_INT, config->framerate_factor[i][0],
          "stream.fps_div", G_TYPE_INT, config->framerate_factor[i][1],
          "stream.slice_num", G_TYPE_UINT, config->encode_param[i].jpeg_param.jpeg_slice_num,
          "stream.quality", G_TYPE_INT, config->encode_param[i].jpeg_param.quality,
          "stream.chroma_format", G_TYPE_INT, config->encode_param[i].jpeg_param.jpeg_chroma_format,
          "stream.restart_interval", G_TYPE_INT, config->encode_param[i].jpeg_param.restart_interval,
          NULL);
    }

    if (s) {
      enc_info_list[i] = gst_structure_to_string(s);
      if (enc_info_list[i] == NULL) {
        DPRINT_ERROR("convert stream %d encoding configure information to string failed\n", i);
        goto enc_end;
      }

      gst_structure_free (s);
      s = NULL;

      count++;
    }
  }


  for (i = 0; i < config->res_info.channel_num; i++) {
    /*if (!(vsrc_id & (1 << i))) {
      continue;
    }*/
    dptz = &config->multi_chan_dptz[i];

    snprintf(name, sizeof(name) - 1, "channel_id_%d", i);

    s = gst_structure_new (name,
        "vin.framerate", G_TYPE_INT, config->vin_framerate[i],
        NULL);
    for (j = 0; j < config->res_info.scale_pass_num[i]; j++) {
      for (k = IAV_SRCBUF_FIRST; k < IAV_SRCBUF_LAST_PMN; k++) {
        snprintf(dptz_name, sizeof(dptz_name) - 1, "dptz_pass_%d_srcbuf_%d", j, k);
        snprintf(dptz_value, sizeof(dptz_value) - 1, "in_%d.%d.%d.%d_out_%d.%d.%d.%d",
            dptz->source_buffer_format[j][k].input_x,
            dptz->source_buffer_format[j][k].input_y,
            dptz->source_buffer_format[j][k].input_width,
            dptz->source_buffer_format[j][k].input_height,
            dptz->source_buffer_format[j][k].output_x,
            dptz->source_buffer_format[j][k].output_y,
            dptz->source_buffer_format[j][k].output_width,
            dptz->source_buffer_format[j][k].output_height);
        gst_structure_set (s, dptz_name, G_TYPE_STRING, dptz_value, NULL);
      }
    }
    enc_info_list[count] = gst_structure_to_string(s);
    if (enc_info_list[count] == NULL) {
      DPRINT_ERROR("convert vsrc %d encoding configure information to string failed\n", i);
      goto enc_end;
    }

    gst_structure_free (s);
    s = NULL;

    count++;
  }


  enc_info_all = g_strjoinv ("\n", enc_info_list);

enc_end:

  if (s) {
    gst_structure_free (s);
    s = NULL;
  }
  for (i = 0; i < count; i++) {
    if (enc_info_list[i]) {
      g_free(enc_info_list[i]);
      enc_info_list[i] = NULL;
    }
  }

  return enc_info_all;
}

#endif

#endif

static int __parse_next_uint(
  char *p_cur, char **pp_next, char delimiter, unsigned int *out_i)
{
  char *p_delimiter = strchr(p_cur, delimiter);

  *pp_next = NULL;

  if (!p_delimiter) {
    DPRINT_ERROR("do not find '%c'\n", delimiter);
    return COM_ECODE_BAD_PARAMS;
  }
  *p_delimiter = 0x0;
  *out_i = atoi(p_cur);

  *pp_next = p_delimiter + 1;

  return COM_ECODE_OK;
}

// enc:off_x.off_y.size_x.size_y
int parse_enc_resolution (const char *reso_string, enc_resolution_t *reso)
{
  int str_length = strlen(reso_string);
  char *p_dup_string;
  char *p_cur, * p_next;
  int ret;

  p_dup_string = (char *) malloc (str_length + 4);
  if (!p_dup_string) {
    DPRINT_ERROR("no memory, size %d\n", str_length + 4);
    return COM_ECODE_NO_MEMORY;
  }
  memset(p_dup_string, 0x0, str_length + 4);
  memcpy(p_dup_string, reso_string, str_length);

  do {
    p_cur = p_dup_string;

    // enc_index
    ret = __parse_next_uint(
        p_cur, &p_next, ':', &reso->enc_index);
    if (ret) {
      break;
    }

    p_cur = p_next;
    // off_x
    ret = __parse_next_uint(
        p_cur, &p_next, '.', &reso->off_x);
    if (ret) {
      break;
    }

    p_cur = p_next;
    // off_y
    ret = __parse_next_uint(
        p_cur, &p_next, '.', &reso->off_y);
    if (ret) {
      break;
    }

    p_cur = p_next;
    // size_x
    ret = __parse_next_uint(
        p_cur, &p_next, '.', &reso->size_x);
    if (ret) {
      break;
    }

    p_cur = p_next;
    // size_y
    reso->size_y = atoi(p_cur);

    // OK
    ret = COM_ECODE_OK;
  } while (0);

  free(p_dup_string);

  if (ret) {
    DPRINT_ERROR("bad params\n");
  }

  return ret;
}

// enc:bitrate
int parse_enc_bitrate (const char *bitrate_string, enc_bitrate_t *bitrate)
{
  int str_length = strlen(bitrate_string);
  char *p_dup_string;
  char *p_cur, * p_next;
  int ret;

  p_dup_string = (char *) malloc (str_length + 4);
  if (!p_dup_string) {
    DPRINT_ERROR("no memory, size %d\n", str_length + 4);
    return COM_ECODE_NO_MEMORY;
  }
  memset(p_dup_string, 0x0, str_length + 4);
  memcpy(p_dup_string, bitrate_string, str_length);

  do {
    p_cur = p_dup_string;

    // enc_index
    ret = __parse_next_uint(
        p_cur, &p_next, ':', &bitrate->enc_index);
    if (ret) {
      break;
    }

    p_cur = p_next;
    // bitrate
    bitrate->bitrate = atoi(p_cur);

    // OK
    ret = COM_ECODE_OK;
  } while (0);

  free(p_dup_string);

  if (ret) {
    DPRINT_ERROR("bad params\n");
  }

  return ret;
}

// enc:framerate
int parse_enc_framerate (const char *framerate_string, enc_framerate_t *framerate)
{
  int str_length = strlen(framerate_string);
  char *p_dup_string;
  char *p_cur, * p_next;
  int ret;

  p_dup_string = (char *) malloc (str_length + 4);
  if (!p_dup_string) {
    DPRINT_ERROR("no memory, size %d\n", str_length + 4);
    return COM_ECODE_NO_MEMORY;
  }
  memset(p_dup_string, 0x0, str_length + 4);
  memcpy(p_dup_string, framerate_string, str_length);

  do {
    p_cur = p_dup_string;

    // enc_index
    ret = __parse_next_uint(
        p_cur, &p_next, ':', &framerate->enc_index);
    if (ret) {
      break;
    }

    p_cur = p_next;
    // framerate
    framerate->framerate_i = atoi(p_cur);

    // OK
    ret = COM_ECODE_OK;
  } while (0);

  free(p_dup_string);

  if (ret) {
    DPRINT_ERROR("bad params\n");
  }

  return ret;
}

int parse_enc_bitrate_frameate (
  const char *bitrate_framerate_string, enc_bitrate_framerate_t *bitrate_framerate)
{
  DUNUSED(bitrate_framerate_string);
  DUNUSED(bitrate_framerate);
  DPRINT_WARNING("todo\n");
  return COM_ECODE_OK;
}

// enc:h264/h265/mjpeg
int parse_enc_codec_type (
  const char *codec_type_string, enc_codec_type_t *codec_type)
{
  int str_length = strlen(codec_type_string);
  char *p_dup_string;
  char *p_cur, * p_next;
  int ret;

  p_dup_string = (char *) malloc (str_length + 4);
  if (!p_dup_string) {
    DPRINT_ERROR("no memory, size %d\n", str_length + 4);
    return COM_ECODE_NO_MEMORY;
  }
  memset(p_dup_string, 0x0, str_length + 4);
  memcpy(p_dup_string, codec_type_string, str_length);

  do {
    p_cur = p_dup_string;

    // enc_index
    ret = __parse_next_uint(
        p_cur, &p_next, ':', &codec_type->enc_index);
    if (ret) {
      break;
    }

    p_cur = p_next;
    // codec type
    if (!strcmp(p_cur, "h264")) {
      codec_type->codec_type = EAMDSP_VIDEO_CODEC_TYPE_H264;
    } else if (!strcmp(p_cur, "h265")) {
      codec_type->codec_type = EAMDSP_VIDEO_CODEC_TYPE_H265;
    } else if (!strcmp(p_cur, "mjpeg")) {
      codec_type->codec_type = EAMDSP_VIDEO_CODEC_TYPE_MJPEG;
    } else {
      DPRINT_ERROR("not recognized codec type %s\n", p_cur);
      ret = COM_ECODE_BAD_PARAMS;
      break;
    }

    // OK
    ret = COM_ECODE_OK;
  } while (0);

  free(p_dup_string);

  if (ret) {
    DPRINT_ERROR("bad params\n");
  }

  return ret;
}

int parse_enc_gop_structure (
  const char *gop_structure_string, enc_gop_structure_t *gop_structure)
{
  DUNUSED(gop_structure_string);
  DUNUSED(gop_structure);
  DPRINT_WARNING("todo\n");
  return COM_ECODE_OK;
}

int parse_enc_force_idr (
  const char *force_idr_string, enc_force_idr_t *force_idr)
{
  force_idr->enc_index = atoi(force_idr_string);
  return COM_ECODE_OK;
}


static int
__set_stream_resolution (enc_format_t *enc_fmt,
    const char *value)
{
  if (value) {
    unsigned int num = 0;
    char **strv = NULL;

    strv = g_strsplit (value, ".", 4);
    num = g_strv_length (strv);

    if (num == 2) {
      enc_fmt->offset_x = 0;
      enc_fmt->offset_y = 0;
      enc_fmt->offset_changed_flag = 0;

      enc_fmt->width = (int) g_ascii_strtoll (strv[0], NULL, 10);
      enc_fmt->height = (int) g_ascii_strtoll (strv[1], NULL, 10);
      enc_fmt->resolution_changed_flag = 1;
    } else if (num == 4) {
      enc_fmt->offset_x = (int) g_ascii_strtoll (strv[0], NULL, 10);
      enc_fmt->offset_y = (int) g_ascii_strtoll (strv[1], NULL, 10);
      enc_fmt->offset_changed_flag = 1;

      enc_fmt->width = (int) g_ascii_strtoll (strv[2], NULL, 10);
      enc_fmt->height = (int) g_ascii_strtoll (strv[3], NULL, 10);
      enc_fmt->resolution_changed_flag = 1;
    } else {
      DPRINT_ERROR ("Invalid param, should be resolution:offset_x+offset_y+width+height\n");
      return -1;
    }

    g_strfreev (strv);

  } else {
    DPRINT_ERROR ("params error\n");
  }
  return 0;
}

static int is_interactive(const char *name)
{
  char * separator;
  char key = 'm';

  separator = strchr(name, key);
  if (!separator) {
    return 0;
  }

  return 1;
}


static int
__set_src_input_buf_resolution (enc_config_t *config, const char *value)
{
  source_buffer_format_t *src_buf_fmt = NULL;

  if (value) {
    src_buf_fmt = &config->multi_chan_dptz[config->current_channel].source_buffer_format[config->current_pass][config->current_buffer];
    if (is_interactive(value)) {
      if (!has_interactive_change(config)) {
        src_buf_fmt->input_size_interactive_change_flag = 1;
        config->multi_chan_cfg_changed_flag = 1;
      }
    } else {
      unsigned int num = 0;
      char **strv = NULL;

      strv = g_strsplit (value, ".", 4);
      num = g_strv_length (strv);

      if (num == 2) {
        src_buf_fmt->input_x = 0;
        src_buf_fmt->input_y = 0;
        src_buf_fmt->input_offset_changed_flag = 0;

        src_buf_fmt->input_width = (int) g_ascii_strtoll (strv[0], NULL, 10);
        src_buf_fmt->input_height = (int) g_ascii_strtoll (strv[1], NULL, 10);
        src_buf_fmt->input_size_changed_flag = 1;
        config->multi_chan_cfg_changed_flag = 1;
      } else if (num == 4) {
        src_buf_fmt->input_x = (int) g_ascii_strtoll (strv[0], NULL, 10);
        src_buf_fmt->input_y = (int) g_ascii_strtoll (strv[1], NULL, 10);
        src_buf_fmt->input_offset_changed_flag = 1;

        src_buf_fmt->input_width = (int) g_ascii_strtoll (strv[2], NULL, 10);
        src_buf_fmt->input_height = (int) g_ascii_strtoll (strv[3], NULL, 10);
        src_buf_fmt->input_size_changed_flag = 1;
        config->multi_chan_cfg_changed_flag = 1;
      } else {
        DPRINT_ERROR ("Invalid param, should be resolution:offset_x+offset_y+width+height\n");
        return -1;
      }

      g_strfreev (strv);
    }

  } else {
    DPRINT_ERROR ("params error\n");
  }
  return 0;
}

static int
__set_src_output_buf_resolution (enc_config_t *config, const char *value)
{
  source_buffer_format_t *src_buf_fmt = NULL;

  if (value) {
    src_buf_fmt = &config->multi_chan_dptz[config->current_channel].source_buffer_format[config->current_pass][config->current_buffer];
    if (is_interactive(value)) {
      if (!has_interactive_change(config)) {
        src_buf_fmt->output_size_interactive_change_flag = 1;
        config->multi_chan_cfg_changed_flag = 1;
      }
    } else {
      unsigned int num = 0;
      char **strv = NULL;

      strv = g_strsplit (value, ".", 4);
      num = g_strv_length (strv);

      if (num == 2) {
        src_buf_fmt->output_x = 0;
        src_buf_fmt->output_y = 0;
        src_buf_fmt->output_offset_changed_flag = 0;

        src_buf_fmt->output_width = (int) g_ascii_strtoll (strv[0], NULL, 10);
        src_buf_fmt->output_height = (int) g_ascii_strtoll (strv[1], NULL, 10);
        src_buf_fmt->output_size_changed_flag = 1;
        config->multi_chan_cfg_changed_flag = 1;
      } else if (num == 4) {
        src_buf_fmt->output_x = (int) g_ascii_strtoll (strv[0], NULL, 10);
        src_buf_fmt->output_y = (int) g_ascii_strtoll (strv[1], NULL, 10);
        src_buf_fmt->output_offset_changed_flag = 1;

        src_buf_fmt->output_width = (int) g_ascii_strtoll (strv[2], NULL, 10);
        src_buf_fmt->output_height = (int) g_ascii_strtoll (strv[3], NULL, 10);
        src_buf_fmt->output_size_changed_flag = 1;
        config->multi_chan_cfg_changed_flag = 1;
      } else {
        DPRINT_ERROR ("Invalid param, should be resolution:offset_x+offset_y+width+height\n");
        return -1;
      }

      g_strfreev (strv);
    }

  } else {
    DPRINT_ERROR ("params error\n");
  }
  return 0;
}

static int
__setprop_type (enc_format_t *enc_fmt,
                const char *value)
{
  if (value) {
    if (g_ascii_strcasecmp (value, "h264") == 0) {
      enc_fmt->type = IAV_STREAM_TYPE_H264;
      enc_fmt->type_changed_flag = 1;
    } else if (g_ascii_strcasecmp (value, "h265") == 0) {
      enc_fmt->type = IAV_STREAM_TYPE_H265;
      enc_fmt->type_changed_flag = 1;
    } else if (g_ascii_strcasecmp (value, "mjpeg") == 0) {
      enc_fmt->type = IAV_STREAM_TYPE_MJPEG;
      enc_fmt->type_changed_flag = 1;
    } else {
      printf("not supported codec type: %s\n", value);
      return -1;
    }

  } else {
    DPRINT_ERROR ("params error\n");
    return -1;
  }
  return 0;
}

//first second value must in format "x~y" if delimiter is '~'
static int get_two_unsigned_int(const char *name, u32 *first, u32 *second, char delimiter)
{
  char tmp_string[16];
  char *separator;

  separator = strchr(name, delimiter);
  if (!separator) {
    printf("range should be like a%cb \n", delimiter);
    return -1;
  }

  strncpy(tmp_string, name, separator - name);
  tmp_string[separator - name] = '\0';
  *first = atoi(tmp_string);
  strncpy(tmp_string, separator + 1,  name + strlen(name) - separator);
  *second = atoi(tmp_string);

  return 0;
}

static int get_bitrate_control(const char *name)
{
  if (strcmp(name, "cbr") == 0)
    return IAV_CBR;
  else if (strcmp(name, "vbr") == 0)
    return IAV_VBR;
  else if (strcmp(name, "cbr-quality") == 0)
    return IAV_CBR_QUALITY_KEEPING;
  else if (strcmp(name, "vbr-quality") == 0)
    return IAV_VBR_QUALITY_KEEPING;
  else
    return -1;
}

static int get_chroma_format(const char *format, int encode_type)
{
  int chroma = atoi(format);
  if (chroma == 0) {
    return (encode_type == IAV_STREAM_TYPE_MJPEG) ?
    JPEG_CHROMA_YUV420 :
    H264_CHROMA_YUV420;
  } else if (chroma == 1) {
    return (encode_type == IAV_STREAM_TYPE_MJPEG) ?
    JPEG_CHROMA_MONO :
    H264_CHROMA_MONO;
  } else {
    printf("invalid chroma format : %d.\n", chroma);
    return -1;
  }
}

int parse_enc (int iav_fd, const char *custom_properties, enc_config_t *config)
{
  if (custom_properties && config) {
    char **options;
    unsigned int i = 0, len = 0;

    memset(config, 0, sizeof(enc_config_t));

    config->current_stream = -1;
    config->source = -1;
    config->current_channel = -1;
    config->current_buffer = -1;
    config->current_canvas = -1;
    config->current_pass = 0;

    get_resource_info(iav_fd, &config->res_info);
    options = g_strsplit (custom_properties, ",", -1);
    len = g_strv_length (options);


    for (i = 0; i < len; ++i) {
      char **option = g_strsplit (options[i], ":", -1);

      if (g_strv_length (option) > 1) {
        g_strstrip (option[0]);
        g_strstrip (option[1]);

        if (g_ascii_strcasecmp (option[0], "stream_id") == 0) {
          config->current_stream = (int) g_ascii_strtoll (option[1], NULL, 10);
        } else if (g_ascii_strcasecmp (option[0], "stream-output") == 0) {
          __set_stream_resolution(&config->encode_fmt[config->current_stream], option[1]);
          config->encode_format_changed_id |= (1 << config->current_stream);
        } else if (g_ascii_strcasecmp (option[0], "stop") == 0) {
          config->stop_stream_id |= (1 << config->current_stream);//(int) g_ascii_strtoll (option[1], NULL, 10);
        } else if (g_ascii_strcasecmp(option[0], "restart") == 0) {
          config->restart_stream_id |= (1 << config->current_stream);// (int) g_ascii_strtoll (option[1], NULL, 10);
        } else if (g_ascii_strcasecmp(option[0], "start") == 0) {
          config->start_stream_id |= (1 << config->current_stream);// (int) g_ascii_strtoll (option[1], NULL, 10);
        } else if (g_ascii_strcasecmp (option[0], "abort") == 0) {
          config->abort_stream_id |= (1 << config->current_stream);
        } else if (g_ascii_strcasecmp (option[0], "type") == 0) {
          __setprop_type(&config->encode_fmt[config->current_stream], option[1]);
          config->encode_format_changed_id |= (1 << config->current_stream);
        } else if (g_ascii_strcasecmp(option[0], "bitrate") == 0) {
          config->encode_param[config->current_stream].h264_param.h264_cbr_avg_bitrate = (int) g_ascii_strtoll (option[1], NULL, 10);
          config->encode_param[config->current_stream].h264_param.h264_cbr_bitrate_flag = 1;
          config->encode_param_changed_id |= (1 << config->current_stream);
        } else if (g_ascii_strcasecmp(option[0], "vbr_bitrate") == 0) {
          u32 min_value = 0, max_value = 0;
          if (get_two_unsigned_int(option[1], &min_value, &max_value, '~') < 0) {
            return -1;
          }
          config->encode_param[config->current_stream].h264_param.h264_vbr_min_bitrate = min_value;
          config->encode_param[config->current_stream].h264_param.h264_vbr_max_bitrate = max_value;
          config->encode_param[config->current_stream].h264_param.h264_vbr_bitrate_flag = 1;
          config->encode_param_changed_id |= (1 << config->current_stream);
        } else if (g_ascii_strcasecmp(option[0], "frame_factor") == 0) {
          u32 numerator = 0, denominator = 0;
          if (get_two_unsigned_int(option[1], &numerator, &denominator, '/') < 0) {
            return -1;
          }
          config->framerate_factor_changed_id |= (1 << config->current_stream);
          config->framerate_factor[config->current_stream][0] = numerator;
          config->framerate_factor[config->current_stream][1] = denominator;
        } else if (g_ascii_strcasecmp(option[0], "frame_factor_sync") == 0) {
          u32 numerator = 0, denominator = 0;
          if (get_two_unsigned_int(option[1], &numerator, &denominator, '/') < 0) {
            return -1;
          }
          config->framerate_factor_sync_id |= (1 << config->current_stream);
          config->framerate_factor[config->current_stream][0] = numerator;
          config->framerate_factor[config->current_stream][1] = denominator;
        } else if (g_ascii_strcasecmp(option[0], "abs-br") == 0) {
          int val = (int) g_ascii_strtoll (option[1], NULL, 10);
          if (val != 0 && val != 1) {
            printf("Invalid value [%d], must be in [0|1].\n", val);
            return -1;
          }
          config->encode_param[config->current_stream].h264_param.h264_abs_br = val;
          config->encode_param[config->current_stream].h264_param.h264_abs_br_flag = 1;
          config->encode_param_changed_id |= (1 << config->current_stream);
        } else if (g_ascii_strcasecmp(option[0], "M") == 0) {
          config->encode_param[config->current_stream].h264_param.h264_M = (int) g_ascii_strtoll (option[1], NULL, 10);
          config->encode_param[config->current_stream].h264_param.h264_M_flag = 1;
          config->encode_param_changed_id |= (1 << config->current_stream);
        } else if (g_ascii_strcasecmp(option[0], "N") == 0) {
          config->encode_param[config->current_stream].h264_param.h264_N = (int) g_ascii_strtoll (option[1], NULL, 10);
          config->encode_param[config->current_stream].h264_param.h264_N_flag = 1;
          config->encode_param_changed_id |= (1 << config->current_stream);
        } else if (g_ascii_strcasecmp(option[0], "idr_interval") == 0) {
          config->encode_param[config->current_stream].h264_param.h264_idr_interval = (int) g_ascii_strtoll (option[1], NULL, 10);
          config->encode_param[config->current_stream].h264_param.h264_idr_interval_flag = 1;
          config->encode_param_changed_id |= (1 << config->current_stream);
        } else if (g_ascii_strcasecmp(option[0], "canvas-id") == 0) {
          int val = (int) g_ascii_strtoll (option[1], NULL, 10);
#if defined (BUILD_DSP_AMBA_V5)
          if (val < 0 || (val >= IAV_MAX_CANVAS_BUF_NUM
              && val != IAV_SRCBUF_EFM && val != IAV_SRCBUF_JS)) {
            printf("Invalid encode canvas [%d], must be in the range of "
                   "[0~%d|%d|%d].\n", val, IAV_MAX_CANVAS_BUF_NUM - 1, IAV_SRCBUF_EFM, IAV_SRCBUF_JS);
            return -1;
          }
#elif defined (BUILD_DSP_AMBA_V6)
          if (val < 0 || (val >= IAV_MAX_CANVAS_BUF_NUM
              && val != IAV_SRCBUF_EFM)) {
            printf("Invalid encode canvas [%d], must be in the range of "
              "[0~%d|%d].\n", val, IAV_MAX_CANVAS_BUF_NUM - 1, IAV_SRCBUF_EFM);
            return -1;
          }
#endif
          config->encode_fmt[config->current_stream].source = val;
          config->encode_fmt[config->current_stream].source_changed_flag = 1;
          config->encode_format_changed_id |= (1 << config->current_stream);
        } else if (g_ascii_strcasecmp(option[0], "force_idr") == 0) {
          config->force_idr_id |= (1 << config->current_stream);
        } else if (g_ascii_strcasecmp(option[0], "bc") == 0) {
          config->encode_param[config->current_stream].h264_param.h264_bitrate_control = get_bitrate_control (option[1]);
          config->encode_param[config->current_stream].h264_param.h264_bitrate_control_flag = 1;
          config->encode_param_changed_id |= (1 << config->current_stream);
        } else if (g_ascii_strcasecmp(option[0], "cbr_stable_br_adjust") == 0) {
          int val = (int) g_ascii_strtoll (option[1], NULL, 10);
          if (val > H264_CBR_STABLE_BR_ADJUST_MAX ||
            val < H264_CBR_STABLE_BR_ADJUST_MIN) {
            printf("Invalid param for cbr stable br adjust, range [%d~%d].\n",
              H264_CBR_STABLE_BR_ADJUST_MIN, H264_CBR_STABLE_BR_ADJUST_MAX);
            return -1;
          }
          config->encode_param[config->current_stream].h264_param.h264_cbr_stable_br_adjust = val;
          config->encode_param[config->current_stream].h264_param.h264_cbr_stable_br_adjust_flag = 1;
          config->encode_param_changed_id |= (1 << config->current_stream);
        } else if (g_ascii_strcasecmp(option[0], "trigger_frame") == 0) {
          int val = (int) g_ascii_strtoll (option[1], NULL, 10);
          config->trigger_frame_enable[config->current_stream] = !!val;
          config->trigger_frame_enable_flag[config->current_stream] = 1;
          config->trigger_frame_stream_id |= (1 << config->current_stream);
        } else if (g_ascii_strcasecmp(option[0], "trigger_frame_num") == 0) {
          int val = (int) g_ascii_strtoll (option[1], NULL, 10);
          if (val < 0 || val > MAX_TRIGGER_FRAME_NUM) {
            printf("Invalid trigger num value [%d], must be in [0~%d].\n", val, MAX_TRIGGER_FRAME_NUM);
            return -1;
          }
          config->trigger_frame_num[config->current_stream] = val;
          config->trigger_frame_num_flag[config->current_stream] = 1;
          config->trigger_frame_stream_id |= (1 << config->current_stream);
        } else if (g_ascii_strcasecmp(option[0], "trigger_frame_repeat") == 0) {
          int val = (int) g_ascii_strtoll (option[1], NULL, 10);
          config->trigger_frame_repeat[config->current_stream] = !!val;\
          config->trigger_frame_repeat_flag[config->current_stream] = 1;\
          config->trigger_frame_stream_id |= (1 << config->current_stream);
        } else if (g_ascii_strcasecmp(option[0], "stream_abs_fps_enable") == 0) {
          int val = (int) g_ascii_strtoll (option[1], NULL, 10);
          config->stream_abs_fps_enable[config->current_stream] = !!val;
          config->stream_abs_fps_enabled_id |= (1 << config->current_stream);
        } else if (g_ascii_strcasecmp(option[0], "stream_abs_fps") == 0) {
          int val = (int) g_ascii_strtoll (option[1], NULL, 10);
          if (val < MIN_ABS_FPS || val > MAX_ABS_FPS) {
            printf("Invalid value [%d], must be in [1~60].\n", val);
            return -1;
          }
          config->stream_abs_fps[config->current_stream] = val;
          config->stream_abs_fps_changed_id |= (1 << config->current_stream);
        } else if (g_ascii_strcasecmp(option[0], "fps_update_mode") == 0) {
          int val = (int) g_ascii_strtoll (option[1], NULL, 10);
          config->frame_rate_update_mode[config->current_stream] = !!val;
          config->frame_rate_update_mode_changed_id |= (1 << config->current_stream);
        } else if (g_ascii_strcasecmp(option[0], "vsrc") == 0) {
          config->source = (int) g_ascii_strtoll (option[1], NULL, 10);
        } else if (g_ascii_strcasecmp(option[0], "vin_frame_rate") == 0) {
          config->vin_framerate_changed_id |= (1 << config->source);
          config->vin_framerate[config->source] = (int) g_ascii_strtoll (option[1], NULL, 10);
          switch (config->vin_framerate[config->source]) {
          case 0:
            config->vin_framerate[config->source] = AMBA_VIDEO_FPS_AUTO;
            break;
          case 0x10000:
            config->vin_framerate[config->source] = AMBA_VIDEO_FPS_29_97;
            break;
          case 0x10001:
            config->vin_framerate[config->source] = AMBA_VIDEO_FPS_59_94;
            break;
          case 0x10003:
            config->vin_framerate[config->source] = AMBA_VIDEO_FPS_12_5;
            break;
          case 0x10006:
            config->vin_framerate[config->source] = AMBA_VIDEO_FPS_7_5;
            break;
          default:
            config->vin_framerate[config->source] = DIV_ROUND(512000000, config->vin_framerate[config->source]);
            break;
          }

        } else if (g_ascii_strcasecmp (option[0], "chan-id") == 0) {
          config->current_channel = (int) g_ascii_strtoll (option[1], NULL, 10);
          if ((config->current_channel < 0) || (config->current_channel >= (int) config->res_info.channel_num)) {
            printf ("channel id wrong %d \n", config->current_channel);
            return -1;
          }
          config->channel_id |= (1 << config->current_channel);
        } else if (g_ascii_strcasecmp (option[0], "pass") == 0) {
          config->current_pass = (int) g_ascii_strtoll (option[1], NULL, 10);
          if ((config->current_pass < 0) || (config->current_pass >= IAV_MAX_PASS_NUM)) {
            printf ("pass id wrong %d \n", config->current_pass);
            return -1;
          }
          if (config->res_info.encode_mode == DSP_MULTI_REGION_WARP_MODE &&
            config->current_pass == 0) {
            printf("Do not support on-the-fly change buffer settings of pass 0 for encode mode 1.\n");
            return -1;
          }
        } else if (g_ascii_strcasecmp(option[0], "srcbuf-id") == 0) {
          int val = (int) g_ascii_strtoll (option[1], NULL, 10);

          if (!config->res_info.skip_chan_check_in_blend_case) {
            if ((config->current_channel < 0) || (config->current_channel >= (int) config->res_info.channel_num)) {
              printf ("channel id wrong %d \n", config->current_channel);
              return -1;
            }
            if ((config->current_pass < 0) || (config->current_pass >= IAV_MAX_PASS_NUM)) {
              printf ("pass id wrong %d \n", config->current_pass);
              return -1;
            }
          }
          switch (val) {
            case 0:
              config->current_buffer = IAV_SRCBUF_1;
              break;
            case 1:
              config->current_buffer = IAV_SRCBUF_2;
              break;
            case 2:
              config->current_buffer = IAV_SRCBUF_3;
              break;
            case 3:
              config->current_buffer = IAV_SRCBUF_4;
              break;
            case 4:
              config->current_buffer = IAV_SRCBUF_5;
              break;
            case 5:
              config->current_buffer = IAV_SRCBUF_6;
              break;
            default:
              printf("Invalid iav src buf [%d], must be in the range of [%d~%d].\n",
                  val, 0, IAV_SRCBUF_LAST_PMN);
              return -1;
          }

        } else if (g_ascii_strcasecmp (option[0], "srcbuf-input") == 0) {
          __set_src_input_buf_resolution(config, option[1]);
        } else if (g_ascii_strcasecmp (option[0], "srcbuf-output") == 0) {
          __set_src_output_buf_resolution(config, option[1]);
        } else if (g_ascii_strcasecmp (option[0], "ch_lua") == 0) {
          snprintf(config->multi_chan_cfg_file_name, DMAX_FILE_NAME_LEN - 1, "%s", option[1]);
          config->multi_chan_lua_flag = 1;
          config->multi_chan_cfg_changed_flag = 1;
        } else if (g_ascii_strcasecmp (option[0], "sar") == 0) {
          u32 w = 0, h = 0;
          if (get_two_unsigned_int(option[1], &w, &h, '/') < 0) {
            return -1;
          }

          config->encode_param[config->current_stream].h264_param.sar_width = w;
          config->encode_param[config->current_stream].h264_param.sar_height = h;
          config->encode_param[config->current_stream].h264_param.sar_flag = 1;
          config->encode_param_changed_id |= (1 << config->current_stream);
        } else if (g_ascii_strcasecmp (option[0], "chroma") == 0) {
          int val0 = 0, val1 = 0;
          if ((val0 = get_chroma_format(option[1], IAV_STREAM_TYPE_H264)) < 0) {
            printf("get_chroma_format failed\n");
            return -1;
          }
          if ((val1 = get_chroma_format(option[1], IAV_STREAM_TYPE_MJPEG)) < 0) {
            printf("get_chroma_format failed\n");
            return -1;
          }
          config->encode_param[config->current_stream].h264_param.h264_chroma_format = val0;
          config->encode_param[config->current_stream].h264_param.h264_chroma_format_flag = 1;
          config->encode_param[config->current_stream].jpeg_param.jpeg_chroma_format = val1;
          config->encode_param[config->current_stream].jpeg_param.jpeg_chroma_format_flag = 1;
          config->encode_param_changed_id |= (1 << config->current_stream);
        } else if (g_ascii_strcasecmp (option[0], "quality") == 0) {
          config->encode_param[config->current_stream].jpeg_param.quality = atoi(option[1]);
          if (config->encode_param[config->current_stream].jpeg_param.quality > 100 ||
              config->encode_param[config->current_stream].jpeg_param.quality < 1) {
            printf("quality of mjpeg should be 1~100, 100 is best quality\n");
            return -1;
          }
          config->encode_param[config->current_stream].jpeg_param.quality_changed_flag  = 1;
          config->encode_param_changed_id |= (1 << config->current_stream);
        } else if (g_ascii_strcasecmp (option[0], "restart-interval") == 0) {
          config->encode_param[config->current_stream].jpeg_param.restart_interval = atoi(option[1]);
          config->encode_param[config->current_stream].jpeg_param.restart_interval_flag  = 1;
          config->encode_param_changed_id |= (1 << config->current_stream);
        } else if (g_ascii_strcasecmp (option[0], "jpeg-slice") == 0) {
          int val = atoi(option[1]);
          if (val < 0 || val > IAV_MJPEG_MAX_SLICE_NUM) {
            printf("Invalid MJPEG slice num %d, must be in the range of "
              "[0~%d].\n", val, IAV_MJPEG_MAX_SLICE_NUM);
            return -1;
          }
          config->encode_param[config->current_stream].jpeg_param.jpeg_slice_num = val;
          config->encode_param[config->current_stream].jpeg_param.jpeg_slice_num_flag = 1;
          config->encode_param_changed_id |= (1 << config->current_stream);
        } else if (g_ascii_strcasecmp (option[0], "frame-drop") == 0) {
          int val = atoi(option[1]);
          if ((val < 0) || (val > 255)) {
            printf("Invalid frame-drop value [%d], must be in [0, 255].\n", val);
            return -1;
          }
          config->encode_param[config->current_stream].h264_param.h264_drop_frames = val;
          config->encode_param[config->current_stream].jpeg_param.jpeg_drop_frames = val;
          config->encode_param[config->current_stream].h264_param.h264_drop_frames_flag = 1;
          config->encode_param[config->current_stream].jpeg_param.jpeg_drop_frames_flag = 1;
          config->encode_param_changed_id |= (1 << config->current_stream);
        } else if (g_ascii_strcasecmp (option[0], "frame-drop-repeat") == 0) {
          config->encode_param[config->current_stream].h264_param.h264_frame_drop_repeat_enable = !!(atoi(option[1]));
          config->encode_param[config->current_stream].jpeg_param.jpeg_frame_drop_repeat_enable = !!(atoi(option[1]));
          config->encode_param[config->current_stream].h264_param.h264_drop_frames_flag = 1;
          config->encode_param[config->current_stream].jpeg_param.jpeg_drop_frames_flag = 1;
          config->encode_param_changed_id |= (1 << config->current_stream);
        } else if (g_ascii_strcasecmp (option[0], "slices_per_info") == 0) {
          // 0: one bits info for each tile, 255: one bits info per frame.
          int val = atoi(option[1]);
          if ((val < 0) || (val > 255)) {
            printf("Invalid value [%d], must be in [0, 255].\n", val);
            return -1;
          }
          config->encode_param[config->current_stream].h264_param.h264_slices_per_info = val;
          config->encode_param[config->current_stream].h264_param.h264_slices_per_info_flag = 1;
          config->encode_param_changed_id |= (1 << config->current_stream);
        } else {
          printf ("Unknown option (%s).", options[i]);
          return -1;
        }
      }

      g_strfreev (option);
    }

    g_strfreev (options);

  } else {
    printf("no params\n");
    return -1;
  }

  return 0;
}

