###############################################################################
## amba_gst_plugins/src/Makefile
##
## History:
##   2022/10/12 - [Zhi He] created file
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

include ../ambabuild/prerequisites.mk

include $(TOP_DIR)/ambabuild/config.mk
include $(TOP_DIR)/ambabuild/extern_lib.mk

SUBDIRS := common cyclonedds elements gst_utils iav_al mm modified_elements platform_al plugin simulators utils

## common
common_src := common/iav_ctx.c common/bitstream_parse.c common/bitstream_state.c common/decoder_common.c common/cv_vproc.c common/overlay_common.c common/amba_draw_data_area_flags_meta.c
common_obj := $(common_src:common/%.c=common/%.o)

## elements
elements_src := elements/ambahwclock/gstambahwclock.c elements/venccap/gstambavenccap.c  elements/venccap_v2/gstambavenccap2.c elements/vencdemux/gstambavencdemux.c
elements_src += elements/heicfilesink/gstambaheicfilesink.c elements/camsrc/gstambacamsrc.c elements/camsrc_v2/gstambacamsrc2.c
elements_src += elements/filevenc/gstambafilevenc.c elements/img_cvt/gstambavprocimgcvt.c
elements_src += elements/hwvdec/gstambahwvdec.c elements/hwvdecsrc/gstambahwvdecsrc.c
elements_src += elements/hwvdecv2/gstambahwvdecv2.c elements/hwvdecv2/gstambahwvdecv2_iav.c
elements_src += elements/vsink/gstambavsink.c elements/mlinference/gstmlinference.c elements/mlinference_v2/gstmlinference2.c
elements_src += elements/mlpostprocess/gstmlpostprocess.c elements/mlpostprocess/amba_ml_decoded_meta.c
elements_src += elements/mlpostprocess/ml_postprocess_registry.c elements/mlpostprocess/yolov3.c
elements_src += elements/mlpostprocess/yolov5.c elements/mlpostprocess/yolop.c elements/mlpostprocess/segmentation.c
elements_src += elements/mlpostprocess/classification.c elements/mlpostprocess/rtmpose.c elements/mlpostprocess/clip_image.c
elements_src += elements/mlpostprocess/retinaface.c elements/mlpostprocess/eazyai_postprocess.c
elements_src += elements/mlinference/yolov3.c elements/mlinference/yolov5.c
elements_src += elements/mlinference/customized_nn_process_factory.c elements/vencoverlaybbox/gstambavencoverlaybbox.c
elements_src += elements/vencoverlay/gstambavencoverlay.c
elements_src += elements/overlaysrc/amba_draw_data_picture.c elements/overlaysrc/amba_draw_data_string.c
elements_src += elements/overlaysrc/amba_draw_data_time.c elements/overlaysrc/gstambaoverlaysrc.c
elements_src += elements/drawdatagen/gstambadrawdatagen.c elements/drawdatagen/drawdatagen_bbox.c
elements_src += elements/drawdatagen/drawdatagen_classification.c elements/drawdatagen/drawdatagen_pose.c elements/drawdatagen/drawdatagen_depth.c elements/drawdatagen/drawdatagen_clip_score.c
elements_src += elements/drawdatagen/drawdatagen_bmp.c elements/drawdatagen/drawdatagen_format.c elements/drawdatagen/drawdatagen_str.c elements/drawdatagen/drawdatagen_time.c
elements_src += elements/overlaydraw/gstambaoverlaydraw.c
elements_src += elements/vencblur/gstambavencblur.c
elements_src += elements/aac/gstambaaacenc.c elements/aac/gstambaaacdec.c elements/amba_efr/gstambaefr.c elements/videoscale/gstambavideoscale.c
elements_src += elements/amba_filemuxer/gstambafilemuxer.c elements/amba_filemuxer/amba_private_data.c
elements_src += elements/amba_event_recorder/gstambaeventrecorder.c
elements_src += elements/seiinject/gstambaseiinject.c elements/seiinject/gstambaseidecoder.c elements/seiinject/gstambaseimeta.c

