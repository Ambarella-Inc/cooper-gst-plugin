###########################################################
## History:
##    2022/10/14 - [Zhi He] Create
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
###########################################################

#DEPS(amba.mk) libgstamba(jobserver): unselect &&??generic-header &&??ambvideo-header \
	ambcavalry-header &&??prebuild-alsa-lib &&??prebuild-pulseaudio &&??liblwapputils libvproc \
	libnnctrl libcavalrymem &&??prebuild-libpng &&??prebuild-libjpeg-turbo \
	&&??libmcl &&||prebuild-lua &&??prebuild-gstreamer1 &&??prebuild-gstreamer1-plugins-bad \
	&&??prebuild-gstreamer1-plugins-base &&??prebuild-gstreamer1-plugins-good \
	&&??prebuild-gstreamer1-plugins-ugly &&??prebuild-gstreamer1-rtsp-server \
	&&??prebuild-freetype &&??libtextinsert &&??prebuild-aac-new &&??libiav-efm \
	&&??libiav-blur &&??prebuild-opencv &&??libeazyai-postprocess &&??libsei_box\
	&&??prebuild-cyclonedds &&??prebuild-iceoryx-binding-c &&??prebuild-iceoryx-posh &&??prebuild-iceoryx-hoofs

PACKAGE_NAME = libgstamba
PACKAGE_DEPS = generic-header ambvideo-header ambcavalry-header freetype2 liblwapputils libvproc libnnctrl libcavalrymem alsa pulseaudio prebuild-aac-new libpng16 libjpeg-turbo libtextinsert
PACKAGE_DEPS += libmcl lua gstreamer1 gstreamer1-plugins-bad gstreamer1-plugins-base gstreamer1-plugins-good gstreamer1-plugins-ugly gstreamer1-rtsp-server libiav-efm libiav-blur
PACKAGE_DEPS += opencv4 libeazyai-postprocess libsei_box
PACKAGE_DEPS += cyclonedds iceoryx-binding-c iceoryx-posh iceoryx-hoofs

LIBSO_NAME = libgstamba.so

BOARD_CONFIG ?= $(ENV_DEP_ROOT)/usr/include/board/board_config
include $(BOARD_CONFIG)

ifeq ($(ENV_BUILD_MODE), yocto)
INSTALL_TODIR_libso = $(INSTALL_LIBRARIES) /usr/lib/gstreamer-1.0
else
INSTALL_TODIR_libso = $(INSTALL_LIBRARIES) /usr/lib64/gstreamer-1.0
endif

# Public headers for external use (amba_ml_decoded_result.h), same pattern as vproc
INSTALL_HDR = amba-gst-plugins-1.0
INSTALL_HEADERS = ../include/amba_ml_decoded_result.h elements/seiinject/gstambaseimeta.h

##SDK version
SDK_VER_LESS_THAN_030011 := n

##debug
##BUILD_CONFIG_DEBUG := n
##DEBUG := y

##draw text
BUILD_AMBARELLA_GST_DRAW_TEXT := y

CFLAGS += -Wall -I../include -I./internal_include/

##SRCS = $(shell find -name "*.c" | sed "s/\(\.\/\)\(.*\)/\2/g" | xargs)
## common
common_src := common/iav_ctx.c common/bitstream_parse.c common/bitstream_state.c common/decoder_common.c common/cv_vproc.c common/overlay_common.c common/amba_draw_data_area_flags_meta.c

