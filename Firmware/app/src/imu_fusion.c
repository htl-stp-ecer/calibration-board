#include "imu_fusion.h"

#include <math.h>
#include <string.h>

/* ── Tuning ─────────────────────────────────────────────────────────
 * MADGWICK_BETA:
 *   Filter-Gain.  Höher = Accel-Anteil stärker (schnellere Korrektur
 *   von Roll/Pitch, aber empfindlicher gegen Bewegungs-Beschleunigung).
 *   0.05–0.1 typisch für 6-DOF ohne Mag.  Bei Bot-Anwendung mit viel
 *   linearer Beschleunigung würde man dynamisch reduzieren — wir
 *   bleiben pragmatisch.
 *
 * BIAS_EMA_ALPHA:
 *   Lernrate für den Bias.  α = 0.001 bei 1 kHz → effektive Zeit-
 *   konstante ~1 s.  Während at-rest 5 s = 5000 Updates konvergiert das
 *   sauber.
 *
 * STATS_EMA_ALPHA:
 *   Lernrate für die laufenden Mean/Varianz-Schätzer für die at-rest
 *   Detection.  α = 1e-4 bei 1 kHz → effektive Zeitkonstante ~10 s, d. h.
 *   die Statistik bezieht sich auf die letzten ~10 s an Samples.  Das
 *   ist genau das was der User wollte: "10 s relative window".
 *
 * REST_GYRO_STDDEV_DPS / REST_ACCEL_STDDEV_G:
 *   "Still" gilt wenn die laufende StdDev (sqrt der EMA-Varianz) auf
 *   ALLEN sechs Achsen unter dem Schwellwert liegt.  Das ist relativ
 *   zum eigenen Mittelwert — Bias ist also egal.  Schwellen großzügig
 *   gewählt: 3 dps Standardabweichung erlaubt typisches Hand-Auflegen
 *   ohne Mikro-Vibration als "Bewegung" zu zählen.
 *
 * WARMUP_MS:
 *   Direkt nach Boot ist die EMA-Varianz künstlich klein (Init = 0) →
 *   wir würden sofort at-rest signalisieren obwohl wir noch nichts
 *   wissen.  Erste WARMUP_MS davon lassen wir das at-rest-Flag aus.
 */
#define MADGWICK_BETA          0.08f
#define BIAS_EMA_ALPHA         0.001f
#define STATS_EMA_ALPHA        1.0e-4f
#define REST_GYRO_STDDEV_DPS   3.0f
#define REST_ACCEL_STDDEV_G    0.05f
#define WARMUP_MS              2000u

#define DEG_TO_RAD 0.01745329251f

static volatile float s_qw = 1.0f, s_qx = 0.0f, s_qy = 0.0f, s_qz = 0.0f;
static volatile float s_bias_x = 0.0f, s_bias_y = 0.0f, s_bias_z = 0.0f;
static volatile float s_corr_gx = 0.0f, s_corr_gy = 0.0f, s_corr_gz = 0.0f;
static volatile float s_bias_temp_c = 0.0f;
static volatile bool  s_at_rest = false;

/* Laufende Mean/Var über ~10 s je Achse — EMA-Form.
 * Var initialisiert auf etwas Hohes, damit at-rest erst nach Warmup
 * triggern kann (eigentlich nochmal explizit von s_warmup_ms gegated). */
static float s_mean_gx = 0, s_mean_gy = 0, s_mean_gz = 0;
static float s_var_gx  = 0, s_var_gy  = 0, s_var_gz  = 0;
static float s_mean_ax = 0, s_mean_ay = 0, s_mean_az = 0;
static float s_var_ax  = 0, s_var_ay  = 0, s_var_az  = 0;

static uint32_t s_warmup_ms = 0;

extern uint32_t HAL_GetTick(void);

/* Diagnose */
volatile uint32_t g_imu_fusion_updates = 0;
volatile uint32_t g_imu_at_rest_seconds = 0;  /* Sekunden im at-rest seit Boot */
volatile float    g_imu_stddev_gx_dps = 0;
volatile float    g_imu_stddev_gy_dps = 0;
volatile float    g_imu_stddev_gz_dps = 0;

void imu_fusion_init(float bx, float by, float bz)
{
    s_qw = 1.0f; s_qx = 0.0f; s_qy = 0.0f; s_qz = 0.0f;
    s_bias_x = bx; s_bias_y = by; s_bias_z = bz;
    s_corr_gx = s_corr_gy = s_corr_gz = 0.0f;
    s_at_rest = false;
    s_warmup_ms = 0;
    s_mean_gx = s_mean_gy = s_mean_gz = 0;
    s_var_gx  = s_var_gy  = s_var_gz  = 100.0f;   /* hoch → kein at-rest */
    s_mean_ax = s_mean_ay = s_mean_az = 0;
    s_var_ax  = s_var_ay  = s_var_az  = 1.0f;
}

