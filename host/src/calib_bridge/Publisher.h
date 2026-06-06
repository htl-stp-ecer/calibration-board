#pragma once

#include "calib_bridge/Framing.h"

#include <cstdint>
#include <memory>
#include <string>

namespace raccoon { class Transport; }

namespace calib_bridge
{
    /** Dünner Wrapper um raccoon::Transport — mappt jeden Frame-Typ auf
     *  die richtigen Channels und Message-Typen.  Timestamps werden in
     *  Mikrosekunden seit UNIX-Epoche gestempelt (raccoon-Konvention).
     */
    class Publisher
    {
    public:
        explicit Publisher(const std::string& provider);
        ~Publisher();

        Publisher(const Publisher&) = delete;
        Publisher& operator=(const Publisher&) = delete;

        void publish_icm(const framing::IcmSample& s);
        void publish_paa(const framing::PaaSample& s);

        // PAA Kalibrierung (aus FW PAA_CAL frame).
        void publish_paa_cal(const framing::PaaCalFrame& s);

        // Orientation (aus FW ORIENTATION frame) — Quaternion + Euler +
        // bias-korrigierter Gyro + at-rest + Bias.
        void publish_orientation(const framing::OrientationFrame& s,
                                  float roll_deg, float pitch_deg, float yaw_deg);

        // Skalierte PAA-Werte — Bridge berechnet das aus dem Raw-Sample
        // und der aktuellen Kalibrierung.
        void publish_paa_cm(float dx_cm, float dy_cm, float pos_x_cm, float pos_y_cm);

        // Odometrie-Pose (vom Bridge fusioniert).
        void publish_odom(float pos_x_cm, float pos_y_cm, float heading_deg);

        // Eine optionale Raw-Schnittstelle für den Transport, damit die
        // Application Command-Subscriber registrieren kann ohne ein
        // Transport-Member zu duplizieren.
        raccoon::Transport& transport();

        // Status-Strings auf die jeweiligen Status-Channels.
        void publish_board_status(const std::string& state, const std::string& port);
        void publish_icm_status(const std::string& state);
        void publish_paa_status(const std::string& state);

        // Maschinell auswertbare Stats — eine Zeile JSON.
        void publish_stats(const std::string& json);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
