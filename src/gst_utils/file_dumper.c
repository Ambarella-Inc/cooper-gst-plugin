/*
 * file_dumper.c
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

#include <gst/gst.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <stdio.h>

#include "common_err_code_c.h"

#include "debug_log.h"

#include "internal.h"

#include "element_common.h"

#include "file_dumper.h"

void file_dumper_deinit (file_dump_t * thiz)
{
  if (thiz->pf) {
    fclose (thiz->pf);
    thiz->pf = NULL;
  }

  if (thiz->p_buf_base) {
    free(thiz->p_buf_base);
    thiz->p_buf_base = NULL;
  }

  if (thiz->filename_base) {
    free(thiz->filename_base);
    thiz->filename_base = NULL;
  }

  if (thiz->p_filename_buf) {
    free(thiz->p_filename_buf);
    thiz->p_filename_buf = NULL;
  }
}

int file_dumper_init (file_dump_t * thiz)
{
  thiz->pf = NULL;

  thiz->filename_base = NULL;
  thiz->file_index = 0;

  thiz->p_filename_buf = NULL;
  thiz->filename_buf_size = 0;

  thiz->p_buf_base = NULL;
  thiz->buf_size = 0;
  thiz->data_size = 0;

  return 0;
}

int file_dumper_open_new_file (file_dump_t * thiz)
{
  char * p_name_base;
  unsigned int name_len;

  if (thiz->pf) {
    fclose(thiz->pf);
    thiz->pf = NULL;
  }

  if (thiz->filename_base) {
    p_name_base = thiz->filename_base;
  } else {
    // default name
    p_name_base = "./amba_%06d.heic";
  }
  name_len = strlen (p_name_base) + 16;
  if ((name_len > thiz->filename_buf_size) || (!thiz->p_filename_buf)) {
    if (thiz->p_filename_buf) {
      free(thiz->p_filename_buf);
      thiz->p_filename_buf = NULL;
      thiz->filename_buf_size = 0;
    }

    thiz->p_filename_buf = malloc (name_len);
    if (!thiz->p_filename_buf) {
      GST_ERROR ("no memory\n");
      return (-1);
    }

    thiz->filename_buf_size = name_len;
  }

  memset(thiz->p_filename_buf, 0x0, thiz->filename_buf_size);
  snprintf(thiz->p_filename_buf, thiz->filename_buf_size,
    p_name_base, thiz->file_index);

  thiz->pf = fopen (thiz->p_filename_buf, "wb+");
  if (!thiz->pf) {
      GST_ERROR ("open file (%s) failed\n", thiz->p_filename_buf);
      return (-2);
  }

  thiz->file_index ++;

  return 0;
}

int file_dumper_write_from_buffer(file_dump_t * thiz,
  void * gst_buffer)
{
  int ret = 0;
  unsigned int i, num_mem;
  unsigned char * p_cur;

  unsigned int tot_size;

  GstMemory * mem;
  GstMapInfo map;

  shared_stream_info_u shared_stream_info;
  int is_start = 0;

  GstBuffer * buffer = (void *) gst_buffer;

  num_mem = gst_buffer_n_memory (buffer);
  tot_size = gst_buffer_get_size (buffer);

  if ((tot_size > thiz->buf_size) || (!thiz->p_buf_base)) {
    if (thiz->p_buf_base) {
      free(thiz->p_buf_base);
      thiz->p_buf_base = NULL;
      thiz->buf_size = 0;
    }

    thiz->p_buf_base = (unsigned char *) malloc (tot_size);
    if (!thiz->p_buf_base) {
      GST_ERROR ("no memory, size %d\n", tot_size);
      return (-1);
    }
  }

  p_cur = thiz->p_buf_base;

  for (i = 0; i < num_mem; ++i) {
    mem = gst_buffer_peek_memory (buffer, i);
    if (gst_memory_map (mem, &map, GST_MAP_READ)) {
      if (map.size) {
        memcpy(p_cur, map.data, map.size);
        p_cur += map.size;
      }
      shared_stream_info.ul_v = (unsigned long) map.user_data[0];
      if (shared_stream_info.info_v.is_frame_start) {
        if (0 == i) {
          is_start = 1;
        } else {
          GST_ERROR ("is_start flag not in first piece of memory\n");
        }
      }

    } else {
      GST_ERROR ("map mem[%d] failed.\n", i);
      return (-2);
    }
    gst_memory_unmap (mem, &map);
  }

  if (is_start) {
    ret = file_dumper_open_new_file (thiz);
    if (0 > ret) {
      GST_ERROR ("open new file failed.\n");
      return (-3);
    }
  }

  fwrite(thiz->p_buf_base, 1, tot_size, thiz->pf);

  return 0;
}

int file_dumper_write_from_buffer_v2(file_dump_t * thiz,
  void * gst_buffer, unsigned char is_frame_start)
{
  int ret = 0;
  unsigned int tot_size;
  GstMapInfo map;
  GstBuffer * buffer = (void *) gst_buffer;

  tot_size = gst_buffer_get_size (buffer);

  if (!gst_buffer_map (buffer, &map, GST_MAP_READ)) {
    GST_ERROR ("Cannot map buffer to read.\n");
    return (-2);
  }

  if (is_frame_start) {
    ret = file_dumper_open_new_file (thiz);
    if (0 > ret) {
      gst_buffer_unmap (buffer, &map);
      GST_ERROR ("open new file failed.\n");
      return (-3);
    }
  }

  if (thiz->pf) {
    fwrite(map.data, 1, tot_size, thiz->pf);
  }
  gst_buffer_unmap (buffer, &map);

  return 0;
}

static int file_dumper_open_file_v3 (file_dump_t * thiz)
{
  char * p_name_base;
  unsigned int name_len;

  if (thiz->pf == NULL) {
    if (thiz->filename_base) {
      p_name_base = thiz->filename_base;
    } else {
      // default name
      p_name_base = "./amba_%06d.heic";
    }
    name_len = strlen (p_name_base) + 16;
    if ((name_len > thiz->filename_buf_size) || (!thiz->p_filename_buf)) {
      if (thiz->p_filename_buf) {
        free(thiz->p_filename_buf);
        thiz->p_filename_buf = NULL;
        thiz->filename_buf_size = 0;
      }

      thiz->p_filename_buf = malloc (name_len);
      if (!thiz->p_filename_buf) {
        GST_ERROR ("no memory\n");
        return (-1);
      }

      thiz->filename_buf_size = name_len;
    }

    memset(thiz->p_filename_buf, 0x0, thiz->filename_buf_size);
    snprintf(thiz->p_filename_buf, thiz->filename_buf_size,
      p_name_base, thiz->file_index);

    thiz->pf = fopen (thiz->p_filename_buf, "wb+");
    if (!thiz->pf) {
        GST_ERROR ("open file (%s) failed\n", thiz->p_filename_buf);
        return (-2);
    }

    thiz->file_index ++;
  }

  return 0;
}

int file_dumper_write_from_buffer_v3(file_dump_t * thiz,
  void * gst_buffer, unsigned char is_frame_end)
{
  int ret = 0;
  unsigned int tot_size;
  GstMapInfo map;
  GstBuffer * buffer = (void *) gst_buffer;

  tot_size = gst_buffer_get_size (buffer);

  if (!gst_buffer_map (buffer, &map, GST_MAP_READ)) {
    GST_ERROR ("Cannot map buffer to read.\n");
    return (-2);
  }

  ret = file_dumper_open_file_v3 (thiz);
  if (0 > ret) {
    gst_buffer_unmap (buffer, &map);
    GST_ERROR ("open file failed.\n");
    return (-3);
  }

  if (thiz->pf) {
    fwrite(map.data, 1, tot_size, thiz->pf);
  }
  gst_buffer_unmap (buffer, &map);

  if (is_frame_end) {
    if (thiz->pf) {
      fclose(thiz->pf);
      thiz->pf = NULL;
    }
  }

  return 0;
}

