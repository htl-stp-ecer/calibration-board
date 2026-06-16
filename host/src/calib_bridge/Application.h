#pragma once

#include "calib_bridge/Config.h"
#include "calib_bridge/FrameDecoder.h"
#include "calib_bridge/Publisher.h"
#include "calib_bridge/SerialPort.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace calib_bridge
{
    /** Hauptloop:
     *   - hält den Serial-Port (auto-detect + auto-reconnect)
     *   - füttert eingehende Bytes in FrameDecoder
     *   - publisht typisierte Samples auf raccoon-transport
     *   - emittiert in regelmäßigen Abständen Status (Board / ICM / PAA /
     *     Stats-JSON) damit Subscriber die Lage kennen ohne raten zu müssen
     *
     *  Komplett single-threaded — alles im selben Polling-Loop. */
    class Application
    {
    public:
        explicit Application(Config cfg);

        /** Blockierende Hauptschleife.  Return wenn request_stop() oder ein
         *  unrecoverable Fehler.  Exit-Code 0 für sauberes Stoppen. */
        int run();

        void request_stop() noexcept { stop_.store(true, std::memory_order_relaxed); }

    private:
        using Clock = std::chrono::steady_clock;

        void tick();
        void on_icm(const framing::IcmSample& s);
        void on_paa(const framing::PaaSample& s);
        void on_paa_acc(const framing::PaaAccFrame& s);
        void on_status(const framing::StatusFrame& s);
        void on_paa_cal(const framing::PaaCalFrame& s);
        void on_orientation(const framing::OrientationFrame& s);
        void try_open();
        void emit_status_if_due();
        void wire_command_subscribers();
        void send_set_paa_cal(float cx, float cy, float h);
        void send_set_paa_offset(float off_x_mm, float off_y_mm);
        void send_save_gyro_bias();
        void send_reset_gyro_bias();

        // Hilfsstrings
        std::string board_state_string() const;
        std::string icm_state_string() const;
        std::string paa_state_string() const;
        std::string stats_json() const;

        Config       cfg_;
        SerialPort   port_;
        FrameDecoder decoder_;
        Publisher    publisher_;
        std::atomic<bool> stop_{false};

        // — Zustand, den wir veröffentlichen —
        // Board: open() erfolgreich + irgendwann Bytes empfangen
        Clock::time_point  last_byte_at_{};
        Clock::time_point  last_icm_at_{};
        Clock::time_point  last_paa_at_{};
        Clock::time_point  last_status_frame_at_{};
        Clock::time_point  last_status_publish_at_{};
        Clock::time_point  last_open_attempt_{};

        // ICM-Status: bekommen wir Frames?  STATUS-Frame hat ggf. detail.
        std::optional<int> icm_init_status_;
        // PAA-Status: STATUS-Frame ist Quelle der Wahrheit; falls Firmware
        // noch keine sendet, fallback auf "Frame in den letzten 3 s?".
        std::optional<int> paa_init_status_;
        bool               paa_seen_in_status_ = false;

        uint64_t icm_frames_total_ = 0;
        uint64_t paa_frames_total_ = 0;
        uint64_t status_frames_total_ = 0;

        // Aktuelle PAA-Kalibrierung — vom FW via PAA_CAL frame gepusht.
        // Default-Werte, bis das erste Frame kommt.
        float    paa_cx_per_cm_ = 11.9f;
        float    paa_cy_per_cm_ = 11.9f;
        float    paa_height_mm_ = 19.0f;
        bool     paa_cal_valid_ = false;

        // PAA-Montageoffset vom Drehzentrum in cm (Body-Frame).  Vom FW via
        // PAA_CAL frame gepusht (dort in mm).  Wird in on_paa() benutzt um
        // den Rotations-Scheinfluss (ω×r) aus der Odometrie zu rechnen.
        float    paa_off_x_cm_ = 0.0f;
        float    paa_off_y_cm_ = 0.0f;

        // Integrierte Position in cm (kann vom Host via CMD_PAA_RESET_POS
        // genullt werden).
        float    paa_pos_x_cm_ = 0.0f;
        float    paa_pos_y_cm_ = 0.0f;

        // Aktuelle Quaternion (body→world, Hamilton).  Wird vom
        // ORIENTATION frame upgedated; Odometrie nutzt sie um PAA
        // body-frame Verschiebungen in den World-Frame zu rotieren.
        float    qw_ = 1.0f, qx_ = 0.0f, qy_ = 0.0f, qz_ = 0.0f;

        // Heading (yaw, rad) beim vorigen PAA-Sample — für die ω×r-
        // Korrektur brauchen wir die Heading-Änderung Δθ zwischen zwei
        // PAA-Samples.  have_prev_yaw_ verhindert einen Δθ-Sprung beim
        // allerersten Sample (und nach Odom-Reset).
        float    prev_yaw_rad_  = 0.0f;
        bool     have_prev_yaw_ = false;

        // Odometrie-Pose (World-Frame, cm).
        float    odom_pos_x_cm_ = 0.0f;
        float    odom_pos_y_cm_ = 0.0f;

        // Pending-Queue für Commands die ankamen während der Port noch
        // zu war (raccoon-transport retained-delivery kommt typischer-
        // weise ms vor dem ersten erfolgreichen Port-Open).  Wir
        // serialisieren die fertigen Frames hier und flushen sie sobald
        // der Port da ist.
        std::vector<std::vector<uint8_t>> pending_tx_;
    };
}
