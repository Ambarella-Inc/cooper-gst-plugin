/*
 * iav_al.h
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

#ifndef __IAV_AL_H__
#define __IAV_AL_H__

#include "iav_ioctl.h"

#include "iav_al_enc_params.h"

#define DAMBADSP_MAX_INTRA_DECODE_CMD_NUMBER 4
#define DAMBADSP_MAX_INTRA_FB_NUMBER 4
#define DAMBADSP_MAX_INTRA_YUV2YUV_DST_FB_NUMBER 3

#define DAMBA_H264_GOP_HEADER_LENGTH 22
#define DAMBA_H265_GOP_HEADER_LENGTH 23
#define DAMBA_MAX_GOP_HEADER_LENGTH 24

#define DAMBA_RESERVED_SPACE 32

#define DDECODER_STOPPED (-1678)

#define DDSP_MAX_PYRAMID_LAYERS 6

#define DAMBADSP_MAX_DECODER_NUMBER 16
#define DAMBADSP_MAX_VOUT_NUMBER 4

#define DAMBA_HWTIMER_OUTPUT_FREQ (90000)

#define DAMBA_MAX_YUV_BUF_NUM (IAV_MAX_CANVAS_BUF_NUM > IAV_MAX_PYRAMID_LAYERS ? IAV_MAX_CANVAS_BUF_NUM : IAV_MAX_PYRAMID_LAYERS)

#define MAX_OVERLAY_AREA_NUM (MAX_NUM_OVERLAY_AREA)
#define OVERLAY_CLUT_NUM (16)
#ifndef OVERLAY_CLUT_SIZE
#define OVERLAY_CLUT_SIZE (1024)
#endif
//#define OVERLAY_MOTION_CLUT_OFFSET (OVERLAY_CLUT_NUM * OVERLAY_CLUT_SIZE)
#define OVERLAY_YUV_OFFSET (OVERLAY_CLUT_NUM * OVERLAY_CLUT_SIZE)
#ifndef OVERLAY_BUF_PITCH_ALIGN
#define OSD_BUF_PITCH_ALIGN (32)
#else
#define OSD_BUF_PITCH_ALIGN (OVERLAY_BUF_PITCH_ALIGN)
#endif

#ifndef OVERLAY_BUF_WIDTH_ALIGN
#define OSD_BUF_WIDTH_ALIGN (4)
#else
#define OSD_BUF_WIDTH_ALIGN (OVERLAY_BUF_WIDTH_ALIGN)
#endif

#define OSD_MAX_BUFFER_NUM (4)
#define MAX_BLUR_AREA_NUM (MAX_NUM_BLUR_AREA)

#ifndef AM_IOCTL
#define AM_IOCTL(_filp, _cmd, _arg) \
    do { \
      if (ioctl(_filp, _cmd, _arg) < 0) { \
        perror(#_cmd); \
        return -1; \
      } \
    } while (0)
#endif

enum {
  EAMDSP_VIDEO_CODEC_TYPE_INVALID = 0x00,
  EAMDSP_VIDEO_CODEC_TYPE_H264 = 0x01,
  EAMDSP_VIDEO_CODEC_TYPE_H265 = 0x02,
  EAMDSP_VIDEO_CODEC_TYPE_MJPEG = 0x03,
};

enum {
  EAMDSP_BUFFER_PIX_FMT_420 = 1,
  EAMDSP_BUFFER_PIX_FMT_422 = 2,
};

enum {
  EAMDSP_TRICK_PLAY_PAUSE = 0,
  EAMDSP_TRICK_PLAY_RESUME = 1,
  EAMDSP_TRICK_PLAY_STEP = 2,
};

enum {
  EAMDSP_PB_DIRECTION_FW = 0,
  EAMDSP_PB_DIRECTION_BW = 1,
};

enum {
  EAMDSP_PB_SCAN_MODE_ALL_FRAMES = 0,
  EAMDSP_PB_SCAN_MODE_I_ONLY = 1,
};

enum {
  EAMDSP_ENC_STREAM_STATE_IDLE = 0,
  EAMDSP_ENC_STREAM_STATE_STARTING = 1,
  EAMDSP_ENC_STREAM_STATE_ENCODING = 2,
  EAMDSP_ENC_STREAM_STATE_STOPPING = 3,
  EAMDSP_ENC_STREAM_STATE_UNKNOWN = 4,
  EAMDSP_ENC_STREAM_STATE_ERROR = 5,
};

enum {
  EAMDSP_ENC_STREAM_SOURCE_BUFFER_MAIN = 0,
  EAMDSP_ENC_STREAM_SOURCE_BUFFER_SECOND = 1,
  EAMDSP_ENC_STREAM_SOURCE_BUFFER_THIRD = 2,
  EAMDSP_ENC_STREAM_SOURCE_BUFFER_FOURTH = 3,
  EAMDSP_ENC_STREAM_SOURCE_BUFFER_FIFTH = 4,
  EAMDSP_ENC_STREAM_SOURCE_BUFFER_EFM = 5,

  EAMDSP_ENC_STREAM_SOURCE_BUFFER_INVALID = 255,
};

typedef enum {
  EDSP_PYRAMID_SCALE_SQRT2 = 0, /*!< 0, 1/sqrt(2) for each layers, both width and height */
  EDSP_PYRAMID_SCALE_2X = 1, /*!< 1, 1/2 for each layers, both width and height */
  EDSP_PYRAMID_SCALE_ARBITRARY = 2, /*!< 2, arbitrary size for pyramid layer 1 */
} EDSPPyramidScale;

enum {
  //from dsp define
  EPredefinedPictureType_IDR = 1,
  EPredefinedPictureType_I = 2,
  EPredefinedPictureType_P = 3,
  EPredefinedPictureType_B = 4,
};

typedef struct {
  unsigned int dsp_mode;
} amba_dsp_mode_t;

typedef struct {
  unsigned int x, y, w, h;
} rect_t;