elements_src += elements/amshmem_sink/gstamshmemsink.c elements/amshmem_sink/gst_amshmem_scm.c
elements_src += elements/amshmem_sink/gst_amshmem_phys_mmap.c elements/amshmem_sink/gstamshmemcommonslot.c
elements_src += elements/amshmem_src/gstamshmemsrc.c
elements_src += elements/amshmem_src/gstamshmemsrcbufferpool.c
elements_src += elements/amba_compositor/gstambacompositor.c elements/amba_compositor/gstambaframesync.c
elements_src += cyclonedds/dds_msgs/AmShMem_Msg.c cyclonedds/dds_msgs/FreeFrame_Msg.c
elements_src += cyclonedds/source/data_publisher.cpp cyclonedds/source/data_subscriber.cpp
elements_obj := $(elements_src:elements/%.c=elements/%.o)
elements_obj := $(elements_obj:cyclonedds/%.c=cyclonedds/%.o)
elements_obj := $(elements_obj:cyclonedds/%.cpp=cyclonedds/%.o)

## gst_utils
gst_utils_src := gst_utils/file_dumper.c
gst_utils_obj := $(gst_utils_src:gst_utils/%.c=gst_utils/%.o)

## iav_al
iav_al_src := iav_al/iav_al.c iav_al/linux_device_lcd.c iav_al/iav_al_enc_params.c
iav_al_obj := $(iav_al_src:iav_al/%.c=iav_al/%.o)

## mm
mm_src := mm/amba_direct_mem.c mm/buffer_utils.c mm/gst_amba_cavalry_allocator.c mm/gst_amba_cavalry_bufferpool.c
mm_obj := $(mm_src:mm/%.c=mm/%.o)

## modified_elements
modified_elements_src := modified_elements/alsasrc/gstmalsa.c modified_elements/alsasrc/gstmalsasrc.c
modified_elements_src += modified_elements/alsasrc/gstmalsaelement.c
modified_elements_src += modified_elements/filesink/gstmfilesink.c modified_elements/filesink/gstmelements_private.c
modified_elements_src += modified_elements/unixfd/gstmunixfd.c modified_elements/unixfd/gstmunixfdsink.c modified_elements/unixfd/gstmunixfdsrc.c
modified_elements_src += modified_elements/splitmuxsink/gstmsplitmuxsink.c
modified_elements_obj := $(modified_elements_src:modified_elements/%.c=modified_elements/%.o)

## platform_al
platform_al_src := platform_al/clock.c platform_al/amba_hwtimer.c
platform_al_obj := $(platform_al_src:platform_al/%.c=platform_al/%.o)

## plugin
plugin_src := plugin/amba_gst_plugin.c
plugin_obj := $(plugin_src:plugin/%.c=plugin/%.o)

## simulators
simulators_src := simulators/avc_encoder_file_simulator.c simulators/hevc_encoder_file_simulator.c
simulators_obj := $(simulators_src:simulators/%.c=simulators/%.o)

## utils
utils_src := utils/debug_log.c utils/utils.c utils/codec_parser.c
utils_obj := $(utils_src:utils/%.c=utils/%.o)


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
    $(utils_src)

LIB_GST_AMBA_OBJ := $(common_obj) \
    $(elements_obj) \
    $(gst_utils_obj) \
    $(iav_al_obj) \
    $(mm_obj) \
    $(modified_elements_obj) \
    $(platform_al_obj) \
    $(plugin_obj) \
    $(simulators_obj) \
    $(utils_obj)

