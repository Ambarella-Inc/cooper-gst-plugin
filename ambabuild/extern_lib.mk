###############################################################################
## extern_lib.mk
##
## History:
##    2023/12/01 - [Yang Yu] created file
##
## Copyright (C) 2022 Ambarella International LP
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

include $(TOP_DIR)/ambabuild/config.mk

ifeq ($(ENABLE_LYCHEE), y)
include $(TOP_DIR)/ambabuild/extern_lib_lychee.mk
else
include $(TOP_DIR)/ambabuild/extern_lib_cross.mk
endif