/*
 * codec_interface.h
 *
 * History:
 *    11/18/2013 - [Zhi He] created file
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

#ifndef __CODEC_INTERFACE_H__
#define __CODEC_INTERFACE_H__

enum {
  H264_FMT_INVALID = 0,
  H264_FMT_AVCC,      // nal delimit: 4 byte, which represents data length
  H264_FMT_ANNEXB,     // nal delimiter: 00 00 00 01
};

enum {
  EH264SliceType_P = 0,
  EH264SliceType_B = 1,
  EH264SliceType_I = 2,
  EH264SliceType_SP = 3,
  EH264SliceType_SI = 4,

  EH264SliceType_FieldOffset = 5,
  EH264SliceType_MaxValue = 9,
};

typedef struct {
#ifdef DCONFIG_BIG_ENDIAN
  unsigned char syncword_11to4 : 8;
  unsigned char syncword_3to0 : 4;
  unsigned char id : 1;
  unsigned char layer : 2;
  unsigned char protection_absent : 1;
  unsigned char profile : 2;
  unsigned char sampling_frequency_index : 4;
  unsigned char private_bit : 1;
  unsigned char channel_configuration_2 : 1;
  unsigned char channel_configuration_1to0 : 2;
  unsigned char orignal_copy : 1;
  unsigned char home : 1;
  unsigned char reserved : 4;
#else
  unsigned char syncword_11to4 : 8;
  unsigned char protection_absent : 1;
  unsigned char layer : 2;
  unsigned char id : 1;
  unsigned char syncword_3to0 : 4;
  unsigned char channel_configuration_2 : 1;
  unsigned char private_bit : 1;
  unsigned char sampling_frequency_index : 4;
  unsigned char profile : 2;
  unsigned char reserved : 4;
  unsigned char home : 1;
  unsigned char orignal_copy : 1;
  unsigned char channel_configuration_1to0 : 2;
#endif
} SADTSFixedHeader;

unsigned char *gstNALUFindNextStartCode (unsigned char *p, unsigned int len);
unsigned char *gstNALUFindIDR (unsigned char *p, unsigned int len);
unsigned char *gstNALUFindSPSHeader (unsigned char *p, unsigned int len);
unsigned char *gstNALUFindPPSEnd (unsigned char *p, unsigned int len);

#if 0
void gstFindH264SpsPpsIdr (unsigned char *data_base, int data_size, unsigned char &has_sps, unsigned char &has_pps, unsigned char &has_idr, unsigned char *&p_sps, unsigned char *&p_pps, unsigned char *&p_pps_end, unsigned char *&p_idr);
void gstFindH265VpsSpsPpsIdr (unsigned char *data_base, int data_size, unsigned char &has_vps, unsigned char &has_sps, unsigned char &has_pps, unsigned char &has_idr, unsigned char *&p_vps, unsigned char *&p_sps, unsigned char *&p_pps, unsigned char *&p_pps_end, unsigned char *&p_idr);
#endif

typedef enum {
  EADTSID_MPEG2 = 0,
  EADTSID_MPEG4 = 1,
} EADTSID;

typedef enum {
  EADTSMPEG2Profile_Main = 0,
  EADTSMPEG2Profile_LowComplexity = 1,
  EADTSMPEG2Profile_ScalableSamplingRate = 2,
} EADTSMPEG2Profile;

typedef enum {
  EADTSSamplingstrequency_96000 = 0,
  EADTSSamplingstrequency_88200 = 1,
  EADTSSamplingstrequency_64000 = 2,
  EADTSSamplingstrequency_48000 = 3,
  EADTSSamplingstrequency_44100 = 4,
  EADTSSamplingstrequency_32000 = 5,
  EADTSSamplingstrequency_24000 = 6,
  EADTSSamplingstrequency_22050 = 7,
  EADTSSamplingstrequency_16000 = 8,
  EADTSSamplingstrequency_12000 = 9,
  EADTSSamplingstrequency_11025 = 10,
  EADTSSamplingstrequency_8000 = 11,
  EADTSSamplingstrequency_7350 = 12,
} EADTSSamplingstrequency;

//AAC
typedef struct {
  unsigned char ID;
  unsigned char layer;
  unsigned char protection_absent;
  unsigned char profile;
  unsigned char sampling_frequency_index;
  unsigned char private_bit;
  unsigned char channel_configuration;
  unsigned char original_copy;
  unsigned char home;

  unsigned char number_of_raw_data_blocks_in_frame;
  unsigned char copyright_identification_bit;
  unsigned char copyright_identification_start;
  unsigned short aac_frame_length;
  unsigned short adts_buffer_fullness;
} SADTSHeader;

unsigned int gstGetADTSFrameLength (unsigned char *p);
int gstParseADTSHeader (unsigned char *p, SADTSHeader *header);
int gstBuildADTSHeader (unsigned char *p,
                           unsigned char ID, unsigned char layer, unsigned char protection_absent, unsigned char profile, unsigned char sampling_frequency_index, unsigned char private_bit, unsigned char channel_configuration,
                           unsigned char original_copy, unsigned char home, unsigned char copyright_identification_bit, unsigned char copyright_identification_start,
                           unsigned int frame_length, unsigned short adts_buffer_fullness, unsigned char number_of_raw_data_blocks_in_frame);
unsigned int gstGetADTSSamplingstrequency (unsigned char sampling_frequency_index);


//H264
#define DQP_MAX_NUM (51 + 6*6)
#define DMAX_SPS_COUNT          32
#define DMAX_PPS_COUNT         256
#define DMIN_LOG2_MAX_FRAME_NUM 4
#define DMAX_LOG2_MAX_FRAME_NUM 16
#define DMAX_PICTURE_COUNT 36
#define DEXTENDED_SAR          255
#define DMIN_CACHE_BITS 25
#define DARRAY_ELEMS(a) (sizeof(a) / sizeof((a)[0]))

typedef enum {
  EColorPrimaries_BT709       = 1, ///< also ITU-R BT1361 / IEC 61966-2-4 / SMPTE RP177 Annex B
  EColorPrimaries_Unspecified = 2,
  EColorPrimaries_BT470M      = 4,
  EColorPrimaries_BT470BG     = 5, ///< also ITU-R BT601-6 625 / ITU-R BT1358 625 / ITU-R BT1700 625 PAL & SECAM
  EColorPrimaries_SMPTE170M   = 6, ///< also ITU-R BT601-6 525 / ITU-R BT1358 525 / ITU-R BT1700 NTSC
  EColorPrimaries_SMPTE240M   = 7, ///< functionally identical to above
  EColorPrimaries_FILM        = 8,
  EColorPrimaries_BT2020      = 9, ///< ITU-R BT2020
  EColorPrimaries_NB, ///< Not part of ABI
} EColorPrimaries;

typedef enum {
  EColorTransferCharacteristic_BT709       = 1, ///< also ITU-R BT1361
  EColorTransferCharacteristic_UNSPECIFIED = 2,
  EColorTransferCharacteristic_GAMMA22     = 4, ///< also ITU-R BT470M / ITU-R BT1700 625 PAL & SECAM
  EColorTransferCharacteristic_GAMMA28     = 5, ///< also ITU-R BT470BG
  EColorTransferCharacteristic_SMPTE170M    =  6, ///< also ITU-R BT601-6 525 or 625 / ITU-R BT1358 525 or 625 / ITU-R BT1700 NTSC
  EColorTransferCharacteristic_SMPTE240M   = 7,
  EColorTransferCharacteristic_LINEAR       =  8, ///< "Linear transfer characteristics"
  EColorTransferCharacteristic_LOG          =  9, ///< "Logarithmic transfer characteristic (100:1 range)"
  EColorTransferCharacteristic_LOG_SQRT     = 10, ///< "Logarithmic transfer characteristic (100 * Sqrt( 10 ) : 1 range)"
  EColorTransferCharacteristic_IEC61966_2_4 = 11, ///< IEC 61966-2-4
  EColorTransferCharacteristic_BT1361_ECG   = 12, ///< ITU-R BT1361 Extended Colour Gamut
  EColorTransferCharacteristic_IEC61966_2_1 = 13, ///< IEC 61966-2-1 (sRGB or sYCC)
  EColorTransferCharacteristic_BT2020_10    = 14, ///< ITU-R BT2020 for 10 bit system
  EColorTransferCharacteristic_BT2020_12    = 15, ///< ITU-R BT2020 for 12 bit system
  EColorTransferCharacteristic_NB, ///< Not part of ABI
} EColorTransferCharacteristic;

typedef enum {
  EColorSpace_RGB         = 0,
  EColorSpace_BT709       = 1, ///< also ITU-R BT1361 / IEC 61966-2-4 xvYCC709 / SMPTE RP177 Annex B
  EColorSpace_UNSPECIFIED = 2,
  EColorSpace_FCC         = 4,
  EColorSpace_BT470BG     = 5, ///< also ITU-R BT601-6 625 / ITU-R BT1358 625 / ITU-R BT1700 625 PAL & SECAM / IEC 61966-2-4 xvYCC601
  EColorSpace_SMPTE170M   = 6, ///< also ITU-R BT601-6 525 / ITU-R BT1358 525 / ITU-R BT1700 NTSC / functionally identical to above
  EColorSpace_SMPTE240M   = 7,
  EColorSpace_YCOCG       = 8, ///< Used by Dirac / VC-2 and H.264 FRext, see ITU-T SG16
  EColorSpace_BT2020_NCL  =  9, ///< ITU-R BT2020 non-constant luminance system
  EColorSpace_BT2020_CL   = 10, ///< ITU-R BT2020 constant luminance system
  EColorSpace_NB             , ///< Not part of ABI
} EColorSpace;

typedef struct {
  int profile_idc;
  int level_idc;
  int chroma_format_idc;
  int transform_bypass;              ///< qpprime_y_zero_transform_bypass_flag
  int log2_max_frame_num;            ///< log2_max_frame_num_minus4 + 4
  int poc_type;                      ///< pic_order_cnt_type
  int log2_max_poc_lsb;              ///< log2_max_pic_order_cnt_lsb_minus4
  int delta_pic_order_always_zero_flag;
  int offset_for_non_ref_pic;
  int offset_for_top_to_bottom_field;
  int poc_cycle_length;              ///< num_ref_frames_in_pic_order_cnt_cycle
  int ref_frame_count;               ///< num_ref_frames
  int gaps_in_frame_num_allowed_flag;
  int mb_width;                      ///< pic_width_in_mbs_minus1 + 1
  int mb_height;                     ///< pic_height_in_map_units_minus1 + 1
  int frame_mbs_only_flag;
  int mb_aff;                        ///< mb_adaptive_frame_field_flag
  int direct_8x8_inference_flag;
  int crop;                          ///< frame_cropping_flag
  unsigned int crop_left;            ///< frame_cropping_rect_left_offset
  unsigned int crop_right;           ///< frame_cropping_rect_right_offset
  unsigned int crop_top;             ///< frame_cropping_rect_top_offset
  unsigned int crop_bottom;          ///< frame_cropping_rect_bottom_offset
  int vui_parameters_present_flag;
  unsigned int sar_num;
  unsigned int sar_den;
  int video_signal_type_present_flag;
  int full_range;
  int colour_description_present_flag;
  int timing_info_present_flag;
  unsigned int num_units_in_tick;
  unsigned int time_scale;
  int fixed_frame_rate_flag;
  short offset_for_ref_frame[256]; // FIXME dyn aloc?
  int bitstream_restriction_flag;
  int num_reorder_frames;
  int scaling_matrix_present;
  unsigned char scaling_matrix4[6][16];
  unsigned char scaling_matrix8[6][64];
  int nal_hrd_parameters_present_flag;
  int vcl_hrd_parameters_present_flag;
  int pic_struct_present_flag;
  int time_offset_length;
  int cpb_cnt;                          ///< See H.264 E.1.2
  int initial_cpb_removal_delay_length; ///< initial_cpb_removal_delay_length_minus1 + 1
  int cpb_removal_delay_length;         ///< cpb_removal_delay_length_minus1 + 1
  int dpb_output_delay_length;          ///< dpb_output_delay_length_minus1 + 1
  int bit_depth_luma;                   ///< bit_depth_luma_minus8 + 8
  int bit_depth_chroma;                 ///< bit_depth_chroma_minus8 + 8
  int residual_color_transform_flag;    ///< residual_colour_transform_flag
  int constraint_set_flags;             ///< constraint_set[0-3]_flag
  int isnew;                              ///< flag to keep track if the decoder context needs re-init due to changed SPS

  EColorPrimaries color_primaries;
  EColorTransferCharacteristic color_trc;
  EColorSpace colorspace;
} SCodecVideoH264SPS;

/**
 * Picture parameter set
 */
