#include "imu_fusion.h"

#include <math.h>
#include <string.h>

/* ── Tuning ─────────────────────────────────────────────────────────
 * MADGWICK_BETA:
 *   Filter-Gain.  Höher = Accel-Anteil stärker (schnellere Korrektur
 *   von Roll/Pitch, aber empfindlicher gegen Bewegungs-Beschleunigung).
 *   0.05–0.1 typisch für 6-DOF ohne Mag.
 *
 * BIAS_EMA_ALPHA:
 *   Lernrate für den Bias.  α = 0.001 bei 1 kHz → effektive Zeit-
 *   konstante ~1 s.  Während at-rest 5 s = 5000 Updates konvergiert das
 *   sauber.
 *
 * REST_BUF_SIZE / REST_SUBSAMPLE_DECIM:
 *   Wir behalten einen 10s-Ring-Buffer der Roh-Samples (downgesampled
 *   auf 100 Hz, also 1000 Slots à 4 Byte × 6 Achsen = 24 KB).  Die
 *   at-rest Detection schaut auf den **Max-Min-Spread** in diesem
 *   Buffer — das ist by-design bias-unabhängig, weil Spread nur misst
 *   wie WEIT die Werte um ihren eigenen Mittelpunkt variieren, nicht
 *   wo dieser Mittelpunkt liegt.
 *
 * REST_GYRO_SPREAD_DPS / REST_ACCEL_SPREAD_G:
 *   "Still" gilt wenn der Max-Min-Spread über die letzten 10 s auf
 *   ALLEN sechs Achsen unter dem Schwellwert liegt.  Großzügig gewählt
 *   um Lüfter-Vibration / leichtes Tisch-Wackeln zu tolerieren.
 */
#define MADGWICK_BETA          0.08f
#define BIAS_EMA_ALPHA         0.001f

#define REST_BUF_SIZE          1000u  /* 10 s @ 100 Hz */
#define REST_SUBSAMPLE_DECIM   10u    /* 1 kHz / 10  = 100 Hz */
#define REST_GYRO_SPREAD_DPS   15.0f
#define REST_ACCEL_SPREAD_G    0.25f

#define DEG_TO_RAD 0.01745329251f

static volatile float s_qw = 1.0f, s_qx = 0.0f, s_qy = 0.0f, s_qz = 0.0f;
static volatile float s_bias_x = 0.0f, s_bias_y = 0.0f, s_bias_z = 0.0f;
static volatile float s_corr_gx = 0.0f, s_corr_gy = 0.0f, s_corr_gz = 0.0f;
static volatile float s_bias_temp_c = 0.0f;
static volatile bool  s_at_rest = false;

/* 10s Ring-Buffer für at-rest Detection — je Achse 1000 floats. */
static float s_buf_gx[REST_BUF_SIZE];
static float s_buf_gy[REST_BUF_SIZE];
static float s_buf_gz[REST_BUF_SIZE];
static float s_buf_ax[REST_BUF_SIZE];
static float s_buf_ay[REST_BUF_SIZE];
static float s_buf_az[REST_BUF_SIZE];
static uint32_t s_buf_head = 0;     /* nächster Schreibindex */
static uint32_t s_buf_count = 0;    /* Anzahl gültiger Samples (clamped) */
static uint32_t s_subsample_ctr = 0;

extern uint32_t HAL_GetTick(void);

/* Diagnose */
volatile uint32_t g_imu_fusion_updates = 0;
volatile uint32_t g_imu_at_rest_seconds = 0;
volatile float    g_imu_spread_gx_dps = 0;
volatile float    g_imu_spread_gy_dps = 0;
volatile float    g_imu_spread_gz_dps = 0;
volatile float    g_imu_spread_ax_g = 0;
volatile float    g_imu_spread_ay_g = 0;
volatile float    g_imu_spread_az_g = 0;
volatile uint32_t g_imu_rest_buf_count = 0;

