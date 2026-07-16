/*******************************************************************************
 * osd_draw_types.h
 *
 * History:
 *    5/28/2018 - [Huaiqing Wang] created file
 *    5/12/2024 - [Peng-Xue Duan] created file
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
/*! @file osd_draw_types.h
 *  @bried This file contains several data defination about overlay
 */
#ifndef _OSD_DRAW_TYPES_H_
#define _OSD_DRAW_TYPES_H_

//#include <locale.h>
#include <stddef.h>
#include "text_insert_v2.h"

#define AMBA_DRAW_STRING_MAX_NUM (256) //!< it limits the maximum string length which is added on overlay
#define AMBA_DMAX_NAME_LENGTH (128)

//const char default_char_set[AMBA_DRAW_STRING_MAX_NUM] =
    //" !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";

/*! @enum AMBA_DRAW_ROTATE_MODE
 *  @brief This enum lists all drawing rorate mode.
 */
enum AMBA_DRAW_ROTATE_MODE {
  AMBA_DRAW_NO_ROTATE_FLIP = 0,   //!< no rorate
  AMBA_DRAW_HORIZONTAL_FLIP = (1 << 0), //!< horizontal flip
  AMBA_DRAW_VERTICAL_FLIP = (1 << 1), //!<  vertical flip
  AMBA_DRAW_ROTATE_90 = (1 << 2), //!<  rorate 90

  AMBA_DRAW_AUTO_ROTATE = (1 << 3),

  AMBA_DRAW_CW_ROTATE_90 = AMBA_DRAW_ROTATE_90, //!< rorate 90
  AMBA_DRAW_CW_ROTATE_180 = AMBA_DRAW_HORIZONTAL_FLIP | AMBA_DRAW_VERTICAL_FLIP, //!< rorate 180
  AMBA_DRAW_CW_ROTATE_270 = AMBA_DRAW_ROTATE_90 | AMBA_DRAW_CW_ROTATE_180,    //!< rorate 270

};

/*! @enum AMBA_DRAW_FORMAT
 *  @brief This enum lists all drawing formats supported by overlay
 */
enum AMBA_DRAW_FORMAT
{
  AMBA_DRAW_FORMAT_NONE = -1, //!< invalid format

  AMBA_DRAW_FORMAT_8BIT_CLUT = 0, //!< 8bit YUV lookup table

  // DSP V6
  //16-bits
  AMBA_DRAW_FORMAT_RGB565 = 1, //!< 5:6:5 (RGB),value 0x0000 is full transparent
  AMBA_DRAW_FORMAT_UYV565 = 2, /* 5:6:5 (UYV) or (BGR) */
  AMBA_DRAW_FORMAT_BGR565 = 3,
  AMBA_DRAW_FORMAT_AYUV4444 = 4, /* 4:4:4:4 (AYUV) */
  AMBA_DRAW_FORMAT_RGBA4444 = 5, /* 4:4:4:4 (RGBA) */
  AMBA_DRAW_FORMAT_BGRA4444 = 6, /* 4:4:4:4 (BGRA) */
  AMBA_DRAW_FORMAT_ABGR4444 = 7, /* 4:4:4:4 (ABGR) */
  AMBA_DRAW_FORMAT_ARGB4444 = 8, /* 4:4:4:4 (ARGB) */
  AMBA_DRAW_FORMAT_AYUV1555 = 9, /* 1:5:5:5 (AYUV) */
  AMBA_DRAW_FORMAT_YUV1555 = 10, /* 1:5:5:5 (MSB ignored, YUV) */
  AMBA_DRAW_FORMAT_RGBA5551 = 11, /* 5:5:5:1 (RGBA) */
  AMBA_DRAW_FORMAT_BGRA5551 = 12, /* 5:5:5:1 (BGRA) */
  AMBA_DRAW_FORMAT_ABGR1555 = 13, /* 1:5:5:5 (ABGR) */
  AMBA_DRAW_FORMAT_ARGB1555 = 14, /* 1:5:5:5 (ARGB) */
  AMBA_DRAW_FORMAT_16BIT_LAST = 15,
  AMBA_DRAW_FORMAT_16BIT_FIRST = AMBA_DRAW_FORMAT_RGB565,

  //32-bits
  AMBA_DRAW_FORMAT_AYUV8888 = 15, /* 8:8:8:8 (AYUV) */
  AMBA_DRAW_FORMAT_RGBA8888 = 16,
  AMBA_DRAW_FORMAT_BGRA8888 = 17,
  AMBA_DRAW_FORMAT_ABGR8888 = 18,
  AMBA_DRAW_FORMAT_ARGB8888 = 19, //!< ARGB8888
  AMBA_DRAW_FORMAT_32BIT_LAST = 20,
  AMBA_DRAW_FORMAT_32BIT_FIRST = AMBA_DRAW_FORMAT_AYUV8888,

};


