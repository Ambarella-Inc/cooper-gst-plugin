/*
 * encoder_simulator.h
 *
 * History:
 *    6/5/2022 - [Zhi He] created file
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

#ifndef __ENCODER_SIMULATOR_H__
#define __ENCODER_SIMULATOR_H__

#define D_ENC_SIM_KEY_FRAME 0x01
#define D_ENC_SIM_EXTRA_DATA 0x02
#define D_ENC_SIM_EOS 0x04
#define D_ENC_SIM_EOB 0x08

typedef struct enc_simulator_base_s enc_simulator_base_t;

typedef int (* tf_encode_sim) (enc_simulator_base_t * ctx,
  unsigned char ** pp_data,
  unsigned int * p_size, unsigned int * p_flag);

typedef void (* tf_destroy) (enc_simulator_base_t * ctx);

struct enc_simulator_base_s {
  tf_encode_sim f_encode_sim;
  tf_destroy f_destroy;
};

enc_simulator_base_t * create_avc_encoder_file_simulator(const char * filename);
enc_simulator_base_t * create_hevc_encoder_file_simulator(const char * filename);

#endif

