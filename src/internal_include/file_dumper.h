/*
 * file_dumper.h
 *
 * History:
 *    6/13/2022 - [Zhi He] created file
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

#ifndef __FILE_DUMPER_H__
#define __FILE_DUMPER_H__

typedef struct {
  FILE * pf;

  char * filename_base;
  unsigned int file_index;

  char * p_filename_buf;
  unsigned int filename_buf_size;

  unsigned char * p_buf_base;
  unsigned int buf_size;
  unsigned int data_size;
} file_dump_t;

void file_dumper_deinit (file_dump_t * thiz);

int file_dumper_init (file_dump_t * thiz);

int file_dumper_open_new_file (file_dump_t * thiz);

int file_dumper_write_from_buffer(file_dump_t * thiz,
  void * gst_buffer);

int file_dumper_write_from_buffer_v2(file_dump_t * thiz,
  void * gst_buffer, unsigned char is_frame_start);

int file_dumper_write_from_buffer_v3(file_dump_t * thiz,
  void * gst_buffer, unsigned char is_frame_end);

#endif /* __FILE_DUMPER_H__ */

