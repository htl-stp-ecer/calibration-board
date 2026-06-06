#include <stdio.h>
#include <string.h>

#include "main.h"
#include "stm32f7xx_hal.h"

#include "module.h"
#include "icm42688p.h"
#include "framing.h"
#include "imu_fusion.h"

/* ICM-Modul: liest so schnell wie SPI3 das hergibt und schiebt jedes
 * Sample als binäres Frame in die USB-CDC-Ring.  Kein Throttle — bei
 * 13.5 MHz SPI sind ~1 kHz Samples drin, was perfekt zur ICM-ODR passt.
 * Debug-Globals bleiben fürs SWD-Probing aktiv. */

static icm42688p_status_t s_init_status = ICM42688P_ERR_IO;
static uint32_t           s_last_tick;

volatile int      g_icm_module_init_status = 0xDEADBEEF;
volatile uint32_t g_icm_dropped_frames     = 0;

static void icm_setup(void)
{
    printf("[icm] init...\r\n");
    s_init_status = icm42688p_init();
    g_icm_module_init_status = (int)s_init_status;
    printf("[icm] init -> %d (whoami=0x%02x)\r\n",
           (int)s_init_status, icm42688p_get_who_am_i());
}

static void icm_loop(void)
{
    if (s_init_status != ICM42688P_OK) return;

    /* ICM-ODR ist 1 kHz — schneller polling liefert nur Duplikate. Wir
     * lesen genau einmal pro SysTick-Millisekunde.  Sauberer wäre DRDY
     * via EXTI auf PA15, aber das bleibt für später (siehe ICM_INT-Pin
     * im docs/hardware.md). */
    uint32_t now = HAL_GetTick();
    if (now == s_last_tick) return;
    s_last_tick = now;

    icm42688p_sample_t s;
    if (icm42688p_read_sample(&s) != ICM42688P_OK) return;

    /* Skalierung: ICM-Counts → physikalische Einheiten.
     *   Gyro:  16.384 LSB/dps  (FS ±2000 dps)
     *   Accel: 8192.0 LSB/g    (FS ±4 g)
     *   Temp:  raw/132.48 + 25 °C
     * Diese Konstanten sind in icm42688p.c gespiegelt. */
    const float gx_dps = (float)s.gx / 16.384f;
    const float gy_dps = (float)s.gy / 16.384f;
    const float gz_dps = (float)s.gz / 16.384f;
    const float ax_g   = (float)s.ax / 8192.0f;
    const float ay_g   = (float)s.ay / 8192.0f;
    const float az_g   = (float)s.az / 8192.0f;
    const float temp_c = (float)s.temp / 132.48f + 25.0f;

    /* Fusion-Step + Bias-Tracking.  Sample-Periode = 1 ms (1 kHz ODR).
     * Konstant statt aus HAL_GetTick gerechnet — Jitter ist im
     * Sub-Sample-Bereich, wirkt sich auf den Filter kaum aus. */
    imu_fusion_update(gx_dps, gy_dps, gz_dps,
                      ax_g, ay_g, az_g,
                      0.001f, temp_c);

    /* Bias-korrigierte Gyro-Werte für das outgoing ICM-Frame —
     * Downstream-Konsumenten kriegen also "bereinigtes" Gyro. */
    float cgx, cgy, cgz;
    imu_fusion_get_corrected_gyro(&cgx, &cgy, &cgz);
    int16_t gx_corr = (int16_t)(cgx * 16.384f);
    int16_t gy_corr = (int16_t)(cgy * 16.384f);
    int16_t gz_corr = (int16_t)(cgz * 16.384f);

    uint8_t payload[FRAME_PAYLOAD_ICM];
    memcpy(&payload[0],  &s.ax,    2);
    memcpy(&payload[2],  &s.ay,    2);
    memcpy(&payload[4],  &s.az,    2);
    memcpy(&payload[6],  &gx_corr, 2);
    memcpy(&payload[8],  &gy_corr, 2);
    memcpy(&payload[10], &gz_corr, 2);
    memcpy(&payload[12], &s.temp,  2);

    if (!frame_send(FRAME_TYPE_ICM, payload, FRAME_PAYLOAD_ICM)) {
        g_icm_dropped_frames++;
    }
}

const module_t icm_module = {
    .name    = "icm",
    .setup   = icm_setup,
    .loop    = icm_loop,
    .enabled = true,
};
