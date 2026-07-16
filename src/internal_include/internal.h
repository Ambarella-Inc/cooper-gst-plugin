/*
 * internal.h
 *
 * History:
 *    7/24/2015 - [Zhi He] created file
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

#ifndef __INTERNAL_H__
#define __INTERNAL_H__

#if defined(AMBA_SOC_CV2) || defined(AMBA_SOC_CV22) || defined(AMBA_SOC_CV25) || defined(AMBA_SOC_CV28)
#define DBUILD_AMBA_CAVALRY_V2
#elif defined(AMBA_SOC_CV5) || defined(AMBA_SOC_CV52)
#define DBUILD_AMBA_CAVALRY_V2
#elif defined(AMBA_SOC_CV7) || defined(AMBA_SOC_CV72) || defined(AMBA_SOC_CV75) || defined(AMBA_SOC_N1) || defined(AMBA_SOC_N1_655) || defined(AMBA_SOC_CV7)
#define DBUILD_AMBA_CAVALRY_V3
#else
#error "Unknown SOC For Amba Gst Plugin"
#endif

#define DSYSTEM_MAX_CHANNEL_NUM 16
#define DMAX_FILE_NAME_LENGTH 512
#define DMAX_UT_VOUT_STRING_LENGTH 32

#define DINVALID_VALUE_TAG_64 0xFEDCFEFEFEDCFEFELL

#define DFLAG_STATUS_EOS 0
#define DCAL_BITMASK(x) (1 << x)

#ifndef D_MAX_STREAM_NUM
#define D_MAX_STREAM_NUM  20
#endif

#define DMAX_LABEL_LEN   128

#define DUNUSED(x) (void)x

//-----------------------------------------------------------------------
//
//  Video related defines
//
//-----------------------------------------------------------------------

//refer to 264 spec table 7.1
enum {
  ENalType_unspecified = 0,

  ENalType_NON_IDR_BEGIN = 0x01,

  ENalType_IDR = 5,
  ENalType_SEI = 6,
  ENalType_SPS = 7,
  ENalType_PPS = 8,
  ENalType_AUD = 9,
  ENalType_END_OF_SEQUENCE = 0x0a,
  ENalType_END_OF_STREAM = 0x0b,
};

//refer to 265 spec table 7.1
enum {
  EHEVCNalType_TRAIL_N = 0,
  EHEVCNalType_TRAIL_R = 1,
  EHEVCNalType_TSA_N = 2,
  EHEVCNalType_TSA_R = 3,
  EHEVCNalType_STSA_N = 4,
  EHEVCNalType_STSA_R = 5,
  EHEVCNalType_RADL_N = 6,
  EHEVCNalType_RADL_R = 7,
  EHEVCNalType_RASL_N = 8,
  EHEVCNalType_RASL_R = 9,
  EHEVCNalType_RSV_VCL_N10 = 10,
  EHEVCNalType_RSV_VCL_R11 = 11,
  EHEVCNalType_RSV_VCL_N12 = 12,
  EHEVCNalType_RSV_VCL_R13 = 13,
  EHEVCNalType_RSV_VCL_N14 = 14,
  EHEVCNalType_RSV_VCL_R15 = 15,
  EHEVCNalType_BLA_W_LP = 16,
  EHEVCNalType_BLA_W_RADL = 17,
  EHEVCNalType_BLA_N_LP = 18,
  EHEVCNalType_IDR_W_RADL = 19,
  EHEVCNalType_IDR_N_LP = 20,
  EHEVCNalType_CRA_NUT = 21,
  EHEVCNalType_RSV_IRAP_VCL22 = 22,
  EHEVCNalType_RSV_IRAP_VCL23 = 23,
  EHEVCNalType_RSV_VCL24 = 24,
  EHEVCNalType_RSV_VCL25 = 25,
  EHEVCNalType_RSV_VCL26 = 26,
  EHEVCNalType_RSV_VCL27 = 27,
  EHEVCNalType_RSV_VCL28 = 28,
  EHEVCNalType_RSV_VCL29 = 29,
  EHEVCNalType_RSV_VCL30 = 30,
  EHEVCNalType_RSV_VCL31 = 31,
  EHEVCNalType_VPS = 32,
  EHEVCNalType_SPS = 33,
  EHEVCNalType_PPS = 34,
  EHEVCNalType_AUD = 35,
  EHEVCNalType_EOS = 36,
  EHEVCNalType_EOB = 37,
  EHEVCNalType_FD = 38,
  EHEVCNalType_PREFIX_SEI = 39,
  EHEVCNalType_SUFFIX_SEI = 40,
  EHEVCNalType_RSV_NVCL41 = 41,
  EHEVCNalType_RSV_NVCL42 = 42,
  EHEVCNalType_RSV_NVCL43 = 43,
  EHEVCNalType_RSV_NVCL44 = 44,
  EHEVCNalType_RSV_NVCL45 = 45,
  EHEVCNalType_RSV_NVCL46 = 46,
  EHEVCNalType_RSV_NVCL47 = 47,

  EHEVCNalType_VCL_END = 31,
};

//refer to jpeg spec, itu-t81, annex-b
enum {
  EJPEG_MarkerPrefix = 0xFF,

  //Start Of Frame markers, non-differential, Huffman coding
  EJPEG_SOF0 = 0xC0, // baseline DCT
  EJPEG_SOF1 = 0xC1, //extented sequential DCT
  EJPEG_SOF2 = 0xC2, //progressive DCT
  EJPEG_SOF3 = 0xC3, //lossless (sequential)

  //Start Of Frame markers, differential, Huffman coding
  EJPEG_SOF5 = 0xC5, //differential sequential DCT
  EJPEG_SOF6 = 0xC6, //differential progressive DCT
  EJPEG_SOF7 = 0xC7, //differential lossless (sequential)

  //Start Of Frame markers, non-differential, arithmetic coding
  EJPEG_SOF8 = 0xC8, //reserved for JPEG externtions
  EJPEG_SOF9 = 0xC9, //extented sequential DCT
  EJPEG_SOF10 = 0xCA, //progressive DCT
  EJPEG_SOF11 = 0xCB, //lossless (sequential)

  //Start Of Frame markers, differential, arithmetic coding
  EJPEG_SOF13 = 0xCD, //differential sequential DCT
  EJPEG_SOF14 = 0xCE, //differential progressive DCT
  EJPEG_SOF15 = 0xCF, //differential lossless (sequential)

  //Huffman table specification
  EJPEG_DHT = 0xC4, //define Huffman table(s)

  //Arithmetic coding conditioning specification
  EJPEG_DAT = 0xCC, //define arithmetic coding conditioning(s)

  //Restart interval termination
  EJPEG_RESTART_MIN = 0xD0,
  EJPEG_RESTART_MAX = 0xD7,

  EJPEG_SOI = 0xD8, //start of image
  EJPEG_EOI = 0xD9, //end of image
  EJPEG_SOS = 0xDA, //start of scan
  EJPEG_DQT = 0xDB, //define quantization table(s)
  EJPEG_DNL = 0xDC, //define number of lines
  EJPEG_DRI = 0xDD, //define restart interval
  EJPEG_DHP = 0xDE, //define hierarchical progression
  EJPEG_EXP = 0xDF, //expand reference component(s)

  //Reserved for application segments
  EJPEG_APP_MIN = 0xE0,
  EJPEG_APP_MAX = 0xEF,

  //Reserved for JPEG extensions
  EJPEG_JPEG_MIN = 0xF0,
  EJPEG_JPEG_MAX = 0xFD,

  EJPEG_COMMENT = 0xFE,

  //Reserved markers
  EJPEG_REV_MIN = 0x01,
  EJPEG_REV_MAX = 0xBF,
};

enum {
  ERTCPType_SR = 200,
  ERTCPType_RR = 201,
  ERTCPType_SDC = 202,
  ERTCPType_BYE = 203,
};

enum {
  RTP_VERSION = 2,

  RTP_PT_G711_PCMU = 0,
  RTP_PT_G723 = 4,
  RTP_PT_G711_PCMA = 8,
  RTP_PT_G722 = 9,
  RTP_PT_G728 = 15,
  RTP_PT_G729 = 18,

  RTP_PT_JPG = 26,
  RTP_PT_H261 = 31,
  RTP_PT_MPV = 32,
  RTP_PT_MP2T = 33,
  RTP_PT_H263 = 34,

  RTP_PAYLOAD_TYPE_PRIVATE = 96,
  RTP_PT_H264 = RTP_PAYLOAD_TYPE_PRIVATE,
  RTP_PT_H265,
  RTP_PT_AAC,
};

enum {
  RTCP_SR     = 200,
  RTCP_RR     = 201,
  RTCP_SDES   = 202,
  RTCP_BYE    = 203,
  RTCP_APP    = 204,
};


#define DRecommandMaxUDPPayloadLength 1440
#define DRecommandMaxRTPPayloadLength (DRecommandMaxUDPPayloadLength - 32)

#define DRTP_UDP_HEADER_LENGTH 12
#define DRTP_TCP_HEADER_LENGTH 16
#define DRTP_JPEG_HEADER_LENGTH 8
#define DRTP_JPEG_QT_HEADER_LENGTH 4
#define DRTP_TCP_FRAGMENTATION_HEADER_LENGTH 4

#define DSRTING_RTSP_CLIENT_TAG     "User-Agent: " DCorpLOGO " RTSP Client v20200402\r\n"
#define DSRTING_RTSP_SERVER_TAG     "RTSP Server: " DCorpLOGO " RTSP Server v20200402"
#define DSRTING_RTSP_SERVER_SDP_TAG     "Session streamed by \"" DCorpLOGO " RTSP Server\""
#define DSRTING_RTSP_REALM DCorpLOGO " RTSP Server"
#define DSRTING_RTSP_SERVER_VERSION      "2020.04.02"

#define DRTP_OVER_RTSP_MAGIC 0x24

#define DNTP_OFFSET 2208988800ULL
#define DNTP_OFFSET_US (DNTP_OFFSET * 1000000ULL)

#define RTSP_MAX_BUFFER_SIZE 4096
#define RTSP_MAX_DATE_BUFFER_SIZE 512

#define DPresetSocketTimeOutUintSeconds 0
#define DPresetSocketTimeOutUintUSeconds 300000

#define DInvalidTimeStamp (-1LL)

#define DMaxChannelNameLength 128

#define DRTP_HEADER_FIXED_LENGTH 12

#define DREAD_BE16(x) (((*((unsigned char*)x))<<8) | (*((unsigned char*)x + 1)))
#define DREAD_BE32(x) (((*((unsigned char*)x))<<24) | ((*((unsigned char*)x + 1))<<16) | ((*((unsigned char*)x + 2))<<8) | (*((unsigned char*)x + 3)))
#define DREAD_BE64(x) (((unsigned long)(*((unsigned char*)x))<<56) | ((unsigned long)(*((unsigned char*)x + 1))<<48) | ((unsigned long)(*((unsigned char*)x + 2))<<40) | ((unsigned long)(*((unsigned char*)x + 3))<<32) | ((unsigned long)(*((unsigned char*)x + 4))<<24) | ((unsigned long)(*((unsigned char*)x + 5))<<16) | ((unsigned long)(*((unsigned char*)x + 6))<<8) | (unsigned long)(*((unsigned char*)x + 7)))

#define DMIN(a,b) ((a) > (b) ? (b) : (a))
#define DMAX(a,b) ((a) > (b) ? (a) : (b))

#define BE_16(x) (((unsigned char *)(x))[0] <<  8 | ((unsigned char *)(x))[1])

#define DBEW64(x, p) do { \
    p[0] = (x >> 56) & 0xff; \
    p[1] = (x >> 48) & 0xff; \
    p[2] = (x >> 40) & 0xff; \
    p[3] = (x >> 32) & 0xff; \
    p[4] = (x >> 24) & 0xff; \
    p[5] = (x >> 16) & 0xff; \
    p[6] = (x >> 8) & 0xff; \
    p[7] = x & 0xff; \
  } while(0)

#define DBEW48(x, p) do { \
    p[0] = (x >> 40) & 0xff; \
    p[1] = (x >> 32) & 0xff; \
    p[2] = (x >> 24) & 0xff; \
    p[3] = (x >> 16) & 0xff; \
    p[4] = (x >> 8) & 0xff; \
    p[5] = x & 0xff; \
  } while(0)

#define DBEW32(x, p) do { \
    p[0] = (x >> 24) & 0xff; \
    p[1] = (x >> 16) & 0xff; \
    p[2] = (x >> 8) & 0xff; \
    p[3] = x & 0xff; \
  } while(0)

#define DBEW24(x, p) do { \
    p[0] = (x >> 16) & 0xff; \
    p[1] = (x >> 8) & 0xff; \
    p[2] = (x) & 0xff; \
  } while(0)

#define DBEW16(x, p) do { \
    p[0] = (x >> 8) & 0xff; \
    p[1] = x & 0xff; \
  } while(0)

#define DBER64(x, p) do { \
    x = ((unsigned long)p[0] << 56) | ((unsigned long)p[1] << 48) | ((unsigned long)p[2] << 40) | ((unsigned long)p[3] << 32) | ((unsigned long)p[4] << 24) | ((unsigned long)p[5] << 16) | ((unsigned long)p[6] << 8) | (unsigned long)p[7]; \
  } while(0)

#define DBER48(x, p) do { \
    x = ((unsigned long)p[0] << 40) | ((unsigned long)p[1] << 32) | ((unsigned long)p[2] << 24) | ((unsigned long)p[3] << 16) | ((unsigned long)p[4] << 8) | (unsigned long)p[5] ; \
  } while(0)

#define DBER32(x, p) do { \
    x = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; \
  } while(0)

#define DBER24(x, p) do { \
    x = (p[0] << 16) | (p[1] << 8) | p[2]; \
  } while(0)

#define DBER16(x, p) do { \
    x = (p[0] << 8) | p[1]; \
  } while(0)

#define DLEW64(x, p) do { \
    p[0] = x & 0xff; \
    p[1] = (x >> 8) & 0xff; \
    p[2] = (x >> 16) & 0xff; \
    p[3] = (x >> 24) & 0xff; \
    p[4] = (x >> 32) & 0xff; \
    p[5] = (x >> 40) & 0xff; \
    p[6] = (x >> 48) & 0xff; \
    p[7] = (x >> 56) & 0xff; \
  } while(0)

#define DLEW32(x, p) do { \
    p[0] = x & 0xff; \
    p[1] = (x >> 8) & 0xff; \
    p[2] = (x >> 16) & 0xff; \
    p[3] = (x >> 24) & 0xff; \
  } while(0)

#define DLEW16(x, p) do { \
    p[0] = x & 0xff; \
    p[1] = (x >> 8) & 0xff; \
  } while(0)

#define DLER64(x, p) do { \
    x = ((unsigned long)p[7] << 56) | ((unsigned long)p[6] << 48) | ((unsigned long)p[5] << 40) | ((unsigned long)p[4] << 32) | ((unsigned long)p[3] << 24) | ((unsigned long)p[2] << 16) | ((unsigned long)p[1] << 8) | (unsigned long)p[0]; \
  } while(0)

#define DLER32(x, p) do { \
    x = (p[3] << 24) | (p[2] << 16) | (p[1] << 8) | p[0]; \
  } while(0)

#define DLER16(x, p) do { \
    x = (p[1] << 8) | p[0]; \
  } while(0)

#ifndef DROUND_UP
#define DROUND_UP(_size, _align)    (((_size) + ((_align) - 1)) & ~((_align) - 1))
#endif

#ifndef DROUND_DOWN
#define DROUND_DOWN(_size, _align)  ((_size) & ~((_align) - 1))
#endif

#ifndef DCAL_MIN
#define DCAL_MIN(_a, _b)    ((_a) < (_b) ? (_a) : (_b))
#endif

#ifndef DCAL_MAX
#define DCAL_MAX(_a, _b)    ((_a) > (_b) ? (_a) : (_b))
#endif

#ifndef DARRAY_SIZE
#define DARRAY_SIZE(_array) (sizeof(_array) / sizeof(_array[0]))
#endif

#ifndef DPTR_OFFSET
#define DPTR_OFFSET(_type, _member) ((int)&((_type*)0)->member)
#endif

#ifndef DPTR_ADD
#define DPTR_ADD(_ptr, _size)   (void*)((char*)(_ptr) + (_size))
#endif



#define DDefaultTimeScale 90000
#define DDefaultVideoFramerateNum 90000
#define DDefaultVideoFramerateDen 3003
#define DDefaultAudioSampleRate 48000
#define DDefaultAudioFrameSize 1024
#define DDefaultAudioChannelNumber 1
#define DDefaultAudioBufferNumber 24
#define DDefaultAudioOutputBufferFrameCount 48

#define AUDIO_CHUNK_SIZE 1024
#define SINGLE_CHANNEL_MAX_AUDIO_BUFFER_SIZE (AUDIO_CHUNK_SIZE * sizeof(TS16))
#define MAX_AUDIO_BUFFER_SIZE (SINGLE_CHANNEL_MAX_AUDIO_BUFFER_SIZE * 2)

#define DMaxFileExterntionLength 32
#define DMaxFileIndexLength 32


//filename handling
enum {
  eFileNameHandling_noAppendExtention,//have '.'+'known externtion', like "xxx.mp4", "xxx.3gp", "xxx.ts"
  eFileNameHandling_appendExtention,//have no '.' + 'known externtion'
};

enum {
  eFileNameHandling_noInsert,
  eFileNameHandling_insertFileNumber,//have '%', and first is '%d' or '%06d', like "xxx_%d.mp4", "xxx_%06d.mp4"
  eFileNameHandling_insertDateTime,//have '%t', and first is '%t', will insert datetime, like "xxx_%t.mp4" ---> "xxx_20111223_115503.mp4"
};


/* Definition of structure storing data for this element. */

