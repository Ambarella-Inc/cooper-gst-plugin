/*
 * eazyai_postprocess.h
 *
 * History:
 *    3/10/2026 - [pxduan] created file
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
 *
 * libeazyai_postprocess wrapper for mlpostprocess.
 */

#ifndef __EAZYAI_POSTPROCESS_H__
#define __EAZYAI_POSTPROCESS_H__

#include "ea_postproc_common.h"
#include "ea_postproc_yolov8_det.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Register eazyai-backed postprocessors (yolov8_det, etc.) */
void ml_register_eazyai_postprocessors(void);

#ifdef __cplusplus
}
#endif

#endif /* __EAZYAI_POSTPROCESS_H__ */
