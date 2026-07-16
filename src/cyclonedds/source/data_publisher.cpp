/*******************************************************************************
 *  data_publisher.cpp
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
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include "ddsc/dds.h"

#include "msg_type.h"
#include "include/data_publisher.h"

/*
 * Optional: block until a reader matches (set AMSHMEM_DDS_WAIT_MATCH=1).
 * Default is OFF: blocking here deadlocks multi-branch gst-launch when another
 * branch's sink must finish start() (e.g. FreeFrame reader) before this writer
 * can match - e.g. domain-1 AmShMem wait prevents domain-0 sink from starting.
 */
static int
amshmem_wait_publication_matched_loop (dds_entity_t writer, uint32_t domain_id,
    const char *topic, dds_duration_t timeout)
{
  dds_publication_matched_status_t st;
  dds_return_t rc;
  dds_duration_t waited = 0;
  const dds_duration_t step = DDS_MSECS (50);

  while (waited < timeout) {
    rc = dds_get_publication_matched_status (writer, &st);
    if (rc != DDS_RETCODE_OK) {
      printf ("=== [Publisher] dds_get_publication_matched_status failed: %s\n",
          dds_strretcode (-rc));
      return (int) rc;
    }
    if (st.current_count > 0) {
      printf ("=== [Publisher matched] domain=%u topic=%s current_count=%d total_count=%d\n",
          (unsigned) domain_id, topic, (int) st.current_count, (int) st.total_count);
      return 0;
    }
    dds_sleepfor (step);
    waited += step;
  }

  printf ("=== [Publisher TIMEOUT] domain=%u topic=%s: no reader after %.1f s\n",
      (unsigned) domain_id, topic, (double) timeout / 1e9);
  return -1;
}

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

/* Async discovery by default; see AMSHMEM_DDS_WAIT_MATCH above. */
int init_publisher (dds_publisher_ctx *dds_pub, int msg_type, uint32_t domain_id,
    const char *topic_name)
{
    const char *topic = amshmem_resolve_topic_name (msg_type, topic_name);

    dds_pub->participant = dds_create_participant ((dds_domainid_t) domain_id, NULL, NULL);
    if (dds_pub->participant < 0) {
        DDS_FATAL("dds_create_participant: %s\n", dds_strretcode(-dds_pub->participant));
        return dds_pub->participant;
    }

    switch (msg_type) {
        case FREE_FRAME_MSGTYPE:
            dds_pub->topic = dds_create_topic(dds_pub->participant,
                &FreeFrame_Msg_desc, topic, NULL, NULL);
            if (dds_pub->topic < 0) {
                DDS_FATAL("dds_create the topic: %s\n", dds_strretcode(-dds_pub->topic));
                return dds_pub->topic;
            }
            break;

        case AM_SHMEM_MSGTYPE:
            dds_pub->topic = dds_create_topic(dds_pub->participant,
                &AmShMem_Msg_desc, topic, NULL, NULL);
            if (dds_pub->topic < 0) {
                DDS_FATAL("dds_create the topic: %s\n", dds_strretcode(-dds_pub->topic));
                return dds_pub->topic;
            }
            break;

        default:
            printf("unknown msg_type:%d\n", msg_type);
            return -1;
    }

    dds_pub->qos = dds_create_qos();
    dds_qset_durability(dds_pub->qos, DDS_DURABILITY_TRANSIENT_LOCAL);
    dds_qset_reliability(dds_pub->qos, DDS_RELIABILITY_RELIABLE, DDS_SECS(100));
    dds_qset_history (dds_pub->qos, DDS_HISTORY_KEEP_LAST, 10);
    dds_qset_lifespan(dds_pub->qos, DDS_SECS(3));
    dds_qset_deadline(dds_pub->qos, DDS_SECS(5));
    dds_qset_latency_budget(dds_pub->qos, DDS_SECS(3));
    dds_qset_destination_order(dds_pub->qos, DDS_DESTINATIONORDER_BY_SOURCE_TIMESTAMP);
    //dds_qset_reader_data_lifecycle(dds_pub->qos, DDS_SECS(3), DDS_SECS(3));
    dds_pub->writer = dds_create_writer(dds_pub->participant,
        dds_pub->topic, dds_pub->qos, NULL);
    if (dds_pub->writer < 0) {
        DDS_FATAL("dds_create the writer: %s\n", dds_strretcode(-dds_pub->writer));
        return dds_pub->writer;
    }

    dds_delete_qos(dds_pub->qos);

    dds_pub->rc = dds_set_status_mask (dds_pub->writer, DDS_PUBLICATION_MATCHED_STATUS);
    if (dds_pub->rc != DDS_RETCODE_OK) {
        DDS_FATAL("dds_set_status_mask: %s\n", dds_strretcode(-dds_pub->rc));
        return dds_pub->rc;
    }

    {
      const char *wait_match = getenv ("AMSHMEM_DDS_WAIT_MATCH");
      if (wait_match && wait_match[0] == '1' && wait_match[1] == '\0') {
        printf ("=== [Publisher domain=%u topic=%s] AMSHMEM_DDS_WAIT_MATCH=1: waiting for reader ...\n",
            (unsigned) domain_id, topic);
        if (amshmem_wait_publication_matched_loop (dds_pub->writer, domain_id, topic,
                DDS_SECS (120)) != 0)
          return -1;
      } else {
        printf ("=== [Publisher domain=%u topic=%s] writer ready (async discovery; AMSHMEM_DDS_WAIT_MATCH=1 to block)\n",
            (unsigned) domain_id, topic);
      }
    }

    return 0;
}

