###############################################################################
## extern_lib.mk
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

INSTALL_INC_DIR := $(ENV_INS_ROOT)/usr/include
INSTALL_LIB_DIR := $(ENV_INS_ROOT)/usr/lib

##gstreamer
EXTERN_LIB_GST_INC := -I$(INSTALL_INC_DIR)/gstreamer1/gstreamer-1.0
EXTERN_LIB_GST_LIB := -L$(INSTALL_LIB_DIR) -lgstreamer-1.0

##gstreamer plugin base
EXTERN_LIB_GST_PLUGIN_BASE_INC := -I$(INSTALL_INC_DIR)/gstreamer1-plugins-base/gstreamer-1.0
EXTERN_LIB_GST_PLUGIN_BASE_LIB := -L$(INSTALL_LIB_DIR) -lgstbase-1.0 -lgstaudio-1.0 -lgstvideo-1.0

##freetype
EXTERN_LIB_FREETYPE_INC := -I$(INSTALL_INC_DIR)/freetype/freetype
EXTERN_LIB_FREETYPE_LIB := -L$(INSTALL_LIB_DIR) -lfreetype

##bzip2
EXTERN_LIB_BZIP2_INC := -I$(INSTALL_INC_DIR)/bzip2
EXTERN_LIB_BZIP2_LIB := -L$(INSTALL_LIB_DIR) -lbz2

##libz
EXTERN_LIB_LIBZ_INC := -I$(INSTALL_INC_DIR)/zlib
EXTERN_LIB_LIBZ_LIB := -L$(INSTALL_LIB_DIR) -lz

##libpng
EXTERN_LIB_PNG_INC := -I$(INSTALL_INC_DIR)/libpng
EXTERN_LIB_PNG_LIB := -L$(INSTALL_LIB_DIR) -lpng

##liblwapputils.a
EXTERN_LIB_LWAPPUTILS_INC := -I$(INSTALL_INC_DIR)/liblwapputils
EXTERN_LIB_LWAPPUTILS_LIB := -L$(INSTALL_LIB_DIR) -llwapputils

##alsa-lib
EXTERN_LIB_ALSALIB_INC := -I$(INSTALL_INC_DIR)
EXTERN_LIB_ALSALIB_LIB := -L$(INSTALL_LIB_DIR) -lasound -latopology

##glib2
EXTERN_LIB_GLIB2_INC := -I$(INSTALL_INC_DIR)/glib2/glib-2.0/gio -I$(INSTALL_INC_DIR)/glib2/glib-2.0/glib -I$(INSTALL_INC_DIR)/glib2/glib-2.0/gobject -I$(INSTALL_INC_DIR)/glib2/glib-2.0
EXTERN_LIB_GLIB2_LIB := -L$(INSTALL_LIB_DIR) -lgobject-2.0 -lgthread-2.0 -lgio-2.0 -lgmodule-2.0 -lglib-2.0

##generic
GENERIC_INC := -I$(INSTALL_INC_DIR)/generic-header

##board
BOARD_INC := -I$(INSTALL_INC_DIR)/board

##amba video
AMBA_VIDEO_INC := -I$(INSTALL_INC_DIR)/ambvideo-header

##amba cavalry
AMBA_CAVALRY_INC := -I$(INSTALL_INC_DIR)/ambcavalry-header

##vproc
EXTERN_LIB_VPROC_INC := -I$(INSTALL_INC_DIR)/libvproc
EXTERN_LIB_VPROC_LIB := -L$(INSTALL_LIB_DIR) -lvproc

##nnctrl
EXTERN_LIB_NNCTRL_INC := -I$(INSTALL_INC_DIR)/libnnctrl
EXTERN_LIB_NNCTRL_LIB := -L$(INSTALL_LIB_DIR) -lnnctrl

##cavalry mem
EXTERN_LIB_CAVALRY_MEM_INC := -I$(INSTALL_INC_DIR)/libcavalrymem
EXTERN_LIB_CAVALRY_MEM_LIB := -L$(INSTALL_LIB_DIR) -lcavalry_mem

##pulseaudio
EXTERN_LIB_PULSEAUDIO_INC := -I$(INSTALL_INC_DIR)/pulseaudio
EXTERN_LIB_PULSEAUDIO_LIB := -L$(INSTALL_LIB_DIR) -lpulse

##libopus
EXTERN_LIB_OPUS_INC := -I$(INSTALL_INC_DIR)/opus
EXTERN_LIB_OPUS_LIB := -L$(INSTALL_LIB_DIR) -lopus

##libsndfile
EXTERN_LIB_SNDFILE_INC := -I$(INSTALL_INC_DIR)/libsndfile
EXTERN_LIB_SNDFILE_LIB := -L$(INSTALL_LIB_DIR) -lsndfile

##taglib
EXTERN_LIB_TAGLIB_INC := -I$(INSTALL_INC_DIR)/taglib
EXTERN_LIB_TAGLIB_LIB := -L$(INSTALL_LIB_DIR) -ltag

##nettle
EXTERN_LIB_NETTLE_INC := -I$(INSTALL_INC_DIR)/nettle3
EXTERN_LIB_NETTLE_LIB := -L$(INSTALL_LIB_DIR) -lnettle

