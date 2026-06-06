#include "calib_store.h"

#include <string.h>

#include "stm32f7xx_hal.h"

#define CALIB_FLASH_SECTOR     FLASH_SECTOR_7
#define CALIB_FLASH_ADDR       0x08060000u   /* Sektor 7 Start */
#define CALIB_BLOB_BYTES       64u           /* PAA + ICM bias + temp, aligned 64 B */

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
    uint32_t reserved[5];     /* +40 .. +59  — Platz für künftige Felder */
    uint32_t crc32;           /* +60 */
} blob_t;
_Static_assert(sizeof(blob_t) == CALIB_BLOB_BYTES, "blob size drifted");

/* Diagnose-Globals — über SWD lesbar. */
volatile int      g_calib_load_result = 0;   /* 0=OK, 1=defaults, 2=bad-magic, 3=bad-crc */
volatile int      g_calib_save_result = 0;
volatile uint32_t g_calib_save_count  = 0;

static void apply_defaults(calib_store_t *out)
{
    out->paa_cx_per_cm   = CALIB_DEFAULT_CX_PER_CM;
    out->paa_cy_per_cm   = CALIB_DEFAULT_CY_PER_CM;
    out->paa_height_mm   = CALIB_DEFAULT_HEIGHT_MM;
    out->icm_gyro_bias_x = 0.0f;
    out->icm_gyro_bias_y = 0.0f;
    out->icm_gyro_bias_z = 0.0f;
    out->icm_bias_temp_c = 0.0f;
    out->calibrated_ms   = 0;
    out->valid           = false;
}

void calib_store_load(calib_store_t *out)
{
    const blob_t *b = (const blob_t *)CALIB_FLASH_ADDR;

    if (b->magic != CALIB_STORE_MAGIC) {
        apply_defaults(out);
        g_calib_load_result = (b->magic == 0xFFFFFFFFu) ? 1 : 2;
        return;
    }
    /* CRC über die ersten 28 Bytes (alles außer dem CRC-Feld). */
    uint32_t want = crc32_ieee((const uint8_t *)b, CALIB_BLOB_BYTES - 4u);
    if (want != b->crc32) {
        apply_defaults(out);
        g_calib_load_result = 3;
        return;
    }

    out->paa_cx_per_cm   = b->cx_per_cm;
    out->paa_cy_per_cm   = b->cy_per_cm;
    out->paa_height_mm   = b->height_mm;
    out->icm_gyro_bias_x = b->gyro_bias_x;
    out->icm_gyro_bias_y = b->gyro_bias_y;
    out->icm_gyro_bias_z = b->gyro_bias_z;
    out->icm_bias_temp_c = b->bias_temp_c;
    out->calibrated_ms   = b->calibrated_ms;
    out->valid           = true;
    g_calib_load_result = 0;
}

int calib_store_save(const calib_store_t *cfg)
{
    blob_t b = {
        .magic         = CALIB_STORE_MAGIC,
        .schema_ver    = CALIB_STORE_VERSION,
        .cx_per_cm     = cfg->paa_cx_per_cm,
        .cy_per_cm     = cfg->paa_cy_per_cm,
        .height_mm     = cfg->paa_height_mm,
        .gyro_bias_x   = cfg->icm_gyro_bias_x,
        .gyro_bias_y   = cfg->icm_gyro_bias_y,
        .gyro_bias_z   = cfg->icm_gyro_bias_z,
        .bias_temp_c   = cfg->icm_bias_temp_c,
        .calibrated_ms = cfg->calibrated_ms,
        .reserved      = {0},
        .crc32         = 0,
    };
    b.crc32 = crc32_ieee((const uint8_t *)&b, CALIB_BLOB_BYTES - 4u);

    if (HAL_FLASH_Unlock() != HAL_OK) {
        g_calib_save_result = -1;
        return -1;
    }

    /* Sektor 7 erasen — voltage range 3 (VDD ≥ 2.7 V) für 32-bit
     * Parallelism, das ist bei 3V3-Versorgung sicher. */
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

    /* 8× uint32 ans Sektor-Start schreiben. */
    const uint32_t *words = (const uint32_t *)&b;
    for (uint32_t i = 0; i < CALIB_BLOB_BYTES / 4u; i++) {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                              CALIB_FLASH_ADDR + i * 4u,
                              words[i]) != HAL_OK) {
            HAL_FLASH_Lock();
            g_calib_save_result = -3;
            return -3;
        }
    }
    HAL_FLASH_Lock();

    g_calib_save_count++;
    g_calib_save_result = 0;
    return 0;
}