typedef struct {
  unsigned int sps_id;
  int cabac;                  ///< entropy_coding_mode_flag
  int pic_order_present;      ///< pic_order_present_flag
  int slice_group_count;      ///< num_slice_groups_minus1 + 1
  int mb_slice_group_map_type;
  unsigned int ref_count[2];  ///< num_ref_idx_l0/1_active_minus1 + 1
  int weighted_pred;          ///< weighted_pred_flag
  int weighted_bipred_idc;
  int init_qp;                ///< pic_init_qp_minus26 + 26
  int init_qs;                ///< pic_init_qs_minus26 + 26
  int chroma_qp_index_offset[2];
  int deblocking_filter_parameters_present; ///< deblocking_filter_parameters_present_flag
  int constrained_intra_pred;     ///< constrained_intra_pred_flag
  int redundant_pic_cnt_present;  ///< redundant_pic_cnt_present_flag
  int transform_8x8_mode;         ///< transform_8x8_mode_flag
  unsigned char scaling_matrix4[6][16];
  unsigned char scaling_matrix8[6][64];
  unsigned char chroma_qp_table[2][DQP_MAX_NUM + 1];  ///< pre-scaled (with chroma_qp_index_offset) version of qp_table
  int chroma_qp_diff;
} SCodecVideoH264PPS;

