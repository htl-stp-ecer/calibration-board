#include "calib_store.h"

#include <string.h>

#include "stm32f7xx_hal.h"

#define CALIB_FLASH_SECTOR     FLASH_SECTOR_7
#define CALIB_FLASH_ADDR       0x08060000u   /* Sektor 7 Start */
#define CALIB_BLOB_BYTES       64u           /* PAA + ICM bias + temp, aligned 64 B */

/* Wear-Leveling: Sektor 7 (128 KB) fasst 2048 Blobs à 64 B.  Statt immer
 * an den Sektor-Anfang zu schreiben (und dafür jedes Mal den ganzen
 * Sektor zu erasen), hängen wir jeden Save an den nächsten freien
 * (gelöschten) Slot.  Der Sektor wird nur erased wenn alle Slots voll
 * sind — also einmal pro 2048 Saves.  Das streckt die ~10k garantierten
 * P/E-Zyklen effektiv auf ~20M Saves. */
#define CALIB_FLASH_SECTOR_BYTES 0x20000u    /* Sektor 7 = 128 KB */
#define CALIB_SLOT_COUNT         (CALIB_FLASH_SECTOR_BYTES / CALIB_BLOB_BYTES)  /* 2048 */

/* Schreib-Layout: 32 Bytes, 32-bit aligned.  Wir programmieren als 8×
 * 32-bit Words.  STM32F7 FLASH unterstützt auch 64-bit Programming,
 * aber 32-bit reicht und ist einfacher. */

static uint32_t crc32_ieee(const uint8_t *data, uint32_t len)
{
    /* Standard CRC-32/IEEE 802.3 — keine Hardware-CRC-Unit verwendet
     * (die ist anders parametriert), Software-Variante reicht hier. */
    uint32_t c = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; i++) {
        c ^= data[i];
        for (int b = 0; b < 8; b++) {
            c = (c >> 1) ^ (0xEDB88320u & -(int32_t)(c & 1));
        }
    }
    return ~c;
}

/* Compile-Time-Assert dass das Layout exakt 32 Bytes hat — sonst
 * driftet on-disk-Format auseinander. */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t schema_ver;
    /* PAA */
    float    cx_per_cm;       /* +8 */
    float    cy_per_cm;       /* +12 */
    float    height_mm;       /* +16 */
    /* ICM gyro bias (dps) + Erfassungs-Temperatur (°C) */
    float    gyro_bias_x;     /* +20 */
    float    gyro_bias_y;     /* +24 */
    float    gyro_bias_z;     /* +28 */
    float    bias_temp_c;     /* +32 */
    /* Metadata */
    uint32_t calibrated_ms;   /* +36 */
    /* PAA-Sensor-Montageoffset vom Drehzentrum (mm).  Belegt die ersten
     * beiden ehemaligen reserved-Words → alte Blobs (reserved=0) lesen
     * hier 0.0f, also "kein Offset". */
    float    off_x_mm;        /* +40 */
    float    off_y_mm;        /* +44 */
    uint32_t reserved[3];     /* +48 .. +59  — Platz für künftige Felder */
    uint32_t crc32;           /* +60 */
} blob_t;
_Static_assert(sizeof(blob_t) == CALIB_BLOB_BYTES, "blob size drifted");

/* Diagnose-Globals — über SWD lesbar. */
volatile int      g_calib_load_result = 0;   /* 0=OK, 1=defaults, 2=bad-magic, 3=bad-crc */
volatile int      g_calib_save_result = 0;
volatile uint32_t g_calib_save_count  = 0;
volatile uint32_t g_calib_slot        = 0;   /* aktuell genutzter Slot-Index */
volatile uint32_t g_calib_sector_erases = 0; /* wie oft der Sektor erased wurde */

static const blob_t *slot_ptr(uint32_t i)
{
    return (const blob_t *)(CALIB_FLASH_ADDR + i * CALIB_BLOB_BYTES);
}

static bool slot_is_valid(const blob_t *b)
{
    if (b->magic != CALIB_STORE_MAGIC) return false;
    return crc32_ieee((const uint8_t *)b, CALIB_BLOB_BYTES - 4u) == b->crc32;
}

static bool slot_is_erased(const blob_t *b)
{
    /* Magic ist das erste Word und wird zuerst programmiert — ist es noch
     * 0xFFFFFFFF, wurde der Slot nie angefasst (oder frisch erased). */
    return b->magic == 0xFFFFFFFFu;
}

static void apply_defaults(calib_store_t *out)
{
    out->paa_cx_per_cm   = CALIB_DEFAULT_CX_PER_CM;
    out->paa_cy_per_cm   = CALIB_DEFAULT_CY_PER_CM;
    out->paa_height_mm   = CALIB_DEFAULT_HEIGHT_MM;
    out->paa_off_x_mm    = 0.0f;
    out->paa_off_y_mm    = 0.0f;
    out->icm_gyro_bias_x = 0.0f;
    out->icm_gyro_bias_y = 0.0f;
    out->icm_gyro_bias_z = 0.0f;
    out->icm_bias_temp_c = 0.0f;
    out->calibrated_ms   = 0;
    out->valid           = false;
}

