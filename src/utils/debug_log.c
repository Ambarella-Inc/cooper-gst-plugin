/*
 * debug_log.c
 *
 * History:
 *    5/24/2022 - [Zhi He] created file
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

#include "debug_log.h"

int g_log_level_agp = (int) DLogLevel_Debug;

void set_log_level_amba_gst_plugin(int log_level)
{
  printf("log level %d\n", log_level);
  g_log_level_agp = (int) log_level;
}

void print_memory_u8(unsigned char *p, unsigned int len)
{
  while (len >= 8) {
    printf("0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x,\n",
      p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
    p += 8;
    len -= 8;
  }

  if (len > 0) {
    while (len > 0) {
      printf("0x%02x, ", p[0]);
      p ++;
      len --;
    }
    printf("\n");
  }
}

void print_memory_ull(unsigned long long *p, unsigned int len)
{
  while (len >= 8) {
    printf("0x%llx, 0x%llx, 0x%llx, 0x%llx, 0x%llx, 0x%llx, 0x%llx, 0x%llx,\n",
      p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
    p += 8;
    len -= 8;
  }

  if (len > 0) {
    while (len > 0) {
      printf("0x%llx, ", p[0]);
      p ++;
      len --;
    }
    printf("\n");
  }
}

void print_memory_u32(unsigned int *p, unsigned int len)
{
  while (len >= 8) {
    printf("0x%08x, 0x%08x, 0x%08x, 0x%08x, 0x%08x, 0x%08x, 0x%08x, 0x%08x,\n",
      p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
    p += 8;
    len -= 8;
  }
  if (len > 0) {
    while (len > 0) {
      printf("0x%08x, ", p[0]);
      p ++;
      len --;
    }
    printf("\n");
  }
}