##lame
EXTERN_LIB_LAME_INC := -I$(INSTALL_INC_DIR)/lame
EXTERN_LIB_LAME_LIB := -L$(INSTALL_LIB_DIR) -lmp3lame

##libsrtp
EXTERN_LIB_SRTP_INC := -I$(INSTALL_INC_DIR)/libsrtp
EXTERN_LIB_SRTP_LIB := -L$(INSTALL_LIB_DIR) -lsrtp2

##wavpack
EXTERN_LIB_WAVPACK_INC := -I$(INSTALL_INC_DIR)/wavpack
EXTERN_LIB_WAVPACK_LIB := -L$(INSTALL_LIB_DIR) -lwavpack

##libogg
EXTERN_LIB_OGG_INC := -I$(INSTALL_INC_DIR)/libogg
EXTERN_LIB_OGG_LIB := -L$(INSTALL_LIB_DIR) -logg

##openssl
EXTERN_LIB_OPENSSL_INC := -I$(INSTALL_INC_DIR)/openssl
EXTERN_LIB_OPENSSL_LIB := -L$(INSTALL_LIB_DIR) -lcrypto

##faad2
EXTERN_LIB_FAAD2_INC := -I$(INSTALL_INC_DIR)/faad2
EXTERN_LIB_FAAD2_LIB := -L$(INSTALL_LIB_DIR) -lfaad

##mpg123
EXTERN_LIB_MPG123_INC := -I$(INSTALL_INC_DIR)/mpg123
EXTERN_LIB_MPG123_LIB := -L$(INSTALL_LIB_DIR) -lmpg123

##webrtc-audio-processing
EXTERN_LIB_WEBRTC_INC := -I$(INSTALL_INC_DIR)/webrtc-audio-processing
EXTERN_LIB_WEBRTC_LIB := -L$(INSTALL_LIB_DIR) -lwebrtc_audio_processing

##libNE10
EXTERN_LIB_NE10_INC := -I$(INSTALL_INC_DIR)/libNE10
EXTERN_LIB_NE10_LIB := -L$(INSTALL_LIB_DIR) -lNE10

##systemd
EXTERN_LIB_SYSTEMD_INC := -I$(INSTALL_INC_DIR)/systemd
EXTERN_LIB_SYSTEMD_LIB := -L$(INSTALL_LIB_DIR) -lsystemd

##libcurl
EXTERN_LIB_CURL_INC := -I$(INSTALL_INC_DIR)/curl
EXTERN_LIB_CURL_LIB := -L$(INSTALL_LIB_DIR) -lcurl

##libcap
EXTERN_LIB_CAP_INC := -I$(INSTALL_INC_DIR)/libcap
EXTERN_LIB_CAP_LIB := -L$(INSTALL_LIB_DIR) -lcap

##libnghttp2
EXTERN_LIB_NGHTTP2_INC := -I$(INSTALL_INC_DIR)/libnghttp2
EXTERN_LIB_NGHTTP2_LIB := -L$(INSTALL_LIB_DIR) -lnghttp2

##libgmp
EXTERN_LIB_GMP_INC := -I$(INSTALL_INC_DIR)/gmp
EXTERN_LIB_GMP_LIB := -L$(INSTALL_LIB_DIR) -lgmp

##libtasn1
EXTERN_LIB_TASN1_INC := -I$(INSTALL_INC_DIR)/libtasn1
EXTERN_LIB_TASN1_LIB := -L$(INSTALL_LIB_DIR) -ltasn1

##libunistring
EXTERN_LIB_UNISTRING_INC := -I$(INSTALL_INC_DIR)/libunistring
EXTERN_LIB_UNISTRING_LIB := -L$(INSTALL_LIB_DIR) -lunistring

##libidn2
EXTERN_LIB_IDN2_INC := -I$(INSTALL_INC_DIR)/libidn2
EXTERN_LIB_IDN2_LIB := -L$(INSTALL_LIB_DIR) -lidn2

##gnutls
EXTERN_LIB_GNUTLS_INC := -I$(INSTALL_INC_DIR)/gnutls
EXTERN_LIB_GNUTLS_LIB := -L$(INSTALL_LIB_DIR) -lgnutls

##speex
EXTERN_LIB_SPEEX_INC := -I$(INSTALL_INC_DIR)/speex
EXTERN_LIB_SPEEX_LIB := -L$(INSTALL_LIB_DIR) -lspeex

##libnice
EXTERN_LIB_NICE_INC := -I$(INSTALL_INC_DIR)/libnice
EXTERN_LIB_NICE_LIB := -L$(INSTALL_LIB_DIR) -lnice

##libvpx
EXTERN_LIB_VPX_INC := -I$(INSTALL_INC_DIR)/libvpx
EXTERN_LIB_VPX_LIB := -L$(INSTALL_LIB_DIR) -lvpx

##cyclonedds
EXTERN_LIB_CYCLONEDDS_INC := -I$(INSTALL_INC_DIR)/cyclonedds
EXTERN_LIB_CYCLONEDDS_LIB := -L$(INSTALL_LIB_DIR) -lddsc