/*! @enum AMBA_DRAW_DATA_TYPE
 *  @brief This enum lists all data types supported by overlay and framebuffer
 */
enum AMBA_DRAW_DATA_TYPE
{
  AMBA_DRAW_DATA_TYPE_NONE = -1, //!< invalid data type
  AMBA_DRAW_DATA_TYPE_STRING = 0, //!< string type
  AMBA_DRAW_DATA_TYPE_PICTURE, //!< bmp type
  AMBA_DRAW_DATA_TYPE_TIME, //!< timestamp type
  AMBA_DRAW_DATA_TYPE_ANIMATION, //!< animation type
  AMBA_DRAW_DATA_TYPE_LINE, //!< line type
  AMBA_DRAW_DATA_TYPE_RECTANGLE, //!< rectangle type
  AMBA_DRAW_DATA_TYPE_FD, //!< face detect type
  AMBA_DRAW_DATA_TYPE_DRAM, //!< dram type, just supported by framebuffer
  AMBA_DRAW_DATA_TYPE_NUM,//!< type number
};

/*! @enum AMBA_DRAW_PRIORITY
 *  @brief This enum lists all data manipulate mode
 */
enum AMBA_DRAW_PRIORITY
{
  AMBA_DRAW_PRIORITY_INVALID = -1, //!< invalid manipulate mode
  AMBA_DRAW_PRIORITY_FLEXIBILITY = 0, //!< flexible manipulate mode
  AMBA_DRAW_PRIORITY_EFFICIENCY, //!< performance manipulate mode

  AMBA_DRAW_PRIORITY_DEFAULT = AMBA_DRAW_PRIORITY_FLEXIBILITY,
};

/*! @enum AM_OVERLAY_AREA_ROTATE_MODE
 *  @brief It defines overlay area flip or rotate state.
 */
enum AMBA_DRAW_AREA_ROTATE_MODE {
  AMBA_DRAW_AREA_NO_ROTATE = 0, //!< osd area will not auto flip or rotate when
                                 //!< encode stream is flip or rotate state
  AMBA_DRAW_AREA_ALL_ROTATE, //!< osd area will auto flip or rotate when encode
                              //!< stream is flip or rotate state
  AMBA_DRAW_AREA_RECT_STRING_NO_ROTATE, //!< osd area will auto flip or rotate
                                         //!< when encode stream is flip or
                                         //!< rotate state,but rectangle's
                                         //!< string will not auto flip or
                                         //!< rotate.
};

/*! @enum AMBA_DRAW_DATA_CLUSTER_TYPE
 *  @brief This enum lists all data cluster types
 */
enum AMBA_DRAW_DATA_CLUSTER_TYPE
{
  AMBA_DRAW_DATA_CLUSTER_MULTI_INSTANCE_MULTI_COLOR = 0, //!< multi instance and multi color cluster. can add unlimited number line,
                                                      //!< rectangle and fd data type, and each data can have its individual color.
  AMBA_DRAW_DATA_CLUSTER_SINGLE_INSTANCE, //!< single instance cluster. only can add one picture, animation and dram type data.
};

/*! @enum AM_OVERLAY_RTG_TYPE
 *  @brief This enum lists all rectangle types
 */
enum AM_OVERLAY_RTG_TYPE
{
  AMBA_DRAW_RTG_TYPE_NONE = -1, //!< invalid type
  AMBA_DRAW_RTG_TYPE_HOLLOW = 0,//!< hollow rectangle
  AMBA_DRAW_RTG_TYPE_SOLID, //!< solid rectangle
  AMBA_DRAW_RTG_TYPE_NUM,//!< type number
};

/*! @enum AMBA_DRAW_COLOR
 *  @brief This enum lists all drawing colors which has beed defined by oryx
 *  @sa AMDrawingCLUT
 */
enum AMBA_DRAW_COLOR
{
  AMBA_DRAW_COLOR_WHITE  = 0, //!< white
  AMBA_DRAW_COLOR_BLACK, //!< black
  AMBA_DRAW_COLOR_RED, //!< red
  AMBA_DRAW_COLOR_BLUE, //!< blue
  AMBA_DRAW_COLOR_GREEN, //!< green
  AMBA_DRAW_COLOR_YELLOW, //!< yellow
  AMBA_DRAW_COLOR_CYAN, //!< cyan
  AMBA_DRAW_COLOR_MAGENTA, //!< magenta
  AMBA_DRAW_COLOR_CUSTOM, //!< if you want to define color by yourself, please select this option. and then define the color by @ref AMDrawingCLUT

  AMBA_DRAW_COLOR_NUM = AMBA_DRAW_COLOR_CUSTOM, //!< defining the color number
};

typedef struct {
  int x;
  int y;
} amba_point_t;

