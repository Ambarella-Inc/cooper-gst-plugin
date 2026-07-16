/*
 * iav_ctx.h
 *
 * History:
 *    5/25/2022 - [Zhi He] created file
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

#ifndef __IAV_CTX_H__
#define __IAV_CTX_H__

#include "iav_al.h"

typedef struct {
  amba_dsp_decode_mode_config_t mModeConfig;//enter mode

  amba_dsp_dec_vout_config_t vout_configs[DAMBADSP_MAX_VOUT_NUMBER];//creat decoder

} iav_dec_mode_t;

typedef struct {
  int iav_fd; // fd

  unsigned int iav_fd_opened : 1;
  unsigned int bsb_mapped : 1;
  unsigned int dsp_mapped : 1;
  unsigned int overlay_mapped : 1;
  unsigned int decode_mode_entered : 1;
  unsigned int exit_decode_mode : 1;
  unsigned int dec_bsb_mapped : 1;
  unsigned int reserved : 25;

  unsigned int used_num; // reference counter

  iav_map_bsb_t map_bsb; // bit-stream buffer, encoded bistream
  iav_map_dsp_t map_dsp; // memory for video frames
  iav_map_overlay_t map_overlay;
  iav_map_bsb_t map_dec_bsb; // bit-stream buffer, for bistream to decode

  iav_al_t iav_al; // function pointers

  iav_dec_mode_t dec_mode; // for decoder
} iav_ctx_t;

void cleanup_iav_ctx ();
int setup_iav_ctx ();
iav_ctx_t * acquire_iav_ctx (int auto_setup);
void release_iav_ctx (int auto_cleanup);

#endif

