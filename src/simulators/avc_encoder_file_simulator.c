/*
 * avc_encoder_file_simulator.c
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

#include "stdlib.h"
#include "stdio.h"
#include "string.h"

#include "common_err_code_c.h"

#include "internal.h"
#include "debug_log.h"

#include "bitstream_parse.h"
#include "encoder_simulator.h"

typedef struct {
  enc_simulator_base_t base;

  FILE * p_file;

  unsigned int start_frame_index;
  unsigned int cur_frame_index;

  unsigned char * p_buf;
  unsigned int buf_size;

  unsigned char * p_data;
  unsigned int tot_data_size;

  unsigned char * p_cur;
  unsigned int remaining_size;
} avc_enc_file_simulator_ctx_t;

static void __clean_avc_encoder_file_simulator(
  avc_enc_file_simulator_ctx_t * thiz)
{
  if (thiz) {
    if (thiz->p_file) {
      fclose(thiz->p_file);
      thiz->p_file = NULL;
    }
    if (thiz->p_buf) {
      free(thiz->p_buf);
      thiz->p_buf = NULL;
    }
    free(thiz);
  }
}

static void destroy_avc_encoder_file_simulator(
  enc_simulator_base_t * ctx)
{
  __clean_avc_encoder_file_simulator((avc_enc_file_simulator_ctx_t * ) ctx);
}

static int encode_sim (enc_simulator_base_t * ctx,
  unsigned char ** pp_data,
  unsigned int * p_size, unsigned int * p_flag)
{
  avc_enc_file_simulator_ctx_t * thiz = (avc_enc_file_simulator_ctx_t * ) ctx;
  unsigned int next_size;
  unsigned char nal_type = 0, need_more_data = 0, reach_tail = 0;

  next_size = avc_get_next_frame(thiz->p_cur,
    thiz->p_cur + thiz->remaining_size, &nal_type,
    &need_more_data, &reach_tail);

  if (next_size) {
    if (thiz->remaining_size < next_size) {
      DPRINT_ERROR("bad next_size %d, remaining size %d\n",
        next_size, thiz->remaining_size);
      return COM_ECODE_CORRUPTED_DATA;
    }
    *pp_data = thiz->p_cur;
    *p_size = next_size;

    thiz->p_cur += next_size;
    thiz->remaining_size -= next_size;
    if (ENalType_IDR == nal_type) {
      *p_flag = D_ENC_SIM_KEY_FRAME;
    } else {
      *p_flag = 0;
    }
  } else {
    if (reach_tail) {
      *pp_data = thiz->p_cur;
      *p_size = thiz->remaining_size;

      thiz->p_cur += thiz->remaining_size;
      thiz->remaining_size = 0;

      DPRINT_NOTICE("end of file\n");
      *p_flag = D_ENC_SIM_EOB;
    } else {
      DPRINT_ERROR("bad tail, remaining size %d\n",
        thiz->remaining_size);
      return COM_ECODE_CORRUPTED_DATA;
    }
  }

  return COM_ECODE_OK;
}

enc_simulator_base_t * create_avc_encoder_file_simulator(const char * filename)
{
  avc_enc_file_simulator_ctx_t * thiz = NULL;
  int failed = 0;
  int ret = 0;

  do {
    thiz = malloc (sizeof(avc_enc_file_simulator_ctx_t));
    if (!thiz) {
      DPRINT_ERROR("no memory\n");
      failed = 1;
      break;
    }
    memset (thiz, 0x0, sizeof(avc_enc_file_simulator_ctx_t));

    thiz->p_file = fopen(filename, "rb");
    if (!thiz->p_file) {
      DPRINT_ERROR("open file (%s) failed\n", filename);
      failed = 1;
      break;
    }

    fseek(thiz->p_file, 0, SEEK_END);
    thiz->buf_size = (unsigned int) ftell(thiz->p_file);
    fseek(thiz->p_file, 0, SEEK_SET);

    thiz->p_buf = (unsigned char *) malloc(thiz->buf_size);
    if (!thiz->p_buf) {
      DPRINT_ERROR("malloc(%d) failed\n", thiz->buf_size);
      failed = 1;
      break;
    }

    thiz->p_data = thiz->p_buf;
    thiz->tot_data_size = thiz->buf_size;
    ret = fread(thiz->p_data, 1, thiz->tot_data_size, thiz->p_file);
    if (ret != (int) thiz->tot_data_size) {
      DPRINT_ERROR("load file (%s) failed, ret %d, file_size %d\n",
        filename, ret, thiz->tot_data_size);
      failed = 1;
      break;
    }
    thiz->p_cur = thiz->p_data;
    thiz->remaining_size = thiz->tot_data_size;
  } while (0);

  if (failed) {
    __clean_avc_encoder_file_simulator(thiz);
    return NULL;
  } else {
    thiz->base.f_encode_sim = encode_sim;
    thiz->base.f_destroy = destroy_avc_encoder_file_simulator;
  }

  return (enc_simulator_base_t *) thiz;
}


