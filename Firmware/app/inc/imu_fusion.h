#pragma once

#include <stdbool.h>
#include <stdint.h>

/* 6-DOF Sensor-Fusion (Gyro + Accel) auf STM32F7 mit FPU.
 *
 * - Madgwick-Quaternion-Filter (kein Magnetometer) — Yaw driftet
 *   langsam, Roll/Pitch werden vom Gravity-Vektor korrigiert.
 * - At-Rest-Detection per Gyro-Magnitude + |Accel| ≈ 1 g.  Während
 *   at-rest läuft eine Exponential-Moving-Average auf den rohen
 *   Gyro-Werten → das ist der Bias.
 * - Der Bias wird live abgezogen bevor er in den Madgwick geht
 *   und bevor er auf USB raus geht (g_icm_gx/gy/gz Felder).
 *
 * Sample-Rate: 1 kHz (ICM-ODR).  Filter-Step ≈ 1 µs auf M7@216 MHz mit FPU.
 *
 * Frame-Konvention (ICM-42688-P body):
 *   x = +X axis chip, y = +Y, z = +Z (orientation per Footprint auf PCB)
 *   Wenn man die Orientation auf andere Robot-Achsen mappen will, macht
 *   man das ein-mal in der Bridge — wir leben hier in Sensor-Body-Frame.
 *
 * Quaternion-Konvention: q = w + i·x + j·y + k·z  (Hamilton, q rotates
 * body→world).  Bei Reset = (1, 0, 0, 0) = Identity. */

void imu_fusion_init(float gyro_bias_x_dps,
                     float gyro_bias_y_dps,
                     float gyro_bias_z_dps);

/* Pro ICM-Sample aufrufen.  Roh-Werte aus dem Treiber (vor Bias-
 * Subtraktion); die Funktion subtrahiert den aktuellen Bias selbst.
 *   dt_s: Sample-Periode in Sekunden (bei 1 kHz = 0.001f).
 *   temp_c: Aktuelle ICM-Die-Temperatur, für Bias-Capture-Marker. */
void imu_fusion_update(float gx_raw_dps, float gy_raw_dps, float gz_raw_dps,
                       float ax_g, float ay_g, float az_g,
                       float dt_s, float temp_c);

/* Aktuelle korrigierte Werte (Bias subtrahiert) — Caller benutzt die
 * statt der Raw-Werte für ICM-Telemetry. */
void imu_fusion_get_corrected_gyro(float *gx, float *gy, float *gz);
void imu_fusion_get_quaternion(float *qw, float *qx, float *qy, float *qz);
void imu_fusion_get_bias(float *bx, float *by, float *bz);
float imu_fusion_get_bias_temp(void);
bool  imu_fusion_is_at_rest(void);

/* Manuelle Bias-Operationen (vom Command-Handler aufgerufen). */
void imu_fusion_reset_bias(void);
void imu_fusion_snapshot_bias_temp(void);  /* aktuelle Temp als Capture-Temp */
