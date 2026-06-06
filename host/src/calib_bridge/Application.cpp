#include "calib_bridge/Application.h"
#include "calib_bridge/Channels.h"

#include <raccoon/Transport.h>
#include <raccoon/scalar_f_t.hpp>
#include <raccoon/scalar_i32_t.hpp>
#include <raccoon/string_t.hpp>

#include <spdlog/spdlog.h>

#include <array>
#include <cmath>
#include <cstring>
#include <sstream>
#include <thread>

namespace calib_bridge
{
    namespace
    {
        std::string init_status_string(int code)
        {
            switch (code)
            {
                case  0: return "ok";
                case -1: return "io_error";
                case -2: return "id_or_who_am_i_mismatch";
                case -3: return "spi_config_error";
                default: return "unknown:" + std::to_string(code);
            }
        }
    }

    Application::Application(Config cfg)
        : cfg_(std::move(cfg))
        , publisher_(cfg_.transport_provider)
    {
        decoder_.on_icm([this](const framing::IcmSample& s)    { on_icm(s); });
        decoder_.on_paa([this](const framing::PaaSample& s)    { on_paa(s); });
        decoder_.on_status([this](const framing::StatusFrame& s){ on_status(s); });
        decoder_.on_paa_cal([this](const framing::PaaCalFrame& s){ on_paa_cal(s); });
        decoder_.on_orientation([this](const framing::OrientationFrame& s){ on_orientation(s); });
        wire_command_subscribers();
    }