typedef struct {
  unsigned int max_width, max_height;
  unsigned int framerate_num;
  unsigned int framerate_den;

  StreamFormat format;
  VideoFrameRate framerate;

  unsigned int profile_indicator;
  unsigned int level_indicator;
} SCodecVideoCommon;

typedef struct {
  SCodecVideoCommon common;

  SCodecVideoH264SPS sps;
  SCodecVideoH264PPS pps;
} SCodecVideoH264;

extern SCodecVideoCommon *gstGetVideoCodecParser (unsigned char *p_data, TMemSize data_size, StreamFormat format, int &ret);
extern void gstReleaseVideoCodecParser (SCodecVideoCommon *parser);
extern unsigned char gstGetH264SilceType (unsigned char *pdata);

typedef struct {
  unsigned char  configurationVersion;
  unsigned char  general_profile_space; // 2bits
  unsigned char  general_tier_flag; // 1bit
  unsigned char  general_profile_idc; //5bits
  unsigned int general_profile_compatibility_flags;
  TU64 general_constraint_indicator_flags; // 48 bits
  unsigned char  general_level_idc;
  unsigned short min_spatial_segmentation_idc; //'1111' + 12bits
  unsigned char  parallelismType; // '111111' + 2bits
  unsigned char  chromaFormat; // '111111' + 2bits
  unsigned char  bitDepthLumaMinus8; // '11111' + 3bits
  unsigned char  bitDepthChromaMinus8; // '11111' + 3bits
  unsigned short avgstrameRate;
  unsigned char  constantFrameRate; // 2bits
  unsigned char  numTemporalLayers; // 3bits
  unsigned char  temporalIdNested; // 1 bit
  unsigned char  lengthSizeMinusOne; // 2bits
  unsigned char  numOfArrays;
} SHEVCDecoderConfigurationRecord;

