###############################################################################
## amba_gst_plugins/src/elements/drawdatagen/Makefile
##
## History:
##    3/3/2026 - [pxduan] created file
##
## Copyright (C) 2022 Ambarella International LP
##
## Filter: mlpostprocess -> draw data (overlay, blur, etc.)
##################################################################################

default: all

include ../../../ambabuild/prerequisites.mk

include $(TOP_DIR)/ambabuild/config.mk
include $(TOP_DIR)/ambabuild/extern_lib.mk

COMPONENT_SRC := gstambadrawdatagen.c drawdatagen_bbox.c drawdatagen_classification.c drawdatagen_pose.c drawdatagen_depth.c drawdatagen_clip_score.c drawdatagen_bmp.c drawdatagen_format.c drawdatagen_str.c drawdatagen_time.c
COMPONENT_OBJ := $(COMPONENT_SRC:.c=.o)

COMPONENT_INC := -I./ -I../../../include -I../../internal_include -I../../mm -I../overlaysrc -I../amba_filemuxer
COMPONENT_INC += $(EXTERN_LIB_TEXT_INSERT_INC)
COMPONENT_INC += $(EXTERN_LIB_GST_INC) $(EXTERN_LIB_GST_PLUGIN_BASE_INC)
COMPONENT_INC += $(EXTERN_LIB_GLIB2_INC)
COMPONENT_INC += $(GENERIC_INC) $(BOARD_INC) $(AMBA_VIDEO_INC)

include $(TOP_DIR)/ambabuild/rules.mk
