###############################################################################
## amba_gst_plugins/src/elements/mlpostprocess/Makefile
##
## History:
##    2/6/2026 - [pxduan] created file
##
## Copyright (c) 2022 Ambarella International LP
##
## Post-processing element for mlinference2 (YOLO NMS, bounding boxes).
##
##################################################################################

default: all

include ../../../ambabuild/prerequisites.mk

include $(TOP_DIR)/ambabuild/config.mk
include $(TOP_DIR)/ambabuild/extern_lib.mk

COMPONENT_SRC := gstmlpostprocess.c amba_ml_decoded_meta.c ml_postprocess_registry.c yolov3.c yolov5.c yolop.c segmentation.c classification.c rtmpose.c clip_image.c retinaface.c eazyai_postprocess.c
COMPONENT_OBJ := $(COMPONENT_SRC:.c=.o)

COMPONENT_INC := -I./ -I../../../include -I../../internal_include -I../../mm -I../amba_filemuxer
COMPONENT_INC += $(EXTERN_LIB_EAZYAI_POSTPROCESS_INC) $(EXTERN_LIB_OPENCV_INC)
COMPONENT_INC += $(EXTERN_LIB_GST_INC) $(EXTERN_LIB_GST_PLUGIN_BASE_INC)
COMPONENT_INC += $(EXTERN_LIB_GLIB2_INC) $(EXTERN_LIB_ALSALIB_INC)
COMPONENT_INC += $(GENERIC_INC)

include $(TOP_DIR)/ambabuild/rules.mk
