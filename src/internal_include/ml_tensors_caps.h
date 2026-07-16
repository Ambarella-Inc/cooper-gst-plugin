/*
 * ml_tensors_caps.h
 *
 * History:
 *    3/27/2026 - [pxdaun] created file
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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Custom GStreamer caps for ML tensor data (mlinference2 -> mlpostprocess).
 * Not nnstreamer's other/tensors - Ambarella-specific format.
 *
 * application/x-amba-ml-tensors
 *   num_tensors: int
 *   dimensions: comma-separated, each "D0:D1:D2:batch" — must match nnctrl/Cavalry layout for that
 *     tensor so mlpostprocess can f16→f32-walk the buffer (height × channels × pitch bytes per row).
 *     For some models (e.g. YOLOv8 seg proto) the three numbers are (C,H,W); do not rewrite caps to
 *     “semantic” W:H:C here or unpack order breaks. YOLO inst-seg uses eazyai_postprocess
 *     yolo_seg_proto_effective_dims() for mask/EasyAI spatial semantics.
 *   types: string (e.g. "float32,float32,float32")
 *   pitches: bytes per row in that unpack (same order as tensors; from nnctrl dim.pitch).
 *   names: string (optional, tensor names for lookup)
 *   nn_input_res: string (optional, e.g. "416x416" — NN input WIDTHxHEIGHT for coord scaling)
 *   format: string, "static" (optional)
 *
 * Buffer: single GstMemory, tensors contiguous; per-tensor size = height * channels * pitch (see
 * gstmlpostprocess chain).
 */

#ifndef __ML_TENSORS_CAPS_H__
#define __ML_TENSORS_CAPS_H__

#define GST_AMBA_ML_TENSORS_CAPS "application/x-amba-ml-tensors"

/* Only macro for max tensor count (mlinference2, mlpostprocess, eazyai). Override: -DAMBA_ML_MAX_TENSORS=32 */
#ifndef AMBA_ML_MAX_TENSORS
#define AMBA_ML_MAX_TENSORS 16
#endif

#endif /* __ML_TENSORS_CAPS_H__ */
