#!/bin/sh

COPY_SRC_PATH=/dump45/zhe/s5l_dds/ambarella/packages
AMBA_GST_PLUGIN_DIR=amba_gst_plugins
NNSTREAMER_DIR=nnstreamer
GSTD_DIR=gstd-0.14.0

PACKAGE_NAME=ambarella_gst_v1.0.0.tar.bz2

mv packages packages_ori
mkdir packages

cp $(COPY_SRC_PATH)/$(AMBA_GST_PLUGIN_DIR) ./packages/ -rf
cp $(COPY_SRC_PATH)/$(NNSTREAMER_DIR) ./packages/ -rf
cp $(COPY_SRC_PATH)/$(GSTD_DIR) ./packages/ -rf

rm ./packages/$(AMBA_GST_PLUGIN_DIR)/.git -rf
rm ./packages/$(NNSTREAMER_DIR)/.git -rf
rm ./packages/$(GSTD_DIR)/.git -rf

rm ./packages/$(AMBA_GST_PLUGIN_DIR)/bak -rf
rm ./packages/$(AMBA_GST_PLUGIN_DIR)/additional/pack_script -rf
rm ./packages/$(GSTD_DIR)/extern_lib/readline-8.1 -rf

tar jcvf $(PACKAGE_NAME) packages

rm -rf packages
mv packages_ori packages
sync
