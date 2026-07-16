/*
 * iav_al_enc_params.h
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

#ifndef __IAV_AL_ENC_PARAMS_H__
#define __IAV_AL_ENC_PARAMS_H__

#ifndef MIN_ABS_FPS
#define MIN_ABS_FPS 1
#endif
#ifndef MAX_ABS_FPS
#define MAX_ABS_FPS 60
#endif
#ifndef MAX_TRIGGER_FRAME_NUM
#define MAX_TRIGGER_FRAME_NUM 255
#endif

#ifndef DMAX_FILE_NAME_LEN
#define DMAX_FILE_NAME_LEN 512
#endif

#define DROUND_ALIGN(_size, _align)    (((_size) + ((_align) - 1)) & ~((_align) - 1))

#define DAMBA_MAX_CHANNEL_NUM_ALIGIN DROUND_ALIGN(IAV_MAX_CHANNEL_NUM, 32)
#define DAMBA_MAX_CANVAS_BUF_NUM_ALIGIN DROUND_ALIGN(IAV_MAX_CANVAS_BUF_NUM, 32)
#define DAMBA_MAX_STREAM_NUM_ALIGIN DROUND_ALIGN(IAV_STREAM_MAX_NUM_ALL, 32)

typedef struct {
  unsigned int enc_index;

  unsigned int off_x, off_y;
  unsigned int size_x, size_y;
} enc_resolution_t;

typedef struct {
  unsigned int enc_index;

  unsigned int bitrate;
} enc_bitrate_t;

typedef struct {
  unsigned int enc_index;

  unsigned int framerate_i;
  float framerate_f;

  unsigned int framerate_num;
  unsigned int framerate_den;
} enc_framerate_t;

typedef struct {
  unsigned int enc_index;

  enc_bitrate_t bitrate;
  enc_framerate_t framerate;
} enc_bitrate_framerate_t;

typedef struct {
  unsigned int enc_index;

  unsigned int codec_type;
} enc_codec_type_t;

typedef struct {
  unsigned int enc_index;

  unsigned int idr_interval;
  unsigned int m, n;
} enc_gop_structure_t;

typedef struct {
  unsigned int enc_index;
  unsigned int pts;
  int stream_type;
} enc_force_idr_t;

typedef struct {
  int type;
  int type_changed_flag;

  int width;
  int height;
  int resolution_changed_flag;

  int offset_x;
  int offset_y;
  int offset_changed_flag;

  int source;
  int source_changed_flag;

  int source_map;
  int source_map_changed_flag;

  int hflip;
  int hflip_flag;

  int vflip;
  int vflip_flag;

  int rotate;
  int rotate_flag;

  u16 duration;
  u16 duration_flag;

  int efm_customize_fps;
  int efm_customize_fps_flag;

  u32 session_id;
  u32 session_id_flag;

  u32 fake_avg_pts;
  u32 fake_avg_pts_flag;
} enc_format_t;

// h.264 config
typedef struct h264_param_s {
  int h264_M;
  int h264_N;
  int h264_N_update_mode;
  int h264_idr_interval;
  int h264_gop_model;
  int h264_bitrate_control;
  int h264_cbr_avg_bitrate;
  int h264_vbr_min_bitrate;
  int h264_vbr_max_bitrate;
  int h264_cbr_stable_br_adjust;

  int h264_deblocking_filter_alpha;
  int h264_deblocking_filter_beta;
  int h264_deblocking_filter_enable;

  int h264_chroma_format;			// 0: YUV420; 1: Mono
  int h264_chroma_format_flag;

  int h264_M_flag;
  int h264_N_flag;
  int h264_N_update_mode_flag;
  int h264_idr_interval_flag;
  int h264_gop_model_flag;
  int h264_bitrate_control_flag;
  int h264_cbr_bitrate_flag;
  int h264_vbr_bitrate_flag;
  int h264_cbr_stable_br_adjust_flag;

  int h264_deblocking_filter_alpha_flag;
  int h264_deblocking_filter_beta_flag;
  int h264_deblocking_filter_enable_flag;

  int h264_profile_level;
  int h264_profile_level_flag;

  int intra_refresh_cycle;
  int intra_refresh_cycle_flag;

  int h264_zmv_threshold_enable;
  int h264_zmv_threshold_enable_flag;

  int h264_zmv_threshold_qp_offset;
  int h264_zmv_threshold_qp_offset_flag;

  int h264_fast_seek_intvl;
  int h264_fast_seek_intvl_flag;

  int h264_frame_drop_repeat_enable;
  int h264_drop_frames;
  int h264_drop_frames_flag;

  int h264_user1_intra_bias;
  int h264_user1_intra_bias_flag;
  int h264_user1_direct_bias;
  int h264_user1_direct_bias_flag;
  int h264_user2_intra_bias;
  int h264_user2_intra_bias_flag;
  int h264_user2_direct_bias;
  int h264_user2_direct_bias_flag;

  int h264_long_start_code;
  int h264_long_start_code_flag;

  int au_type;
  int au_type_flag;

  u8 cpb_buf_idc;
  u8 cpb_cmp_idc;
  u8 en_panic_rc;
  u8 fast_rc_idc;
  u32 cpb_user_size;
  int panic_mode_flag;

  int h264_abs_br;
  int h264_abs_br_flag;

  int h264_slice_num;
  int h264_slice_num_flag;
  int h264_slices_per_info;
  int h264_slices_per_info_flag;

  int md_cat_lut[MD_CAT_MAX_NUM];
  int md_cat_lut_flag;

  int pskip_repeat_enable;
  int pskip_repeat_num;
  int pskip_repeat_mode;
  int pskip_flag;

  int sar_width;
  int sar_height;
  int sar_flag;

  int h264_ltrs_type;
  int h264_ltrs_type_flag;

  int h264_log2_num_ltrp_per_gop;
  int h264_log2_num_ltrp_per_gop_flag;

  int h264_two_ltrs_mode;
  int h264_two_ltrs_mode_flag;

  int h264_two_str;
  int h264_two_str_flag;

  int h264_aqp_type;
  int h264_aqp_type_flag;

  int stream_dummy_latency;
  int stream_dummy_latency_flag;

  int wp_mode;
  int wp_mode_flag;

  int h264_skip_strength;
  int h264_skip_strength_flag;

  int h264_disable_cu8;
  int h264_disable_cu8_flag;

  int h264_disable_cu16;
  int h264_disable_cu16_flag;

  int h264_cu8_bias_level;
  int h264_cu8_bias_level_flag;

  int h264_cu16_bias_level;
  int h264_cu16_bias_level_flag;

  int h264_cu32_bias_level;
  int h264_cu32_bias_level_flag;

  int h264_chroma_qp_offset_flag;
  int h264_chroma_qp_offset;

  int h264_one_frm_qp_offset_flag;
  int h264_one_frm_qp_offset;

  int h264_qp_smooth_enable;
  int h264_qp_smooth_enable_flag;

  int h264_svc_extension_enable;
  int h264_svc_extension_enable_flag;
} h264_param_t;

typedef struct jpeg_param_s{
  int quality;
  int quality_changed_flag;

  int jpeg_chroma_format;// 0: Mono, 1: YUV420; 2: YUV422
  int jpeg_chroma_format_flag;

  int jpeg_frame_drop_repeat_enable;
  int jpeg_drop_frames_flag;
  int jpeg_drop_frames;

  int restart_interval;
  int restart_interval_flag;

  u32 jpeg_slice_num;
  u32 jpeg_slice_num_flag;
} jpeg_param_t;

typedef struct encode_param_s {
  h264_param_t h264_param;
  jpeg_param_t jpeg_param;
} enc_param_t;

//source buffer format
typedef struct source_buffer_format_s {
  int input_width;
  int input_height;
  int input_size_changed_flag;
  int input_size_interactive_change_flag;

  int input_x;
  int input_y;
  int input_offset_changed_flag;
  int input_offset_interactive_change_flag;

  int output_width;
  int output_height;
  int output_size_changed_flag;
  int output_size_interactive_change_flag;

  int output_x;
  int output_y;
  int output_offset_changed_flag;
  int output_offset_interactive_change_flag;
} source_buffer_format_t;

typedef struct multi_chan_dptz_s {
  unsigned int order;
  unsigned int order_changed_flag;

  source_buffer_format_t source_buffer_format[IAV_MAX_PASS_NUM][IAV_SRCBUF_LAST_PMN];
} multi_chan_dptz_t;

typedef struct {
  unsigned int channel_num;
  unsigned int canvas_num;
  unsigned int max_stream_num;

  unsigned int scale_pass_num[IAV_MAX_CHANNEL_NUM];
  unsigned char vcap_mode_flag[DAMBA_MAX_CHANNEL_NUM_ALIGIN];
  unsigned char pyramid_manual_feed[DAMBA_MAX_CHANNEL_NUM_ALIGIN];
  unsigned char pyramid_ext_mem[DAMBA_MAX_CHANNEL_NUM_ALIGIN];
  unsigned int pyramid_fps[DAMBA_MAX_CHANNEL_NUM_ALIGIN];
  unsigned int pyramid_width[IAV_MAX_CHANNEL_NUM][IAV_MAX_PYRAMID_LAYERS];
  unsigned int pyramid_height[IAV_MAX_CHANNEL_NUM][IAV_MAX_PYRAMID_LAYERS];

  unsigned int canvas_fps[IAV_MAX_CANVAS_BUF_NUM];
  unsigned int pts_intval[IAV_MAX_CANVAS_BUF_NUM];
  unsigned char canvas_mf_enable[DAMBA_MAX_CANVAS_BUF_NUM_ALIGIN];
  unsigned char canvas_yuv_buffer_disable[DAMBA_MAX_CANVAS_BUF_NUM_ALIGIN];
  unsigned char canvas_me_buffer_disable[DAMBA_MAX_CANVAS_BUF_NUM_ALIGIN];
  unsigned char canvas_ext_mem_enable[DAMBA_MAX_CANVAS_BUF_NUM_ALIGIN];
  unsigned char canvas_enc_dummy_latency[DAMBA_MAX_CANVAS_BUF_NUM_ALIGIN];

  unsigned int canvas_width[IAV_MAX_CANVAS_BUF_NUM];
  unsigned int canvas_height[IAV_MAX_CANVAS_BUF_NUM];
  unsigned int back_pressure_enable[IAV_MAX_CANVAS_BUF_NUM];

  unsigned char encode_mode;
  unsigned char skip_chan_check_in_blend_case;
  unsigned char enable_hp_fps;
  unsigned char img_scale_enable;
  unsigned int img_scale_max_input_width;
  unsigned int img_scale_max_input_height;
  unsigned int img_scale_max_output_width;
  unsigned int img_scale_max_output_height;

  u8 vsrc_id[IAV_MAX_CHANNEL_NUM];

  u32 enc_raw_rgb : 1; /*!< This is a flag to enable / disable encoding from raw CFA data */
  u32 enc_raw_yuv : 1; /*!< This is a flag to enable / disable encoding from raw YUV422 data */
  u32 enc_raw_nv12 : 1; /*!< This is a flag to enable / disable encoding from raw YUV420 data */
  u32 reserved0: 29;
} amba_resource_info_t;