int run_publisher(dds_publisher_ctx *dds_pub, int msg_type)
{
    int ret = 0;
    static unsigned freeframe_ok_log_left = 64;

    switch (msg_type) {
        case FREE_FRAME_MSGTYPE:
            dds_pub->rc = dds_write(dds_pub->writer, &dds_pub->free_frame_msg);
            if (dds_pub->rc != DDS_RETCODE_OK) {
                printf ("=== [FreeFrame dds_write FAIL] msg_id=%u frame_id=%u buf_idx=%u: %s\n",
                    (unsigned) dds_pub->free_frame_msg.msg_id,
                    (unsigned) dds_pub->free_frame_msg.frame_id,
                    (unsigned) dds_pub->free_frame_msg.buffer_index,
                    dds_strretcode (-dds_pub->rc));
                DDS_FATAL("dds_write: %s\n", dds_strretcode(-dds_pub->rc));
                ret = dds_pub->rc;
            } else if (freeframe_ok_log_left > 0) {
                printf ("=== [FreeFrame dds_write ok] msg_id=%u frame_id=%u buf_idx=%u phys_y=0x%" PRIx64 "\n",
                    (unsigned) dds_pub->free_frame_msg.msg_id,
                    (unsigned) dds_pub->free_frame_msg.frame_id,
                    (unsigned) dds_pub->free_frame_msg.buffer_index,
                    (uint64_t) dds_pub->free_frame_msg.phys_y_addr);
                freeframe_ok_log_left--;
            }
            break;

        case AM_SHMEM_MSGTYPE:
            dds_pub->rc = dds_write(dds_pub->writer, &dds_pub->am_shmem_msg);
            if (dds_pub->rc != DDS_RETCODE_OK) {
                DDS_FATAL("dds_write: %s\n", dds_strretcode(-dds_pub->rc));
                ret = dds_pub->rc;
            }
            break;

        default:
            printf("unknown msg_type:%d\n", msg_type);
            ret = -1;
            break;
    }

    return ret;
}

int delete_publisher(dds_publisher_ctx *dds_pub)
{
    int ret = 0;
    if (dds_pub->participant > 0) {
        dds_pub->rc = dds_delete(dds_pub->participant);
        if (dds_pub->rc != DDS_RETCODE_OK) {
            DDS_FATAL("dds_delete: %s\n", dds_strretcode(-dds_pub->rc));
            ret = dds_pub->rc;
        }

        dds_pub->participant = DDS_ENTITY_NIL;
    }

    return ret;
}
