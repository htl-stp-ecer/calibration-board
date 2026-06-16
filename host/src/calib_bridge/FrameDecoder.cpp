#include "calib_bridge/FrameDecoder.h"

#include <cstring>

namespace calib_bridge
{
    using framing::SYNC;
    using framing::FrameType;
    using framing::HDR_BYTES;
    using framing::OVERHEAD;
    using framing::crc8_smbus;

    namespace
    {
        // Little-endian Pull-Helpers — alle Felder im Protokoll sind LE.
        uint16_t rd_u16(const uint8_t* p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
        int16_t  rd_i16(const uint8_t* p) { return static_cast<int16_t>(rd_u16(p)); }
        uint32_t rd_u32(const uint8_t* p)
        {
            return uint32_t(p[0])
                 | (uint32_t(p[1]) <<  8)
                 | (uint32_t(p[2]) << 16)
                 | (uint32_t(p[3]) << 24);
        }
        int32_t  rd_i32(const uint8_t* p) { return static_cast<int32_t>(rd_u32(p)); }
    }

    void FrameDecoder::reset()
    {
        buf_.clear();
    }

    void FrameDecoder::feed(std::span<const uint8_t> bytes)
    {
        if (bytes.empty()) return;
        stats_.bytes_in += bytes.size();
        buf_.insert(buf_.end(), bytes.begin(), bytes.end());

        std::size_t off = 0;
        while (off < buf_.size())
        {
            // shift den ungelesenen Rest nach vorne, damit try_parse_one
            // immer ab Index 0 arbeitet.  Spart eine "start offset"-API.
            if (off > 0)
            {
                buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(off));
                off = 0;
            }
            const std::size_t consumed = try_parse_one();
            if (consumed == 0) break;  // brauche mehr Bytes
            off += consumed;
        }
        if (off > 0)
        {
            buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(off));
        }
    }

    std::size_t FrameDecoder::try_parse_one()
    {
        // 1) auf SYNC syncen
        std::size_t sync_idx = 0;
        while (sync_idx < buf_.size() && buf_[sync_idx] != SYNC)
        {
            ++sync_idx;
            ++stats_.resyncs;
        }
        if (sync_idx > 0)
        {
            return sync_idx;  // schmeiß die nicht-SYNC-Bytes weg
        }
        if (buf_.size() < HDR_BYTES) return 0;

        const uint8_t type = buf_[1];
        const uint8_t len  = buf_[2];

        // Erwartete Payload-Länge je Typ — unbekannte Typen oder falsche
        // Länge → 1-Byte resync.
        std::size_t expected = 0;
        switch (static_cast<FrameType>(type))
        {
            case FrameType::ICM:         expected = framing::PAYLOAD_LEN_ICM;         break;
            case FrameType::PAA:         expected = framing::PAYLOAD_LEN_PAA;         break;
            case FrameType::PaaAcc:      expected = framing::PAYLOAD_LEN_PAA_ACC;     break;
            case FrameType::Status:      expected = framing::PAYLOAD_LEN_STATUS;      break;
            case FrameType::PaaCal:      expected = framing::PAYLOAD_LEN_PAA_CAL;     break;
            case FrameType::Orientation: expected = framing::PAYLOAD_LEN_ORIENTATION; break;
            default:
                ++stats_.unknown_types;
                ++stats_.resyncs;
                return 1;
        }
        if (len != expected)
        {
            ++stats_.resyncs;
            return 1;
        }

        const std::size_t total = OVERHEAD + len;
        if (buf_.size() < total) return 0;

        const uint8_t crc_have = buf_[HDR_BYTES + len];
        const uint8_t crc_want = crc8_smbus(buf_.data() + 1, /* type..payload */
                                            2 + 4 + len);
        if (crc_have != crc_want)
        {
            ++stats_.crc_errors;
            ++stats_.resyncs;
            return 1;
        }

        const uint32_t t_ms = rd_u32(buf_.data() + 3);
        const uint8_t* p    = buf_.data() + HDR_BYTES;

        switch (static_cast<FrameType>(type))
        {
            case FrameType::ICM:
            {
                framing::IcmSample s{};
                s.t_ms = t_ms;
                s.ax   = rd_i16(p +  0);
                s.ay   = rd_i16(p +  2);
                s.az   = rd_i16(p +  4);
                s.gx   = rd_i16(p +  6);
                s.gy   = rd_i16(p +  8);
                s.gz   = rd_i16(p + 10);
                s.temp = rd_i16(p + 12);
                if (icm_cb_) icm_cb_(s);
                break;
            }
            case FrameType::PAA:
            {
                framing::PaaSample s{};
                s.t_ms    = t_ms;
                s.dx      = rd_i16(p + 0);
                s.dy      = rd_i16(p + 2);
                s.squal   = p[4];
                s.shutter = static_cast<uint16_t>((uint16_t(p[5]) << 8) | uint16_t(p[6]));
                s.motion  = p[7];
                if (paa_cb_) paa_cb_(s);
                break;
            }
            case FrameType::PaaAcc:
            {
                framing::PaaAccFrame s{};
                s.t_ms  = t_ms;
                s.acc_x = rd_i32(p + 0);
                s.acc_y = rd_i32(p + 4);
                if (paa_acc_cb_) paa_acc_cb_(s);
                break;
            }
            case FrameType::Orientation:
            {
                framing::OrientationFrame s{};
                s.t_ms = t_ms;
                std::memcpy(&s.qw,         p + 0,  4);
                std::memcpy(&s.qx,         p + 4,  4);
                std::memcpy(&s.qy,         p + 8,  4);
                std::memcpy(&s.qz,         p + 12, 4);
                std::memcpy(&s.gx_dps,     p + 16, 4);
                std::memcpy(&s.gy_dps,     p + 20, 4);
                std::memcpy(&s.gz_dps,     p + 24, 4);
                std::memcpy(&s.bias_x_dps, p + 28, 4);
                std::memcpy(&s.bias_y_dps, p + 32, 4);
                std::memcpy(&s.bias_z_dps, p + 36, 4);
                uint8_t flags = p[40];
                s.at_rest         = (flags & 0x01) != 0;
                s.bias_persisted  = (flags & 0x02) != 0;
                if (orient_cb_) orient_cb_(s);
                break;
            }
            case FrameType::PaaCal:
            {
                framing::PaaCalFrame s{};
                s.t_ms = t_ms;
                // Floats werden Wort-für-Wort übertragen — wir können
                // sie nicht direkt aus uint32_t rd_u32 lesen ohne Float-
                // Aliasing, also memcpy.
                std::memcpy(&s.cx_per_cm, p +  0, 4);
                std::memcpy(&s.cy_per_cm, p +  4, 4);
                std::memcpy(&s.height_mm, p +  8, 4);
                std::memcpy(&s.off_x_mm,  p + 12, 4);
                std::memcpy(&s.off_y_mm,  p + 16, 4);
                s.valid = p[20] != 0;
                if (paa_cal_cb_) paa_cal_cb_(s);
                break;
            }
            case FrameType::Status:
            {
                framing::StatusFrame s{};
                s.t_ms              = t_ms;
                s.icm_init_status   = static_cast<int8_t>(p[0]);
                s.paa_init_status   = static_cast<int8_t>(p[1]);
                s.paa_seen          = p[2];
                s.icm_sample_count  = rd_u32(p + 4);
                s.icm_dropped       = rd_u32(p + 8);
                s.paa_init_attempts = rd_u32(p + 12);
                s.paa_reconnects    = rd_u16(p + 16);
                s.paa_disconnects   = rd_u16(p + 18);
                if (status_cb_) status_cb_(s);
                break;
            }
        }
        ++stats_.frames_ok;
        return total;
    }

    std::size_t FrameDecoder::encode_set_paa_cal(uint8_t* out, std::size_t out_cap,
                                                 float cx, float cy, float h)
    {
        const std::size_t total = framing::OVERHEAD + framing::PAYLOAD_LEN_CMD_SET_PAA_CAL;
        if (out_cap < total) return 0;

        out[0] = SYNC;
        out[1] = static_cast<uint8_t>(FrameType::CmdSetPaaCal);
        out[2] = framing::PAYLOAD_LEN_CMD_SET_PAA_CAL;
        // T_MS: 0 — FW ignoriert es bei Commands.
        out[3] = out[4] = out[5] = out[6] = 0;
        std::memcpy(out + 7,  &cx, 4);
        std::memcpy(out + 11, &cy, 4);
        std::memcpy(out + 15, &h,  4);
        out[7 + framing::PAYLOAD_LEN_CMD_SET_PAA_CAL] =
            crc8_smbus(out + 1, 2 + 4 + framing::PAYLOAD_LEN_CMD_SET_PAA_CAL);
        return total;
    }

    std::size_t FrameDecoder::encode_set_paa_offset(uint8_t* out, std::size_t out_cap,
                                                    float off_x_mm, float off_y_mm)
    {
        const std::size_t total = framing::OVERHEAD + framing::PAYLOAD_LEN_CMD_SET_PAA_OFFSET;
        if (out_cap < total) return 0;

        out[0] = SYNC;
        out[1] = static_cast<uint8_t>(FrameType::CmdSetPaaOffset);
        out[2] = framing::PAYLOAD_LEN_CMD_SET_PAA_OFFSET;
        // T_MS: 0 — FW ignoriert es bei Commands.
        out[3] = out[4] = out[5] = out[6] = 0;
        std::memcpy(out + 7,  &off_x_mm, 4);
        std::memcpy(out + 11, &off_y_mm, 4);
        out[7 + framing::PAYLOAD_LEN_CMD_SET_PAA_OFFSET] =
            crc8_smbus(out + 1, 2 + 4 + framing::PAYLOAD_LEN_CMD_SET_PAA_OFFSET);
        return total;
    }

    static std::size_t encode_trigger(uint8_t* out, std::size_t out_cap, FrameType type)
    {
        const std::size_t total = framing::OVERHEAD + 0;
        if (out_cap < total) return 0;
        out[0] = SYNC;
        out[1] = static_cast<uint8_t>(type);
        out[2] = 0;
        out[3] = out[4] = out[5] = out[6] = 0;
        out[7] = crc8_smbus(out + 1, 2 + 4);
        return total;
    }

    std::size_t FrameDecoder::encode_save_gyro_bias(uint8_t* out, std::size_t out_cap)
    {
        return encode_trigger(out, out_cap, FrameType::CmdSaveGyroBias);
    }
    std::size_t FrameDecoder::encode_reset_gyro_bias(uint8_t* out, std::size_t out_cap)
    {
        return encode_trigger(out, out_cap, FrameType::CmdResetGyroBias);
    }
}