typedef struct {
  enc_format_t encode_fmt[IAV_STREAM_MAX_NUM_ALL];
  u32  encode_format_changed_id;
  enc_param_t encode_param[IAV_STREAM_MAX_NUM_ALL];
  u32 encode_param_changed_id;
  //stream and source buffer identifier
  int current_stream;     // -1 is a invalid stream, for initialize data only
  //encode start/stop/format control variables
  u32 start_stream_id;
  u32 stop_stream_id;
  u32 abort_stream_id;
  u32 restart_stream_id;

  u32 force_idr_id;

  u32 force_fast_seek_id;

  u32 trigger_frame_stream_id;
  u8 trigger_frame_enable[DAMBA_MAX_STREAM_NUM_ALIGIN];
  u8 trigger_frame_enable_flag[DAMBA_MAX_STREAM_NUM_ALIGIN];
  u8 trigger_frame_repeat[DAMBA_MAX_STREAM_NUM_ALIGIN];
  u8 trigger_frame_repeat_flag[DAMBA_MAX_STREAM_NUM_ALIGIN];
  u8 trigger_frame_num[DAMBA_MAX_STREAM_NUM_ALIGIN];
  u8 trigger_frame_num_flag[DAMBA_MAX_STREAM_NUM_ALIGIN];

  //encoding frame rate settings
  u32 framerate_factor_changed_id;
  u32 framerate_factor_sync_id;
  int framerate_factor[IAV_STREAM_MAX_NUM_ALL][2];
  //only for getting stream encoding fps
  int stream_fps[IAV_STREAM_MAX_NUM_ALL];

  u32 frame_rate_update_mode_changed_id;
  int frame_rate_update_mode[IAV_STREAM_MAX_NUM_ALL];

  int stream_abs_fps_enable[IAV_STREAM_MAX_NUM_ALL];
  int stream_abs_fps_enabled_id;
  int stream_abs_fps[IAV_STREAM_MAX_NUM_ALL];
  int stream_abs_fps_changed_id;

  //rate control settings
  u32 qp_limit_changed_id;

  u32 intra_mb_rows_changed_id;

  //vin
  int source; //not specify source, vsrc_id
  int vin_framerate_changed_id;
  int vin_framerate[VIN_CONTROLLER_NUM];

  //multi channel
  int current_channel;
  int current_buffer;
  int current_canvas;
  int current_pass;

  u32 channel_id;
  u32 buffer_id;
  u32 canvas_id;
  int canvas_fps_map;

  int multi_chan_cfg_changed_flag;
  //int multi_blend_dptz_flag;
  int multi_chan_lua_flag;

  multi_chan_dptz_t multi_chan_dptz[CONFIG_AMBARELLA_MAX_CHANNEL_NUM];
  //multi_blend_dptz_t multi_blend_dptz[IAV_MAX_CANVAS_BUF_NUM];
  char multi_chan_cfg_file_name[DMAX_FILE_NAME_LEN];
  //int canvas_fps[IAV_MAX_CANVAS_BUF_NUM];
  u8 zero_fps_flag[DAMBA_MAX_CANVAS_BUF_NUM_ALIGIN];

  //resource info
  amba_resource_info_t res_info;
} enc_config_t;

