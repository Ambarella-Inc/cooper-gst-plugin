###############################################################################
## prerequisites.mk
##
## History:
##   2022/10/11 - [Zhi He] created file
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

UNIQUE_NAME_TAG ?= amba_gst_plugins
BUILDSYSTEM_DIR ?= $(word 1, $(subst /$(UNIQUE_NAME_TAG), ,$(shell pwd)))
DEVICE_NAME_TAG := packages
DEVICE_DIR ?= $(word 1, $(subst /$(DEVICE_NAME_TAG), ,$(BUILDSYSTEM_DIR)))


TOP_DIR		=	$(BUILDSYSTEM_DIR)/$(UNIQUE_NAME_TAG)
OBJ_DIR		=	$(TOP_DIR)/out/CROSS_COMPILE_TARGET_ARCH/objs
BIN_DIR	=	$(TOP_DIR)/out/$(CROSS_COMPILE_TARGET_ARCH)/binary
LIB_DIR	=	$(TOP_DIR)/out/$(CROSS_COMPILE_TARGET_ARCH)/lib/gstreamer-1.0
INC_DIR	=	$(TOP_DIR)/out/$(CROSS_COMPILE_TARGET_ARCH)/include

export BUILDSYSTEM_DIR
export DEVICE_DIR
export UNIQUE_NAME_TAG

export TOP_DIR
export OBJ_DIR
export BIN_DIR
export LIB_DIR
export INC_DIR