## elements
elements_src := elements/ambahwclock/gstambahwclock.c elements/venccap/gstambavenccap.c elements/venccap_v2/gstambavenccap2.c elements/vencdemux/gstambavencdemux.c
elements_inc := -I elements/ambahwclock -I elements/venccap -I elements/venccap_v2 -I elements/vencdemux
elements_src += elements/heicfilesink/gstambaheicfilesink.c elements/camsrc/gstambacamsrc.c elements/camsrc_v2/gstambacamsrc2.c
elements_inc += -I elements/heicfilesink -I elements/camsrc -I elements/camsrc_v2
elements_src += elements/hwvdec/gstambahwvdec.c elements/hwvdecsrc/gstambahwvdecsrc.c
elements_inc += -I elements/hwvdec -I elements/hwvdecsrc
elements_src += elements/hwvdecv2/gstambahwvdecv2.c elements/hwvdecv2/gstambahwvdecv2_iav.c
elements_inc += -I elements/hwvdecv2
elements_src += elements/vsink/gstambavsink.c elements/mlinference/gstmlinference.c
elements_inc += -I elements/vsink -I elements/mlinference
elements_src += elements/mlinference/yolov3.c elements/mlinference/yolov5.c
elements_src += elements/mlinference/customized_nn_process_factory.c elements/vencoverlaybbox/gstambavencoverlaybbox.c
elements_inc += -I elements/vencoverlaybbox
elements_src += elements/vencoverlay/gstambavencoverlay.c
elements_inc += -I elements/vencoverlay
elements_src += elements/overlaysrc/amba_draw_data_picture.c elements/overlaysrc/amba_draw_data_string.c
elements_src += elements/overlaysrc/amba_draw_data_time.c elements/overlaysrc/gstambaoverlaysrc.c
elements_inc += -I elements/overlaysrc
elements_src += elements/aac/gstambaaacenc.c elements/aac/gstambaaacdec.c
elements_inc += -I elements/aac
elements_src += elements/videoscale/gstambavideoscale.c
elements_inc += -I elements/videoscale
elements_src += elements/amba_efr/gstambaefr.c
elements_inc += -I elements/amba_efr
elements_src += elements/filevenc/gstambafilevenc.c
elements_inc += -I elements/filevenc
elements_src += elements/amba_filemuxer/gstambafilemuxer.c elements/amba_filemuxer/amba_private_data.c
elements_inc += -I elements/amba_filemuxer
elements_src += elements/img_cvt/gstambavprocimgcvt.c
elements_inc += -I elements/img_cvt
elements_src += elements/mlinference_v2/gstmlinference2.c
elements_inc += -I elements/mlinference_v2
elements_src += elements/mlpostprocess/gstmlpostprocess.c elements/mlpostprocess/amba_ml_decoded_meta.c
elements_src += elements/mlpostprocess/ml_postprocess_registry.c elements/mlpostprocess/yolov3.c
elements_src += elements/mlpostprocess/yolov5.c elements/mlpostprocess/yolop.c
elements_src += elements/mlpostprocess/segmentation.c elements/mlpostprocess/classification.c
elements_src += elements/mlpostprocess/rtmpose.c
elements_src += elements/mlpostprocess/clip_image.c
elements_src += elements/mlpostprocess/eazyai_postprocess.c
elements_src += elements/mlpostprocess/retinaface.c
elements_inc += -I elements/mlpostprocess
elements_src += elements/drawdatagen/gstambadrawdatagen.c elements/drawdatagen/drawdatagen_bbox.c elements/drawdatagen/drawdatagen_classification.c
elements_src += elements/drawdatagen/drawdatagen_pose.c
elements_src += elements/drawdatagen/drawdatagen_depth.c
elements_src += elements/drawdatagen/drawdatagen_clip_score.c
elements_src += elements/drawdatagen/drawdatagen_bmp.c elements/drawdatagen/drawdatagen_format.c elements/drawdatagen/drawdatagen_str.c elements/drawdatagen/drawdatagen_time.c
elements_inc += -I elements/drawdatagen
elements_src += elements/overlaydraw/gstambaoverlaydraw.c
elements_inc += -I elements/overlaydraw
elements_src += elements/seiinject/gstambaseiinject.c elements/seiinject/gstambaseidecoder.c elements/seiinject/gstambaseimeta.c
elements_inc += -I elements/seiinject
elements_src += elements/amba_event_recorder/gstambaeventrecorder.c
elements_inc += -I elements/amba_event_recorder
elements_src += elements/vencblur/gstambavencblur.c
elements_inc += -I elements/vencblur