#if defined (BUILD_DSP_AMBA_V5)
typedef struct {
  u32 vinc_id;		/*!< VIN Control ID */
  u32 raw_frame_size;	/*!< RAW data size */
  u32 pitch : 16;		/*!< RAW data pitch */
  u32 raw_hdec_dpitch : 16; /*!< HDEC data pitch */
  u32 raw_frame_num : 8;	/*!< RAW frame number */
  u32 use_ext_buf : 1;	/*!< External buf used for raw enc */
  u32 raw_format : 2;	/*!< RGB RAW format */
  u32 hdec_raw_format : 2;	/*!< RGB HDEC RAW (CE) format */
  u32 buf_idx : 6;
  u32 buf_num : 6;
  u32 reserved : 7;
  u32 frame_pts;		/*!< Frame Software PTS in 90KHz */
  u32 frame_hw_pts;	/*!< Frame Hardware PTS in 12.288MHz */
  unsigned long raw_daddr_offset;	/*!< RAW data DRAM offset */
  unsigned long raw_hdec_daddr_offset; /*!< HDEC data DRAM offset */
  unsigned long ext_buf_addr;	 /*!< External buf start address used for raw enc */
  unsigned long uv_daddr_offset;	/*!< UV data DRAM offset. Only for YUV422/YUV420 EFR.
    1. When doing YUV422/YUV420 EFR, it is highly suggested to put Y and UV buffer together(UV right after Y).
    If so, uv_daddr_offset can be set as 0.
    2. To support feeding canvas buffer reported from one channel into DSP as EFR channel again, users need
    to specify the DRAM address of its UV buffer explicitly as there are fixed padding between Y and UV buffer. */
} amba_efr_setup_t;
#elif defined (BUILD_DSP_AMBA_V6)
typedef struct {
  u64 frame_pts;
  u32 vinc_id : 8;  /* Input Params */
  u32 buf_idx : 6;
  u32 mem_init_needed : 1;
  u32 buf_num : 6;
  u32 reserved0 : 11;
  u32 reserved1;
} amba_efr_setup_t;