typedef struct {
  unsigned char chan_id;
  unsigned char decoder_type;
  unsigned char enable_vout;
  unsigned char layers_map; /*!< bit map for pyramid layers, one bit for one layer */

  unsigned int max_frm_width;
  unsigned int max_frm_height;

  //pyramid
  void *ext_buf_addr;
  unsigned int ext_buf_size;

  EDSPPyramidScale scale_type; /*!< pyramid scale type, @sa EDSPPyramidScale */
  rect_t crop_win[DDSP_MAX_PYRAMID_LAYERS]; /*!< Cropping window in each layer output coordinate */

  unsigned int layer1_width; /*!< Rescale size for pyramid layer 1, valid for arbitrary scale, 1/2 for next layers */
  unsigned int layer1_height;
} amba_dsp_decode_chan_config_t;

typedef struct {
  unsigned int chan_num;
  amba_dsp_decode_chan_config_t dec_cfg[DAMBADSP_MAX_DECODER_NUMBER];
} amba_dsp_decode_multi_chan_config_t;

typedef struct {
  unsigned char b_support_ff_fb_bw;
  unsigned char debug_max_frame_per_interrupt;
  unsigned char debug_use_dproc;
  unsigned char num_decoder;

  unsigned short max_gop_size;
  unsigned char vout_mask;
  unsigned char num_vout;

  unsigned int max_vout0_width;
  unsigned int max_vout0_height;

  unsigned int max_vout1_width;
  unsigned int max_vout1_height;

  //single chan, old version
  unsigned char decoder_type[DAMBADSP_MAX_DECODER_NUMBER];
  unsigned char enable_vout[DAMBADSP_MAX_DECODER_NUMBER];
  unsigned int max_frm_width[DAMBADSP_MAX_DECODER_NUMBER];
  unsigned int max_frm_height[DAMBADSP_MAX_DECODER_NUMBER];

  //multi chan, new version
  amba_dsp_decode_chan_config_t multi_chan_configs[DAMBADSP_MAX_DECODER_NUMBER];
} amba_dsp_decode_mode_config_t;

typedef struct {
  unsigned char vout_id;
  unsigned char enable;
  unsigned char flip;
  unsigned char rotate;

  unsigned short    target_win_offset_x;
  unsigned short    target_win_offset_y;

  unsigned short    target_win_width;
  unsigned short    target_win_height;

  unsigned int    zoom_factor_x;
  unsigned int    zoom_factor_y;
  unsigned int    vout_mode;
} amba_dsp_dec_vout_config_t;

typedef struct {
  unsigned char decoder_id;
  unsigned char decoder_type;
  unsigned char num_vout;
  unsigned char b_use_addr;

  unsigned int    width;
  unsigned int    height;

  amba_dsp_dec_vout_config_t vout_configs[DAMBADSP_MAX_VOUT_NUMBER];

  unsigned int    bsb_start_offset;
  unsigned int    bsb_size;
} amba_dsp_decoder_info_t;

typedef struct {
  unsigned char auto_map_bsb;
  unsigned char rendering_monitor_mode;
  unsigned char reserved0;
  unsigned char reserved1;
} amba_dsp_query_decode_config_t;

typedef struct {
  unsigned char  decoder_id;
  unsigned char  num_frames;
  unsigned char  reserved1;
  unsigned char  reserved2;

  unsigned int start_ptr_offset;
  unsigned int end_ptr_offset;

  unsigned int first_frame_display;
} amba_dsp_decode_t;

typedef struct {
  int sink_id;
  int sink_type;
  int source_id;

  int rotate;
  int flip;
  int offset_x;
  int offset_y;
  int width;
  int height;
  unsigned int mode;
} amba_dsp_vout_info_t;

typedef struct {
  unsigned int vsrc_id;    /* Input params */
  unsigned int width;
  unsigned int height;
  unsigned int fps;

  unsigned int fr_num;
  unsigned int fr_den;
  unsigned int vinc_id;

  unsigned char  format;
  unsigned char  type;
  unsigned char  bits;
  unsigned char  ratio;

  unsigned char  system;
  unsigned char  flip;
  unsigned char  rotate;
  unsigned char  pattern;
} amba_dsp_vin_info_t;

typedef struct {
  unsigned short framefactor_num;
  unsigned short framefactor_den;
} amba_dsp_stream_framefactor_t;

typedef struct {
  unsigned char b_two_times;
  unsigned char b_enable_read;
  unsigned char b_enable_write;
  unsigned char reserved0;

  void *base;
  unsigned int size;
} iav_map_bsb_t;

typedef struct {
  void *base;
  unsigned int size;
} iav_map_dsp_t;

typedef struct {
  unsigned long size;
  unsigned char *base;
} iav_map_overlay_t;

typedef struct {
  unsigned char enable;
  unsigned char reserved[3];

  unsigned int width;
  unsigned int height;
  unsigned int x;
  unsigned int y;
  int buf_id;
  unsigned int buf_num;
  unsigned long buf_data[OSD_MAX_BUFFER_NUM];
} osd_info_t;

typedef struct {
  //unsigned char osd_enable;
  unsigned char overlay_max_num;
  unsigned char rotate;
  unsigned char sync_with_pts;
  unsigned char reserved;

  osd_info_t osd[MAX_OVERLAY_AREA_NUM];

  struct iav_overlay_insert overlay_insert;
} iav_set_overlay_t;

#ifdef BUILD_DSP_AMBA_V5
typedef struct {
  u8 stream_id;  /*!< EFM stream id */
  u8 stream_type;  /*!< EFM stream type */
  u8 efm_enable : 1;  /*!< This is a flag to enable / disable encoding from memory */
  u8 reserved0 : 7;
  u8 jpeg_slice_num;  /*!< Slice number for MJPEG multi-slice encoding */
  struct iav_efm_get_pool_info pool_info;
} iav_efm_stream_cfg_t;

typedef struct {
  u32 skip_cache_sync : 1;  /*!< The buffer cache will not be synced if this flag is enabled */
  u32 reserved :31;
  u64 pts;  /*!< each frame pts */
} iav_efm_feed_cfg_t;

typedef struct {
  int iav_fd;
  u32 request_mode[IAV_STREAM_MAX_NUM_ALL];  /*!< efm buf request mode */
  u32 no_prefetch_stream_map;  /*!< bitmap of the streams that enable no prefetch for their EFM frame buffer pool */
  void (*iav_state_notifier)(void *private_data, u32 iav_state, int reinit_status);  /*!< When iav_state is detected to be changed by lib_efm, this callback func will be invoked */
  void *notifier_private_data;  /*!< private_data of iav_state_notifier */
} iav_efm_usr_cfg_t;
#endif