typedef enum {
  StreamFormat_Invalid = 0,
  StreamFormat_H264 = 0x01,
  StreamFormat_VC1 = 0x02,
  StreamFormat_MPEG4 = 0x03,
  StreamFormat_WMV3 = 0x04,
  StreamFormat_MPEG12 = 0x05,
  StreamFormat_HybridMPEG4 = 0x06,
  StreamFormat_HybridRV40 = 0x07,
  StreamFormat_VideoSW = 0x08,
  StreamFormat_JPEG = 0x09,
  StreamFormat_AAC = 0x0a,
  StreamFormat_MPEG12Audio = 0x0b,
  StreamFormat_MP2 = 0x0c,
  StreamFormat_MP3 = 0x0d,
  StreamFormat_AC3 = 0x0e,
  StreamFormat_ADPCM = 0x0f,
  StreamFormat_AMR_NB = 0x10,
  StreamFormat_AMR_WB = 0x11,
  StreamFormat_PCMU = 0x12,
  StreamFormat_PCMA = 0x13,
  StreamFormat_H265 = 0x14,
  StreamFormat_HEIC = 0x15,
  StreamFormat_BYTE = 0x16,
  StreamFormat_H264_BYTE = 0x17,
  StreamFormat_H265_BYTE = 0x18,

  StreamFormat_PixelFormat_YUV420p = 0x40,
  StreamFormat_PixelFormat_NV12 = 0x41,
  StreamFormat_PixelFormat_YUYV = 0x42,
  StreamFormat_PixelFormat_YUV422p = 0x43,
  StreamFormat_PixelFormat_YVU420p = 0x44,
  StreamFormat_PixelFormat_GRAY8 = 0x45,
  StreamFormat_PixelFormat_YUV444p = 0x46,

  StreamFormat_PCM_S16 = 0x58,

  StreamFormat_FFMpegCustomized = 0x68,
} StreamFormat;