void calib_store_load(calib_store_t *out)
{
    /* Alle Slots scannen — der jüngste gültige (höchster Index, weil wir
     * immer aufsteigend anhängen) ist die aktuelle Kalibrierung.  Slots
     * mit kaputter CRC (z. B. abgebrochener Save bei Power-Loss) werden
     * übersprungen. */
    int latest = -1;
    for (uint32_t i = 0; i < CALIB_SLOT_COUNT; i++) {
        if (slot_is_valid(slot_ptr(i))) latest = (int)i;
    }

    if (latest < 0) {
        apply_defaults(out);
        g_calib_load_result = slot_is_erased(slot_ptr(0)) ? 1 : 2;
        g_calib_slot = 0;
        return;
    }

    const blob_t *b = slot_ptr((uint32_t)latest);
    out->paa_cx_per_cm   = b->cx_per_cm;
    out->paa_cy_per_cm   = b->cy_per_cm;
    out->paa_height_mm   = b->height_mm;
    out->paa_off_x_mm    = b->off_x_mm;
    out->paa_off_y_mm    = b->off_y_mm;
    out->icm_gyro_bias_x = b->gyro_bias_x;
    out->icm_gyro_bias_y = b->gyro_bias_y;
    out->icm_gyro_bias_z = b->gyro_bias_z;
    out->icm_bias_temp_c = b->bias_temp_c;
    out->calibrated_ms   = b->calibrated_ms;
    out->valid           = true;
    g_calib_load_result  = 0;
    g_calib_slot         = (uint32_t)latest;
}

int calib_store_save(const calib_store_t *cfg)
{
    blob_t b = {
        .magic         = CALIB_STORE_MAGIC,
        .schema_ver    = CALIB_STORE_VERSION,
        .cx_per_cm     = cfg->paa_cx_per_cm,
        .cy_per_cm     = cfg->paa_cy_per_cm,
        .height_mm     = cfg->paa_height_mm,
        .off_x_mm      = cfg->paa_off_x_mm,
        .off_y_mm      = cfg->paa_off_y_mm,
        .gyro_bias_x   = cfg->icm_gyro_bias_x,
        .gyro_bias_y   = cfg->icm_gyro_bias_y,
        .gyro_bias_z   = cfg->icm_gyro_bias_z,
        .bias_temp_c   = cfg->icm_bias_temp_c,
        .calibrated_ms = cfg->calibrated_ms,
        .reserved      = {0},
        .crc32         = 0,
    };
    b.crc32 = crc32_ieee((const uint8_t *)&b, CALIB_BLOB_BYTES - 4u);

    /* Nächsten freien (gelöschten) Slot suchen — anhängendes Schreiben
     * statt immer an den Sektor-Anfang.  Ein angefangener (CRC-kaputter)
     * Slot hat magic != 0xFFFFFFFF und gilt als belegt → wird nie
     * überschrieben. */
    uint32_t slot = CALIB_SLOT_COUNT;   /* Sentinel: "kein freier Slot" */
    for (uint32_t i = 0; i < CALIB_SLOT_COUNT; i++) {
        if (slot_is_erased(slot_ptr(i))) { slot = i; break; }
    }

    if (HAL_FLASH_Unlock() != HAL_OK) {
        g_calib_save_result = -1;
        return -1;
    }

    if (slot >= CALIB_SLOT_COUNT) {
        /* Sektor voll → einmal erasen und vorne neu anfangen.  Passiert
         * nur alle CALIB_SLOT_COUNT (2048) Saves → P/E-Last /2048.
         * Voltage range 3 (VDD ≥ 2.7 V) ist bei 3V3 sicher. */
        FLASH_EraseInitTypeDef erase = {
            .TypeErase    = FLASH_TYPEERASE_SECTORS,
            .Sector       = CALIB_FLASH_SECTOR,
            .NbSectors    = 1,
            .VoltageRange = FLASH_VOLTAGE_RANGE_3,
        };
        uint32_t sector_err = 0;
        if (HAL_FLASHEx_Erase(&erase, &sector_err) != HAL_OK) {
            HAL_FLASH_Lock();
            g_calib_save_result = -2;
            return -2;
        }
        g_calib_sector_erases++;
        slot = 0;
    }

    /* 16× uint32 in den freien Slot schreiben (in Slot-Reihenfolge:
     * magic zuerst, crc zuletzt — abgebrochener Save bleibt CRC-invalid). */
    const uint32_t addr = CALIB_FLASH_ADDR + slot * CALIB_BLOB_BYTES;
    const uint32_t *words = (const uint32_t *)&b;
    for (uint32_t i = 0; i < CALIB_BLOB_BYTES / 4u; i++) {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                              addr + i * 4u,
                              words[i]) != HAL_OK) {
            HAL_FLASH_Lock();
            g_calib_save_result = -3;
            return -3;
        }
    }
    HAL_FLASH_Lock();

    g_calib_slot = slot;
    g_calib_save_count++;
    g_calib_save_result = 0;
    return 0;
}
