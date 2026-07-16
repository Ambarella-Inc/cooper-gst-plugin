###############################################################################
## amba_gst_plugins/src/iav_al/Makefile
##
## History:
##   2022/10/13 - [Zhi He] created file
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

include ../../ambabuild/prerequisites.mk

include $(TOP_DIR)/ambabuild/config.mk
include $(TOP_DIR)/ambabuild/extern_lib.mk

COMPONENT_SRC := iav_al.c linux_device_lcd.c iav_al_enc_params.c
COMPONENT_OBJ := $(COMPONENT_SRC:.c=.o)

COMPONENT_INC := -I../../include -I../internal_include -I../elements/hwvdec
COMPONENT_INC += $(EXTERN_LIB_GST_INC) $(EXTERN_LIB_GST_PLUGIN_BASE_INC)
COMPONENT_INC += $(EXTERN_LIB_GLIB2_INC) $(EXTERN_LIB_MCL_INC) $(EXTERN_LIB_AMBA_EFM_INC)
COMPONENT_INC += $(EXTERN_LIB_IAV_BLUR_INC)
COMPONENT_INC += $(GENERIC_INC)
COMPONENT_LDFLAG += $(EXTERN_LIB_IAV_BLUR_LIB)

include $(TOP_DIR)/ambabuild/rules.mk

