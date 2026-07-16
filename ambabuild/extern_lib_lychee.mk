###############################################################################
## extern_lib.mk
##
## History:
##   2023/12/01 - [Yang Yu] created file
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

# change this for differnet platforms
AMBACPU := n1

SYS_INC_DIR := $(shell rpm --eval %_includedir)
SYS_LIB_DIR := $(shell rpm --eval %_libdir)

##use pkg-config to get needed flags
PKG-INC := pkg-config --cflags
PKG-LIB := pkg-config --libs

##gstreamer
EXTERN_LIB_GST_INC ?= $(shell $(PKG-INC) gstreamer-1.0)
EXTERN_LIB_GST_LIB ?= $(shell $(PKG-LIB) gstreamer-1.0)

##gstreamer plugin base
EXTERN_LIB_GST_PLUGIN_BASE_INC ?= $(shell $(PKG-INC) gstreamer-plugins-base-1.0)
EXTERN_LIB_GST_PLUGIN_BASE_LIB ?= $(shell $(PKG-LIB) gstreamer-video-1.0) $(shell $(PKG-LIB) gstreamer-audio-1.0)
EXTERN_LIB_GST_PLUGIN_BASE_LIB += $(shell $(PKG-LIB) gstreamer-allocators-1.0) $(shell $(PKG-LIB) gstreamer-pbutils-1.0)

##gstreamer rtsp
EXTERN_LIB_GST_RTSP_INC ?= $(shell $(PKG-INC) gstreamer-rtsp-1.0 gstreamer-rtsp-server-1.0)
EXTERN_LIB_GST_RTSP_LIB ?= $(shell $(PKG-LIB) gstreamer-rtsp-1.0 gstreamer-rtsp-server-1.0)

##gstreamer webrtc
EXTERN_LIB_GST_WEBRTC_INC ?= $(shell $(PKG-INC) gstreamer-rtp-1.0 gstreamer-sdp-1.0 gstreamer-webrtc-1.0 gstreamer-webrtc-nice-1.0)
EXTERN_LIB_GST_WEBRTC_LIB ?= $(shell $(PKG-LIB) gstreamer-rtp-1.0 gstreamer-sdp-1.0 gstreamer-webrtc-1.0 gstreamer-webrtc-nice-1.0)

#json-glib
EXTERN_LIB_JSON_GLIB_INC ?= $(shell $(PKG-INC) json-glib-1.0)
EXTERN_LIB_JSON_GLIB_LIB ?= $(shell $(PKG-LIB) json-glib-1.0)

##libsoup-3.0
EXTERN_LIB_SOUP3_INC ?= $(shell $(PKG-INC) libsoup-3.0)
EXTERN_LIB_SOUP3_LIB ?= $(shell $(PKG-LIB) libsoup-3.0)

##freetype
EXTERN_LIB_FREETYPE_INC ?= $(shell $(PKG-INC) freetype2)
EXTERN_LIB_FREETYPE_LIB ?= $(shell $(PKG-LIB) freetype2)

##bzip2
EXTERN_LIB_BZIP2_INC ?= $(shell $(PKG-INC) bzip2)
EXTERN_LIB_BZIP2_LIB ?= $(shell $(PKG-LIB) bzip2)

##libz
EXTERN_LIB_LIBZ_INC ?= $(shell $(PKG-INC) zlib)
EXTERN_LIB_LIBZ_LIB ?= $(shell $(PKG-LIB) zlib)

##libpng
EXTERN_LIB_PNG_INC ?= $(shell $(PKG-INC) libpng)
EXTERN_LIB_PNG_LIB ?= $(shell $(PKG-LIB) libpng)

##libamba-lwapputils
EXTERN_LIB_LWAPPUTILS_INC ?= $(shell $(PKG-INC) libamba-lwapputils)
EXTERN_LIB_LWAPPUTILS_LIB ?= $(shell $(PKG-LIB) libamba-lwapputils)

##libamba-aac
EXTERN_LIB_AMBA_AAC_INC ?= $(shell $(PKG-INC) libamba-aacenc libamba-aacdec)
EXTERN_LIB_AMBA_AAC_LIB ?= $(shell $(PKG-LIB) libamba-aacenc libamba-aacdec)

##alsa-lib
EXTERN_LIB_ALSALIB_INC ?= $(shell $(PKG-INC) alsa-topology)
EXTERN_LIB_ALSALIB_LIB ?= $(shell $(PKG-LIB) alsa-topology)

##glib2
EXTERN_LIB_GLIB2_INC ?= $(shell $(PKG-INC) glib-2.0 gio-unix-2.0)
EXTERN_LIB_GLIB2_LIB ?= $(shell $(PKG-LIB) glib-2.0)
EXTERN_LIB_GLIB2_LIB += $(shell $(PKG-LIB) gmodule-2.0)
EXTERN_LIB_GLIB2_LIB += $(shell $(PKG-LIB) gio-2.0)
EXTERN_LIB_GLIB2_LIB += $(shell $(PKG-LIB) gthread-2.0)