enum {
  eAudioObjectType_AAC_MAIN = 1,
  eAudioObjectType_AAC_LC = 2,
  eAudioObjectType_AAC_SSR = 3,
  eAudioObjectType_AAC_LTP = 4,
  eAudioObjectType_AAC_scalable = 6,
  //add others, todo

  eSamplingstrequencyIndex_96000 = 0,
  eSamplingstrequencyIndex_88200 = 1,
  eSamplingstrequencyIndex_64000 = 2,
  eSamplingstrequencyIndex_48000 = 3,
  eSamplingstrequencyIndex_44100 = 4,
  eSamplingstrequencyIndex_32000 = 5,
  eSamplingstrequencyIndex_24000 = 6,
  eSamplingstrequencyIndex_22050 = 7,
  eSamplingstrequencyIndex_16000 = 8,
  eSamplingstrequencyIndex_12000 = 9,
  eSamplingstrequencyIndex_11025 = 0xa,
  eSamplingstrequencyIndex_8000 = 0xb,
  eSamplingstrequencyIndex_7350 = 0xc,
  eSamplingstrequencyIndex_escape = 0xf,//should not be this value
};

//refer to iso14496-3
#ifdef BUILD_OS_WINDOWS
#pragma pack(push,1)
typedef struct {
  unsigned char samplingstrequencyIndex_high : 3;
  unsigned char audioObjectType : 5;
  unsigned char bitLeft : 3;
  unsigned char channelConfiguration : 4;
  unsigned char samplingstrequencyIndex_low : 1;
} SSimpleAudioSpecificConfig;
#pragma pack(pop)
#else
typedef struct {
  unsigned char samplingstrequencyIndex_high : 3;
  unsigned char audioObjectType : 5;
  unsigned char bitLeft : 3;
  unsigned char channelConfiguration : 4;
  unsigned char samplingstrequencyIndex_low : 1;
} __attribute__ ( (packed) ) SSimpleAudioSpecificConfig;
#endif

