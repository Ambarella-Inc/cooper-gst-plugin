/*
 * gps.h
 *
 * GPS payload for SEI: read from Linux serial (NMEA) or binary driver.
 *
 * Copyright (C) 2025 Ambarella International LP
 */

#ifndef __GPS_H__
#define __GPS_H__

#include <gst/gst.h>
#include <stddef.h>
#include <stdint.h>

G_BEGIN_DECLS

/* Payload: 1 valid + 4 lat + 4 lon + 4 alt = 13 bytes. Lat/Lon in 1e-7 degrees, alt in cm. */
#define GPS_PAYLOAD_MAX 32
#define GPS_PAYLOAD_BYTES 13

typedef struct {
  gboolean valid;
  int32_t  lat;   /* degrees * 1e7 */
  int32_t  lon;
  int32_t  alt_cm;
} GpsFix;

/**
 * gps_init:
 * @device_path: e.g. "/dev/ttyUSB0" or NULL to disable.
 *
 * Opens the GPS device (if any). Call once from element init.
 * Returns TRUE if device opened or path is NULL (no GPS).
 */
gboolean gps_init (const char *device_path);

/**
 * gps_deinit:
 * Closes the device. Call from element finalize.
 */
void gps_deinit (void);

/**
 * gps_get_payload:
 * @out: output buffer for payload
 * @out_max: size of @out (use at least GPS_PAYLOAD_BYTES)
 *
 * Fills @out with last known fix: 1 byte valid, 4 bytes lat, 4 bytes lon, 4 bytes alt (LE).
 * If no fix or no device, valid=0 and rest zeros.
 * Returns number of bytes written.
 */
size_t gps_get_payload (guint8 *out, size_t out_max);

/**
 * gps_poll:
 * Reads one NMEA sentence from device (if open) and updates internal fix.
 * Call periodically (e.g. from chain or a timer).
 */
void gps_poll (void);

G_END_DECLS

#endif /* __GPS_H__ */
