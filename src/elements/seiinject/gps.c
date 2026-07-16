/*
 * gps.c
 *
 * Read GPS from Linux serial (NMEA 0183). Optional: device path property.
 *
 * Copyright (C) 2025 Ambarella International LP
 */

#include "gps.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#ifdef __linux__
#include <termios.h>
#endif

static int gps_fd = -1;
static GpsFix gps_fix;
static GMutex gps_lock;

static int
open_serial (const char *path)
{
  int fd;
#ifdef __linux__
  struct termios tty;
#endif

  if (!path || path[0] == '\0')
    return -1;
  fd = open (path, O_RDONLY | O_NOCTTY | O_NONBLOCK);
  if (fd < 0)
    return -1;
#ifdef __linux__
  if (tcgetattr (fd, &tty) == 0) {
    cfsetispeed (&tty, B4800);
    cfsetospeed (&tty, B4800);
    tty.c_cflag &= ~(PARENB | PARODD | CRTSCTS | CSIZE);
    tty.c_cflag |= CS8 | CREAD | CLOCAL;
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY | INLCR | ICRNL);
    tty.c_oflag &= ~OPOST;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 5;
    tcsetattr (fd, TCSANOW, &tty);
  }
#endif
  return fd;
}

static void
parse_gga (const char *line)
{
  /* $GPGGA,123519.00,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47 */
  double lat = 0, lon = 0, alt = 0;
  int q;
  char ns = 0, ew = 0;
  const char *p = line;

  if (strncmp (p, "$GPGGA,", 7) != 0 && strncmp (p, "$GNGGA,", 7) != 0)
    return;
  p += 7;
  while (*p && *p != ',') p++;
  if (*p) p++;
  /* lat: DDMM.MMMM */
  if (sscanf (p, "%lf,%c", &lat, &ns) < 2) return;
  while (*p && *p != ',') p++;
  if (*p) p++;
  /* lon: DDDMM.MMMM */
  if (sscanf (p, "%lf,%c", &lon, &ew) < 2) return;
  while (*p && *p != ',') p++;
  if (*p) p++;
  if (sscanf (p, "%d", &q) != 1 || q == 0) return; /* fix quality */
  while (*p && *p != ',') p++;
  if (*p) p++;
  while (*p && *p != ',') p++;
  if (*p) p++;
  if (sscanf (p, "%lf", &alt) != 1) return;

  /* Convert to degrees */
  int lat_deg = (int)(lat / 100);
  double lat_min = lat - lat_deg * 100;
  lat = lat_deg + lat_min / 60;
  if (ns == 'S') lat = -lat;
  int lon_deg = (int)(lon / 100);
  double lon_min = lon - lon_deg * 100;
  lon = lon_deg + lon_min / 60;
  if (ew == 'W') lon = -lon;

  g_mutex_lock (&gps_lock);
  gps_fix.valid = TRUE;
  gps_fix.lat = (int32_t)(lat * 1e7);
  gps_fix.lon = (int32_t)(lon * 1e7);
  gps_fix.alt_cm = (int32_t)(alt * 100);
  g_mutex_unlock (&gps_lock);
}

static void
read_one_nmea (void)
{
  char line[128];
  int n = 0;

  if (gps_fd < 0)
    return;
  while (n < (int)sizeof (line) - 1) {
    char c;
    if (read (gps_fd, &c, 1) != 1)
      break;
    if (c == '\r' || c == '\n') {
      if (n > 0) {
        line[n] = '\0';
        parse_gga (line);
      }
      return;
    }
    line[n++] = c;
  }
}

gboolean
gps_init (const char *device_path)
{
  g_mutex_init (&gps_lock);
  gps_fix.valid = FALSE;
  gps_fix.lat = gps_fix.lon = gps_fix.alt_cm = 0;

  if (!device_path || device_path[0] == '\0') {
    gps_fd = -1;
    return TRUE;
  }
  gps_fd = open_serial (device_path);
  if (gps_fd < 0)
    return TRUE;  /* no device is not fatal */
  return TRUE;
}

void
gps_deinit (void)
{
  if (gps_fd >= 0) {
    close (gps_fd);
    gps_fd = -1;
  }
  g_mutex_clear (&gps_lock);
}

void
gps_poll (void)
{
  read_one_nmea ();
}

static void
put_le32 (guint8 *p, int32_t v)
{
  guint32 u = (guint32) v;
  p[0] = (guint8) (u);
  p[1] = (guint8) (u >> 8);
  p[2] = (guint8) (u >> 16);
  p[3] = (guint8) (u >> 24);
}

size_t
gps_get_payload (guint8 *out, size_t out_max)
{
  GpsFix fix;

  if (!out || out_max < GPS_PAYLOAD_BYTES)
    return 0;

  g_mutex_lock (&gps_lock);
  fix = gps_fix;
  g_mutex_unlock (&gps_lock);

  out[0] = fix.valid ? 1 : 0;
  put_le32 (out + 1, fix.lat);
  put_le32 (out + 5, fix.lon);
  put_le32 (out + 9, fix.alt_cm);
  return GPS_PAYLOAD_BYTES;
}