#ifdef BUILD_DSP_AMBA_V6
typedef struct {
  u8 stream_id;  /*!< EFM stream id */
  u8 stream_type;  /*!< EFM stream type */
  u16 efm_enable : 1;  /*!< This is a flag to enable / disable encoding from memory */
  u16 efm_blur_enable : 1; /*!< This is a flag to enable / disable blur insertion to EFM stream */
  u16 reserved0 : 14;
  u32 reserved1[2];
  struct iav_efm_get_pool_info pool_info;
} iav_efm_stream_cfg_t;

typedef struct {
  u32 skip_cache_sync : 1;  /*!< The buffer cache will not be synced if this flag is enabled */
  u32 is_last_frame : 1;
  u32 reserved1 :30;
  u64 pts;  /*!< each frame pts */
  u32 reserved2[5];
} iav_efm_feed_cfg_t;

typedef struct {
  int iav_fd;
  void (*iav_state_notifier)(void *private_data, u32 iav_state, int reinit_status);  /*!< When iav_state is detected to be changed by lib_efm, this callback func will be invoked */
  void *notifier_private_data;  /*!< private_data of iav_state_notifier */
  u32 stream_map;    /*!< EFM stream map. */
  u32 reserved[2];
} iav_efm_usr_cfg_t;
#endif

typedef struct {
  u8 stream_id;  /*!< EFM stream id */
  u8 reserved[3];
  u32 frame_idx;  /*!< EFM frame index */
  u16 yuv_pitch;  /*!< YUV pitch */
  u16 yuv_height;  /*!< Height of the YUV data */
  u16 me1_pitch;  /*!< ME1 pitch */
  u16 me1_height;  /*!< Height of the ME1 data */
  u16 me1_width;  /*!< Width of the ME1 data */
  u16 me0_pitch;  /*!< ME0 pitch */
  u16 me0_height;  /*!< Height of the ME0 data */
  u16 me0_width;  /*!< Height of the ME0 data */
  unsigned long yuv_luma_phy_addr;  /*!< YUV Luma data physical address */
  void *yuv_luma_vir_addr;  /*!< YUV Luma data virtual address */
  unsigned long yuv_chroma_phy_addr;  /*!< YUV chroma data physical address */
  void *yuv_chroma_vir_addr;  /*!< YUV chroma data virtual address */
  unsigned long me1_phy_addr;  /*!< ME1 data physical address */
  void *me1_vir_addr;  /*!< ME1 data virtual address */
  unsigned long me0_phy_addr;  /*!< ME0 data physical address */
  void *me0_vir_addr;  /*!< ME0 data virtual address */
#ifdef BUILD_DSP_AMBA_V6
  u32 reserved2;
  u32 frame_me_idx; /*!< ME buffer index */
#endif
} iav_efm_buf_info_t;

typedef struct {
  unsigned char canvas_id; /*!< Canvas ID */
  unsigned char yuv_use_dma_buf_fd; /*!< When set, dma-buf:fd will be used to describe YUV data. */
  unsigned char me_use_dma_buf_fd; /*!< When set, dma-buf:fd will be used to describe ME data. */
  unsigned char reserved;

  u64 feed_seq_num; /*!< The sequence number of current canvas buffer for manual feed.
      For IAV_IOC_RELEASE_CANVAS_BUF, when feed_seq_num is specified to -1(0xFFFFFFFFFFFFFFFF),
      it will release all locked canvas buffers. */
  int yuv_dma_buf_fd; /*!< The dma-buf:fd of YUV data, only valid when yuv_use_dma_buf_fd is set */
  int me_dma_buf_fd; /*!< The dma-buf:fd of ME data (ME0 and ME1 share the same dma-buf:fd),
      only valid when me_use_dma_buf_fd is set */

  u32 seq_num; /*!< Sequence number of the YUV data */

} iav_release_canvas_cfg_t;


typedef struct {
  unsigned int stream_idx;

  unsigned int offset;
  unsigned int size; // 0 means stream end
  unsigned long pts;

  unsigned int video_width;
  unsigned int video_height;

  unsigned int stream_format;

  unsigned int encoded_frame_num;
  unsigned char slice_id;
  unsigned char slice_num;
  unsigned char tile_id;
  unsigned char tile_num;

  unsigned char hint_frame_type;
  unsigned char hint_is_keyframe;
  unsigned char reserved0;
  unsigned char reserved1;

  unsigned int timeout_ms;
  void *framedesc;

  unsigned char * p_data_sim;
} amba_dsp_read_bitstream_t;

typedef struct {
  unsigned int stream_idx;
  void *framedesc;
} amba_dsp_release_bitstream_t;

typedef struct {
  unsigned char decoder_id;
  unsigned char reserved0;
  unsigned char reserved1;
  unsigned char reserved2;

  unsigned int  start_offset;
  unsigned int  room;

  unsigned int  dsp_read_offset;
  unsigned int  free_room;
} amba_dsp_bsb_status_t;

typedef struct {
  unsigned char decoder_id;
  unsigned char reserved0;
  unsigned char is_started;
  unsigned char is_send_stop_cmd;

  unsigned int  last_pts;

  unsigned int  decode_state;
  unsigned int  error_status;
  unsigned int  total_error_count;
  unsigned int  decoded_pic_number;

  unsigned int  write_offset;
  unsigned int  room;
  unsigned int  dsp_read_offset;
  unsigned int  free_room;

  unsigned int  irq_count;
  unsigned int  yuv422_y_addr;
  unsigned int  yuv422_uv_addr;
} amba_dsp_decode_status_t;

typedef struct {
  unsigned char decoder_id;
  unsigned char reserved0;
  unsigned char reserved1;
  unsigned char reserved2;

  unsigned int last_pts_high;
  unsigned int last_pts_low;
} amba_dsp_decode_eos_timestamp_t;

enum {
  ECHECK_CANVAS_STYLE_UNKNOWN = 0x0,
  ECHECK_CANVAS_STYLE_ENABLED = 0x1,
  ECHECK_CANVAS_STYLE_DISABLED = 0x2,
};

typedef struct {
  unsigned char buf_id;
  unsigned char reserved0;
  unsigned char reserved1;
  unsigned char check_canvas_style;

  unsigned int size_width;
  unsigned int size_height;

  unsigned int crop_size_x;
  unsigned int crop_size_y;
  unsigned int crop_pos_x;
  unsigned int crop_pos_y;
} amba_dsp_source_buffer_info_t;

