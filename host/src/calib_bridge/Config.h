#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace calib_bridge
{
    struct Config
    {
        // Wenn leer: Auto-Detect via VID:PID = 0483:5740 (STMicro CDC).
        std::string serial_port;

        // CDC ignoriert die Baudrate, aber pyserial-Kompat und manche
        // Tools setzen sie trotzdem.
        int baud = 921600;

        // Polling-Pause wenn /dev/ttyACM* fehlt — wir versuchen alle N ms
        // erneut zu öffnen.
        std::chrono::milliseconds reconnect_interval{500};

        // Status-Publish-Rate (auch wenn keine Sensordaten kommen — Watchdog
        // soll wissen dass die Bridge selber lebt).
        std::chrono::milliseconds status_publish_interval{1000};

        // Wenn länger als das kein einziges ICM-Frame mehr ankommt, gilt das
        // Board als "stuck/dead" und wir reopenen den Port.
        std::chrono::milliseconds icm_silence_timeout{2000};

        // Wenn kein STATUS-Frame in dieser Zeit kommt, fällt die PAA-
        // Status-Inferenz auf "absent" zurück (Bridge weiß sonst nichts
        // über den Sensor).  Alte Firmware ohne STATUS-Frame schickt nie
        // einen → reine Heuristik aus Frame-Rate.
        std::chrono::milliseconds status_frame_timeout{3000};

        // Log-Level: "trace" | "debug" | "info" | "warn" | "error"
        std::string log_level = "info";

        // Default-Provider für raccoon::Transport (leer = LCM-Default,
        // typischerweise udpm://239.255.76.67:7667 mit loopback multicast).
        std::string transport_provider;

        static Config from_env();
    };
}
