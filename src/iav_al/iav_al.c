/*
 * iav_al.c
 *
 * History:
 *    5/1/2022 - [Peng-Xue Duan] created file
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

// for file simulation
#include "encoder_simulator.h"

#ifdef BUILD_MODULE_AMBA_DSP

#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <limits.h>

#include "errno.h"
#include "basetypes.h"
#include "iav_ioctl.h"
#include "lib_efm.h"
#include "lib_blur.h"

#include "linux_device_lcd.h"


static void __parse_fps (unsigned int fps_q9, amba_dsp_vin_info_t *vininfo)
{

  switch (fps_q9) {

    case AMBA_VIDEO_FPS_AUTO:
      vininfo->fps = 30;
      vininfo->fr_num = 90000;
      vininfo->fr_den = 3003;
      break;

    case AMBA_VIDEO_FPS_29_97:
      vininfo->fps = 30;
      vininfo->fr_num = 90000;
      vininfo->fr_den = 3003;
      break;

    case AMBA_VIDEO_FPS_59_94:
      vininfo->fps = 60;
      vininfo->fr_num = 90000;
      vininfo->fr_den = 1501;
      break;

    default:
      vininfo->fps = (unsigned long) ( (unsigned long) 512000000 + (fps_q9 >> 1) ) / (unsigned long) (fps_q9);
      vininfo->fr_num = 90000;
      vininfo->fr_den = (unsigned long) 90000 * (unsigned long) (fps_q9) / (512000000);
      break;

  }

}

static int __get_single_vout_info (int iav_fd, int chan, int type, amba_dsp_vout_info_t *voutinfo)
{
  DUNUSED(chan);

  if (!voutinfo) {
    return -1;
  }

  struct vout_params param;

  memset (&param, 0x0, sizeof (param) );

  switch (type) {
    case EAMDSP_VOUT_TYPE_DIGITAL: {
      param.id = VOUT_LCD;
      voutinfo->source_id = 0;
    }
    break;

    case EAMDSP_VOUT_TYPE_HDMI: {
      param.id = VOUT_TV;
      voutinfo->source_id = 1;
    }
    break;

    case EAMDSP_VOUT_TYPE_CVBS: {
      param.id = VOUT_TV;
      voutinfo->source_id = 1;
    }
    break;

    default:
      DPRINT_ERROR ("Invalid VOUT type!");
      return -1;
  }

  if (ioctl (iav_fd, IAV_IOC_VOUT_GET_PARAMS, &param) < 0) {
    perror ("IAV_IOC_VOUT_GET_PARAMS");
    DPRINT_ERROR ("IAV_IOC_VOUT_GET_PARAMS failed\n");
    return -2;
  }

  voutinfo->width = param.vout_win.width;
  voutinfo->height = param.vout_win.height;
  voutinfo->offset_x = param.vout_win.x;
  voutinfo->offset_y = param.vout_win.y;
  voutinfo->rotate = param.video_rotate;
  voutinfo->flip = param.video_flip;

  return 0;
}

typedef struct {
  const char  *name;
  int mode;
  int width;
  int height;
} GstVoutResolution;

GstVoutResolution gGstVoutResolutionList[] = {
  //Typically for Analog and HDMI
  {"480i", AMBA_VIDEO_MODE_480I, 720, 480},
  {"576i", AMBA_VIDEO_MODE_576I, 720, 576},
  {"480p", AMBA_VIDEO_MODE_D1_NTSC, 720, 480},
  {"576p", AMBA_VIDEO_MODE_D1_PAL, 720, 576},
  {"720p", AMBA_VIDEO_MODE_720P, 1280, 720},
  {"1080i", AMBA_VIDEO_MODE_1080I, 1920, 1080},
  {"1080p", AMBA_VIDEO_MODE_1080P, 1920, 1080},
  // Typically for LCD
  {"D480I", AMBA_VIDEO_MODE_480I, 720, 480},
  {"D576I", AMBA_VIDEO_MODE_576I, 720, 576},
  {"D480P", AMBA_VIDEO_MODE_D1_NTSC, 720, 480},
  {"D576P", AMBA_VIDEO_MODE_D1_PAL, 720, 576},
  {"D720P", AMBA_VIDEO_MODE_720P, 1280, 720},
  {"D1080I", AMBA_VIDEO_MODE_1080I, 1920, 1080},
  {"D1080P", AMBA_VIDEO_MODE_1080P, 1920, 1080},
  {"D960x240", AMBA_VIDEO_MODE_960_240, 960, 240},    //AUO27
  {"D320x240", AMBA_VIDEO_MODE_320_240, 320, 240},    //AUO27
  {"D320x288", AMBA_VIDEO_MODE_320_288, 320, 288},    //AUO27
  {"D360x240", AMBA_VIDEO_MODE_360_240, 360, 240},    //AUO27
  {"D360x288", AMBA_VIDEO_MODE_360_288, 360, 288},    //AUO27
  {"D480x640", AMBA_VIDEO_MODE_480_640, 480, 640},    //P28K
  {"D480x800", AMBA_VIDEO_MODE_480_800, 480, 800},    //TPO648
  {"hvga", AMBA_VIDEO_MODE_HVGA, 320, 480},   //TPO489
  {"vga",  AMBA_VIDEO_MODE_VGA, 640, 480},
  {"wvga", AMBA_VIDEO_MODE_WVGA,   800, 480}, //TD043
  {"D240x400", AMBA_VIDEO_MODE_240_400, 240, 400},    //WDF2440
  {"xga",  AMBA_VIDEO_MODE_XGA, 1024, 768},   //EJ080NA
  {"wsvga", AMBA_VIDEO_MODE_WSVGA, 1024, 600},    //AT070TNA2
  {"D960x540", AMBA_VIDEO_MODE_960_540, 960, 540},    //E330QHD
};

GstVoutResolution *__find_vout_mode_res (const char *name)
{
  unsigned int i;

  for (i = 0; i < sizeof (gGstVoutResolutionList) / sizeof (gGstVoutResolutionList[0]); i++) {
    if (strcmp (gGstVoutResolutionList[i].name, name) == 0) {
      return &gGstVoutResolutionList[i];
    }
  }

  DPRINT_ERROR ("vout resolution '%s' not found\n", name);
  return NULL;
}

void gstPrintAvailableVideoOutputMode()
{
  unsigned int i;
  printf ("Available video output mode:\n");

  for (i = 0; i < sizeof (gGstVoutResolutionList) / sizeof (gGstVoutResolutionList[0]); i++) {
    printf ("\t%s:\t\t%dx%d\n", gGstVoutResolutionList[i].name, gGstVoutResolutionList[i].width, gGstVoutResolutionList[i].height);
  }
}

enum amba_vout_sink_type __sink_type_from_string (const char *string)
{
  if (string) {
    if (!strcmp ("hdmi", string) ) {
      return AMBA_VOUT_SINK_TYPE_HDMI;

    } else if (!strcmp ("digital", string) ) {
      return AMBA_VOUT_SINK_TYPE_DIGITAL;

    } else if (!strcmp ("cvbs", string) ) {
      return AMBA_VOUT_SINK_TYPE_CVBS;

    } else if (!strcmp ("svideo", string) ) {
      return AMBA_VOUT_SINK_TYPE_SVIDEO;

    } else if (!strcmp ("ypbpr", string) ) {
      return AMBA_VOUT_SINK_TYPE_YPBPR;

    } else if (!strcmp ("mipi", string) ) {
      return AMBA_VOUT_SINK_TYPE_MIPI;

    } else {
      DPRINT_ERROR ("not known vout sink type: %s\n", string);
    }

  } else {
    DPRINT_ERROR ("NULL string\n");
  }

  return AMBA_VOUT_SINK_TYPE_AUTO;
}

static int __interlaced_mode_to_iav (const char *mode)
{
  int interlaced = 0; // 0: non-interlaced  1: interlaced

  if ( (!strcmp (mode, "480i") )
       || (!strcmp (mode, "576i") )
       || (!strcmp (mode, "1080i") )
       || (!strcmp (mode, "D480I") )
       || (!strcmp (mode, "D576I") )
       || (!strcmp (mode, "D1080I") ) ) {
    interlaced = 1;
  }

  return interlaced;
}

static int __fps_mode_to_iav (const char *mode)
{
  int fps = 60000;

  if ( (!strcmp (mode, "480i") ) || (!strcmp (mode, "D480I") ) ) {
    fps = 59940;

  } else if ( (!strcmp (mode, "480p") ) || (!strcmp (mode, "D480P") ) ) {
    fps = 60000;

  } else if ( (!strcmp (mode, "576i") ) || (!strcmp (mode, "576p") )
              || (!strcmp (mode, "D576I") ) || (!strcmp (mode, "D576P") ) ) {
    fps = 50000;
  }

  return fps;
}

static int __configure_vout (int iav_fd, amba_vout_config_t *vout_config)
{
  struct voutdev_format format = {0};

  enum amba_vout_sink_type sink_type = AMBA_VOUT_SINK_TYPE_AUTO;

  if (vout_config->sink_type_string) {
    sink_type = __sink_type_from_string (vout_config->sink_type_string);

    if (AMBA_VOUT_SINK_TYPE_AUTO == sink_type) {
      DPRINT_ERROR ("not reconized sink type: %s\n", vout_config->sink_type_string);
      return (-1);
    }

  } else {
    DPRINT_ERROR ("NULL sink type string\n");
    return (-2);
  }

  switch (sink_type) {
    case AMBA_VOUT_SINK_TYPE_DIGITAL: {
      format.id = VOUT_LCD;
      format.type = VOUT_TYPE_LCD;
    }
    break;

    case AMBA_VOUT_SINK_TYPE_HDMI: {
      format.id = VOUT_TV;
      format.type = VOUT_TYPE_HDMI;
    }
    break;

    case AMBA_VOUT_SINK_TYPE_CVBS: {
      format.id = VOUT_TV;
      format.type = VOUT_TYPE_CVBS;
    }
    break;

    default:
      DPRINT_ERROR ("Invalid VOUT type!");
      return (-3);
  }

  GstVoutResolution *v_mode_res = NULL;

  if (vout_config->mode_string) {
    v_mode_res = __find_vout_mode_res (vout_config->mode_string);

    if (!v_mode_res) {
      DPRINT_ERROR ("not reconized video mode: %s\n", vout_config->mode_string);
      return (-4);
    }

  } else {
    DPRINT_ERROR ("NULL mode string\n");
    return (-5);
  }

  format.mode = v_mode_res->mode;
  format.interlaced = __interlaced_mode_to_iav (v_mode_res->name);
  format.fps = __fps_mode_to_iav (v_mode_res->name);

  DPRINT_NOTICE("format.id %d, format.type %d, format.mode %d, format.interlaced %d, format.fps %d\n",
    format.id, format.type, format.mode, format.interlaced, format.fps);

  if (ioctl (iav_fd, IAV_IOC_VOUT_SET_MODE, &format) < 0) {
    perror ("IAV_IOC_VOUT_SET_MODE");
    DPRINT_NOTICE ("IAV_IOC_VOUT_SET_MODE failed\n");
    return (-6);
  }

  return 0;
}

static int __is_vout_alive (int iav_fd, int chan)
{
  struct vout_params params;
  memset (&params, 0x0, sizeof (params) );

  params.id = chan;

  if (ioctl (iav_fd, IAV_IOC_VOUT_GET_PARAMS, &params) < 0) {
    perror ("IAV_IOC_VOUT_GET_PARAMS");
    return 0;
  }

  if (params.video_enable) {
    return 1;
  }

  return 0;
}

static int __halt_vout (int iav_fd, int vout_id)
{
  struct vout_onoff onoff = {0};
  onoff.id = vout_id;
  onoff.on = 0;   /* 0: off, 1: on */

  if (ioctl (iav_fd, IAV_IOC_VOUT_SWITCH_VIDEO, &onoff) < 0) {
    perror ("IAV_IOC_VOUT_SWITCH_VIDEO");
    return -1;
  }

  return 0;
}

#if defined (BUILD_DSP_AMBA_V5)

static int __v5_get_dsp_mode (int iav_fd, amba_dsp_mode_t *mode)
{
  int state = 0;
  int ret = 0;

  ret = ioctl (iav_fd, IAV_IOC_GET_IAV_STATE, &state);

  if (0 > ret) {
    perror ("IAV_IOC_GET_IAV_STATE");
    DPRINT_ERROR ("IAV_IOC_GET_IAV_STATE fail, errno %d\n", errno);
    return ret;
  }

  switch (state) {

    case IAV_STATE_INIT:
      mode->dsp_mode = EAMDSP_MODE_INIT;
      break;

    case IAV_STATE_IDLE:
      mode->dsp_mode = EAMDSP_MODE_IDLE;
      break;

    case IAV_STATE_PREVIEW:
      mode->dsp_mode = EAMDSP_MODE_PREVIEW;
      break;

    case IAV_STATE_ENCODING:
      mode->dsp_mode = EAMDSP_MODE_ENCODE;
      break;

    case IAV_STATE_DECODING:
      mode->dsp_mode = EAMDSP_MODE_DECODE;
      break;

    default:
      DPRINT_ERROR ("un expected dsp mode %d\n", state);
      mode->dsp_mode = EAMDSP_MODE_INVALID;
      break;
  }

  return 0;
}

static int __v5_enter_decode_mode (int iav_fd, amba_dsp_decode_mode_config_t *mode_config)
{
  struct iav_decode_mode_config decode_mode;
  int i = 0, j = 0;

  memset (&decode_mode, 0x0, sizeof (decode_mode) );

  if (mode_config->num_decoder > DAMBADSP_MAX_DECODER_NUMBER) {
    DPRINT_ERROR ("BAD num_decoder %d\n", mode_config->num_decoder);
    return (-100);
  }

  decode_mode.num_decoder = mode_config->num_decoder;
  decode_mode.num_vout = 0;

  for (i = 0; i < mode_config->num_decoder; i ++) {
    decode_mode.max_frm_width[i] = mode_config->multi_chan_configs[i].max_frm_width;
    decode_mode.max_frm_height[i] = mode_config->multi_chan_configs[i].max_frm_height;

    if (EAMDSP_VIDEO_CODEC_TYPE_H264 == mode_config->multi_chan_configs[i].decoder_type) {
      decode_mode.decoder_type[i] = IAV_DECODER_TYPE_H264;

    } else if (EAMDSP_VIDEO_CODEC_TYPE_H265 == mode_config->multi_chan_configs[i].decoder_type) {
      decode_mode.decoder_type[i] = IAV_DECODER_TYPE_H265;

    } else {
      DPRINT_ERROR ("bad video decoder type %d\n", mode_config->multi_chan_configs[i].decoder_type);
      return (-101);
    }

    if (mode_config->multi_chan_configs[i].layers_map) {
      decode_mode.pyramid[i].chan_id = i;
      decode_mode.pyramid[i].enable = 1;
      decode_mode.pyramid[i].layers_map = mode_config->multi_chan_configs[i].layers_map;
      decode_mode.pyramid[i].buf_addr = (long) mode_config->multi_chan_configs[i].ext_buf_addr;
      decode_mode.pyramid[i].buf_size = mode_config->multi_chan_configs[i].ext_buf_size;
      decode_mode.pyramid[i].scale_type = (enum iav_pyramid_scale) mode_config->multi_chan_configs[i].scale_type;
      decode_mode.pyramid[i].rescale_size.width = mode_config->multi_chan_configs[i].layer1_width;
      decode_mode.pyramid[i].rescale_size.height = mode_config->multi_chan_configs[i].layer1_height;

      for (j = 0; j < DDSP_MAX_PYRAMID_LAYERS; j ++) {
        decode_mode.pyramid[i].crop_win[j].x = mode_config->multi_chan_configs[i].crop_win[j].x;
        decode_mode.pyramid[i].crop_win[j].y = mode_config->multi_chan_configs[i].crop_win[j].y;
        decode_mode.pyramid[i].crop_win[j].width = mode_config->multi_chan_configs[i].crop_win[j].w;
        decode_mode.pyramid[i].crop_win[j].height = mode_config->multi_chan_configs[i].crop_win[j].h;
      }
    }

    decode_mode.enable_vout[i] = mode_config->multi_chan_configs[i].enable_vout;

    if (decode_mode.enable_vout[i]) {
      decode_mode.num_vout ++;
    }
  }

  decode_mode.max_vout0_width = mode_config->max_vout0_width;
  decode_mode.max_vout0_height = mode_config->max_vout0_height;
  decode_mode.max_vout1_width = mode_config->max_vout1_width;
  decode_mode.max_vout1_height = mode_config->max_vout1_height;
  decode_mode.b_support_ff_fb_bw = mode_config->b_support_ff_fb_bw;
  decode_mode.max_n_to_m_ratio = mode_config->max_gop_size;
  decode_mode.debug_max_frame_per_interrupt = mode_config->debug_max_frame_per_interrupt;
  decode_mode.debug_use_dproc = mode_config->debug_use_dproc;

  i = ioctl (iav_fd, IAV_IOC_ENTER_DECODE_MODE, &decode_mode);

  if (0 > i) {
    perror ("IAV_IOC_ENTER_DECODE_MODE");
    DPRINT_ERROR ("enter decode mode fail, errno %d\n", errno);
    return i;
  }

  return 0;
}

static int __v5_leave_decode_mode (int iav_fd)
{
  int ret = ioctl (iav_fd, IAV_IOC_LEAVE_DECODE_MODE);

  if (0 > ret) {
    perror ("IAV_IOC_LEAVE_DECODE_MODE");
    DPRINT_ERROR ("leave decode mode fail, errno %d\n", errno);
  }

  return ret;
}

static int __v5_create_decoder (int iav_fd, amba_dsp_decoder_info_t *p_decoder_info)
{
  int i = 0;
  struct iav_decoder_info decoder_info;

  memset (&decoder_info, 0x0, sizeof (decoder_info) );

  decoder_info.decoder_id = p_decoder_info->decoder_id;

  if (EAMDSP_VIDEO_CODEC_TYPE_H264 == p_decoder_info->decoder_type) {
    decoder_info.decoder_type = IAV_DECODER_TYPE_H264;

  } else if (EAMDSP_VIDEO_CODEC_TYPE_H265 == p_decoder_info->decoder_type) {
    decoder_info.decoder_type = IAV_DECODER_TYPE_H265;

  } else {
    DPRINT_ERROR ("bad video codec type %d\n", p_decoder_info->decoder_type);
    return (-101);
  }

  decoder_info.num_vout = p_decoder_info->num_vout;
  decoder_info.width = p_decoder_info->width;
  decoder_info.height = p_decoder_info->height;

  if (decoder_info.num_vout > DAMBADSP_MAX_VOUT_NUMBER) {
    DPRINT_ERROR ("BAD num_vout %d\n", p_decoder_info->num_vout);
    return (-100);
  }

  for (i = 0; i < decoder_info.num_vout; i ++) {
    decoder_info.vout_configs[i].vout_id = p_decoder_info->vout_configs[i].vout_id;
    decoder_info.vout_configs[i].enable = p_decoder_info->vout_configs[i].enable;
    decoder_info.vout_configs[i].flip = p_decoder_info->vout_configs[i].flip;
    decoder_info.vout_configs[i].rotate = p_decoder_info->vout_configs[i].rotate;

    decoder_info.vout_configs[i].target_win_offset_x = p_decoder_info->vout_configs[i].target_win_offset_x;
    decoder_info.vout_configs[i].target_win_offset_y = p_decoder_info->vout_configs[i].target_win_offset_y;

    decoder_info.vout_configs[i].target_win_width = p_decoder_info->vout_configs[i].target_win_width;
    decoder_info.vout_configs[i].target_win_height = p_decoder_info->vout_configs[i].target_win_height;

    decoder_info.vout_configs[i].zoom_factor_x = p_decoder_info->vout_configs[i].zoom_factor_x;
    decoder_info.vout_configs[i].zoom_factor_y = p_decoder_info->vout_configs[i].zoom_factor_y;

    decoder_info.vout_configs[i].vout_mode = p_decoder_info->vout_configs[i].vout_mode;
  }

  decoder_info.bsb_start_offset = p_decoder_info->bsb_start_offset;
  decoder_info.bsb_size = p_decoder_info->bsb_size;

  i = ioctl (iav_fd, IAV_IOC_CREATE_DECODER, &decoder_info);

  if (0 > i) {
    perror ("IAV_IOC_CREATE_DECODER");
    DPRINT_ERROR ("create decoder fail, errno %d\n", errno);
    return i;
  }

  p_decoder_info->bsb_start_offset = decoder_info.bsb_start_offset;
  p_decoder_info->bsb_size = decoder_info.bsb_size;

  return 0;
}

static int __v5_destroy_decoder (int iav_fd, unsigned char decoder_id)
{
  int ret = ioctl (iav_fd, IAV_IOC_DESTROY_DECODER, decoder_id);

  if (0 > ret) {
    perror ("IAV_IOC_DESTROY_DECODER");
    DPRINT_ERROR ("destroy decoder fail, errno %d\n", errno);
  }

  return ret;
}

static int __v5_query_decode_config (int iav_fd, amba_dsp_query_decode_config_t *config)
{
  DUNUSED(iav_fd);
  if (config) {
    config->auto_map_bsb = 0;
    config->rendering_monitor_mode = 0;

  } else {
    DPRINT_ERROR ("NULL\n");
    return (-1);
  }

  return 0;
}

static int __v5_decode_trick_play (int iav_fd, unsigned char decoder_id, unsigned char trick_play)
{
  int ret;
  struct iav_decode_trick_play trickplay;
  memset (&trickplay, 0x0, sizeof (trickplay) );

  trickplay.decoder_id = decoder_id;
  trickplay.trick_play = trick_play;
  ret = ioctl (iav_fd, IAV_IOC_DECODE_TRICK_PLAY, &trickplay);

  if (0 > ret) {
    if (EACCES == errno) {
      DPRINT_ERROR ("stopped\n");
      return DDECODER_STOPPED;
    }

    perror ("IAV_IOC_DECODE_TRICK_PLAY");
    DPRINT_ERROR ("trickplay error, errno %d\n", errno);
    return ret;
  }

  return 0;
}

static int __v5_decode_start (int iav_fd, unsigned char decoder_id)
{
  int ret = ioctl (iav_fd, IAV_IOC_DECODE_START, decoder_id);

  if (ret < 0) {
    perror ("IAV_IOC_DECODE_START");
    DPRINT_ERROR ("decode start error, errno %d\n", errno);
    return ret;
  }

  return 0;
}

static int __v5_decode_stop (int iav_fd, unsigned char decoder_id, unsigned char stop_flag)
{
  int ret;
  struct iav_decode_stop stop;

  memset(&stop, 0x0, sizeof(stop));

  stop.decoder_id = decoder_id;
  stop.stop_flag = stop_flag;

  ret = ioctl (iav_fd, IAV_IOC_DECODE_STOP, &stop);

  if (0 > ret) {
    perror ("IAV_IOC_DECODE_STOP");
    DPRINT_ERROR ("decode stop error, errno %d\n", errno);
    return ret;
  }

  return 0;
}

static int __v5_decode_speed (int iav_fd, unsigned char decoder_id, unsigned short speed, unsigned char scan_mode, unsigned char direction)
{
  int ret;
  struct iav_decode_speed spd;

  memset(&spd, 0x0, sizeof(spd));

  spd.decoder_id = decoder_id;
  spd.direction = direction;
  spd.speed = speed;
  spd.scan_mode = scan_mode;

  ret = ioctl (iav_fd, IAV_IOC_DECODE_SPEED, &spd);

  if (0 > ret) {
    if (EACCES == errno) {
      DPRINT_ERROR ("stopped\n");
      return DDECODER_STOPPED;
    }

    perror ("IAV_IOC_DECODE_SPEED");
    DPRINT_ERROR ("decode speed error, errno %d\n", errno);
    return ret;
  }

  return 0;
}

static int __v5_decode_request_bits_fifo (int iav_fd, int decoder_id, unsigned int size, void *cur_pos_offset)
{
  struct iav_decode_bsb wait;
  int ret;

  memset(&wait, 0x0, sizeof(wait));

  wait.decoder_id = decoder_id;
  wait.room = size;
  wait.start_offset = (unsigned int) (unsigned long) cur_pos_offset;

  ret = ioctl (iav_fd, IAV_IOC_WAIT_DECODE_BSB, &wait);

  if (0 > ret) {
    if (EACCES == errno) {
      DPRINT_ERROR ("stopped\n");
      return DDECODER_STOPPED;
    }

    DPRINT_ERROR ("IAV_IOC_WAIT_DECODE_BSB fail, errno %d.\n", errno);
    perror ("IAV_IOC_WAIT_DECODE_BSB");
    return ret;
  }

  return 0;
}

static int __v5_decode (int iav_fd, amba_dsp_decode_t *dec)
{
  int ret = 0;
  struct iav_decode_video decode_video;

  memset (&decode_video, 0, sizeof (decode_video) );
  decode_video.decoder_id = dec->decoder_id;
  decode_video.num_frames = dec->num_frames;

  decode_video.start_ptr_offset = dec->start_ptr_offset;
  decode_video.end_ptr_offset = dec->end_ptr_offset;
  decode_video.first_frame_display = dec->first_frame_display;

  ret = ioctl (iav_fd, IAV_IOC_DECODE_VIDEO, &decode_video);

  if (0 > ret) {
    if (EACCES == errno) {
      DPRINT_ERROR ("stopped\n");
      return DDECODER_STOPPED;
    }

    perror ("IAV_IOC_DECODE_VIDEO");
    DPRINT_ERROR ("IAV_IOC_DECODE_VIDEO fail, errno %d.\n", errno);
    return ret;
  }

  return 0;
}

static int __v5_decode_query_bsb_status_and_print (int iav_fd, unsigned char decoder_id)
{
  int ret;
  struct iav_decode_bsb bsb;

  memset (&bsb, 0x0, sizeof (bsb) );
  bsb.decoder_id = decoder_id;

  ret = ioctl (iav_fd, IAV_IOC_QUERY_DECODE_BSB, &bsb);

  if (0 > ret) {
    if (EACCES == errno) {
      DPRINT_ERROR ("stopped\n");
      return DDECODER_STOPPED;
    }

    DPRINT_ERROR ("IAV_IOC_QUERY_DECODE_BSB fail, errno %d.\n", errno);
    perror ("IAV_IOC_QUERY_DECODE_BSB");
    return ret;
  }

  DPRINT_NOTICE ("[bsb]: current write offset (arm) 0x%08lx, current read offset (dsp) 0x%08lx, safe room (minus 256 bytes) %d, free room %d\n", bsb.start_offset, bsb.dsp_read_offset, bsb.room, bsb.free_room);

  return 0;
}

static int __v5_decode_query_status_and_print (int iav_fd, unsigned char decoder_id)
{
  int ret;
  struct iav_decode_status status;

  memset (&status, 0x0, sizeof (status) );
  status.decoder_id = decoder_id;

  ret = ioctl (iav_fd, IAV_IOC_QUERY_DECODE_STATUS, &status);

  if (0 > ret) {
    if (EACCES == errno) {
      DPRINT_ERROR ("stopped\n");
      return DDECODER_STOPPED;
    }

    perror ("IAV_IOC_QUERY_DECODE_STATUS");
    DPRINT_ERROR ("IAV_IOC_QUERY_DECODE_STATUS fail, errno %d.\n", errno);
    return ret;
  }

  DPRINT_NOTICE ("[decode status]: decode_state %d, decoded_pic_number %d, error_status %d, total_error_count %d, irq_count %d\n", status.decode_state, status.decoded_pic_number, status.error_status, status.total_error_count, status.irq_count);
  DPRINT_NOTICE ("[decode status, bsb]: current write offset (arm) 0x%08lx, current read offset (dsp) 0x%08lx, safe room (minus 256 bytes) %d, free room %d\n", status.write_offset, status.dsp_read_offset, status.room, status.free_room);
  DPRINT_NOTICE ("[decode status, last pts]: %d, is_started %d, is_send_stop_cmd %d\n", status.last_pts, status.is_started, status.is_send_stop_cmd);
  DPRINT_NOTICE ("[decode status, yuv addr]: yuv422_y 0x%08lx, yuv422_uv 0x%08lx\n", status.yuv422_y_addr, status.yuv422_uv_addr);

  return 0;
}

static int __v5_decode_query_bsb_status (int iav_fd, amba_dsp_bsb_status_t *status)
{
  int ret;
  struct iav_decode_bsb bsb;
  memset (&bsb, 0x0, sizeof (bsb) );
  bsb.decoder_id = status->decoder_id;
  ret = ioctl (iav_fd, IAV_IOC_QUERY_DECODE_BSB, &bsb);

  if (0 > ret) {
    if (EACCES == errno) {
      DPRINT_ERROR ("stopped\n");
      return DDECODER_STOPPED;
    }

    DPRINT_ERROR ("IAV_IOC_QUERY_DECODE_BSB fail, errno %d.\n", errno);
    perror ("IAV_IOC_QUERY_DECODE_BSB");
    return ret;
  }

  status->start_offset = bsb.start_offset;
  status->room = bsb.room;
  status->dsp_read_offset = bsb.dsp_read_offset;
  status->free_room = bsb.free_room;

  return 0;
}

static int __v5_decode_query_status (int iav_fd, amba_dsp_decode_status_t *status)
{
  int ret;
  struct iav_decode_status dec_status;
  memset (&dec_status, 0x0, sizeof (dec_status) );
  dec_status.decoder_id = status->decoder_id;
  ret = ioctl (iav_fd, IAV_IOC_QUERY_DECODE_STATUS, &dec_status);

  if (0 > ret) {
    if (EACCES == errno) {
      DPRINT_ERROR ("stopped\n");
      return DDECODER_STOPPED;
    }

    perror ("IAV_IOC_QUERY_DECODE_STATUS");
    DPRINT_ERROR ("IAV_IOC_QUERY_DECODE_STATUS fail, errno %d.\n", errno);
    return ret;
  }

  status->is_started = dec_status.is_started;
  status->is_send_stop_cmd = dec_status.is_send_stop_cmd;
  status->last_pts = dec_status.last_pts;
  status->decode_state = dec_status.decode_state;
  status->error_status = dec_status.error_status;
  status->total_error_count = dec_status.total_error_count;
  status->decoded_pic_number = dec_status.decoded_pic_number;

  status->write_offset = dec_status.write_offset;
  status->room = dec_status.room;
  status->dsp_read_offset = dec_status.dsp_read_offset;
  status->free_room = dec_status.free_room;
  status->irq_count = dec_status.irq_count;
  status->yuv422_y_addr = dec_status.yuv422_y_addr;
  status->yuv422_uv_addr = dec_status.yuv422_uv_addr;
  return 0;
}

