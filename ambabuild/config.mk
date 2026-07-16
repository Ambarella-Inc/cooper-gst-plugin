###############################################################################
## config.mk
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

MAKE_PRINT		=	@

##apollo version
#APOLLO_VER := 8
##choose chip platform
CVCHIP := AMBA_SOC_N1

##build in lychee
ENABLE_LYCHEE = y

##Cross compile
ifeq ($(ENABLE_LYCHEE), y)
ENABLE_CROSS_COMPILE := n
else
ENABLE_CROSS_COMPILE := y
endif
CROSS_COMPILE_TARGET_ARCH := AARCH64
CROSS_COMPILE_HOST_ARCH := X86_64

ifeq ($(ENABLE_CROSS_COMPILE), y)
ifeq ($(CROSS_COMPILE_TARGET_ARCH), AARCH64)

ifneq ($(strip $(AARCH64_CROSS_COMPILE)),)
CROSS_COMPILE := $(AARCH64_CROSS_COMPILE)
else
CROSS_COMPILE := "aarch64-linux-gnu-"
endif

ifneq ($(strip $(AARCH64_TOOLCHAIN_PATH)),)
TOOLCHAIN_PATH := $(AARCH64_TOOLCHAIN_PATH)
else
##TOOLCHAIN_PATH := /usr/local/cortex-a76-2022.04-gcc11.2-linux5.15/bin
TOOLCHAIN_PATH := /usr/local/cortex-a76-2022.08-gcc12.1-linux5.15/bin
endif

TARGRT_CPU_ARCH_NAME := armv8-a
TARGRT_CPU_ARCH_VER_NAME := armv8.2

endif
endif


##debug
BUILD_CONFIG_DEBUG = n

##CPU arch and option
BUILD_CONFIG_CPU_ARCH_X86 := n
BUILD_CONFIG_CPU_ARCH_X64 := n
BUILD_CONFIG_CPU_OPT_SSE := n
BUILD_CONFIG_CPU_OPT_AVX := n
BUILD_CONFIG_CPU_OPT_AVX2 := n
BUILD_CONFIG_CPU_ARCH_ARMV7 := n
BUILD_CONFIG_CPU_ARCH_ARMV8 := y
BUILD_CONFIG_CPU_OPT_NEON := y

##OS typs
BUILD_CONGIG_OS_LINUX := y
BUILD_CONGIG_OS_QNX := n
BUILD_CONGIG_OS_THREADX := n
BUILD_CONGIG_OS_AMRTOS := n
BUILD_CONGIG_OS_WINDOWS := n
BUILD_CONGIG_OS_ANDROID := n
BUILD_CONGIG_OS_IOS := n

##OS abstraction layer
BUILD_CONFIG_OSAL_PTHREAD := y
BUILD_CONFIG_OSAL_STDIO := y
BUILD_CONFIG_OSAL_STDLIB := y

##DSP related
BUILD_CONGIG_DSP_AMBA_S5L := n
BUILD_CONGIG_DSP_AMBA_S6Lm := n
BUILD_CONGIG_DSP_AMBA_CV2x := n
BUILD_CONGIG_DSP_AMBA_CV5x := y
BUILD_CONGIG_DSP_AMBA_N1 := n

##CV framework
BUILD_CONGIG_CV_FLEXDAG := y
BUILD_CONGIG_CV_CAVALRY := n

##SW depenancy related
BUILD_CONFIG_SWDEP_CYCLONE_DDS := y
BUILD_CONFIG_SWDEP_FAST_DDS := n

OBJ_DIR = $(OUT_DIR)/$(CROSS_COMPILE_TARGET_ARCH)/objs
INTERNAL_BIN_DIR = $(OUT_DIR)/$(CROSS_COMPILE_TARGET_ARCH)/binary
INTERNAL_LIB_DIR = $(OUT_DIR)/$(CROSS_COMPILE_TARGET_ARCH)/lib
INTERNAL_TAR_DIR = $(OUT_DIR)/$(CROSS_COMPILE_TARGET_ARCH)/tar
INTERNAL_INC_DIR = $(OUT_DIR)/$(CROSS_COMPILE_TARGET_ARCH)/include

