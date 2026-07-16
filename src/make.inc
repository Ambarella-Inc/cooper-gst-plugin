##
##
## History:
##   Jun 3, 2022 - [Zhi He] created file
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

ifeq ($(BUILD_AMBARELLA_GST_PLUGIN), y)

SDK_VER_LESS_THAN_030011 := n

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_TARGET := libgstamba.so

LOCAL_SRCS   := $(LOCAL_PATH)/plugin/amba_gst_plugin.c
LOCAL_SRCS   += $(LOCAL_PATH)/common/iav_ctx.c
LOCAL_SRCS   += $(LOCAL_PATH)/common/bitstream_parse.c
LOCAL_SRCS   += $(LOCAL_PATH)/common/decoder_common.c
##LOCAL_SRCS   += $(LOCAL_PATH)/common/cv_vproc.c
LOCAL_SRCS   += $(LOCAL_PATH)/mm/amba_direct_mem.c
LOCAL_SRCS   += $(LOCAL_PATH)/mm/buffer_utils.c
LOCAL_SRCS   += $(LOCAL_PATH)/gst_utils/file_dumper.c
LOCAL_SRCS   += $(LOCAL_PATH)/iav_al/iav_al.c
LOCAL_SRCS   += $(LOCAL_PATH)/iav_al/linux_device_lcd.c
LOCAL_SRCS   += $(LOCAL_PATH)/iav_al/iav_al_enc_params.c
LOCAL_SRCS   += $(LOCAL_PATH)/utils/debug_log.c
LOCAL_SRCS   += $(LOCAL_PATH)/utils/utils.c
LOCAL_SRCS   += $(LOCAL_PATH)/elements/venccap/gstambavenccap.c
LOCAL_SRCS   += $(LOCAL_PATH)/elements/vencdemux/gstambavencdemux.c
LOCAL_SRCS   += $(LOCAL_PATH)/elements/heicfilesink/gstambaheicfilesink.c
LOCAL_SRCS   += $(LOCAL_PATH)/elements/camsrc/gstambacamsrc.c
LOCAL_SRCS   += $(LOCAL_PATH)/elements/hwvdec/gstambahwvdec.c
LOCAL_SRCS   += $(LOCAL_PATH)/elements/hwvdecsrc/gstambahwvdecsrc.c
LOCAL_SRCS   += $(LOCAL_PATH)/elements/vsink/gstambavsink.c
LOCAL_SRCS   += $(LOCAL_PATH)/elements/mlinference/gstmlinference.c
LOCAL_SRCS   += $(LOCAL_PATH)/elements/mlinference/yolov3.c
LOCAL_SRCS   += $(LOCAL_PATH)/elements/mlinference/yolov5.c
LOCAL_SRCS   += $(LOCAL_PATH)/elements/mlinference/customized_nn_process_factory.c
LOCAL_SRCS   += $(LOCAL_PATH)/elements/vencoverlay/gstambavencoverlay.c
LOCAL_SRCS   += $(LOCAL_PATH)/elements/vencoverlaybbox/gstambavencoverlaybbox.c
LOCAL_SRCS   += $(LOCAL_PATH)/elements/overlaysrc/amba_draw_data_picture.c
LOCAL_SRCS   += $(LOCAL_PATH)/elements/overlaysrc/amba_draw_data_string.c
LOCAL_SRCS   += $(LOCAL_PATH)/elements/overlaysrc/amba_draw_data_time.c
LOCAL_SRCS   += $(LOCAL_PATH)/elements/overlaysrc/gstambaoverlaysrc.c
LOCAL_SRCS   += $(LOCAL_PATH)/elements/ambahwclock/gstambahwclock.c


LOCAL_SRCS   += $(LOCAL_PATH)/simulators/avc_encoder_file_simulator.c
LOCAL_SRCS   += $(LOCAL_PATH)/simulators/hevc_encoder_file_simulator.c
LOCAL_SRCS   += $(LOCAL_PATH)/platform_al/clock.c
LOCAL_SRCS   += $(LOCAL_PATH)/platform_al/amba_hwtimer.c

