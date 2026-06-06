#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Persistente Kalibrierungs-Daten — leben in STM32-Flash Sektor 7
 * (0x08060000–0x0807FFFF, 128 KB).  Wir schreiben dort genau einen
 * 64-Byte aligned Block; der Rest des Sektors bleibt 0xFF.
 *
 * Sektor 7 ist gewählt weil der Firmware-Code mit ~60 KB komplett in
 * Sektoren 0..3 (64 KB total) passt — Sektor 4..7 sind frei.  Wenn die
 * Firmware mal über ~448 KB wächst muss dieses Layout angepasst werden.
 *
 * Format (alle Felder little-endian, native ARM):
 *
 *   +0   uint32  magic       = 0xCAB10001   (Magic + Version implizit)
 *   +4   uint32  schema_ver  = 1
 *   +8   float   paa_cx_per_cm
 *   +12  float   paa_cy_per_cm
 *   +16  float   paa_height_mm
 *   +20  uint32  calibrated_unix_ms_lo  (HAL_GetTick beim Save — relativ
 *                                        zur Board-Bootzeit; nur zur
 *                                        Anzeige "Cal von vor X min")
 *   +24  uint32  reserved
 *   +28  uint32  crc32       (über Byte 0..27)
 *
 *  Default wenn Flash leer / CRC kaputt:
 *    paa_cx_per_cm = 11.9   (geschätzt für 19 mm Höhe)
 *    paa_cy_per_cm = 11.9
 *    paa_height_mm = 19.0
 *    valid         = false   (Caller weiß "noch nie kalibriert")
 */

/* Magic bumped auf 0xCAB10002 weil das Blob-Layout neue Felder kriegt
 * (Gyro-Bias + Capture-Temp).  Alte Sektor-7-Inhalte werden als invalid
 * gelesen und auf Defaults gefallen — der User muss einmal neu
 * kalibrieren, das ist OK weil PAA-Cal eh erst gerade eingeführt wurde. */
#define CALIB_STORE_MAGIC      0xCAB10002u
#define CALIB_STORE_VERSION    2u
#define CALIB_DEFAULT_CX_PER_CM 11.9f
#define CALIB_DEFAULT_CY_PER_CM 11.9f
#define CALIB_DEFAULT_HEIGHT_MM 19.0f

typedef struct {
    /* PAA optical flow scaling */
    float    paa_cx_per_cm;
    float    paa_cy_per_cm;
    float    paa_height_mm;
    /* ICM gyro bias (dps) — bei at-rest gemessen.  Vom Fusion-Modul
     * automatisch nachgeführt (EMA), vom FW subtrahiert bevor jeder
     * ICM-Sample in den Madgwick-Filter und in das Telemetry-Frame
     * geht. */
    float    icm_gyro_bias_x;
    float    icm_gyro_bias_y;
    float    icm_gyro_bias_z;
    /* Temperatur bei der der Bias zuletzt persistiert wurde.  Phase 2:
     * über mehrere Temps capturen und linear interpolieren (dB/dT ~
     * 0.05 dps/°C laut ICM-42688-P Datasheet).  Für jetzt: Doku. */
    float    icm_bias_temp_c;

    uint32_t calibrated_ms;  /* HAL_GetTick beim Save, 0 wenn nie */
    bool     valid;          /* false = aus Defaults gelesen, nicht aus Flash */
} calib_store_t;

/* Liest die Kalibrierung aus Flash.  Bei kaputter CRC oder Magic werden
 * die Defaults eingetragen und valid=false gesetzt.  Idempotent —
 * Caller kann jederzeit aufrufen, kostet ein paar µs (nur Memory-Read +
 * CRC). */
void calib_store_load(calib_store_t *out);

/* Schreibt die übergebene Kalibrierung ins Flash.  Blockiert für ~1 s
 * (Sector-Erase) während der Operation — Interrupts werden NICHT
 * deaktiviert, aber der HAL_FLASH-Treiber wartet busy auf BSY/EOP.
 * Caller sollte das nicht im Sample-Hot-Path machen — UI-getriggert ist OK.
 * Return 0 = OK, negativ = HAL-Fehler. */
int  calib_store_save(const calib_store_t *cfg);
