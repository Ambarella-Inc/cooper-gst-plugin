#!/bin/sh

##COPY_SRC_PATH := /dump45/zhe/s5l_dds/ambarella/packages
##AMBA_GST_PLUGIN_DIR := amba_gst_plugins
##NNSTREAMER_DIR := nnstreamer
##GSTD_DIR := gstd-0.14.0

##PACKAGE_NAME := ambarella_gst_v1.0.0.tar.bz2

mv packages packages_ori
mkdir packages
sync

cp /dump45/zhe/s5l_dds/ambarella/packages/amba_gst_plugins ./packages/ -rf
cp /dump45/zhe/s5l_dds/ambarella/packages/nnstreamer ./packages/ -rf
cp /dump45/zhe/s5l_dds/ambarella/packages/gstd-0.14.0 ./packages/ -rf

rm ./packages/amba_gst_plugins/.git -rf
rm ./packages/nnstreamer/.git -rf
rm ./packages/gstd-0.14.0/.git -rf

rm ./packages/amba_gst_plugins/bak -rf
rm ./packages/amba_gst_plugins/additional/pack_script -rf
rm ./packages/gstd-0.14.0/extern_lib/readline-8.1 -rf

tar jcvf ambarella_gst_v1.0.0.tar.bz2 packages

mv packages packages_packing
sync
mv packages_ori packages
sync