COMPONENT_INC := $(EXTERN_LIB_GST_INC) $(EXTERN_LIB_GST_PLUGIN_BASE_INC) $(EXTERN_LIB_FREETYPE_INC)
COMPONENT_INC += $(EXTERN_LIB_BZIP2_INC) $(EXTERN_LIB_LIBZ_INC) $(EXTERN_LIB_PNG_INC)
COMPONENT_INC += $(EXTERN_LIB_LWAPPUTILS_INC) $(EXTERN_LIB_ALSALIB_INC) $(EXTERN_LIB_GLIB2_INC)
COMPONENT_INC += $(EXTERN_LIB_CAVALRY_MEM_INC) $(EXTERN_LIB_VPROC_INC) $(EXTERN_LIB_NNCTRL_INC)
COMPONENT_INC += $(GENERIC_INC) $(AMBA_CAVALRY_INC) $(EXTERN_LIB_TEXT_INSERT_INC) $(EXTERN_LIB_AMBA_AAC_INC) $(EXTERN_LIB_AMBA_EFM_INC)
COMPONENT_INC += -I$(TOP_DIR)/src/elements/hwvdecv2
COMPONENT_INC += -I$(TOP_DIR)/src/elements/amshmem_sink -I$(TOP_DIR)/src/elements/amshmem_src
COMPONENT_INC += -I$(TOP_DIR)/src/elements/amba_compositor -I$(TOP_DIR)/src/elements/drawdatagen
COMPONENT_INC += -I$(TOP_DIR)/src/elements/overlaydraw -I$(TOP_DIR)/src/elements/vencblur
COMPONENT_INC += -I$(TOP_DIR)/src/elements/seiinject
COMPONENT_INC += -I$(TOP_DIR)/src/cyclonedds -I$(TOP_DIR)/src/cyclonedds/include
COMPONENT_INC += $(EXTERN_LIB_CYCLONEDDS_INC) $(EXTERN_LIB_EAZYAI_POSTPROCESS_INC) $(EXTERN_LIB_OPENCV_INC)
COMPONENT_INC += $(EXTERN_LIB_IAV_BLUR_INC) $(EXTERN_LIB_SEI_BOX_INC)

COMPONENT_LDFLAG := $(EXTERN_LIB_GST_LIB) $(EXTERN_LIB_GST_PLUGIN_BASE_LIB) $(EXTERN_LIB_FREETYPE_LIB)
COMPONENT_LDFLAG += $(EXTERN_LIB_BZIP2_LIB) $(EXTERN_LIB_LIBZ_LIB) $(EXTERN_LIB_PNG_LIB)
COMPONENT_LDFLAG += $(EXTERN_LIB_LWAPPUTILS_LIB) $(EXTERN_LIB_ALSALIB_LIB) $(EXTERN_LIB_GLIB2_LIB)
COMPONENT_LDFLAG += $(EXTERN_LIB_CAVALRY_MEM_LIB) $(EXTERN_LIB_VPROC_LIB) $(EXTERN_LIB_NNCTRL_LIB) $(EXTERN_LIB_MCL_LIB)
COMPONENT_LDFLAG += $(EXTERN_LIB_TEXT_INSERT_LIB) $(EXTERN_LIB_AMBA_AAC_LIB) $(EXTERN_LIB_AMBA_EFM_LIB) $(EXTERN_LIB_CYCLONEDDS_LIB)
COMPONENT_LDFLAG += $(EXTERN_LIB_EAZYAI_POSTPROCESS_LIB) $(EXTERN_LIB_OPENCV_LIB) $(EXTERN_LIB_IAV_BLUR_LIB)
COMPONENT_LDFLAG += $(EXTERN_LIB_SEI_BOX_LIB)

## libgstamba
##STATIC_LIB_NAMES = gstamba
SHARED_LIB_NAMES = gstamba

gstamba_src := $(LIB_GST_AMBA_SRC)
gstamba_obj := $(LIB_GST_AMBA_OBJ)

####################################################################
## install

#  helpful usage: sudo make install INSTALL_DIR=/usr
####################################################################
install:
ifeq ($(ENABLE_LYCHEE), n)
endif
	$(INSTALL) -m 0755 -D $(INTERNAL_LIB_DIR)/* -t $(INSTALL_LIB_DIR)/gstreamer-1.0/


include $(TOP_DIR)/ambabuild/rules.mk
