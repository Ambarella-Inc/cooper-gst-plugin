###############################################################################
## amba_gst_plugins/src/elements/overlaydraw/Makefile
##
## History:
##    3/3/2026 - [pxduan] created file
##
## Copyright (C) 2022 Ambarella International LP
##
## Sink: draw overlay data onto Ambarella overlay hardware
##################################################################################

default: all

include ../../../ambabuild/prerequisites.mk

include $(TOP_DIR)/ambabuild/config.mk
include $(TOP_DIR)/ambabuild/extern_lib.mk

COMPONENT_SRC := gstambaoverlaydraw.c
COMPONENT_OBJ := $(COMPONENT_SRC:.c=.o)

COMPONENT_INC := -I./ -I../../../include -I../../internal_include -I../../mm -I../amba_filemuxer
COMPONENT_INC += $(EXTERN_LIB_GST_INC) $(EXTERN_LIB_GST_PLUGIN_BASE_INC)
COMPONENT_INC += $(EXTERN_LIB_GLIB2_INC)
COMPONENT_INC += $(EXTERN_LIB_TEXT_INSERT_INC)
COMPONENT_INC += $(GENERIC_INC)

include $(TOP_DIR)/ambabuild/rules.mk
