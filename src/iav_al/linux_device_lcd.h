/*
 * linux_device_lcd.h
 *
 * History:
 *    5/27/2016 - [Zhi He] created file
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

#ifndef __LINUX_DEVICE_LCD_H__
#define __LINUX_DEVICE_LCD_H__

typedef int (*LCD_SETMODE_FUNC) (int mode, struct amba_video_sink_mode *pcfg);
typedef int (*LCD_POST_SETMODE_FUNC) (int mode);

typedef struct lcd_model {
  const char      *model;
  const LCD_SETMODE_FUNC    lcd_setmode;
  const LCD_POST_SETMODE_FUNC lcd_post_setmode;
} GstLCDModel;

GstLCDModel *gstFindLCDFromName (const char *name);
void gstPrintAvailableLCDModel();
#endif