static inline float fast_invsqrt(float x)
{
    /* M7 hat VSQRT — 1.0f / sqrtf ist hier präzise und schnell genug. */
    return 1.0f / sqrtf(x);
}

void imu_fusion_update(float gx_raw, float gy_raw, float gz_raw,
                       float ax, float ay, float az,
                       float dt_s, float temp_c)
{
    g_imu_fusion_updates++;

    /* 1) Bias subtrahieren */
    float gx = gx_raw - s_bias_x;
    float gy = gy_raw - s_bias_y;
    float gz = gz_raw - s_bias_z;
    s_corr_gx = gx;
    s_corr_gy = gy;
    s_corr_gz = gz;

    /* 2) At-rest Detection — EMA-Stddev über ~10 s relativ zum
     *    laufenden Mittelwert je Achse.  Wenn die Streuung auf
     *    allen 3 Gyro- und 3 Accel-Achsen unter den Schwellen liegt
     *    → still.  Bias ist hier komplett irrelevant: ein konstanter
     *    Offset im Gyro-Signal macht den Mittelwert größer, aber die
     *    Varianz um diesen Mittelwert bleibt klein.
     *
     *    EMA-Form für Mean+Var:
     *      mean += α·(x − mean)
     *      var  = (1 − α)·(var + α·(x − mean)²)
     *    Wobei (x − mean) NACH dem Mean-Update kleiner ist als vorher;
     *    die Konvention "vor dem Update" liefert die stabilere Schätzung.
     */
    float dgx = gx_raw - s_mean_gx;  s_mean_gx += STATS_EMA_ALPHA * dgx;
    float dgy = gy_raw - s_mean_gy;  s_mean_gy += STATS_EMA_ALPHA * dgy;
    float dgz = gz_raw - s_mean_gz;  s_mean_gz += STATS_EMA_ALPHA * dgz;
    s_var_gx = (1.0f - STATS_EMA_ALPHA) * (s_var_gx + STATS_EMA_ALPHA * dgx * dgx);
    s_var_gy = (1.0f - STATS_EMA_ALPHA) * (s_var_gy + STATS_EMA_ALPHA * dgy * dgy);
    s_var_gz = (1.0f - STATS_EMA_ALPHA) * (s_var_gz + STATS_EMA_ALPHA * dgz * dgz);

    float dax = ax - s_mean_ax;  s_mean_ax += STATS_EMA_ALPHA * dax;
    float day = ay - s_mean_ay;  s_mean_ay += STATS_EMA_ALPHA * day;
    float daz = az - s_mean_az;  s_mean_az += STATS_EMA_ALPHA * daz;
    s_var_ax = (1.0f - STATS_EMA_ALPHA) * (s_var_ax + STATS_EMA_ALPHA * dax * dax);
    s_var_ay = (1.0f - STATS_EMA_ALPHA) * (s_var_ay + STATS_EMA_ALPHA * day * day);
    s_var_az = (1.0f - STATS_EMA_ALPHA) * (s_var_az + STATS_EMA_ALPHA * daz * daz);

    float sgx = sqrtf(s_var_gx);
    float sgy = sqrtf(s_var_gy);
    float sgz = sqrtf(s_var_gz);
    float sax = sqrtf(s_var_ax);
    float say = sqrtf(s_var_ay);
    float saz = sqrtf(s_var_az);
    g_imu_stddev_gx_dps = sgx;
    g_imu_stddev_gy_dps = sgy;
    g_imu_stddev_gz_dps = sgz;

    /* Warmup-Zähler — erst nach WARMUP_MS reagieren wir auf at-rest. */
    if (s_warmup_ms < WARMUP_MS) {
        s_warmup_ms += (uint32_t)(dt_s * 1000.0f);
    }

    bool still = (sgx < REST_GYRO_STDDEV_DPS) &&
                 (sgy < REST_GYRO_STDDEV_DPS) &&
                 (sgz < REST_GYRO_STDDEV_DPS) &&
                 (sax < REST_ACCEL_STDDEV_G)  &&
                 (say < REST_ACCEL_STDDEV_G)  &&
                 (saz < REST_ACCEL_STDDEV_G)  &&
                 (s_warmup_ms >= WARMUP_MS);
    s_at_rest = still;

    /* 3) Bias-EMA nur wenn at-rest.  Wir lernen den Bias gegen die
     *    Raw-Gyro-Werte — bei at-rest sollte das der wahre Bias sein. */
    if (still) {
        s_bias_x += BIAS_EMA_ALPHA * (gx_raw - s_bias_x);
        s_bias_y += BIAS_EMA_ALPHA * (gy_raw - s_bias_y);
        s_bias_z += BIAS_EMA_ALPHA * (gz_raw - s_bias_z);
        s_bias_temp_c = temp_c;
    }

    /* 4) Madgwick — Standard IMU (kein Mag).
     *    Gyro in rad/s. */
    float wx = gx * DEG_TO_RAD;
    float wy = gy * DEG_TO_RAD;
    float wz = gz * DEG_TO_RAD;

    float qw = s_qw, qx = s_qx, qy = s_qy, qz = s_qz;

    /* qDot = 0.5 * q ⊗ (0, w) */
    float qDw = 0.5f * (-qx*wx - qy*wy - qz*wz);
    float qDx = 0.5f * ( qw*wx + qy*wz - qz*wy);
    float qDy = 0.5f * ( qw*wy - qx*wz + qz*wx);
    float qDz = 0.5f * ( qw*wz + qx*wy - qy*wx);

    /* Gravity-Korrekturschritt nur wenn Accel sinnvoll ist (zwischen
     * 0.5 g und 1.5 g — sonst freier Fall / massive Beschleunigung). */
    float anorm = sqrtf(ax*ax + ay*ay + az*az);
    if (anorm > 0.5f && anorm < 1.5f) {
        float inv = fast_invsqrt(ax*ax + ay*ay + az*az);
        ax *= inv; ay *= inv; az *= inv;

        /* Gradient (Original Madgwick IMU 2010, vereinfacht) */
        float _2qw = 2.0f*qw, _2qx = 2.0f*qx, _2qy = 2.0f*qy, _2qz = 2.0f*qz;
        float _4qw = 4.0f*qw, _4qx = 4.0f*qx, _4qy = 4.0f*qy;
        float _8qx = 8.0f*qx, _8qy = 8.0f*qy;
        float qwqw = qw*qw, qxqx = qx*qx, qyqy = qy*qy, qzqz = qz*qz;

        float s0 = _4qw*qyqy + _2qy*ax + _4qw*qxqx - _2qx*ay;
        float s1 = _4qx*qzqz - _2qz*ax + 4.0f*qwqw*qx - _2qw*ay
                 - _4qx + _8qx*qxqx + _8qx*qyqy + _4qx*az;
        float s2 = 4.0f*qwqw*qy + _2qw*ax + _4qy*qzqz - _2qz*ay
                 - _4qy + _8qy*qxqx + _8qy*qyqy + _4qy*az;
        float s3 = 4.0f*qxqx*qz - _2qx*ax + 4.0f*qyqy*qz - _2qy*ay;

        float sNorm = fast_invsqrt(s0*s0 + s1*s1 + s2*s2 + s3*s3);
        s0 *= sNorm; s1 *= sNorm; s2 *= sNorm; s3 *= sNorm;

        qDw -= MADGWICK_BETA * s0;
        qDx -= MADGWICK_BETA * s1;
        qDy -= MADGWICK_BETA * s2;
        qDz -= MADGWICK_BETA * s3;
    }

    /* Integrate */
    qw += qDw * dt_s;
    qx += qDx * dt_s;
    qy += qDy * dt_s;
    qz += qDz * dt_s;

    /* Normalize */
    float qNorm = fast_invsqrt(qw*qw + qx*qx + qy*qy + qz*qz);
    s_qw = qw * qNorm;
    s_qx = qx * qNorm;
    s_qy = qy * qNorm;
    s_qz = qz * qNorm;
}

void imu_fusion_get_corrected_gyro(float *gx, float *gy, float *gz)
{
    *gx = s_corr_gx; *gy = s_corr_gy; *gz = s_corr_gz;
}

void imu_fusion_get_quaternion(float *qw, float *qx, float *qy, float *qz)
{
    *qw = s_qw; *qx = s_qx; *qy = s_qy; *qz = s_qz;
}

void imu_fusion_get_bias(float *bx, float *by, float *bz)
{
    *bx = s_bias_x; *by = s_bias_y; *bz = s_bias_z;
}

float imu_fusion_get_bias_temp(void) { return s_bias_temp_c; }

bool imu_fusion_is_at_rest(void) { return s_at_rest; }

void imu_fusion_reset_bias(void)
{
    s_bias_x = s_bias_y = s_bias_z = 0.0f;
}

void imu_fusion_snapshot_bias_temp(void)
{
    /* Wird vom Save-Befehl gerufen — nur als Marker. */
}
