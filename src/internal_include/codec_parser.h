/*******************************************************************************
 * codec_parser.h
 *
 * History:
 *    2015/02/10 - [Zhi He] create file
 *
 * Copyright (c) 2016 Ambarella International LP
 *
 * This file and its contents ( "Software" ) are protected by intellectual
 * property rights including, without limitation, U.S. and/or foreign
 * copyrights. This Software is also the confidential and proprietary
 * information of Ambarella International LP and its licensors. You may not use, reproduce,
 * disclose, distribute, modify, or otherwise prepare derivative works of this
 * Software or any portion thereof except pursuant to a signed license agreement
 * or nondisclosure agreement with Ambarella International LP or its authorized affiliates.
 * In the absence of such an agreement, you agree to promptly notify and return
 * this Software to Ambarella International LP.
 *
 * This file includes sample code and is only for internal testing and evaluation.  If you
 * distribute this sample code (whether in source, object, or binary code form), it will be
 * without any warranty or indemnity protection from Ambarella International LP or its affiliates.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF NON-INFRINGEMENT,
 * MERCHANTABILITY, AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL AMBARELLA INTERNATIONAL LP OR ITS AFFILIATES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; COMPUTER FAILURE OR MALFUNCTION; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
******************************************************************************/

#ifndef CODEC_PARSER_H
#define CODEC_PARSER_H

enum AVC_SLICE_TYPE {
    AVC_SLICE_TYPE_NONE = 0x00,
    AVC_SLICE_TYPE_I = 0x01,
    AVC_SLICE_TYPE_P = 0x02,
    AVC_SLICE_TYPE_B = 0x03,
    AVC_SLICE_TYPE_S = 0x04,

    AVC_SLICE_TYPE_SI = 0x05,
    AVC_SLICE_TYPE_SP = 0x06,
    AVC_SLICE_TYPE_BI = 0x07,
};

unsigned char get_h264_slice_type_le(unsigned char *pdata, unsigned char *first_mb_in_slice);
int get_h264_reso_from_sps(unsigned char *p_data, unsigned int data_size, unsigned int *width, unsigned int *height);
int get_h265_reso_from_sps(unsigned char *p_data, unsigned int data_size, unsigned int *pic_width, unsigned int *pic_height);

#endif