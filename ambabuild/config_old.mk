###############################################################################
## config.mk
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

MAKE_PRINT		=	@

##Cross compile
ENABLE_CROSS_COMPILE := y
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
TOOLCHAIN_PATH := /usr/local/cortex-a53-2022.08-gcc12.1-linux5.4/bin
endif

TARGRT_CPU_ARCH_NAME := armv8-a
TARGRT_CPU_ARCH_VER_NAME := armv8.0

endif
endif

##SDK version
SDK_VER_LESS_THAN_030011 := n

##debug
BUILD_CONFIG_DEBUG := n

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
BUILD_CONGIG_DSP_AMBA_CV2x := y
BUILD_CONGIG_DSP_AMBA_CV5x := n
BUILD_CONGIG_DSP_AMBA_N1 := n

##CV framework
BUILD_CONGIG_CV_FLEXDAG := n
BUILD_CONGIG_CV_CAVALRY := y
CONFIG_CAVALRY_VERSION := 2

##selected modules
BUILD_AMBARELLA_GST_VPROC := y
BUILD_AMBARELLA_GST_CAVALRY := y
BUILD_AMBARELLA_GST_DRAW_TEXT := y

COMPILE_FLAGS = -I$(TOP_DIR) -I$(TOP_DIR)/include -I$(TOP_DIR)/src/internal_include -I$(DEVICE_DIR)/include -I$(DEVICE_DIR)/include/arch_v5
LINK_FLAGS =

ifeq ($(BUILD_CONFIG_DEBUG), y)
COMPILE_FLAGS += -g
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

ifeq ($(BUILD_AMBARELLA_GST_VPROC), y)
COMPILE_FLAGS += -DBUILD_AMBARELLA_GST_VPROC
endif

ifeq ($(BUILD_AMBARELLA_GST_CAVALRY), y)
COMPILE_FLAGS += -DBUILD_AMBARELLA_GST_CAVALRY
endif

ifeq ($(BUILD_AMBARELLA_GST_DRAW_TEXT), y)
COMPILE_FLAGS += -DBUILD_AMBARELLA_GST_DRAW_TEXT
endif

COMPILE_FLAGS += -DBUILD_MODULE_AMBA_DSP -DBUILD_DSP_AMBA_V5=1

ifeq ($(SDK_VER_LESS_THAN_030011), y)
COMPILE_FLAGS += -DSDK_VER_LESS_THAN_030011
endif

COMPILE_FLAGS += -DAMBA_AMYOC_BUILD -DAMBA_SOC_CV22

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

