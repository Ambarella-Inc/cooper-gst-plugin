/*******************************************************************************
 *  data_subscriber.h
 *
 * History:
 *  2025/09/19 - [Da-Shun Pei] Create
 *
 * Copyright (c) 2023 Ambarella International LP
 *
 * This file and its contents ( "Software" ) are protected by intellectual
 * property rights including, without limitation, U.S. and/or foreign
 * copyrights. This Software is also the confidential and proprietary
 * information of Ambarella International LP and its licensors. You may not use, reproduce,
 * disclose, distribute, modify, or otherwise prepare derivative works of this
 * Software or any portion thereof except pursuant to a signed license agreement
 * or nondisclosure agreement with Ambarella International LP or its authorized affiliates.
 * In the absence of such an agreement, you agree to promptly notify and return
 * this Software to Ambarella International LP
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

#ifndef DATA_SUBSCRIBER_H
#define DATA_SUBSCRIBER_H

#include <stdint.h>
#include "dds_msgs/FreeFrame_Msg.h"
#include "dds_msgs/AmShMem_Msg.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_SAMPLES (1)

typedef struct  {
    dds_entity_t participant;
    dds_entity_t topic;
    dds_entity_t reader;

    FreeFrame_Msg *free_frame_msg;
    AmShMem_Msg *am_shmem_msg;

    uint32_t status;
    void *samples[MAX_SAMPLES];
    dds_sample_info_t infos[MAX_SAMPLES];
    dds_return_t rc;
    dds_qos_t *qos;
    uint32_t stream_num;
} dds_subscriber_ctx;

/* topic_name: NULL or "" -> default ("AmShMem_Msg" / "FreeFrame_Msg"). */
int init_subscriber (dds_subscriber_ctx *dds_sub, int msg_type, uint32_t domain_id,
    const char *topic_name);
int run_subscriber(dds_subscriber_ctx *dds_sub, int msg_type);
int delete_subscriber(dds_subscriber_ctx *dds_sub, int msg_type);

#ifdef __cplusplus
}
#endif

#endif /* DATA_SUBSCRIBER_H */
