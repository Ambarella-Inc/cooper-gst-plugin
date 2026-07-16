/*
 * utils.h
 *
 * History:
 *    7/31/2015 - [Zhi He] created file
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

#ifndef __UTILS_H__
#define __UTILS_H__

unsigned int gstSkipDelimter (unsigned char *p);
unsigned int gstSkipSEI (unsigned char *p, unsigned int len);
unsigned int gstSkipDelimterHEVC (unsigned char *p);
unsigned int gstSkipSEIHEVC (unsigned char *p, unsigned int len);
void gstFillAmbaH264GopHeader(unsigned char *p_gop_header, unsigned int frame_tick, unsigned int time_scale, unsigned int pts, unsigned char gopsize, unsigned char m);
void gstUpdateAmbaH264GopHeader(unsigned char *p_gop_header, unsigned int pts, unsigned char gopsize);
void gstFillAmbaH265GopHeader(unsigned char *p_gop_header, unsigned int frame_tick, unsigned int time_scale, unsigned int pts, unsigned char gopsize, unsigned char m);
void gstUpdateAmbaH265GopHeader(unsigned char *p_gop_header, unsigned int pts, unsigned char gopsize);
#endif

