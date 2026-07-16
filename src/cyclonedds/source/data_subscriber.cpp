/*******************************************************************************
 *  data_subscriber.cpp
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
#include <stdlib.h>
#include "ddsc/dds.h"

#include "msg_type.h"
#include "include/data_subscriber.h"

static const char *
amshmem_resolve_topic_name (int msg_type, const char *configured)
{
  if (configured && configured[0] != '\0')
    return configured;
  switch (msg_type) {
    case FREE_FRAME_MSGTYPE:
      return "FreeFrame_Msg";
    case AM_SHMEM_MSGTYPE:
      return "AmShMem_Msg";
    default:
      return "Unknown";
  }
}

int init_subscriber (dds_subscriber_ctx *dds_sub, int msg_type, uint32_t domain_id,
    const char *topic_name)
{
    const char *topic = amshmem_resolve_topic_name (msg_type, topic_name);

    dds_sub->status = 0;

    dds_sub->participant = dds_create_participant ((dds_domainid_t) domain_id, NULL, NULL);
    if (dds_sub->participant < 0) {
        DDS_FATAL("dds_create_participant: %s\n", dds_strretcode(-dds_sub->participant));
    }

    switch (msg_type) {
        case FREE_FRAME_MSGTYPE:
            dds_sub->topic = dds_create_topic(dds_sub->participant,
                &FreeFrame_Msg_desc, topic, NULL, NULL);
            if (dds_sub->topic < 0) {
                DDS_FATAL("dds_create the topic: %s\n", dds_strretcode(-dds_sub->topic));
                return dds_sub->topic;
            }
            break;

        case AM_SHMEM_MSGTYPE:
            dds_sub->topic = dds_create_topic(dds_sub->participant,
                &AmShMem_Msg_desc, topic, NULL, NULL);
            if (dds_sub->topic < 0) {
                DDS_FATAL("dds_create the topic: %s\n", dds_strretcode(-dds_sub->topic));
                return dds_sub->topic;
            }
            break;

        default:
            printf("unknown msg_type:%d\n", msg_type);
            return -1;
    }

    dds_sub->qos = dds_create_qos();
    dds_qset_durability(dds_sub->qos, DDS_DURABILITY_TRANSIENT_LOCAL);
    dds_qset_reliability(dds_sub->qos, DDS_RELIABILITY_RELIABLE, DDS_SECS(100));
    dds_qset_history(dds_sub->qos, DDS_HISTORY_KEEP_LAST, 10);
    dds_qset_lifespan(dds_sub->qos, DDS_SECS(3));
    dds_qset_deadline(dds_sub->qos, DDS_SECS(5));
    dds_qset_latency_budget(dds_sub->qos, DDS_SECS(3));
    dds_qset_destination_order(dds_sub->qos, DDS_DESTINATIONORDER_BY_SOURCE_TIMESTAMP);
    //dds_qset_reader_data_lifecycle(dds_sub->qos, DDS_SECS(3), DDS_SECS(3));
    dds_sub->reader = dds_create_reader(
        dds_sub->participant, dds_sub->topic, dds_sub->qos, NULL);
    if (dds_sub->reader < 0) {
        DDS_FATAL("dds_create the reader: %s\n", dds_strretcode(-dds_sub->reader));
    }
    dds_delete_qos(dds_sub->qos);

    printf ("\n=== [Subscriber domain=%u topic=%s] Waiting for a sample ...\n",
        (unsigned) domain_id, topic);

    switch (msg_type) {
        case FREE_FRAME_MSGTYPE:
            for (int i = 0; i < MAX_SAMPLES; i++) {
                dds_sub->samples[i] = FreeFrame_Msg__alloc();
            }
            break;

        case AM_SHMEM_MSGTYPE:
            for (int i = 0; i < MAX_SAMPLES; i++) {
                dds_sub->samples[i] = AmShMem_Msg__alloc();
            }
            break;

        default:
            printf("unknown msg_type:%d\n", msg_type);
            return -1;
    }

    return 0;
}

int run_subscriber(dds_subscriber_ctx *dds_sub, int msg_type)
{
    dds_sub->rc = dds_take(dds_sub->reader, dds_sub->samples,
        dds_sub->infos, MAX_SAMPLES, MAX_SAMPLES);
    if (dds_sub->rc < 0) {
        DDS_FATAL("dds_take: %s\n", dds_strretcode(-dds_sub->rc));
        return dds_sub->rc;
    }

    for (int i = 0; i < dds_sub->rc && i < MAX_SAMPLES; i++) {
        if (dds_sub->infos[i].valid_data) {
            switch (msg_type) {
                case FREE_FRAME_MSGTYPE:
                    dds_sub->free_frame_msg = (FreeFrame_Msg*)dds_sub->samples[i];
                    break;
                case AM_SHMEM_MSGTYPE:
                    dds_sub->am_shmem_msg = (AmShMem_Msg*)dds_sub->samples[i];
                    break;
                default:
                    printf("unknown msg_type:%d\n", msg_type);
                    return -1;
            }
            return 1;
        }
    }
    if (dds_sub->rc > MAX_SAMPLES) {
        printf("=== [Subscriber] warning: dds_take rc=%d > MAX_SAMPLES=%d (extra samples not in array)\n",
            dds_sub->rc, MAX_SAMPLES);
    }

    return 0;
}

int delete_subscriber(dds_subscriber_ctx *dds_sub, int msg_type)
{
    for (int i = 0; i < MAX_SAMPLES; i++) {
        switch (msg_type) {
            case FREE_FRAME_MSGTYPE:
                FreeFrame_Msg_free(dds_sub->samples[i], DDS_FREE_ALL);
                break;
            case AM_SHMEM_MSGTYPE:
                AmShMem_Msg_free(dds_sub->samples[i], DDS_FREE_ALL);
                break;
            default:
                printf("unknown msg_type:%d\n", msg_type);
                return -1;
        }
    }

    if (dds_sub->participant > 0) {
        dds_sub->rc = dds_delete(dds_sub->participant);
        if (dds_sub->rc != DDS_RETCODE_OK) {
            DDS_FATAL("dds_delete: %s\n", dds_strretcode(-dds_sub->rc));
            return dds_sub->rc;
        }

        dds_sub->participant = DDS_ENTITY_NIL;
    }

    return 0;
}
