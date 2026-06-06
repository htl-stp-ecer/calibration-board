#include <stdio.h>
#include <string.h>

#include "main.h"
#include "stm32f7xx_hal.h"

#include "module.h"
#include "framing.h"
#include "calib_store.h"
#include "imu_fusion.h"

/* Cal-Modul (PAA + ICM-Bias unified):
 *  - Lädt beim Boot Kalibrierung aus Flash, initialisiert imu_fusion
 *    mit den gespeicherten Gyro-Bias-Werten
 *  - Schiebt 1 Hz PAA_CAL telemetry (PAA-Scaling)
 *  - Schiebt 100 Hz ORIENTATION (Quaternion + bias + at-rest)
 *  - Hört auf RX-Commands:
 *      CMD_SET_PAA_CAL       — neue PAA-Scaling speichern
 *      CMD_SAVE_GYRO_BIAS    — aktuellen at-rest gemittelten Bias persistieren
 *      CMD_RESET_GYRO_BIAS   — Bias auf 0 (nicht persistent)
 *
 * Flash-Save läuft im Main-Loop (Pending-Flag aus Interrupt-Kontext),
 * weil HAL_FLASHEx_Erase blockt. */

#define ORIENTATION_TX_PERIOD_MS  10u    /* 100 Hz */

static calib_store_t s_cal;
static volatile bool s_save_pending;
static calib_store_t s_pending;
static uint32_t      s_last_paa_cal_tx;
static uint32_t      s_last_orient_tx;

/* Diagnose */
volatile int      g_paa_cal_loaded     = 0;
volatile uint32_t g_paa_cal_set_count  = 0;
volatile uint32_t g_gyro_bias_save_count  = 0;
volatile uint32_t g_gyro_bias_reset_count = 0;

static void on_cmd(uint8_t type, const uint8_t *payload, uint8_t len)
{
    switch (type) {
    case FRAME_TYPE_CMD_SET_PAA_CAL: {
        if (len != FRAME_PAYLOAD_CMD_SET_PAA_CAL) return;
        float cx, cy, h;
        memcpy(&cx, payload + 0, 4);
        memcpy(&cy, payload + 4, 4);
        memcpy(&h,  payload + 8, 4);
        if (cx <= 0.1f || cx > 10000.0f) return;
        if (cy <= 0.1f || cy > 10000.0f) return;
        if (h  <= 1.0f || h  > 200.0f)   return;

        s_pending = s_cal;  /* aktuellen State als Basis */
        s_pending.paa_cx_per_cm = cx;
        s_pending.paa_cy_per_cm = cy;
        s_pending.paa_height_mm = h;
        s_pending.calibrated_ms = HAL_GetTick();
        s_pending.valid         = true;
        s_save_pending          = true;
        g_paa_cal_set_count++;
        break;
    }
    case FRAME_TYPE_CMD_SAVE_GYRO_BIAS: {
        /* Aktueller EMA-Bias aus dem Fusion-Modul + die Live-Temp
         * als Capture-Marker. */
        float bx, by, bz;
        imu_fusion_get_bias(&bx, &by, &bz);
        s_pending = s_cal;
        s_pending.icm_gyro_bias_x = bx;
        s_pending.icm_gyro_bias_y = by;
        s_pending.icm_gyro_bias_z = bz;
        s_pending.icm_bias_temp_c = imu_fusion_get_bias_temp();
        s_pending.calibrated_ms   = HAL_GetTick();
        s_pending.valid           = true;
        s_save_pending            = true;
        g_gyro_bias_save_count++;
        break;
    }
    case FRAME_TYPE_CMD_RESET_GYRO_BIAS: {
        imu_fusion_reset_bias();
        g_gyro_bias_reset_count++;
        break;
    }
    default: break;
    }
}

