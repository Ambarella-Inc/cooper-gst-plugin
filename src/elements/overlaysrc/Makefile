###############################################################################
## amba_gst_plugins/src/elements/overlaysrc/Makefile
##
## History:
##   2024/06/03 - [Yang Yu] created file
##
## Copyright (c) 2022 Ambarella International LP
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

include ../../../ambabuild/prerequisites.mk

include $(TOP_DIR)/ambabuild/config.mk
include $(TOP_DIR)/ambabuild/extern_lib.mk

COMPONENT_SRC := $(shell find ./ -name '*.c')
COMPONENT_OBJ := $(COMPONENT_SRC:.c=.o)

COMPONENT_INC := -I./ -I../../../include -I../../internal_include -I../../mm -I../mlinference -I../ambahwclock
COMPONENT_INC += $(EXTERN_LIB_GST_INC) $(EXTERN_LIB_GST_PLUGIN_BASE_INC)
COMPONENT_INC += $(EXTERN_LIB_GLIB2_INC) $(EXTERN_LIB_ALSALIB_INC)
COMPONENT_INC += $(EXTERN_LIB_VPROC_INC) $(AMBA_CAVALRY_INC) $(EXTERN_LIB_NNCTRL_INC) $(EXTERN_LIB_CAVALRY_MEM_INC)
COMPONENT_INC += $(EXTERN_LIB_LWAPPUTILS_INC) $(EXTERN_LIB_TEXT_INSERT_INC)
COMPONENT_INC += $(GENERIC_INC) $(BOARD_INC) $(AMBA_VIDEO_INC)

include $(TOP_DIR)/ambabuild/rules.mk

