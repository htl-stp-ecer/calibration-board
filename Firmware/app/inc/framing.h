#pragma once

#include <stdint.h>
#include <stdbool.h>

/* ─── Binär-Frame-Protokoll für USB-CDC Streaming ──────────────────────────
 *
 * Layout (alle Mehrbyte-Felder little-endian):
 *
 *   +0  uint8  SYNC      = 0xA5
 *   +1  uint8  TYPE      (FRAME_TYPE_*)
 *   +2  uint8  LEN       (Anzahl Payload-Bytes nach diesem Header,
 *                         OHNE CRC am Ende)
 *   +3  uint32 T_MS      HAL_GetTick() zum Samplezeitpunkt
 *   +7  PAYLOAD[LEN]     Sensor-Roh-Daten
 *   +7+LEN  uint8 CRC8   CRC-8/SMBUS (poly 0x07, init 0x00) über die
 *                        Bytes TYPE..letztes Payload-Byte (NICHT SYNC).
 *
 * Der Host synct auf 0xA5, prüft CRC.  Falsche CRC → 1 Byte vorrücken und
 * erneut synchronisieren.  Der Header ist absichtlich klein damit auf
 * USB-FS bei 1 kHz ODR genug Headroom bleibt.
 *
 * Payloads:
 *   ICM (type 0x01):  ax,ay,az, gx,gy,gz, temp   — alle int16  → 14 Byte
 *                     LSB: 8192 / g  (FS ±4 g)
 *                          16.384 / dps (FS ±2000 dps)
 *                          temp_c = raw/132.48 + 25
 *   PAA (type 0x02):  dx,dy (int16), squal,shutter_hi,shutter_lo,motion (uint8)
 *                     → 10 Byte
 */

#define FRAME_SYNC            0xA5u
#define FRAME_TYPE_ICM        0x01u
#define FRAME_TYPE_PAA        0x02u
#define FRAME_TYPE_STATUS     0x03u
/* PAA_CAL — Firmware → Host, 1 Hz (oder bei Änderung): aktuelle PAA-
 * Kalibrierung wie sie aus dem Flash geladen ist. */
#define FRAME_TYPE_PAA_CAL    0x04u

/* ORIENTATION — Firmware → Host, 100 Hz: aktueller Quaternion +
 * korrigierter Gyro + at-rest Flag + Bias.  Bridge nutzt das für
 * Odometrie und Display. */
#define FRAME_TYPE_ORIENTATION 0x05u

/* PAA_ACC — Firmware → Host: frei laufender, vorzeichenbehafteter
 * Zähler der dx/dy-Counts (int32).  Die Platine integriert selbst; der
 * Host akkumuliert NICHT mehr.  Der Kalibrier-Wizard liest Start-/End-
 * Stand und nimmt die Differenz → netto-Verschiebung in Counts. */
#define FRAME_TYPE_PAA_ACC     0x06u

/* Host → Firmware Command-Frames.  Selbes Wire-Format (SYNC/TYPE/LEN/
 * T_MS/CRC), T_MS wird vom Host gesetzt und ignoriert. */
#define FRAME_TYPE_CMD_SET_PAA_CAL  0x10u
/* Snapshot des aktuellen at-rest gemittelten Bias in den Flash. */
#define FRAME_TYPE_CMD_SAVE_GYRO_BIAS 0x11u
/* Bias auf 0 zurücksetzen (nicht im Flash). */
#define FRAME_TYPE_CMD_RESET_GYRO_BIAS 0x12u
/* PAA-Montageoffset vom Drehzentrum (mm) setzen + in Flash schreiben. */
#define FRAME_TYPE_CMD_SET_PAA_OFFSET 0x13u

#define FRAME_PAYLOAD_ICM     14u
#define FRAME_PAYLOAD_PAA     10u
/* PAA_ACC payload: acc_x (int32 LE) + acc_y (int32 LE) = 8 Byte */
#define FRAME_PAYLOAD_PAA_ACC 8u
#define FRAME_PAYLOAD_STATUS  20u
/* PAA_CAL payload: cx_per_cm (f32 LE) + cy_per_cm (f32 LE) +
 *                  height_mm (f32 LE) + off_x_mm (f32 LE) +
 *                  off_y_mm (f32 LE) + valid (u8) = 21 Byte */
#define FRAME_PAYLOAD_PAA_CAL 21u
/* CMD_SET_PAA_CAL payload: cx (f32) + cy (f32) + height (f32) = 12 B */
#define FRAME_PAYLOAD_CMD_SET_PAA_CAL 12u
/* CMD_SET_PAA_OFFSET payload: off_x_mm (f32) + off_y_mm (f32) = 8 B */
#define FRAME_PAYLOAD_CMD_SET_PAA_OFFSET 8u

/* ORIENTATION payload (41 B):
 *   +0   float    qw
 *   +4   float    qx
 *   +8   float    qy
 *   +12  float    qz
 *   +16  float    gyro_x_corrected_dps
 *   +20  float    gyro_y_corrected_dps
 *   +24  float    gyro_z_corrected_dps
 *   +28  float    bias_x_dps
 *   +32  float    bias_y_dps
 *   +36  float    bias_z_dps
 *   +40  uint8    flags  (bit0 = at_rest, bit1 = bias_persisted)
 *   100 Hz × 41 B = 4.1 KB/s — auf USB-FS gut vertretbar. */
#define FRAME_PAYLOAD_ORIENTATION 41u

/* Commands ohne Payload (Trigger). */
#define FRAME_PAYLOAD_CMD_NONE 0u
/* STATUS-Payload (alle little-endian):
 *   +0   int8   icm_init_status
 *   +1   int8   paa_init_status
 *   +2   uint8  paa_who_am_i_present  (1 wenn PAA antwortet, sonst 0)
 *   +3   uint8  reserved              (für Alignment)
 *   +4   uint32 icm_sample_count
 *   +8   uint32 icm_dropped_frames
 *   +12  uint32 paa_init_attempts
 *   +16  uint16 paa_reconnect_count
 *   +18  uint16 paa_disconnect_count
 */

#define FRAME_HDR_BYTES     7u   /* sync + type + len + t_ms */
#define FRAME_OVERHEAD      8u   /* hdr + crc */

/* Sendet ein Frame über die CDC-TX-Ring.  Non-blocking; bei voller Ring
 * wird das Frame komplett verworfen (kein Teil-Frame), damit Streams
 * intakt bleiben.  Return: true wenn geschrieben. */
bool frame_send(uint8_t type, const uint8_t *payload, uint8_t len);

/* RX-Pfad: füttert Bytes vom Host in den Frame-Parser.  Bei einem
 * vollständigen Command-Frame mit gültiger CRC wird der Handler aufgerufen.
 * Aufrufen aus dem USB-CDC-Receive-Callback, kommt also potenziell aus
 * Interrupt-Kontext — Handler dürfen NICHT lange blocken. */
typedef void (*frame_cmd_handler_t)(uint8_t type, const uint8_t *payload, uint8_t len);
void frame_set_cmd_handler(frame_cmd_handler_t h);
void frame_feed_rx(const uint8_t *data, uint32_t len);