typedef struct {
  u32 vinc_id : 8;  /* Input Params */
  u32 buf_idx : 8;
  u32 mem_init_needed : 1;
  u32 buf_num : 6;
  u32 reserved0 : 9;
  unsigned long raw_buf_addr;	/*!< RAW data DRAM addr */
  unsigned long raw_hdec_buf_addr;	/*!< HDEC data DRAM addr */
  unsigned long raw_low_hdec_buf_addr;	/*!< Low HDEC data DRAM addr, only valid when enable_raw_low_hdec = 1 */
  u16 raw_pitch;	/*!< RAW data pitch */
  u16 raw_width;	/*!< RAW data width */
  u16 raw_height;	/*!< RAW data height */
  u16 raw_hdec_pitch;	/*!< HDEC data pitch */
  u16 raw_hdec_width;	/*!< HDEC data width */
  u16 raw_hdec_height;	/*!< HDEC data height */
  u16 raw_low_hdec_pitch;	/*!< Low HDEC data pitch, only valid when enable_raw_low_hdec = 1 */
  u16 raw_low_hdec_width;	/*!< Low HDEC data width, same as raw_hdec_width, only valid when enable_raw_low_hdec = 1 */
  u16 raw_low_hdec_height;	/*!< Low HDEC data height, 1/4 of raw_hdec_height, only valid when enable_raw_low_hdec = 1 */
  u16 reserved1[3];
} amba_efr_cfg_t;
#endif