## install dir
INSTALL_DIR = $(OUT_DIR)/$(CROSS_COMPILE_TARGET_ARCH)/install/amba_gst_plugins
## bin
INSTALL_BIN_DIR = $(INSTALL_DIR)/bin
## includes
INSTALL_INC_DIR = $(INSTALL_DIR)/include
## libs
INSTALL_LIB_DIR = $(INSTALL_DIR)/lib64
##scripts
INSTALL_SCRIPTS_DIR = $(INSTALL_DIR)/scripts


COMPILE_FLAGS = -I$(TOP_DIR) -I$(TOP_DIR)/include -I$(TOP_DIR)/src/internal_include
#for include from system install location while packaging
COMPILE_FLAGS += -I$(INSTALL_INC_DIR)

LINK_FLAGS = -ldl -lm -lpthread -lrt
#for link from system install location while packaging
LINK_FLAGS += -L$(INSTALL_LIB_DIR)

ifeq ($(BUILD_CONFIG_DEBUG), y)
COMPILE_FLAGS += -g -O0
else
COMPILE_FLAGS += -O3
endif

ifeq ($(BUILD_CONGIG_OS_LINUX), y)
COMPILE_FLAGS += -DBUILD_OS_LINUX
endif

ifeq ($(BUILD_CONGIG_OS_QNX), y)
COMPILE_FLAGS += -DBUILD_OS_QNX
endif

ifeq ($(BUILD_CONGIG_OS_THREADX), y)
COMPILE_FLAGS += -DBUILD_OS_THREADX
endif

ifeq ($(BUILD_CONGIG_OS_AMRTOS), y)
COMPILE_FLAGS += -DBUILD_OS_AMRTOS
endif

ifeq ($(BUILD_CONGIG_OS_WINDOWS), y)
COMPILE_FLAGS += -DBUILD_OS_WINDOWS
endif

ifeq ($(BUILD_CONGIG_OS_ANDROID), y)
COMPILE_FLAGS += -DBUILD_OS_ANDROID
endif

ifeq ($(BUILD_CONGIG_OS_IOS), y)
COMPILE_FLAGS += -DBUILD_OS_IOS
endif

## build in lychee
ifeq ($(ENABLE_LYCHEE), y)
COMPILE_FLAGS += -DBUILD_OS_LYCHEE
endif

##for N1 platform
COMPILE_FLAGS += -D$(CVCHIP)

##for apollo version
#COMPILE_FLAGS += -DAPOLLO_VER=$(APOLLO_VER)

##for amba gst
COMPILE_FLAGS += -DBUILD_MODULE_AMBA_DSP -DBUILD_DSP_AMBA_V6=1 -DBUILD_AMBARELLA_GST_DRAW_TEXT

######################################################################
# toolchain config
######################################################################
ifeq ($(ENABLE_CROSS_COMPILE), y)
CC     = $(TOOLCHAIN_PATH)/$(CROSS_COMPILE)gcc
CXX    = $(TOOLCHAIN_PATH)/$(CROSS_COMPILE)g++
GCC    = $(TOOLCHAIN_PATH)/$(CROSS_COMPILE)gcc
LD     = $(TOOLCHAIN_PATH)/$(CROSS_COMPILE)ld
AS     = $(TOOLCHAIN_PATH)/$(CROSS_COMPILE)as
AR     = $(TOOLCHAIN_PATH)/$(CROSS_COMPILE)ar
STRIP  = $(TOOLCHAIN_PATH)/$(CROSS_COMPILE)strip
RANLIB = $(TOOLCHAIN_PATH)/$(CROSS_COMPILE)ranlib
else
CC     = gcc
CXX    = g++
GCC    = gcc
LD     = ld
AS     = as
AR     = ar
STRIP  = strip
RANLIB = ranlib
endif

export COMPILE_FLAGS
export LINK_FLAGS
#export APOLLO_VER