typedef enum {
  AUTO_FORMAT = -1,
  YUV420_IYUV = 0,	// Pattern: YYYYYYYYUUVV
  YUV420_YV12 = 1,	// Pattern: YYYYYYYYVVUU
  YUV420_NV12 = 2,	// Pattern: YYYYYYYYUVUV
  YUV422_YU16 = 3,	// Pattern: YYYYYYYYUUUUVVVV
  YUV422_YV16 = 4,	// Pattern: YYYYYYYYVVVVUUUU
  YUV422_NV16 = 5,	// Pattern: YYYYYYYYUVUVUVUV
  YUV444 = 6,
  YUV_FORMAT_TOTAL_NUM,
  YUV_FORMAT_FIRST = YUV420_IYUV,
  YUV_FORMAT_LAST = YUV_FORMAT_TOTAL_NUM,
} AMBA_YUV_FORMAT;

typedef enum {
  CAPTURE_PREVIEW_BUFFER = 0x0,
  CAPTURE_PYRAMID_BUFFER = 0x1,
  CAPTURE_ME1_BUFFER = 0x2,
  CAPTURE_ME0_BUFFER = 0x3,
  CAPTURE_RAW_BUFFER = 0x4,
  CAPTURE_CUSTOM_AISP_BUFFER = 0x5,

  CAPTURE_TYPE_NUM = 0xfe,
  CAPTURE_NONE = 0xff,
} AMBA_CAPTURE_TYPE;

typedef struct {
  unsigned char * gdma_buf;
  unsigned int gdma_buf_size;
  int dma_buf_fd;
  int gdma_part_id;

  unsigned int y_addr_offset;
  unsigned int uv_addr_offset;
  unsigned int me0_addr_offset;
  unsigned int me1_addr_offset;

  /* Destination row pitch for external dma-buf GDMA (e.g. cavalry pool). Zero: use source pitch. */
  unsigned int dst_yuv_pitch;
  unsigned int dst_me0_pitch;
  unsigned int dst_me1_pitch;
} amba_gdma_buf_t;

typedef struct {
  void *virt_addr;
  unsigned long request_size;
  unsigned long phys_addr;
  unsigned long allocate_size;
  int partition_id;
} amba_iav_partition_t;

typedef struct {
  unsigned char query_canvasgrp_flag;
  unsigned char discard_cached_items;
  unsigned char query_extra_raw_info_flag;
  unsigned char capture_me0;
  unsigned char capture_me1;
  unsigned char reserved[3];

  unsigned char canvas_mf_enable[DAMBA_MAX_CANVAS_BUF_NUM_ALIGIN];
  unsigned char canvas_yuv_buffer_disable[DAMBA_MAX_CANVAS_BUF_NUM_ALIGIN];
  unsigned char canvas_me_buffer_disable[DAMBA_MAX_CANVAS_BUF_NUM_ALIGIN];
  unsigned char canvas_ext_mem_enable[DAMBA_MAX_CANVAS_BUF_NUM_ALIGIN];

  unsigned int canvas_num;
  unsigned int canvas_buffer_map;
} amba_canvas_opt_t;

typedef struct {
  unsigned int channel_id;
  unsigned int pyramid_buffer_map;

  unsigned char pyramid_manual_feed[DAMBA_MAX_CHANNEL_NUM_ALIGIN];
  unsigned char pyramid_ext_mem[DAMBA_MAX_CHANNEL_NUM_ALIGIN];

} amba_pyramid_opt_t;

typedef struct {
  unsigned int vinc_id;
  unsigned int channel_num;
  int capture_raw_ce;
} amba_raw_opt_t;

typedef struct {
  unsigned int canvas_num;
  unsigned int canvas_buffer_map;

  int is_me1;

  unsigned char canvas_me_buffer_disable[DAMBA_MAX_CANVAS_BUF_NUM_ALIGIN];
  unsigned char canvas_mf_enable[DAMBA_MAX_CANVAS_BUF_NUM_ALIGIN];

} amba_me_opt_t;

typedef struct {
  u32 width;		/*!< Width of the YUV data */
  u32 height;		/*!< Height of the YUV data */
  u32 pitch;		/*!< Pitch size of the YUV data */
  u32 seq_num;	/*!< Sequence number of the YUV data */
  u32 format;		/*!< Format of the YUV data */
  u32 dsp_pts;	/*!< Intermediate PTS from DSP */
  u64 mono_pts;	/*!< Monotonic PTS of the YUV data */
  unsigned long y_addr_offset; /*!< Address offset for frame Y data */
  unsigned long uv_addr_offset;/*!< Address offset for frame UV data */
  int yuv_dma_buf_fd;
} amba_yuv_buf_t;

typedef struct {
  unsigned long raw_addr_offset; /*!< Address offset for frame raw picture */
  unsigned long ce_addr_offset;	/*!< Contrast enhance image addr for RGB format data, chroma addr for YUV format data. */
  unsigned int width;
  unsigned int height;
  unsigned int pitch;
  u16 ce_pitch;	/*!< Contrast enhance image pitch for RGB format data, chroma pitch for YUV format data. */
  u16 ce_width;	/*!< Contrast enhance image width for RGB format data, chroma width for YUV format data. */
  unsigned int seq_num;
  unsigned int format;

  unsigned int dsp_pts;
  unsigned long mono_pts;
} amba_raw_buf_t;

typedef struct {
  u32 width;		/*!< Width of the frame ME data */
  u32 height;		/*!< Height of the frame ME data */
  u32 pitch;		/*!< Pitch size of the frame ME data */
  u32 seq_num;	/*!< Sequence number of the frame ME data */
  u32 dsp_pts;	/*!< Intermediate PTS from DSP */
  u64 mono_pts;	/*!< Monotonic PTS of the frame ME data */
  unsigned long data_addr_offset; /*!< Address offset for frame ME data */
  int me_dma_buf_fd;
} amba_me_buf_t;


