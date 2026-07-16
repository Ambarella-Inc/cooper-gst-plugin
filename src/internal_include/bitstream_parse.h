/*
 * bistream_parse.h
 *
 * History:
 *    5/31/2022 - [Zhi He] created file
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

#ifndef __BITSTREAM_PARSE_H__
#define __BITSTREAM_PARSE_H__

unsigned int avc_get_next_frame(unsigned char * pstart,
  unsigned char * pend, unsigned char * p_nal_type,
  unsigned char * need_more_data, unsigned char * reach_tail);

unsigned int hevc_get_next_frame(unsigned char * pstart,
  unsigned char * pend, unsigned char * p_nal_type,
  unsigned char * need_more_data, unsigned char * reach_tail);

#endif

