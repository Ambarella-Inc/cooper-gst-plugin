/*
 * bistream_state.h
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

#ifndef __BITSTREAM_STATE_H__
#define __BITSTREAM_STATE_H__

typedef struct {
  unsigned int key_frame_comes : 1; // include vps/sps/pps
  unsigned int reserved0 : 7;
  unsigned int slice_num_per_frame : 4;
  unsigned int tile_num_per_frame : 4;
  unsigned int codec_format : 8;
  unsigned int stream_id : 8;

  unsigned int last_slice_id : 4;
  unsigned int last_tile_id : 4;
  unsigned int slice_tile_num : 8;
  unsigned int reserved1 : 16;

  unsigned int width;
  unsigned int height;
  unsigned int framerate_num;
  unsigned int framerate_den;
  unsigned int bitrate;
  float framerate;

  unsigned long last_pts;

} video_bs_state_t;

#endif

