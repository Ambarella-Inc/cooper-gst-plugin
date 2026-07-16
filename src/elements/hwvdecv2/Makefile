###############################################################################
## amba_gst_plugins/src/elements/hwvdecv2/Makefile
##
## Copyright (c) 2026 Ambarella International LP
##
##################################################################################

default: all

include ../../../ambabuild/prerequisites.mk

include $(TOP_DIR)/ambabuild/config.mk
include $(TOP_DIR)/ambabuild/extern_lib.mk

COMPONENT_SRC := gstambahwvdecv2.c gstambahwvdecv2_iav.c
COMPONENT_OBJ := $(COMPONENT_SRC:.c=.o)

COMPONENT_INC := -I./ -I../ambahwclock -I../amshmem_sink -I../../../include -I../../internal_include -I../../mm -I../../cyclonedds -I../../cyclonedds/include
COMPONENT_INC += $(EXTERN_LIB_GST_INC) $(EXTERN_LIB_GST_PLUGIN_BASE_INC)
COMPONENT_INC += $(EXTERN_LIB_GLIB2_INC)
COMPONENT_INC += $(EXTERN_LIB_CAVALRY_MEM_INC) $(AMBA_CAVALRY_INC)
COMPONENT_INC += $(GENERIC_INC)

include $(TOP_DIR)/ambabuild/rules.mk