#define DMAX_JPEG_QT_TABLE_NUMBER 16

enum {
  EJpegTypeInFrameHeader_YUV422 = 0,
  EJpegTypeInFrameHeader_YUV420 = 1,
  EJpegTypeInFrameHeader_GREY8 = 2,
  EJpegTypeInFrameHeader_YUV444 = 3,
};

typedef struct {
  unsigned char *p_table;
  unsigned int length;
} SJPEGQtTable;

typedef struct {
  unsigned int width, height;

  unsigned char type; //0: 422, 1: 420
  unsigned char number_qt_tables;
  unsigned short precision;

  SJPEGQtTable qt_tables[DMAX_JPEG_QT_TABLE_NUMBER];

  unsigned int total_tables_length;

  unsigned char *p_jpeg_content;
  unsigned int jpeg_content_length;//except eoi

  unsigned char *p_app_reserved;
  unsigned int app_reserved_length;
} SJPEGInfo;

extern unsigned char *gstGenerateAACExtraData (unsigned int samplerate, unsigned int channel_number, unsigned int &size);
extern void gstParseAACExtraData (unsigned char *extra_data, unsigned int *samplerate, unsigned int *channel_number);
extern unsigned char gstGetAACSamplingstrequencyIndex (unsigned int samplerate);
extern int gstGetH264Extradata (unsigned char *data_base, unsigned int data_size, unsigned char *&p_extradata, unsigned int &extradata_size);
extern int gstGetH264SPSPPS (unsigned char *data_base, unsigned int data_size, unsigned char *&p_sps, unsigned int &sps_size, unsigned char *&p_pps, unsigned int &pps_size);
extern int gstGetH265Extradata (unsigned char *data_base, unsigned int data_size, unsigned char *&p_extradata, unsigned int &extradata_size);
extern int gstGetH265VPSSPSPPS (unsigned char *data_base, unsigned int data_size, unsigned char *&p_vps, unsigned int &vps_size, unsigned char *&p_sps, unsigned int &sps_size, unsigned char *&p_pps, unsigned int &pps_size);

extern unsigned char *gstNALUFindFirstAVCSliceHeader (unsigned char *p, unsigned int len);
extern unsigned char *nalu_find_first_avc_slice_header_type (unsigned char *p, unsigned int len, unsigned char &nal_type);
extern unsigned char *nalu_find_first_hevc_slice_header_type (unsigned char *p, unsigned int len, unsigned char &nal_type, unsigned char &is_first_slice);
extern unsigned char *gstNALUFindFirstHEVCVPSSPSPPSAndSliceNalType (unsigned char *p, unsigned int len, unsigned char &nal_type);
extern unsigned char *gstNALUFindFirstAVCNalType (unsigned char *p, unsigned int len, unsigned char &nal_type);

extern int gstGetH265SizeFromSPS (unsigned char *p_data, unsigned int data_size, unsigned int &pic_width, unsigned int &pic_height);
extern int gstGetH265SizeFromExtradata (unsigned char *p_data, unsigned int data_size, unsigned int &pic_width, unsigned int &pic_height);
extern int gstGenerateHEVCDecoderConfigurationRecord (SHEVCDecoderConfigurationRecord *record, unsigned char *vps, unsigned int vps_length, unsigned char *sps, unsigned int sps_length, unsigned char *pps, unsigned int pps_length, unsigned int &pic_width, unsigned int &pic_height);
extern int gstParseHEVCSPS (SHEVCDecoderConfigurationRecord *record, unsigned char *sps, unsigned int sps_length, unsigned int &pic_width, unsigned int &pic_height);

extern int gstParseJPEG (unsigned char *p, unsigned int size, SJPEGInfo *info);

extern void gstAmendVideoResolution (unsigned int &width, unsigned int &height);
extern int gstGetH264SizeFromSPS (unsigned char *p_sps, unsigned int &width, unsigned int &height);

#endif

