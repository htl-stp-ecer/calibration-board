// Shared frame protocol — must stay byte-for-byte identical to
// Firmware/app/inc/framing.h.  Single source of truth in spirit, two
// copies in practice because the firmware is C and we don't want to
// pull a C/C++ header into both build systems.  If you change one,
// change the other.

#pragma once

#include <array>
#include <cstdint>

namespace calib_bridge::framing
{
    inline constexpr uint8_t SYNC = 0xA5;

    enum class FrameType : uint8_t
    {
        ICM         = 0x01,
        PAA         = 0x02,
        Status      = 0x03,
        PaaCal      = 0x04,
        Orientation = 0x05,
        PaaAcc      = 0x06,
        // Host → device
        CmdSetPaaCal       = 0x10,
        CmdSaveGyroBias    = 0x11,
        CmdResetGyroBias   = 0x12,
        CmdSetPaaOffset    = 0x13,
    };

    inline constexpr uint8_t PAYLOAD_LEN_ICM             = 14;
    inline constexpr uint8_t PAYLOAD_LEN_PAA             = 10;
    inline constexpr uint8_t PAYLOAD_LEN_PAA_ACC         = 8;
    inline constexpr uint8_t PAYLOAD_LEN_STATUS          = 20;
    inline constexpr uint8_t PAYLOAD_LEN_PAA_CAL         = 21;
    inline constexpr uint8_t PAYLOAD_LEN_ORIENTATION     = 41;
    inline constexpr uint8_t PAYLOAD_LEN_CMD_SET_PAA_CAL = 12;
    inline constexpr uint8_t PAYLOAD_LEN_CMD_SET_PAA_OFFSET = 8;

    inline constexpr uint8_t HDR_BYTES   = 7;   // sync + type + len + t_ms
    inline constexpr uint8_t OVERHEAD    = 8;   // hdr + crc8

    // Scaling — must match icm42688p.c
    inline constexpr float ICM_GYRO_LSB_PER_DPS = 16.384f;
    inline constexpr float ICM_ACCEL_LSB_PER_G  = 8192.0f;
    // Temperature: temp_c = raw / 132.48 + 25.0
    inline constexpr float ICM_TEMP_LSB_PER_C   = 132.48f;
    inline constexpr float ICM_TEMP_OFFSET_C    = 25.0f;

    // CRC-8/SMBUS: poly=0x07, init=0x00, ref-in=ref-out=false, xor-out=0x00.
    // Bitexakt mit framing.c im Firmware.
    inline uint8_t crc8_smbus(const uint8_t* data, std::size_t len) noexcept
    {
        uint8_t c = 0x00;
        for (std::size_t i = 0; i < len; ++i)
        {
            c ^= data[i];
            for (int b = 0; b < 8; ++b)
            {
                c = static_cast<uint8_t>((c & 0x80) ? ((c << 1) ^ 0x07) : (c << 1));
            }
        }
        return c;
    }

    struct IcmSample
    {
        uint32_t t_ms = 0;
        int16_t  ax = 0, ay = 0, az = 0;
        int16_t  gx = 0, gy = 0, gz = 0;
        int16_t  temp = 0;

        [[nodiscard]] float ax_g()  const { return ax / ICM_ACCEL_LSB_PER_G; }
        [[nodiscard]] float ay_g()  const { return ay / ICM_ACCEL_LSB_PER_G; }
        [[nodiscard]] float az_g()  const { return az / ICM_ACCEL_LSB_PER_G; }
        [[nodiscard]] float gx_dps() const { return gx / ICM_GYRO_LSB_PER_DPS; }
        [[nodiscard]] float gy_dps() const { return gy / ICM_GYRO_LSB_PER_DPS; }
        [[nodiscard]] float gz_dps() const { return gz / ICM_GYRO_LSB_PER_DPS; }
        [[nodiscard]] float temp_c() const { return temp / ICM_TEMP_LSB_PER_C + ICM_TEMP_OFFSET_C; }
    };

    struct PaaSample
    {
        uint32_t t_ms = 0;
        int16_t  dx = 0;
        int16_t  dy = 0;
        uint8_t  squal = 0;
        uint16_t shutter = 0;
        uint8_t  motion = 0;
    };

    struct PaaAccFrame
    {
        uint32_t t_ms = 0;
        int32_t  acc_x = 0;   // frei laufender signed Counts-Akkumulator
        int32_t  acc_y = 0;
    };

    struct PaaCalFrame
    {
        uint32_t t_ms = 0;
        float    cx_per_cm = 0;
        float    cy_per_cm = 0;
        float    height_mm = 0;
        float    off_x_mm  = 0;   // PAA-Montageoffset vom Drehzentrum (mm)
        float    off_y_mm  = 0;
        bool     valid     = false;
    };

    struct OrientationFrame
    {
        uint32_t t_ms = 0;
        float    qw = 1, qx = 0, qy = 0, qz = 0;
        float    gx_dps = 0, gy_dps = 0, gz_dps = 0;
        float    bias_x_dps = 0, bias_y_dps = 0, bias_z_dps = 0;
        bool     at_rest = false;
        bool     bias_persisted = false;
    };

    struct StatusFrame
    {
        uint32_t t_ms = 0;
        int8_t   icm_init_status   = 0;
        int8_t   paa_init_status   = 0;
        uint8_t  paa_seen          = 0;
        uint32_t icm_sample_count  = 0;
        uint32_t icm_dropped       = 0;
        uint32_t paa_init_attempts = 0;
        uint16_t paa_reconnects    = 0;
        uint16_t paa_disconnects   = 0;
    };
}
