#pragma once

#include "calib_bridge/Framing.h"

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace calib_bridge
{
    /** Stream-Parser für das Binär-Frame-Protokoll.  feed() füttert Rohbytes,
     *  intern wird auf SYNC synchronisiert, Länge geprüft, CRC verifiziert
     *  und die typisierten Callbacks gefeuert.  Resilient gegen mid-stream
     *  Verbindungsabbrüche — kaputter Frame → resync. */
    class FrameDecoder
    {
    public:
        struct Stats
        {
            uint64_t bytes_in   = 0;
            uint64_t frames_ok  = 0;
            uint64_t crc_errors = 0;
            uint64_t resyncs    = 0;   // Anzahl Byte-für-Byte Resync-Vorgänge
            uint64_t unknown_types = 0;
        };

        using IcmCallback         = std::function<void(const framing::IcmSample&)>;
        using PaaCallback         = std::function<void(const framing::PaaSample&)>;
        using PaaAccCallback      = std::function<void(const framing::PaaAccFrame&)>;
        using StatusCallback      = std::function<void(const framing::StatusFrame&)>;
        using PaaCalCallback      = std::function<void(const framing::PaaCalFrame&)>;
        using OrientationCallback = std::function<void(const framing::OrientationFrame&)>;

        void on_icm(IcmCallback cb)               { icm_cb_ = std::move(cb); }
        void on_paa(PaaCallback cb)               { paa_cb_ = std::move(cb); }
        void on_paa_acc(PaaAccCallback cb)        { paa_acc_cb_ = std::move(cb); }
        void on_status(StatusCallback cb)         { status_cb_ = std::move(cb); }
        void on_paa_cal(PaaCalCallback cb)        { paa_cal_cb_ = std::move(cb); }
        void on_orientation(OrientationCallback cb) { orient_cb_ = std::move(cb); }

        // Erzeugt ein CMD_SET_PAA_CAL Frame in den Output-Buffer (mit
        // SYNC/header/CRC) — Caller schreibt das in den SerialPort.
        // Return: tatsächliche Frame-Größe in `out`.
        static std::size_t encode_set_paa_cal(uint8_t* out, std::size_t out_cap,
                                              float cx_per_cm, float cy_per_cm,
                                              float height_mm);

        // CMD_SET_PAA_OFFSET — Montageoffset (mm) vom Drehzentrum.
        static std::size_t encode_set_paa_offset(uint8_t* out, std::size_t out_cap,
                                                 float off_x_mm, float off_y_mm);

        // Trigger-Frames ohne Payload.
        static std::size_t encode_save_gyro_bias(uint8_t* out, std::size_t out_cap);
        static std::size_t encode_reset_gyro_bias(uint8_t* out, std::size_t out_cap);

        void feed(std::span<const uint8_t> bytes);
        void reset();  // beim Reconnect — Buffer leeren

        [[nodiscard]] const Stats& stats() const noexcept { return stats_; }

    private:
        // Versucht ab buf_[0] ein Frame zu parsen.  Return:
        //  >0 = Bytes konsumiert (Frame ok ODER 1-Byte Resync)
        //   0 = brauche mehr Bytes
        std::size_t try_parse_one();

        std::vector<uint8_t> buf_;
        Stats                stats_{};
        IcmCallback          icm_cb_;
        PaaCallback          paa_cb_;
        PaaAccCallback       paa_acc_cb_;
        StatusCallback       status_cb_;
        PaaCalCallback       paa_cal_cb_;
        OrientationCallback  orient_cb_;
    };
}