elements_src += elements/amshmem_sink/gstamshmemsink.c elements/amshmem_sink/gst_amshmem_scm.c
elements_src += elements/amshmem_sink/gst_amshmem_phys_mmap.c
elements_src += elements/amshmem_src/gstamshmemsrc.c elements/amshmem_src/gstamshmemsrcbufferpool.c
elements_src += elements/amshmem_sink/gstamshmemcommonslot.c
elements_src += cyclonedds/dds_msgs/AmShMem_Msg.c cyclonedds/dds_msgs/FreeFrame_Msg.c
elements_src += cyclonedds/source/data_publisher.cpp cyclonedds/source/data_subscriber.cpp
elements_inc += -I elements/amshmem_sink -I elements/amshmem_src -I cyclonedds -I cyclonedds/include
elements_src += elements/amba_compositor/gstambacompositor.c
elements_src += elements/amba_compositor/gstambaframesync.c
elements_inc += -I elements/amba_compositor

## gst_utils
gst_utils_src := gst_utils/file_dumper.c

## iav_al
iav_al_src := iav_al/iav_al.c iav_al/linux_device_lcd.c iav_al/iav_al_enc_params.c
iav_al_inc := -I iav_al

## mm
## mm/gst_amba_cavalry_allocator.c
mm_src := mm/amba_direct_mem.c mm/buffer_utils.c mm/gst_amba_cavalry_allocator.c mm/gst_amba_cavalry_bufferpool.c
mm_inc := -I mm

## modified_elements
modified_elements_src := modified_elements/alsasrc/gstmalsa.c modified_elements/alsasrc/gstmalsasrc.c
modified_elements_src += modified_elements/alsasrc/gstmalsaelement.c
modified_elements_src += modified_elements/filesink/gstmfilesink.c modified_elements/filesink/gstmelements_private.c
##modified_elements_src += modified_elements/unixfd/gstmunixfd.c modified_elements/unixfd/gstmunixfdsink.c modified_elements/unixfd/gstmunixfdsrc.c
modified_elements_src += modified_elements/splitmuxsink/gstmsplitmuxsink.c
modified_elements_inc := -I modified_elements/alsasrc -I modified_elements/filesink -I modified_elements/unixfd -I modified_elements/splitmuxsink

## platform_al
platform_al_src := platform_al/clock.c platform_al/amba_hwtimer.c

## plugin
plugin_src := plugin/amba_gst_plugin.c

## simulators
simulators_src := simulators/avc_encoder_file_simulator.c simulators/hevc_encoder_file_simulator.c

## utils
utils_src := utils/debug_log.c utils/utils.c utils/codec_parser.c

## lib/amsei
amsei_src := #lib/amsei/am_sei_inject.c lib/amsei/am_sei_parse.c lib/amsei/am_sei_tlv.c lib/amsei/am_sei_api.c lib/amsei/am_sei_log.c
amsei_inc := #-I lib/amsei

## library

LIB_GST_AMBA_SRC := $(common_src) \
    $(elements_src) \
    $(gst_utils_src) \
    $(iav_al_src) \
    $(mm_src) \
    $(modified_elements_src) \
    $(platform_al_src) \
    $(plugin_src) \
    $(simulators_src) \
    $(utils_src) \
    $(amsei_src)

LIB_GST_AMBA_INC := $(elements_inc) \
    $(iav_al_inc) \
    $(mm_inc) \
    $(modified_elements_inc) \
    $(amsei_inc)

SRCS := $(LIB_GST_AMBA_SRC)
CFLAGS += $(LIB_GST_AMBA_INC)

## -lopus
LDFLAGS += -lpthread -lrt -lm
LDFLAGS += -lgstreamer-1.0 -lgstbase-1.0 -lgstaudio-1.0 -lgstvideo-1.0 -lgstallocators-1.0 -lgstpbutils-1.0
LDFLAGS += -lgobject-2.0 -lgthread-2.0 -lgio-2.0 -lgmodule-2.0 -lglib-2.0
LDFLAGS += -lasound -latopology -lpulse
LDFLAGS += -lvproc -lnnctrl -lcavalry_mem -lmcl -laacenc -laacdec -liav_efm -lsei_box -liav_blur
LDFLAGS += -leazyai_postprocess -lopencv_core -lopencv_imgproc -fopenmp
## cyclonedds
LDFLAGS += -ldds_security_crypto -ldds_security_auth -ldds_security_ac -lddsc -lssl -lcrypto -ldl -lrt -lstdc++
LDFLAGS += -liceoryx_binding_c -liceoryx_posh -liceoryx_hoofs -liceoryx_platform

