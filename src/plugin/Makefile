###############################################################################
## amba_gst_plugins/src/plugin/Makefile
##
## History:
##   2022/10/14 - [Zhi He] created file
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

COMPONENT_SRC := amba_gst_plugin.c
COMPONENT_OBJ := $(COMPONENT_SRC:.c=.o)

COMPONENT_INC := -I../elements/hwvdec -I../elements/hwvdecv2 -I../elements/ambahwclock -I../elements/amba_filemuxer -I../elements/amba_event_recorder
COMPONENT_INC += -I../elements/camsrc -I../elements/camsrc_v2 -I../elements/heicfilesink -I../elements/hwvdecsrc
COMPONENT_INC += -I../elements/amba_efr -I../elements/aac -I../elements/videoscale -I../elements/filevenc -I../elements/img_cvt
COMPONENT_INC += -I../elements/mlinference -I../elements/mlinference_v2 -I../elements/mlpostprocess -I../elements/venccap -I../elements/venccap_v2 -I../elements/vencdemux
COMPONENT_INC += -I../elements/overlaysrc -I../elements/seiinject -I../elements/vencoverlay -I../elements/vencoverlaybbox
COMPONENT_INC += -I../elements/drawdatagen -I../elements/overlaydraw -I../elements/vencblur
COMPONENT_INC += -I../elements/vsink -I../modified_elements/alsasrc -I../modified_elements/filesink -I../modified_elements/unixfd

COMPONENT_INC += -I../elements/amba_compositor
COMPONENT_INC += -I../elements/amshmem_sink -I../elements/amshmem_src
COMPONENT_INC += -I../cyclonedds -I../cyclonedds/include
COMPONENT_INC += $(EXTERN_LIB_CYCLONEDDS_INC)

COMPONENT_INC += -I../modified_elements/splitmuxsink
COMPONENT_INC += $(EXTERN_LIB_GST_INC) $(EXTERN_LIB_GST_PLUGIN_BASE_INC)
COMPONENT_INC += $(EXTERN_LIB_GLIB2_INC) $(EXTERN_LIB_ALSALIB_INC) $(EXTERN_LIB_AMBA_AAC_INC)
COMPONENT_INC += $(EXTERN_LIB_VPROC_INC) $(AMBA_CAVALRY_INC) $(EXTERN_LIB_NNCTRL_INC)
COMPONENT_INC += $(GENERIC_INC) $(EXTERN_LIB_TEXT_INSERT_INC)
COMPONENT_INC += $(EXTERN_LIB_IAV_BLUR_INC) $(EXTERN_LIB_SEI_BOX_INC)

#$(info $(EXTERN_LIB_GST_INC))

include $(TOP_DIR)/ambabuild/rules.mk