static int __v5_decode_wait_eos (int iav_fd, amba_dsp_decode_eos_timestamp_t *eos_timestamp)
{
  int ret = 0;
  struct iav_decode_wait_eos wait_eos;

  memset (&wait_eos, 0, sizeof (wait_eos) );
  wait_eos.decoder_id = eos_timestamp->decoder_id;

  ret = ioctl (iav_fd, IAV_IOC_WAIT_PLAYBACK_EOS, &wait_eos);

  if (ret < 0) {
    if (EACCES == errno) {
      DPRINT_ERROR ("stopped\n");
      return DDECODER_STOPPED;
    }

    DPRINT_ERROR ("error: IAV_IOC_WAIT_PLAYBACK_EOS fail, ret %d\n", ret);
    perror ("IAV_IOC_WAIT_PLAYBACK_EOS");
    return ret;
  }

  return 0;
}

static int __v5_get_vin_info (int iav_fd, amba_dsp_vin_info_t *vininfo)
{
  struct vindev_video_info vin_info;
  struct vindev_fps active_fps;
  struct vindev_devinfo vindev_info;
  unsigned int fps_q9 = 1;
  int ret = 0;

  memset (&vin_info, 0x0, sizeof (vin_info) );
  vin_info.vsrc_id = vininfo->vsrc_id;
  vin_info.info.mode = AMBA_VIDEO_MODE_CURRENT;
  ret = ioctl (iav_fd, IAV_IOC_VIN_GET_VIDEOINFO, &vin_info);

  if (0 > ret) {
    perror ("IAV_IOC_VIN_GET_VIDEOINFO");
    DPRINT_ERROR ("IAV_IOC_VIN_GET_VIDEOINFO fail, errno %d\n", errno);
    return ret;
  }

  memset (&active_fps, 0, sizeof (active_fps) );
  active_fps.vsrc_id = vininfo->vsrc_id;
  ret = ioctl (iav_fd, IAV_IOC_VIN_GET_FPS, &active_fps);

  if (0 > ret) {
    perror ("IAV_IOC_VIN_GET_FPS");
    DPRINT_ERROR ("IAV_IOC_VIN_GET_FPS fail, errno %d\n", errno);
    return ret;
  }

  memset (&vindev_info, 0x0, sizeof (vindev_info));
  ioctl (iav_fd, IAV_IOC_VIN_GET_DEVINFO, &vindev_info);
  if (0 > ret) {
    perror ("IAV_IOC_VIN_GET_DEVINFO");
    DPRINT_ERROR ("IAV_IOC_VIN_GET_DEVINFO fail, errno %d\n", errno);
    return ret;
  }

  fps_q9 = active_fps.fps;

  __parse_fps (fps_q9, vininfo);

  vininfo->width = vin_info.info.width;
  vininfo->height = vin_info.info.height;

  vininfo->format = vin_info.info.format;
  vininfo->type = vin_info.info.type;
  vininfo->bits = vin_info.info.bits;
  vininfo->ratio = vin_info.info.ratio;
  vininfo->system = vin_info.info.system;
  vininfo->flip = vin_info.info.flip;
  vininfo->rotate = vin_info.info.rotate;
  vininfo->vinc_id = vindev_info.vinc_id;

  return 0;
}

static int __v5_get_stream_framefactor (int iav_fd, int index, amba_dsp_stream_framefactor_t *framefactor)
{
  struct iav_stream_cfg streamcfg;
  int ret = 0;

  if (IAV_STREAM_MAX_NUM_ALL <= index) {
    DPRINT_ERROR ("index(%d) not as expected\n", index);
    return (-10);
  }

  memset (&streamcfg, 0, sizeof (streamcfg) );
  streamcfg.id = index;
  streamcfg.cid = IAV_STMCFG_FPS;
  ret = ioctl (iav_fd, IAV_IOC_GET_STREAM_CONFIG, &streamcfg);

  if (0 > ret) {
    perror ("IAV_IOC_GET_STREAM_CONFIG");
    DPRINT_ERROR ("IAV_IOC_GET_STREAM_CONFIG fail, errno %d\n", errno);
    return ret;
  }

  framefactor->framefactor_num = streamcfg.arg.fps.fps_multi;
  framefactor->framefactor_den = streamcfg.arg.fps.fps_div;

  return 0;
}

static int __v5_map_bsb (int iav_fd, iav_map_bsb_t *map_bsb)
{
  int ret = 0;
  unsigned int map_size = 0;
  struct iav_querymem query_mem;
  struct iav_mem_part_info *mem_part;

  memset (&query_mem, 0, sizeof (query_mem) );
  query_mem.mid = IAV_MEM_PARTITION;
  mem_part = &query_mem.arg.partition;
  mem_part->pid = IAV_PART_BSB;
  ret = ioctl (iav_fd, IAV_IOC_QUERY_MEMBLOCK, &query_mem);

  if (0 > ret) {
    perror ("IAV_IOC_QUERY_MEMBLOCK");
    DPRINT_ERROR ("IAV_IOC_QUERY_MEMBLOCK fail, errno %d\n", errno);
    return ret;
  }

  map_bsb->size = mem_part->mem.length;

  if (map_bsb->b_two_times) {
    map_size = mem_part->mem.length * 2;

  } else {
    map_size = mem_part->mem.length;
  }

  if (map_bsb->b_enable_read && map_bsb->b_enable_write) {
    map_bsb->base = mmap (NULL, map_size, PROT_WRITE | PROT_READ, MAP_SHARED, iav_fd, mem_part->mem.addr);

  } else if (map_bsb->b_enable_read && !map_bsb->b_enable_write) {
    map_bsb->base = mmap (NULL, map_size, PROT_READ, MAP_SHARED, iav_fd, mem_part->mem.addr);

  } else if (!map_bsb->b_enable_read && map_bsb->b_enable_write) {
    map_bsb->base = mmap (NULL, map_size, PROT_WRITE, MAP_SHARED, iav_fd, mem_part->mem.addr);

  } else {
    DPRINT_ERROR ("not read or write\n");
    return (-1);
  }

  if (map_bsb->base == MAP_FAILED) {
    perror ("mmap");
    DPRINT_ERROR ("mmap fail\n");
    return -1;
  }

  DPRINT_NOTICE ("[mmap]: bsb_mem = %p, size = 0x%x\n", map_bsb->base, map_bsb->size);
  return 0;
}

static int __v5_map_dec_bsb (int iav_fd, iav_map_bsb_t *map_bsb)
{
  return __v5_map_bsb (iav_fd, map_bsb);
}

static int __v5_map_dsp (int iav_fd, iav_map_dsp_t *map_dsp)
{
  int ret = 0;
  struct iav_querymem query_mem;
  struct iav_mem_part_info *mem_part;

  memset (&query_mem, 0, sizeof (query_mem) );
  query_mem.mid = IAV_MEM_PARTITION;
  mem_part = &query_mem.arg.partition;
  mem_part->pid = IAV_PART_DSP;
  ret = ioctl (iav_fd, IAV_IOC_QUERY_MEMBLOCK, &query_mem);

  if (0 > ret) {
    perror ("IAV_IOC_QUERY_MEMBLOCK");
    DPRINT_ERROR ("IAV_IOC_QUERY_MEMBLOCK fail, errno %d\n", errno);
    return ret;
  }

  map_dsp->size = mem_part->mem.length;
  map_dsp->base = mmap (NULL, map_dsp->size, PROT_READ | PROT_WRITE, MAP_SHARED, iav_fd, mem_part->mem.addr);

  if (map_dsp->base == MAP_FAILED) {
    perror ("mmap");
    DPRINT_ERROR ("mmap fail, errno %d\n", errno);
    return -1;
  }

  DPRINT_NOTICE ("[mmap]: dsp_mem = %p, size = 0x%x\n", map_dsp->base, map_dsp->size);
  return 0;
}

static int __v5_map_overlay(int iav_fd, iav_map_overlay_t *map_overlay)
{
  int ret = 0;
  struct iav_querymem query_mem;
  struct iav_mem_part_info *part_info = NULL;

  memset (&query_mem, 0, sizeof (query_mem) );
  query_mem.mid = IAV_MEM_PARTITION;
  part_info = &query_mem.arg.partition;
  part_info->pid = IAV_PART_OVERLAY;

  ioctl(iav_fd, IAV_IOC_QUERY_MEMBLOCK, &query_mem);
  if (part_info->mem.length == 0) {
    printf("IAV_PART_OVERLAY is not allocated.\n");
    ret = -1;
    goto map_overlay_exit;
  }

  map_overlay->size = part_info->mem.length;
  map_overlay->base = mmap(NULL, map_overlay->size, PROT_WRITE, MAP_SHARED, iav_fd,
    part_info->mem.addr);
  if (map_overlay->base == MAP_FAILED) {
    perror("mmap");
    DPRINT_ERROR("mmap(map_user) failed\n");
    return COM_ECODE_MEM_MAP_FAILED;
  }

  DPRINT_NOTICE ("[mmap]: overlay_mem = %p, size = 0x%lx\n", map_overlay->base, map_overlay->size);

map_overlay_exit:
  return ret;
}

static int __v5_unmap_bsb (int iav_fd, iav_map_bsb_t *map_bsb)
{
  DUNUSED(iav_fd);
  if (map_bsb->base && map_bsb->size) {
    int ret = 0;
    unsigned int map_size = 0;

    if (map_bsb->b_two_times) {
      map_size = map_bsb->size * 2;

    } else {
      map_size = map_bsb->size;
    }

    ret = munmap (map_bsb->base, map_size);
    DPRINT_NOTICE ("[munmap]: bsb_mem = %p, size = 0x%x\n", map_bsb->base, map_bsb->size);
    map_bsb->base = NULL;
    map_bsb->size = 0;

    if (0 > ret) {
      perror ("munmap");
      DPRINT_ERROR ("munmap fail, errno %d\n", errno);
      return ret;
    }

  } else {
    DPRINT_ERROR ("bad params, %p, %d\n", map_bsb->base, map_bsb->size);
    return (-1);
  }

  return 0;
}

static int __v5_unmap_dec_bsb (int iav_fd, iav_map_bsb_t *map_bsb)
{
  return __v5_unmap_bsb (iav_fd, map_bsb);
}

static int __v5_unmap_dsp (int iav_fd, iav_map_dsp_t *map_dsp)
{
  DUNUSED(iav_fd);
  if (map_dsp->base && map_dsp->size) {
    int ret = 0;
    ret = munmap (map_dsp->base, map_dsp->size);
    DPRINT_NOTICE ("[munmap]: dsp_mem = %p, size = 0x%x\n", map_dsp->base, map_dsp->size);
    map_dsp->base = NULL;
    map_dsp->size = 0;

    if (0 > ret) {
      perror ("munmap");
      DPRINT_ERROR ("munmap fail, errno %d\n", errno);
      return ret;
    }

  } else {
    DPRINT_ERROR ("bad params, %p, %d\n", map_dsp->base, map_dsp->size);
    return (-1);
  }

  return 0;
}

static int __v5_unmap_overlay(int iav_fd, iav_map_overlay_t *map_overlay)
{
  DUNUSED(iav_fd);
  if (map_overlay->base && map_overlay->size) {
    int ret = 0;
    ret = munmap(map_overlay->base, map_overlay->size);
    DPRINT_NOTICE("[munmap]: dsp_mem = %p, size = 0x%lx\n", map_overlay->base, map_overlay->size);
    map_overlay->base = NULL;
    map_overlay->size = 0;
    if (0 > ret) {
      perror("munmap");
      DPRINT_ERROR("munmap failed, errno %d\n", errno);
      return COM_ECODE_MEM_UNMAP_FAILED;
    }
  } else {
    DPRINT_ERROR("bad params, %p, %ld\n", map_overlay->base, map_overlay->size);
    return COM_ECODE_BAD_PARAMS;
  }

  return COM_ECODE_OK;
}

static int __v5_flush_frame_desc(int fd_iav, unsigned int streamid,
    unsigned int force_idr_type, u64 mono_pts)
{
  int rval = 0;
  struct iav_flush_framedesc flush_framedesc = {0};
  struct iav_stream_cfg stream_cfg;
  struct iav_querydesc query_desc;

  memset(&flush_framedesc, 0, sizeof(flush_framedesc));
  flush_framedesc.stream_id = streamid;
  flush_framedesc.enable_force_idr = (IAV_FLUSH_FORCE_IDR_DISABLE != force_idr_type);
  if (IAV_FLUSH_FORCE_IDR_WITH_PTS == force_idr_type) {
    memset(&stream_cfg, 0, sizeof(stream_cfg));
    stream_cfg.id = streamid;
    stream_cfg.cid = IAV_STMCFG_FORMAT;
    rval = ioctl (fd_iav, IAV_IOC_GET_STREAM_CONFIG, &stream_cfg);
    if (rval < 0) {
      perror("IAV_IOC_GET_STREAM_CONFIG");
      return rval;
    }
    do {
      memset(&query_desc, 0, sizeof(query_desc));
      query_desc.arg.canvas.canvas_id = stream_cfg.arg.format.enc_src_id;
      query_desc.arg.canvas.is_dsp_hw_pts = 1;
      query_desc.qid = IAV_DESC_CANVAS;
      rval = ioctl (fd_iav, IAV_IOC_QUERY_DESC, &query_desc);
      if (rval < 0) {
        perror("IAV_IOC_QUERY_DESC");
        return rval;
      }
      if (query_desc.arg.canvas.yuv.mono_pts >= mono_pts) {
        break;
      }
    } while (1);

    flush_framedesc.force_idr_dsp_pts = query_desc.arg.canvas.yuv.dsp_pts;
    flush_framedesc.force_idr_mono_pts = query_desc.arg.canvas.yuv.mono_pts;
    DPRINT_NOTICE("flush frame in stream[%d]: force idr with pts %lld\n", streamid, flush_framedesc.force_idr_mono_pts);
  }
  rval = ioctl (fd_iav, IAV_IOC_FLUSH_FRAMEDESC, &flush_framedesc);
  if (rval < 0) {
    perror("IAV_IOC_FLUSH_FRAMEDESC");
  }

  return rval;
}


static int __v5_read_bitstream (int iav_fd, amba_dsp_read_bitstream_t *bitstream)
{
  struct iav_querydesc query_desc;
  struct iav_framedesc *frame_desc = NULL;
  int ret = 0;

  memset (&query_desc, 0, sizeof (query_desc) );
  frame_desc = &query_desc.arg.frame;
  query_desc.qid = IAV_DESC_FRAME;
  frame_desc->id = bitstream->stream_idx;
  frame_desc->time_ms = bitstream->timeout_ms;

  ret = ioctl (iav_fd, IAV_IOC_QUERY_DESC, &query_desc);

  if (ret) {
    if (EAGAIN == errno) {
      return COM_ECODE_TRY_AGAIN;
    }
    perror ("IAV_IOC_QUERY_DESC");
    DPRINT_ERROR ("IAV_IOC_QUERY_DESC faild, errno %d\n", errno);
    return COM_ECODE_BAD_STATE;
  }

  bitstream->framedesc = frame_desc;
  bitstream->stream_idx = frame_desc->id;
  bitstream->offset = frame_desc->data_addr_offset;
  bitstream->size = frame_desc->size;
  bitstream->pts = frame_desc->arm_pts;
  bitstream->video_width = frame_desc->reso.width;
  bitstream->video_height = frame_desc->reso.height;
  bitstream->slice_id = frame_desc->slice_id;
  bitstream->slice_num = frame_desc->slice_num;
  bitstream->tile_id = frame_desc->tile_id;
  bitstream->tile_num = frame_desc->tile_num;
  bitstream->encoded_frame_num = frame_desc->encoded_frame_num;

  if (frame_desc->stream_end) {
    bitstream->size = 0;
    bitstream->offset = 0;
    DPRINT_NOTICE("stream end\n");
    return COM_ECODE_COMPLETE;
  }

  if (IAV_PIC_TYPE_B_FRAME == frame_desc->pic_type) {
    bitstream->hint_frame_type = EPredefinedPictureType_B;
    bitstream->hint_is_keyframe = 0;
  } else if ( (IAV_PIC_TYPE_P_FRAME == frame_desc->pic_type)
              || (IAV_PIC_TYPE_P_FAST_SEEK_FRAME == frame_desc->pic_type) ) {
    bitstream->hint_frame_type = EPredefinedPictureType_P;
    bitstream->hint_is_keyframe = 0;
  } else if (IAV_PIC_TYPE_I_FRAME == frame_desc->pic_type) {
    bitstream->hint_frame_type = EPredefinedPictureType_I;
    bitstream->hint_is_keyframe = 0;
  } else if (IAV_PIC_TYPE_IDR_FRAME == frame_desc->pic_type) {
    bitstream->hint_frame_type = EPredefinedPictureType_IDR;
    bitstream->hint_is_keyframe = 1;
  } else if (IAV_PIC_TYPE_MJPEG_FRAME == frame_desc->pic_type) {
    bitstream->hint_frame_type = EPredefinedPictureType_IDR;
    bitstream->hint_is_keyframe = 1;
  } else if (0x07 == frame_desc->pic_type) {
    bitstream->size = 0;
    bitstream->offset = 0;
    DPRINT_NOTICE("frame end\n");
    return COM_ECODE_COMPLETE;
  } else {
    bitstream->hint_frame_type = 0;
    bitstream->hint_is_keyframe = 0;
    DPRINT_ERROR ("bad frame type %d\n", frame_desc->pic_type);
    return COM_ECODE_BAD_STATE;
  }

  if (IAV_STREAM_TYPE_H264 == frame_desc->stream_type) {
    bitstream->stream_format = StreamFormat_H264;
  } else if (IAV_STREAM_TYPE_H265 == frame_desc->stream_type) {
    bitstream->stream_format = StreamFormat_H265;
  } else if (IAV_STREAM_TYPE_MJPEG == frame_desc->stream_type) {
    bitstream->stream_format = StreamFormat_JPEG;
  } else {
    DPRINT_ERROR ("bad stream type %d\n", frame_desc->stream_type);
    bitstream->stream_format = StreamFormat_Invalid;
    return COM_ECODE_BAD_STATE;
  }

  if ((frame_desc->arm_pts & 0x8000000000000000ULL) != 0) {
    DPRINT_ERROR ("arm_pts is too large %llu(0x%llX), may lead to overflow\n", frame_desc->arm_pts, frame_desc->arm_pts);
    return COM_ECODE_BAD_STATE;
  }

  return COM_ECODE_OK;
}

static void __v5_release_bitstream (int iav_fd, amba_dsp_release_bitstream_t *release_bitstream)
{
  struct iav_framedesc *frame_desc = (struct iav_framedesc *) release_bitstream->framedesc;

  if (ioctl (iav_fd, IAV_IOC_RELEASE_FRAMEDESC, frame_desc) < 0) {
    perror ("IAV_IOC_RELEASE_FRAMEDESC\n");
  }
}

static int __v5_is_ready_for_read_bitstream (int iav_fd)
{
  DUNUSED(iav_fd);
  return 1;
}

static int __v5_encode_start (int iav_fd, unsigned int mask)
{
  int ret = ioctl (iav_fd, IAV_IOC_START_ENCODE, mask);

  if (ret) {
    perror ("IAV_IOC_START_ENCODE");
    DPRINT_ERROR ("IAV_IOC_START_ENCODE fail, errno %d, mask 0x%08x\n", errno, mask);
    return ret;
  }

  return 0;
}

static int __v5_encode_stop (int iav_fd, unsigned int mask)
{
  int ret = ioctl (iav_fd, IAV_IOC_STOP_ENCODE, mask);

  if (ret) {
    perror ("IAV_IOC_STOP_ENCODE");
    DPRINT_ERROR ("IAV_IOC_STOP_ENCODE fail, errno %d, mask 0x%08x\n", errno, mask);
    return ret;
  }

  return 0;
}

static int __v5_gdma_alloc_buf(int iav_fd, unsigned int size,
    amba_gdma_buf_t *gdma_ctx, unsigned char is_dma_buf)
{
  DUNUSED(is_dma_buf);
  struct iav_alloc_mem_part_fd alloc_mem_part;
  memset(&alloc_mem_part, 0x0, sizeof(alloc_mem_part));
  alloc_mem_part.length = size;
  alloc_mem_part.enable_cache = 1;

  if (ioctl(iav_fd, IAV_IOC_ALLOC_ANON_MEM_PART_FD, &alloc_mem_part) < 0) {
    perror("IAV_IOC_ALLOC_ANON_MEM_PART_FD");
    return -1;
  }
  gdma_ctx->dma_buf_fd = alloc_mem_part.dma_buf_fd;
  gdma_ctx->gdma_buf_size = lseek(gdma_ctx->dma_buf_fd, 0, SEEK_END);
  if (gdma_ctx->gdma_buf_size) {
    gdma_ctx->gdma_buf = mmap(NULL, gdma_ctx->gdma_buf_size, PROT_READ | PROT_WRITE, MAP_SHARED,
      gdma_ctx->dma_buf_fd, 0);
    if (gdma_ctx->gdma_buf == MAP_FAILED) {
      perror("mmap dma-buf:fd dst buffer failed\n");
      return -1;
    }
  }

  return 0;
}

static int __v5_gdma_free_buf(int iav_fd, amba_gdma_buf_t *gdma_ctx)
{
  struct iav_alloc_mem_part alloc_mem_part = {0};

  if (gdma_ctx->gdma_buf && gdma_ctx->gdma_buf_size) {
    munmap(gdma_ctx->gdma_buf, gdma_ctx->gdma_buf_size);
    gdma_ctx->gdma_buf = NULL;
    gdma_ctx->gdma_buf_size = 0;
  }

  if (gdma_ctx->gdma_part_id >= 0) {
    alloc_mem_part.pid = gdma_ctx->gdma_part_id;
    if (ioctl(iav_fd, IAV_IOC_FREE_MEM_PART, &alloc_mem_part) < 0) {
      perror("IAV_IOC_FREE_MEM_PART");
      return -1;
    }
    gdma_ctx->gdma_part_id = -1;
  }

  if (gdma_ctx->dma_buf_fd >= 0) {
    close(gdma_ctx->dma_buf_fd);
    gdma_ctx->dma_buf_fd = -1;
  }

  return 0;
}

static int __v5_query_encode_stream_info (int iav_fd, amba_dsp_enc_stream_info_t *info)
{
  struct iav_queryinfo query_info;
  int ret = 0;
  memset(&query_info, 0x0, sizeof(query_info));

  query_info.qid = IAV_INFO_STREAM;
  query_info.arg.stream.id = info->id;
  ret = ioctl (iav_fd, IAV_IOC_QUERY_INFO, &query_info);

  if (ret) {
    perror ("IAV_IOC_QUERY_INFO");
    DPRINT_ERROR ("IAV_IOC_QUERY_INFO fail, errno %d\n", errno);
    return ret;
  }

  switch (query_info.arg.stream.state) {

    case IAV_STREAM_STATE_IDLE:
      info->state = EAMDSP_ENC_STREAM_STATE_IDLE;
      break;

    case IAV_STREAM_STATE_STARTING:
      info->state = EAMDSP_ENC_STREAM_STATE_STARTING;
      break;

    case IAV_STREAM_STATE_ENCODING:
      info->state = EAMDSP_ENC_STREAM_STATE_ENCODING;
      break;

    case IAV_STREAM_STATE_STOPPING:
      info->state = EAMDSP_ENC_STREAM_STATE_STOPPING;
      break;

    case IAV_STREAM_STATE_UNKNOWN:
      DPRINT_ERROR ("unknown state\n");
      info->state = EAMDSP_ENC_STREAM_STATE_UNKNOWN;
      return (-1);
      break;

    default:
      DPRINT_ERROR ("unexpected state %d\n", query_info.arg.stream.state);
      info->state = EAMDSP_ENC_STREAM_STATE_ERROR;
      return (-2);
      break;
  }

  return 0;
}

static int __v5_query_encode_stream_format (int iav_fd, amba_dsp_enc_stream_format_t *fmt)
{
  struct iav_stream_cfg stream_cfg;
  struct iav_stream_format *format;
  int ret = 0;

  memset (&stream_cfg, 0, sizeof (stream_cfg) );
  stream_cfg.cid = IAV_STMCFG_FORMAT;
  stream_cfg.id = fmt->id;
  format = &stream_cfg.arg.format;
  ret = ioctl (iav_fd, IAV_IOC_GET_STREAM_CONFIG, &stream_cfg);

  if (ret) {
    perror ("IAV_IOC_GET_STREAM_CONFIG");
    DPRINT_ERROR ("IAV_IOC_GET_STREAM_CONFIG fail, errno %d\n", errno);
    return ret;
  }

  switch (format->type) {

    case IAV_STREAM_TYPE_H264:
      fmt->codec = EAMDSP_VIDEO_CODEC_TYPE_H264;
      break;

    case IAV_STREAM_TYPE_H265:
      fmt->codec = EAMDSP_VIDEO_CODEC_TYPE_H265;
      break;

    case IAV_STREAM_TYPE_MJPEG:
      fmt->codec = EAMDSP_VIDEO_CODEC_TYPE_MJPEG;
      break;

    case IAV_STREAM_TYPE_NONE:
      DPRINT_ERROR ("codec none?\n");
      fmt->codec = EAMDSP_VIDEO_CODEC_TYPE_INVALID;
      break;

    default:
      DPRINT_ERROR ("unexpected codec %d\n", format->type);
      fmt->codec = EAMDSP_VIDEO_CODEC_TYPE_INVALID;
      return (-1);
      break;
  }

  switch (format->enc_src_id) {

    case IAV_SRCBUF_MN:
      fmt->source_buffer = EAMDSP_ENC_STREAM_SOURCE_BUFFER_MAIN;
      break;

    case IAV_SRCBUF_PC:
      fmt->source_buffer = EAMDSP_ENC_STREAM_SOURCE_BUFFER_SECOND;
      break;

    case IAV_SRCBUF_PB:
      fmt->source_buffer = EAMDSP_ENC_STREAM_SOURCE_BUFFER_THIRD;
      break;

    case IAV_SRCBUF_PA:
      fmt->source_buffer = EAMDSP_ENC_STREAM_SOURCE_BUFFER_FOURTH;
      break;

    case IAV_SRCBUF_PD:
      fmt->source_buffer = EAMDSP_ENC_STREAM_SOURCE_BUFFER_FIFTH;
      break;

    case IAV_SRCBUF_EFM:
      fmt->source_buffer = EAMDSP_ENC_STREAM_SOURCE_BUFFER_EFM;
      break;

    default:
      DPRINT_ERROR ("unexpected source buffer %d\n", format->enc_src_id);
      fmt->source_buffer = EAMDSP_ENC_STREAM_SOURCE_BUFFER_INVALID;
      return (-2);
      break;
  }

  fmt->enc_win_width = format->enc_win.width;
  fmt->enc_win_height = format->enc_win.height;
  fmt->rotate_cw = format->rotate_cw ? 1 : 0;

  return 0;
}

static int __v5_get_stream_overlay_pixel_format(int iav_fd, int stream_id)
{
  DUNUSED(iav_fd);
  DUNUSED(stream_id);
  return 0;
}

static int __v5_query_source_buffer_info (int iav_fd, amba_dsp_source_buffer_info_t *info)
{
  struct iav_video_proc vproc;
  struct iav_dptz *dptz;

  memset (&vproc, 0, sizeof (vproc) );
  vproc.cid = IAV_VIDEO_PROC_DPTZ;
  /* FIXME: Use channel 0 by default */
  dptz = &vproc.arg.dptz;
  dptz->channel_id = 0;
  dptz->buf_id = info->buf_id;

  if (ioctl (iav_fd, IAV_IOC_GET_VIDEO_PROC, &vproc) < 0) {
    perror ("IAV_IOC_GET_VIDEO_PROC");
    DPRINT_ERROR ("IAV_IOC_GET_VIDEO_PROC fail, errno %d\n", errno);
    return (-1);
  }

  info->size_width = dptz->buf_cfg.output.width;
  info->size_height = dptz->buf_cfg.output.height;

  info->crop_size_x = dptz->buf_cfg.input.width;
  info->crop_size_y = dptz->buf_cfg.input.height;
  info->crop_pos_x = dptz->buf_cfg.input.x;
  info->crop_pos_y = dptz->buf_cfg.input.y;

  return 0;
}

static int __v5_check_iav_state(int iav_fd, unsigned char *decode_mode)
{
  int state;
  if (ioctl(iav_fd, IAV_IOC_GET_IAV_STATE, &state) < 0) {
    perror("IAV_IOC_GET_IAV_STATE");
    exit(2);
  }

  if ((state != IAV_STATE_PREVIEW) && (state != IAV_STATE_ENCODING) &&
      (state != IAV_STATE_DECODING)) {
    DPRINT_ERROR("IAV is not in preview / encoding /decoding state, cannot get yuv buf!\n");
    return -1;
  }

  if (state == IAV_STATE_DECODING) {
    if (decode_mode != NULL) {
      *decode_mode = 1;
    }
  }

  return 0;
}

static int __v5_get_iav_state(int iav_fd)
{
  int state = IAV_STATE_INIT;
  if (ioctl(iav_fd, IAV_IOC_GET_IAV_STATE, &state) < 0) {
    perror("IAV_IOC_GET_IAV_STATE");
    exit(2);
  }

  return state;
}

