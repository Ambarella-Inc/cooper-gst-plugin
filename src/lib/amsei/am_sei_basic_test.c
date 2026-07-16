/*
 * am_sei_basic_test.c
 *
 * History:
 *    04/09/2026 - [Yang Yu] created file
 *
 * Copyright (C) 2026 Ambarella International LP
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

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "am_sei.h"

typedef struct {
  const char *name;
  AmSeiCodec codec;
  const uint8_t *au;
  size_t au_len;
} BasicCase;

/* H264: VCL-first AU (IDR). */
static const uint8_t k_h264_vcl_au[] = {
  0x00, 0x00, 0x00, 0x01, 0x65, 0x88, 0x84, 0x21
};
/* H264: non-VCL (SPS) + VCL (IDR) AU. */
static const uint8_t k_h264_nonvcl_then_vcl_au[] = {
  0x00, 0x00, 0x00, 0x01, 0x67, 0x64, 0x00, 0x1f,
  0x00, 0x00, 0x00, 0x01, 0x65, 0x88, 0x84, 0x21
};
/* H265: VCL-first AU (type=1). */
static const uint8_t k_h265_vcl_au[] = {
  0x00, 0x00, 0x00, 0x01, 0x02, 0x01, 0xaa, 0xbb
};
/* H265: non-VCL (VPS type=32) + VCL (type=1) AU. */
static const uint8_t k_h265_nonvcl_then_vcl_au[] = {
  0x00, 0x00, 0x00, 0x01, 0x40, 0x01, 0x11, 0x22,
  0x00, 0x00, 0x00, 0x01, 0x02, 0x01, 0xaa, 0xbb
};
static const BasicCase k_cases[] = {
  {"h264_vcl", AM_SEI_CODEC_H264, k_h264_vcl_au, sizeof (k_h264_vcl_au)},
  {"h264_nonvcl_then_vcl", AM_SEI_CODEC_H264,
      k_h264_nonvcl_then_vcl_au, sizeof (k_h264_nonvcl_then_vcl_au)},
  {"h265_vcl", AM_SEI_CODEC_H265, k_h265_vcl_au, sizeof (k_h265_vcl_au)},
  {"h265_nonvcl_then_vcl", AM_SEI_CODEC_H265,
      k_h265_nonvcl_then_vcl_au, sizeof (k_h265_nonvcl_then_vcl_au)},
};

static void
run_case (const BasicCase *c, const AmSeiTimestamp ts_in, const AmSeiGpsData *gps_in,
    AmSeiInfo *in_info, AmSeiInfo *out_info)
{
  uint8_t *injected_au = NULL;
  size_t injected_au_len = 0;
  AmSeiTimestamp ts_out = 0;
  AmSeiGpsData gps_out;
  AmSeiDecodeMeta meta;
  int rc;

  assert (in_info != NULL && out_info != NULL);

  /* Stage A: input preparation + inject. */
  am_sei_info_reset (in_info);

  rc = am_sei_info_set (in_info, AM_SEI_T_TIMESTAMP,
      &ts_in, AM_SEI_INFO_VALUE_SIZE_TIMESTAMP);
  assert (rc == AM_SEI_OK);
  rc = am_sei_info_set (in_info, AM_SEI_T_GPS,
      gps_in, AM_SEI_INFO_VALUE_SIZE_GPS);
  assert (rc == AM_SEI_OK);

  rc = am_sei_inject_au_with_info (c->au, c->au_len, c->codec,
      in_info, &injected_au, &injected_au_len);
  assert (rc == AM_SEI_OK);
  assert (injected_au != NULL);
  assert (injected_au_len > c->au_len);

  /* Stage B: output preparation + parse + verify round-trip. */
  am_sei_info_reset (out_info);
  am_sei_decode_meta_init (&meta);
  rc = am_sei_parse_au_to_info (injected_au, injected_au_len, c->codec, out_info, &meta);
  assert (rc == AM_SEI_OK);

  memset (&gps_out, 0, sizeof (gps_out));
  assert (am_sei_info_is_present (out_info, AM_SEI_T_TIMESTAMP) == 1);
  assert (am_sei_info_is_present (out_info, AM_SEI_T_GPS) == 1);

  rc = am_sei_info_get (out_info, AM_SEI_T_TIMESTAMP,
      &ts_out, AM_SEI_INFO_VALUE_SIZE_TIMESTAMP);
  assert (rc == AM_SEI_OK);
  assert (ts_out == ts_in);

  rc = am_sei_info_get (out_info, AM_SEI_T_GPS,
      &gps_out, AM_SEI_INFO_VALUE_SIZE_GPS);
  assert (rc == AM_SEI_OK);
  assert (gps_out.valid == gps_in->valid);
  assert (gps_out.lat_e7 == gps_in->lat_e7);
  assert (gps_out.lon_e7 == gps_in->lon_e7);
  assert (gps_out.alt_cm == gps_in->alt_cm);

  assert (meta.payload_version == AM_SEI_PAYLOAD_VERSION_V1);
  assert ((meta.payload_flags & AM_SEI_FLAG_HAS_TIMESTAMP) != 0);
  assert ((meta.payload_flags & AM_SEI_FLAG_HAS_GPS) != 0);

  am_sei_free (injected_au);

  printf ("[%s] passed\n", c->name);
}

int
main (void)
{
  /* Example business inputs written into AMSE payload. */
  AmSeiTimestamp ts_in = 123456789ULL;
  AmSeiGpsData gps_in;
  /* Reusable per-frame containers (one encode input, one decode output). */
  AmSeiInfo *in_info = NULL;
  AmSeiInfo *out_info = NULL;
  size_t i;
  int rc;

  memset (&gps_in, 0, sizeof (gps_in));
  gps_in.valid = 1;
  gps_in.lat_e7 = 223000000;
  gps_in.lon_e7 = 1141700000;
  gps_in.alt_cm = 2500;

  /* Public lifecycle entry; currently lightweight/no-op but keep call for
   * forward compatibility with future global resource management. */
  rc = am_sei_init ();
  assert (rc == AM_SEI_OK);

  /* Typical usage model: allocate once, reuse for many AUs via info_reset(). */
  in_info = am_sei_info_new ();
  out_info = am_sei_info_new ();
  assert (in_info != NULL && out_info != NULL);

  /* Run matrix cases to validate inject/parse round-trip across codecs and
   * VCL/non-VCL AU layouts. */
  for (i = 0; i < sizeof (k_cases) / sizeof (k_cases[0]); ++i)
    run_case (&k_cases[i], ts_in, &gps_in, in_info, out_info);

  /* Release per-frame containers and close lifecycle hook. */
  am_sei_info_free (in_info);
  am_sei_info_free (out_info);
  am_sei_deinit ();

  printf ("am_sei_basic_test all cases passed.\n");
  return 0;
}
