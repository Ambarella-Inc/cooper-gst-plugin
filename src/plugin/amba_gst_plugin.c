/*
 * amba_gst_plugin.c
 *
 * History:
 *    4/7/2022 - [Peng-Xue Duan] created file
 *
 * Copyright (C) 2022 Ambarella International LP
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "internal.h"

#include <stdio.h>

#include "bitstream_state.h"

#include "iav_al.h"
#include "iav_ctx.h"

#include "gstambavenccap.h"
#include "gstambavenccap2.h"
#include "gstambavencdemux.h"
#include "gstambaheicfilesink.h"
#include "gstambacamsrc.h"
#include "gstambacamsrc2.h"
#include "gstambahwvdec.h"
#include "gstambahwvdecv2.h"
#include "gstambahwvdecsrc.h"
#include "gstambavsink.h"
#include "gstmalsasrc.h"
#include "gstmlinference.h"
#include "gstambavencoverlaybbox.h"
#include "gstambaoverlaysrc.h"
#include "gstambavencoverlay.h"
#include "gstambaefr.h"
#include "gstmfilesink.h"
#include "gstambaaacenc.h"
#include "gstambaaacdec.h"
#include "gstambavideoscale.h"
#include "gstambafilevenc.h"
#include "gstambavprocimgcvt.h"
#include "gstambafilemuxer.h"
#include "gstmsplitmuxsink.h"
#include "gstmlinference2.h"
#include "gstmlpostprocess.h"
#include "gstambadrawdatagen.h"
#include "gstambaoverlaydraw.h"
#include "gstambaseiinject.h"
#include "gstambaseidecoder.h"
#include "gstambaeventrecorder.h"
#include "gstambavencblur.h"
#include "gstamshmemsink.h"
#include "gstamshmemsrc.h"
#include "gstambacompositor.h"


#if GST_CHECK_VERSION(1, 24, 0)
#include "gstmunixfd.h"
#endif

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
amba_plugin_init (GstPlugin *amba)
{
  gboolean ret = TRUE;

  do {
    if (!gst_element_register (amba, "amba_venccap", GST_RANK_MARGINAL,
        GST_TYPE_AMBAVENCCAP) ) {
      GST_ERROR ("Failed to register the element of amba_venccap!");
      ret = FALSE;
      break;
    }

    if (!gst_element_register (amba, "amba_venccap2", GST_RANK_MARGINAL,
        GST_TYPE_AMBAVENCCAP2) ) {
      GST_ERROR ("Failed to register the element of amba_venccap2!");
      ret = FALSE;
      break;
    }

    if (!gst_element_register (amba, "amba_vencdemux", GST_RANK_MARGINAL,
        GST_TYPE_AMBAVENCDEMUX) ) {
      GST_ERROR ("Failed to register the element of amba_vencdemux!");
      ret = FALSE;
      break;
    }

    if (!gst_element_register (amba, "amba_heicfilesink", GST_RANK_MARGINAL,
        GST_TYPE_AMBAHEICFILESINK) ) {
      GST_ERROR ("Failed to register the element of amba_heicfilesink!");
      ret = FALSE;
      break;
    }

    if (!gst_element_register (amba, "amba_camsrc", GST_RANK_MARGINAL,
        GST_TYPE_AMBA_CAMSRC) ) {
      GST_ERROR ("Failed to register the element of amba_camsrc!");
      ret = FALSE;
      break;
    }

    if (!gst_element_register (amba, "amba_camsrc2", GST_RANK_MARGINAL,
        GST_TYPE_AMBA_CAMSRC2) ) {
      GST_ERROR ("Failed to register the element of amba_camsrc2!");
      ret = FALSE;
      break;
    }

    if (!gst_element_register (amba, "amba_hwvdec", GST_RANK_NONE,
        GST_TYPE_AMBAHWVDEC) ) {
      GST_ERROR ("Failed to register the element of amba_hwvdec!");
      ret = FALSE;
      break;
    }

    if (!gst_element_register (amba, "amba_hwvdecv2", GST_RANK_MARGINAL,
        GST_TYPE_AMBA_HWVDECV2) ) {
      GST_ERROR ("Failed to register the element amba_hwvdecv2!");
      ret = FALSE;
      break;
    }

    if (!gst_element_register (amba, "amba_hwvdecsrc", GST_RANK_MARGINAL,
        GST_TYPE_AMBA_HWVDECSRC) ) {
      GST_ERROR ("Failed to register the element of amba_hwvdecsrc!");
      ret = FALSE;
      break;
    }

    if (!gst_element_register (amba, "amba_vsink", GST_RANK_MARGINAL,
        GST_TYPE_AMBA_VSINK) ) {
      GST_ERROR ("Failed to register the element of amba_vsink!");
      ret = FALSE;
      break;
    }

    if (!gst_element_register (amba, "malsasrc", GST_RANK_MARGINAL,
        GST_TYPE_MALSA_SRC) ) {
      GST_ERROR ("Failed to register the element of modified alsasrc!");
      ret = FALSE;
      break;
    }

    if (!gst_element_register (amba, "mlinference", GST_RANK_MARGINAL,
        GST_TYPE_MLINFERENCE) ) {
      GST_ERROR ("Failed to register the element of ml inference!");
      ret = FALSE;
      break;
    }

    if (!gst_element_register (amba, "amba_venc_overlay_bbox", GST_RANK_NONE,
        GST_TYPE_AMBAVENCOVERLAYBBOX) ) {
      GST_ERROR ("Failed to register the element of amba_venc_overlay_bbox!");
      ret = FALSE;
      break;
    }

    if (!gst_element_register (amba, "amba_venc_overlay", GST_RANK_NONE,
        GST_TYPE_AMBAVENCOVERLAY) ) {
      GST_ERROR ("Failed to register the element of amba_venc_overlay!");
      ret = FALSE;
      break;
    }

    if (!gst_element_register (amba, "amba_overlay_src", GST_RANK_NONE,
        GST_TYPE_AMBAOVERLAYSRC) ) {
      GST_ERROR ("Failed to register the element of amba_overlay_src!");
      ret = FALSE;
      break;
    }

    if (!gst_element_register (amba, "mfilesink", GST_RANK_MARGINAL,
        GST_TYPE_MFILE_SINK) ) {
      GST_ERROR ("Failed to register the element of modified filesink!");
      ret = FALSE;
      break;
    }

    if (!gst_element_register (amba, "amba_videoscale", GST_RANK_MARGINAL,
        GST_TYPE_AMBA_VIDEOSCALE) ) {
      GST_ERROR ("Failed to register the element of amba videoscale!");
      ret = FALSE;
      break;
    }

    if (!gst_element_register (amba, "amba_efr", GST_RANK_NONE,
        GST_TYPE_AMBA_EFR) ) {
      GST_ERROR ("Failed to register the element of amba_efr!");
      ret = FALSE;
      break;
    }

    if (!gst_element_register (amba, "amba_file_venc", GST_RANK_MARGINAL,
        GST_TYPE_AMBA_FILEVENC) ) {
      GST_ERROR ("Failed to register the element of amba_file_venc!");
      ret = FALSE;
      break;
    }

    if (!gst_element_register (amba, "amba_img_cvt", GST_RANK_MARGINAL,
        GST_TYPE_AMBA_VPROC_IMCVT) ) {
      GST_ERROR ("Failed to register the element of amba_img_cvt!");
      ret = FALSE;
      break;
    }

    if (!gst_element_register (amba, "amba_filemuxer", GST_RANK_MARGINAL,
        GST_TYPE_AMBA_FILEMUXER) ) {
      GST_ERROR ("Failed to register the element of amba_filemuxer!");
      ret = FALSE;
      break;
    }

   if (!gst_element_register (amba, "amba_venc_blur", GST_RANK_NONE,
        GST_TYPE_AMBAVENCBLUR) ) {
      GST_ERROR ("Failed to register the element of amba_venc_blur!");
      ret = FALSE;
      break;
   }

#if GST_CHECK_VERSION(1, 24, 0)
    if (!GST_ELEMENT_REGISTER (munixfdsrc, amba)) {
      GST_ERROR ("Failed to register the element of modified unixfdsrc!");
      ret = FALSE;
      break;
    }

    if (!GST_ELEMENT_REGISTER (munixfdsink, amba)) {
      GST_ERROR ("Failed to register the element of modified unixfdsink!");
      ret = FALSE;
      break;
    }
#endif

    if (!gst_element_register (amba, "amba_aac_enc", GST_RANK_NONE,
        GST_TYPE_AMBA_AAC_ENC) ) {
      GST_ERROR ("Failed to register the element of amba aac encoder!");
      ret = FALSE;
      break;
    }

    if (!gst_element_register (amba, "amba_aac_dec", GST_RANK_NONE,
        GST_TYPE_AMBA_AAC_DEC) ) {
      GST_ERROR ("Failed to register the element of amba aac decoder!");
      ret = FALSE;
      break;
    }

    if (!gst_element_register (amba, "msplitmuxsink", GST_RANK_NONE,
        GST_TYPE_MSPLITMUX_SINK) ) {
      GST_ERROR ("Failed to register the element of modified splitmuxsink!");
      ret = FALSE;
      break;
    }

    if (!gst_element_register (amba, "mlinference2", GST_RANK_MARGINAL,
        GST_TYPE_MLINFERENCE2) ) {
      GST_ERROR ("Failed to register the element of ml inference2!");
      ret = FALSE;
      break;
    }

    if (!gst_element_register (amba, "mlpostprocess", GST_RANK_MARGINAL,
        GST_TYPE_ML_POSTPROCESS) ) {
      GST_ERROR ("Failed to register the element of ml postprocess!");
      ret = FALSE;
      break;
    }

    if (!gst_element_register (amba, "amba_draw_data_gen", GST_RANK_MARGINAL,
        GST_TYPE_AMBA_DRAW_DATA_GEN) ) {
      GST_ERROR ("Failed to register the element of amba_draw_data_gen!");
      ret = FALSE;
      break;
    }

    if (!gst_element_register (amba, "amba_overlay_draw", GST_RANK_NONE,
        GST_TYPE_AMBA_OVERLAY_DRAW) ) {
      GST_ERROR ("Failed to register the element of amba_overlay_draw!");
      ret = FALSE;
      break;
    }

    if (!gst_element_register (amba, "amba_seiinject", GST_RANK_MARGINAL,
        GST_TYPE_AMBA_SEIINJECT) ) {
      GST_ERROR ("Failed to register the element of amba_seiinject!");
      ret = FALSE;
      break;
    }

    if (!gst_element_register (amba, "amba_sei_decoder", GST_RANK_MARGINAL,
        GST_TYPE_AMBA_SEIDECODER) ) {
      GST_ERROR ("Failed to register the element of amba_sei_decoder!");
      ret = FALSE;
      break;
    }

    if (!gst_element_register (amba, "amba_event_recorder", GST_RANK_MARGINAL,
        GST_TYPE_AMBA_EVENT_RECORDER) ) {
      GST_ERROR ("Failed to register the element of amba_event_recorder!");
      ret = FALSE;
      break;
    }

    if (!gst_element_register (amba, "amshmem_sink", GST_RANK_MARGINAL,
        GST_TYPE_AMSHMEM_SINK) ) {
      GST_ERROR ("Failed to register the element of amshmem_sink!");
      ret = FALSE;
      break;
    }

    if (!gst_element_register (amba, "amshmem_src", GST_RANK_MARGINAL,
        GST_TYPE_AMSHMEM_SRC) ) {
      GST_ERROR ("Failed to register the element of amshmem_src!");
      ret = FALSE;
      break;
    }

    if (!gst_element_register (amba, "ambacompositor", GST_RANK_MARGINAL,
        GST_TYPE_ambacompositor) ) {
      GST_ERROR ("Failed to register the element ambacompositor!");
      ret = FALSE;
      break;
    }

  } while (0);

  return ret;
}

/* GST_PLUGIN_DEFINE needs PACKAGE to be defined */
#ifndef PACKAGE
#define PACKAGE "gst-plugins-good"
#endif

#define VERSION "1.6.0"
#define GST_LICENSE "LGPL"
#define GST_PACKAGE_NAME "Ambarella Plugin"
#define GST_PACKAGE_ORIGIN "https://www.ambarella.com"

/* gstreamer looks for this structure to register plugins */
GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  amba,
  "Ambarella Plugin",
  amba_plugin_init,
  VERSION,
  GST_LICENSE,
  GST_PACKAGE_NAME,
  GST_PACKAGE_ORIGIN
)

