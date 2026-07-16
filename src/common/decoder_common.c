/*
 * iav_ctx.c
 *
 * History:
 *    5/25/2022 - [Zhi He] created file
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

#include "stdlib.h"
#include "stdio.h"
#include "string.h"

#include "pthread.h"

#include "common_err_code_c.h"

#include "internal.h"

#include "debug_log.h"

#include "iav_al.h"

#include "iav_ctx.h"

#include "gstambahwvdec.h"

#include "decoder_common.h"

#if defined (BUILD_DSP_AMBA_V5)
static int __check_dsp_vout(GstAmbaHwvdec * self)
{
  int err = COM_ECODE_OK;
  amba_dsp_mode_t dsp_mode;

    //check dsp state
    err = self->iav_ctx->iav_al.f_get_dsp_mode(self->iav_ctx->iav_fd, &dsp_mode);
    if (err) {
        DPRINT_ERROR("IAV driver or ucode not loaded\n");
        return err;
    }

#if defined (BUILD_DSP_AMBA_V5)
#if 1
    if (EAMDSP_MODE_DECODE != dsp_mode.dsp_mode) {
        if (!self->vout_number) {
            DPRINT_ERROR("should specify vout first: e.g. add params '-V1080p --hdmi'. (current dsp mode %d)\n", dsp_mode.dsp_mode);
            return (-2);
        }
        if (EAMDSP_MODE_INIT == dsp_mode.dsp_mode) {
            DPRINT_NOTICE("dsp (%d), boot first, then enter idle mode\n", dsp_mode.dsp_mode);
            err = self->iav_ctx->iav_al.f_enter_idle_mode(self->iav_ctx->iav_fd, 0, 0);
            if (err) {
                DPRINT_ERROR("enter idle fail\n");
                return err;
            }
        } else {
            DPRINT_NOTICE("dsp (%d), not in decode mode, return idle mode first\n", dsp_mode.dsp_mode);
            err = self->iav_ctx->iav_al.f_enter_idle_mode(self->iav_ctx->iav_fd, 0, 0);
            if (err) {
                DPRINT_ERROR("enter idle fail\n");
                return err;
            }
            err = halt_amba_vout(self->iav_ctx->iav_fd, self->vout_number);
            if (COM_ECODE_OK != err) {
                DPRINT_ERROR("halt current vout fail\n");
                return (-4);
            }
        }

    } else {
        if (self->vout_number) {
            err = halt_amba_vout(self->iav_ctx->iav_fd, self->vout_number);
            if (COM_ECODE_OK != err) {
                DPRINT_ERROR("halt current vout fail\n");
                return (-5);
            }
        }
    }
#else
    if (!self->vout_number) {
        DPRINT_ERROR("should specify vout first: e.g. add params '-V1080p --hdmi'. (current dsp mode %d)\n", dsp_mode.dsp_mode);
        return (-2);
    }

    err = self->iav_ctx->iav_al.f_enter_idle_mode(self->iav_ctx->iav_fd, 0, 0);
    if (err) {
        DPRINT_ERROR("enter idle fail\n");
        return err;
    }

    if (EAMDSP_MODE_INIT != dsp_mode.dsp_mode) {
        err = halt_amba_vout(self->iav_ctx->iav_fd, self->vout_number);
        if (COM_ECODE_OK != err) {
            DPRINT_ERROR("halt current vout fail\n");
            return (-5);
        }
    }

#endif
#elif defined (BUILD_DSP_AMBA_V6)

    if (!self->vout_number) {
        DPRINT_ERROR("should specify vout first: e.g. 'test_encode --resource-cfg dec_1ch.lua --vout-cfg /usr/share/ambarella/lua_scripts/vout_hdmi.lua'. (current dsp mode %d)\n", dsp_mode.dsp_mode);
        return (-2);
    }

    err = self->iav_ctx->iav_al.f_enter_idle_mode(self->iav_ctx->iav_fd, 1, 0);
    if (err) {
        DPRINT_ERROR("enter idle fail\n");
        return err;
    }

    err = self->iav_ctx->iav_al.f_enable_preview(self->iav_ctx->iav_fd, 0);
    if (err) {
        DPRINT_ERROR("enable preview fail\n");
        return err;
    }

    if (EAMDSP_MODE_INIT != dsp_mode.dsp_mode) {
        err = halt_amba_vout(self->iav_ctx->iav_fd, self->vout_number);
        if (COM_ECODE_OK != err) {
            DPRINT_ERROR("halt current vout fail\n");
            return (-4);
        }
    }

#endif

    for (int i = 0; i < self->vout_number; i++) {
        amba_vout_config_t config;

        memset(&config, 0x0, sizeof(config));
        config.vout_id = i;
        config.mode_string = &self->vout_configs[i].mode_string[0];
        config.sink_type_string = &self->vout_configs[i].sinktype_string[0];
        config.device_string = &self->vout_configs[i].device_string[0];

        config.b_config_mixer = 1;
        config.mixer_flag = 1;
        if (!strcmp(get_dsp_platform_name(), "S2E")) {
            config.b_direct_2_dsp = 1;
        }
        printf("platform %s: b_config_mixer %d, mixer_flag %d, direct2dsp %d\n",
            get_dsp_platform_name(),
            config.b_config_mixer,
            config.mixer_flag,
            config.b_direct_2_dsp);

        printf("mode_string %s, sink_type_string %s, device_string %s\n",
            config.mode_string,
            config.sink_type_string,
            config.device_string);

        int err = config_amba_vout(self->iav_ctx->iav_fd,
          &self->iav_ctx->iav_al, &config);
        if (COM_ECODE_OK != err) {
            DPRINT_NOTICE("config vout %d failed, ret 0x%08x\n", i, err);
            return (-1);
        }
    }

    return 0;
}
#endif

int enter_decode_mode(GstAmbaHwvdec * self)
{
    int ret = 0;
    int i = 0;
    amba_dsp_decode_chan_config_t *p_dec_config = NULL;
    SDSPPyramidConfig *p_dsp_pyd_config = NULL;
    int iav_fd = self->iav_ctx->iav_fd;
    iav_al_t *iav_al = &self->iav_ctx->iav_al;
    iav_dec_mode_t *dec_mode = &self->iav_ctx->dec_mode;

    if (self->iav_ctx->decode_mode_entered) {
        return 0;
    }

#if defined (BUILD_DSP_AMBA_V5)
    amba_dsp_vout_info_t mVoutInfos[3];

    int width = 1920;
    int height = 1080;

    __check_dsp_vout(self);

    for (i = 0; i < self->vout_number; i++) {
        int has_digital = 0, has_hdmi = 0, has_cvbs = 0;
        int vout_num = 0;

        SConfigVout *vout_configs = &self->vout_configs[i];

        memset(mVoutInfos, 0x0, sizeof(amba_dsp_vout_info_t) * 3);

        ret = iav_al->f_get_vout_info(iav_fd, i, EAMDSP_VOUT_TYPE_DIGITAL, &mVoutInfos[0]);
        if ((0 > ret) || (!mVoutInfos[0].width) || (!mVoutInfos[0].height)) {
            DPRINT_NOTICE("digital vout %d not enabled\n", i);
            vout_configs->b_digital_vout = 0;
        } else {
            has_digital = 1;
            vout_num ++;
        }

        ret = iav_al->f_get_vout_info(iav_fd, i, EAMDSP_VOUT_TYPE_HDMI, &mVoutInfos[1]);
        if ((0 > ret) || (!mVoutInfos[1].width) || (!mVoutInfos[1].height)) {
            DPRINT_NOTICE("hdmi vout %d not enabled\n", i);
            vout_configs->b_hdmi_vout = 0;
        } else {
            has_hdmi = 1;
            vout_num ++;
        }
        ret = iav_al->f_get_vout_info(iav_fd, i, EAMDSP_VOUT_TYPE_CVBS, &mVoutInfos[2]);
        if ((0 > ret) || (!mVoutInfos[2].width) || (!mVoutInfos[2].height)) {
            DPRINT_NOTICE("cvbs vout %d not enabled\n", i);
            vout_configs->b_cvbs_vout = 0;
        } else {
            has_cvbs = 1;
            vout_num ++;
        }
        if (!vout_num) {
            DPRINT_ERROR("no vout specified\n");
            return -1;
        }
        if ((!vout_configs->b_digital_vout)
            && (!vout_configs->b_hdmi_vout)
            && (!vout_configs->b_cvbs_vout)) {
            if (has_hdmi) {
                vout_configs->b_hdmi_vout = 1;
            } else if (has_digital) {
                vout_configs->b_digital_vout = 1;
            } else if (has_cvbs) {
                vout_configs->b_cvbs_vout = 1;
            }
            DPRINT_WARNING("usr do not specify vout %d, guess default: cvbs %d, digital %d, hdmi %d\n",
                i,
                vout_configs->b_cvbs_vout,
                vout_configs->b_digital_vout,
                vout_configs->b_hdmi_vout);
        } else {
            if (vout_configs->b_hdmi_vout) {
                vout_configs->b_digital_vout = 0;
                vout_configs->b_cvbs_vout = 0;
                dec_mode->mModeConfig.vout_mask = 0x02;
            } else if (vout_configs->b_digital_vout) {
                vout_configs->b_hdmi_vout = 0;
                vout_configs->b_cvbs_vout = 0;
                dec_mode->mModeConfig.vout_mask = 0x01;
            } else if (vout_configs->b_cvbs_vout) {
                vout_configs->b_hdmi_vout = 0;
                vout_configs->b_digital_vout = 0;
                dec_mode->mModeConfig.vout_mask = 0x02;
            }
        }


        if (vout_configs->b_digital_vout) {
            dec_mode->vout_configs[i].enable = 1;
            dec_mode->vout_configs[i].vout_id = 0;
            dec_mode->vout_configs[i].flip = mVoutInfos[0].flip;
            dec_mode->vout_configs[i].rotate = mVoutInfos[0].rotate;
            dec_mode->vout_configs[i].target_win_offset_x = mVoutInfos[0].offset_x;
            dec_mode->vout_configs[i].target_win_offset_y = mVoutInfos[0].offset_y;
            dec_mode->vout_configs[i].target_win_width = mVoutInfos[0].width;
            dec_mode->vout_configs[i].target_win_height = mVoutInfos[0].height;
            dec_mode->vout_configs[i].zoom_factor_x = (mVoutInfos[0].width * 0x10000) / width;
            dec_mode->vout_configs[i].zoom_factor_y = (mVoutInfos[0].height * 0x10000) / height;
            dec_mode->vout_configs[i].vout_mode = mVoutInfos[0].mode;
        } else if (vout_configs->b_hdmi_vout) {
            dec_mode->vout_configs[i].enable = 1;
            dec_mode->vout_configs[i].vout_id = 1;
            dec_mode->vout_configs[i].flip = mVoutInfos[1].flip;
            dec_mode->vout_configs[i].rotate = mVoutInfos[1].rotate;
            dec_mode->vout_configs[i].target_win_offset_x = mVoutInfos[1].offset_x;
            dec_mode->vout_configs[i].target_win_offset_y = mVoutInfos[1].offset_y;
            dec_mode->vout_configs[i].target_win_width = mVoutInfos[1].width;
            dec_mode->vout_configs[i].target_win_height = mVoutInfos[1].height;
            dec_mode->vout_configs[i].zoom_factor_x = (mVoutInfos[1].width * 0x10000) / width;
            dec_mode->vout_configs[i].zoom_factor_y = (mVoutInfos[1].height * 0x10000) / height;
            dec_mode->vout_configs[i].vout_mode = mVoutInfos[1].mode;
        } else if (vout_configs->b_cvbs_vout) {
            dec_mode->vout_configs[i].enable = 1;
            dec_mode->vout_configs[i].vout_id = 1;
            dec_mode->vout_configs[i].flip = mVoutInfos[2].flip;
            dec_mode->vout_configs[i].rotate = mVoutInfos[2].rotate;
            dec_mode->vout_configs[i].target_win_offset_x = mVoutInfos[2].offset_x;
            dec_mode->vout_configs[i].target_win_offset_y = mVoutInfos[2].offset_y;
            dec_mode->vout_configs[i].target_win_width = mVoutInfos[2].width;
            dec_mode->vout_configs[i].target_win_height = mVoutInfos[2].height;
            dec_mode->vout_configs[i].zoom_factor_x = (mVoutInfos[2].width * 0x10000) / width;
            dec_mode->vout_configs[i].zoom_factor_y = (mVoutInfos[2].height * 0x10000) / height;
            dec_mode->vout_configs[i].vout_mode = mVoutInfos[2].mode;
        }  else {
            DPRINT_ERROR("no vout %d\n", i);
            return -1;
        }
    }

#endif

    dec_mode->mModeConfig.b_support_ff_fb_bw = self->b_support_ff_fb_bw;
    dec_mode->mModeConfig.num_vout = self->vout_number;
    dec_mode->mModeConfig.num_decoder = self->decoder_number;
    dec_mode->mModeConfig.max_vout0_width = self->max_vout0_width;
    dec_mode->mModeConfig.max_vout0_height = self->max_vout0_height;
    dec_mode->mModeConfig.max_vout1_width = self->max_vout1_width;
    dec_mode->mModeConfig.max_vout1_height = self->max_vout1_height;

    if (self->mbSupportAllframeBackwardPlayback) {
        dec_mode->mModeConfig.max_gop_size = self->mMaxGopSize;
    } else {
        dec_mode->mModeConfig.max_gop_size = 0;
    }

    for (i = 0; i < self->decoder_number; i++) {
        p_dec_config = &dec_mode->mModeConfig.multi_chan_configs[i];
        p_dsp_pyd_config = &self->pb_pyramid_configs[self->pyramid_id[i]];

        p_dec_config->max_frm_width = self->mCapMaxCodedWidth[i];
        p_dec_config->max_frm_height = self->mCapMaxCodedHeight[i];
        dec_mode->mModeConfig.max_frm_width[i] = self->mCapMaxCodedWidth[i];
        dec_mode->mModeConfig.max_frm_height[i] = self->mCapMaxCodedHeight[i];
        if (StreamFormat_H264 == self->mCodecFormat[i]
          || StreamFormat_H264_BYTE == self->mCodecFormat[i]) {
            dec_mode->mModeConfig.decoder_type[i] = EAMDSP_VIDEO_CODEC_TYPE_H264;
            p_dec_config->decoder_type = EAMDSP_VIDEO_CODEC_TYPE_H264;
        } else if (StreamFormat_H265 == self->mCodecFormat[i]
          || StreamFormat_H265_BYTE == self->mCodecFormat[i]) {
            dec_mode->mModeConfig.decoder_type[i] = EAMDSP_VIDEO_CODEC_TYPE_H265;
            p_dec_config->decoder_type = EAMDSP_VIDEO_CODEC_TYPE_H265;
        } else {
            DPRINT_ERROR("bad codec format %d in decoder %d\n", self->mCodecFormat[i], i);
            return -1;
        }

#if defined (BUILD_DSP_AMBA_V5)
        p_dec_config->enable_vout = self->enable_vout[i];

        if (self->enable_pb_pyramid[i]) {
            dec_mode->mModeConfig.b_support_ff_fb_bw = 0;
            DPRINT_NOTICE("disable fast fw/bw when pyramid is enabled\n");
            p_dec_config->layers_map = p_dsp_pyd_config->layers_map;
            p_dec_config->scale_type = p_dsp_pyd_config->scale_type;
            for (int j = 0; j < DDSP_MAX_PYRAMID_LAYERS; j ++) {
                p_dec_config->crop_win[j].x = p_dsp_pyd_config->crop_win[j].x;
                p_dec_config->crop_win[j].y = p_dsp_pyd_config->crop_win[j].y;
                p_dec_config->crop_win[j].w = p_dsp_pyd_config->crop_win[j].w;
                p_dec_config->crop_win[j].h = p_dsp_pyd_config->crop_win[j].h;
            }
            p_dec_config->layer1_width = p_dsp_pyd_config->layer1_width;
            p_dec_config->layer1_height = p_dsp_pyd_config->layer1_height;
        }
#else
      DUNUSED(p_dsp_pyd_config);
#endif
    }


    if (!self->mbDumpOnly) {
        DPRINT_NOTICE("enter decode mode...\n");
        ret = iav_al->f_enter_mode(iav_fd, &dec_mode->mModeConfig);
        if (0 > ret) {
            DPRINT_ERROR("enter decode mode fail, ret %d\n", ret);
            return ret;
        }
        DPRINT_NOTICE("enter decode mode done\n");
        self->iav_ctx->decode_mode_entered = 1;
    }

    return ret;
}

int leave_decode_mode(iav_ctx_t *iav_ctx)
{
    int ret = 0;

    if (iav_ctx->decode_mode_entered) {
      iav_ctx->exit_decode_mode--;
      if (iav_ctx->exit_decode_mode == 0) {
        if ((iav_ctx->iav_fd > 0) && (iav_ctx->iav_al.f_leave_mode)) {

            DPRINT_NOTICE("leave decode mode...\n");
            ret = iav_ctx->iav_al.f_leave_mode(iav_ctx->iav_fd);
            if (0 > ret) {
                DPRINT_ERROR("leave decode mode fail, ret %d\n", ret);
                return ret;
            }
            DPRINT_NOTICE("leave decode mode done\n");
            iav_ctx->decode_mode_entered = 0;
        }
      }
    }

    return ret;
}
