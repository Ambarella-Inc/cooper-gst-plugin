/*
 * yolov3.c
 *
 * History:
 *  2021/6/15 - [pxduan] create file
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <wchar.h>

#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <linux/fb.h>
#include <fcntl.h>
#include <dirent.h>
#include <pthread.h>
#include <math.h>
#include <float.h>

#include "cavalry_ioctl.h"
#include "cavalry_mem.h"
#include "nnctrl.h"
#include "vproc.h"

#include "internal.h"
#include "debug_log.h"
#include "gstmlinference.h"

#ifndef TINY_YOLOV3_FEATURE_MAP_NUM
#define TINY_YOLOV3_FEATURE_MAP_NUM     2
#endif
#ifndef TINY_YOLOV3_ANCHOR_NUM
#define TINY_YOLOV3_ANCHOR_NUM          3
#endif
#ifndef YOLOV3_MIN_WH
#define YOLOV3_MIN_WH                   2
#endif
#ifndef YOLOV3_MAX_WH
#define YOLOV3_MAX_WH                   4096
#endif
#ifndef YOLOV3_MULTI_CLASS_PER_ANCHOR
#define YOLOV3_MULTI_CLASS_PER_ANCHOR   0
#endif


static float const tiny_yolov3_anchors[TINY_YOLOV3_FEATURE_MAP_NUM][TINY_YOLOV3_ANCHOR_NUM][2] = {
    {
        {189, 126}, {137, 236}, {265, 259}   // anchor box of feature map 1
    },
    {
        {26, 48}, {67, 84}, {72, 175}        // anchor box of feature map 2
    }
};


static int __nms(float *x1y1x2y2score, void *aux, size_t aux_element_size, int num, float threshold, int use_iou_min, int top_k,
          float *out_x1y1x2y2score, void *out_aux, int *out_num)
{
    int rval = 0;
    float *area = NULL;
    int *sort = NULL;
    int *status = NULL;
    int i, k;
    int temp;
    int high_ind;
    float area_high;
    float area_i;
    float max_x;
    float max_y;
    float min_x;
    float min_y;
    float iou_width;
    float iou_height;
    float iou_area;
    float iou_ratio;
    int chosen_count = 0;


    if (x1y1x2y2score != NULL && out_x1y1x2y2score != NULL && out_num != NULL) {

        do {

            *out_num = 0;

            if (num <= 0) {
                break;
            }

            area = (float *) malloc(sizeof(float) * num);
            if (area == NULL) {
                DPRINT_ERROR("no memory\n");
                rval = -1;
                break;
            }

            for (i = 0; i < num; i++) {
                area[i] = (x1y1x2y2score[i * 5 + 2] - x1y1x2y2score[i * 5 + 0]) *
                          (x1y1x2y2score[i * 5 + 3] - x1y1x2y2score[i * 5 + 1]);
                area[i] = DCAL_MAX(0, area[i]);
            }

            sort = (int *) malloc(sizeof(int) * num);
            if (sort == NULL) {
                DPRINT_ERROR("no memory\n");
                rval = -1;
                break;
            }
            for (i = 0; i < num; i++) {
                sort[i] = i;
            }

            for (i = 0; i < num - 1; i++) {
                for (k = i + 1; k < num; k++) {
                    if (x1y1x2y2score[sort[i] * 5 + 4] < x1y1x2y2score[sort[k] * 5 + 4]) {
                        temp = sort[i];
                        sort[i] = sort[k];
                        sort[k] = temp;
                    }
                }
            }

            status = (int *) malloc(sizeof(int) * num);
            if (status == NULL) {
                DPRINT_ERROR("no memory\n");
                rval = -1;
                break;
            }
            for (i = 0; i < num; i++) {
                status[i] = NMS_INIT;
            }

            while (1) {
                high_ind = -1;
                for (i = 0; i < num; i++) {
                    if (status[sort[i]] == NMS_INIT) {
                        high_ind = sort[i];
                        break;
                    }
                }

                if (high_ind == -1) {
                    break;
                }

                status[high_ind] = NMS_CHOSEN;
                chosen_count++;
                if (top_k > 0 && chosen_count >= top_k) {
                    break;
                }

                for (i = 0; i < num; i++) {
                    if (status[i] == NMS_INIT) {
                        area_high = area[high_ind];
                        area_i = area[i];
                        max_x = DCAL_MAX(x1y1x2y2score[i * 5 + 0] , x1y1x2y2score[high_ind * 5 + 0]);
                        max_y = DCAL_MAX(x1y1x2y2score[i * 5 + 1] , x1y1x2y2score[high_ind * 5 + 1]);
                        min_x = DCAL_MIN(x1y1x2y2score[i * 5 + 2] , x1y1x2y2score[high_ind * 5 + 2]);
                        min_y = DCAL_MIN(x1y1x2y2score[i * 5 + 3] , x1y1x2y2score[high_ind * 5 + 3]);

                        iou_width = ((min_x - max_x) > 0) ? (min_x - max_x) : 0;
                        iou_height = ((min_y - max_y) > 0) ? (min_y - max_y) : 0;
                        iou_area = iou_width * iou_height;

                        if (use_iou_min) {
                            iou_ratio = iou_area / DCAL_MIN(area_high, area_i);
                        } else { // use iou union
                            iou_ratio = iou_area / (area_high + area_i - iou_area);
                        }

                        if(iou_ratio > threshold) {
                            status[i] = NMS_DISCARD;
                        }
                    }
                }
            }

            for (i = 0; i < num; i++) {
                if (status[sort[i]] == NMS_CHOSEN) {
                    memcpy(&out_x1y1x2y2score[(*out_num) * 5], &x1y1x2y2score[sort[i] * 5], sizeof(float) * 5);
                    if (aux) {
                        memcpy(&((uint8_t *) out_aux)[(*out_num) * aux_element_size], &((uint8_t *) aux)[sort[i] * aux_element_size], aux_element_size);
                    }
                    *out_num += 1;
                }
            }
        } while (0);
    } else {
        DPRINT_ERROR("no memory\n");
        rval = -1;
    }

    if (area) {
        free(area);
        area = NULL;
    }

    if (sort) {
        free(sort);
        sort = NULL;
    }

    if (status) {
        free(status);
        status = NULL;
    }

    return rval;
}

int yolov3_post_process(void *context)
{
    int rval = 0;
    priv_ml_infer_ctx_t *ctx = (priv_ml_infer_ctx_t *) context;
    bounding_boxes_t *result = &ctx->bboxs;
    int feature_map_num = ctx->output_num;
    const float *yolo_anchors = &tiny_yolov3_anchors[0][0][0];
    int class_num = ctx->output_cfg.out_desc[0].dim.depth / TINY_YOLOV3_ANCHOR_NUM - 5;
    int height[feature_map_num];
    int width[feature_map_num];
    int max_det_num_in_class = 0;
    uint8_t *feature_map_data;
    uint8_t *feature_map_data_in_anchor;
    uint8_t *x_data, *y_data, *w_data, *h_data, *box_conf_data, *cls_conf_data;
    uint8_t *x_data_w, *y_data_w, *w_data_w, *h_data_w, *box_conf_data_w, *cls_conf_data_w;
    int shape[4];
    int pitch;
    int stride_w, stride_h;
    float xywhscore[5];
    float cls_conf;
    float **x1y1x2y2score_in_class = NULL;
    int *valid_count_in_class = NULL;
    float **nms_x1y1x2y2score_in_class = NULL;
    int *nms_valid_count_in_class = NULL;
    int i, m, a, c, h, w;
#if YOLOV3_MULTI_CLASS_PER_ANCHOR
#else
    int best_cls;
    float best_cls_conf;
#endif

    if (feature_map_num != TINY_YOLOV3_FEATURE_MAP_NUM) {
        DPRINT_ERROR("yolov3 feature map num should be 2\n");
        return -1;
    }

    do {
        for (m = 0; m < feature_map_num; m++) {
#if ENABLE_CACHE_ON_NET_MEM
            if (ctx->cache_en) {
                cavalry_mem_sync_cache(ctx->output_cfg.out_desc[m].size,
                    ctx->output_cfg.out_desc[m].addr, 0, 1);
            }
#endif
            height[m] = ctx->output_cfg.out_desc[m].dim.height;
            width[m] = ctx->output_cfg.out_desc[m].dim.width;
            max_det_num_in_class += height[m] * width[m] * TINY_YOLOV3_ANCHOR_NUM;
        }

        x1y1x2y2score_in_class = (float **) malloc(class_num * sizeof(float *));
        if (x1y1x2y2score_in_class == NULL) {
            DPRINT_ERROR("memory error\n");
            rval = -1;
            break;
        }
        for (c = 0; c < class_num; c++) {
            x1y1x2y2score_in_class[c] = (float *) malloc(max_det_num_in_class * 5 * sizeof(float));
            if (x1y1x2y2score_in_class[c] == NULL) {
                DPRINT_ERROR("memory error\n");
                rval = -1;
                break;
            }
        }

        valid_count_in_class = (int *) malloc(class_num * sizeof(int));
        if (valid_count_in_class == NULL) {
            DPRINT_ERROR("memory error\n");
            rval = -1;
            break;
        }
        memset(valid_count_in_class, 0, class_num * sizeof(int));

        for (m = 0; m < feature_map_num; m++) {
            shape[0] = ctx->output_cfg.out_desc[m].dim.plane;
            shape[1] = ctx->output_cfg.out_desc[m].dim.depth;
            shape[2] = ctx->output_cfg.out_desc[m].dim.height;
            shape[3] = ctx->output_cfg.out_desc[m].dim.width;
            pitch = ctx->output_cfg.out_desc[m].dim.pitch;
            if (ctx->output_cfg.out_desc[m].data_fmt.size == 1) {
                gushort *f16_data = (gushort *) ctx->output_cfg.out_desc[m].virt;
                gfloat *f32_data = (gfloat *) ctx->nn_out_f32;

                for (i = 0; i < shape[0] * shape[1] * shape[2]; i++) {
                    for (w = 0; w < shape[3]; w++) {
                      *f32_data = *(_Float16 *)(f16_data + w);
                      f32_data++;
                    }
                    f16_data = (gushort *) ((guchar *) f16_data + pitch);
                }

                pitch = shape[3] * sizeof(float);
                feature_map_data = (uint8_t *) ctx->nn_out_f32;
            } else if (ctx->output_cfg.out_desc[m].data_fmt.size == 2) {
                feature_map_data = (uint8_t *) (ctx->output_cfg.out_desc[m].virt);
            } else {
                DPRINT_ERROR("not supported data format %d\n", ctx->output_cfg.out_desc[m].data_fmt.size);
                rval = -1;
                break;
            }

            stride_w = ctx->input_cfg.in_desc[0].dim.width / shape[3];
            stride_h = ctx->input_cfg.in_desc[0].dim.height / shape[2];
            for (a = 0; a < TINY_YOLOV3_ANCHOR_NUM; a++, yolo_anchors+=2) {
                feature_map_data_in_anchor = feature_map_data + shape[2] * pitch * (class_num + 5) * a;
                x_data = feature_map_data_in_anchor;
                y_data = x_data + shape[2] * pitch;
                w_data = y_data + shape[2] * pitch;
                h_data = w_data + shape[2] * pitch;
                box_conf_data = h_data + shape[2] * pitch;
                if (ctx->use_multi_cls) {
                    for (c = 0; c < class_num; c++) {
                        cls_conf_data = box_conf_data + shape[2] * pitch + c * shape[2] * pitch;
                        for (h = 0; h < height[m]; h++) {
                            x_data_w = x_data + h * pitch;
                            y_data_w = y_data + h * pitch;
                            w_data_w = w_data + h * pitch;
                            h_data_w = h_data + h * pitch;
                            x_data_w = x_data + h * pitch;
                            box_conf_data_w = box_conf_data + h * pitch;
                            cls_conf_data_w = cls_conf_data + h * pitch;
                            for (w = 0; w < width[m]; w++) {
                                xywhscore[4] = ((float *) box_conf_data_w)[w];
                                xywhscore[4] = SIGMOID(xywhscore[4]);
                                if (xywhscore[4] > ctx->conf_threshold) {
                                    xywhscore[2] = ((float *) w_data_w)[w];
                                    xywhscore[2] = SIGMOID(xywhscore[2]);
                                    xywhscore[2] = pow(xywhscore[2] * 2.0, 2.0) * yolo_anchors[0];
                                    if (xywhscore[2] > YOLOV3_MIN_WH && xywhscore[2] < YOLOV3_MAX_WH) {
                                        xywhscore[3] = ((float *) h_data_w)[w];
                                        xywhscore[3] = SIGMOID(xywhscore[3]);
                                        xywhscore[3] = pow(xywhscore[3] * 2.0, 2.0) * yolo_anchors[1];
                                        if (xywhscore[3] > YOLOV3_MIN_WH && xywhscore[3] < YOLOV3_MAX_WH) {
                                            cls_conf = ((float *) cls_conf_data_w)[w];
                                            cls_conf = SIGMOID(cls_conf);

                                            xywhscore[4] = xywhscore[4] * cls_conf;
                                            if (xywhscore[4] > ctx->conf_threshold) {
                                                xywhscore[0] = ((float *) x_data_w)[w];
                                                xywhscore[0] = SIGMOID(xywhscore[0]);
                                                xywhscore[0] = (xywhscore[0] * 2.0 - 0.5 + w) * stride_w;

                                                xywhscore[1] = ((float *) y_data_w)[w];
                                                xywhscore[1] = SIGMOID(xywhscore[1]);
                                                xywhscore[1] = (xywhscore[1] * 2.0 - 0.5 + h) * stride_h;
                                                x1y1x2y2score_in_class[c][valid_count_in_class[c] * 5 + 0] = xywhscore[0] - xywhscore[2] / 2.0;
                                                x1y1x2y2score_in_class[c][valid_count_in_class[c] * 5 + 1] = xywhscore[1] - xywhscore[3] / 2.0;
                                                x1y1x2y2score_in_class[c][valid_count_in_class[c] * 5 + 2] = xywhscore[0] + xywhscore[2] / 2.0;
                                                x1y1x2y2score_in_class[c][valid_count_in_class[c] * 5 + 3] = xywhscore[1] + xywhscore[3] / 2.0;
                                                x1y1x2y2score_in_class[c][valid_count_in_class[c] * 5 + 4] = xywhscore[4];
                                                valid_count_in_class[c]++;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                } else {
                    for (h = 0; h < height[m]; h++) {
                        x_data_w = x_data + h * pitch;
                        y_data_w = y_data + h * pitch;
                        w_data_w = w_data + h * pitch;
                        h_data_w = h_data + h * pitch;
                        x_data_w = x_data + h * pitch;
                        box_conf_data_w = box_conf_data + h * pitch;
                        for (w = 0; w < width[m]; w++) {
                            xywhscore[4] = ((float *) box_conf_data_w)[w];
                            xywhscore[4] = SIGMOID(xywhscore[4]);
                            if (xywhscore[4] > ctx->conf_threshold) {
                                xywhscore[2] = ((float *) w_data_w)[w];
                                xywhscore[2] = SIGMOID(xywhscore[2]);
                                xywhscore[2] = pow(xywhscore[2] * 2.0, 2.0) * yolo_anchors[0];
                                if (xywhscore[2] > YOLOV3_MIN_WH && xywhscore[2] < YOLOV3_MAX_WH) {
                                    xywhscore[3] = ((float *) h_data_w)[w];
                                    xywhscore[3] = SIGMOID(xywhscore[3]);
                                    xywhscore[3] = pow(xywhscore[3] * 2.0, 2.0) * yolo_anchors[1];
                                    if (xywhscore[3] > YOLOV3_MIN_WH && xywhscore[3] < YOLOV3_MAX_WH) {
                                        best_cls = 0;
                                        best_cls_conf = -FLT_MAX;
                                        for (c = 0; c < class_num; c++) {
                                            cls_conf_data = box_conf_data + shape[2] * pitch + c * shape[2] * pitch;
                                            cls_conf_data_w = cls_conf_data + h * pitch;
                                            cls_conf = ((float *) cls_conf_data_w)[w];
                                            if (best_cls_conf < cls_conf) {
                                                best_cls_conf = cls_conf;
                                                best_cls = c;
                                            }
                                        }

                                        c = best_cls;
                                        cls_conf = best_cls_conf;
                                        cls_conf = SIGMOID(cls_conf);

                                        xywhscore[4] = xywhscore[4] * cls_conf;
#if 0   // the post process in python code doesn't have the following check on conf_threshold check.
                                        if (xywhscore[4] > ctx->conf_threshold) {
#endif
                                            xywhscore[0] = ((float *) x_data_w)[w];
                                            xywhscore[0] = SIGMOID(xywhscore[0]);
                                            xywhscore[0] = (xywhscore[0] * 2.0 - 0.5 + w) * stride_w;

                                            xywhscore[1] = ((float *) y_data_w)[w];
                                            xywhscore[1] = SIGMOID(xywhscore[1]);
                                            xywhscore[1] = (xywhscore[1] * 2.0 - 0.5 + h) * stride_h;
                                            x1y1x2y2score_in_class[c][valid_count_in_class[c] * 5 + 0] = xywhscore[0] - xywhscore[2] / 2.0;
                                            x1y1x2y2score_in_class[c][valid_count_in_class[c] * 5 + 1] = xywhscore[1] - xywhscore[3] / 2.0;
                                            x1y1x2y2score_in_class[c][valid_count_in_class[c] * 5 + 2] = xywhscore[0] + xywhscore[2] / 2.0;
                                            x1y1x2y2score_in_class[c][valid_count_in_class[c] * 5 + 3] = xywhscore[1] + xywhscore[3] / 2.0;
                                            x1y1x2y2score_in_class[c][valid_count_in_class[c] * 5 + 4] = xywhscore[4];
                                            valid_count_in_class[c]++;
#if 0
                                        }
#endif
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        nms_x1y1x2y2score_in_class = (float **) malloc(class_num * sizeof(float *));
        if (nms_x1y1x2y2score_in_class == NULL) {
            DPRINT_ERROR("no memory\n");
            rval = -1;
            break;
        }
        for (c = 0; c < class_num; c++) {
            nms_x1y1x2y2score_in_class[c] = (float *) malloc(valid_count_in_class[c] * 5 * sizeof(float));
            if (nms_x1y1x2y2score_in_class[c] == NULL) {
                DPRINT_ERROR("no memory\n");
                rval = -1;
                break;
            }
        }

        nms_valid_count_in_class = (int *) malloc(class_num * sizeof(int));
        if (valid_count_in_class == NULL) {
            DPRINT_ERROR("no memory\n");
            rval = -1;
            break;
        }

        for (c = 0; c < class_num; c++) {
            __nms(x1y1x2y2score_in_class[c], NULL, 0, valid_count_in_class[c], ctx->nms_threshold, 0/*use_iou_min*/, ctx->top_k/*tok_k*/,
                  nms_x1y1x2y2score_in_class[c], NULL, &nms_valid_count_in_class[c]);
        }

        result->det_num = 0;
        for (c = 0; c < class_num; c++) {
            for (i = 0; i < nms_valid_count_in_class[c]; i++) {
                if (nms_x1y1x2y2score_in_class[c][i * 5 + 4] > ctx->conf_threshold) {
                    result->detections[result->det_num].id = c;
                    result->detections[result->det_num].score = nms_x1y1x2y2score_in_class[c][i * 5 + 4];

                    result->detections[result->det_num].x_start =
                        nms_x1y1x2y2score_in_class[c][i * 5 + 0] / ctx->input_cfg.in_desc[0].dim.width;
                    result->detections[result->det_num].x_start = DCAL_MAX(0.0, result->detections[result->det_num].x_start);
                    result->detections[result->det_num].x_start = DCAL_MIN(1.0, result->detections[result->det_num].x_start);

                    result->detections[result->det_num].y_start =
                        nms_x1y1x2y2score_in_class[c][i * 5 + 1] / ctx->input_cfg.in_desc[0].dim.height;
                    result->detections[result->det_num].y_start = DCAL_MAX(0.0, result->detections[result->det_num].y_start);
                    result->detections[result->det_num].y_start = DCAL_MIN(1.0, result->detections[result->det_num].y_start);

                    result->detections[result->det_num].x_end =
                        nms_x1y1x2y2score_in_class[c][i * 5 + 2] / ctx->input_cfg.in_desc[0].dim.width;
                    result->detections[result->det_num].x_end =
                        DCAL_MAX(result->detections[result->det_num].x_start, result->detections[result->det_num].x_end);
                    result->detections[result->det_num].x_end =
                        DCAL_MIN(1.0, result->detections[result->det_num].x_end);

                    result->detections[result->det_num].y_end =
                        nms_x1y1x2y2score_in_class[c][i * 5 + 3] / ctx->input_cfg.in_desc[0].dim.height;
                    result->detections[result->det_num].y_end =
                        DCAL_MAX(result->detections[result->det_num].y_start, result->detections[result->det_num].y_end);
                    result->detections[result->det_num].y_end =
                        DCAL_MIN(1.0, result->detections[result->det_num].y_end);

                    strncpy(result->detections[result->det_num].label, ctx->labels[result->detections[result->det_num].id],
                            sizeof(result->detections[result->det_num].label));
                    result->det_num++;
                    if (result->det_num >= DMAX_DET_NUM) {
                        break;
                    }
                }
            }

            if (result->det_num >= DMAX_DET_NUM) {
                break;
            }
        }
    } while (0);

    if (x1y1x2y2score_in_class) {
        for (c = 0; c < class_num; c++) {
            if (x1y1x2y2score_in_class[c]) {
                free(x1y1x2y2score_in_class[c]);
            }
        }

        free(x1y1x2y2score_in_class);
    }

    if (valid_count_in_class) {
        free(valid_count_in_class);
    }

    if (nms_x1y1x2y2score_in_class) {
        for (c = 0; c < class_num; c++) {
            if (nms_x1y1x2y2score_in_class[c]) {
                free(nms_x1y1x2y2score_in_class[c]);
            }
        }

        free(nms_x1y1x2y2score_in_class);
    }

    if (nms_valid_count_in_class) {
        free(nms_valid_count_in_class);
    }

    return rval;
}