static void send_paa_cal_telemetry(void)
{
    uint8_t p[FRAME_PAYLOAD_PAA_CAL];
    memcpy(p + 0, &s_cal.paa_cx_per_cm, 4);
    memcpy(p + 4, &s_cal.paa_cy_per_cm, 4);
    memcpy(p + 8, &s_cal.paa_height_mm, 4);
    p[12] = s_cal.valid ? 1 : 0;
    (void)frame_send(FRAME_TYPE_PAA_CAL, p, FRAME_PAYLOAD_PAA_CAL);
}

static void send_orientation_telemetry(void)
{
    float qw, qx, qy, qz;
    float gx, gy, gz;
    float bx, by, bz;
    imu_fusion_get_quaternion(&qw, &qx, &qy, &qz);
    imu_fusion_get_corrected_gyro(&gx, &gy, &gz);
    imu_fusion_get_bias(&bx, &by, &bz);

    uint8_t p[FRAME_PAYLOAD_ORIENTATION];
    memcpy(p +  0, &qw, 4);
    memcpy(p +  4, &qx, 4);
    memcpy(p +  8, &qy, 4);
    memcpy(p + 12, &qz, 4);
    memcpy(p + 16, &gx, 4);
    memcpy(p + 20, &gy, 4);
    memcpy(p + 24, &gz, 4);
    memcpy(p + 28, &bx, 4);
    memcpy(p + 32, &by, 4);
    memcpy(p + 36, &bz, 4);
    uint8_t flags = 0;
    if (imu_fusion_is_at_rest()) flags |= 0x01;
    if (s_cal.valid)              flags |= 0x02;
    p[40] = flags;
    (void)frame_send(FRAME_TYPE_ORIENTATION, p, FRAME_PAYLOAD_ORIENTATION);
}

static void cal_setup(void)
{
    calib_store_load(&s_cal);
    g_paa_cal_loaded = s_cal.valid ? 1 : 0;
    imu_fusion_init(s_cal.icm_gyro_bias_x,
                    s_cal.icm_gyro_bias_y,
                    s_cal.icm_gyro_bias_z);
    frame_set_cmd_handler(on_cmd);
    printf("[cal] loaded valid=%d  paa(cx=%.3f cy=%.3f h=%.2f mm)  gyro_bias=(%.3f %.3f %.3f) dps @ %.1f°C\r\n",
           (int)s_cal.valid,
           s_cal.paa_cx_per_cm, s_cal.paa_cy_per_cm, s_cal.paa_height_mm,
           s_cal.icm_gyro_bias_x, s_cal.icm_gyro_bias_y, s_cal.icm_gyro_bias_z,
           s_cal.icm_bias_temp_c);
}

static void cal_loop(void)
{
    if (s_save_pending) {
        s_save_pending = false;
        int rc = calib_store_save(&s_pending);
        if (rc == 0) {
            s_cal = s_pending;
            printf("[cal] saved  paa(cx=%.3f cy=%.3f)  bias=(%.3f %.3f %.3f) @ %.1f°C\r\n",
                   s_cal.paa_cx_per_cm, s_cal.paa_cy_per_cm,
                   s_cal.icm_gyro_bias_x, s_cal.icm_gyro_bias_y, s_cal.icm_gyro_bias_z,
                   s_cal.icm_bias_temp_c);
            send_paa_cal_telemetry();
        } else {
            printf("[cal] save FAILED rc=%d\r\n", rc);
        }
    }

    uint32_t now = HAL_GetTick();
    if ((now - s_last_paa_cal_tx) >= 1000u) {
        s_last_paa_cal_tx = now;
        send_paa_cal_telemetry();
    }
    if ((now - s_last_orient_tx) >= ORIENTATION_TX_PERIOD_MS) {
        s_last_orient_tx = now;
        send_orientation_telemetry();
    }
}

const module_t paa_cal_module = {
    .name    = "cal",
    .setup   = cal_setup,
    .loop    = cal_loop,
    .enabled = true,
};
