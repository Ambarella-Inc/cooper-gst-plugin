###############################################################################
## amba_gst_plugins/src/elements/Makefile
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

SUBDIRS := aac amba_event_recorder amba_efr amba_filemuxer ambahwclock amba_compositor amshmem_sink amshmem_src \
           camsrc camsrc_v2 drawdatagen filevenc heicfilesink hwvdec hwvdecv2 hwvdecsrc mlinference \
           mlinference_v2 mlpostprocess overlaysrc overlaydraw venccap venccap_v2 vencdemux \
           vencoverlay vencoverlaybbox videoscale vsink img_cvt vencblur seiinject

include $(TOP_DIR)/ambabuild/rules.mk
