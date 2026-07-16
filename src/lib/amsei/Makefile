###############################################################################
## amba_gst_plugins/src/lib/amsei/Makefile
##
## History:
##   2026/04/09 - [Yang Yu] created file
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
##################################################################################

default: all

include ../../../ambabuild/prerequisites.mk

include $(TOP_DIR)/ambabuild/config.mk
include $(TOP_DIR)/ambabuild/extern_lib.mk

COMPONENT_SRC := am_sei_inject.c am_sei_parse.c am_sei_tlv.c am_sei_api.c am_sei_log.c
COMPONENT_OBJ := $(COMPONENT_SRC:.c=.o)

COMPONENT_INC := -I./ -I../../../include -I../../internal_include
COMPONENT_INC += $(EXTERN_LIB_GLIB2_INC)


include $(TOP_DIR)/ambabuild/rules.mk