##ifeq ($(BUILD_AMBARELLA_GST_VPROC), y)
##CFLAGS += -DBUILD_AMBARELLA_GST_VPROC
##LDFLAGS += -lvproc
##endif

##ifeq ($(BUILD_AMBARELLA_GST_CAVALRY), y)
##CFLAGS += -DBUILD_AMBARELLA_GST_CAVALRY
##LDFLAGS +=  -lnnctrl -lcavalry_mem
##endif


ifeq ($(BUILD_AMBARELLA_GST_DRAW_TEXT), y)
CFLAGS += -DBUILD_AMBARELLA_GST_DRAW_TEXT
LDFLAGS += -llwapputils -lfreetype -lbz2 -lz -lpng -ltextinsert_v2
endif

ifeq ($(SDK_VER_LESS_THAN_030011), y)
CFLAGS += -DSDK_VER_LESS_THAN_030011
endif

##ifndef CONFIG_AMBARELLA_AAC_ENC_FULL_SUPPORT
##CFLAGS += -DCONFIG_AMBARELLA_AAC_ENC_FULL_SUPPORT=y
##endif
##ifndef CONFIG_AMBARELLA_AAC_DEC_FULL_SUPPORT
##CFLAGS += -DCONFIG_AMBARELLA_AAC_DEC_FULL_SUPPORT=y
##endif

##CFLAGS += -DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_72

##CFLAGS += -DBUILD_MODULE_AMBA_DSP -DBUILD_DSP_AMBA_V5=1
##CFLAGS += -DAMBA_AMYOC_BUILD -DAMBA_SOC_CV22
CFLAGS += -DBUILD_MODULE_AMBA_DSP
CFLAGS += $(shell echo -DBUILD_DSP_AMBA_$(AMBA_DSP_ARCH) -DBUILD_DSP_AMBA_$(AMBA_SOC) | tr [:lower:] [:upper:])

.PHONY: all clean install

all:
	@echo "Build $(PACKAGE_NAME) Done."

include $(ENV_MAKE_DIR)/inc.app.mk

ifeq ($(BUILD_CONFIG_DEBUG), y)
CFLAGS += -g
else
CFLAGS += -O3
endif

ifeq ($(ENV_BUILD_MODE), yocto)
CFLAGS += -I $(ENV_DEP_ROOT)/usr/include/gstreamer-1.0
CFLAGS += -I $(ENV_DEP_ROOT)/usr/include/glib-2.0 -I $(ENV_DEP_ROOT)/usr/include/gio-unix-2.0
CFLAGS += -I $(ENV_DEP_ROOT)/usr/lib/glib-2.0/include
LDFLAGS += -L $(ENV_DEP_ROOT)/usr/lib/gstreamer-1.0
else
CFLAGS += -I $(ENV_GLO_SYSROOT_DIR)/usr/include/gstreamer-1.0
CFLAGS += -I $(ENV_GLO_SYSROOT_DIR)/usr/include/glib-2.0 -I $(ENV_GLO_SYSROOT_DIR)/usr/include/gio-unix-2.0
CFLAGS += -I $(ENV_GLO_SYSROOT_DIR)/usr/lib64/glib-2.0/include/
LDFLAGS += -L $(ENV_GLO_SYSROOT_DIR)/usr/lib64/gstreamer-1.0
endif

LIBS += libnnctrl.so libcavalry_mem.so libvproc.so liblwapputils.a


all: $(LIB_TARGETS)

clean: clean_objs
	@-rm -f $(LIB_TARGETS)
	@echo "Clean $(PACKAGE_NAME) Done."

install: install_todir_libso install_hdrs