    int Application::run()
    {
        spdlog::info("calib-bridge starting");
        spdlog::info("  port              = {}", cfg_.serial_port.empty() ? "(auto-detect)" : cfg_.serial_port);
        spdlog::info("  reconnect_ms      = {}", cfg_.reconnect_interval.count());
        spdlog::info("  status_publish_ms = {}", cfg_.status_publish_interval.count());
        spdlog::info("  transport         = {}", cfg_.transport_provider.empty() ? "(default)" : cfg_.transport_provider);

        // Initial-Status raus damit Subscriber das Board als "noch nicht
        // verbunden" sehen ehe der erste Port-Open-Versuch durchgeht.
        publisher_.publish_board_status("disconnected", "(none)");
        publisher_.publish_icm_status("unknown");
        publisher_.publish_paa_status("unknown");

        last_open_attempt_      = Clock::now() - cfg_.reconnect_interval;  // sofortiger Versuch
        last_status_publish_at_ = Clock::now();

        while (!stop_.load(std::memory_order_relaxed))
        {
            tick();
            // Kein Sleep wenn Daten fließen — wir sind eh non-blocking
            // und wollen die Latenz niedrig halten.  Sleep nur wenn der
            // Port zu ist (Strom sparen, CPU nicht hochjazzen).
            if (!port_.is_open())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            else
            {
                // 1 ms Sleep — bei FS-CDC kommen Bursts alle ms, kürzer
                // bringt nichts und kostet CPU.
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        spdlog::info("calib-bridge stopping");
        publisher_.publish_board_status("disconnected", "(stopped)");
        return 0;
    }

    void Application::tick()
    {
        // raccoon::Transport sammelt zwar ankommende Messages im Hinter-
        // grund, dispatched die User-Callbacks aber NUR während spin()
        // oder spinOnce().  Ohne diesen Call kommen die Command-Subscriber
        // (PAA-Cal-Save, ICM-Bias-Save, Odom-Reset) nie zur Ausführung —
        // die UI publisht ins Leere.  0 ms timeout = non-blocking pump.
        publisher_.transport().spinOnce(0);

        if (!port_.is_open())
        {
            const auto now = Clock::now();
            if (now - last_open_attempt_ >= cfg_.reconnect_interval)
            {
                last_open_attempt_ = now;
                try_open();
            }
            emit_status_if_due();
            return;
        }

        std::array<uint8_t, 4096> chunk{};
        const int n = port_.read(chunk);
        if (n > 0)
        {
            last_byte_at_ = Clock::now();
            decoder_.feed(std::span<const uint8_t>(chunk.data(), static_cast<std::size_t>(n)));
        }
        else if (n < 0)
        {
            spdlog::warn("read error on {} — closing", port_.path());
            port_.close();
            decoder_.reset();
            publisher_.publish_board_status("disconnected", "(none)");
        }

        // Watchdog: wenn länger nichts kommt, ist das Board hängen
        // geblieben → close + reopen.  CDC stürzt manchmal still ab.
        if (port_.is_open() && last_byte_at_ != Clock::time_point{} &&
            Clock::now() - last_byte_at_ > cfg_.icm_silence_timeout)
        {
            spdlog::warn("no bytes from {} for {} ms — forcing reopen",
                         port_.path(), cfg_.icm_silence_timeout.count());
            port_.close();
            decoder_.reset();
            publisher_.publish_board_status("disconnected", "(silence_timeout)");
            last_byte_at_ = Clock::time_point{};
        }

        emit_status_if_due();
    }

    void Application::try_open()
    {
        std::string path = cfg_.serial_port;
        if (path.empty())
        {
            auto detected = SerialPort::autodetect();
            if (!detected)
            {
                return;  // still nichts — beim nächsten reconnect-Tick.
            }
            path = *detected;
        }
        if (!port_.open(path))
        {
            spdlog::debug("open({}) failed — will retry", path);
            return;
        }
        spdlog::info("opened {}", path);
        decoder_.reset();
        last_byte_at_ = Clock::now();
        publisher_.publish_board_status("connected", path);

        // Falls Commands während des Disconnect angekommen sind — jetzt
        // rausschicken (in der Reihenfolge ihres Empfangs).
        if (!pending_tx_.empty()) {
            spdlog::info("flushing {} pending command(s) after reconnect",
                         pending_tx_.size());
            for (const auto& f : pending_tx_) {
                port_.write(std::span<const uint8_t>(f.data(), f.size()));
            }
            pending_tx_.clear();
        }
    }

    void Application::on_icm(const framing::IcmSample& s)
    {
        ++icm_frames_total_;
        last_icm_at_ = Clock::now();
        publisher_.publish_icm(s);
        if (!icm_init_status_)
        {
            // Erste ICM-Frames angekommen → ICM ist offensichtlich gesund,
            // auch wenn STATUS-Frame (alte Firmware) nie kommt.
            icm_init_status_ = 0;
        }
    }

    void Application::on_paa(const framing::PaaSample& s)
    {
        ++paa_frames_total_;
        last_paa_at_ = Clock::now();
        publisher_.publish_paa(s);

        // Scaling: counts → cm.  Bei valid=false benutzen wir die
        // Defaults (~11.9 counts/cm @ 19 mm), das stimmt grob für jede
        // unkalibrierte Installation — nach Kalibrierung wird's exakt.
        const float dx_cm = (paa_cx_per_cm_ > 0.0f)
            ? (static_cast<float>(s.dx) / paa_cx_per_cm_) : 0.0f;
        const float dy_cm = (paa_cy_per_cm_ > 0.0f)
            ? (static_cast<float>(s.dy) / paa_cy_per_cm_) : 0.0f;
        paa_pos_x_cm_ += dx_cm;
        paa_pos_y_cm_ += dy_cm;
        publisher_.publish_paa_cm(dx_cm, dy_cm, paa_pos_x_cm_, paa_pos_y_cm_);

        // Odometrie-Fusion: PAA gibt body-frame [dx, dy, 0] (Ground-Plane
        // Bewegung unter dem Sensor).  Wir rotieren mit der aktuellen
        // Quaternion in den World-Frame.  Das nutzt die ersten zwei
        // Zeilen der Rotationsmatrix R(q):
        //   R00 = 1 - 2(qy² + qz²)
        //   R01 = 2(qx*qy - qw*qz)
        //   R10 = 2(qx*qy + qw*qz)
        //   R11 = 1 - 2(qx² + qz²)
        // [vx, vy] = R · [dx_cm, dy_cm, 0]^T projiziert auf XY-Plane.
        const float r00 = 1.0f - 2.0f * (qy_*qy_ + qz_*qz_);
        const float r01 = 2.0f * (qx_*qy_ - qw_*qz_);
        const float r10 = 2.0f * (qx_*qy_ + qw_*qz_);
        const float r11 = 1.0f - 2.0f * (qx_*qx_ + qz_*qz_);
        const float vx = r00 * dx_cm + r01 * dy_cm;
        const float vy = r10 * dx_cm + r11 * dy_cm;
        odom_pos_x_cm_ += vx;
        odom_pos_y_cm_ += vy;

        // Heading = yaw aus dem Quaternion.
        const float yaw_rad = std::atan2(2.0f * (qw_*qz_ + qx_*qy_),
                                          1.0f - 2.0f * (qy_*qy_ + qz_*qz_));
        const float yaw_deg = yaw_rad * 57.29577951f;
        publisher_.publish_odom(odom_pos_x_cm_, odom_pos_y_cm_, yaw_deg);
    }

    void Application::on_orientation(const framing::OrientationFrame& s)
    {
        qw_ = s.qw; qx_ = s.qx; qy_ = s.qy; qz_ = s.qz;

        // Euler aus Quaternion (Tait-Bryan, ZYX-Konvention, world frame).
        const float sinr_cosp = 2.0f * (s.qw*s.qx + s.qy*s.qz);
        const float cosr_cosp = 1.0f - 2.0f * (s.qx*s.qx + s.qy*s.qy);
        const float roll  = std::atan2(sinr_cosp, cosr_cosp);

        const float sinp = 2.0f * (s.qw*s.qy - s.qz*s.qx);
        const float pitch = std::abs(sinp) >= 1.0f
            ? std::copysign(1.5707963f, sinp)   // gimbal lock
            : std::asin(sinp);

        const float siny_cosp = 2.0f * (s.qw*s.qz + s.qx*s.qy);
        const float cosy_cosp = 1.0f - 2.0f * (s.qy*s.qy + s.qz*s.qz);
        const float yaw  = std::atan2(siny_cosp, cosy_cosp);

        constexpr float RAD2DEG = 57.29577951f;
        publisher_.publish_orientation(s, roll * RAD2DEG, pitch * RAD2DEG, yaw * RAD2DEG);
    }

    void Application::on_paa_cal(const framing::PaaCalFrame& s)
    {
        const bool changed =
            s.cx_per_cm != paa_cx_per_cm_ ||
            s.cy_per_cm != paa_cy_per_cm_ ||
            s.height_mm != paa_height_mm_ ||
            s.valid     != paa_cal_valid_;

        paa_cx_per_cm_ = s.cx_per_cm;
        paa_cy_per_cm_ = s.cy_per_cm;
        paa_height_mm_ = s.height_mm;
        paa_cal_valid_ = s.valid;
        publisher_.publish_paa_cal(s);

        if (changed) {
            spdlog::info("PAA cal updated: cx={:.3f} cy={:.3f} h={:.2f} valid={}",
                         s.cx_per_cm, s.cy_per_cm, s.height_mm, s.valid);
        }
    }

    void Application::wire_command_subscribers()
    {
        // SET_PAA_CAL — string_t JSON.  Parser ist Minimal-Hand-Roll
        // damit wir keine JSON-Library reinholen müssen.
        publisher_.transport().subscribe<raccoon::string_t>(
            Channels::CMD_PAA_SET_CAL,
            [this](const raccoon::string_t& m) {
                const std::string& j = m.value;
                // Erwarten "cx_per_cm":X, "cy_per_cm":Y, "height_mm":Z
                auto pick = [&](const char* key, float& dst) -> bool {
                    auto pos = j.find(key);
                    if (pos == std::string::npos) return false;
                    pos = j.find(':', pos);
                    if (pos == std::string::npos) return false;
                    try {
                        dst = std::stof(j.substr(pos + 1));
                        return true;
                    } catch (...) { return false; }
                };
                float cx = paa_cx_per_cm_, cy = paa_cy_per_cm_, h = paa_height_mm_;
                bool ok = pick("cx_per_cm", cx) | pick("cy_per_cm", cy) | pick("height_mm", h);
                if (!ok) {
                    spdlog::warn("CMD_PAA_SET_CAL: kein bekannter Key in '{}'", j);
                    return;
                }
                spdlog::info("CMD_PAA_SET_CAL parsed: cx={:.3f} cy={:.3f} h={:.2f}", cx, cy, h);
                send_set_paa_cal(cx, cy, h);
            });

        // RESET_POSITION — Bridge-interne Integration zurücksetzen.
        publisher_.transport().subscribe<raccoon::scalar_i32_t>(
            Channels::CMD_PAA_RESET_POS,
            [this](const raccoon::scalar_i32_t&) {
                spdlog::info("CMD_PAA_RESET_POS: Position zurückgesetzt");
                paa_pos_x_cm_ = 0.0f;
                paa_pos_y_cm_ = 0.0f;
            });

        publisher_.transport().subscribe<raccoon::scalar_i32_t>(
            Channels::CMD_ICM_SAVE_GYRO_BIAS,
            [this](const raccoon::scalar_i32_t&) {
                spdlog::info("CMD_ICM_SAVE_GYRO_BIAS: Trigger FW");
                send_save_gyro_bias();
            });

        publisher_.transport().subscribe<raccoon::scalar_i32_t>(
            Channels::CMD_ICM_RESET_GYRO_BIAS,
            [this](const raccoon::scalar_i32_t&) {
                spdlog::info("CMD_ICM_RESET_GYRO_BIAS: Trigger FW");
                send_reset_gyro_bias();
            });

        publisher_.transport().subscribe<raccoon::scalar_i32_t>(
            Channels::CMD_ODOM_RESET,
            [this](const raccoon::scalar_i32_t&) {
                spdlog::info("CMD_ODOM_RESET: Odometrie-Pose genullt");
                odom_pos_x_cm_ = 0.0f;
                odom_pos_y_cm_ = 0.0f;
            });
    }

    namespace {
        // Wrappt das Encoding+Senden mit Queue-Fallback: bei offenem
        // Port direkt schreiben, sonst hängen wir die Bytes hinten an die
        // pending_tx_-Queue an damit der nächste port-open sie flusht.
        void queue_or_send(SerialPort& port,
                            std::vector<std::vector<uint8_t>>& pending,
                            const uint8_t* buf, std::size_t n,
                            const char* what)
        {
            if (n == 0) return;
            if (port.is_open()) {
                const int w = port.write(std::span<const uint8_t>(buf, n));
                if (w < 0) {
                    spdlog::warn("{} write failed — queued for next reconnect", what);
                    pending.emplace_back(buf, buf + n);
                } else {
                    spdlog::info("{} sent to FW ({} bytes)", what, w);
                }
            } else {
                spdlog::info("{} queued — port not open yet", what);
                pending.emplace_back(buf, buf + n);
            }
        }
    }

    void Application::send_set_paa_cal(float cx, float cy, float h)
    {
        std::array<uint8_t, 32> buf{};
        const auto n = FrameDecoder::encode_set_paa_cal(buf.data(), buf.size(), cx, cy, h);
        queue_or_send(port_, pending_tx_, buf.data(), n, "CMD_SET_PAA_CAL");
    }

    void Application::send_save_gyro_bias()
    {
        std::array<uint8_t, 16> buf{};
        const auto n = FrameDecoder::encode_save_gyro_bias(buf.data(), buf.size());
        queue_or_send(port_, pending_tx_, buf.data(), n, "CMD_SAVE_GYRO_BIAS");
    }

    void Application::send_reset_gyro_bias()
    {
        std::array<uint8_t, 16> buf{};
        const auto n = FrameDecoder::encode_reset_gyro_bias(buf.data(), buf.size());
        queue_or_send(port_, pending_tx_, buf.data(), n, "CMD_RESET_GYRO_BIAS");
    }

    void Application::on_status(const framing::StatusFrame& s)
    {
        ++status_frames_total_;
        last_status_frame_at_ = Clock::now();

        const bool icm_changed = !icm_init_status_ || *icm_init_status_ != s.icm_init_status;
        const bool paa_changed = !paa_init_status_ || *paa_init_status_ != s.paa_init_status
                              || paa_seen_in_status_ != (s.paa_seen != 0);

        icm_init_status_     = s.icm_init_status;
        paa_init_status_     = s.paa_init_status;
        paa_seen_in_status_  = s.paa_seen != 0;

        if (icm_changed)
        {
            publisher_.publish_icm_status(icm_state_string());
            spdlog::info("ICM status: {}", icm_state_string());
        }
        if (paa_changed)
        {
            publisher_.publish_paa_status(paa_state_string());
            spdlog::info("PAA status: {} (attempts={}, reconnects={}, disconnects={})",
                         paa_state_string(), s.paa_init_attempts,
                         s.paa_reconnects, s.paa_disconnects);
        }
    }

    void Application::emit_status_if_due()
    {
        const auto now = Clock::now();
        if (now - last_status_publish_at_ < cfg_.status_publish_interval) return;
        last_status_publish_at_ = now;

        publisher_.publish_board_status(board_state_string(),
                                        port_.is_open() ? port_.path() : std::string{"(none)"});
        publisher_.publish_icm_status(icm_state_string());
        publisher_.publish_paa_status(paa_state_string());
        publisher_.publish_stats(stats_json());
    }

    std::string Application::board_state_string() const
    {
        return port_.is_open() ? "connected" : "disconnected";
    }

    std::string Application::icm_state_string() const
    {
        if (!port_.is_open()) return "board_disconnected";
        if (!icm_init_status_) return "no_frames_yet";
        if (*icm_init_status_ == 0) return "ok";
        return "init_failed:" + init_status_string(*icm_init_status_);
    }

    std::string Application::paa_state_string() const
    {
        if (!port_.is_open()) return "board_disconnected";

        const auto now = Clock::now();
        const bool have_status_frame =
            last_status_frame_at_ != Clock::time_point{} &&
            (now - last_status_frame_at_) < cfg_.status_frame_timeout;

        if (have_status_frame && paa_init_status_)
        {
            if (*paa_init_status_ == 0) return "connected";
            if (!paa_seen_in_status_)   return "absent";
            return "init_failed:" + init_status_string(*paa_init_status_);
        }

        // Fallback: alte Firmware ohne STATUS-Frame → aus Frame-Rate ableiten.
        if (last_paa_at_ == Clock::time_point{})           return "absent";
        if (now - last_paa_at_ > std::chrono::seconds(2))  return "absent";
        return "connected";
    }

    std::string Application::stats_json() const
    {
        const auto& d = decoder_.stats();
        std::ostringstream os;
        os << "{"
           << "\"bytes_in\":"       << d.bytes_in
           << ",\"frames_ok\":"     << d.frames_ok
           << ",\"crc_errors\":"    << d.crc_errors
           << ",\"resyncs\":"       << d.resyncs
           << ",\"unknown_types\":" << d.unknown_types
           << ",\"icm_frames\":"    << icm_frames_total_
           << ",\"paa_frames\":"    << paa_frames_total_
           << ",\"status_frames\":" << status_frames_total_
           << "}";
        return os.str();
    }
}
