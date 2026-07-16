/*******************************************************************************
 * osd_draw_defalut_color.h
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

#ifndef _OSD_DRAW_DEFAULT_COLOR_H_
#define _OSD_DRAW_DEFAULT_COLOR_H_

#include "osd_draw_types.h"

static const amba_draw_clut_t AMBA_DEFAULT_DRAW_YUV[] = { { 128, 128, 235, 255 },  //white
                                                     { 128, 128, 12, 255 },   //black
                                                     { 240, 90, 82, 255 },    //red
                                                     { 110, 240, 41, 255 },   //blue
                                                     { 34, 54, 145, 255 },    //green
                                                     { 146, 16, 210, 255 },   //yellow
                                                     { 16, 166, 170, 255 },   //cyan
                                                     { 222, 202, 107, 255 },  //magenta
};

static const amba_draw_clut_t AMBA_DEFAULT_DRAW_RGB[] = { { 255, 255, 255, 255 },  //white
                                                     { 0, 0, 0, 255 },        //black
                                                     { 0, 0, 255, 255 },      //red
                                                     { 255, 0, 0, 255 },      //blue
                                                     { 0, 255, 0, 255 },      //green
                                                     { 0, 255, 255, 255 },    //yellow
                                                     { 255, 255, 0, 255 },    //cyan
                                                     { 255, 0, 255, 255 },    //magenta
};


#endif /* _AM_DRAWING_DEFAULT_COLOR_H_ */