typedef struct {
  unsigned char non_block_flag;
  unsigned char vca_flag;
  unsigned char check_canvas_style;
  unsigned char capture_select;

  unsigned char gdma_copy_enable;
  unsigned char decode_mode;
  unsigned char reserved[2];

  unsigned int canvas_map_thru_dmabuf;

  //yuv
  amba_yuv_buf_t yuv_ctx[DAMBA_MAX_YUV_BUF_NUM];
  amba_raw_buf_t raw_ctx[AMBA_VINC_MAX_NUM];
  amba_me_buf_t me0_ctx[DAMBA_MAX_YUV_BUF_NUM];
  amba_me_buf_t me1_ctx[DAMBA_MAX_YUV_BUF_NUM];

  //canvas
  amba_canvas_opt_t canvas_options;
  //pyramid
  amba_pyramid_opt_t pyramid_options;
  //raw
  amba_raw_opt_t raw_options;
  //me
  amba_me_opt_t me_options;

  //gdma
  amba_gdma_buf_t *gdma_ctx;
  unsigned char *dsp_base;
} amba_dsp_query_yuv_buffer_t;

typedef struct {
  unsigned char id;
  unsigned char state; // EAMDSP_ENC_STREAM_STATE_xx
  unsigned char reserved1;
  unsigned char reserved2;
} amba_dsp_enc_stream_info_t;

typedef struct {
  unsigned char id;
  unsigned char codec; /* EAMDSP_VIDEO_CODEC_TYPE_xx */
  unsigned char source_buffer; /* EAMDSP_ENC_STREAM_SOURCE_BUFFER_xx */

  /* IAV_STMCFG_FORMAT: encode window + rotation (overlay coord vs encoded picture) */
  unsigned char rotate_cw;
  unsigned short enc_win_width;
  unsigned short enc_win_height;
} amba_dsp_enc_stream_format_t;

typedef struct {
  u32 canvas_id;

  u16 width;      /*!< Canvas width */
  u16 height;     /*!< Canvas height */
  u16 yuv_pitch;  /*!< Canvas YUV pitch */
  u16 me1_pitch;  /*!< Canvas ME1 pitch */
  u16 me0_pitch;  /*!< Canvas ME0 pitch */
  u16 me0_width;  /*!< Canvas ME0 width */
  u16 me0_height; /*!< Canvas ME0 height */
  u16 me1_width;  /*!< Canvas ME1 width */
  u16 me1_height; /*!< Canvas ME1 height */
  u16 reserved;
} amba_canvas_info_t;

typedef struct {
  u16 width;  /*!< Width of pyramid layer */
  u16 height; /*!< Hgight of pyramid layer */
  u16 pitch;  /*!< Pitch of pyramid layer */
  u16 reserved1;
} amba_pyramid_layer_size_t;

typedef struct {
  u32 channel_id;

  amba_pyramid_layer_size_t layer_size[IAV_MAX_PYRAMID_LAYERS];	/*!< Size for each pyramid layer */
} amba_pyramid_info_t;


typedef enum {
  EAmbaBufferType_DSP = 0,
  EAmbaBufferType_BSB = 1,
  EAmbaBufferType_USR = 2,
  EAmbaBufferType_MV = 3,
  EAmbaBufferType_OVERLAY = 4,
  EAmbaBufferType_QPMATRIX = 5,
  EAmbaBufferType_WARP = 6,
  EAmbaBufferType_QUANT = 7,
  EAmbaBufferType_IMG = 8,
  EAmbaBufferType_PM_IDSP = 9,
  EAmbaBufferType_CMD_SYNC = 10,
  EAmbaBufferType_FB_DATA = 11,
  EAmbaBufferType_FB_AUDIO = 12,
  EAmbaBufferType_QPMATRIX_RAW = 13,
  EAmbaBufferType_INTRA_PB = 14,
  EAmbaBufferType_SBP = 15,
  EAmbaBufferType_MULTI_CHAN = 16,
} EAmbaBufferType;

typedef struct {
  unsigned int src_offset;
  unsigned int dst_offset;
  unsigned int src_pitch;
  unsigned int dst_pitch;
  unsigned int width;
  unsigned int height;
  unsigned short src_use_dma_buf_fd;
  unsigned short dst_use_dma_buf_fd;
  int src_dma_buf_fd;
  int dst_dma_buf_fd;;
} amba_gdma_copy_t;

typedef struct {
  const char *mode_string;
  const char *sink_type_string;
  const char *device_string;
  unsigned char vout_id;
  unsigned char b_config_mixer;
  unsigned char mixer_flag;
  unsigned char b_direct_2_dsp;
} amba_vout_config_t;

typedef struct {
  amba_vout_config_t vout_config[DAMBADSP_MAX_VOUT_NUMBER];
  unsigned char vout_number;
  unsigned char reserved0;
  unsigned char reserved1;
  unsigned char reserved2;
} amba_vout_configs_t;


enum {
  EAMDSP_VOUT_TYPE_INVALID = 0x00,
  EAMDSP_VOUT_TYPE_DIGITAL = 0x01,
  EAMDSP_VOUT_TYPE_HDMI = 0x02,
  EAMDSP_VOUT_TYPE_CVBS = 0x03,
};

//dsp related
enum {
  EAMDSP_MODE_INVALID = 0x00,
  EAMDSP_MODE_INIT = 0x01,
  EAMDSP_MODE_IDLE = 0x02,
  EAMDSP_MODE_PREVIEW = 0x03,
  EAMDSP_MODE_ENCODE = 0x04,
  EAMDSP_MODE_DECODE = 0x05,
};

typedef struct {
    unsigned short enable : 1;
    unsigned short reserved1 : 15;
    unsigned short start_x;
    unsigned short start_y;
    unsigned short width;
    unsigned short pitch;
    unsigned short height;
    unsigned int total_size;
    unsigned long clut_addr_offset;
    unsigned long data_addr_offset;
} overlay_area_t;

typedef struct {
  rect_t input;
  rect_t output;

  unsigned int input_buf_pid;
  unsigned int input_buf_picth;
  unsigned int input_buf_height;

  unsigned int output_buf_pid;
  unsigned int output_buf_picth;
  unsigned int output_buf_height;
} img_scale_cfg_t;

typedef enum {
  IAV_FLUSH_FORCE_IDR_DISABLE = 0,
  IAV_FLUSH_FORCE_IDR_ENABLE,
  IAV_FLUSH_FORCE_IDR_WITH_PTS,
} iav_flush_frame_type;