LOCAL_SRCS   += $(LOCAL_PATH)/modified_elements/alsasrc/gstmalsa.c
LOCAL_SRCS   += $(LOCAL_PATH)/modified_elements/alsasrc/gstmalsasrc.c
LOCAL_SRCS   += $(LOCAL_PATH)/modified_elements/alsasrc/gstmalsaelement.c

LOCAL_CFLAGS := -I$(LOCAL_PATH)/../include                              \
                -I$(LOCAL_PATH)/mm                                                    \
                -I$(LOCAL_PATH)/internal_include                                   \
                -I$(LOCAL_PATH)/elements/ambahwclock                               \
                -I$(LOCAL_PATH)/elements/venccap                               \
                -I$(LOCAL_PATH)/elements/vencdemux                          \
                -I$(LOCAL_PATH)/elements/heicfilesink                           \
                -I$(LOCAL_PATH)/elements/camsrc                           \
                -I$(LOCAL_PATH)/elements/hwvdec                           \
                -I$(LOCAL_PATH)/elements/hwvdecsrc                           \
                -I$(LOCAL_PATH)/elements/vsink                           \
                -I$(LOCAL_PATH)/elements/mlinference                           \
                -I$(LOCAL_PATH)/modified_elements/alsasrc                   \
                -I$(LOCAL_PATH)/elements/vencoverlay                           \
                -I$(LOCAL_PATH)/elements/vencoverlaybbox                           \
                -I$(LOCAL_PATH)/elements/overlaysrc                           \
                -I$(PREBUILD_OSS_DIR)/alsa-lib/include                         \
                -I$(PREBUILD_OSS_DIR)/glib2/include/glib-2.0                \
                -I$(PREBUILD_OSS_DIR)/gstreamer1/include/gstreamer-1.0       \
                -I$(PREBUILD_OSS_DIR)/gstreamer1-plugins-base/include/gstreamer-1.0 \
                -std=c99 -Werror

LOCAL_CFLAGS += -DBUILD_MODULE_AMBA_DSP

ifeq ($(BUILD_AMBARELLA_GST_VPROC), y)
LOCAL_SRCS += $(LOCAL_PATH)/common/cv_vproc.c
endif

ifeq ($(BUILD_AMBARELLA_GST_CAVALRY), y)

## keep compatible before SDK 3.0.11
ifeq ($(SDK_VER_LESS_THAN_030011), y)
LOCAL_CFLAGS += -I$(AMB_TOPDIR)/packages/nnctrl/inc \
                -I$(AMB_TOPDIR)/packages/cavalry_mem/inc \
                -I$(AMB_TOPDIR)/packages/vproc/inc \
                -I$(AMB_TOPDIR)/include/cavalry

LOCAL_CFLAGS += -DSDK_VER_LESS_THAN_030011
else
LOCAL_CFLAGS += -I$(AMB_TOPDIR)/packages/nnctrl/cavalry_v$(CONFIG_CAVALRY_VERSION)/inc \
                -I$(AMB_TOPDIR)/packages/cavalry_mem/inc \
                -I$(AMB_TOPDIR)/packages/vproc/cavalry_v$(CONFIG_CAVALRY_VERSION)/inc \
                -I$(AMB_TOPDIR)/include/cavalry/cavalry_v$(CONFIG_CAVALRY_VERSION)
endif

LOCAL_LIBS += libnnctrl.so libcavalry_mem.so libvproc.so
endif