typedef struct {
  amba_point_t p1;
  amba_point_t p2;
} amba_point_pair_t;

typedef struct {
  int width;
  int height;
} amba_resolution_t;

typedef struct {
  int x;
  int y;
  int width;
  int height;
  int pitch;
} amba_rect_t;

/*! @struct AMDrawingCLUT
 *  @brief This struct defines the yuv color look up table
 */

typedef struct {
  unsigned char v;//!< v component
  unsigned char u;//!< u component
  unsigned char y;//!< y component
  unsigned char a;//!< a component
} amba_draw_clut_t;


/*! @struct AMDrawingARGB
 *  @brief This struct defines the argb parameter
 */
typedef struct {
  unsigned char b; //!< b component, 0
  unsigned char g; //!< g component, 0
  unsigned char r; //!< r component, 0
  unsigned char a; //!< a component, 255
} amba_draw_argb_t;

/*! @struct AMDrawingFont
 *  @brief This struct defines relevant parameters of string font
 */
typedef struct {
  unsigned int width; //!< font width
  unsigned int height; //!< font height
  unsigned int outline_width; //!< outline width
  int ver_bold; //!< setting vertical bold of the font
  int hor_bold; //!< setting horizontal bold of the font
  unsigned int italic; //!< setting the italic of font
  unsigned int disable_anti_alias; //!< disable anti-alias
  char ttf_name[AMBA_DMAX_NAME_LENGTH]; //!< the absolute path of the ttf
} amba_draw_font_t;

/*! @struct AMDrawingColor
 *  @brief This struct defines type of string and time font color
 *  @sa AMBA_DRAW_COLOR
 *  @sa AMDrawingCLUT
 */
typedef struct {
    unsigned int id;//!< 0~7: predefine color: 0(white), 1(black), 2(red), 3(blue), 4(green), 5(yellow), 6(cyan), 7(magenta), 8: custom color set by color value, please refer to @ref AMBA_DRAW_COLOR
    unsigned int enable;
    amba_draw_clut_t color; //!< if the id == 8, please use this parameter to define you color, please refer to @ref AMDrawingCLUT
} amba_draw_color_t;

typedef struct {
  unsigned char background_color_index;
  unsigned char outline_color_index;
  unsigned char color_index;
  unsigned char reserved[3];

  unsigned int m_font_lib_init;
  text2bitmap_t m_bitmap;
} amba_text_bitmap_t;

/*! @struct AMDrawingTextBox
 *  @brief This struct defines the text box parameter of string type
 *  @sa AMDrawingFont
 *  @sa AMDrawingColor
 *  @sa AMDrawingCLUT
 */
typedef struct {
  int spacing;// 0; !< space between two adjacent charactors
  char str[AMBA_DRAW_STRING_MAX_NUM]; //!< the string which was added on overlay or framebuffer
  amba_draw_font_t   font; //!< string font parameter, please refer to @ref AMDrawingFont
  amba_draw_color_t  font_color; //!< string font color parameter, please refer to @ref AMDrawingColor
  amba_draw_clut_t outline_color;//!< defining outline color, please refer to @ref AMDrawingCLUT
  amba_draw_clut_t   background_color;//!< defining backgroud color, please refer to @ref AMDrawingCLUT
  amba_point_pair_t pp; //!< defining rect points, please refer to @ref AMPointPair.just for rect's string.
  unsigned char m_string_is_from_rect;// false;
  unsigned char is_cut_off_string;// false; !< The options WRAPPED and TRUNCATED give you the option to wrap or truncate values if they exceed the specified length.

  amba_text_bitmap_t m_bitmap;
} amba_draw_text_box_t;

/*! @struct AMDrawingTime
 *  @brief This struct defines the parameter of time type
 *  @sa AMDrawingTextBox
 */
typedef struct {
  amba_draw_text_box_t text; //!< the box of time, please refer to @ref AMDrawingTextBox
  char pre_str[AMBA_DRAW_STRING_MAX_NUM];//!< prefix string of timestamp added on overlay or framebuffer
  char suf_str[AMBA_DRAW_STRING_MAX_NUM];//!< suffix string of timestamp added on overlay or framebuffer
  int en_msec;//!< whether to enable msec display for time type
  int format; //!< format of time display
  int is_12h; //!< time style which you can choose between 12h(is_12h=1) and 24h(is_12h=0)
} amba_draw_time_t;

/*! @struct AMDrawingColorKey
 *  @brief This struct defines some parameters about picture transparency
 *  @sa AMDrawingCLUT
 */
typedef struct {
  amba_draw_clut_t color; //!< the central color of transparency, this parameter just for 8bit picture. when color value is in [color-range, color+range], it will do transparent. if you want to know the detail about this struct, please refet to @ref  AMDrawingCLUT
  amba_draw_argb_t argb; //!< the transparency value of 32-bit picture

  unsigned char range; //!< the span of color which will be transparent, this parameter just for 8bit picture
  unsigned short rgb; //!< this parameter defines the 16-bit picture parameters of transparency
  unsigned char reserved;
} amba_draw_color_key_t;

