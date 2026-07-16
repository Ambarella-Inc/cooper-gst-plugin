###############################################################################
## amba_gst_plugins/unit_test/event_recorder/Makefile
##
## History:
##   2026/03/26 - [Yang Yu] created file
##
## Copyright (c) 2026 Ambarella International LP
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

include ../ambabuild/prerequisites.mk

include $(TOP_DIR)/ambabuild/config.mk
include $(TOP_DIR)/ambabuild/extern_lib.mk

test-event-recording_src := test_event_recording.c

COMPONENT_SRC := $(test-event-recording_src)
COMPONENT_OBJ := $(COMPONENT_SRC:.c=.o)

COMPONENT_INC += $(EXTERN_LIB_GST_INC) $(EXTERN_LIB_GST_PLUGIN_BASE_INC)

## unit test
EXECUTABLE_FILES := test-event-recording

UT_LDFLAGS := $(EXTERN_LIB_GST_LIB) $(EXTERN_LIB_GST_PLUGIN_BASE_LIB)

test-event-recording_obj := $(test-event-recording_src:.c=.o)
test-event-recording_ldflag := $(UT_LDFLAGS)

####################################################################
## install
#  helpful usage: sudo make install INSTALL_DIR=/usr
####################################################################
install:
ifeq ($(ENABLE_LYCHEE), n)
endif
	$(INSTALL) -m 0755 -D $(INTERNAL_BIN_DIR)/* -t $(INSTALL_BIN_DIR)/


include $(TOP_DIR)/ambabuild/rules.mk
