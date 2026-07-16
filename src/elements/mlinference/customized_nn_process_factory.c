/*
 * customized_nn_process_factory.c
 *
 * History:
 *  2022/05/08 - [pxduan] create file
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

#include "common_err_code_c.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <wchar.h>

#include "internal.h"
#include "debug_log.h"
#include "gstmlinference.h"

extern int yolov3_post_process(void *context);

extern int yolov5_post_process(void *context);

int setup_customized_nn_process_factory(priv_ml_infer_ctx_t *thiz, const char *nn_type) {
    if (thiz && nn_type) {
        if (!strcmp(nn_type, EDNNTypeName_YoloV5S)) {
            thiz->model_type = EDNNType_YoloV5_S;
            thiz->func_post_process = yolov5_post_process;
        } else if (!strcmp(nn_type, EDNNTypeName_YoloV5M)) {
            thiz->model_type = EDNNType_YoloV5_M;
            thiz->func_post_process = yolov5_post_process;
        } else if (!strcmp(nn_type, EDNNTypeName_YoloV5L)) {
            thiz->model_type = EDNNType_YoloV5_L;
            thiz->func_post_process = yolov5_post_process;
        } else if (!strcmp(nn_type, EDNNTypeName_YoloV5X)) {
            thiz->model_type = EDNNType_YoloV5_X;
            thiz->func_post_process = yolov5_post_process;
        } else if (!strcmp(nn_type, EDNNTypeName_YoloV3S)) {
            thiz->model_type = EDNNType_YoloV3_S;
            thiz->func_post_process = yolov3_post_process;
        } else if (!strcmp(nn_type, EDNNTypeName_YoloV3M)) {
            thiz->model_type = EDNNType_YoloV3_M;
            thiz->func_post_process = yolov3_post_process;
        } else if (!strcmp(nn_type, EDNNTypeName_YoloV3L)) {
            thiz->model_type = EDNNType_YoloV3_L;
            thiz->func_post_process = yolov3_post_process;
        } else if (!strcmp(nn_type, EDNNTypeName_YoloV3X)) {
            thiz->model_type = EDNNType_YoloV3_X;
            thiz->func_post_process = yolov3_post_process;
        } else {
            DPRINT_WARNING("skip %s post process\n", nn_type);
            return 0;
        }
    } else {
        DPRINT_ERROR("error: bad params\n");
        return -1;
    }

    return 0;
}