typedef enum {
  StreamType_Invalid = 0,
  StreamType_Video,
  StreamType_Audio,
  StreamType_Subtitle,
  StreamType_PrivateData,

  StreamType_Cmd,

  StreamType_TotalNum,
} StreamType;

typedef enum {
  EntropyType_NOTSet = 0,
  EntropyType_H264_CABAC,
  EntropyType_H264_CAVLC,
} EntropyType;

typedef enum {
  AudioSampleFMT_NONE = -1,
  AudioSampleFMT_U8,          ///< unsigned 8 bits
  AudioSampleFMT_S16,         ///< signed 16 bits
  AudioSampleFMT_S32,         ///< signed 32 bits
  AudioSampleFMT_FLT,         ///< float
  AudioSampleFMT_DBL,         ///< double
  AudioSampleFMT_NB           ///< Number of sample formats. DO NOT USE if linking dynamically
} AudioSampleFMT;

typedef struct {
  unsigned int pic_width;
  unsigned int pic_height;
  unsigned int pic_offset_x;
  unsigned int pic_offset_y;
  unsigned int framerate_num;
  unsigned int framerate_den;
  float framerate;
  unsigned int M, N, IDRInterval;//control P, I, IDR percentage
  unsigned int sample_aspect_ratio_num;
  unsigned int sample_aspect_ratio_den;
  unsigned int bitrate;
  unsigned int lowdelay;
  EntropyType entropy_type;

  unsigned long durationMS;
} SVideoParams;