typedef struct {
  int enable;
  u16 width;
  u16 height;
  u16 x;
  u16 y;
  u8 strength;
  u8 i_mode;
  u8 is_blocky;
  u8 coeff;
  u8 color_idx;
  u8 color_enable : 1;
  u8 reserved0 : 7;
  u8 reserved1[2];
} blur_info_t;

typedef struct {
  int blur_enable;
  unsigned char blur_rotate;
  int blur_win_width;
  int blur_win_height;
  int max_blur_area_num;
  int blur_type;
  unsigned char sync_with_pts;
  blur_info_t blurs[MAX_BLUR_AREA_NUM];
} blur_stream_info_t;

typedef struct {
  u16 max_width;
  u16 max_height;
  u8 use_arb_blur : 1;
  u8 reserved : 7;
  u8 buf_num;
  u16 reserved2;
} iav_blur_area_mem_cfg_t;

typedef struct {
  u32 max_area_num[IAV_STREAM_MAX_NUM_ALL];
  iav_blur_area_mem_cfg_t area[IAV_STREAM_MAX_NUM_ALL][MAX_NUM_BLUR_AREA];
  u32 type : 2;
  u32 reserved : 30;
} iav_blur_mem_cfg_t;

typedef struct {
  u8 enable : 1;
  u8 use_arb_blur : 1;
  u8 strength : 2;
  u8 is_blocky : 1;
  u8 coeff : 2;
  u8 color_enable : 1;
  u8 color_idx;
  u8 h_factor;
  u8 v_factor;
  u16 max_width;
  u16 max_height;
  u16 act_width;
  u16 act_height;
  u16 start_x;
  u16 start_y;
} iav_blur_area_cfg_t;

typedef struct {
  u8 stream_id;
  u8 enable : 1;
  u8 type : 2;
  u8 reserved : 5;
  u8 max_area_num;
  u8 act_area_num;
  iav_blur_area_cfg_t area[MAX_NUM_BLUR_AREA];
} iav_blur_stream_cfg_t;

typedef struct {
  u32 color_idx_map;
  u8 U[MAX_NUM_BLUR_COLOR];
  u8 V[MAX_NUM_BLUR_COLOR];
  u32 reserved[3];
} iav_blur_color_cfg_t;

typedef struct {
  u8 stream_id;
  u8 area_id;
  u16 pitch;
  u32 size;
  u8 *addr;
} iav_blur_area_buf_t;

typedef struct {
  union {
    u32 stream_map;
    u32 canvas_map;
  };
  u32 frame_sync : 1;
  u32 reserved : 31;
} iav_blur_apply_cfg_t;

typedef int (*TFDSPGetDSPMode) (int iav_fd, amba_dsp_mode_t *mode);

typedef int (*TFDSPEnterDecodeMode) (int iav_fd, amba_dsp_decode_mode_config_t *mode_config);
typedef int (*TFDSPLeaveDecodeMode) (int iav_fd);
typedef int (*TFDSPCreateDecoder) (int iav_fd, amba_dsp_decoder_info_t *p_decoder_info);
typedef int (*TFDSPDestroyDecoder) (int iav_fd, unsigned char decoder_id);
typedef int (*TFDSPQueryDecodeConfig) (int iav_fd, amba_dsp_query_decode_config_t *config);

typedef int (*TFDSPDecodeTrickPlay) (int iav_fd, unsigned char decoder_id, unsigned char trick_play);
typedef int (*TFDSPDecodeStart) (int iav_fd, unsigned char decoder_id);
typedef int (*TFDSPDecodeStop) (int iav_fd, unsigned char decoder_id, unsigned char stop_flag);
typedef int (*TFDSPDecodeSpeed) (int iav_fd, unsigned char decoder_id, unsigned short speed, unsigned char scan_mode, unsigned char direction);
typedef int (*TFDSPDecodeRequestBitsFifo) (int iav_fd, int decoder_id, unsigned int size, void *cur_pos_offset);

typedef int (*TFDSPDecode) (int iav_fd, amba_dsp_decode_t *dec);

typedef int (*TFDSPDecodeQueryBSBAndPrint) (int iav_fd, unsigned char decoder_id);
typedef int (*TFDSPDecodeQueryStatusAndPrint) (int iav_fd, unsigned char decoder_id);
typedef int (*TFDSPDecodeQueryBSB) (int iav_fd, amba_dsp_bsb_status_t *status);
typedef int (*TFDSPDecodeQueryStatus) (int iav_fd, amba_dsp_decode_status_t *status);

typedef int (*TFDSPDecodeWaitVoutDormant) (int iav_fd, unsigned char decoder_id);
typedef int (*TFDSPDecodeWakeVout) (int iav_fd, unsigned char decoder_id);
typedef int (*TFDSPDecodeWaitEOSFlag) (int iav_fd, amba_dsp_decode_eos_timestamp_t *eos_timestamp);
typedef int (*TFDSPDecodeWaitEOS) (int iav_fd, amba_dsp_decode_eos_timestamp_t *eos_timestamp);

typedef int (*TFDSPConfigureVout) (int iav_fd, amba_vout_config_t *vout_config);

typedef int (*TFDSPGetVoutInfo) (int iav_fd, int index, int type, amba_dsp_vout_info_t *voutinfo);
typedef int (*TFDSPGetVinInfo) (int iav_fd, amba_dsp_vin_info_t *vintinfo);

typedef int (*TFDSPGetStreamFrameFactor) (int iav_fd, int index, amba_dsp_stream_framefactor_t *framefactor);

typedef int (*TFDSPMapBSB) (int iav_fd, iav_map_bsb_t *map_bsb);
typedef int (*TFDSPMapDSP) (int iav_fd, iav_map_dsp_t *map_dsp);
typedef int (*TFDSPMapOverlay) (int iav_fd, iav_map_overlay_t *map_overlay);

typedef int (*TFDSPUnmapBSB) (int iav_fd, iav_map_bsb_t *map_bsb);
typedef int (*TFDSPUnmapDSP) (int iav_fd, iav_map_dsp_t *map_dsp);
typedef int (*TFDSPUnmapOverlay) (int iav_fd, iav_map_overlay_t *map_overlay);

typedef int (*TFDSPFlushFrameDesc) (int iav_fd, unsigned int stream_id, unsigned int force_idr_type, u64 mono_pts);
typedef int (*TFDSPReadBitstream) (int iav_fd, amba_dsp_read_bitstream_t *read_bitstream);
typedef void (*TFDSPReleaseBitstream) (int iav_fd, amba_dsp_release_bitstream_t *release_bitstream);
typedef int (*TFDSPIsReadyForReadBitstream) (int iav_fd);

