###########################################################
## History:
##    2023/11/17 - [pxduan] Create
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

#DEPS(amba.mk) ambagst-test(jobserver): unselect &&??prebuild-libsoup &&??prebuild-libnice &&??libgstamba &&??libsei_box

PACKAGE_NAME = ambagst-test
PACKAGE_DEPS = libgstamba libsei_box


.PHONY: all clean install

all:
	@echo "Build $(PACKAGE_NAME) Done."

include $(ENV_MAKE_DIR)/inc.app.mk

test-launch-srcs = test_launch_v2.c
test-launch-v3-srcs = rtsp_ssl_example/rtsp_config.c rtsp_ssl_example/test_launch_v3.c
test-client-srcs = rtsp_ssl_example/simple_verify_tls_interaction.c rtsp_ssl_example/chain_verify_tls_interaction.c \
                    rtsp_ssl_example/rtsp_config.c rtsp_ssl_example/test_client.c
test-webrtc-sendrecv-srcs = webrtc_example/custom_agent.c webrtc_example/webrtc-sendrecv.c
test-seiinject-srcs = seiinject/test_seiinject.c
test-sei-meta-appsink-srcs = seiinject/test_sei_meta_appsink.c
test-event-recording-srcs = event_recorder/test_event_recording.c

## general flags
ambagsttest-cflags := -O3 -Wall -I./
ambagsttest-ldflags += -lpthread -lrt -lm

ifeq ($(ENV_BUILD_MODE), yocto)
ambagsttest-cflags += -I $(ENV_DEP_ROOT)/usr/include/gstreamer-1.0
ambagsttest-cflags += -I $(ENV_DEP_ROOT)/usr/include/glib-2.0 -I $(ENV_DEP_ROOT)/usr/include/gio-unix-2.0
ambagsttest-cflags += -I $(ENV_DEP_ROOT)/usr/lib/glib-2.0/include
ambagsttest-ldflags += -L $(ENV_DEP_ROOT)/usr/lib/gstreamer-1.0
else
ambagsttest-cflags += -I $(ENV_GLO_SYSROOT_DIR)/usr/include/gstreamer-1.0
ambagsttest-cflags += -I $(ENV_GLO_SYSROOT_DIR)/usr/include/glib-2.0 -I $(ENV_GLO_SYSROOT_DIR)/usr/include/gio-unix-2.0
ambagsttest-cflags += -I $(ENV_GLO_SYSROOT_DIR)/usr/lib64/glib-2.0/include/
ambagsttest-ldflags += -L $(ENV_GLO_SYSROOT_DIR)/usr/lib64/gstreamer-1.0
endif

ambagsttest-ldflags += -lgobject-2.0 -lglib-2.0 -lgmodule-2.0 -lgio-2.0
ambagsttest-ldflags += -lgstreamer-1.0 -lgstbase-1.0 -lgstaudio-1.0 -lgstvideo-1.0
##ambagsttest-ldflags += -ljson-c -ljson-glib-1.0 -lsoup-2.4 -lxml2 -ldaemon -lffi
##ambagsttest-ldflags += -lselinux -lmount -lblkid -lsqlite3 -llzma -lpcre -lz


##### rtsp related
ambagsttest-rtsp-cflags := $(ambagsttest-cflags)
ambagsttest-rtsp-ldflags := $(ambagsttest-ldflags) -lgstrtspserver-1.0 -lgstrtspclientsink

##### webrtc related
ambagsttest-webrtc-cflags := $(ambagsttest-cflags) -DGST_USE_UNSTABLE_API
ifeq ($(ENV_BUILD_MODE), yocto)
ambagsttest-webrtc-cflags += -I $(ENV_DEP_ROOT)/usr/include/nice
ambagsttest-webrtc-cflags += -I $(ENV_DEP_ROOT)/usr/include/libsoup-2.4 -I $(ENV_DEP_ROOT)/usr/include/json-glib-1.0
else
ambagsttest-webrtc-cflags += -I $(ENV_GLO_SYSROOT_DIR)/usr/include/nice
ambagsttest-webrtc-cflags += -I $(ENV_GLO_SYSROOT_DIR)/usr/include/libsoup-2.4 -I $(ENV_GLO_SYSROOT_DIR)/usr/include/json-glib-1.0
endif

ambagsttest-webrtc-ldflags := $(ambagsttest-ldflags) -lgstrtp-1.0 -lgstsdp-1.0 -ljson-glib-1.0 -lsoup-2.4 -lgstwebrtcnice-1.0 -lnice -lgstwebrtc-1.0

##### seiinject related
ifeq ($(ENV_BUILD_MODE), yocto)
test-sei-meta-appsink-cflags := $(ambagsttest-cflags) -I $(ENV_DEP_ROOT)/usr/include/amba-gst-plugins-1.0
else
test-sei-meta-appsink-cflags := $(ambagsttest-cflags) -I../src/elements/seiinject
endif
test-sei-meta-appsink-ldflags := $(ambagsttest-ldflags)

$(call set_flags,CFLAGS,$(test-launch-srcs),$(ambagsttest-rtsp-cflags))
$(eval $(call add-bin-build,test-launch-v2,$(test-launch-srcs),$(ambagsttest-rtsp-ldflags)))

$(call set_flags,CFLAGS,$(test-launch-v3-srcs),$(ambagsttest-rtsp-cflags))
$(eval $(call add-bin-build,test-launch-v3,$(test-launch-v3-srcs),$(ambagsttest-rtsp-ldflags)))

$(call set_flags,CFLAGS,$(test-client-srcs),$(ambagsttest-rtsp-cflags))
$(eval $(call add-bin-build,test-client,$(test-client-srcs),$(ambagsttest-rtsp-ldflags)))

#ifneq ($(ENV_BUILD_MODE), yocto)
$(call set_flags,CFLAGS,$(test-webrtc-sendrecv-srcs),$(ambagsttest-webrtc-cflags))
$(eval $(call add-bin-build,test-webrtc-sendrecv,$(test-webrtc-sendrecv-srcs),$(ambagsttest-webrtc-ldflags)))
#endif

$(call set_flags,CFLAGS,$(test-seiinject-srcs),$(ambagsttest-cflags))
$(eval $(call add-bin-build,test-seiinject,$(test-seiinject-srcs),$(ambagsttest-ldflags)))


$(call set_flags,CFLAGS,$(test-sei-meta-appsink-srcs),$(test-sei-meta-appsink-cflags))
$(eval $(call add-bin-build,test-sei-meta-appsink,$(test-sei-meta-appsink-srcs),$(test-sei-meta-appsink-ldflags)))

$(call set_flags,CFLAGS,$(test-event-recording-srcs),$(ambagsttest-cflags))
$(eval $(call add-bin-build,test-event-recording,$(test-event-recording-srcs),$(ambagsttest-ldflags)))

INSTALL_DATAS_certs = rtsp_ssl_example/ssl_cert/* /ambarella/gst_rtsp_ssl/cert
INSTALL_DATAS_configs = rtsp_ssl_example/*.conf /ambarella/gst_rtsp_ssl/config

all: $(BIN_TARGETS)

clean: clean_objs
	@-rm -f $(BIN_TARGETS)
	@echo "Clean $(PACKAGE_NAME) Done."

install: install_bins install_datas_certs install_datas_configs