typedef struct {
  unsigned int sample_rate;
  AudioSampleFMT sample_format;
  unsigned int channel_number;
  unsigned int channel_layout;
  unsigned int frame_size;
  unsigned int bitrate;
  unsigned int need_skip_adts_header;
  unsigned int pts_unit_num;//pts's unit
  unsigned int pts_unit_den;

  unsigned char is_channel_interlave;
  unsigned char is_big_endian;
  unsigned char reserved0, reserved1;

  unsigned int codec_format;
  unsigned int customized_codec_type;

  unsigned long durationMS;
} SAudioParams;

typedef union {
  SVideoParams video;
  SAudioParams audio;
} UFormatSpecific;

typedef struct {
  unsigned int codec_id;
  unsigned int payload_type;

  unsigned char stream_index;
  unsigned char stream_presence;
  unsigned char stream_enabled;
  unsigned char inited;

  StreamType stream_type;
  StreamFormat stream_format;
  UFormatSpecific spec;
} stream_codec_info_t;

enum DecoderFeedingRule {
  DecoderFeedingRule_NotValid = 0,
  DecoderFeedingRule_AllFrames,
  DecoderFeedingRule_RefOnly,
  DecoderFeedingRule_IOnly,
  DecoderFeedingRule_IDROnly,
};

#endif

