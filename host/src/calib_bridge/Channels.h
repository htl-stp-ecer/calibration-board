#pragma once

namespace calib_bridge::Channels
{
    // ── Sensor-Streams ────────────────────────────────────────────────────
    constexpr auto ICM_ACCEL = "raccoon/calib_board/icm/accel";        // vector3f_t [g]
    constexpr auto ICM_GYRO  = "raccoon/calib_board/icm/gyro";         // vector3f_t [dps]
    constexpr auto ICM_TEMP  = "raccoon/calib_board/icm/temperature";  // scalar_f_t [°C]

    constexpr auto PAA_DX       = "raccoon/calib_board/paa/delta_x";   // scalar_i32_t (raw counts)
    constexpr auto PAA_DY       = "raccoon/calib_board/paa/delta_y";   // scalar_i32_t (raw counts)
    constexpr auto PAA_SQUAL    = "raccoon/calib_board/paa/squal";     // scalar_i32_t (0..169)
    constexpr auto PAA_SHUTTER  = "raccoon/calib_board/paa/shutter";   // scalar_i32_t (raw)
    constexpr auto PAA_MOTION   = "raccoon/calib_board/paa/motion";    // scalar_i32_t (bitfield)

    // Board-seitig integrierter signed Counts-Akkumulator (frei laufend).
    // Der Host akkumuliert NICHT — der Kalibrier-Wizard nimmt Differenzen.
    constexpr auto PAA_ACC_X    = "raccoon/calib_board/paa/acc_x";     // scalar_i32_t (counts)
    constexpr auto PAA_ACC_Y    = "raccoon/calib_board/paa/acc_y";     // scalar_i32_t (counts)

    // PAA Kalibrierung (vom FW-Flash kommend, vom Host als Status republished)
    constexpr auto PAA_CAL_CX        = "raccoon/calib_board/paa/cal/cx_per_cm"; // scalar_f_t
    constexpr auto PAA_CAL_CY        = "raccoon/calib_board/paa/cal/cy_per_cm"; // scalar_f_t
    constexpr auto PAA_CAL_HEIGHT    = "raccoon/calib_board/paa/cal/height_mm"; // scalar_f_t
    constexpr auto PAA_CAL_VALID     = "raccoon/calib_board/paa/cal/valid";     // scalar_i32_t (0/1)
    // PAA-Montageoffset vom Drehzentrum (mm, Body-Frame) — vom FW-Flash.
    constexpr auto PAA_CAL_OFF_X     = "raccoon/calib_board/paa/cal/off_x_mm";  // scalar_f_t
    constexpr auto PAA_CAL_OFF_Y     = "raccoon/calib_board/paa/cal/off_y_mm";  // scalar_f_t

    // Skalierte PAA-Werte (counts → cm) — Bridge wendet Kalibrierung an.
    constexpr auto PAA_CM_X      = "raccoon/calib_board/paa/cm/dx";   // scalar_f_t [cm/sample]
    constexpr auto PAA_CM_Y      = "raccoon/calib_board/paa/cm/dy";   // scalar_f_t [cm/sample]
    constexpr auto PAA_CM_POS_X  = "raccoon/calib_board/paa/cm/pos_x"; // scalar_f_t [cm, integriert]
    constexpr auto PAA_CM_POS_Y  = "raccoon/calib_board/paa/cm/pos_y"; // scalar_f_t [cm, integriert]

