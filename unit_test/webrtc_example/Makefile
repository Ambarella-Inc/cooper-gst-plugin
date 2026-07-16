###############################################################################
## amba_gst_plugins/unit_test/webrtc_example/Makefile
##
## History:
##   2025/03/24 - [Yang Yu] created file
##
## Copyright (c) 2025 Ambarella International LP
##
## This library is free software; you can redistribute it and/or
## modify it under the terms of the GNU Library General Public
## License as published by the Free Software Foundation; either
## version 2 of the License, or (at your option) any later version.
##
## This library is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
## Library General Public License for more details.
##
## You should have received a copy of the GNU Library General Public
## License along with this library; if not, write to the
## Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
## Boston, MA 02110-1301, USA.
##
##
##################################################################################

default: all

include ../../ambabuild/prerequisites.mk

include $(TOP_DIR)/ambabuild/config.mk
include $(TOP_DIR)/ambabuild/extern_lib.mk

test-webrtc-sendrecv_src := webrtc-sendrecv.c custom_agent.c

COMPONENT_SRC := $(test-webrtc-sendrecv_src)
COMPONENT_OBJ := $(COMPONENT_SRC:.c=.o)

COMPONENT_INC += $(EXTERN_LIB_GST_INC) $(EXTERN_LIB_GST_PLUGIN_BASE_INC) $(EXTERN_LIB_GST_WEBRTC_INC)
COMPONENT_INC += $(EXTERN_LIB_JSON_GLIB_INC) $(EXTERN_LIB_SOUP3_INC)
COMPONENT_INC += -DGST_USE_UNSTABLE_API

## unit test
EXECUTABLE_FILES := test-webrtc-sendrecv

UT_LDFLAGS := $(EXTERN_LIB_GST_LIB) $(EXTERN_LIB_GST_PLUGIN_BASE_LIB) $(EXTERN_LIB_GST_WEBRTC_LIB)
UT_LDFLAGS += $(EXTERN_LIB_JSON_GLIB_LIB) $(EXTERN_LIB_SOUP3_LIB)

test-webrtc-sendrecv_obj := $(test-webrtc-sendrecv_src:.c=.o)
test-webrtc-sendrecv_ldflag := $(UT_LDFLAGS)

####################################################################
## install
#  helpful usage: sudo make install INSTALL_DIR=/usr
####################################################################
install:
ifeq ($(ENABLE_LYCHEE), n)
endif
	$(INSTALL) -m 0755 -D $(INTERNAL_BIN_DIR)/* -t $(INSTALL_BIN_DIR)/


include $(TOP_DIR)/ambabuild/rules.mk