/*! @struct AMDrawingPicture
 *  @brief This struct defines some parameters of picture type
 *  @sa AMDrawingColorKey
 */
typedef struct {
  char filename[AMBA_DMAX_NAME_LENGTH]; //!< picture absolute path
  amba_draw_color_key_t colorkey; //!< color which user wants to be transparent in picture, please refer to @ref AMDrawingColorKey
  unsigned char use_bmp_alpha; //!< use 8bit bmp clut reserved value as alpha, it need use a specify drawing tool to make alpha value to picture. false
  unsigned char alpha; //!< define alpha for all picture pixel data when picture format is 8bit, default value is 255
  unsigned char reserved[2];
} amba_draw_picture_t;


/*! @struct AMDrawingLine
 *  @brief This struct defines line parameters
 *  @sa AMPoint
 *  @sa AMDrawingColor
 */
typedef struct {
  amba_point_t point; //!< the ends position info of the line, please refer to @ref AMPoint
  amba_draw_color_t color; //!< the color of line, please refer to @ref AMDrawingColor
  int thickness; //!< the line thickness
} amba_draw_line_t;

/*! @struct AMDrawingHollowParam
 *  @brief This struct defines hollow rectangle parameters
 *  @sa AMPointPair
 *  @sa AMDrawingColor
 *  @sa AMDrawingFont
 */
typedef struct {
  amba_point_pair_t pp; //!< two points info of rectangle, please refer to @ref AMPointPair
  amba_draw_color_t color; //!< color of rectangle, please refer to @ref AMDrawingColor
  int thickness; //!< thickness of line of rectangle

  char str[AMBA_DRAW_STRING_MAX_NUM]; //!< string which will be added in rectangle
  amba_draw_font_t font; //!< font of string, please refer to @ref AMDrawingFont
  unsigned char is_cut_off_string; //!< To wrap or truncate values if they exceed the specified length. false
  unsigned char reserved[3];
} amba_draw_hollow_param_t;


/*! @struct AMDrawingSolidParam
 *  @brief This struct defines solid rectangle parameter
 *  @sa AMPoint
 *  @sa AMDrawingColor
 */
typedef struct {
  amba_point_t p; //!< the central point info of solid rectangle, please refer to @ref AMPoint
  amba_draw_color_t color; //!< the color info of solid rectangle, please refer to @ref AMDrawingColor
  int thickness; //!< the thickness of the rectangle, it determines the size of the rectangle
} amba_draw_solid_param_t;


/*! @struct AMDrawingRectangle
 *  @brief This struct defines the rectangle info, including hollow and solid rectangle
 *  @sa AMDrawingHollowParam
 *  @sa AMDrawingSolidParam
 */
typedef struct {
  amba_draw_hollow_param_t h_rect; //!< hollow rectangle info
  amba_draw_solid_param_t s_rect;  //!< solid rectangle info
} amba_draw_rectangle_t;


/*! @struct AMDrawingParams
 *  @brief This struct contains all types data of framebuffer and overlay
 *  @sa AMBA_DRAW_DATA_TYPE
 *  @sa AMDrawingTextBox
 *  @sa AMDrawingTime
 *  @sa AMDrawingPicture
 *  @sa AMDrawingAnimation
 *  @sa AMDrawingLine
 *  @sa AMDrawingRectangle
 *  @sa AMDrawingDram
 *  @sa AMRect
 */
typedef struct {
  int type; //!< this option determines which type is added on framebuffer and overlay, please refer to @ref AMBA_DRAW_DATA_TYPE
  amba_draw_text_box_t text;//!< it is used for string type, please refer to @ref AMDrawingTextBox
  amba_draw_time_t time;//!< it is used for time type, please refer to @ref AMDrawingTime
  amba_draw_picture_t pic; //!< it is used for picture type, please refer to @ref AMDrawingPicture
  amba_draw_line_t line;//!< it is used for line, please refer to @ref AMDrawingLine
  amba_draw_rectangle_t rtg;//!< it is used for rectangle, please refer to @ref AMDrawingRectangle
  amba_draw_rectangle_t fd; //!< it is used for face detect, please refer to @ref AMDrawingRectangle
  amba_rect_t rect;//!< data block size and offset in area, please refer to @ref AMRect
} amba_draw_params_t;

typedef struct {
  wchar_t wch;//!< wchar, L'0'
  int width;//!< bitmap width
  int height;//!< bitmap height
  unsigned char *buffer;  //!< bitmap buf address pointed by this pointer
} amba_draw_char_cache_dram_t;

#endif /* _AMBA_DRAW_TYPES_H_ */
