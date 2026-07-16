###############################################################################
## amba_gst_plugins/unit_test/seiinject/Makefile
##
## Unit test for amba_seiinject element.
##
## Copyright (c) 2025 Ambarella International LP
##################################################################################

default: all

include ../../ambabuild/prerequisites.mk

include $(TOP_DIR)/ambabuild/config.mk
include $(TOP_DIR)/ambabuild/extern_lib.mk

test-seiinject_src := test_seiinject.c
test-sei-meta-appsink_src := test_sei_meta_appsink.c

COMPONENT_SRC := $(test-seiinject_src) $(test-sei-meta-appsink_src)
COMPONENT_OBJ := $(COMPONENT_SRC:.c=.o)

COMPONENT_INC += $(EXTERN_LIB_GST_INC) $(EXTERN_LIB_GST_PLUGIN_BASE_INC)
COMPONENT_INC += $(EXTERN_LIB_SEI_BOX_INC)
COMPONENT_INC += -I../../src/elements/seiinject

## unit test executable
EXECUTABLE_FILES := test-seiinject test-sei-meta-appsink

UT_LDFLAGS := $(EXTERN_LIB_GST_LIB) $(EXTERN_LIB_GST_PLUGIN_BASE_LIB) $(EXTERN_LIB_SEI_BOX_LIB)

test-seiinject_obj := $(test-seiinject_src:.c=.o)
test-seiinject_ldflag := $(UT_LDFLAGS)

test-sei-meta-appsink_obj := $(test-sei-meta-appsink_src:.c=.o)
test-sei-meta-appsink_ldflag := $(UT_LDFLAGS)

####################################################################
## install
####################################################################
install:
ifeq ($(ENABLE_LYCHEE), n)
endif
	$(INSTALL) -m 0755 -D $(INTERNAL_BIN_DIR)/* -t $(INSTALL_BIN_DIR)/

include $(TOP_DIR)/ambabuild/rules.mk
