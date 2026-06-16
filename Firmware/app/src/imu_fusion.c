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
 * At-Rest-Detection (REST_WIN / REST_DECIM / REST_DWELL_MS):
 *   Kurzes gleitendes Fenster (~0.25 s) Max-Min-Spread auf den drei
 *   Gyro-Achsen plus dem Accel-BETRAG.  Spread ist by-design bias- und
 *   DC-unabhängig (misst nur die Variation, nicht den Offset), und der
 *   Accel-Betrag liegt bei Ruhe immer bei 1 g — egal in welcher Lage.
 *   "Still" wird erst nach REST_DWELL_MS ununterbrochener Ruhe gesetzt
 *   (Zeit-basiert via HAL_GetTick, also rate-unabhängig); die erste
 *   laute Probe wirft sofort zurück auf "moving" (Hysterese per Dwell).
 *
 *   Bewusst KEIN 10s-Buffer mehr: der war ausreißer-empfindlich (ein
 *   Glitch-Sample vergiftete das ganze Fenster für 10 s), brauchte 10 s
 *   Warmlauf bevor "rest" überhaupt möglich war, und hing an einer
 *   exakten 1 kHz-Loop.  Jetzt: ~0.5 s bis "AT REST", sofort raus bei
 *   Bewegung, ~0.5 KB statt 24 KB RAM.
 */
#define MADGWICK_BETA          0.08f
#define BIAS_EMA_ALPHA         0.001f

#define REST_WIN               32u    /* Fenstergröße (Samples nach Decim) */
#define REST_DECIM             8u     /* nur jedes 8. Sample ins Fenster */
#define REST_GYRO_SPREAD_DPS   2.5f   /* Max-Min je Gyro-Achse über Fenster */
#define REST_ACCEL_SPREAD_G    0.05f  /* Max-Min des Accel-Betrags */
#define REST_DWELL_MS          400u   /* so lange still bis "AT REST" */

#define DEG_TO_RAD 0.01745329251f

static volatile float s_qw = 1.0f, s_qx = 0.0f, s_qy = 0.0f, s_qz = 0.0f;
static volatile float s_bias_x = 0.0f, s_bias_y = 0.0f, s_bias_z = 0.0f;
static volatile float s_corr_gx = 0.0f, s_corr_gy = 0.0f, s_corr_gz = 0.0f;
static volatile float s_bias_temp_c = 0.0f;
static volatile bool  s_at_rest = false;

/* Kurzes gleitendes Fenster für at-rest Detection (Gyro-Achsen + Accel-
 * Betrag).  Decimiert, damit das Fenster ~0.25 s abdeckt ohne riesig zu
 * werden. */
static float s_w_gx[REST_WIN];
static float s_w_gy[REST_WIN];
static float s_w_gz[REST_WIN];
static float s_w_an[REST_WIN];      /* Accel-Betrag ‖a‖ [g] */
static uint32_t s_w_head = 0;       /* nächster Schreibindex */
static uint32_t s_w_count = 0;      /* gefüllte Slots (clamped) */
static uint32_t s_w_decim = 0;      /* Decimation-Zähler */
static uint32_t s_quiet_since_ms = 0; /* HAL-Tick als Ruhe begann, 0 = laut */

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
    s_w_head = 0;
    s_w_count = 0;
    s_w_decim = 0;
    s_quiet_since_ms = 0;
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

    /* 2) At-rest Detection: kurzes gleitendes Fenster (Max-Min-Spread)
     *    + Zeit-Dwell.  Spread ist bias- und DC-unabhängig (misst nur die
     *    Variation, nicht den Offset), der Accel-BETRAG ‖a‖ liegt bei Ruhe
     *    immer bei 1 g.  Wir nehmen nur jedes REST_DECIM-te Sample, damit
     *    REST_WIN Slots ~0.25 s abdecken.  "Still" gilt erst nach
     *    REST_DWELL_MS ununterbrochener Ruhe (Zeit-basiert → rate-unab-
     *    hängig); jede laute Probe wirft sofort zurück. */
    s_w_decim++;
    if (s_w_decim >= REST_DECIM) {
        s_w_decim = 0;
        const float an = sqrtf(ax*ax + ay*ay + az*az);   /* noch un-normiert */
        s_w_gx[s_w_head] = gx_raw;
        s_w_gy[s_w_head] = gy_raw;
        s_w_gz[s_w_head] = gz_raw;
        s_w_an[s_w_head] = an;
        s_w_head = (s_w_head + 1u) % REST_WIN;
        if (s_w_count < REST_WIN) {
            s_w_count++;
            g_imu_rest_buf_count = s_w_count;
        }

        if (s_w_count >= REST_WIN) {
            float gx_lo = s_w_gx[0], gx_hi = gx_lo;
            float gy_lo = s_w_gy[0], gy_hi = gy_lo;
            float gz_lo = s_w_gz[0], gz_hi = gz_lo;
            float an_lo = s_w_an[0], an_hi = an_lo;
            for (uint32_t i = 1; i < REST_WIN; i++) {
                float v;
                v = s_w_gx[i]; if (v < gx_lo) gx_lo = v; if (v > gx_hi) gx_hi = v;
                v = s_w_gy[i]; if (v < gy_lo) gy_lo = v; if (v > gy_hi) gy_hi = v;
                v = s_w_gz[i]; if (v < gz_lo) gz_lo = v; if (v > gz_hi) gz_hi = v;
                v = s_w_an[i]; if (v < an_lo) an_lo = v; if (v > an_hi) an_hi = v;
            }
            const float sg_x = gx_hi - gx_lo;
            const float sg_y = gy_hi - gy_lo;
            const float sg_z = gz_hi - gz_lo;
            const float sa   = an_hi - an_lo;
            g_imu_spread_gx_dps = sg_x;
            g_imu_spread_gy_dps = sg_y;
            g_imu_spread_gz_dps = sg_z;
            g_imu_spread_ax_g   = sa;   /* repurposed: Spread des Accel-Betrags */

            const bool quiet = (sg_x < REST_GYRO_SPREAD_DPS) &&
                               (sg_y < REST_GYRO_SPREAD_DPS) &&
                               (sg_z < REST_GYRO_SPREAD_DPS) &&
                               (sa   < REST_ACCEL_SPREAD_G);
            const uint32_t now = HAL_GetTick();
            if (quiet) {
                if (s_quiet_since_ms == 0u) s_quiet_since_ms = (now == 0u) ? 1u : now;
                s_at_rest = (now - s_quiet_since_ms) >= REST_DWELL_MS;
            } else {
                s_quiet_since_ms = 0u;
                s_at_rest = false;
            }
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