    // Command-Channels (Host → Bridge → FW)
    //   cmd/paa/set_calibration:  string_t mit JSON
    //     {"cx_per_cm": 12.3, "cy_per_cm": 12.1, "height_mm": 19.0}
    //   cmd/paa/reset_position:   scalar_i32_t (value ignored, kept als Trigger)
    constexpr auto CMD_PAA_SET_CAL     = "raccoon/calib_board/cmd/paa/set_calibration";
    constexpr auto CMD_PAA_RESET_POS   = "raccoon/calib_board/cmd/paa/reset_position";
    //   cmd/paa/set_offset: string_t mit JSON {"off_x_mm": 12.3, "off_y_mm": -4.5}
    constexpr auto CMD_PAA_SET_OFFSET  = "raccoon/calib_board/cmd/paa/set_offset";
    //   cmd/icm/save_gyro_bias:   trigger — schreibt den aktuellen at-rest gemittelten Bias ins Flash
    //   cmd/icm/reset_gyro_bias:  trigger — setzt Bias auf 0 (nicht persistent)
    constexpr auto CMD_ICM_SAVE_GYRO_BIAS  = "raccoon/calib_board/cmd/icm/save_gyro_bias";
    constexpr auto CMD_ICM_RESET_GYRO_BIAS = "raccoon/calib_board/cmd/icm/reset_gyro_bias";

    // ── ICM Fusion / Orientation ─────────────────────────────────────────
    // 100 Hz Quaternion + bias-korrigierter Gyro + at-rest Flag.
    constexpr auto ICM_QUAT_W   = "raccoon/calib_board/icm/quat/w";   // scalar_f_t
    constexpr auto ICM_QUAT_X   = "raccoon/calib_board/icm/quat/x";
    constexpr auto ICM_QUAT_Y   = "raccoon/calib_board/icm/quat/y";
    constexpr auto ICM_QUAT_Z   = "raccoon/calib_board/icm/quat/z";
    constexpr auto ICM_EULER_ROLL  = "raccoon/calib_board/icm/euler/roll";   // scalar_f_t deg
    constexpr auto ICM_EULER_PITCH = "raccoon/calib_board/icm/euler/pitch";
    constexpr auto ICM_EULER_YAW   = "raccoon/calib_board/icm/euler/yaw";
    constexpr auto ICM_GYRO_CORR   = "raccoon/calib_board/icm/gyro_corrected"; // vector3f_t dps
    constexpr auto ICM_GYRO_BIAS   = "raccoon/calib_board/icm/gyro_bias";      // vector3f_t dps
    constexpr auto ICM_AT_REST     = "raccoon/calib_board/icm/at_rest";        // scalar_i32_t 0/1
    constexpr auto ICM_BIAS_VALID  = "raccoon/calib_board/icm/bias_persisted"; // scalar_i32_t 0/1

    // ── Odometrie (Bridge fusioniert PAA + Quaternion) ──────────────────
    // Body-Frame dx/dy aus PAA werden über die Quaternion in den World-
    // Frame rotiert und integriert.
    constexpr auto ODOM_POS_X    = "raccoon/calib_board/odom/pos_x";   // scalar_f_t cm
    constexpr auto ODOM_POS_Y    = "raccoon/calib_board/odom/pos_y";   // scalar_f_t cm
    constexpr auto ODOM_HEADING  = "raccoon/calib_board/odom/heading"; // scalar_f_t deg (yaw)
    constexpr auto CMD_ODOM_RESET = "raccoon/calib_board/cmd/odom/reset";

    // ── Status ────────────────────────────────────────────────────────────
    // Erkenntnisse der Bridge: ist das Board am USB angesteckt?  Antworten
    // ICM / PAA?  string_t mit menschenlesbarer Kurzform; Maschinen lesen
    // die Counter-Channels darunter.
    constexpr auto STATUS_BOARD   = "raccoon/calib_board/status/board";    // "connected" / "disconnected"
    constexpr auto STATUS_ICM     = "raccoon/calib_board/status/icm";      // "ok" / "init_failed:<code>"
    constexpr auto STATUS_PAA     = "raccoon/calib_board/status/paa";      // "connected" / "absent" / "init_failed:<code>"
    constexpr auto STATUS_PORT    = "raccoon/calib_board/status/port";     // "/dev/ttyACMx" oder "(none)"

    // Maschinell auswertbare Counter — string_t mit JSON-Inhalt, damit kein
    // Custom-Messagetype gebraucht wird.  Format siehe README.
    constexpr auto STATUS_STATS   = "raccoon/calib_board/status/stats";    // JSON
}