typedef int (*TFDSPEncodeStart) (int iav_fd, unsigned int mask);
typedef int (*TFDSPEncodeStop) (int iav_fd, unsigned int mask);

typedef int (*TFDSPQueryEncodeStreamInfo) (int iav_fd, amba_dsp_enc_stream_info_t *info);
typedef int (*TFDSPQueryEncodeStreamFormat) (int iav_fd, amba_dsp_enc_stream_format_t *fmt);

typedef int (*TFDSPQuerySourceBufferInfo) (int iav_fd, amba_dsp_source_buffer_info_t *info);
typedef int (*TFDSPQueryYUVBuffer) (int iav_fd, amba_dsp_query_yuv_buffer_t *yuv_buffer);
typedef int (*TFDSPReleaseCanvasBuffer) (int iav_fd, iav_release_canvas_cfg_t *ctx);

typedef int (*TFDSPQueryCanvasInfo)(int iav_fd, amba_canvas_info_t *info);
typedef int (*TFDSPQueryPyramidInfo)(int iav_fd, amba_pyramid_info_t *info);

typedef int (*TFDSPGDMACopy) (int iav_fd, amba_gdma_copy_t *copy);
typedef int (*TFDSPGDMAAlloc) (int iav_fd, unsigned int size, amba_gdma_buf_t *ctx, unsigned char is_dma_buf);
typedef int (*TFDSPGDMAFree) (int iav_fd, amba_gdma_buf_t *ctx);

typedef int (*TFDSPEnterIdleMode) (int iav_fd, int vin_off, int no_vout_reset);
typedef int (*TFDSPEnablePreview) (int iav_fd, int no_vout_reset);

typedef int (*tf_set_overlay)(int iav_fd, iav_set_overlay_t *overlay_set);
typedef int (*tf_set_frame_sync)(int iav_fd, iav_set_overlay_t *overlay_set);
typedef int (*tf_apply_frame_sync)(int iav_fd, unsigned int dsp_pts,
    unsigned int stream_updated_map, unsigned int force_update);

typedef int (*TFDSPCheckIAVState)(int iav_fd, unsigned char *decode_mode);
typedef int (*TFDSPGetIAVState)(int iav_fd);
typedef int (*TFDSPGetResourceInfo)(int iav_fd, amba_resource_info_t *info);
/** IAV_IOC_GET_STREAM_RESOURCE: returns overlay_pixel_format, or -1 on error */
typedef int (*TFDSPGetStreamOverlayPixelFormat)(int iav_fd, int stream_id);
typedef unsigned int (*TFDSPGetEncSrcCanvasId)(int iav_fd, unsigned int stream_id);
typedef unsigned int (*TFDSPGetEncDummyLatency)(int iav_fd, unsigned int canvas_id);

typedef int (*TFDSPGetStreamState)(int iav_fd, unsigned int stream_id);
typedef int (*TFDSPSetImgScale)(int iav_fd, img_scale_cfg_t *img_scale_cfg);

// efr related
typedef int (*TFDSPEfrMemAllocMap) (int iav_fd, amba_iav_partition_t *iav_partition);
typedef int (*TFDSPEfrMemUnmap) (int iav_fd, amba_iav_partition_t *iav_partition);
typedef int (*TFDSPSetEfrSetUp)(int iav_fd, amba_efr_setup_t *efr_setup);
typedef int (*TFDSPGetEfrSetUp)(int iav_fd, amba_efr_setup_t *efr_setup);
typedef int (*TFDSPWaitEfr)(int iav_fd, int vinc_id);
#if defined (BUILD_DSP_AMBA_V6)
typedef int (*TFDSPSetEfrCfg)(int iav_fd, amba_efr_cfg_t *efr_cfg);
#endif

typedef int (*TFDSPEFMLibInit)(iav_efm_usr_cfg_t *cfg);
typedef int (*TFDSPEFMLibDeinit)(void);
typedef int (*TFDSPEFMGetStreamCfg)(iav_efm_stream_cfg_t *stream_cfg);
typedef int (*TFDSPEFMGetBuf)(iav_efm_buf_info_t *buf_info);
typedef int (*TFDSPEFMFeedBuf)(iav_efm_buf_info_t *buf_info, iav_efm_feed_cfg_t *feed_cfg);

typedef int (*TFDSPBLURLibInit)(int iav_fd);
typedef int (*TFDSPBLURLibDeinit)(void);
typedef int (*TFDSPBLURSETCOLOR)(iav_blur_color_cfg_t *color_cfg);
typedef int (*TFDSPBLURGETCOLOR)(iav_blur_color_cfg_t *color_cfg);
typedef int (*TFDSPBLURGETAREABUF)(iav_blur_area_buf_t *buf_info);
typedef int (*TFDSPBLURPUTAREABUF)(iav_blur_area_buf_t *buf_info);
typedef int (*TFDSPBLURSETSTREAMCFG)(iav_blur_stream_cfg_t *stream_cfg);
typedef int (*TFDSPBLURGETSTREAMCFG)(iav_blur_stream_cfg_t *stream_cfg);
typedef int (*TFDSPBLURSETMEMCFG)(iav_blur_mem_cfg_t *cfg);
typedef int (*TFDSPBLURGETMEMCFG)(iav_blur_mem_cfg_t *cfg);
typedef int (*TFDSPBLURAPPLY)(iav_blur_apply_cfg_t *cfg);