##ambarella-cooper-n1-sdk-generic-header
GENERIC_INC ?= $(shell $(PKG-INC) $(AMBACPU)-cooper-sdk-generic-header)

##ambarella-n1-sdk-cavalry-header
AMBA_CAVALRY_INC ?= $(shell $(PKG-INC) $(AMBACPU)-cavalry-header)

##amba mcl
EXTERN_LIB_MCL_INC ?= $(shell $(PKG-INC) libamba-mcl-$(AMBACPU))
EXTERN_LIB_MCL_LIB ?= $(shell $(PKG-LIB) libamba-mcl-$(AMBACPU))

##vproc
EXTERN_LIB_VPROC_INC ?= $(shell $(PKG-INC) libamba-vproc-$(AMBACPU))
EXTERN_LIB_VPROC_LIB ?= $(shell $(PKG-LIB) libamba-vproc-$(AMBACPU))

##nnctrl
EXTERN_LIB_NNCTRL_INC ?= $(shell $(PKG-INC) libamba-nnctrl-$(AMBACPU))
EXTERN_LIB_NNCTRL_LIB ?= $(shell $(PKG-LIB) libamba-nnctrl-$(AMBACPU))

##cavalry mem
EXTERN_LIB_CAVALRY_MEM_INC ?= $(shell $(PKG-INC) libamba-cavalry-mem-$(AMBACPU))
EXTERN_LIB_CAVALRY_MEM_LIB ?= $(shell $(PKG-LIB) libamba-cavalry-mem-$(AMBACPU))

##libtextinsert
EXTERN_LIB_TEXT_INSERT_INC ?= $(shell $(PKG-INC) libamba-textinsert_v2)
EXTERN_LIB_TEXT_INSERT_LIB ?= $(shell $(PKG-LIB) libamba-textinsert_v2)

##libamba-efm
EXTERN_LIB_AMBA_EFM_INC ?= $(shell $(PKG-INC) libamba-efm-$(AMBACPU))
EXTERN_LIB_AMBA_EFM_LIB ?= $(shell $(PKG-LIB) libamba-efm-$(AMBACPU))

##cyclonedds
EXTERN_LIB_CYCLONEDDS_INC ?= $(shell $(PKG-INC) CycloneDDS)
EXTERN_LIB_CYCLONEDDS_LIB ?= $(shell $(PKG-LIB) CycloneDDS)

##libamba-eazyai-postprocess
EXTERN_LIB_EAZYAI_POSTPROCESS_INC ?= $(shell $(PKG-INC) libamba-eazyai-postprocess-$(AMBACPU))
EXTERN_LIB_EAZYAI_POSTPROCESS_LIB ?= $(shell $(PKG-LIB) libamba-eazyai-postprocess-$(AMBACPU))

##opencv
EXTERN_LIB_OPENCV_INC ?= $(shell $(PKG-INC) opencv)
EXTERN_LIB_OPENCV_LIB ?= $(shell $(PKG-LIB) opencv)

##libiav-blur
EXTERN_LIB_IAV_BLUR_INC ?= $(shell $(PKG-INC) libiav-blur-$(AMBACPU))
EXTERN_LIB_IAV_BLUR_LIB ?= $(shell $(PKG-LIB) libiav-blur-$(AMBACPU))

##libamba-sei-box
EXTERN_LIB_SEI_BOX_INC ?= $(shell $(PKG-INC) libamba-sei-box)
EXTERN_LIB_SEI_BOX_LIB ?= $(shell $(PKG-LIB) libamba-sei-box)


##################################################################################
## BELOW NOT USED IN LYCHEE !
##################################################################################

##board
BOARD_INC := -I$(SYS_INC_DIR)/board

##amba video
AMBA_VIDEO_INC := -I$(SYS_INC_DIR)/ambvideo-header

##pulseaudio
EXTERN_LIB_PULSEAUDIO_INC := -I$(SYS_INC_DIR)/pulseaudio
EXTERN_LIB_PULSEAUDIO_LIB := -L$(SYS_LIB_DIR) -lpulse

##libopus
EXTERN_LIB_OPUS_INC := -I$(SYS_INC_DIR)/opus
EXTERN_LIB_OPUS_LIB := -L$(SYS_LIB_DIR) -lopus

##libsndfile
EXTERN_LIB_SNDFILE_INC := -I$(SYS_INC_DIR)/libsndfile
EXTERN_LIB_SNDFILE_LIB := -L$(SYS_LIB_DIR) -lsndfile

##taglib
EXTERN_LIB_TAGLIB_INC := -I$(SYS_INC_DIR)/taglib
EXTERN_LIB_TAGLIB_LIB := -L$(SYS_LIB_DIR) -ltag