ifeq ($(AMBARELLA_ARCH), cv2)
LOCAL_CFLAGS += -DBUILD_DSP_AMBA_CV2=1
LOCAL_CFLAGS += -DAMBA_SOC_CV2
LOCAL_CFLAGS += -I$(AMB_TOPDIR)/packages/multi_chan_lua_parser/arch_v5
endif
ifeq ($(AMBARELLA_ARCH), cv22)
LOCAL_CFLAGS += -DBUILD_DSP_AMBA_CV22=1
LOCAL_CFLAGS += -DAMBA_SOC_CV22
LOCAL_CFLAGS += -I$(AMB_TOPDIR)/packages/multi_chan_lua_parser/arch_v5
endif
ifeq ($(AMBARELLA_ARCH), cv25)
LOCAL_CFLAGS += -DBUILD_DSP_AMBA_CV25=1
LOCAL_CFLAGS += -DAMBA_SOC_CV25
LOCAL_CFLAGS += -I$(AMB_TOPDIR)/packages/multi_chan_lua_parser/arch_v5
endif
ifeq ($(AMBARELLA_ARCH), cv25m)
LOCAL_CFLAGS += -DBUILD_DSP_AMBA_CV25=1
LOCAL_CFLAGS += -DAMBA_SOC_CV25
LOCAL_CFLAGS += -I$(AMB_TOPDIR)/packages/multi_chan_lua_parser/arch_v5
endif
ifeq ($(AMBARELLA_ARCH), s6lm)
LOCAL_CFLAGS += -DBUILD_DSP_AMBA_S6LM=1
LOCAL_CFLAGS += -I$(AMB_TOPDIR)/packages/multi_chan_lua_parser/arch_v5
endif
ifeq ($(CONFIG_DRIVER_VERSION_V5), y)
LOCAL_CFLAGS += -DBUILD_DSP_AMBA_V5=1
LOCAL_CFLAGS += -I$(AMB_TOPDIR)/packages/multi_chan_lua_parser/arch_v5
endif
ifeq ($(CONFIG_DRIVER_VERSION_V6), y)
LOCAL_CFLAGS += -DBUILD_DSP_AMBA_V6=1
LOCAL_CFLAGS += -I$(AMB_TOPDIR)/packages/multi_chan_lua_parser/arch_v6
endif


LOCAL_LDFLAGS := -L$(PREBUILD_OSS_DIR)/alsa-lib/usr/lib -lasound -latopology \
                 -L$(PREBUILD_OSS_DIR)/glib2/usr/lib -lgobject-2.0 -lglib-2.0 \
                 -L$(PREBUILD_OSS_DIR)/gstreamer1/usr/lib -lgstreamer-1.0     \
                 -L$(PREBUILD_OSS_DIR)/gstreamer1-plugins-base/usr/lib -lgstbase-1.0 -lgstaudio-1.0 -lgstvideo-1.0 \
                 -Wl,-rpath-link=$(PREBUILD_OSS_DIR)/lua/usr/lib

LOCAL_LIBS += libmcl.so


ifeq ($(BUILD_AMBARELLA_GST_DRAW_TEXT), y)
LOCAL_CFLAGS += -D BUILD_AMBARELLA_GST_DRAW_TEXT
LOCAL_CFLAGS += -I$(AMB_TOPDIR)/packages/lw_app_utils/include
##                -I$(PREBUILD_OSS_DIR)/freetype/include/freetype2/ \
##                -I$(PREBUILD_OSS_DIR)/bzip2/include \
##                -I$(PREBUILD_OSS_DIR)/libpng/include \
##                -I$(PREBUILD_OSS_DIR)/zlib/include
LOCAL_LIBS += liblwapputils.a
LOCAL_LDFLAGS += -lpthread -lrt -lm -lfreetype -lbz2 -lpng -lz
LOCAL_LDFLAGS += -L$(PREBUILD_OSS_DIR)/freetype/usr/lib
LOCAL_LDFLAGS += -L$(PREBUILD_OSS_DIR)/bzip2/usr/lib
LOCAL_LDFLAGS += -L$(PREBUILD_OSS_DIR)/libpng/usr/lib
LOCAL_LDFLAGS += -L$(PREBUILD_OSS_DIR)/zlib/usr/lib
endif

LOCAL_CFLAGS += -I$(AMB_TOPDIR)/packages/textinsert
LOCAL_LIBS += libtextinsert.a

LOCAL_SO_NAME := $(LOCAL_TARGET)

include $(BUILD_APP)

.PHONY: $(LOCAL_TARGET)

$(LOCAL_TARGET): $(LOCAL_MODULE)
	@mkdir -p $(FAKEROOT_DIR)/usr/lib/gstreamer-1.0
	@cp -dpRf $< $(FAKEROOT_DIR)/usr/lib/gstreamer-1.0/
	@echo "Build $@ Done."

$(call add-target-into-build, $(LOCAL_TARGET))

endif