typedef struct {
  TFDSPGetDSPMode f_get_dsp_mode;

  TFDSPEnterDecodeMode f_enter_mode;
  TFDSPLeaveDecodeMode f_leave_mode;
  TFDSPCreateDecoder f_create_decoder;
  TFDSPDestroyDecoder f_destroy_decoder;
  TFDSPQueryDecodeConfig f_query_decode_config;

  TFDSPDecodeTrickPlay f_trickplay;
  TFDSPDecodeStart f_start;
  TFDSPDecodeStop f_stop;
  TFDSPDecodeSpeed f_speed;
  TFDSPDecodeRequestBitsFifo f_request_bsb;

  TFDSPDecode f_decode;

  TFDSPDecodeQueryBSBAndPrint f_query_print_decode_bsb_status;
  TFDSPDecodeQueryStatusAndPrint f_query_print_decode_status;
  TFDSPDecodeQueryBSB f_query_decode_bsb_status;
  TFDSPDecodeQueryStatus f_query_decode_status;

  TFDSPDecodeWaitVoutDormant f_decode_wait_vout_dormant;
  TFDSPDecodeWakeVout f_decode_wake_vout;
  TFDSPDecodeWaitEOSFlag f_decode_wait_eos_flag;
  TFDSPDecodeWaitEOS f_decode_wait_eos;

  TFDSPConfigureVout f_configure_vout;

  TFDSPGetVoutInfo f_get_vout_info;
  TFDSPGetVinInfo f_get_vin_info;

  TFDSPGetStreamFrameFactor f_get_stream_framefactor;

  TFDSPMapBSB f_map_bsb;
  TFDSPMapDSP f_map_dsp;
  TFDSPMapOverlay f_map_overlay;
  TFDSPMapBSB f_map_dec_bsb;

  TFDSPUnmapBSB f_unmap_bsb;
  TFDSPUnmapDSP f_unmap_dsp;
  TFDSPUnmapOverlay f_unmap_overlay;
  TFDSPUnmapBSB f_unmap_dec_bsb;

  TFDSPFlushFrameDesc f_flush_frame_desc;
  TFDSPReadBitstream f_read_bitstream;
  TFDSPReleaseBitstream f_release_bitstream;
  TFDSPIsReadyForReadBitstream f_is_ready_for_read_bitstream;

  TFDSPEncodeStart f_encode_start;
  TFDSPEncodeStop f_encode_stop;

  TFDSPQueryEncodeStreamInfo f_query_encode_stream_info;
  TFDSPQueryEncodeStreamFormat f_query_encode_stream_fmt;

  TFDSPQuerySourceBufferInfo f_query_source_buffer_info;
  TFDSPQueryYUVBuffer f_query_yuv_buffer;
  TFDSPReleaseCanvasBuffer f_release_canvas_buffer;

  TFDSPQueryCanvasInfo f_query_canvas_info;
  TFDSPQueryPyramidInfo f_query_pyramid_info;

  TFDSPGDMACopy f_gdma_copy;
  TFDSPGDMAAlloc f_gdma_alloc_buf;
  TFDSPGDMAFree f_gdma_free_buf;

  TFDSPEnterIdleMode f_enter_idle_mode;
  TFDSPEnablePreview f_enable_preview;

  // overlay related
  tf_set_overlay f_set_overlay;
  tf_set_frame_sync f_set_frame_sync;
  tf_apply_frame_sync f_apply_frame_sync;

  // encoding related
  tf_update_enc_resolution f_update_enc_resolution;
  tf_update_enc_bitrate f_update_enc_bitrate;
  tf_update_enc_frameate f_update_enc_framerate;
  tf_update_enc_bitrate_frameate f_update_enc_bitrate_frameate;
  tf_update_enc_codec_type f_update_enc_codec_type;
  tf_update_enc_gop_structure f_update_enc_gop_structure;
  tf_enc_force_idr f_enc_force_idr;

  TFDSPCheckIAVState f_check_iav_state;
  TFDSPGetIAVState f_get_iav_state;
  TFDSPGetResourceInfo f_get_resource_info;
  TFDSPGetStreamOverlayPixelFormat f_get_stream_overlay_pixel_format;
  TFDSPGetEncSrcCanvasId f_get_enc_src_canvas_id;
  TFDSPGetEncDummyLatency f_get_enc_dummy_latency;
  TFDSPGetStreamState f_get_stream_state;
  TFDSPSetImgScale f_set_img_scale;

  // efr related
  TFDSPEfrMemAllocMap f_alloc_map_efr_mem;
  TFDSPEfrMemUnmap f_unmap_efr_mem;
  TFDSPGetEfrSetUp f_get_efr_setup;
  TFDSPSetEfrSetUp f_set_efr_setup;
  TFDSPWaitEfr f_wait_efr_done;
#if defined (BUILD_DSP_AMBA_V6)
  TFDSPSetEfrCfg f_set_efr_cfg;
#endif

  //efm related
  TFDSPEFMLibInit f_efm_lib_init;
  TFDSPEFMLibDeinit f_efm_lib_deinit;
  TFDSPEFMGetStreamCfg f_efm_get_stream_cfg;
  TFDSPEFMGetBuf f_efm_get_buf;
  TFDSPEFMFeedBuf f_efm_feed_buf;

  //blur related
  TFDSPBLURLibInit f_blur_lib_init;
  TFDSPBLURLibDeinit f_blur_lib_deinit;
  TFDSPBLURSETCOLOR f_blur_set_color;
  TFDSPBLURGETCOLOR f_blur_get_color;
  TFDSPBLURGETAREABUF f_blur_get_area_buf;
  TFDSPBLURPUTAREABUF f_blur_put_area_buf;
  TFDSPBLURSETSTREAMCFG f_blur_set_stream_cfg;
  TFDSPBLURGETSTREAMCFG f_blur_get_stream_cfg;
  TFDSPBLURSETMEMCFG f_blur_set_mem_cfg;
  TFDSPBLURGETMEMCFG f_blur_get_mem_cfg;
  TFDSPBLURAPPLY f_blur_apply;
} iav_al_t;

extern int open_iav_handle ();
extern void close_iav_handle (int fd);

extern void initialize_iav_al (iav_al_t *al);

const char *get_dsp_platform_name();

int config_amba_vout (int fd, iav_al_t *al, amba_vout_config_t *vouts);

int halt_amba_vout(int fd, int vout_number);


void fill_amba_h264_gop_header (unsigned char *p_gop_header,
  unsigned int frame_tick, unsigned int time_scale,
  unsigned int pts, unsigned char gopsize, unsigned char m);
void update_amba_h264_gop_header (unsigned char *p_gop_header,
  unsigned int pts, unsigned char gopsize);

void fill_amba_h265_gop_header (unsigned char *p_gop_header,
  unsigned int frame_tick, unsigned int time_scale,
  unsigned int pts, unsigned char gopsize, unsigned char m);
void update_amba_h265_gop_header (unsigned char *p_gop_header,
  unsigned int pts, unsigned char gopsize);

#endif

