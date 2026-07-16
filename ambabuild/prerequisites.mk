###############################################################################
## prerequisites.mk
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

UNIQUE_NAME_TAG ?= amba_gst_plugins
TMP_STR := $(subst /$(UNIQUE_NAME_TAG), ,$(shell pwd))
#$(info $(TMP_STR))

ifeq ($(words /$(TMP_STR)), 1)
PRJ_PATH ?= $(word 1, /$(TMP_STR))
else
ifeq ($(words /$(TMP_STR)), 2)
PRJ_PATH ?= $(word 1, /$(TMP_STR))
else
$(error there are multiple "amba_gst_plugins" in full path name)
endif
endif

TOP_DIR := $(PRJ_PATH)/$(UNIQUE_NAME_TAG)
OUT_DIR := $(TOP_DIR)/out
#AMB_DIR := $(PRJ_PATH)/..
#SDK_DIR := $(PRJ_PATH)/../..
#AUTO_MW_DIR := $(PRJ_PATH)



export TOP_DIR
export OUT_DIR
#export AMB_DIR
#export SDK_DIR
#export AUTO_MW_DIR