##nettle
EXTERN_LIB_NETTLE_INC := -I$(SYS_INC_DIR)/nettle3
EXTERN_LIB_NETTLE_LIB := -L$(SYS_LIB_DIR) -lnettle

##lame
EXTERN_LIB_LAME_INC := -I$(SYS_INC_DIR)/lame
EXTERN_LIB_LAME_LIB := -L$(SYS_LIB_DIR) -lmp3lame

##libsrtp
EXTERN_LIB_SRTP_INC := -I$(SYS_INC_DIR)/libsrtp
EXTERN_LIB_SRTP_LIB := -L$(SYS_LIB_DIR) -lsrtp2

##wavpack
EXTERN_LIB_WAVPACK_INC := -I$(SYS_INC_DIR)/wavpack
EXTERN_LIB_WAVPACK_LIB := -L$(SYS_LIB_DIR) -lwavpack

##libogg
EXTERN_LIB_OGG_INC := -I$(SYS_INC_DIR)/libogg
EXTERN_LIB_OGG_LIB := -L$(SYS_LIB_DIR) -logg

##openssl
EXTERN_LIB_OPENSSL_INC := -I$(SYS_INC_DIR)/openssl
EXTERN_LIB_OPENSSL_LIB := -L$(SYS_LIB_DIR) -lcrypto

##faad2
EXTERN_LIB_FAAD2_INC := -I$(SYS_INC_DIR)/faad2
EXTERN_LIB_FAAD2_LIB := -L$(SYS_LIB_DIR) -lfaad

##mpg123
EXTERN_LIB_MPG123_INC := -I$(SYS_INC_DIR)/mpg123
EXTERN_LIB_MPG123_LIB := -L$(SYS_LIB_DIR) -lmpg123

##webrtc-audio-processing
EXTERN_LIB_WEBRTC_INC := -I$(SYS_INC_DIR)/webrtc-audio-processing
EXTERN_LIB_WEBRTC_LIB := -L$(SYS_LIB_DIR) -lwebrtc_audio_processing

##libNE10
EXTERN_LIB_NE10_INC := -I$(SYS_INC_DIR)/libNE10
EXTERN_LIB_NE10_LIB := -L$(SYS_LIB_DIR) -lNE10

##systemd
EXTERN_LIB_SYSTEMD_INC := -I$(SYS_INC_DIR)/systemd
EXTERN_LIB_SYSTEMD_LIB := -L$(SYS_LIB_DIR) -lsystemd

##libcurl
EXTERN_LIB_CURL_INC := -I$(SYS_INC_DIR)/curl
EXTERN_LIB_CURL_LIB := -L$(SYS_LIB_DIR) -lcurl

##libcap
EXTERN_LIB_CAP_INC := -I$(SYS_INC_DIR)/libcap
EXTERN_LIB_CAP_LIB := -L$(SYS_LIB_DIR) -lcap

##libnghttp2
EXTERN_LIB_NGHTTP2_INC := -I$(SYS_INC_DIR)/libnghttp2
EXTERN_LIB_NGHTTP2_LIB := -L$(SYS_LIB_DIR) -lnghttp2

##libgmp
EXTERN_LIB_GMP_INC := -I$(SYS_INC_DIR)/gmp
EXTERN_LIB_GMP_LIB := -L$(SYS_LIB_DIR) -lgmp

##libtasn1
EXTERN_LIB_TASN1_INC := -I$(SYS_INC_DIR)/libtasn1
EXTERN_LIB_TASN1_LIB := -L$(SYS_LIB_DIR) -ltasn1

##libunistring
EXTERN_LIB_UNISTRING_INC := -I$(SYS_INC_DIR)/libunistring
EXTERN_LIB_UNISTRING_LIB := -L$(SYS_LIB_DIR) -lunistring

##libidn2
EXTERN_LIB_IDN2_INC := -I$(SYS_INC_DIR)/libidn2
EXTERN_LIB_IDN2_LIB := -L$(SYS_LIB_DIR) -lidn2

##gnutls
EXTERN_LIB_GNUTLS_INC := -I$(SYS_INC_DIR)/gnutls
EXTERN_LIB_GNUTLS_LIB := -L$(SYS_LIB_DIR) -lgnutls

##speex
EXTERN_LIB_SPEEX_INC := -I$(SYS_INC_DIR)/speex
EXTERN_LIB_SPEEX_LIB := -L$(SYS_LIB_DIR) -lspeex

##libnice
EXTERN_LIB_NICE_INC := -I$(SYS_INC_DIR)/libnice
EXTERN_LIB_NICE_LIB := -L$(SYS_LIB_DIR) -lnice

##libvpx
EXTERN_LIB_VPX_INC := -I$(SYS_INC_DIR)/libvpx
EXTERN_LIB_VPX_LIB := -L$(SYS_LIB_DIR) -lvpx
