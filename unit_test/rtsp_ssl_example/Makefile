###############################################################################
## amba_gst_plugins/unit_test/rtsp_ssl_example/Makefile
##
## History:
##   2024/12/06 - [Yang Yu] created file
##
## Copyright (c) 2024 Ambarella International LP
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

test-client_src := simple_verify_tls_interaction.c chain_verify_tls_interaction.c rtsp_config.c test_client.c
test-launch-v3_src := test_launch_v3.c rtsp_config.c

COMPONENT_SRC := $(test-client_src) $(test-launch-v3_src)
COMPONENT_OBJ := $(COMPONENT_SRC:.c=.o)

COMPONENT_INC += $(EXTERN_LIB_GST_INC) $(EXTERN_LIB_GST_PLUGIN_BASE_INC) $(EXTERN_LIB_GST_RTSP_INC)

## unit test
EXECUTABLE_FILES := test-client test-launch-v3

UT_LDFLAGS := $(EXTERN_LIB_GST_LIB) $(EXTERN_LIB_GST_PLUGIN_BASE_LIB) $(EXTERN_LIB_GST_RTSP_LIB)

test-client_obj := $(test-client_src:.c=.o)
test-client_ldflag := $(UT_LDFLAGS)

test-launch-v3_obj := $(test-launch-v3_src:.c=.o)
test-launch-v3_ldflag := $(UT_LDFLAGS)

####################################################################
## install
#  helpful usage: sudo make install INSTALL_DIR=/usr
####################################################################
install:
ifeq ($(ENABLE_LYCHEE), n)
endif
	$(INSTALL) -m 0755 -D $(INTERNAL_BIN_DIR)/* -t $(INSTALL_BIN_DIR)/


include $(TOP_DIR)/ambabuild/rules.mk