typedef int (*tf_update_enc_resolution) (int iav_fd, enc_resolution_t * reso);

typedef int (*tf_update_enc_bitrate) (int iav_fd, enc_bitrate_t * bitrate);

typedef int (*tf_update_enc_frameate) (int iav_fd, enc_framerate_t * framerate);

typedef int (*tf_update_enc_bitrate_frameate)
  (int iav_fd, enc_bitrate_framerate_t * bitrate_framerate);

typedef int (*tf_update_enc_codec_type) (int iav_fd, enc_codec_type_t * codec_type);

typedef int (*tf_update_enc_gop_structure) (int iav_fd, enc_gop_structure_t * gop_structure);

typedef int (*tf_enc_force_idr) (int iav_fd, enc_force_idr_t * codec_type);


int update_enc_resolution (int iav_fd, enc_resolution_t * reso);
int update_enc_bitrate (int iav_fd, enc_bitrate_t * bitrate);
int update_enc_framerate (int iav_fd, enc_framerate_t * framerate);
int update_enc_bitrate_frameate
  (int iav_fd, enc_bitrate_framerate_t * bitrate_framerate);
int update_enc_codec_type (int iav_fd, enc_codec_type_t * codec_type)
;
int update_enc_gop_structure (int iav_fd, enc_gop_structure_t * gop_structure)
;
int enc_force_idr (int iav_fd, enc_force_idr_t * force_idr);
int update_enc (int iav_fd, enc_config_t *config);


int parse_enc_resolution (const char * reso_string, enc_resolution_t * reso);

// enc:bitrate
int parse_enc_bitrate (const char * bitrate_string, enc_bitrate_t * bitrate);

// enc:framerate
int parse_enc_framerate (const char * framerate_string, enc_framerate_t * framerate);

int parse_enc_bitrate_frameate (
  const char * bitrate_framerate_string, enc_bitrate_framerate_t * bitrate_framerate);

// enc:h264/h265
int parse_enc_codec_type (
  const char * codec_type_string, enc_codec_type_t * codec_type)
;

int parse_enc_gop_structure (
  const char * gop_structure_string, enc_gop_structure_t * gop_structure)
;

int parse_enc_force_idr (
  const char * force_idr_string, enc_force_idr_t * force_idr);

int parse_enc (int iav_fd, const char * custom_properties, enc_config_t *config);

int get_enc_info_config (int iav_fd, enc_config_t *config, u32 stream_id, u32 vsrc_id);

char * get_enc_info_string(int iav_fd, enc_config_t *config);

int get_frame_rate(int iav_fd, enc_config_t *config, int stream_idx);

int sync_frame_force_idr (int iav_fd, enc_force_idr_t *force_idr);

int stop_encode(int iav_fd, u32 stream_map);

#endif

