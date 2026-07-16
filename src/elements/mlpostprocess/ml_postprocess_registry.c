/*
 * ml_postprocess_registry.c
 *
 * History:
 *    3/3/2026 - [pxduan] created file
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
 * Post-processor registry and built-in registrations for mlpostprocess.
 */

#include <string.h>
#include "ml_postprocess_if.h"
#include "gstmlpostprocess.h"
#include "debug_log.h"
#include "eazyai_postprocess.h"

extern void ml_register_yolov5_builtin(void);
extern void ml_register_yolov3_builtin(void);
extern void ml_register_yolop_builtin(void);
extern void ml_register_segmentation(void);
extern void ml_register_classification_builtin(void);
extern void ml_register_rtmpose_builtin(void);
extern void ml_register_clip_image_builtin(void);

extern int mlpp_retinaface_process(void *ctx);

static const ml_postproc_output_pad_spec_t s_retinaface_builtin_pads[] = {
  { .name = "bbox", .kind = ML_POSTPROC_PAD_BBOX },
};

static const ml_postproc_output_pad_spec_t *retinaface_builtin_get_pads(int *count)
{
  *count = 1;
  return s_retinaface_builtin_pads;
}

static const ml_postproc_ops_t mlpp_retinaface_builtin_ops = {
  .name = "retinaface",
  .description = "RetinaFace (merged box/conf + optional landmarks; PriorBox ResNet50 cfg_re50 + decode + NMS)",
  .process = mlpp_retinaface_process,
  .get_result_types = NULL,
  .get_output_pads = retinaface_builtin_get_pads,
  .get_output_layout = NULL,
  .deinit_user_ctx = NULL,
  .output_coords_normalized = FALSE,
};

static void ml_register_retinaface_builtin(void)
{
  ml_register_postproc("retinaface", &mlpp_retinaface_builtin_ops);
}

static void __attribute__((constructor)) ml_register_builtin(void)
{
  ml_register_eazyai_postprocessors();
  ml_register_yolov5_builtin();
  ml_register_yolov3_builtin();
  ml_register_yolop_builtin();
  ml_register_segmentation();
  ml_register_classification_builtin();
  ml_register_rtmpose_builtin();
  ml_register_clip_image_builtin();
  ml_register_retinaface_builtin();
}

#ifndef ML_MAX_REGISTERED
#define ML_MAX_REGISTERED 96  /* eazyai supports + builtin + segmentation + margin */
#endif

typedef struct {
  char name[64];
  const ml_postproc_ops_t *ops;
} ml_reg_entry_t;

static ml_reg_entry_t s_registry[ML_MAX_REGISTERED];
static int s_reg_count = 0;

void ml_register_postproc(const char *name, const ml_postproc_ops_t *ops)
{
  if (!name || !ops || s_reg_count >= ML_MAX_REGISTERED)
    return;
  strncpy(s_registry[s_reg_count].name, name, sizeof(s_registry[s_reg_count].name) - 1);
  s_registry[s_reg_count].name[sizeof(s_registry[s_reg_count].name) - 1] = '\0';
  s_registry[s_reg_count].ops = ops;
  s_reg_count++;
}

const ml_postproc_ops_t *ml_find_postproc(const char *name)
{
  int i;
  if (!name || name[0] == '\0')
    return NULL;
  for (i = 0; i < s_reg_count; i++) {
    if (strcmp(s_registry[i].name, name) == 0)
      return s_registry[i].ops;
  }
  return NULL;
}

void ml_unregister_all_postproc(void)
{
  s_reg_count = 0;
}

int setup_ml_postproc_factory(mlpp_priv_ctx_t *thiz, const char *nn_type)
{
  const ml_postproc_ops_t *ops;

  if (!thiz || !nn_type) {
    DPRINT_ERROR("error: bad params\n");
    return -1;
  }

  /* Deinit previous eazyai ctx if type is changing */
  if (thiz->postprocess_ops && thiz->postprocess_ops->deinit_user_ctx)
    thiz->postprocess_ops->deinit_user_ctx(thiz);

  ops = ml_find_postproc(nn_type);
  if (ops) {
    thiz->func_post_process = ops->process;
    thiz->postprocess_ops = ops;
  } else {
    DPRINT_WARNING("skip %s post process (not registered)\n", nn_type);
    thiz->func_post_process = NULL;
    thiz->postprocess_ops = NULL;
  }

  return 0;
}