void imu_fusion_init(float bx, float by, float bz)
{
    s_qw = 1.0f; s_qx = 0.0f; s_qy = 0.0f; s_qz = 0.0f;
    s_bias_x = bx; s_bias_y = by; s_bias_z = bz;
    s_corr_gx = s_corr_gy = s_corr_gz = 0.0f;
    s_at_rest = false;
    s_buf_head = 0;
    s_buf_count = 0;
    s_subsample_ctr = 0;
    g_imu_rest_buf_count = 0;
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

    /* 2) At-rest Detection per 10 s Ring-Buffer + Max-Min-Spread.
     *
     *    Wir samplen mit 1 kHz, behalten aber nur jedes 10. Sample im
     *    Buffer (= 100 Hz Decimation, 1000 Slots = 10 s).  Wenn der
     *    Buffer voll ist, scannen wir alle 6 Achsen einmal kurz durch
     *    und ermitteln Max − Min.  Spread auf ALLEN 6 Achsen unter dem
     *    Schwellwert → still.  Bias ist by-design irrelevant: Spread
     *    misst nur, wie WEIT die Werte sich vom eigenen Median wegbewegt
     *    haben, nicht WO dieser Median liegt.
     *
     *    Buffer-Refresh kostet 6×1000 = 6 k Vergleiche × 100 Hz = 600 k/s
     *    auf M7@216 MHz mit FPU → unter 1 % CPU. */
    s_subsample_ctr++;
    if (s_subsample_ctr >= REST_SUBSAMPLE_DECIM) {
        s_subsample_ctr = 0;
        s_buf_gx[s_buf_head] = gx_raw;
        s_buf_gy[s_buf_head] = gy_raw;
        s_buf_gz[s_buf_head] = gz_raw;
        s_buf_ax[s_buf_head] = ax;
        s_buf_ay[s_buf_head] = ay;
        s_buf_az[s_buf_head] = az;
        s_buf_head = (s_buf_head + 1u) % REST_BUF_SIZE;
        if (s_buf_count < REST_BUF_SIZE) {
            s_buf_count++;
            g_imu_rest_buf_count = s_buf_count;
        }

        if (s_buf_count >= REST_BUF_SIZE) {
            float gx_min = s_buf_gx[0], gx_max = gx_min;
            float gy_min = s_buf_gy[0], gy_max = gy_min;
            float gz_min = s_buf_gz[0], gz_max = gz_min;
            float ax_min = s_buf_ax[0], ax_max = ax_min;
            float ay_min = s_buf_ay[0], ay_max = ay_min;
            float az_min = s_buf_az[0], az_max = az_min;
            for (uint32_t i = 1; i < REST_BUF_SIZE; i++) {
                float v;
                v = s_buf_gx[i]; if (v < gx_min) gx_min = v; if (v > gx_max) gx_max = v;
                v = s_buf_gy[i]; if (v < gy_min) gy_min = v; if (v > gy_max) gy_max = v;
                v = s_buf_gz[i]; if (v < gz_min) gz_min = v; if (v > gz_max) gz_max = v;
                v = s_buf_ax[i]; if (v < ax_min) ax_min = v; if (v > ax_max) ax_max = v;
                v = s_buf_ay[i]; if (v < ay_min) ay_min = v; if (v > ay_max) ay_max = v;
                v = s_buf_az[i]; if (v < az_min) az_min = v; if (v > az_max) az_max = v;
            }
            float sg_x = gx_max - gx_min;
            float sg_y = gy_max - gy_min;
            float sg_z = gz_max - gz_min;
            float sa_x = ax_max - ax_min;
            float sa_y = ay_max - ay_min;
            float sa_z = az_max - az_min;
            g_imu_spread_gx_dps = sg_x;
            g_imu_spread_gy_dps = sg_y;
            g_imu_spread_gz_dps = sg_z;
            g_imu_spread_ax_g   = sa_x;
            g_imu_spread_ay_g   = sa_y;
            g_imu_spread_az_g   = sa_z;
            s_at_rest = (sg_x < REST_GYRO_SPREAD_DPS) &&
                         (sg_y < REST_GYRO_SPREAD_DPS) &&
                         (sg_z < REST_GYRO_SPREAD_DPS) &&
                         (sa_x < REST_ACCEL_SPREAD_G) &&
                         (sa_y < REST_ACCEL_SPREAD_G) &&
                         (sa_z < REST_ACCEL_SPREAD_G);
        }
    }

    /* 3) Bias-EMA nur wenn at-rest.  Wir lernen den Bias gegen die
     *    Raw-Gyro-Werte — bei at-rest sollte das der wahre Bias sein. */
    if (s_at_rest) {
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