static int __v5_get_resource_info(int iav_fd, amba_resource_info_t *info)
{
  struct iav_system_resource resource;
  struct iav_pyramid_cfg pyramid_cfg;
  //struct iav_video_proc vproc = {0};
  unsigned char i = 0, j = 0, frame_rate = 0;
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
  info->img_scale_enable = resource.img_scale_cfg.enable;
  info->img_scale_max_input_width = resource.img_scale_cfg.max_input_width;
  info->img_scale_max_input_height = INT_MAX;
  info->img_scale_max_output_width = resource.img_scale_cfg.max_output_width;
  info->img_scale_max_output_height = INT_MAX;
  info->enc_raw_rgb = resource.enc_raw_rgb;
  info->enc_raw_nv12 = resource.enc_raw_nv12;
  info->enc_raw_yuv = resource.enc_raw_yuv;

  for (i = 0; i < resource.chan_num; ++i) {
    info->vcap_mode_flag[i] = resource.chan_cfg[i].vcap_mode_flags;
    info->scale_pass_num[i] = resource.chan_cfg[i].pass_num;
    info->vsrc_id[i] = resource.chan_cfg[i].vsrc_id;
  }

  /* get canvas & pyramid buffer info from IAV */
  for (i = 0; i < resource.canvas_num; i++) {
    /*if (resource.canvas_cfg[i].type != IAV_CANVAS_TYPE_OFF) {
    info->canvas_num++;
    }*/
    frame_rate = resource.enable_hp_fps ? resource.canvas_cfg[i].frame_rate_hp : resource.canvas_cfg[i].frame_rate;
    if (frame_rate != 0) {
      info->pts_intval[i] = DAMBA_HWTIMER_OUTPUT_FREQ / frame_rate;
      info->canvas_fps[i] = frame_rate;
    }
    info->canvas_mf_enable[i] = resource.canvas_cfg[i].manual_feed;
    info->canvas_yuv_buffer_disable[i] = resource.canvas_cfg[i].disable_yuv_dram;
    info->canvas_me_buffer_disable[i] = resource.canvas_cfg[i].disable_me_dram;
    info->canvas_enc_dummy_latency[i] = resource.canvas_cfg[i].enc_dummy_latency;
    info->canvas_height[i] = resource.canvas_cfg[i].max.height;
    info->canvas_width[i] = resource.canvas_cfg[i].max.width;
  }

  for (i = 0; i < info->channel_num; i++) {
    memset(&pyramid_cfg, 0, sizeof(struct iav_pyramid_cfg));
    pyramid_cfg.chan_id = i;
    if (ioctl(iav_fd, IAV_IOC_GET_PYRAMID_CFG, &pyramid_cfg) < 0) {
      perror("IAV_IOC_GET_PYRAMID_CFG\n");
      return -1;
    }
    info->pyramid_fps[i] = resource.enable_hp_fps ? pyramid_cfg.frame_rate_hp : pyramid_cfg.frame_rate;
    info->pyramid_manual_feed[i] = pyramid_cfg.manual_feed;

    for (j = 0; j < IAV_MAX_PYRAMID_LAYERS; j++) {
      info->pyramid_height[i][j] = pyramid_cfg.crop_win[j].height;
      info->pyramid_width[i][j] = pyramid_cfg.crop_win[j].width;
    }
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

static unsigned int __get_yuv_buffer_size(struct iav_yuv_cap *yuv_cap, int format)
{
  /* layout: luma + chroma + convert_chroma(if yuv type needs to convert) */
  unsigned int luma_size;
  unsigned int total_size;

  luma_size = yuv_cap->pitch * ROUND_UP(yuv_cap->height, 16);

  if (yuv_cap->format == IAV_YUV_FORMAT_YUV420) {
    switch (format) {
    case YUV420_IYUV:
    case YUV420_YV12:
    case YUV420_NV12:
      total_size = (luma_size * 3) >> 1;
      break;
    case YUV444:
      total_size = (luma_size * 7) >> 1;
      break;
    default:
      total_size = 0;
      break;
    }
  } else if (yuv_cap->format == IAV_YUV_FORMAT_YUV422) {
    switch (format) {
    case YUV422_NV16:
      total_size = luma_size * 2;
      break;
    case YUV422_YU16:
    case YUV422_YV16:
      total_size = luma_size * 3;
      break;
    case YUV444:
      total_size = luma_size * 4;
      break;
    default:
      total_size = 0;
      break;
    }
  } else {
    total_size = 0;
  }

  total_size = ROUND_UP(total_size, 16);

  return total_size;
}

static int __get_yuv_format(int format, struct iav_yuv_cap *yuv_cap)
{
  int data_format = format;

  if (data_format == AUTO_FORMAT) {
    if (yuv_cap->format == IAV_YUV_FORMAT_YUV420) {
      data_format = YUV420_NV12;
    } else if (yuv_cap->format == IAV_YUV_FORMAT_YUV422) {
      data_format = YUV422_NV16;
    } else {
      printf("Unknown YUV format: %d\n", yuv_cap->format);
    }
  } else {
    /* Auto change the format between 420 and 422. */
    switch (data_format) {
      case YUV420_IYUV:
      case YUV420_YV12:
      case YUV420_NV12:
        /* YUV422 to YUV420 is not supported, change save format to YV16 */
        if (yuv_cap->format == IAV_YUV_FORMAT_YUV422) {
          data_format = YUV422_YV16;
        }
        break;
      case YUV422_YU16:
      case YUV422_YV16:
      case YUV422_NV16:
        /* YUV420 to YUV422 is not supported, change save format to NV12 */
        if (yuv_cap->format == IAV_YUV_FORMAT_YUV420) {
          data_format = YUV420_NV12;
        }
        break;
      default:
        break;
    }
  }

  return data_format;
}

static int __copy_yuv_data(int iav_fd, amba_gdma_buf_t *gdma_ctx,
  int dmabuf_fd, struct iav_yuv_cap *yuv_cap, unsigned char is_canvas, unsigned char is_manual_feed)
{
  struct iav_gdma_copy gdma_copy = {0};
  int rval = 0;
  unsigned int dst_yuv_pitch = yuv_cap->pitch;

  if (gdma_ctx->dst_yuv_pitch > 0) {
    dst_yuv_pitch = gdma_ctx->dst_yuv_pitch;
  }

  gdma_copy.src_skip_cache_sync = 1;
  gdma_copy.src_offset = yuv_cap->y_addr_offset;
  gdma_copy.dst_offset = 0;
  gdma_copy.src_pitch = yuv_cap->pitch;
  gdma_copy.dst_pitch = dst_yuv_pitch;
  gdma_copy.width = yuv_cap->width;
  gdma_copy.height = yuv_cap->height;

  if (dmabuf_fd > 0) {
    gdma_copy.src_dma_buf_fd = dmabuf_fd;
    gdma_copy.src_use_dma_buf_fd = 1;
  } else {
    if (is_canvas) {
      gdma_copy.src_mmap_type = is_manual_feed ? IAV_PART_CANVAS_POOL : IAV_PART_DSP;
    } else {
      gdma_copy.src_mmap_type = is_manual_feed ? IAV_PART_PYRAMID_POOL : IAV_PART_DSP;
    }
  }
  if (gdma_ctx->dma_buf_fd > 0) {
    gdma_copy.dst_dma_buf_fd = gdma_ctx->dma_buf_fd;
    gdma_copy.dst_use_dma_buf_fd = 1;
  } else {
    gdma_copy.dst_mmap_type = gdma_ctx->gdma_part_id;
  }
  if (ioctl(iav_fd, IAV_IOC_GDMA_COPY, &gdma_copy) < 0) {
    perror("IAV_IOC_GDMA_COPY");
    rval = -1;
    goto COPY_YUV_DATA_EXIT;
  }

  gdma_ctx->y_addr_offset = 0;

  if (yuv_cap->format != IAV_YUV_FORMAT_YUV400) {

    gdma_copy.src_offset = yuv_cap->uv_addr_offset;
    gdma_copy.dst_offset = dst_yuv_pitch * yuv_cap->height;

    if (yuv_cap->format == IAV_YUV_FORMAT_YUV420) {
      gdma_copy.height = yuv_cap->height / 2;
    } else {
      gdma_copy.height = yuv_cap->height;
    }
    if (ioctl(iav_fd, IAV_IOC_GDMA_COPY, &gdma_copy) < 0) {
      perror("IAV_IOC_GDMA_COPY");
      rval = -1;
      goto COPY_YUV_DATA_EXIT;
    }

    gdma_ctx->uv_addr_offset = dst_yuv_pitch * yuv_cap->height;
  }

COPY_YUV_DATA_EXIT:
  return rval;
}

static int __copy_yuv_me_data(int iav_fd, amba_gdma_buf_t *gdma_ctx,
  int dmabuf_fd, struct iav_yuv_cap *yuv_cap, struct iav_me_cap *me0_cap, struct iav_me_cap *me1_cap,
  unsigned char is_canvas, unsigned char is_manual_feed,
  unsigned char need_me0, unsigned char need_me1)
{
  struct iav_gdma_copy gdma_copy = {0};
  int rval = 0;
  unsigned int offset = 0;
  unsigned int dst_yuv_pitch = yuv_cap->pitch;
  unsigned int dst_me0_pitch = 0;
  unsigned int dst_me1_pitch = 0;

  if (gdma_ctx->dst_yuv_pitch > 0) {
    dst_yuv_pitch = gdma_ctx->dst_yuv_pitch;
  }

  gdma_copy.src_skip_cache_sync = 1;
  gdma_copy.src_offset = yuv_cap->y_addr_offset;
  gdma_copy.dst_offset = 0;
  gdma_copy.src_pitch = yuv_cap->pitch;
  gdma_copy.dst_pitch = dst_yuv_pitch;
  gdma_copy.width = yuv_cap->width;
  gdma_copy.height = yuv_cap->height;

  if (dmabuf_fd > 0) {
    gdma_copy.src_dma_buf_fd = dmabuf_fd;
    gdma_copy.src_use_dma_buf_fd = 1;
  } else {
    if (is_canvas) {
      gdma_copy.src_mmap_type = is_manual_feed ? IAV_PART_CANVAS_POOL : IAV_PART_DSP;
    } else {
      gdma_copy.src_mmap_type = is_manual_feed ? IAV_PART_PYRAMID_POOL : IAV_PART_DSP;
    }
  }
  if (gdma_ctx->dma_buf_fd > 0) {
    gdma_copy.dst_dma_buf_fd = gdma_ctx->dma_buf_fd;
    gdma_copy.dst_use_dma_buf_fd = 1;
  } else {
    gdma_copy.dst_mmap_type = gdma_ctx->gdma_part_id;
  }
  if (ioctl(iav_fd, IAV_IOC_GDMA_COPY, &gdma_copy) < 0) {
    perror("IAV_IOC_GDMA_COPY");
    rval = -1;
    goto COPY_YUV_DATA_EXIT;
  }

  gdma_ctx->y_addr_offset = 0;
  offset += gdma_copy.dst_pitch * gdma_copy.height;

  if (yuv_cap->format != IAV_YUV_FORMAT_YUV400) {

    gdma_copy.src_offset = yuv_cap->uv_addr_offset;
    gdma_copy.dst_offset = offset;

    if (yuv_cap->format == IAV_YUV_FORMAT_YUV420) {
      gdma_copy.height = yuv_cap->height / 2;
    } else {
      gdma_copy.height = yuv_cap->height;
    }
    if (ioctl(iav_fd, IAV_IOC_GDMA_COPY, &gdma_copy) < 0) {
      perror("IAV_IOC_GDMA_COPY");
      rval = -1;
      goto COPY_YUV_DATA_EXIT;
    }

    gdma_ctx->uv_addr_offset = offset;
    offset += gdma_copy.dst_pitch * gdma_copy.height;
  }

  if (need_me0) {
    dst_me0_pitch = me0_cap->pitch;
    if (gdma_ctx->dst_me0_pitch > 0) {
      dst_me0_pitch = gdma_ctx->dst_me0_pitch;
    }
    gdma_copy.src_offset = me0_cap->data_addr_offset;
    gdma_copy.dst_offset = offset;
    gdma_copy.src_pitch = me0_cap->pitch;
    gdma_copy.dst_pitch = dst_me0_pitch;
    gdma_copy.width = me0_cap->width;
    gdma_copy.height = me0_cap->height;

    if (ioctl(iav_fd, IAV_IOC_GDMA_COPY, &gdma_copy) < 0) {
      perror("IAV_IOC_GDMA_COPY");
      rval = -1;
      goto COPY_YUV_DATA_EXIT;
    }

    gdma_ctx->me0_addr_offset = offset;
    offset += gdma_copy.dst_pitch * gdma_copy.height;
  }

  if (need_me1) {
    dst_me1_pitch = me1_cap->pitch;
    if (gdma_ctx->dst_me1_pitch > 0) {
      dst_me1_pitch = gdma_ctx->dst_me1_pitch;
    }
    gdma_copy.src_offset = me1_cap->data_addr_offset;
    gdma_copy.dst_offset = offset;
    gdma_copy.src_pitch = me1_cap->pitch;
    gdma_copy.dst_pitch = dst_me1_pitch;
    gdma_copy.width = me1_cap->width;
    gdma_copy.height = me1_cap->height;

    if (ioctl(iav_fd, IAV_IOC_GDMA_COPY, &gdma_copy) < 0) {
      perror("IAV_IOC_GDMA_COPY");
      rval = -1;
      goto COPY_YUV_DATA_EXIT;
    }

    gdma_ctx->me1_addr_offset = offset;
    offset += gdma_copy.dst_pitch * gdma_copy.height;
  }

COPY_YUV_DATA_EXIT:
  return rval;
}

static int __v5_capture_preview_buffer (int iav_fd, amba_dsp_query_yuv_buffer_t *yuv_buffer)
{
  struct iav_querydesc query_desc;
  struct iav_canvasdesc *canvas_desc;
  struct iav_yuv_cap *yuv_cap = NULL;
  struct iav_me_cap *me0_cap = NULL, *me1_cap = NULL;
  amba_canvas_opt_t *opt = &yuv_buffer->canvas_options;
  amba_gdma_buf_t *gdma_ctx = NULL;
  amba_yuv_buf_t *yuv_ctx = NULL;
  amba_me_buf_t *me_ctx = NULL;
  int rval = 0;
  unsigned int buf = 0;
  unsigned int yuv_buffer_size = 0;
  unsigned int me0_buffer_size = 0, me1_buffer_size = 0;

  do {
    for (buf = 0; buf < opt->canvas_num; buf++) {
      /* query canvas from IAV */
      if (!(opt->canvas_buffer_map & (1 << buf))) {
        continue;
      }

      if (opt->canvas_yuv_buffer_disable[buf]) {
        DPRINT_ERROR("Canvas[%d] yuv buffer is disabled, cannot get yuv data!\n", buf);
        rval = COM_ECODE_BAD_STATE;
        break;
      }

      memset (&query_desc, 0x0, sizeof (query_desc) );
      query_desc.qid = IAV_DESC_CANVAS;
      canvas_desc = &query_desc.arg.canvas;
      canvas_desc->canvas_id = buf;
      canvas_desc->discard_cached_items = opt->discard_cached_items;
      canvas_desc->query_extra_raw = opt->query_extra_raw_info_flag;
      canvas_desc->yuv_use_dma_buf_fd = !!(yuv_buffer->canvas_map_thru_dmabuf & (1 << buf));
      canvas_desc->me_use_dma_buf_fd = !!(yuv_buffer->canvas_map_thru_dmabuf & (1 << buf));
      canvas_desc->skip_cache_sync = 1;
      canvas_desc->idsp_proc_done_pts = 0;
      if (!yuv_buffer->non_block_flag) {
        canvas_desc->non_block_flag &= ~IAV_BUFCAP_NONBLOCK;
      } else {
        canvas_desc->non_block_flag |= IAV_BUFCAP_NONBLOCK;
      }

      rval = ioctl(iav_fd, IAV_IOC_QUERY_DESC, &query_desc);
      if (rval) {
        if ((errno == EINTR) || (errno == EAGAIN)) {
          rval = COM_ECODE_TRY_AGAIN;
          break;
        }
        perror ("IAV_IOC_QUERY_DESC");
        DPRINT_ERROR ("IAV_IOC_QUERY_DESC fail, errno %d\n", errno);
        rval = COM_ECODE_BAD_STATE;
        break;
      }

      yuv_cap = &canvas_desc->yuv;

      if ((yuv_cap->y_addr_offset == 0) ||
        ((yuv_cap->uv_addr_offset == 0) && (yuv_cap->format != IAV_YUV_FORMAT_YUV400))) {
        DPRINT_ERROR("YUV buffer [%08x] address is NULL!\n", opt->canvas_buffer_map);
        rval = COM_ECODE_BAD_STATE;
        break;
      }

      yuv_ctx = &yuv_buffer->yuv_ctx[buf];

      yuv_ctx->width = yuv_cap->width;
      yuv_ctx->height = yuv_cap->height;
      yuv_ctx->pitch = yuv_cap->pitch;
      yuv_ctx->seq_num = yuv_cap->seq_num;
      yuv_ctx->format = yuv_cap->format;
      yuv_ctx->dsp_pts = yuv_cap->dsp_pts;
      yuv_ctx->mono_pts = yuv_cap->mono_pts;
      yuv_ctx->y_addr_offset = yuv_cap->y_addr_offset;
      yuv_ctx->uv_addr_offset = yuv_cap->uv_addr_offset;

      if (opt->capture_me0) {
        if (opt->canvas_me_buffer_disable[buf]) {
          DPRINT_ERROR("Canvas[%d] me buffer is disabled, cannot get me data!\n", buf);
          rval = COM_ECODE_BAD_STATE;
          break;
        }
        me0_cap = &canvas_desc->me0;

        if (me0_cap->width == 0) {
          DPRINT_ERROR("Canvas[%d] doesn't have ME0 buffer!\n", buf);
          rval = -1;
          break;
        }
        me_ctx = &yuv_buffer->me0_ctx[buf];

        me_ctx->width = me0_cap->width;
        me_ctx->height = me0_cap->height;
        me_ctx->pitch = me0_cap->pitch;
        me_ctx->seq_num = me0_cap->seq_num;
        me_ctx->dsp_pts = me0_cap->dsp_pts;
        me_ctx->mono_pts = me0_cap->mono_pts;
        me_ctx->data_addr_offset = me0_cap->data_addr_offset;

        me0_buffer_size = me0_cap->pitch * ROUND_UP(me0_cap->height, 16);
      }

      if (opt->capture_me1) {
        if (opt->canvas_me_buffer_disable[buf]) {
          DPRINT_ERROR("Canvas[%d] me buffer is disabled, cannot get me data!\n", buf);
          rval = COM_ECODE_BAD_STATE;
          break;
        }
        me1_cap = &canvas_desc->me1;

        if (me1_cap->width == 0) {
          DPRINT_ERROR("Canvas[%d] doesn't have ME1 buffer!\n", buf);
          rval = -1;
          break;
        }
        me_ctx = &yuv_buffer->me1_ctx[buf];

        me_ctx->width = me1_cap->width;
        me_ctx->height = me1_cap->height;
        me_ctx->pitch = me1_cap->pitch;
        me_ctx->seq_num = me1_cap->seq_num;
        me_ctx->dsp_pts = me1_cap->dsp_pts;
        me_ctx->mono_pts = me1_cap->mono_pts;
        me_ctx->data_addr_offset = me1_cap->data_addr_offset;

        me1_buffer_size = me1_cap->pitch * ROUND_UP(me1_cap->height, 16);
      }

      if (yuv_buffer->gdma_copy_enable) {
        gdma_ctx = &yuv_buffer->gdma_ctx[buf];

        yuv_buffer_size = __get_yuv_buffer_size(yuv_cap,
            __get_yuv_format(AUTO_FORMAT, yuv_cap));

        if (yuv_buffer_size == 0) {
          DPRINT_ERROR("buffer size need allocate is 0!.\n");
          rval = COM_ECODE_BAD_STATE;
          break;
        }

        if (yuv_buffer_size + me0_buffer_size + me1_buffer_size > gdma_ctx->gdma_buf_size) {
          __v5_gdma_free_buf(iav_fd, gdma_ctx);
        }

        if (gdma_ctx->dma_buf_fd < 0) {
          if (__v5_gdma_alloc_buf(iav_fd, yuv_buffer_size + me0_buffer_size + me1_buffer_size, gdma_ctx, 1)) {
            DPRINT_ERROR("alloc gdma buf failed.\n");
            rval = COM_ECODE_BAD_STATE;
            break;
          }
        }

        /* copy canvas data to prealloc dst buffer through gdma */

        rval = __copy_yuv_me_data(iav_fd, gdma_ctx, canvas_desc->yuv_dma_buf_fd,
          yuv_cap, me0_cap, me1_cap, 1, opt->canvas_mf_enable[buf],
          opt->capture_me0, opt->capture_me1);
        if (canvas_desc->yuv_dma_buf_fd >= 0) {
          close(canvas_desc->yuv_dma_buf_fd);
          canvas_desc->yuv_dma_buf_fd = -1;
        }
        if (canvas_desc->me_dma_buf_fd >= 0) {
          close(canvas_desc->me_dma_buf_fd);
          canvas_desc->me_dma_buf_fd = -1;
        }
        if (rval < 0) {
          DPRINT_ERROR("Failed to copy yuv data of buf [%08x].\n", opt->canvas_buffer_map);
          rval = COM_ECODE_BAD_STATE;
          break;
        }

      }

    }
  } while (0);

  return rval;
}

static int __v5_capture_pyramid_buffer (int iav_fd, amba_dsp_query_yuv_buffer_t *yuv_buffer)
{
  int buf = 0;
  amba_pyramid_opt_t *opt = &yuv_buffer->pyramid_options;
  amba_gdma_buf_t *gdma_ctx = NULL;
  amba_yuv_buf_t *yuv_ctx = NULL;
  struct iav_querydesc query_desc;
  struct iav_yuv_cap *pyramid_cap;
  struct iav_feed_pyramid feed_pyramid;
  struct iav_pyramiddesc *pyramid = NULL;
  unsigned int yuv_buffer_size = 0;
  int rval = 0;

  do {
    if (yuv_buffer->decode_mode) {
      if (opt->channel_id >= DIAV_MAX_DECODER_NUMBER) {
        DPRINT_ERROR("Invalid channel id[%d].", opt->channel_id);
        rval = COM_ECODE_BAD_STATE;
        break;
      }
    }

    /* query pyramid from IAV */
    memset(&query_desc, 0, sizeof(query_desc));
    memset(&feed_pyramid, 0x0, sizeof(feed_pyramid));

    query_desc.qid = IAV_DESC_PYRAMID;
    pyramid = &query_desc.arg.pyramid;
    pyramid->chan_id = opt->channel_id;
    pyramid->use_dma_buf_fd = !!yuv_buffer->canvas_map_thru_dmabuf;
    pyramid->skip_cache_sync = 1;
    if (!yuv_buffer->non_block_flag) {
      pyramid->non_block_flag &= ~IAV_BUFCAP_NONBLOCK;
    } else {
      pyramid->non_block_flag |= IAV_BUFCAP_NONBLOCK;
    }

    /* for pyramid manual feed case, feed pyramid first */
    if (opt->pyramid_manual_feed[opt->channel_id]) {
      feed_pyramid.chan_id = opt->channel_id;
      if (ioctl(iav_fd, IAV_IOC_FEED_PYRAMID_BUF, &feed_pyramid) < 0) {
        perror("IAV_IOC_FEED_PYRAMID_BUF");
        rval = COM_ECODE_BAD_STATE;
        break;
      }
    }

    if (ioctl(iav_fd, IAV_IOC_QUERY_DESC, &query_desc) < 0) {
      if ((errno == EINTR) || (errno == EAGAIN)) {
        rval = COM_ECODE_TRY_AGAIN;
        break;
      } else {
        perror("IAV_IOC_QUERY_DESC");
        rval = COM_ECODE_BAD_STATE;
        break;
      }
    }

    /* save pyramid layers in queried pyramid one by one */
    for (buf = 0; buf < IAV_MAX_PYRAMID_LAYERS; ++buf) {
      if ((opt->pyramid_buffer_map & (1 << buf)) == 0) {
        continue;
      }

      if ((pyramid->layers_map & (1 << buf)) == 0) {
        DPRINT_ERROR("Pyramid channel %d: layer %d is not switched on\n",
            opt->channel_id, buf);
        continue;
      }

      pyramid_cap = &pyramid->layers[buf];

      if ((pyramid_cap->y_addr_offset == 0) ||
        (pyramid_cap->uv_addr_offset == 0)) {
        DPRINT_ERROR("Pyramid layer %d YUV buffer address is NULL!\n", buf);
        rval = COM_ECODE_BAD_STATE;
        break;
      }

      yuv_ctx = &yuv_buffer->yuv_ctx[buf];

      yuv_ctx->width = pyramid_cap->width;
      yuv_ctx->height = pyramid_cap->height;
      yuv_ctx->pitch = pyramid_cap->pitch;
      yuv_ctx->seq_num = pyramid_cap->seq_num;
      yuv_ctx->format = pyramid_cap->format;
      yuv_ctx->dsp_pts = pyramid_cap->dsp_pts;
      yuv_ctx->mono_pts = pyramid_cap->mono_pts;
      yuv_ctx->y_addr_offset = pyramid_cap->y_addr_offset;
      yuv_ctx->uv_addr_offset = pyramid_cap->uv_addr_offset;

      /* allocate dst buffer for gdma copy */

      if (yuv_buffer->gdma_copy_enable) {
        gdma_ctx = &yuv_buffer->gdma_ctx[buf];
        yuv_buffer_size = __get_yuv_buffer_size(pyramid_cap,
            __get_yuv_format(AUTO_FORMAT, pyramid_cap));

        if (yuv_buffer_size == 0) {
          DPRINT_ERROR("buffer size need allocate is 0!.\n");
          return -1;
        }

        if (yuv_buffer_size > gdma_ctx->gdma_buf_size) {
          __v5_gdma_free_buf(iav_fd, gdma_ctx);
        }

        if (gdma_ctx->gdma_part_id < 0 && gdma_ctx->dma_buf_fd < 0) {
          if (__v5_gdma_alloc_buf(iav_fd, yuv_buffer_size, gdma_ctx, 1)) {
            DPRINT_ERROR("alloc gdma buf failed.\n");
            return -1;
          }
        }

        /* copy pyramid layers data to prealloc dst buffer through gdma */
        if (__copy_yuv_data(iav_fd, gdma_ctx, -1, pyramid_cap, 0,
            yuv_buffer->decode_mode || opt->pyramid_manual_feed[opt->channel_id]) < 0) {
          DPRINT_ERROR("Failed to copy yuv data of buf [%d].\n", buf);
          rval = COM_ECODE_BAD_STATE;
          break;
        }
      }

    }

    /* for manual feed case, release the queried pyramid buffer */
    if (opt->pyramid_manual_feed[opt->channel_id]) {
      if (ioctl(iav_fd, IAV_IOC_RELEASE_PYRAMID_BUF, pyramid) < 0) {
        perror("IAV_IOC_RELEASE_PYRAMID_BUF");
        rval = COM_ECODE_BAD_STATE;
        break;
      }
    }
  }while (0);

  return rval;
}

static int __copy_raw_data(int iav_fd, int is_raw, int is_raw_ce, struct iav_rawbufdesc *raw_desc,
    amba_dsp_query_yuv_buffer_t *yuv_buffer)
{
  u32 buffer_size = 0, raw_size = 0, raw_ce_size = 0;
  int rval = 0;
  struct iav_gdma_copy gdma_copy = {0};
  u8 height_multi = 1, height_div = 1;
  amba_gdma_buf_t *gdma_ctx = NULL;

  switch (raw_desc->format) {
    case IAV_RAW_FORMAT_YUV422:
      height_multi = 2;
      break;
    case IAV_RAW_FORMAT_YUV420:
      height_multi = 3;
      height_div = 2;
      break;
    default:
      height_multi = 1;
      height_div = 1;
      break;
  }

  if (is_raw) {
    raw_size = raw_desc->width_in_byte * raw_desc->height * height_multi / height_div;
  }
  if (is_raw_ce) {
    raw_ce_size = raw_desc->ce_width_in_byte * raw_desc->height * height_multi / height_div;
  }

  buffer_size = raw_size + raw_ce_size;

  /* allocate dst buffer for gdma */
  gdma_ctx = &yuv_buffer->gdma_ctx[yuv_buffer->raw_options.vinc_id];

  if (buffer_size > gdma_ctx->gdma_buf_size) {
    __v5_gdma_free_buf(iav_fd, gdma_ctx);
  }

  if (gdma_ctx->gdma_part_id < 0) {
    if (__v5_gdma_alloc_buf(iav_fd, buffer_size, gdma_ctx, 0)) {
      DPRINT_ERROR("alloc gdma buf failed.\n");
      return -1;
    }
  }

  gdma_copy.src_skip_cache_sync = 1;
  /* do gdma copy */
  gdma_copy.src_mmap_type = IAV_PART_DSP;
  gdma_copy.dst_dma_buf_fd = gdma_ctx->gdma_part_id;
  gdma_copy.dst_use_dma_buf_fd = 1;

  if (is_raw) {
    gdma_copy.src_offset = raw_desc->raw_addr_offset;
    gdma_copy.dst_offset = 0;
    gdma_copy.src_pitch = raw_desc->pitch;
    gdma_copy.height = raw_desc->height * height_multi / height_div;
    gdma_copy.dst_pitch = raw_desc->width_in_byte;
    gdma_copy.width = raw_desc->width_in_byte;

    if (ioctl(iav_fd, IAV_IOC_GDMA_COPY, &gdma_copy) < 0) {
      perror("IAV_IOC_GDMA_COPY");
      rval = COM_ECODE_BAD_STATE;
    }
  }

  if (is_raw_ce) {
    gdma_copy.src_offset = raw_desc->ce_addr_offset;
    gdma_copy.dst_offset = raw_size;
    gdma_copy.src_pitch = raw_desc->ce_pitch;
    gdma_copy.height = raw_desc->height * height_multi / height_div;
    gdma_copy.dst_pitch = raw_desc->ce_width_in_byte;
    gdma_copy.width = raw_desc->ce_width_in_byte;

    if (ioctl(iav_fd, IAV_IOC_GDMA_COPY, &gdma_copy) < 0) {
      perror("IAV_IOC_GDMA_COPY");
      rval = COM_ECODE_BAD_STATE;
    }
  }

  return rval;
}

static int __v5_capture_raw(int iav_fd, amba_dsp_query_yuv_buffer_t *yuv_buffer)
{
  amba_raw_opt_t *opt = &yuv_buffer->raw_options;
  amba_raw_buf_t *raw_ctx = NULL;

  struct iav_rawbufdesc *raw_desc;
  struct iav_querydesc query_desc;
  int rval = 0;

  do {
    /* query raw from IAV */
    memset(&query_desc, 0, sizeof(query_desc));
    query_desc.qid = IAV_DESC_RAW;
    raw_desc = &query_desc.arg.raw;
    raw_desc->vin_id = opt->vinc_id;
    raw_desc->skip_cache_sync = 1;

    if (ioctl(iav_fd, IAV_IOC_QUERY_DESC, &query_desc) < 0) {
      if ((errno == EINTR) || (errno == EAGAIN)) {
        rval = COM_ECODE_TRY_AGAIN;
        break;
      } else {
        perror("IAV_IOC_QUERY_DESC");
        rval = COM_ECODE_BAD_STATE;
        break;
      }
    }

    /* check quried raw info */
    if (!raw_desc->pitch || !raw_desc->height || !raw_desc->width_in_pixel) {
      DPRINT_ERROR("Raw data resolution %ux%u with pitch %u is incorrect!\n",
      raw_desc->width_in_pixel, raw_desc->height, raw_desc->pitch);
      rval = COM_ECODE_BAD_STATE;
      break;
    }

    if (opt->capture_raw_ce) {
      if (!raw_desc->ce_pitch || !raw_desc->height || !raw_desc->ce_width_in_pixel) {
        DPRINT_ERROR("Contrast enhance raw data resolution %ux%u with pitch %u is incorrect!\n",
        raw_desc->ce_width_in_pixel, raw_desc->height, raw_desc->ce_pitch);
        rval = COM_ECODE_BAD_STATE;
        break;
      }
    }

    raw_ctx = &yuv_buffer->raw_ctx[opt->vinc_id];

    raw_ctx->raw_addr_offset = raw_desc->raw_addr_offset;
    raw_ctx->ce_addr_offset = raw_desc->ce_addr_offset;
    raw_ctx->width = raw_desc->width;
    raw_ctx->height = raw_desc->height;
    raw_ctx->pitch = raw_desc->pitch;
    raw_ctx->seq_num = raw_desc->seq_num;
    raw_ctx->format = raw_desc->format;

    raw_ctx->dsp_pts = raw_desc->dsp_pts;
    raw_ctx->mono_pts = raw_desc->mono_pts;

    /* allocate dst buffer for gdma copy */

    if (yuv_buffer->gdma_copy_enable) {
      if (__copy_raw_data(iav_fd, 1, opt->capture_raw_ce, raw_desc, yuv_buffer) < 0) {
        rval = COM_ECODE_BAD_STATE;
        break;
      }
    }
  }while (0);

  return rval;
}

static int __v5_query_yuv_buffer (int iav_fd, amba_dsp_query_yuv_buffer_t *yuv_buffer)
{
  int ret = 0;
  switch (yuv_buffer->capture_select) {
    case CAPTURE_PREVIEW_BUFFER:
      if (yuv_buffer->canvas_options.query_canvasgrp_flag) {
        //capture_multi_yuv(canvas_buffer_map, canvas_map_thru_dmabuf, frame_count, !info_only);
        printf("not support query_canvasgrp now!\n");
        ret = COM_ECODE_BAD_STATE;
        break;
      } else {
        ret = __v5_capture_preview_buffer(iav_fd, yuv_buffer);
      }
      break;
    case CAPTURE_PYRAMID_BUFFER:
      ret = __v5_capture_pyramid_buffer(iav_fd, yuv_buffer);
      break;
    case CAPTURE_RAW_BUFFER:
      ret = __v5_capture_raw(iav_fd, yuv_buffer);
      break;
    default:
      printf("Invalid capture mode [%d] !\n", yuv_buffer->capture_select);
      ret = COM_ECODE_BAD_STATE;
      break;
  }
  return ret;
}

static int __v5_release_canvas_buffer(int fd_iav, iav_release_canvas_cfg_t *ctx)
{
  struct iav_canvasdesc canvas;

  memset(&canvas, 0, sizeof(struct iav_canvasdesc));
  canvas.canvas_id = ctx->canvas_id;
  canvas.yuv.seq_num = ctx->seq_num;
  canvas.yuv_use_dma_buf_fd = ctx->yuv_use_dma_buf_fd;
  canvas.yuv_dma_buf_fd = ctx->yuv_dma_buf_fd;
  canvas.me_use_dma_buf_fd = ctx->me_use_dma_buf_fd;
  canvas.me_dma_buf_fd = ctx->me_dma_buf_fd;
  canvas.feed_seq_num = ctx->feed_seq_num;
  if (ioctl(fd_iav, IAV_IOC_RELEASE_CANVAS_BUF, &canvas) < 0) {
    perror("IAV_IOC_RELEASE_CANVAS_BUF\n");
    return -1;
  }
  return 0;
}

static int __v5_gdma_copy (int iav_fd, amba_gdma_copy_t *copy)
{
  struct iav_gdma_copy param = {0};
  int ret = 0;

  param.src_offset = copy->src_offset;
  param.dst_offset = copy->dst_offset;
  param.src_pitch = copy->src_pitch;
  param.dst_pitch = copy->dst_pitch;
  param.width = copy->width;
  param.height = copy->height;
  param.src_dma_buf_fd = copy->src_dma_buf_fd;
  param.dst_dma_buf_fd = copy->dst_dma_buf_fd;
  param.src_use_dma_buf_fd = 1;
  param.dst_use_dma_buf_fd = 1;

  ret = ioctl (iav_fd, IAV_IOC_GDMA_COPY, &param);
  if (ret < 0) {
    perror ("IAV_IOC_GDMA_COPY");
    DPRINT_ERROR ("IAV_IOC_GDMA_COPY fail, errno %d\n", errno);
    return ret;
  }

  return 0;
}

static int __v5_dsp_enter_idle_mode (int iav_fd, int vin_off, int no_vout_reset)
{
  struct iav_idle_params idle_params = {0};
  idle_params.poweroff_vin = vin_off;
  idle_params.no_vout_reset = no_vout_reset;

  int ret = ioctl (iav_fd, IAV_IOC_ENTER_IDLE, &idle_params);

  if (0 > ret) {
    perror ("IAV_IOC_ENTER_IDLE");
    DPRINT_ERROR ("enter idle mode fail, errno %d\n", errno);
  }

  return ret;
}

static int __v5_enable_preview(int iav_fd, int no_vout_reset)
{
  struct iav_preview_params prev_params = {0};
  prev_params.no_vout_reset = no_vout_reset;

  AM_IOCTL(iav_fd, IAV_IOC_ENABLE_PREVIEW, &prev_params);

  return 0;
}

static int __v5_set_overlay(int iav_fd, iav_set_overlay_t *overlay_set)
{
  int i = 0, buf_id = 0;
  struct iav_overlay_area *area = NULL;
  struct iav_overlay_insert *overlay_insert = &overlay_set->overlay_insert;
  if (overlay_insert->enable) {
    for (i = 0; i < overlay_set->overlay_max_num; i++) {
      if (overlay_insert->area[i].enable) {
        area = &overlay_insert->area[i];
        buf_id = overlay_set->osd[i].buf_id;
        area->data_addr_offset = overlay_set->osd[i].buf_data[buf_id];
      }
    }

  }

  if (ioctl (iav_fd, IAV_IOC_SET_OVERLAY_INSERT, overlay_insert) < 0) {
    perror ("IAV_IOC_SET_OVERLAY_INSERT");
    return -1;
  }

  return 0;
}


static int __v5_set_frame_sync(int iav_fd, iav_set_overlay_t *overlay_set)
{
  struct iav_stream_cfg sync_frame = {0};
  int i = 0, buf_id = 0;
  struct iav_overlay_area *area = NULL;
  struct iav_overlay_insert *overlay_insert = &overlay_set->overlay_insert;
  if (overlay_insert->enable) {
    for (i = 0; i < overlay_set->overlay_max_num; i++) {
      if (overlay_insert->area[i].enable) {
        area = &overlay_insert->area[i];
        buf_id = overlay_set->osd[i].buf_id;
        area->data_addr_offset = overlay_set->osd[i].buf_data[buf_id];
      }
    }

  }

  memcpy(&sync_frame.arg.overlay, overlay_insert, sizeof(struct iav_overlay_insert));
  sync_frame.id = overlay_insert->id;
  sync_frame.cid = IAV_STMCFG_OVERLAY;
  sync_frame.strm_sync_type = IAV_FRAME_SYNC;

  if (ioctl (iav_fd, IAV_IOC_CFG_FRAME_SYNC_PROC, &sync_frame) < 0) {
    perror ("IAV_IOC_CFG_FRAME_SYNC_PROC");
    return -1;
  }

  return 0;
}


static int __v5_apply_frame_sync(int iav_fd, unsigned int dsp_pts,
    unsigned int stream_updated_map, unsigned int force_update)
{
  struct iav_apply_frame_sync apply;

  memset(&apply, 0, sizeof(apply));
  apply.dsp_pts = dsp_pts;
  apply.force_update = force_update;
  apply.strm_sync_type = IAV_FRAME_SYNC;
  apply.stream_updated_map = stream_updated_map;
  AM_IOCTL(iav_fd, IAV_IOC_APPLY_FRAME_SYNC_PROC, &apply);
  return 0;
}

static unsigned int __v5_get_enc_src_canvas_id(int iav_fd, unsigned int stream_id)
{
  struct iav_stream_cfg format_cfg = {};

  memset(&format_cfg, 0, sizeof(format_cfg));
  format_cfg.id = stream_id;
  format_cfg.cid = IAV_STMCFG_FORMAT;
  AM_IOCTL(iav_fd, IAV_IOC_GET_STREAM_CONFIG, &format_cfg);
  return format_cfg.arg.format.enc_src_id;
}

static unsigned int __v5_get_enc_dummy_latency(int iav_fd, unsigned int canvas_id)
{
  struct iav_system_resource resource;

  memset(&resource, 0, sizeof(struct iav_system_resource));
  resource.encode_mode = DSP_CURRENT_MODE;
  if (ioctl(iav_fd, IAV_IOC_GET_SYSTEM_RESOURCE, &resource) < 0) {
    perror("IAV_IOC_GET_SYSTEM_RESOURCE\n");
    return -1;
  }

  return resource.canvas_cfg[canvas_id].enc_dummy_latency;
}

static int __v5_get_stream_state(int iav_fd, unsigned int stream_id)
{
  struct iav_queryinfo query_info = {};
  struct iav_stream_info *stream_info = NULL;

  memset(&query_info, 0, sizeof(query_info));
  query_info.qid = IAV_INFO_STREAM;
  stream_info = &query_info.arg.stream;
  stream_info->id = stream_id;
  AM_IOCTL(iav_fd, IAV_IOC_QUERY_INFO, &query_info);

  return stream_info->state;
}

static int __v5_set_img_scale(int iav_fd, img_scale_cfg_t *img_scale_cfg)
{
  struct iav_img_scale img_scale = {0};

  img_scale.format = IAV_YUV_FORMAT_YUV420;
  img_scale.non_block_flag = 0;
  img_scale.input.x = img_scale_cfg->input.x;
  img_scale.input.y = img_scale_cfg->input.y;
  img_scale.input.width = img_scale_cfg->input.w;
  img_scale.input.height = img_scale_cfg->input.h;

  img_scale.output.width = img_scale_cfg->output.w;
  img_scale.output.height = img_scale_cfg->output.h;

  img_scale.input_buf.pid = img_scale_cfg->input_buf_pid;
  img_scale.input_buf.use_phys = 0;
  img_scale.input_buf.use_dma_buf_fd = 1;
  img_scale.input_buf.pitch = img_scale_cfg->input_buf_picth;
  img_scale.input_buf.height = img_scale_cfg->input_buf_height;
  img_scale.input_buf.y_offset = 0;
  img_scale.input_buf.uv_offset = img_scale.input_buf.pitch * img_scale.input_buf.height;

  img_scale.output_buf.pid = img_scale_cfg->output_buf_pid;
  img_scale.output_buf.use_phys = 0;
  img_scale.output_buf.use_dma_buf_fd = 1;
  img_scale.output_buf.pitch = img_scale_cfg->output_buf_picth;
  img_scale.output_buf.height = img_scale_cfg->output_buf_height;
  img_scale.output_buf.y_offset = 0;
  img_scale.output_buf.uv_offset = img_scale.output_buf.pitch * img_scale.output_buf.height;

  AM_IOCTL(iav_fd, IAV_IOC_SET_IMG_SCALE, &img_scale);

  return 0;
}

/* For V5, EFR memory can be from IAV_PART_USR partition or other memory, here use anon memory
 * consider multi VIN EFR case
 */
static int __v5_alloc_map_efr_mem(int iav_fd, amba_iav_partition_t *iav_partition)
{
  struct iav_alloc_mem_part alloc_mem_part;

  memset(&alloc_mem_part, 0, sizeof(alloc_mem_part));
  alloc_mem_part.length = iav_partition->request_size;
  alloc_mem_part.enable_cache = 0;

  if (ioctl(iav_fd, IAV_IOC_ALLOC_ANON_MEM_PART, &alloc_mem_part) < 0) {
    DPRINT_ERROR ("Fail to allocate anonymous memory [size = 0x%lx] for efr\n", iav_partition->request_size);
    return -1;
  }

  iav_partition->partition_id = alloc_mem_part.pid;
  iav_partition->allocate_size = alloc_mem_part.length;
  iav_partition->phys_addr = alloc_mem_part.offset;
  if (iav_partition->allocate_size) {
    iav_partition->virt_addr = mmap(NULL, alloc_mem_part.length, PROT_READ | PROT_WRITE,
      MAP_SHARED, iav_fd, alloc_mem_part.offset);
    if (iav_partition->virt_addr == MAP_FAILED) {
      DPRINT_ERROR ("Fail to mmap anonymous memory for efr\n");
      return -1;
    }
  }

  return 0;
}

static int __v5_unmap_efr_mem(int iav_fd, amba_iav_partition_t *iav_partition)
{
  struct iav_alloc_mem_part alloc_mem_part;

  if (iav_partition->allocate_size && iav_partition->virt_addr != NULL) {
      munmap(iav_partition->virt_addr, iav_partition->allocate_size);
      iav_partition->allocate_size = 0;
      iav_partition->virt_addr = NULL;
      iav_partition->phys_addr = 0;
  }

  if (iav_partition->partition_id >= 0) {
    memset(&alloc_mem_part, 0, sizeof(alloc_mem_part));
    alloc_mem_part.pid = iav_partition->partition_id;
    AM_IOCTL(iav_fd, IAV_IOC_FREE_MEM_PART, &alloc_mem_part);
    iav_partition->partition_id = -1;
  }

  return 0;
}

static int __v5_get_efr_setup(int iav_fd, amba_efr_setup_t *efr_setup)
{
  struct iav_raw_enc_setup setup;

  memset(&setup, 0, sizeof(setup));
  AM_IOCTL(iav_fd, IAV_IOC_GET_RAW_ENCODE, &setup);

  efr_setup->raw_frame_size = setup.raw_frame_size;
  efr_setup->raw_daddr_offset = setup.raw_daddr_offset;
  efr_setup->pitch = setup.pitch;

  return 0;
}

static int __v5_set_efr_setup(int iav_fd, amba_efr_setup_t *efr_setup)
{
  struct iav_raw_enc_setup setup;

  memset(&setup, 0, sizeof(setup));
  setup.vinc_id = efr_setup->vinc_id;
  setup.raw_format = efr_setup->raw_format;
  setup.hdec_raw_format = efr_setup->hdec_raw_format;
  setup.pitch = efr_setup->pitch;
  setup.raw_hdec_dpitch = efr_setup->raw_hdec_dpitch;
  setup.raw_frame_size = efr_setup->raw_frame_size;

  setup.frame_pts = efr_setup->frame_pts;
  setup.raw_frame_num = efr_setup->raw_frame_num;
  setup.frame_hw_pts = efr_setup->frame_hw_pts;
  setup.use_ext_buf = efr_setup->use_ext_buf;
  setup.raw_daddr_offset = efr_setup->raw_daddr_offset;
  setup.raw_hdec_daddr_offset = efr_setup->raw_hdec_daddr_offset;
  setup.ext_buf_addr = efr_setup->ext_buf_addr;
  setup.uv_daddr_offset = efr_setup->uv_daddr_offset;

  AM_IOCTL(iav_fd, IAV_IOC_SET_RAW_ENCODE, &setup);

  return 0;
}

static int __v5_wait_efr_done(int iav_fd, int vinc_id)
{
  DUNUSED(vinc_id);
  int ret = 0;

  ret = ioctl(iav_fd, IAV_IOC_WAIT_RAW_ENCODE, 0);
  if (ret < 0) {
    DPRINT_ERROR ("Sleep 1 second to make sure >= 1 frame time!\n");
    sleep(1);
  }

  return 0;
}

static int __v5_efm_lib_init(iav_efm_usr_cfg_t *efm_cfg_ext)
{
  struct efm_usr_cfg cfg;
  int ret = 0;

  memcpy(&cfg, efm_cfg_ext, sizeof(struct efm_usr_cfg));
  ret = efm_lib_init(&cfg);

  return ret;
}

static int __v5_efm_lib_deinit(void)
{
  int ret = 0;

  ret = efm_lib_deinit();

  return ret;
}

static int __v5_efm_get_buf(iav_efm_buf_info_t *buf_info_ext)
{
  struct efm_buf_info buf_info;
  int ret = 0;

  buf_info.stream_id = buf_info_ext->stream_id;
  ret = efm_get_buf(&buf_info);
  memcpy(buf_info_ext, &buf_info, sizeof(struct efm_buf_info));

  return ret;
}

static int __v5_efm_feed_buf(iav_efm_buf_info_t *buf_info_ext, iav_efm_feed_cfg_t *feed_cfg_ext)
{
  struct efm_buf_info buf_info;
  struct efm_feed_cfg feed_cfg;
  int ret = 0;

  memcpy(&buf_info, buf_info_ext, sizeof(struct efm_buf_info));
  memcpy(&feed_cfg, feed_cfg_ext, sizeof(struct efm_feed_cfg));
  ret = efm_feed_buf(&buf_info, &feed_cfg);

  return ret;
}

static int __v5_efm_get_stream_cfg(iav_efm_stream_cfg_t *stream_cfg_ext)
{
  struct efm_stream_cfg stream_cfg;
  int ret = 0;

  stream_cfg.stream_id = stream_cfg_ext->stream_id;
  ret = efm_get_stream_cfg(&stream_cfg);
  memcpy(stream_cfg_ext, &stream_cfg, sizeof(struct efm_stream_cfg));

  return ret;
}

static int __v5_query_canvas_info(int iav_fd, amba_canvas_info_t *info)
{
  struct iav_queryinfo query_info;
  struct iav_canvas_info *canvas_info;

  memset(&query_info, 0, sizeof(struct iav_queryinfo));
  query_info.qid = IAV_INFO_CANVAS;
  canvas_info = &query_info.arg.canvas;
  canvas_info->canvas_id = info->canvas_id;
  AM_IOCTL(iav_fd, IAV_IOC_QUERY_INFO, &query_info);

  info->width = canvas_info->width;
  info->height = canvas_info->height;
  info->yuv_pitch = canvas_info->yuv_pitch;
  info->me0_pitch = canvas_info->me0_pitch;
  info->me1_pitch = canvas_info->me1_pitch;
  info->me0_width = canvas_info->me0_width;
  info->me0_height = canvas_info->me0_height;
  info->me1_width = canvas_info->me1_width;
  info->me1_height = canvas_info->me1_height;

  return 0;
}

static int __v5_query_pyramid_info(int iav_fd, amba_pyramid_info_t *info)
{
  struct iav_queryinfo query_info;
  struct iav_pyramid_layers_info *pyramid_info;

  memset(&query_info, 0, sizeof(struct iav_queryinfo));
  query_info.qid = IAV_INFO_PYRAMID_LAYERS;
  pyramid_info = &query_info.arg.pyramid_layer_info;
  pyramid_info->channel_id = info->channel_id;
  AM_IOCTL(iav_fd, IAV_IOC_QUERY_INFO, &query_info);

  for (int i = 0; i < IAV_MAX_PYRAMID_LAYERS; i++) {
    info->layer_size[i].width = pyramid_info->layer_size[i].width;
    info->layer_size[i].height = pyramid_info->layer_size[i].height;
    info->layer_size[i].pitch = pyramid_info->layer_size[i].pitch;
  }

  return 0;
}

static int __v5_blur_lib_init(int iav_fd)
{
  int ret = 0;

  ret = blur_lib_init(iav_fd);
  return ret;
}

static int __v5_blur_lib_deinit(void)
{
  int ret = 0;

  ret = blur_lib_deinit();
  return ret;
}

static int __v5_blur_get_mem_cfg(iav_blur_mem_cfg_t *cfg_ext)
{
  struct blur_mem_cfg cfg;
  int ret = 0;

  ret = blur_get_mem_cfg(&cfg);
  memcpy(cfg_ext, &cfg, sizeof(struct blur_mem_cfg));

  return ret;
}

static int __v5_blur_set_mem_cfg(iav_blur_mem_cfg_t *cfg_ext)
{
  struct blur_mem_cfg cfg;
  int ret = 0;

  memcpy(&cfg, cfg_ext, sizeof(struct blur_mem_cfg));
  ret = blur_set_mem_cfg(&cfg);

  return ret;
}

static int __v5_blur_get_stream_cfg(iav_blur_stream_cfg_t *stream_cfg_ext)
{
  struct blur_stream_cfg stream_cfg;
  int ret = 0;

  stream_cfg.stream_id = stream_cfg_ext->stream_id;
  ret = blur_get_stream_cfg(&stream_cfg);
  memcpy(stream_cfg_ext, &stream_cfg, sizeof(struct blur_stream_cfg));

  return ret;
}

static int __v5_blur_set_stream_cfg(iav_blur_stream_cfg_t *stream_cfg_ext)
{
  struct blur_stream_cfg stream_cfg;
  int ret = 0;

  memcpy(&stream_cfg, stream_cfg_ext, sizeof(struct blur_stream_cfg));
  ret = blur_set_stream_cfg(&stream_cfg);

  return ret;
}

static int __v5_blur_get_area_buf(iav_blur_area_buf_t *buf_info_ext)
{
  struct blur_area_buf buf_info;
  int ret = 0;

  buf_info.stream_id = buf_info_ext->stream_id;
  buf_info.area_id = buf_info_ext->area_id;
  ret = blur_get_area_buf(&buf_info);
  memcpy(buf_info_ext, &buf_info, sizeof(struct blur_area_buf));

  return ret;
}

static int __v5_blur_put_area_buf(iav_blur_area_buf_t *buf_info_ext)
{
  struct blur_area_buf buf_info;
  int ret = 0;

  memcpy(&buf_info, buf_info_ext, sizeof(struct blur_area_buf));
  ret = blur_put_area_buf(&buf_info);

  return ret;
}

static int __v5_blur_apply(iav_blur_apply_cfg_t *apply_ext)
{
  struct blur_apply_cfg apply;
  int ret = 0;

  apply.stream_map = apply_ext->stream_map;
  apply.frame_sync = apply_ext->frame_sync;
  ret = blur_apply(&apply);

  return ret;
}

static int __v5_blur_set_color(iav_blur_color_cfg_t *color_cfg_ext)
{
  struct blur_color_cfg blur_color_info;
  int ret = 0;

  memcpy(&blur_color_info, color_cfg_ext, sizeof(struct blur_color_cfg));
  ret = blur_set_color(&blur_color_info);

  return ret;
}

static int __v5_blur_get_color(iav_blur_color_cfg_t *color_cfg_ext)
{
  struct blur_color_cfg blur_color_info;
  int ret = 0;

  ret = blur_get_color(&blur_color_info);
  memcpy(color_cfg_ext, &blur_color_info, sizeof(struct blur_color_cfg));

  return ret;
}

static void __setup_v5_al_context (iav_al_t *al)
{
  al->f_get_dsp_mode = __v5_get_dsp_mode;

  al->f_enter_mode = __v5_enter_decode_mode;
  al->f_leave_mode = __v5_leave_decode_mode;
  al->f_create_decoder = __v5_create_decoder;
  al->f_destroy_decoder = __v5_destroy_decoder;
  al->f_query_decode_config = __v5_query_decode_config;

  al->f_trickplay = __v5_decode_trick_play;
  al->f_start = __v5_decode_start;
  al->f_stop = __v5_decode_stop;
  al->f_speed = __v5_decode_speed;
  al->f_request_bsb = __v5_decode_request_bits_fifo;

  al->f_decode = __v5_decode;

  al->f_query_print_decode_bsb_status = __v5_decode_query_bsb_status_and_print;
  al->f_query_print_decode_status = __v5_decode_query_status_and_print;
  al->f_query_decode_bsb_status = __v5_decode_query_bsb_status;
  al->f_query_decode_status = __v5_decode_query_status;
  al->f_decode_wait_vout_dormant = NULL;
  al->f_decode_wake_vout = NULL;
  al->f_decode_wait_eos_flag = NULL;
  al->f_decode_wait_eos = __v5_decode_wait_eos;

  al->f_configure_vout = __configure_vout;

  al->f_get_vout_info = __get_single_vout_info;
  al->f_get_vin_info = __v5_get_vin_info;
  al->f_get_stream_framefactor = __v5_get_stream_framefactor;

  al->f_map_bsb = __v5_map_bsb;
  al->f_map_dsp = __v5_map_dsp;
  al->f_map_overlay = __v5_map_overlay;
  al->f_map_dec_bsb = __v5_map_dec_bsb;

  al->f_unmap_bsb = __v5_unmap_bsb;
  al->f_unmap_dsp = __v5_unmap_dsp;
  al->f_unmap_overlay = __v5_unmap_overlay;
  al->f_unmap_dec_bsb = __v5_unmap_dec_bsb;

  al->f_flush_frame_desc = __v5_flush_frame_desc;

  // use avc sim
  al->f_read_bitstream = __v5_read_bitstream;

  al->f_release_bitstream = __v5_release_bitstream;
  al->f_is_ready_for_read_bitstream = __v5_is_ready_for_read_bitstream;

  al->f_encode_start = __v5_encode_start;
  al->f_encode_stop = __v5_encode_stop;

  al->f_query_encode_stream_info = __v5_query_encode_stream_info;
  al->f_query_encode_stream_fmt = __v5_query_encode_stream_format;
  al->f_get_stream_overlay_pixel_format = __v5_get_stream_overlay_pixel_format;

  al->f_query_source_buffer_info = __v5_query_source_buffer_info;
  al->f_query_yuv_buffer = __v5_query_yuv_buffer;
  al->f_release_canvas_buffer = __v5_release_canvas_buffer;

  al->f_query_canvas_info = __v5_query_canvas_info;
  al->f_query_pyramid_info = __v5_query_pyramid_info;

  al->f_gdma_copy = __v5_gdma_copy;
  al->f_gdma_alloc_buf = __v5_gdma_alloc_buf;
  al->f_gdma_free_buf = __v5_gdma_free_buf;

  al->f_enter_idle_mode = __v5_dsp_enter_idle_mode;
  al->f_enable_preview = __v5_enable_preview;

  // overlay related
  al->f_set_overlay = __v5_set_overlay;
  al->f_set_frame_sync = __v5_set_frame_sync;
  al->f_apply_frame_sync = __v5_apply_frame_sync;

  // blur related
  al->f_blur_lib_init = __v5_blur_lib_init;
  al->f_blur_get_mem_cfg = __v5_blur_get_mem_cfg;
  al->f_blur_set_mem_cfg = __v5_blur_set_mem_cfg;
  al->f_blur_get_stream_cfg = __v5_blur_get_stream_cfg;
  al->f_blur_set_stream_cfg = __v5_blur_set_stream_cfg;
  al->f_blur_get_area_buf = __v5_blur_get_area_buf;
  al->f_blur_put_area_buf = __v5_blur_put_area_buf;
  al->f_blur_apply = __v5_blur_apply;
  al->f_blur_set_color = __v5_blur_set_color;
  al->f_blur_get_color = __v5_blur_get_color;
  al->f_blur_lib_deinit = __v5_blur_lib_deinit;

  // encoding related
  al->f_update_enc_resolution = update_enc_resolution;
  al->f_update_enc_bitrate = update_enc_bitrate;
  al->f_update_enc_framerate = update_enc_framerate;
  al->f_update_enc_bitrate_frameate = update_enc_bitrate_frameate;
  al->f_update_enc_codec_type = update_enc_codec_type;
  al->f_update_enc_gop_structure = update_enc_gop_structure;
  al->f_enc_force_idr = enc_force_idr;

  al->f_check_iav_state = __v5_check_iav_state;
  al->f_get_iav_state = __v5_get_iav_state;
  al->f_get_resource_info = __v5_get_resource_info;
  al->f_get_enc_src_canvas_id = __v5_get_enc_src_canvas_id;
  al->f_get_enc_dummy_latency = __v5_get_enc_dummy_latency;
  al->f_get_stream_state = __v5_get_stream_state;
  al->f_set_img_scale = __v5_set_img_scale;

  // efr related
  al->f_alloc_map_efr_mem = __v5_alloc_map_efr_mem;
  al->f_unmap_efr_mem = __v5_unmap_efr_mem;
  al->f_set_efr_setup = __v5_set_efr_setup;
  al->f_get_efr_setup = __v5_get_efr_setup;
  al->f_wait_efr_done = __v5_wait_efr_done;
  al->f_efm_lib_init = __v5_efm_lib_init;
  al->f_efm_lib_deinit = __v5_efm_lib_deinit;
  al->f_efm_get_buf = __v5_efm_get_buf;
  al->f_efm_feed_buf = __v5_efm_feed_buf;
  al->f_efm_get_stream_cfg = __v5_efm_get_stream_cfg;
}

#elif defined (BUILD_DSP_AMBA_V6)

static int __v6_get_dsp_mode (int iav_fd, amba_dsp_mode_t *mode)
{
  int state = 0;
  int ret = 0;

  ret = ioctl (iav_fd, IAV_IOC_GET_IAV_STATE, &state);

  if (0 > ret) {
    perror ("IAV_IOC_GET_IAV_STATE");
    DPRINT_ERROR ("IAV_IOC_GET_IAV_STATE fail, errno %d\n", errno);
    return ret;
  }

  switch (state) {

    case IAV_STATE_INIT:
      mode->dsp_mode = EAMDSP_MODE_INIT;
      break;

    case IAV_STATE_IDLE:
      mode->dsp_mode = EAMDSP_MODE_IDLE;
      break;

    case IAV_STATE_PREVIEW:
      mode->dsp_mode = EAMDSP_MODE_PREVIEW;
      break;

    case IAV_STATE_ENCODING:
      mode->dsp_mode = EAMDSP_MODE_ENCODE;
      break;

    case IAV_STATE_DECODING:
      mode->dsp_mode = EAMDSP_MODE_DECODE;
      break;

    default:
      DPRINT_ERROR ("un expected dsp mode %d\n", state);
      mode->dsp_mode = EAMDSP_MODE_INVALID;
      break;
  }

  return 0;
}

static int __v6_enter_decode_mode (int iav_fd, amba_dsp_decode_mode_config_t *mode_config)
{
  struct iav_decode_mode_config decode_mode;
  int i = 0;
  unsigned int mw, mh;

  memset (&decode_mode, 0x0, sizeof (decode_mode) );

  if (mode_config->num_decoder > DAMBADSP_MAX_DECODER_NUMBER) {
    DPRINT_ERROR ("BAD num_decoder %d\n", mode_config->num_decoder);
    return (-100);
  }

  /* Order and fields __v6_enter_decode_mode */
  decode_mode.num_decoder = mode_config->num_decoder;
  decode_mode.num_vout = 0;
  decode_mode.support_ff_fb_bw = mode_config->b_support_ff_fb_bw;

  for (i = 0; i < mode_config->num_decoder; i ++) {
    if (EAMDSP_VIDEO_CODEC_TYPE_H264 == mode_config->multi_chan_configs[i].decoder_type) {
      decode_mode.decoder_type[i] = IAV_DEC_TYPE_H264;

    } else if (EAMDSP_VIDEO_CODEC_TYPE_H265 == mode_config->multi_chan_configs[i].decoder_type) {
      decode_mode.decoder_type[i] = IAV_DEC_TYPE_H265;

    } else {
      DPRINT_ERROR ("decoder[%d] bad video decoder type %d\n", i,
          mode_config->multi_chan_configs[i].decoder_type);
      return (-101);
    }

    decode_mode.enable_vout[i] = mode_config->multi_chan_configs[i].enable_vout;

    if (decode_mode.enable_vout[i]) {
      decode_mode.num_vout ++;
    }
  }

  /* decoder 0 off vout path */
  decode_mode.enable_vout[0] = 0;

  /* ioctl buffer sizing: prefer max_vout0, else first channel max_frm, else defaults */
  mw = mode_config->max_vout0_width;
  mh = mode_config->max_vout0_height;
  if (mw < 16 || mh < 16) {
    mw = mode_config->multi_chan_configs[0].max_frm_width;
    mh = mode_config->multi_chan_configs[0].max_frm_height;
  }
  if (mw < 16)
    mw = 1920;
  if (mh < 16)
    mh = 1088;
  decode_mode.max_width = mw;
  decode_mode.max_height = mh;
  decode_mode.max_vout0_width = mode_config->max_vout0_width;
  decode_mode.max_vout0_height = mode_config->max_vout0_height;
  decode_mode.max_vout1_width = mode_config->max_vout1_width;
  decode_mode.max_vout1_height = mode_config->max_vout1_height;

  AM_IOCTL(iav_fd, IAV_IOC_ENTER_DECODE_MODE, &decode_mode);
  /* update num_decoder to mode_config */
  mode_config->num_decoder = decode_mode.num_decoder;

  return 0;
}

static int __v6_leave_decode_mode (int iav_fd)
{
  int ret = ioctl (iav_fd, IAV_IOC_LEAVE_DECODE_MODE);

  if (0 > ret) {
    perror ("IAV_IOC_LEAVE_DECODE_MODE");
    DPRINT_ERROR ("leave decode mode fail, errno %d\n", errno);
  }

  return ret;
}

static int __v6_create_decoder (int iav_fd, amba_dsp_decoder_info_t *p_decoder_info)
{
  struct iav_decoder_info decoder_info;

  memset (&decoder_info, 0x0, sizeof (decoder_info) );

  decoder_info.decoder_id = p_decoder_info->decoder_id;

  if (EAMDSP_VIDEO_CODEC_TYPE_H264 == p_decoder_info->decoder_type) {
    decoder_info.decoder_type = IAV_DEC_TYPE_H264;

  } else if (EAMDSP_VIDEO_CODEC_TYPE_H265 == p_decoder_info->decoder_type) {
    decoder_info.decoder_type = IAV_DEC_TYPE_H265;

  } else {
    DPRINT_ERROR ("bad video codec type %d\n", p_decoder_info->decoder_type);
    return (-101);
  }

  decoder_info.num_vout = p_decoder_info->num_vout;
  decoder_info.video_width = p_decoder_info->width;
  decoder_info.video_height = p_decoder_info->height;

  if (decoder_info.num_vout > DAMBADSP_MAX_VOUT_NUMBER) {
    DPRINT_ERROR ("BAD num_vout %d\n", p_decoder_info->num_vout);
    return (-100);
  }

  decoder_info.bsb_start_offset = p_decoder_info->bsb_start_offset;
  decoder_info.bsb_size = p_decoder_info->bsb_size;

  AM_IOCTL(iav_fd, IAV_IOC_CREATE_DECODER, &decoder_info);

  p_decoder_info->bsb_start_offset = decoder_info.bsb_start_offset;
  p_decoder_info->bsb_size = decoder_info.bsb_size;

  return 0;
}


static int __v6_destroy_decoder (int iav_fd, unsigned char decoder_id)
{
  AM_IOCTL(iav_fd, IAV_IOC_DESTROY_DECODER, decoder_id);

  return 0;
}


static int __v6_query_decode_config (int iav_fd, amba_dsp_query_decode_config_t *config)
{
  DUNUSED(iav_fd);
  if (config) {
    config->auto_map_bsb = 0;
    config->rendering_monitor_mode = 0;

  } else {
    DPRINT_ERROR ("NULL\n");
    return (-1);
  }

  return 0;
}


static int __v6_decode_trick_play (int iav_fd, unsigned char decoder_id, unsigned char trick_play)
{
  struct iav_decode_trick_play trickplay;
  memset (&trickplay, 0x0, sizeof (trickplay));

  trickplay.decoder_id = decoder_id;
  trickplay.trick_play = trick_play;

  AM_IOCTL(iav_fd, IAV_IOC_DECODE_TRICK_PLAY, &trickplay);

  return 0;
}

static int __v6_decode_start (int iav_fd, unsigned char decoder_id)
{
  AM_IOCTL(iav_fd, IAV_IOC_DECODE_START, decoder_id);

  return 0;
}

static int __v6_decode_stop (int iav_fd, unsigned char decoder_id, unsigned char stop_flag)
{
  struct iav_decode_stop stop;
  memset(&stop, 0x0, sizeof(stop));

  stop.decoder_id = decoder_id;
  stop.stop_flag = stop_flag;

  AM_IOCTL(iav_fd, IAV_IOC_DECODE_STOP, &stop);

  return 0;
}


static int __v6_decode_speed (int iav_fd, unsigned char decoder_id, unsigned short speed, unsigned char scan_mode, unsigned char direction)
{
  struct iav_decode_speed spd;
  memset(&spd, 0x0, sizeof(spd));

  spd.decoder_id = decoder_id;
  spd.direction = direction;
  spd.speed = speed;
  spd.scan_mode = scan_mode;

  AM_IOCTL(iav_fd, IAV_IOC_DECODE_SPEED, &spd);

  return 0;
}


static int __v6_decode_request_bits_fifo (int iav_fd, int decoder_id, unsigned int size, void *cur_pos_offset)
{
  struct iav_decode_bsb wait;
  memset(&wait, 0x0, sizeof(wait));

  wait.decoder_id = decoder_id;
  wait.room = size;
  wait.start_offset = (unsigned int) (unsigned long) cur_pos_offset;

  AM_IOCTL(iav_fd, IAV_IOC_WAIT_DECODE_BSB, &wait);

  return 0;
}


static int __v6_decode (int iav_fd, amba_dsp_decode_t *dec)
{
  struct iav_decode_video decode_video;

  memset (&decode_video, 0, sizeof (decode_video) );
  decode_video.decoder_id = dec->decoder_id;
  decode_video.num_frames = dec->num_frames;

  decode_video.start_ptr_offset = dec->start_ptr_offset;
  decode_video.end_ptr_offset = dec->end_ptr_offset;
  decode_video.first_frame_display = dec->first_frame_display;

  AM_IOCTL (iav_fd, IAV_IOC_DECODE_VIDEO, &decode_video);

  return 0;
}


static int __v6_decode_query_bsb_status_and_print (int iav_fd, unsigned char decoder_id)
{
  struct iav_decode_bsb bsb;

  memset(&bsb, 0x0, sizeof(bsb));
  bsb.decoder_id = decoder_id;

  AM_IOCTL(iav_fd, IAV_IOC_QUERY_DECODE_BSB, &bsb);

  DPRINT_NOTICE ("[BSB]: current arm write offset 0x%08lx, current dsp read offset 0x%08lx, safe room "
    "(minus 256 bytes) %d, free room %d\n", bsb.start_offset, bsb.dsp_read_offset, bsb.room,
    bsb.free_room);

  return 0;
}


static int __v6_decode_wait_eos (int iav_fd, amba_dsp_decode_eos_timestamp_t *eos_timestamp)
{
  struct iav_decode_wait_eos wait_eos;

  memset (&wait_eos, 0, sizeof (wait_eos) );
  wait_eos.decoder_id = eos_timestamp->decoder_id;

  AM_IOCTL (iav_fd, IAV_IOC_WAIT_PLAYBACK_EOS, &wait_eos);

  return 0;
}

static int __v6_decode_query_status_and_print (int iav_fd, unsigned char decoder_id)
{
  struct iav_decode_status status;

  memset(&status, 0x0, sizeof(status));
  status.decoder_id = decoder_id;

  AM_IOCTL(iav_fd, IAV_IOC_QUERY_DECODE_STATUS, &status);

  DPRINT_NOTICE ("[decode status]: decode_state %d, decoded_pic_number %d, error_status %d, "
    "total_error_count %d, irq_count %d\n", status.decode_state, status.decoded_pic_number,
    status.error_status, status.total_error_count, status.irq_count);
  DPRINT_NOTICE ("[decode status, bsb]: current write offset (arm) 0x%08lx, current read offset (dsp) "
    "0x%08lx, safe room (minus 256 bytes) %d, free room %d\n", status.write_offset,
    status.dsp_read_offset, status.room, status.free_room);
  DPRINT_NOTICE ("[decode status, last pts]: %d, is_started %d, is_send_stop_cmd %d\n",
    status.last_pts,status.is_started, status.is_send_stop_cmd);
  DPRINT_NOTICE ("[decode status, dram addr]: luma_addr 0x%08lx, chroma_addr 0x%08lx\n",
    status.yuv422_y_addr, status.yuv422_uv_addr);

  return 0;
}


static int __v6_decode_query_bsb_status (int iav_fd, amba_dsp_bsb_status_t *status)
{
  struct iav_decode_bsb bsb;

  memset(&bsb, 0x0, sizeof(bsb));
  bsb.decoder_id = status->decoder_id;

  AM_IOCTL(iav_fd, IAV_IOC_QUERY_DECODE_BSB, &bsb);

  status->start_offset = bsb.start_offset;
  status->room = bsb.room;
  status->dsp_read_offset = bsb.dsp_read_offset;
  status->free_room = bsb.free_room;

  return 0;
}

static int __v6_decode_query_status (int iav_fd, amba_dsp_decode_status_t *status)
{
  struct iav_decode_status dec_status;

  memset(&dec_status, 0x0, sizeof(dec_status));
  dec_status.decoder_id = status->decoder_id;

  AM_IOCTL(iav_fd, IAV_IOC_QUERY_DECODE_STATUS, &status);

  status->is_started = dec_status.is_started;
  status->is_send_stop_cmd = dec_status.is_send_stop_cmd;
  status->last_pts = dec_status.last_pts;
  status->decode_state = dec_status.decode_state;
  status->error_status = dec_status.error_status;
  status->total_error_count = dec_status.total_error_count;
  status->decoded_pic_number = dec_status.decoded_pic_number;

  status->write_offset = dec_status.write_offset;
  status->room = dec_status.room;
  status->dsp_read_offset = dec_status.dsp_read_offset;
  status->free_room = dec_status.free_room;
  status->irq_count = dec_status.irq_count;
  status->yuv422_y_addr = dec_status.yuv422_y_addr;
  status->yuv422_uv_addr = dec_status.yuv422_uv_addr;
  return 0;
}

static int __v6_get_vin_info (int iav_fd, amba_dsp_vin_info_t *vininfo)
{
  struct vindev_video_info vin_info;
  struct vindev_fps active_fps;
  struct vindev_devinfo vindev_info;
  unsigned int fps_q9 = 1;
  int ret = 0;

  memset (&vin_info, 0x0, sizeof (vin_info) );
  vin_info.vsrc_id = vininfo->vsrc_id;
  vin_info.info.mode = AMBA_VIDEO_MODE_CURRENT;
  ret = ioctl (iav_fd, IAV_IOC_VIN_GET_VIDEOINFO, &vin_info);

  if (0 > ret) {
    perror ("IAV_IOC_VIN_GET_VIDEOINFO");
    DPRINT_ERROR ("IAV_IOC_VIN_GET_VIDEOINFO fail, errno %d\n", errno);
    return ret;
  }

  memset (&active_fps, 0, sizeof (active_fps) );
  active_fps.vsrc_id = vininfo->vsrc_id;
  ret = ioctl (iav_fd, IAV_IOC_VIN_GET_FPS, &active_fps);

  if (0 > ret) {
    perror ("IAV_IOC_VIN_GET_FPS");
    DPRINT_ERROR ("IAV_IOC_VIN_GET_FPS fail, errno %d\n", errno);
    return ret;
  }

  memset (&vindev_info, 0x0, sizeof (vindev_info));
  ioctl (iav_fd, IAV_IOC_VIN_GET_DEVINFO, &vindev_info);
  if (0 > ret) {
    perror ("IAV_IOC_VIN_GET_DEVINFO");
    DPRINT_ERROR ("IAV_IOC_VIN_GET_DEVINFO fail, errno %d\n", errno);
    return ret;
  }

  fps_q9 = active_fps.fps;

  __parse_fps (fps_q9, vininfo);

  vininfo->width = vin_info.info.width;
  vininfo->height = vin_info.info.height;

  vininfo->format = vin_info.info.format;
  vininfo->type = vin_info.info.type;
  vininfo->bits = vin_info.info.bits;
  vininfo->ratio = vin_info.info.ratio;
  vininfo->system = vin_info.info.system;
  vininfo->flip = vin_info.info.flip;
  vininfo->rotate = vin_info.info.rotate;
  vininfo->vinc_id = vindev_info.vinc_id;

  return 0;
}

static int __v6_get_stream_framefactor (int iav_fd, int index, amba_dsp_stream_framefactor_t *framefactor)
{
  struct iav_stream_cfg streamcfg;
  int ret = 0;

  if (IAV_STREAM_MAX_NUM_ALL <= index) {
    DPRINT_ERROR ("index(%d) not as expected\n", index);
    return (-10);
  }

  memset (&streamcfg, 0, sizeof (streamcfg) );
  streamcfg.id = index;
  streamcfg.cid = IAV_STMCFG_FPS;
  ret = ioctl (iav_fd, IAV_IOC_GET_STREAM_CONFIG, &streamcfg);

  if (0 > ret) {
    perror ("IAV_IOC_GET_STREAM_CONFIG");
    DPRINT_ERROR ("IAV_IOC_GET_STREAM_CONFIG fail, errno %d\n", errno);
    return ret;
  }

  framefactor->framefactor_num = streamcfg.arg.fps.fps_multi;
  framefactor->framefactor_den = streamcfg.arg.fps.fps_div;

  return 0;
}

static int __v6_map_bsb (int iav_fd, iav_map_bsb_t *map_bsb)
{
  int ret = 0;
  unsigned int map_size = 0;
  struct iav_querymem query_mem;
  struct iav_mem_part_info *mem_part;

  memset (&query_mem, 0, sizeof (query_mem) );
  query_mem.mid = IAV_MEM_PARTITION;
  mem_part = &query_mem.arg.partition;
  mem_part->pid = IAV_PART_BSB;
  ret = ioctl (iav_fd, IAV_IOC_QUERY_MEMBLOCK, &query_mem);

  if (0 > ret) {
    perror ("IAV_IOC_QUERY_MEMBLOCK");
    DPRINT_ERROR ("IAV_IOC_QUERY_MEMBLOCK fail, errno %d\n", errno);
    return ret;
  }

  map_bsb->size = mem_part->mem.length;

  if (map_bsb->b_two_times) {
    map_size = mem_part->mem.length * 2;

  } else {
    map_size = mem_part->mem.length;
  }

  if (map_bsb->b_enable_read && map_bsb->b_enable_write) {
    map_bsb->base = mmap (NULL, map_size, PROT_WRITE | PROT_READ, MAP_SHARED, iav_fd, mem_part->mem.addr);

  } else if (map_bsb->b_enable_read && !map_bsb->b_enable_write) {
    map_bsb->base = mmap (NULL, map_size, PROT_READ, MAP_SHARED, iav_fd, mem_part->mem.addr);

  } else if (!map_bsb->b_enable_read && map_bsb->b_enable_write) {
    map_bsb->base = mmap (NULL, map_size, PROT_WRITE, MAP_SHARED, iav_fd, mem_part->mem.addr);

  } else {
    DPRINT_ERROR ("not read or write\n");
    return (-1);
  }

  if (map_bsb->base == MAP_FAILED) {
    perror ("mmap");
    DPRINT_ERROR ("mmap fail\n");
    return -1;
  }

  DPRINT_NOTICE ("[mmap]: bsb_mem = %p, size = 0x%x\n", map_bsb->base, map_bsb->size);
  return 0;
}

static int __v6_map_dec_bsb (int iav_fd, iav_map_bsb_t *map_bsb)
{
  int ret = 0;
  unsigned int map_size = 0;
  struct iav_querymem query_mem;
  struct iav_mem_part_info *mem_part;

  memset (&query_mem, 0, sizeof (query_mem) );
  query_mem.mid = IAV_MEM_PARTITION;
  mem_part = &query_mem.arg.partition;
  mem_part->pid = IAV_PART_DEC_BSB;
  ret = ioctl (iav_fd, IAV_IOC_QUERY_MEMBLOCK, &query_mem);

  if (0 > ret) {
    perror ("IAV_IOC_QUERY_MEMBLOCK");
    DPRINT_ERROR ("IAV_IOC_QUERY_MEMBLOCK fail, errno %d\n", errno);
    return ret;
  }

  if (mem_part->mem.length == 0) {
    DPRINT_NOTICE ("no decoding.\n");
    return 0;
  }

  map_bsb->size = mem_part->mem.length;

  if (map_bsb->b_two_times) {
    map_size = mem_part->mem.length * 2;

  } else {
    map_size = mem_part->mem.length;
  }

  if (map_bsb->b_enable_read && map_bsb->b_enable_write) {
    map_bsb->base = mmap (NULL, map_size, PROT_WRITE | PROT_READ, MAP_SHARED, iav_fd, mem_part->mem.addr);

  } else if (map_bsb->b_enable_read && !map_bsb->b_enable_write) {
    map_bsb->base = mmap (NULL, map_size, PROT_READ, MAP_SHARED, iav_fd, mem_part->mem.addr);

  } else if (!map_bsb->b_enable_read && map_bsb->b_enable_write) {
    map_bsb->base = mmap (NULL, map_size, PROT_WRITE, MAP_SHARED, iav_fd, mem_part->mem.addr);

  } else {
    DPRINT_ERROR ("not read or write\n");
    return (-1);
  }

  if (map_bsb->base == MAP_FAILED) {
    perror ("mmap");
    DPRINT_ERROR ("mmap fail\n");
    return -1;
  }

  DPRINT_NOTICE ("[mmap]: dec_bsb_mem = %p, size = 0x%x\n", map_bsb->base, map_bsb->size);
  return 0;
}


static int __v6_map_dsp (int iav_fd, iav_map_dsp_t *map_dsp)
{
  int ret = 0;
  struct iav_querymem query_mem;
  struct iav_mem_part_info *mem_part;

  memset (&query_mem, 0, sizeof (query_mem) );
  query_mem.mid = IAV_MEM_PARTITION;
  mem_part = &query_mem.arg.partition;
  mem_part->pid = IAV_PART_DSP;
  ret = ioctl (iav_fd, IAV_IOC_QUERY_MEMBLOCK, &query_mem);

  if (0 > ret) {
    perror ("IAV_IOC_QUERY_MEMBLOCK");
    DPRINT_ERROR ("IAV_IOC_QUERY_MEMBLOCK fail, errno %d\n", errno);
    return ret;
  }

  map_dsp->size = mem_part->mem.length;
  map_dsp->base = mmap (NULL, map_dsp->size, PROT_READ | PROT_WRITE, MAP_SHARED, iav_fd, mem_part->mem.addr);

  if (map_dsp->base == MAP_FAILED) {
    perror ("mmap");
    DPRINT_ERROR ("mmap fail, errno %d\n", errno);
    return -1;
  }

  DPRINT_NOTICE ("[mmap]: dsp_mem = %p, size = 0x%x\n", map_dsp->base, map_dsp->size);
  return 0;
}

static int __v6_map_overlay(int iav_fd, iav_map_overlay_t *map_overlay)
{
  struct iav_querymem query_mem;
  struct iav_mem_part_info *part_info = NULL;

  memset (&query_mem, 0, sizeof (query_mem) );
  query_mem.mid = IAV_MEM_PARTITION;
  part_info = &query_mem.arg.partition;
  part_info->pid = IAV_PART_OVERLAY;

  AM_IOCTL(iav_fd, IAV_IOC_QUERY_MEMBLOCK, &query_mem);
  if (part_info->mem.length == 0) {
    printf("IAV_PART_OVERLAY is not allocated.\n");
    return -1;
  }

  map_overlay->size = part_info->mem.length;
  map_overlay->base = mmap(NULL, map_overlay->size, PROT_WRITE, MAP_SHARED, iav_fd,
    part_info->mem.addr);
  if (map_overlay->base == MAP_FAILED) {
    perror("mmap (%d) failed: %s\n");
    return COM_ECODE_MEM_MAP_FAILED;
  }

  DPRINT_NOTICE ("[mmap]: overlay_mem = %p, size = 0x%lx\n", map_overlay->base, map_overlay->size);

  return 0;
}

static int __v6_unmap_bsb (int iav_fd, iav_map_bsb_t *map_bsb)
{
  DUNUSED(iav_fd);
  if (map_bsb->base && map_bsb->size) {
    int ret = 0;
    unsigned int map_size = 0;

    if (map_bsb->b_two_times) {
      map_size = map_bsb->size * 2;

    } else {
      map_size = map_bsb->size;
    }

    ret = munmap (map_bsb->base, map_size);
    DPRINT_NOTICE ("[munmap]: bsb_mem = %p, size = 0x%x\n", map_bsb->base, map_bsb->size);
    map_bsb->base = NULL;
    map_bsb->size = 0;

    if (0 > ret) {
      perror ("munmap");
      DPRINT_ERROR ("munmap fail, errno %d\n", errno);
      return ret;
    }

  } else {
    DPRINT_ERROR ("bad params, %p, %d\n", map_bsb->base, map_bsb->size);
    return (-1);
  }

  return 0;
}

static int __v6_unmap_dec_bsb (int iav_fd, iav_map_bsb_t *map_bsb)
{
  return __v6_unmap_bsb (iav_fd, map_bsb);
}

static int __v6_unmap_dsp (int iav_fd, iav_map_dsp_t *map_dsp)
{
  DUNUSED(iav_fd);
  if (map_dsp->base && map_dsp->size) {
    int ret = 0;
    ret = munmap (map_dsp->base, map_dsp->size);
    DPRINT_NOTICE ("[munmap]: dsp_mem = %p, size = 0x%x\n", map_dsp->base, map_dsp->size);
    map_dsp->base = NULL;
    map_dsp->size = 0;

    if (0 > ret) {
      perror ("munmap");
      DPRINT_ERROR ("munmap fail, errno %d\n", errno);
      return ret;
    }

  } else {
    DPRINT_ERROR ("bad params, %p, %d\n", map_dsp->base, map_dsp->size);
    return (-1);
  }

  return 0;
}

static int __v6_unmap_overlay(int iav_fd, iav_map_overlay_t *map_overlay)
{
  DUNUSED(iav_fd);
  if (map_overlay->base && map_overlay->size) {
    int ret = 0;
    ret = munmap(map_overlay->base, map_overlay->size);
    DPRINT_NOTICE("[munmap]: dsp_mem = %p, size = 0x%lx\n", map_overlay->base, map_overlay->size);
    map_overlay->base = NULL;
    map_overlay->size = 0;
    if (0 > ret) {
      perror("munmap");
      DPRINT_ERROR("munmap failed, errno %d\n", errno);
      return COM_ECODE_MEM_UNMAP_FAILED;
    }
  } else {
    DPRINT_ERROR("bad params, %p, %ld\n", map_overlay->base, map_overlay->size);
    return COM_ECODE_BAD_PARAMS;
  }

  return COM_ECODE_OK;
}

static int __v6_flush_frame_desc(int fd_iav, unsigned int streamid,
    unsigned int force_idr_type, u64 mono_pts)
{
  int rval = 0;
  struct iav_flush_framedesc flush_framedesc = {0};
  struct iav_stream_cfg stream_cfg;
  struct iav_querydesc query_desc;

  memset(&flush_framedesc, 0, sizeof(flush_framedesc));
  flush_framedesc.stream_id = streamid;
  flush_framedesc.enable_force_idr = (IAV_FLUSH_FORCE_IDR_DISABLE != force_idr_type);
  if (IAV_FLUSH_FORCE_IDR_WITH_PTS == force_idr_type) {
    memset(&stream_cfg, 0, sizeof(stream_cfg));
    stream_cfg.id = streamid;
    stream_cfg.cid = IAV_STMCFG_FORMAT;
    rval = ioctl (fd_iav, IAV_IOC_GET_STREAM_CONFIG, &stream_cfg);
    if (rval < 0) {
      perror("IAV_IOC_GET_STREAM_CONFIG");
      return rval;
    }
    do {
      memset(&query_desc, 0, sizeof(query_desc));
      query_desc.arg.canvas.canvas_id = stream_cfg.arg.format.enc_src_id;
      query_desc.arg.canvas.is_dsp_hw_pts = 1;
      query_desc.qid = IAV_DESC_CANVAS;
      rval = ioctl (fd_iav, IAV_IOC_QUERY_DESC, &query_desc);
      if (rval < 0) {
        perror("IAV_IOC_QUERY_DESC");
        return rval;
      }
      if (query_desc.arg.canvas.yuv.mono_pts >= mono_pts) {
        break;
      }
    } while (1);
    flush_framedesc.force_idr_dsp_pts = query_desc.arg.canvas.yuv.dsp_pts;
    flush_framedesc.force_idr_mono_pts = query_desc.arg.canvas.yuv.mono_pts;
    DPRINT_NOTICE("flush frame in stream[%d]: force idr with pts %lld\n", streamid, flush_framedesc.force_idr_mono_pts);
  }

  rval = ioctl (fd_iav, IAV_IOC_FLUSH_FRAMEDESC, &flush_framedesc);
  if (rval < 0) {
    perror("IAV_IOC_FLUSH_FRAMEDESC");
  }

  return rval;
}


static int __v6_read_bitstream (int iav_fd, amba_dsp_read_bitstream_t *bitstream)
{
  struct iav_querydesc query_desc;
  struct iav_framedesc *frame_desc = NULL;
  int ret = 0;

  memset (&query_desc, 0, sizeof (query_desc) );
  frame_desc = &query_desc.arg.frame;
  query_desc.qid = IAV_DESC_FRAME;
  frame_desc->id = bitstream->stream_idx;
  frame_desc->time_ms = bitstream->timeout_ms;
  ret = ioctl (iav_fd, IAV_IOC_QUERY_DESC, &query_desc);
  if (ret) {
    if (EAGAIN == errno) {
      return COM_ECODE_TRY_AGAIN;
    }

    perror ("IAV_IOC_QUERY_DESC");
    DPRINT_ERROR ("IAV_IOC_QUERY_DESC fail, errno %d\n", errno);
    return COM_ECODE_BAD_STATE;
  }

  bitstream->framedesc = frame_desc;
  bitstream->stream_idx = frame_desc->id;
  bitstream->offset = frame_desc->data_addr_offset;
  bitstream->size = frame_desc->size;
  bitstream->pts = frame_desc->arm_pts;
  bitstream->video_width = frame_desc->reso.width;
  bitstream->video_height = frame_desc->reso.height;
  bitstream->slice_id = frame_desc->slice_id;
  bitstream->slice_num = frame_desc->slice_num;
  bitstream->tile_id = frame_desc->tile_id;
  bitstream->tile_num = frame_desc->tile_num;
  bitstream->encoded_frame_num = frame_desc->encoded_frame_num;

  if (frame_desc->stream_end) {
    bitstream->size = 0;
    bitstream->offset = 0;
    DPRINT_NOTICE("stream end\n");
    return COM_ECODE_COMPLETE;
  }

  if (IAV_PIC_TYPE_B_FRAME == frame_desc->pic_type) {
    bitstream->hint_frame_type = EPredefinedPictureType_B;
    bitstream->hint_is_keyframe = 0;

  } else if ( (IAV_PIC_TYPE_P_FRAME == frame_desc->pic_type)
              || (IAV_PIC_TYPE_P_FAST_SEEK_FRAME == frame_desc->pic_type) ) {
    bitstream->hint_frame_type = EPredefinedPictureType_P;
    bitstream->hint_is_keyframe = 0;

  } else if (IAV_PIC_TYPE_I_FRAME == frame_desc->pic_type) {
    bitstream->hint_frame_type = EPredefinedPictureType_I;
    bitstream->hint_is_keyframe = 0;

  } else if (IAV_PIC_TYPE_IDR_FRAME == frame_desc->pic_type) {
    bitstream->hint_frame_type = EPredefinedPictureType_IDR;
    bitstream->hint_is_keyframe = 1;

  } else if (IAV_PIC_TYPE_MJPEG_FRAME == frame_desc->pic_type) {
    bitstream->hint_frame_type = EPredefinedPictureType_IDR;
    bitstream->hint_is_keyframe = 1;

  } else if (0x07 == frame_desc->pic_type) {
    bitstream->size = 0;
    bitstream->offset = 0;
    DPRINT_NOTICE("frame end\n");
    return COM_ECODE_COMPLETE;
  } else {
    bitstream->hint_frame_type = 0;
    bitstream->hint_is_keyframe = 0;
    DPRINT_ERROR ("error frame type %d\n", frame_desc->pic_type);
    return COM_ECODE_BAD_STATE;
  }

  if (IAV_STREAM_TYPE_H264 == frame_desc->stream_type) {
    bitstream->stream_format = StreamFormat_H264;

  } else if (IAV_STREAM_TYPE_H265 == frame_desc->stream_type) {
    bitstream->stream_format = StreamFormat_H265;

  } else if (IAV_STREAM_TYPE_MJPEG == frame_desc->stream_type) {
    bitstream->stream_format = StreamFormat_JPEG;

  } else {
    DPRINT_ERROR ("bad stream type %d\n", frame_desc->stream_type);
    bitstream->stream_format = StreamFormat_Invalid;
    return COM_ECODE_BAD_STATE;
  }

  if ((frame_desc->arm_pts & 0x8000000000000000ULL) != 0) {
    DPRINT_ERROR ("arm_pts is too large %llu(0x%llX), may lead to overflow\n", frame_desc->arm_pts, frame_desc->arm_pts);
    return COM_ECODE_BAD_STATE;
  }

  return COM_ECODE_OK;
}

static void __v6_release_bitstream (int iav_fd, amba_dsp_release_bitstream_t *release_bitstream)
{
  DUNUSED(iav_fd);
  DUNUSED(release_bitstream);
  /*struct iav_framedesc *frame_desc = (struct iav_framedesc *)release_bitstream->framedesc;
  if (ioctl(iav_fd, IAV_IOC_RELEASE_FRAMEDESC, frame_desc) < 0) {
    perror("IAV_IOC_RELEASE_FRAMEDESC\n");
  }*/
  return ;
}

static int __v6_is_ready_for_read_bitstream (int iav_fd)
{
  DUNUSED(iav_fd);
  return 1;
}

static int __v6_encode_start (int iav_fd, unsigned int mask)
{
  int ret = ioctl (iav_fd, IAV_IOC_START_ENCODE, mask);

  if (ret) {
    perror ("IAV_IOC_START_ENCODE");
    DPRINT_ERROR ("IAV_IOC_START_ENCODE fail, errno %d, mask 0x%08x\n", errno, mask);
    return ret;
  }

  return 0;
}

static int __v6_encode_stop (int iav_fd, unsigned int mask)
{
  int ret = ioctl (iav_fd, IAV_IOC_STOP_ENCODE, mask);

  if (ret) {
    perror ("IAV_IOC_STOP_ENCODE");
    DPRINT_ERROR ("IAV_IOC_STOP_ENCODE fail, errno %d, mask 0x%08x\n", errno, mask);
    return ret;
  }

  return 0;
}

static int alloc_gdma_dst_buf_for_gdma(int iav_fd, unsigned int size,
    amba_gdma_buf_t *gdma_ctx)
{
  struct iav_alloc_mem_part alloc_mem_part;
  memset(&alloc_mem_part, 0x0, sizeof(alloc_mem_part));
  alloc_mem_part.length = size;
  alloc_mem_part.enable_cache = 1;
  alloc_mem_part.from_private = 1;

  if (ioctl(iav_fd, IAV_IOC_ALLOC_ANON_MEM_PART, &alloc_mem_part) < 0) {
    perror("IAV_IOC_ALLOC_ANON_MEM_PART");
    return -1;
  }
  gdma_ctx->gdma_part_id = alloc_mem_part.pid;
  gdma_ctx->gdma_buf_size = alloc_mem_part.length;
  if (gdma_ctx->gdma_buf_size) {
    gdma_ctx->gdma_buf = mmap(NULL, alloc_mem_part.length, PROT_READ | PROT_WRITE, MAP_SHARED,
      iav_fd, alloc_mem_part.offset);
    if (gdma_ctx->gdma_buf == MAP_FAILED) {
      perror("mmap gdma dst buffer failed\n");
      return -1;
    }
  }

  return 0;
}

static int alloc_gdma_dst_buf_for_dma_buf(int iav_fd, unsigned int size,
    amba_gdma_buf_t *gdma_ctx)
{
  struct iav_alloc_mem_part_fd alloc_mem_part;
  memset(&alloc_mem_part, 0x0, sizeof(alloc_mem_part));
  alloc_mem_part.length = size;
  alloc_mem_part.enable_cache = 1;
  alloc_mem_part.from_private = 0;

  if (ioctl(iav_fd, IAV_IOC_ALLOC_ANON_MEM_PART_FD, &alloc_mem_part) < 0) {
    perror("IAV_IOC_ALLOC_ANON_MEM_PART_FD");
    return -1;
  }
  gdma_ctx->dma_buf_fd = alloc_mem_part.dma_buf_fd;
  gdma_ctx->gdma_buf_size = lseek(gdma_ctx->dma_buf_fd, 0, SEEK_END);
  if (gdma_ctx->gdma_buf_size) {
    gdma_ctx->gdma_buf = mmap(NULL, gdma_ctx->gdma_buf_size, PROT_READ | PROT_WRITE, MAP_SHARED,
      gdma_ctx->dma_buf_fd, 0);
    if (gdma_ctx->gdma_buf == MAP_FAILED) {
      perror("mmap dma-buf:fd dst buffer failed\n");
      return -1;
    }
  }

  return 0;
}

static int __v6_gdma_alloc_buf(int iav_fd, unsigned int size,
    amba_gdma_buf_t *gdma_ctx, unsigned char is_dma_buf)
{
  int rval = 0;

  if (!is_dma_buf) {
    rval = alloc_gdma_dst_buf_for_gdma(iav_fd, size, gdma_ctx);
  } else {
    rval = alloc_gdma_dst_buf_for_dma_buf(iav_fd, size, gdma_ctx);
  }

  return rval;
}

static int __v6_gdma_free_buf(int iav_fd, amba_gdma_buf_t *gdma_ctx)
{
  struct iav_alloc_mem_part alloc_mem_part = {0};

  if (gdma_ctx->gdma_buf && gdma_ctx->gdma_buf_size) {
    munmap(gdma_ctx->gdma_buf, gdma_ctx->gdma_buf_size);
    gdma_ctx->gdma_buf = NULL;
    gdma_ctx->gdma_buf_size = 0;
  }

  if (gdma_ctx->gdma_part_id >= 0) {
    alloc_mem_part.pid = gdma_ctx->gdma_part_id;
    if (ioctl(iav_fd, IAV_IOC_FREE_MEM_PART, &alloc_mem_part) < 0) {
      perror("IAV_IOC_FREE_MEM_PART");
      return -1;
    }
    gdma_ctx->gdma_part_id = -1;
  }

  if (gdma_ctx->dma_buf_fd >= 0) {
    close(gdma_ctx->dma_buf_fd);
    gdma_ctx->dma_buf_fd = -1;
  }

  return 0;
}

static int __v6_query_encode_stream_info (int iav_fd, amba_dsp_enc_stream_info_t *info)
{
  struct iav_queryinfo query_info;
  int ret = 0;
  memset(&query_info, 0x0, sizeof(query_info));
  query_info.qid = IAV_INFO_STREAM;
  query_info.arg.stream.id = info->id;
  ret = ioctl (iav_fd, IAV_IOC_QUERY_INFO, &query_info);

  if (ret) {
    perror ("IAV_IOC_QUERY_INFO");
    DPRINT_ERROR ("IAV_IOC_QUERY_INFO fail, errno %d\n", errno);
    return ret;
  }

  switch (query_info.arg.stream.state) {

    case IAV_STREAM_STATE_IDLE:
      info->state = EAMDSP_ENC_STREAM_STATE_IDLE;
      break;

    case IAV_STREAM_STATE_STARTING:
      info->state = EAMDSP_ENC_STREAM_STATE_STARTING;
      break;

    case IAV_STREAM_STATE_ENCODING:
      info->state = EAMDSP_ENC_STREAM_STATE_ENCODING;
      break;

    case IAV_STREAM_STATE_STOPPING:
      info->state = EAMDSP_ENC_STREAM_STATE_STOPPING;
      break;

    case IAV_STREAM_STATE_UNKNOWN:
      DPRINT_ERROR ("unknown state\n");
      info->state = EAMDSP_ENC_STREAM_STATE_UNKNOWN;
      return (-1);
      break;

    default:
      DPRINT_ERROR ("unexpected state %d\n", query_info.arg.stream.state);
      info->state = EAMDSP_ENC_STREAM_STATE_ERROR;
      return (-2);
      break;
  }

  return 0;
}

static int __v6_query_encode_stream_format (int iav_fd, amba_dsp_enc_stream_format_t *fmt)
{
  struct iav_stream_cfg stream_cfg;
  struct iav_stream_format *format;
  int ret = 0;

  memset (&stream_cfg, 0, sizeof (stream_cfg) );
  stream_cfg.cid = IAV_STMCFG_FORMAT;
  stream_cfg.id = fmt->id;
  format = &stream_cfg.arg.format;
  ret = ioctl (iav_fd, IAV_IOC_GET_STREAM_CONFIG, &stream_cfg);

  if (ret) {
    perror ("IAV_IOC_GET_STREAM_CONFIG");
    DPRINT_ERROR ("IAV_IOC_GET_STREAM_CONFIG fail, errno %d\n", errno);
    return ret;
  }

  switch (format->type) {

    case IAV_STREAM_TYPE_H264:
      fmt->codec = EAMDSP_VIDEO_CODEC_TYPE_H264;
      break;

    case IAV_STREAM_TYPE_H265:
      fmt->codec = EAMDSP_VIDEO_CODEC_TYPE_H265;
      break;

    case IAV_STREAM_TYPE_MJPEG:
      fmt->codec = EAMDSP_VIDEO_CODEC_TYPE_MJPEG;
      break;

    case IAV_STREAM_TYPE_NONE:
      DPRINT_ERROR ("codec none?\n");
      fmt->codec = EAMDSP_VIDEO_CODEC_TYPE_INVALID;
      break;

    default:
      DPRINT_ERROR ("unexpected codec %d\n", format->type);
      fmt->codec = EAMDSP_VIDEO_CODEC_TYPE_INVALID;
      return (-1);
      break;
  }

  switch (format->enc_src_id) {

    case IAV_SRCBUF_MN:
      fmt->source_buffer = EAMDSP_ENC_STREAM_SOURCE_BUFFER_MAIN;
      break;

    case IAV_SRCBUF_PC:
      fmt->source_buffer = EAMDSP_ENC_STREAM_SOURCE_BUFFER_SECOND;
      break;

    case IAV_SRCBUF_PB:
      fmt->source_buffer = EAMDSP_ENC_STREAM_SOURCE_BUFFER_THIRD;
      break;

    case IAV_SRCBUF_PA:
      fmt->source_buffer = EAMDSP_ENC_STREAM_SOURCE_BUFFER_FOURTH;
      break;

    case IAV_SRCBUF_PD:
      fmt->source_buffer = EAMDSP_ENC_STREAM_SOURCE_BUFFER_FIFTH;
      break;

    case IAV_SRCBUF_EFM:
      fmt->source_buffer = EAMDSP_ENC_STREAM_SOURCE_BUFFER_EFM;
      break;

    default:
      DPRINT_ERROR ("unexpected source buffer %d\n", format->enc_src_id);
      fmt->source_buffer = EAMDSP_ENC_STREAM_SOURCE_BUFFER_INVALID;
      return (-2);
      break;
  }

  fmt->enc_win_width = format->enc_win.width;
  fmt->enc_win_height = format->enc_win.height;
  fmt->rotate_cw = format->rotate_cw ? 1 : 0;

  return 0;
}

static int __v6_get_stream_overlay_pixel_format(int iav_fd, int stream_id)
{
  struct iav_stream_resource stream_resource;

  memset(&stream_resource, 0, sizeof(stream_resource));
  stream_resource.stream_id = stream_id;
  if (ioctl(iav_fd, IAV_IOC_GET_STREAM_RESOURCE, &stream_resource) < 0) {
    perror("IAV_IOC_GET_STREAM_RESOURCE");
    return -1;
  }
  return stream_resource.overlay_pixel_format;
}

static int __v6_query_source_buffer_info (int iav_fd, amba_dsp_source_buffer_info_t *info)
{
  struct iav_video_proc vproc;
  struct iav_dptz *dptz;

  memset (&vproc, 0, sizeof (vproc) );
  vproc.cid = IAV_VIDEO_PROC_DPTZ;
  /* FIXME: Use channel 0 by default */
  dptz = &vproc.arg.dptz;
  dptz->channel_id = 0;
  dptz->buf_id = info->buf_id;

  if (ioctl (iav_fd, IAV_IOC_GET_VIDEO_PROC, &vproc) < 0) {
    perror ("IAV_IOC_GET_VIDEO_PROC");
    DPRINT_ERROR ("IAV_IOC_GET_VIDEO_PROC fail, errno %d\n", errno);
    return (-1);
  }

  info->size_width = dptz->buf_cfg.output.width;
  info->size_height = dptz->buf_cfg.output.height;

  info->crop_size_x = dptz->buf_cfg.input.width;
  info->crop_size_y = dptz->buf_cfg.input.height;
  info->crop_pos_x = dptz->buf_cfg.input.x;
  info->crop_pos_y = dptz->buf_cfg.input.y;

  return 0;
}

static int __v6_check_iav_state(int iav_fd, unsigned char *decode_mode)
{
  int state;
  if (ioctl(iav_fd, IAV_IOC_GET_IAV_STATE, &state) < 0) {
    perror("IAV_IOC_GET_IAV_STATE");
    exit(2);
  }

  if ((state != IAV_STATE_PREVIEW) && (state != IAV_STATE_ENCODING) &&
      (state != IAV_STATE_DECODING)) {
    DPRINT_ERROR("IAV is not in preview / encoding /decoding state, cannot get yuv buf!\n");
    return -1;
  }

  if (state == IAV_STATE_DECODING) {
    if (decode_mode != NULL) {
      *decode_mode = 1;
    }
  }

  return 0;
}

static int __v6_get_iav_state(int iav_fd)
{
  int state = IAV_STATE_INIT;
  if (ioctl(iav_fd, IAV_IOC_GET_IAV_STATE, &state) < 0) {
    perror("IAV_IOC_GET_IAV_STATE");
    exit(2);
  }

  return state;
}

static int __v6_get_resource_info(int iav_fd, amba_resource_info_t *info)
{
  struct iav_system_resource resource;
  struct iav_canvas_cfg canvas_cfg;
  struct iav_chan_cfg chan_cfg;
  struct iav_pyramid_cfg pyramid_cfg;
  //struct iav_video_proc vproc;
  unsigned char i = 0, j = 0, frame_rate = 0;
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
  info->img_scale_enable = resource.img_scale_cfg.enable;
  info->img_scale_max_input_width = resource.img_scale_cfg.max_input.width;
  info->img_scale_max_input_height = resource.img_scale_cfg.max_input.height;
  info->img_scale_max_output_width = resource.img_scale_cfg.max_output.width;
  info->img_scale_max_output_height = resource.img_scale_cfg.max_output.height;
  info->enc_raw_rgb = resource.enc_raw_rgb;
  info->enc_raw_nv12 = resource.enc_raw_nv12;
  info->enc_raw_yuv = resource.enc_raw_yuv;

  for (i = 0; i < resource.chan_num; ++i) {
    memset(&chan_cfg, 0, sizeof(chan_cfg));
    chan_cfg.chan_id = i;
    if (ioctl(iav_fd, IAV_IOC_GET_CHAN_CONFIG, &chan_cfg) < 0) {
      perror("IAV_IOC_GET_CHAN_CONFIG\n");
      return -1;
    }

    info->vcap_mode_flag[i] = chan_cfg.vcap_mode_flags;
    info->scale_pass_num[i] = chan_cfg.pass_num;
    info->vsrc_id[i] = chan_cfg.vsrc_id;
  }

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
    info->canvas_ext_mem_enable[i] = canvas_cfg.ext_mem;
    info->canvas_yuv_buffer_disable[i] = canvas_cfg.disable_yuv_dram;
    info->canvas_me_buffer_disable[i] = canvas_cfg.disable_me_dram;
    info->canvas_enc_dummy_latency[i] = canvas_cfg.enc_dummy_latency;
    info->canvas_height[i] = canvas_cfg.max.height;
    info->canvas_width[i] = canvas_cfg.max.width;
    info->back_pressure_enable[i] = canvas_cfg.back_pressure_enable;
  }

  for (i = 0; i < info->channel_num; i++) {
    memset(&pyramid_cfg, 0, sizeof(struct iav_pyramid_cfg));
    pyramid_cfg.chan_id = i;
    if (ioctl(iav_fd, IAV_IOC_GET_PYRAMID_CFG, &pyramid_cfg) < 0) {
      perror("IAV_IOC_GET_PYRAMID_CFG\n");
      return -1;
    }

    info->pyramid_fps[i] = resource.enable_hp_fps ? pyramid_cfg.frame_rate_hp : pyramid_cfg.frame_rate;

    info->pyramid_manual_feed[i] = pyramid_cfg.manual_feed;
    info->pyramid_ext_mem[i] = pyramid_cfg.ext_mem;

    for (j = 0; j < IAV_MAX_PYRAMID_LAYERS; j++) {
      info->pyramid_height[i][j] = pyramid_cfg.crop_win[j].height;
      info->pyramid_width[i][j] = pyramid_cfg.crop_win[j].width;
    }
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

static unsigned int __get_yuv_buffer_size(struct iav_yuv_cap *yuv_cap, int format)
{
  /* layout: luma + chroma + convert_chroma(if yuv type needs to convert) */
  unsigned int luma_size;
  unsigned int total_size;

  luma_size = yuv_cap->pitch * ROUND_UP(yuv_cap->height, 16);
  if (yuv_cap->format == IAV_YUV_FORMAT_YUV420) {
    switch (format) {
    case YUV420_IYUV:
    case YUV420_YV12:
    case YUV420_NV12:
      total_size = (luma_size * 3) >> 1;
      break;
    case YUV444:
      total_size = (luma_size * 7) >> 1;
      break;
    default:
      total_size = 0;
      break;
    }
  } else if (yuv_cap->format == IAV_YUV_FORMAT_YUV422) {
    switch (format) {
    case YUV422_NV16:
      total_size = luma_size * 2;
      break;
    case YUV422_YU16:
    case YUV422_YV16:
      total_size = luma_size * 3;
      break;
    case YUV444:
      total_size = luma_size * 4;
      break;
    default:
      total_size = 0;
      break;
    }
  } else {
    total_size = 0;
  }

  total_size = ROUND_UP(total_size, 16);
  //DPRINT_INFO("Enter __get_yuv_buffer_size, [%d x %d] (luma_size %d)\n", yuv_cap->pitch, yuv_cap->height, luma_size);
  return total_size;
}

static int __get_yuv_format(int format, struct iav_yuv_cap *yuv_cap)
{
  int data_format = format;

  if (data_format == AUTO_FORMAT) {
    if (yuv_cap->format == IAV_YUV_FORMAT_YUV420) {
      data_format = YUV420_NV12;
    } else if (yuv_cap->format == IAV_YUV_FORMAT_YUV422) {
      data_format = YUV422_NV16;
    } else {
      printf("Unknown YUV format: %d\n", yuv_cap->format);
    }
  } else {
    /* Auto change the format between 420 and 422. */
    switch (data_format) {
      case YUV420_IYUV:
      case YUV420_YV12:
      case YUV420_NV12:
        /* YUV422 to YUV420 is not supported, change save format to YV16 */
        if (yuv_cap->format == IAV_YUV_FORMAT_YUV422) {
          data_format = YUV422_YV16;
        }
        break;
      case YUV422_YU16:
      case YUV422_YV16:
      case YUV422_NV16:
        /* YUV420 to YUV422 is not supported, change save format to NV12 */
        if (yuv_cap->format == IAV_YUV_FORMAT_YUV420) {
          data_format = YUV420_NV12;
        }
        break;
      default:
        break;
    }
  }

  return data_format;
}

static int __copy_yuv_data(int iav_fd, amba_gdma_buf_t *gdma_ctx,
  int dmabuf_fd, struct iav_yuv_cap *yuv_cap, unsigned char is_canvas, unsigned char ext_mem)
{
  struct iav_gdma_copy gdma_copy = {0};
  int rval = 0;
  unsigned int dst_yuv_pitch = yuv_cap->pitch;

  if (gdma_ctx->dst_yuv_pitch > 0) {
    dst_yuv_pitch = gdma_ctx->dst_yuv_pitch;
  }
  gdma_copy.src_skip_cache_sync = 1;
  gdma_copy.src_offset = yuv_cap->y_addr_offset;
  gdma_copy.dst_offset = 0;
  gdma_copy.src_pitch = yuv_cap->pitch;
  gdma_copy.dst_pitch = dst_yuv_pitch;
  gdma_copy.width = yuv_cap->width;
  gdma_copy.height = yuv_cap->height;

  if (dmabuf_fd > 0) {
    gdma_copy.src_dma_buf_fd = dmabuf_fd;
    gdma_copy.src_use_dma_buf_fd = 1;
  } else {
    if (is_canvas) {
      gdma_copy.src_mmap_type = ext_mem ? IAV_PART_CANVAS_POOL : IAV_PART_DSP;
    } else {
      gdma_copy.src_mmap_type = ext_mem ? IAV_PART_PYRAMID_POOL : IAV_PART_DSP;
    }
  }

  if (gdma_ctx->dma_buf_fd > 0) {
    gdma_copy.dst_dma_buf_fd = gdma_ctx->dma_buf_fd;
    gdma_copy.dst_use_dma_buf_fd = 1;
  } else {
    gdma_copy.dst_mmap_type = gdma_ctx->gdma_part_id;
  }

  if (ioctl(iav_fd, IAV_IOC_GDMA_COPY, &gdma_copy) < 0) {
    perror("IAV_IOC_GDMA_COPY");
    rval = -1;
    goto COPY_YUV_DATA_EXIT;
  }

  gdma_ctx->y_addr_offset = 0;
  if (yuv_cap->format != IAV_YUV_FORMAT_YUV400) {

    gdma_copy.src_offset = yuv_cap->uv_addr_offset;
    gdma_copy.dst_offset = dst_yuv_pitch * yuv_cap->height;

    if (yuv_cap->format == IAV_YUV_FORMAT_YUV420) {
      gdma_copy.height = yuv_cap->height / 2;
    } else {
      gdma_copy.height = yuv_cap->height;
    }
    if (ioctl(iav_fd, IAV_IOC_GDMA_COPY, &gdma_copy) < 0) {
      perror("IAV_IOC_GDMA_COPY");
      rval = -1;
      goto COPY_YUV_DATA_EXIT;
    }

    gdma_ctx->uv_addr_offset = dst_yuv_pitch * yuv_cap->height;
  }

COPY_YUV_DATA_EXIT:
  return rval;
}

static int __copy_yuv_me_data(int iav_fd, amba_gdma_buf_t *gdma_ctx,
  int dmabuf_fd, struct iav_yuv_cap *yuv_cap, struct iav_me_cap *me0_cap, struct iav_me_cap *me1_cap,
  unsigned char is_canvas, unsigned char ext_mem,
  unsigned char need_me0, unsigned char need_me1)
{
  struct iav_gdma_copy gdma_copy = {0};
  int rval = 0;
  unsigned int offset = 0;
  unsigned int dst_yuv_pitch = yuv_cap->pitch;
  unsigned int dst_me0_pitch = 0;
  unsigned int dst_me1_pitch = 0;

  if (gdma_ctx->dst_yuv_pitch > 0) {
    dst_yuv_pitch = gdma_ctx->dst_yuv_pitch;
  }

  gdma_copy.src_skip_cache_sync = 1;
  gdma_copy.src_offset = yuv_cap->y_addr_offset;
  gdma_copy.dst_offset = 0;
  gdma_copy.src_pitch = yuv_cap->pitch;
  gdma_copy.dst_pitch = dst_yuv_pitch;
  gdma_copy.width = yuv_cap->width;
  gdma_copy.height = yuv_cap->height;

  if (dmabuf_fd > 0) {
    gdma_copy.src_dma_buf_fd = dmabuf_fd;
    gdma_copy.src_use_dma_buf_fd = 1;
  } else {
    if (is_canvas) {
      gdma_copy.src_mmap_type = ext_mem ? IAV_PART_CANVAS_POOL : IAV_PART_DSP;
    } else {
      gdma_copy.src_mmap_type = ext_mem ? IAV_PART_PYRAMID_POOL : IAV_PART_DSP;
    }
  }

  if (gdma_ctx->dma_buf_fd > 0) {
    gdma_copy.dst_dma_buf_fd = gdma_ctx->dma_buf_fd;
    gdma_copy.dst_use_dma_buf_fd = 1;
  } else {
    gdma_copy.dst_mmap_type = gdma_ctx->gdma_part_id;
  }

  if (ioctl(iav_fd, IAV_IOC_GDMA_COPY, &gdma_copy) < 0) {
    perror("IAV_IOC_GDMA_COPY");
    rval = -1;
    goto COPY_YUV_DATA_EXIT;
  }

  gdma_ctx->y_addr_offset = 0;
  offset += gdma_copy.dst_pitch * gdma_copy.height;
  if (yuv_cap->format != IAV_YUV_FORMAT_YUV400) {

    gdma_copy.src_offset = yuv_cap->uv_addr_offset;
    gdma_copy.dst_offset = offset;

    if (yuv_cap->format == IAV_YUV_FORMAT_YUV420) {
      gdma_copy.height = yuv_cap->height / 2;
    } else {
      gdma_copy.height = yuv_cap->height;
    }
    if (ioctl(iav_fd, IAV_IOC_GDMA_COPY, &gdma_copy) < 0) {
      perror("IAV_IOC_GDMA_COPY");
      rval = -1;
      goto COPY_YUV_DATA_EXIT;
    }

    gdma_ctx->uv_addr_offset = offset;
    offset += gdma_copy.dst_pitch * gdma_copy.height;
  }

  if (need_me0) {
    dst_me0_pitch = me0_cap->pitch;
    if (gdma_ctx->dst_me0_pitch > 0) {
      dst_me0_pitch = gdma_ctx->dst_me0_pitch;
    }
    gdma_copy.src_offset = me0_cap->data_addr_offset;
    gdma_copy.dst_offset = offset;
    gdma_copy.src_pitch = me0_cap->pitch;
    gdma_copy.dst_pitch = dst_me0_pitch;
    gdma_copy.width = me0_cap->width;
    gdma_copy.height = me0_cap->height;

    if (ioctl(iav_fd, IAV_IOC_GDMA_COPY, &gdma_copy) < 0) {
      perror("IAV_IOC_GDMA_COPY");
      rval = -1;
      goto COPY_YUV_DATA_EXIT;
    }

    gdma_ctx->me0_addr_offset = offset;
    offset += gdma_copy.dst_pitch * gdma_copy.height;
  }

  if (need_me1) {
    dst_me1_pitch = me1_cap->pitch;
    if (gdma_ctx->dst_me1_pitch > 0) {
      dst_me1_pitch = gdma_ctx->dst_me1_pitch;
    }
    gdma_copy.src_offset = me1_cap->data_addr_offset;
    gdma_copy.dst_offset = offset;
    gdma_copy.src_pitch = me1_cap->pitch;
    gdma_copy.dst_pitch = dst_me1_pitch;
    gdma_copy.width = me1_cap->width;
    gdma_copy.height = me1_cap->height;

    if (ioctl(iav_fd, IAV_IOC_GDMA_COPY, &gdma_copy) < 0) {
      perror("IAV_IOC_GDMA_COPY");
      rval = -1;
      goto COPY_YUV_DATA_EXIT;
    }

    gdma_ctx->me1_addr_offset = offset;
    offset += gdma_copy.dst_pitch * gdma_copy.height;
  }

COPY_YUV_DATA_EXIT:
  return rval;
}

static int __v6_capture_preview_buffer (int iav_fd, amba_dsp_query_yuv_buffer_t *yuv_buffer)
{
  struct iav_querydesc query_desc;
  struct iav_canvasdesc *canvas_desc;
  struct iav_yuv_cap *yuv_cap = NULL;
  struct iav_me_cap *me0_cap = NULL, *me1_cap = NULL;
  amba_canvas_opt_t *opt = &yuv_buffer->canvas_options;
  amba_gdma_buf_t *gdma_ctx = NULL;
  amba_yuv_buf_t *yuv_ctx = NULL;
  amba_me_buf_t *me_ctx = NULL;
  int rval = 0;
  unsigned int buf = 0;
  unsigned int yuv_buffer_size = 0;
  unsigned int me0_buffer_size = 0, me1_buffer_size = 0;

  do {
    for (buf = 0; buf < opt->canvas_num; buf++) {
      /* query canvas from IAV */
      if (!(opt->canvas_buffer_map & (1 << buf))) {
        continue;
      }

      if (opt->canvas_yuv_buffer_disable[buf]) {
        DPRINT_ERROR("Canvas[%d] yuv buffer is disabled, cannot get yuv data!\n", buf);
        rval = COM_ECODE_BAD_STATE;
        break;
      }
      memset (&query_desc, 0x0, sizeof (query_desc) );
      query_desc.qid = IAV_DESC_CANVAS;
      canvas_desc = &query_desc.arg.canvas;
      canvas_desc->canvas_id = buf;
      canvas_desc->discard_cached_items = opt->discard_cached_items;
      canvas_desc->query_extra_raw = opt->query_extra_raw_info_flag;
      canvas_desc->yuv_use_dma_buf_fd = !!(yuv_buffer->canvas_map_thru_dmabuf & (1 << buf));
      canvas_desc->me_use_dma_buf_fd = !!(yuv_buffer->canvas_map_thru_dmabuf & (1 << buf));
      canvas_desc->skip_cache_sync = 1;
      canvas_desc->idsp_proc_done_pts = 0;
      canvas_desc->is_dsp_hw_pts = 1;
      if (!yuv_buffer->non_block_flag) {
        canvas_desc->non_block_flag &= ~IAV_BUFCAP_NONBLOCK;
      } else {
        canvas_desc->non_block_flag |= IAV_BUFCAP_NONBLOCK;
      }

      if (ioctl(iav_fd, IAV_IOC_QUERY_DESC, &query_desc) < 0) {
        if ((errno == EINTR) || (errno == EAGAIN)) {
          rval = COM_ECODE_TRY_AGAIN;
          break;
        }
        perror ("IAV_IOC_QUERY_DESC");
        DPRINT_ERROR ("IAV_IOC_QUERY_DESC fail, errno %d\n", errno);
        rval = COM_ECODE_BAD_STATE;
        break;
      }

      yuv_cap = &canvas_desc->yuv;

      if ((yuv_cap->y_addr_offset == 0) ||
        ((yuv_cap->uv_addr_offset == 0) && (yuv_cap->format != IAV_YUV_FORMAT_YUV400))) {
        DPRINT_ERROR("YUV buffer [%08x] address is NULL!\n", opt->canvas_buffer_map);
        rval = COM_ECODE_BAD_STATE;
        break;
      }

      yuv_ctx = &yuv_buffer->yuv_ctx[buf];

      yuv_ctx->width = yuv_cap->width;
      yuv_ctx->height = yuv_cap->height;
      yuv_ctx->pitch = yuv_cap->pitch;
      yuv_ctx->seq_num = yuv_cap->seq_num;
      yuv_ctx->format = yuv_cap->format;
      yuv_ctx->dsp_pts = yuv_cap->dsp_pts;
      yuv_ctx->mono_pts = yuv_cap->mono_pts;
      yuv_ctx->y_addr_offset = yuv_cap->y_addr_offset;
      yuv_ctx->uv_addr_offset = yuv_cap->uv_addr_offset;

      if (opt->capture_me0) {
        if (opt->canvas_me_buffer_disable[buf]) {
          DPRINT_ERROR("Canvas[%d] me buffer is disabled, cannot get me data!\n", buf);
          rval = COM_ECODE_BAD_STATE;
          break;
        }
        me0_cap = &canvas_desc->me0;

        if (me0_cap->width == 0) {
          DPRINT_ERROR("Canvas[%d] doesn't have ME0 buffer!\n", buf);
          rval = -1;
          break;
        }
        me_ctx = &yuv_buffer->me0_ctx[buf];

        me_ctx->width = me0_cap->width;
        me_ctx->height = me0_cap->height;
        me_ctx->pitch = me0_cap->pitch;
        me_ctx->seq_num = me0_cap->seq_num;
        me_ctx->dsp_pts = me0_cap->dsp_pts;
        me_ctx->mono_pts = me0_cap->mono_pts;
        me_ctx->data_addr_offset = me0_cap->data_addr_offset;

        me0_buffer_size = me0_cap->pitch * ROUND_UP(me0_cap->height, 16);
      }

      if (opt->capture_me1) {
        if (opt->canvas_me_buffer_disable[buf]) {
          DPRINT_ERROR("Canvas[%d] me buffer is disabled, cannot get me data!\n", buf);
          rval = COM_ECODE_BAD_STATE;
          break;
        }
        me1_cap = &canvas_desc->me1;

        if (me1_cap->width == 0) {
          DPRINT_ERROR("Canvas[%d] doesn't have ME1 buffer!\n", buf);
          rval = -1;
          break;
        }
        me_ctx = &yuv_buffer->me1_ctx[buf];

        me_ctx->width = me1_cap->width;
        me_ctx->height = me1_cap->height;
        me_ctx->pitch = me1_cap->pitch;
        me_ctx->seq_num = me1_cap->seq_num;
        me_ctx->dsp_pts = me1_cap->dsp_pts;
        me_ctx->mono_pts = me1_cap->mono_pts;
        me_ctx->data_addr_offset = me1_cap->data_addr_offset;

        me1_buffer_size = me1_cap->pitch * ROUND_UP(me1_cap->height, 16);
      }

      if (yuv_buffer->gdma_copy_enable) {
        gdma_ctx = &yuv_buffer->gdma_ctx[buf];

        yuv_buffer_size = __get_yuv_buffer_size(yuv_cap,
            __get_yuv_format(AUTO_FORMAT, yuv_cap));

        if (yuv_buffer_size == 0) {
          DPRINT_ERROR("buffer size need allocate is 0!.\n");
          rval = COM_ECODE_BAD_STATE;
          break;
        }

        if (yuv_buffer_size + me0_buffer_size + me1_buffer_size > gdma_ctx->gdma_buf_size) {
          DPRINT_INFO("Current buf size [%u] is less than yuv data size [%d], alloc again.\n",
              gdma_ctx->gdma_buf_size, yuv_buffer_size + me0_buffer_size + me1_buffer_size);
          __v6_gdma_free_buf(iav_fd, gdma_ctx);
        }

        if (gdma_ctx->gdma_part_id < 0 && gdma_ctx->dma_buf_fd < 0) {
          if (__v6_gdma_alloc_buf(iav_fd, yuv_buffer_size + me0_buffer_size + me1_buffer_size,
              gdma_ctx, !!(yuv_buffer->canvas_map_thru_dmabuf & (1 << buf)))) {
            DPRINT_ERROR("alloc gdma buf failed.\n");
            rval = COM_ECODE_BAD_STATE;
            break;
          }
        }

        /* copy canvas data to prealloc dst buffer through gdma */
        rval = __copy_yuv_me_data(iav_fd, gdma_ctx, canvas_desc->yuv_dma_buf_fd,
          yuv_cap, me0_cap, me1_cap, 1, opt->canvas_mf_enable[buf] || opt->canvas_ext_mem_enable[buf],
          opt->capture_me0, opt->capture_me1);
        if (rval < 0) {
          DPRINT_ERROR("Failed to copy yuv data of buf [%08x].\n", opt->canvas_buffer_map);
          rval = COM_ECODE_BAD_STATE;
          break;
        }
      }
      yuv_buffer->yuv_ctx[buf].yuv_dma_buf_fd = canvas_desc->yuv_dma_buf_fd;
      yuv_buffer->me0_ctx[buf].me_dma_buf_fd = canvas_desc->me_dma_buf_fd;
      yuv_buffer->me1_ctx[buf].me_dma_buf_fd = canvas_desc->me_dma_buf_fd;

    }
  } while (0);

  return rval;
}

static int __v6_capture_pyramid_buffer (int iav_fd, amba_dsp_query_yuv_buffer_t *yuv_buffer)
{
  int buf = 0;
  amba_pyramid_opt_t *opt = &yuv_buffer->pyramid_options;
  amba_gdma_buf_t *gdma_ctx = NULL;
  amba_yuv_buf_t *yuv_ctx = NULL;
  struct iav_querydesc query_desc;
  struct iav_yuv_cap *pyramid_cap;
  struct iav_feed_pyramid feed_pyramid;
  struct iav_pyramiddesc *pyramid = NULL;
  unsigned int yuv_buffer_size = 0;
  int rval = 0;

  do {
    /* query pyramid from IAV */
    memset(&query_desc, 0, sizeof(query_desc));
    memset(&feed_pyramid, 0x0, sizeof(feed_pyramid));

    query_desc.qid = IAV_DESC_PYRAMID;
    pyramid = &query_desc.arg.pyramid;
    pyramid->chan_id = opt->channel_id;
    pyramid->use_dma_buf_fd = !!yuv_buffer->canvas_map_thru_dmabuf;
    pyramid->skip_cache_sync = 1;
    pyramid->is_dsp_hw_pts = 1;
    if (!yuv_buffer->non_block_flag) {
      pyramid->non_block_flag &= ~IAV_BUFCAP_NONBLOCK;
    } else {
      pyramid->non_block_flag |= IAV_BUFCAP_NONBLOCK;
    }

    /* for pyramid manual feed case, feed pyramid first */
    if (opt->pyramid_manual_feed[opt->channel_id]) {
      feed_pyramid.chan_id = opt->channel_id;
      if (ioctl(iav_fd, IAV_IOC_FEED_PYRAMID_BUF, &feed_pyramid) < 0) {
        perror("IAV_IOC_FEED_PYRAMID_BUF");
        rval = COM_ECODE_BAD_STATE;
        break;
      }
    }

    if (ioctl(iav_fd, IAV_IOC_QUERY_DESC, &query_desc) < 0) {
      if ((errno == EINTR) || (errno == EAGAIN)) {
        rval = COM_ECODE_TRY_AGAIN;
        break;
      } else {
        perror("IAV_IOC_QUERY_DESC");
        DPRINT_ERROR ("IAV_IOC_QUERY_DESC fail, errno %d\n", errno);
        rval = COM_ECODE_BAD_STATE;
        break;
      }
    }

    /* save pyramid layers in queried pyramid one by one */
    for (buf = 0; buf < IAV_MAX_PYRAMID_LAYERS; ++buf) {
      if ((opt->pyramid_buffer_map & (1 << buf)) == 0) {
        continue;
      }

      if ((pyramid->layers_map & (1 << buf)) == 0) {
        DPRINT_ERROR("Pyramid channel %d: layer %d is not switched on\n",
            opt->channel_id, buf);
        continue;
      }

      pyramid_cap = &pyramid->layers[buf];

      if ((pyramid_cap->y_addr_offset == 0) ||
        (pyramid_cap->uv_addr_offset == 0)) {
        DPRINT_ERROR("Pyramid layer %d YUV buffer address is NULL!\n", buf);
        rval = COM_ECODE_BAD_STATE;
        break;
      }

      yuv_ctx = &yuv_buffer->yuv_ctx[buf];

      yuv_ctx->width = pyramid_cap->width;
      yuv_ctx->height = pyramid_cap->height;
      yuv_ctx->pitch = pyramid_cap->pitch;
      yuv_ctx->seq_num = pyramid_cap->seq_num;
      yuv_ctx->format = pyramid_cap->format;
      yuv_ctx->dsp_pts = pyramid_cap->dsp_pts;
      yuv_ctx->mono_pts = pyramid_cap->mono_pts;
      yuv_ctx->y_addr_offset = pyramid_cap->y_addr_offset;
      yuv_ctx->uv_addr_offset = pyramid_cap->uv_addr_offset;

      /* allocate dst buffer for gdma copy */

      if (yuv_buffer->gdma_copy_enable) {
        gdma_ctx = &yuv_buffer->gdma_ctx[buf];
        yuv_buffer_size = __get_yuv_buffer_size(pyramid_cap,
            __get_yuv_format(AUTO_FORMAT, pyramid_cap));

        if (yuv_buffer_size == 0) {
          DPRINT_ERROR("buffer size need allocate is 0!.\n");
          return COM_ECODE_BAD_STATE;
        }

        if (yuv_buffer_size > gdma_ctx->gdma_buf_size) {
          __v6_gdma_free_buf(iav_fd, gdma_ctx);
        }

        if (gdma_ctx->gdma_part_id < 0 && gdma_ctx->dma_buf_fd < 0) {
          if (__v6_gdma_alloc_buf(iav_fd, yuv_buffer_size, gdma_ctx, !!yuv_buffer->canvas_map_thru_dmabuf)) {
            DPRINT_ERROR("alloc gdma buf failed.\n");
            return COM_ECODE_BAD_STATE;
          }
        }

        /* copy pyramid layers data to prealloc dst buffer through gdma */
        if (__copy_yuv_data(iav_fd, gdma_ctx, -1, pyramid_cap, 0,
            opt->pyramid_manual_feed[opt->channel_id] || opt->pyramid_ext_mem[opt->channel_id]) < 0) {
          DPRINT_ERROR("Failed to copy yuv data of buf [%d].\n", buf);
          rval = COM_ECODE_BAD_STATE;
          break;
        }
      }

    }

    /* for manual feed case, release the queried pyramid buffer */
    if (opt->pyramid_manual_feed[opt->channel_id]) {
      if (ioctl(iav_fd, IAV_IOC_RELEASE_PYRAMID_BUF, pyramid) < 0) {
        perror("IAV_IOC_RELEASE_PYRAMID_BUF");
        rval = COM_ECODE_BAD_STATE;
        break;
      }
    }
  }while (0);


  return rval;
}

static int __copy_raw_rgb_data(int iav_fd, int is_raw, int is_raw_ce, struct iav_rawbufdesc *raw_desc,
    amba_dsp_query_yuv_buffer_t *yuv_buffer, unsigned int ce_format,
    int ext_mem)
{
  unsigned int buffer_size = 0, raw_size = 0, raw_ce_size = 0;
  int rval = 0;
  u32 ce_height = 0;
  struct iav_gdma_copy gdma_copy = {0};
  amba_gdma_buf_t *gdma_ctx = NULL;

  if (is_raw) {
    raw_size = raw_desc->width_in_byte * raw_desc->height;
  }

  if (is_raw_ce) {
    if (ce_format == IAV_CE_FORMAT_4H4V) {
      ce_height = raw_desc->height >> 2;
    } else {
      ce_height = raw_desc->height;
    }

    raw_ce_size = raw_desc->ce_width_in_byte * ce_height;
  }

  buffer_size = raw_size + raw_ce_size;
  gdma_ctx = &yuv_buffer->gdma_ctx[yuv_buffer->raw_options.vinc_id];

  if (buffer_size > gdma_ctx->gdma_buf_size) {
    __v6_gdma_free_buf(iav_fd, gdma_ctx);
  }

  if (gdma_ctx->gdma_part_id < 0) {
    if (__v6_gdma_alloc_buf(iav_fd, buffer_size, gdma_ctx, 0)) {
      DPRINT_ERROR("alloc gdma buf failed.\n");
      return -1;
    }
  }

  gdma_copy.src_skip_cache_sync = 1;
  if (ext_mem) {
    gdma_copy.src_mmap_type = IAV_PART_RAW_POOL;
  } else {
    gdma_copy.src_mmap_type = IAV_PART_DSP;
  }
  gdma_copy.dst_mmap_type = gdma_ctx->gdma_part_id;

  if (is_raw) {
    gdma_copy.src_offset = raw_desc->raw_addr_offset;
    gdma_copy.dst_offset = 0;
    gdma_copy.src_pitch = raw_desc->pitch;
    gdma_copy.height = raw_desc->height;
    gdma_copy.dst_pitch = raw_desc->width_in_byte;
    gdma_copy.width = raw_desc->width_in_byte;

    if (ioctl(iav_fd, IAV_IOC_GDMA_COPY, &gdma_copy) < 0) {
      perror("IAV_IOC_GDMA_COPY");
      return -1;
    }
  }

  if (is_raw_ce) {
    gdma_copy.src_offset = raw_desc->ce_addr_offset;
    gdma_copy.dst_offset = raw_size;
    gdma_copy.src_pitch = raw_desc->ce_pitch;
    gdma_copy.height = ce_height;
    gdma_copy.dst_pitch = raw_desc->ce_width_in_byte;
    gdma_copy.width = raw_desc->ce_width_in_byte;

    if (ioctl(iav_fd, IAV_IOC_GDMA_COPY, &gdma_copy) < 0) {
      perror("IAV_IOC_GDMA_COPY");
      return -1;
    }
  }
  return rval;
}

static int __copy_raw_yuv_data(int iav_fd, struct iav_rawbufdesc *raw_desc,
    amba_dsp_query_yuv_buffer_t *yuv_buffer, int ext_mem)
{
  u32 buffer_size = 0;
  int rval = 0;
  int dst_pitch = 0;
  u8 chroma_height_factor = 1;
  struct iav_gdma_copy gdma_copy = {0};
  amba_gdma_buf_t *gdma_ctx = NULL;

  dst_pitch = raw_desc->width_in_byte;

  chroma_height_factor = raw_desc->format == IAV_RAW_FORMAT_YUV422 ? 2 : 1;

  buffer_size = (dst_pitch * raw_desc->height * (2 + chroma_height_factor)) >> 1;

  gdma_ctx = &yuv_buffer->gdma_ctx[yuv_buffer->raw_options.vinc_id];

  if (buffer_size > gdma_ctx->gdma_buf_size) {
    __v6_gdma_free_buf(iav_fd, gdma_ctx);
  }

  if (gdma_ctx->gdma_part_id < 0) {
    if (__v6_gdma_alloc_buf(iav_fd, buffer_size, gdma_ctx, 0)) {
    DPRINT_ERROR("alloc gdma buf failed.\n");
    return -1;
    }
  }

  gdma_copy.src_skip_cache_sync = 1;
  gdma_copy.src_offset = raw_desc->raw_addr_offset;
  gdma_copy.dst_offset = 0;
  gdma_copy.src_pitch =  raw_desc->pitch;
  gdma_copy.height = raw_desc->height;
  gdma_copy.dst_pitch = raw_desc->width_in_byte;
  gdma_copy.width = raw_desc->width_in_byte;
  if (ext_mem) {
    gdma_copy.src_mmap_type = IAV_PART_RAW_POOL;
  } else {
    gdma_copy.src_mmap_type = IAV_PART_DSP;
  }
  gdma_copy.dst_mmap_type = gdma_ctx->gdma_part_id;
  if (ioctl(iav_fd, IAV_IOC_GDMA_COPY, &gdma_copy) < 0) {
    perror("IAV_IOC_GDMA_COPY");
    return -1;
  }

  gdma_copy.src_offset = raw_desc->ce_addr_offset;
  gdma_copy.dst_offset = dst_pitch * raw_desc->height;
  gdma_copy.src_pitch = raw_desc->ce_pitch;
  gdma_copy.height = (raw_desc->height * chroma_height_factor) >> 1;
  gdma_copy.dst_pitch = dst_pitch;
  gdma_copy.width = dst_pitch;
  if (ext_mem) {
    gdma_copy.src_mmap_type = IAV_PART_RAW_POOL;
  } else {
    gdma_copy.src_mmap_type = IAV_PART_DSP;
  }
  if (ioctl(iav_fd, IAV_IOC_GDMA_COPY, &gdma_copy) < 0) {
    perror("IAV_IOC_GDMA_COPY");
    rval = -1;
  }

  return rval;
}

static int __v6_capture_raw (int iav_fd, amba_dsp_query_yuv_buffer_t *yuv_buffer)
{
  amba_raw_opt_t *opt = &yuv_buffer->raw_options;
  amba_raw_buf_t *raw_ctx = NULL;
  struct iav_rawbufdesc *raw_desc = NULL;
  struct iav_querydesc query_desc;
  struct vin_global_info vsrc_info = {0};
  struct iav_chan_cfg chan_cfgs = {0};
  int rval = 0;
  unsigned int i = 0;
  unsigned int ext_mem = 0, ce_format = 0;
  int is_raw_valid = 0, is_ce_valid = 0;

  do {
    AM_IOCTL(iav_fd, IAV_IOC_VIN_GET_GLOBAL_INFO, &vsrc_info);
    for (i = 0; i < opt->channel_num; i++) {
      chan_cfgs.chan_id = i;
      AM_IOCTL(iav_fd, IAV_IOC_GET_CHAN_CONFIG, &chan_cfgs);
      if (vsrc_info.vsrc_map[opt->vinc_id] & (1U << chan_cfgs.vsrc_id)) {
        ext_mem = chan_cfgs.ext_mem || chan_cfgs.raw_manual_feed;
        ce_format = chan_cfgs.ce_format;
        break;
      }
    }

    memset(&query_desc, 0, sizeof(query_desc));
    query_desc.qid = IAV_DESC_RAW;
    raw_desc = &query_desc.arg.raw;
    raw_desc->vin_id = opt->vinc_id;
    raw_desc->skip_cache_sync = 1;
    if (ioctl(iav_fd, IAV_IOC_QUERY_DESC, &query_desc) < 0) {
      if ((errno == EINTR) || (errno == EAGAIN)) {
        rval = COM_ECODE_TRY_AGAIN;
        break;
      } else {
        perror("IAV_IOC_QUERY_DESC");
        DPRINT_ERROR ("IAV_IOC_QUERY_DESC fail, errno %d\n", errno);
        rval = COM_ECODE_BAD_STATE;
        break;
      }
    }

    if (!raw_desc->pitch || !raw_desc->height || !raw_desc->width) {
      //printf("Raw data resolution %ux%u with pitch %u is invalid!\n",
      //raw_desc->width, raw_desc->height, raw_desc->pitch);
      is_raw_valid = 0;
    } else {
      is_raw_valid = 1;
    }

    if (opt->capture_raw_ce) {
      if (!raw_desc->ce_pitch || !raw_desc->height || !raw_desc->ce_width) {
        //printf("Contrast enhance raw data resolution %ux%u with pitch %u is invalid!\n",
        //raw_desc->ce_width, raw_desc->height, raw_desc->ce_pitch);
        is_ce_valid = 0;
      } else {
        is_ce_valid = 1;
      }
    }

    if (!is_raw_valid && !is_ce_valid) {
      DPRINT_ERROR("RAW and CE are both invalid!\n");
      rval = COM_ECODE_BAD_STATE;
      break;
    }
    raw_ctx = &yuv_buffer->raw_ctx[opt->vinc_id];

    raw_ctx->raw_addr_offset = raw_desc->raw_addr_offset;
    raw_ctx->ce_addr_offset = raw_desc->ce_addr_offset;
    raw_ctx->width = raw_desc->width;
    raw_ctx->height = raw_desc->height;
    raw_ctx->pitch = raw_desc->pitch;
    raw_ctx->seq_num = raw_desc->seq_num;
    raw_ctx->format = raw_desc->format;

    raw_ctx->dsp_pts = raw_desc->dsp_pts;
    raw_ctx->mono_pts = raw_desc->mono_pts;

    /* allocate dst buffer for gdma copy */

    if (yuv_buffer->gdma_copy_enable) {
      if (raw_desc->format == IAV_RAW_FORMAT_RGB) {
        if (__copy_raw_rgb_data(iav_fd, is_raw_valid, is_ce_valid, raw_desc, yuv_buffer, ce_format, ext_mem) < 0) {
          rval = COM_ECODE_BAD_STATE;
          break;
        }
      } else {
        if (__copy_raw_yuv_data(iav_fd, raw_desc, yuv_buffer, ext_mem) < 0) {
          rval = COM_ECODE_BAD_STATE;
          break;
        }
      }
    }
  } while (0);
  return rval;
}

static int __v6_query_yuv_buffer (int iav_fd, amba_dsp_query_yuv_buffer_t *yuv_buffer)
{
  int ret = 0;
  switch (yuv_buffer->capture_select) {
    case CAPTURE_PREVIEW_BUFFER:
      if (yuv_buffer->canvas_options.query_canvasgrp_flag) {
        //capture_multi_yuv(canvas_buffer_map, canvas_map_thru_dmabuf, frame_count, !info_only);
        printf("not support query_canvasgrp now!\n");
        ret = COM_ECODE_BAD_STATE;
        break;
      } else {
        ret = __v6_capture_preview_buffer(iav_fd, yuv_buffer);
      }
      break;
    case CAPTURE_PYRAMID_BUFFER:
      ret = __v6_capture_pyramid_buffer(iav_fd, yuv_buffer);
      break;
    case CAPTURE_RAW_BUFFER:
      ret = __v6_capture_raw(iav_fd, yuv_buffer);
      break;
    default:
      printf("Invalid capture mode [%d] !\n", yuv_buffer->capture_select);
      ret = COM_ECODE_BAD_STATE;
      break;
  }
  return ret;
}

static int __v6_release_canvas_buffer(int fd_iav, iav_release_canvas_cfg_t *ctx)
{
  struct iav_canvasdesc canvas;

  memset(&canvas, 0, sizeof(struct iav_canvasdesc));
  canvas.canvas_id = ctx->canvas_id;
  canvas.yuv.seq_num = ctx->seq_num;
  canvas.yuv_use_dma_buf_fd = ctx->yuv_use_dma_buf_fd;
  canvas.yuv_dma_buf_fd = ctx->yuv_dma_buf_fd;
  canvas.me_use_dma_buf_fd = ctx->me_use_dma_buf_fd;
  canvas.me_dma_buf_fd = ctx->me_dma_buf_fd;
  canvas.feed_seq_num = ctx->feed_seq_num;
  if (ioctl(fd_iav, IAV_IOC_RELEASE_CANVAS_BUF, &canvas) < 0) {
    perror("IAV_IOC_RELEASE_CANVAS_BUF\n");
    return -1;
  }
  return 0;
}

static int __v6_gdma_copy (int iav_fd, amba_gdma_copy_t *copy)
{
  struct iav_gdma_copy param = {0};
  int ret = 0;

  param.src_offset = copy->src_offset;
  param.dst_offset = copy->dst_offset;
  param.src_pitch = copy->src_pitch;
  param.dst_pitch = copy->dst_pitch;
  param.width = copy->width;
  param.height = copy->height;
  param.src_dma_buf_fd = copy->src_dma_buf_fd;
  param.dst_dma_buf_fd = copy->dst_dma_buf_fd;
  param.src_use_dma_buf_fd = 1;
  param.dst_use_dma_buf_fd = 1;

  ret = ioctl (iav_fd, IAV_IOC_GDMA_COPY, &param);
  if (ret < 0) {
    perror ("IAV_IOC_GDMA_COPY");
    DPRINT_ERROR ("IAV_IOC_GDMA_COPY fail, errno %d\n", errno);
    return ret;
  }

  return 0;
}

static int __v6_dsp_enter_idle_mode (int iav_fd, int vin_off, int no_vout_reset)
{
  struct iav_idle_params idle_params = {0};
  idle_params.poweroff_vin = vin_off;
  idle_params.no_vout_reset = no_vout_reset;

  int ret = ioctl (iav_fd, IAV_IOC_ENTER_IDLE, &idle_params);

  if (0 > ret) {
    perror ("IAV_IOC_ENTER_IDLE");
    DPRINT_ERROR ("enter idle mode fail, errno %d\n", errno);
  }

  return ret;
}

static int __v6_enable_preview(int iav_fd, int no_vout_reset)
{
  struct iav_preview_params prev_params = {0};
  prev_params.no_vout_reset = no_vout_reset;
  AM_IOCTL(iav_fd, IAV_IOC_ENABLE_PREVIEW, &prev_params);
  return 0;
}

static int __v6_set_overlay(int iav_fd, iav_set_overlay_t *overlay_set)
{
  int i = 0, buf_id = 0;
  struct iav_overlay_area *area = NULL;
  struct iav_overlay_insert overlay_insert = {0};
  memcpy(&overlay_insert, &overlay_set->overlay_insert, sizeof(struct iav_overlay_insert));


  if (overlay_insert.enable) {
    for (i = 0; i < overlay_set->overlay_max_num; i++) {

      if (overlay_insert.area[i].enable) {
        area = &overlay_insert.area[i];
        buf_id = overlay_set->osd[i].buf_id;
        area->data_addr_offset = overlay_set->osd[i].buf_data[buf_id];
      }
    }

  }

  if (ioctl (iav_fd, IAV_IOC_SET_OVERLAY_INSERT, &overlay_insert) < 0) {
    perror ("IAV_IOC_SET_OVERLAY_INSERT");
    return -1;
  }

  return 0;
}

static int __v6_set_frame_sync(int iav_fd, iav_set_overlay_t *overlay_set)
{
  struct iav_stream_cfg sync_frame = {0};
  int i = 0, buf_id = 0;
  struct iav_overlay_area *area = NULL;
  struct iav_overlay_insert *overlay_insert = &overlay_set->overlay_insert;

  if (overlay_insert->enable) {
    for (i = 0; i < overlay_set->overlay_max_num; i++) {
      if (overlay_insert->area[i].enable) {
        area = &overlay_insert->area[i];
        buf_id = overlay_set->osd[i].buf_id;
        area->data_addr_offset = overlay_set->osd[i].buf_data[buf_id];
      }
    }

  }

  memcpy(&sync_frame.arg.overlay, overlay_insert, sizeof(struct iav_overlay_insert));
  sync_frame.id = overlay_insert->id;
  sync_frame.cid = IAV_STMCFG_OVERLAY;
  sync_frame.strm_sync_type = IAV_FRAME_SYNC;

  if (ioctl (iav_fd, IAV_IOC_CFG_FRAME_SYNC_PROC, &sync_frame) < 0) {
    perror ("IAV_IOC_CFG_FRAME_SYNC_PROC");
    return -1;
  }

  return 0;
}

static int __v6_apply_frame_sync(int iav_fd, unsigned int dsp_pts,
    unsigned int stream_updated_map, unsigned int force_update)
{
  struct iav_apply_frame_sync apply;

  memset(&apply, 0, sizeof(apply));
  apply.dsp_pts = dsp_pts;
  apply.force_update = force_update;
  apply.strm_sync_type = IAV_FRAME_SYNC;
  apply.stream_updated_map = stream_updated_map;
  if (ioctl(iav_fd, IAV_IOC_APPLY_FRAME_SYNC_PROC, &apply) < 0) {
    perror("IAV_IOC_APPLY_FRAME_SYNC_PROC");
    return -1;
  }
  return 0;
}

static unsigned int __v6_get_enc_src_canvas_id(int iav_fd, unsigned int stream_id)
{
  struct iav_stream_cfg format_cfg = {};

  memset(&format_cfg, 0, sizeof(format_cfg));
  format_cfg.id = stream_id;
  format_cfg.cid = IAV_STMCFG_FORMAT;
  AM_IOCTL(iav_fd, IAV_IOC_GET_STREAM_CONFIG, &format_cfg);
  return format_cfg.arg.format.enc_src_id;
}

static unsigned int __v6_get_enc_dummy_latency(int iav_fd, unsigned int canvas_id)
{
  struct iav_canvas_cfg canvas_cfg;

  memset(&canvas_cfg, 0, sizeof(canvas_cfg));
  canvas_cfg.canvas_id = canvas_id;
  if (ioctl(iav_fd, IAV_IOC_GET_CANVAS_CONFIG, &canvas_cfg) < 0) {
    perror("IAV_IOC_GET_CANVAS_CONFIG\n");
    return -1;
  }

  return canvas_cfg.enc_dummy_latency;
}

static int __v6_get_stream_state(int iav_fd, unsigned int stream_id)
{
  struct iav_queryinfo query_info = {};
  struct iav_stream_info *stream_info = NULL;

  memset(&query_info, 0, sizeof(query_info));
  query_info.qid = IAV_INFO_STREAM;
  stream_info = &query_info.arg.stream;
  stream_info->id = stream_id;
  AM_IOCTL(iav_fd, IAV_IOC_QUERY_INFO, &query_info);

  return stream_info->state;
}

static int __v6_set_img_scale(int iav_fd, img_scale_cfg_t *img_scale_cfg)
{
  struct iav_img_scale img_scale = {0};

  img_scale.format = IAV_YUV_FORMAT_YUV420;
  img_scale.non_block_flag = 0;
  img_scale.input.x = img_scale_cfg->input.x;
  img_scale.input.y = img_scale_cfg->input.y;
  img_scale.input.width = img_scale_cfg->input.w;
  img_scale.input.height = img_scale_cfg->input.h;

  img_scale.output.x = img_scale_cfg->output.x;
  img_scale.output.y = img_scale_cfg->output.y;
  img_scale.output.width = img_scale_cfg->output.w;
  img_scale.output.height = img_scale_cfg->output.h;


  img_scale.input_buf.pid = img_scale_cfg->input_buf_pid;
  img_scale.input_buf.use_phys = 0;
  img_scale.input_buf.use_dma_buf_fd = 1;
  img_scale.input_buf.pitch = img_scale_cfg->input_buf_picth;
  img_scale.input_buf.height = img_scale_cfg->input_buf_height;
  img_scale.input_buf.y_offset = 0;
  img_scale.input_buf.uv_offset = img_scale.input_buf.pitch * img_scale.input_buf.height;

  img_scale.output_buf.pid = img_scale_cfg->output_buf_pid;
  img_scale.output_buf.use_phys = 0;
  img_scale.output_buf.use_dma_buf_fd = 1;
  img_scale.output_buf.pitch = img_scale_cfg->output_buf_picth;
  img_scale.output_buf.height = img_scale_cfg->output_buf_height;
  img_scale.output_buf.y_offset = 0;
  img_scale.output_buf.uv_offset = img_scale.output_buf.pitch * img_scale.output_buf.height;

  AM_IOCTL(iav_fd, IAV_IOC_SET_IMG_SCALE, &img_scale);

  return 0;
}
/* For V6, EFR memory partition can be from anonymous memory */
static int __v6_alloc_map_efr_mem(int iav_fd, amba_iav_partition_t *iav_partition)
{
  struct iav_alloc_mem_part alloc_mem_part;

  memset(&alloc_mem_part, 0, sizeof(alloc_mem_part));
  alloc_mem_part.length = iav_partition->request_size;
  alloc_mem_part.enable_cache = 0;
  alloc_mem_part.from_private = 1;
  alloc_mem_part.accessibility = IAV_MEM_PART_ACS_ARM | IAV_MEM_PART_ACS_DSP;

  if (ioctl(iav_fd, IAV_IOC_ALLOC_ANON_MEM_PART, &alloc_mem_part) < 0) {
    DPRINT_ERROR ("Fail to allocate anonymous memory [size = 0x%lx] for efr\n", iav_partition->request_size);
    return -1;
  }

  iav_partition->partition_id = alloc_mem_part.pid;
  iav_partition->allocate_size = alloc_mem_part.length;
  iav_partition->phys_addr = alloc_mem_part.offset;
  if (iav_partition->allocate_size) {
    iav_partition->virt_addr = mmap(NULL, alloc_mem_part.length, PROT_READ | PROT_WRITE,
      MAP_SHARED, iav_fd, alloc_mem_part.offset);
    if (iav_partition->virt_addr == MAP_FAILED) {
      DPRINT_ERROR ("Fail to mmap anonymous memory for efr\n");
      return -1;
    }
  }

  return 0;
}

static int __v6_unmap_efr_mem(int iav_fd, amba_iav_partition_t *iav_partition)
{
  struct iav_alloc_mem_part alloc_mem_part;

  if (iav_partition->allocate_size && iav_partition->virt_addr != NULL) {
      munmap(iav_partition->virt_addr, iav_partition->allocate_size);
      iav_partition->allocate_size = 0;
      iav_partition->virt_addr = NULL;
      iav_partition->phys_addr = 0;
  }

  if (iav_partition->partition_id >= 0) {
    memset(&alloc_mem_part, 0, sizeof(alloc_mem_part));
    alloc_mem_part.pid = iav_partition->partition_id;
    AM_IOCTL(iav_fd, IAV_IOC_FREE_MEM_PART, &alloc_mem_part);
    iav_partition->partition_id = -1;
  }

  return 0;
}

static int __v6_get_efr_setup(int iav_fd, amba_efr_setup_t *efr_setup)
{
  struct iav_raw_enc_setup setup;

  memset(&setup, 0, sizeof(setup));
  setup.vinc_id = efr_setup->vinc_id;
  AM_IOCTL(iav_fd, IAV_IOC_GET_RAW_ENCODE, &setup);

  /* Note: Users must ensure that the new buf idx is continuous with the idx at the end of the last setup */
  if ((setup.raw_daddr_buf_idx != IAV_INVALID_RAW_ENC_BUF_ID) &&
    (setup.raw_daddr_buf_idx < efr_setup->buf_num)) {
    efr_setup->buf_idx = (setup.raw_daddr_buf_idx + 1) % efr_setup->buf_num;
    efr_setup->mem_init_needed = 0;
  } else {
    efr_setup->buf_idx = 0;
    efr_setup->mem_init_needed = 1;
  }

  return 0;
}

static int __v6_set_efr_cfg(int iav_fd, amba_efr_cfg_t *efr_cfg)
{
  struct iav_raw_enc_cfg enc_cfg;

  memset(&enc_cfg, 0, sizeof(enc_cfg));
  enc_cfg.vinc_id = efr_cfg->vinc_id;
  enc_cfg.total_buf_num = efr_cfg->buf_num;
  enc_cfg.raw_buf_addr = efr_cfg->raw_buf_addr;
  enc_cfg.raw_hdec_buf_addr = efr_cfg->raw_hdec_buf_addr;
  enc_cfg.raw_pitch = efr_cfg->raw_pitch;
  enc_cfg.raw_width = efr_cfg->raw_width;
  enc_cfg.raw_height = efr_cfg->raw_height;
  enc_cfg.raw_hdec_pitch = efr_cfg->raw_hdec_pitch;
  enc_cfg.raw_hdec_width = efr_cfg->raw_hdec_width;
  enc_cfg.raw_hdec_height = efr_cfg->raw_hdec_height;
  AM_IOCTL(iav_fd, IAV_IOC_SET_RAW_ENC_CFG, &enc_cfg);

  return 0;
}

static int __v6_set_efr_setup(int iav_fd, amba_efr_setup_t *efr_setup)
{
  struct iav_raw_enc_setup setup;

  memset(&setup, 0, sizeof(setup));
  setup.vinc_id = efr_setup->vinc_id;
  setup.raw_daddr_buf_idx = efr_setup->buf_idx;
  setup.raw_hdec_daddr_buf_idx = setup.raw_daddr_buf_idx;
  setup.raw_low_hdec_daddr_buf_idx = setup.raw_daddr_buf_idx;
  setup.frame_pts = efr_setup->frame_pts;
  AM_IOCTL(iav_fd, IAV_IOC_SET_RAW_ENCODE, &setup);

  return 0;
}

static int __v6_wait_efr_done(int iav_fd, int vinc_id)
{
  int ret = 0;

  ret = ioctl(iav_fd, IAV_IOC_WAIT_RAW_ENCODE, &vinc_id);
  if (ret < 0) {
    DPRINT_ERROR ("Sleep 1 second to make sure >= 1 frame time!\n");
    sleep(1);
  }

  return 0;
}

static int __v6_efm_lib_init(iav_efm_usr_cfg_t *efm_cfg_ext)
{
  struct efm_usr_cfg cfg;
  int ret = 0;

  memcpy(&cfg, efm_cfg_ext, sizeof(struct efm_usr_cfg));
  ret = efm_lib_init(&cfg);
  return ret;
}

static int __v6_efm_lib_deinit(void)
{
  int ret = 0;

  ret = efm_lib_deinit();
  return ret;
}

static int __v6_efm_get_buf(iav_efm_buf_info_t *buf_info_ext)
{
  struct efm_buf_info buf_info;
  int ret = 0;

  buf_info.stream_id = buf_info_ext->stream_id;
  ret = efm_get_buf(&buf_info);
  memcpy(buf_info_ext, &buf_info, sizeof(struct efm_buf_info));

  return ret;
}

static int __v6_efm_feed_buf(iav_efm_buf_info_t *buf_info_ext, iav_efm_feed_cfg_t *feed_cfg_ext)
{
  struct efm_buf_info buf_info;
  struct efm_feed_cfg feed_cfg;
  int ret = 0;

  memcpy(&buf_info, buf_info_ext, sizeof(iav_efm_buf_info_t));
  memcpy(&feed_cfg, feed_cfg_ext, sizeof(iav_efm_feed_cfg_t));
  ret = efm_feed_buf(&buf_info, &feed_cfg);
  return ret;
}

static int __v6_efm_get_stream_cfg(iav_efm_stream_cfg_t *stream_cfg_ext)
{
  struct efm_stream_cfg stream_cfg;
  int ret = 0;

  stream_cfg.stream_id = stream_cfg_ext->stream_id;
  ret = efm_get_stream_cfg(&stream_cfg);
  memcpy(stream_cfg_ext, &stream_cfg, sizeof(struct efm_stream_cfg));

  return ret;
}

static int __v6_query_canvas_info(int iav_fd, amba_canvas_info_t *info)
{
  struct iav_queryinfo query_info;
  struct iav_canvas_info *canvas_info;

  memset(&query_info, 0, sizeof(struct iav_queryinfo));
  query_info.qid = IAV_INFO_CANVAS;
  canvas_info = &query_info.arg.canvas;
  canvas_info->canvas_id = info->canvas_id;
  AM_IOCTL(iav_fd, IAV_IOC_QUERY_INFO, &query_info);

  info->width = canvas_info->width;
  info->height = canvas_info->height;
  info->yuv_pitch = canvas_info->yuv_pitch;
  info->me0_pitch = canvas_info->me0_pitch;
  info->me1_pitch = canvas_info->me1_pitch;
  info->me0_width = canvas_info->me0_width;
  info->me0_height = canvas_info->me0_height;
  info->me1_width = canvas_info->me1_width;
  info->me1_height = canvas_info->me1_height;

  return 0;
}

static int __v6_query_pyramid_info(int iav_fd, amba_pyramid_info_t *info)
{
  struct iav_queryinfo query_info;
  struct iav_pyramid_layers_info *pyramid_info;

  memset(&query_info, 0, sizeof(struct iav_queryinfo));
  query_info.qid = IAV_INFO_PYRAMID_LAYERS;
  pyramid_info = &query_info.arg.pyramid_layers_info;
  pyramid_info->channel_id = info->channel_id;
  AM_IOCTL(iav_fd, IAV_IOC_QUERY_INFO, &query_info);

  for (int i = 0; i < IAV_MAX_PYRAMID_LAYERS; i++) {
    info->layer_size[i].width = pyramid_info->layer_size[i].width;
    info->layer_size[i].height = pyramid_info->layer_size[i].height;
    info->layer_size[i].pitch = pyramid_info->layer_size[i].pitch;
  }

  return 0;
}

static int __v6_blur_lib_init(int iav_fd)
{
  int ret = 0;

  ret = blur_lib_init(iav_fd);
  return ret;
}

static int __v6_blur_lib_deinit(void)
{
  int ret = 0;

  ret = blur_lib_deinit();
  return ret;
}

static int __v6_blur_get_mem_cfg(iav_blur_mem_cfg_t *cfg_ext)
{
  struct blur_mem_cfg cfg;
  int ret = 0;

  ret = blur_get_mem_cfg(&cfg);
  memcpy(cfg_ext, &cfg, sizeof(struct blur_mem_cfg));

  return ret;
}

static int __v6_blur_set_mem_cfg(iav_blur_mem_cfg_t *cfg_ext)
{
  struct blur_mem_cfg cfg;
  int ret = 0;

  memcpy(&cfg, cfg_ext, sizeof(struct blur_mem_cfg));
  ret = blur_set_mem_cfg(&cfg);

  return ret;
}

static int __v6_blur_get_stream_cfg(iav_blur_stream_cfg_t *stream_cfg_ext)
{
  struct blur_stream_cfg stream_cfg;
  int ret = 0;

  stream_cfg.stream_id = stream_cfg_ext->stream_id;
  ret = blur_get_stream_cfg(&stream_cfg);
  memcpy(stream_cfg_ext, &stream_cfg, sizeof(struct blur_stream_cfg));

  return ret;
}

static int __v6_blur_set_stream_cfg(iav_blur_stream_cfg_t *stream_cfg_ext)
{
  struct blur_stream_cfg stream_cfg;
  int ret = 0;

  memcpy(&stream_cfg, stream_cfg_ext, sizeof(struct blur_stream_cfg));
  ret = blur_set_stream_cfg(&stream_cfg);

  return ret;
}

static int __v6_blur_get_area_buf(iav_blur_area_buf_t *buf_info_ext)
{
  struct blur_area_buf buf_info;
  int ret = 0;

  buf_info.stream_id = buf_info_ext->stream_id;
  buf_info.area_id = buf_info_ext->area_id;
  ret = blur_get_area_buf(&buf_info);
  memcpy(buf_info_ext, &buf_info, sizeof(struct blur_area_buf));

  return ret;
}

static int __v6_blur_put_area_buf(iav_blur_area_buf_t *buf_info_ext)
{
  struct blur_area_buf buf_info;
  int ret = 0;

  memcpy(&buf_info, buf_info_ext, sizeof(struct blur_area_buf));
  ret = blur_put_area_buf(&buf_info);

  return ret;
}

static int __v6_blur_apply(iav_blur_apply_cfg_t *apply_ext)
{
  struct blur_apply_cfg apply;
  int ret = 0;

  apply.stream_map = apply_ext->stream_map;
  apply.frame_sync = apply_ext->frame_sync;
  ret = blur_apply(&apply);

  return ret;
}

static int __v6_blur_set_color(iav_blur_color_cfg_t *color_cfg_ext)
{
  struct blur_color_cfg blur_color_info;
  int ret = 0;

  memcpy(&blur_color_info, color_cfg_ext, sizeof(struct blur_color_cfg));
  ret = blur_set_color(&blur_color_info);

  return ret;
}

static int __v6_blur_get_color(iav_blur_color_cfg_t *color_cfg_ext)
{
  struct blur_color_cfg blur_color_info;
  int ret = 0;

  ret = blur_get_color(&blur_color_info);
  memcpy(color_cfg_ext, &blur_color_info, sizeof(struct blur_color_cfg));

  return ret;
}

static void __setup_v6_al_context (iav_al_t *al)
{
  al->f_get_dsp_mode = __v6_get_dsp_mode;

  al->f_enter_mode = __v6_enter_decode_mode;
  al->f_leave_mode = __v6_leave_decode_mode;
  al->f_create_decoder = __v6_create_decoder;
  al->f_destroy_decoder = __v6_destroy_decoder;
  al->f_query_decode_config = __v6_query_decode_config;

  al->f_trickplay = __v6_decode_trick_play;
  al->f_start = __v6_decode_start;
  al->f_stop = __v6_decode_stop;
  al->f_speed = __v6_decode_speed;
  al->f_request_bsb = __v6_decode_request_bits_fifo;

  al->f_decode = __v6_decode;

  al->f_query_print_decode_bsb_status = __v6_decode_query_bsb_status_and_print;
  al->f_query_print_decode_status = __v6_decode_query_status_and_print;
  al->f_query_decode_bsb_status = __v6_decode_query_bsb_status;
  al->f_query_decode_status = __v6_decode_query_status;
  al->f_decode_wait_vout_dormant = NULL;
  al->f_decode_wake_vout = NULL;
  al->f_decode_wait_eos_flag = NULL;
  al->f_decode_wait_eos = __v6_decode_wait_eos;

  al->f_configure_vout = __configure_vout;

  al->f_get_vout_info = __get_single_vout_info;
  al->f_get_vin_info = __v6_get_vin_info;
  al->f_get_stream_framefactor = __v6_get_stream_framefactor;

  al->f_map_bsb = __v6_map_bsb;
  al->f_map_dsp = __v6_map_dsp;
  al->f_map_overlay = __v6_map_overlay;
  al->f_map_dec_bsb = __v6_map_dec_bsb;

  al->f_unmap_bsb = __v6_unmap_bsb;
  al->f_unmap_dsp = __v6_unmap_dsp;
  al->f_unmap_overlay = __v6_unmap_overlay;
  al->f_unmap_dec_bsb = __v6_unmap_dec_bsb;

  al->f_flush_frame_desc = __v6_flush_frame_desc;
  al->f_read_bitstream = __v6_read_bitstream;
  al->f_release_bitstream = __v6_release_bitstream;
  al->f_is_ready_for_read_bitstream = __v6_is_ready_for_read_bitstream;

  al->f_encode_start = __v6_encode_start;
  al->f_encode_stop = __v6_encode_stop;

  al->f_query_encode_stream_info = __v6_query_encode_stream_info;
  al->f_query_encode_stream_fmt = __v6_query_encode_stream_format;
  al->f_get_stream_overlay_pixel_format = __v6_get_stream_overlay_pixel_format;

  al->f_query_source_buffer_info = __v6_query_source_buffer_info;
  al->f_query_yuv_buffer = __v6_query_yuv_buffer;
  al->f_release_canvas_buffer = __v6_release_canvas_buffer;

  al->f_query_canvas_info = __v6_query_canvas_info;
  al->f_query_pyramid_info = __v6_query_pyramid_info;

  al->f_gdma_copy = __v6_gdma_copy;
  al->f_gdma_alloc_buf = __v6_gdma_alloc_buf;
  al->f_gdma_free_buf = __v6_gdma_free_buf;

  al->f_enter_idle_mode = __v6_dsp_enter_idle_mode;
  al->f_enable_preview = __v6_enable_preview;

  // overlay related
  al->f_set_overlay = __v6_set_overlay;
  al->f_set_frame_sync = __v6_set_frame_sync;
  al->f_apply_frame_sync = __v6_apply_frame_sync;

 // blur related
 al->f_blur_lib_init = __v6_blur_lib_init;
 al->f_blur_get_mem_cfg = __v6_blur_get_mem_cfg;
 al->f_blur_set_mem_cfg = __v6_blur_set_mem_cfg;
 al->f_blur_get_stream_cfg = __v6_blur_get_stream_cfg;
 al->f_blur_set_stream_cfg = __v6_blur_set_stream_cfg;
 al->f_blur_get_area_buf = __v6_blur_get_area_buf;
 al->f_blur_put_area_buf = __v6_blur_put_area_buf;
 al->f_blur_apply = __v6_blur_apply;
 al->f_blur_set_color = __v6_blur_set_color;
 al->f_blur_get_color = __v6_blur_get_color;
 al->f_blur_lib_deinit = __v6_blur_lib_deinit;

  // encoding related
  al->f_update_enc_resolution = update_enc_resolution;
  al->f_update_enc_bitrate = update_enc_bitrate;
  al->f_update_enc_framerate = update_enc_framerate;
  al->f_update_enc_bitrate_frameate = update_enc_bitrate_frameate;
  al->f_update_enc_codec_type = update_enc_codec_type;
  al->f_update_enc_gop_structure = update_enc_gop_structure;

  al->f_enc_force_idr = enc_force_idr;

  al->f_check_iav_state = __v6_check_iav_state;
  al->f_get_iav_state = __v6_get_iav_state;
  al->f_get_resource_info = __v6_get_resource_info;
  al->f_get_enc_src_canvas_id = __v6_get_enc_src_canvas_id;
  al->f_get_enc_dummy_latency = __v6_get_enc_dummy_latency;
  al->f_get_stream_state = __v6_get_stream_state;
  al->f_set_img_scale = __v6_set_img_scale;

  // EFR related
  al->f_alloc_map_efr_mem = __v6_alloc_map_efr_mem;
  al->f_unmap_efr_mem = __v6_unmap_efr_mem;
  al->f_get_efr_setup= __v6_get_efr_setup;
  al->f_set_efr_cfg = __v6_set_efr_cfg;
  al->f_set_efr_setup = __v6_set_efr_setup;
  al->f_wait_efr_done = __v6_wait_efr_done;
  al->f_efm_lib_init = __v6_efm_lib_init;
  al->f_efm_lib_deinit = __v6_efm_lib_deinit;
  al->f_efm_get_buf = __v6_efm_get_buf;
  al->f_efm_feed_buf = __v6_efm_feed_buf;
  al->f_efm_get_stream_cfg = __v6_efm_get_stream_cfg;
}


#endif

#endif

int open_iav_handle ()
{
#ifdef BUILD_MODULE_AMBA_DSP
  int fd = open ("/dev/iav", O_RDWR, 0);

  if (0 > fd) {
    DPRINT_ERROR ("open iav fail, %d.\n", fd);
  }

  return fd;
#endif

  DPRINT_ERROR ("dsp related is not compiled\n");
  return (-4);
}

void close_iav_handle (int fd)
{
#ifdef BUILD_MODULE_AMBA_DSP

  if (0 > fd) {
    DPRINT_ERROR ("bad fd %d\n", fd);
    return;
  }

  close (fd);
  return;
#endif

  DPRINT_ERROR ("dsp related is not compiled\n");
  return;
}

void initialize_iav_al (iav_al_t *al)
{
#ifdef BUILD_MODULE_AMBA_DSP
#if defined (BUILD_DSP_AMBA_V5)
  __setup_v5_al_context (al);
#elif defined (BUILD_DSP_AMBA_V6)
  __setup_v6_al_context (al);
#else
  DPRINT_ERROR ("add support here\n");
#endif
  return;
#endif

  DPRINT_ERROR ("dsp related is not compiled\n");
  return;
}

const char *get_dsp_platform_name()
{
#ifdef BUILD_MODULE_AMBA_DSP
#if defined (BUILD_DSP_AMBA_V5)
    return "V5";
#elif defined (BUILD_DSP_AMBA_V6)
	return "V6";
#else
    return "Unknown";
#endif
#endif
    return "NoDSP";
}

int config_amba_vout (int fd, iav_al_t *al, amba_vout_config_t *config)
{
#ifdef BUILD_MODULE_AMBA_DSP
  int ret = 0;

  if (!config) {
    DPRINT_ERROR ("NULL config.\n");
    return COM_ECODE_BAD_STATE;
  }

  if (!al->f_configure_vout) {
    DPRINT_ERROR ("configure vout function is NULL.\n");
    return COM_ECODE_BAD_STATE;
  }

  if (fd >= 0) {
    ret = al->f_configure_vout (fd, config);
  } else {
    DPRINT_ERROR ("open_iav_handle fail.\n");
    return COM_ECODE_BAD_STATE;
  }

  if (!ret) {
    return COM_ECODE_OK;
  }

  DPRINT_NOTICE ("configure vout failed\n");
  return COM_ECODE_BAD_STATE;
#endif

  DPRINT_ERROR ("dsp related is not compiled\n");
  return COM_ECODE_NOT_SUPPORTED;
}

int halt_amba_vout(int fd, int vout_number)
{
#ifdef BUILD_MODULE_AMBA_DSP
  int i = 0;

  DPRINT_NOTICE ("halt current vout, begin\n");

  if (fd >= 0) {
    for (i = 0; i < vout_number; i ++) {
      if (__is_vout_alive (fd, i) ) {
        DPRINT_NOTICE ("halt %d\n", i);
        __halt_vout (fd, i);
      }
    }

    DPRINT_NOTICE ("halt current vout, end\n");
    return COM_ECODE_OK;
  }

  DPRINT_ERROR ("open_iav_handle fail.\n");
  return COM_ECODE_BAD_STATE;
#endif
  DPRINT_ERROR ("dsp related is not compiled\n");
  return COM_ECODE_NOT_SUPPORTED;
}

void fill_amba_h264_gop_header (unsigned char *p_gop_header,
  unsigned int frame_tick, unsigned int time_scale,
  unsigned int pts, unsigned char gopsize, unsigned char m)
{
  DUNUSED(pts);
  unsigned int tick_high = frame_tick;
  unsigned int tick_low = tick_high & 0x0000ffff;
  unsigned int scale_high = time_scale;
  unsigned int scale_low = scale_high & 0x0000ffff;
  unsigned int pts_high = 0;
  unsigned int pts_low = 0;

  tick_high >>= 16;
  scale_high >>= 16;

  p_gop_header[0] = 0; // start code prefix
  p_gop_header[1] = 0;
  p_gop_header[2] = 0;
  p_gop_header[3] = 1;

  p_gop_header[4] = 0x7a; // nal type = 0x1a
  p_gop_header[5] = 0x01; // version main
  p_gop_header[6] = 0x01; // version sub

  p_gop_header[7] = tick_high >> 10;
  p_gop_header[8] = tick_high >> 2;
  p_gop_header[9] = (tick_high << 6) | (1 << 5) | (tick_low >> 11);
  p_gop_header[10] = tick_low >> 3;

  p_gop_header[11] = (tick_low << 5) | (1 << 4) | (scale_high >> 12);
  p_gop_header[12] = scale_high >> 4;
  p_gop_header[13] = (scale_high << 4) | (1 << 3) | (scale_low >> 13);
  p_gop_header[14] = scale_low >> 5;

  p_gop_header[15] = (scale_low << 3) | (1 << 2) | (pts_high >> 14);
  p_gop_header[16] = pts_high >> 6;

  p_gop_header[17] = (pts_high << 2) | (1 << 1) | (pts_low >> 15);
  p_gop_header[18] = pts_low >> 7;
  p_gop_header[19] = (pts_low << 1) | 1;

  p_gop_header[20] = gopsize;
  p_gop_header[21] = (m & 0xf) << 4;
}

void update_amba_h264_gop_header (unsigned char *p_gop_header,
  unsigned int pts, unsigned char gopsize)
{
  unsigned int pts_high = (pts >> 16) & 0x0000ffff;
  unsigned int pts_low = pts & 0x0000ffff;

  p_gop_header[15] = (p_gop_header[15]  & 0xFC) | (pts_high >> 14);
  p_gop_header[16] = pts_high >> 6;

  p_gop_header[17] = (pts_high << 2) | (1 << 1) | (pts_low >> 15);
  p_gop_header[18] = pts_low >> 7;
  p_gop_header[19] = (pts_low << 1) | 1;

  p_gop_header[20] = gopsize;
}

void fill_amba_h265_gop_header (unsigned char *p_gop_header,
  unsigned int frame_tick, unsigned int time_scale,
  unsigned int pts, unsigned char gopsize, unsigned char m)
{
  DUNUSED(pts);
  unsigned int tick_high = frame_tick;
  unsigned int tick_low = tick_high & 0x0000ffff;
  unsigned int scale_high = time_scale;
  unsigned int scale_low = scale_high & 0x0000ffff;
  unsigned int pts_high = 0;
  unsigned int pts_low = 0;

  tick_high >>= 16;
  scale_high >>= 16;

  p_gop_header[0] = 0; // start code prefix
  p_gop_header[1] = 0;
  p_gop_header[2] = 0;
  p_gop_header[3] = 1;

  p_gop_header[4] = 0x34; // nal type = 0x1a
  p_gop_header[5] = 0x01;
  p_gop_header[6] = 0x01; // version main
  p_gop_header[7] = 0x01; // version sub

  p_gop_header[8] = tick_high >> 10;
  p_gop_header[9] = tick_high >> 2;
  p_gop_header[10] = (tick_high << 6) | (1 << 5) | (tick_low >> 11);
  p_gop_header[11] = tick_low >> 3;

  p_gop_header[12] = (tick_low << 5) | (1 << 4) | (scale_high >> 12);
  p_gop_header[13] = scale_high >> 4;
  p_gop_header[14] = (scale_high << 4) | (1 << 3) | (scale_low >> 13);
  p_gop_header[15] = scale_low >> 5;

  p_gop_header[16] = (scale_low << 3) | (1 << 2) | (pts_high >> 14);
  p_gop_header[17] = pts_high >> 6;

  p_gop_header[18] = (pts_high << 2) | (1 << 1) | (pts_low >> 15);
  p_gop_header[19] = pts_low >> 7;
  p_gop_header[20] = (pts_low << 1) | 1;

  p_gop_header[21] = gopsize;
  p_gop_header[22] = (m & 0xf) << 4;
}

void update_amba_h265_gop_header (unsigned char *p_gop_header,
  unsigned int pts, unsigned char gopsize)
{
  unsigned int pts_high = (pts >> 16) & 0x0000ffff;
  unsigned int pts_low = pts & 0x0000ffff;

  p_gop_header[16] = (p_gop_header[16]  & 0xFC) | (pts_high >> 14);
  p_gop_header[17] = pts_high >> 6;

  p_gop_header[18] = (pts_high << 2) | (1 << 1) | (pts_low >> 15);
  p_gop_header[19] = pts_low >> 7;
  p_gop_header[20] = (pts_low << 1) | 1;

  p_gop_header[21] = gopsize;
}
