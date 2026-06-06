#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace calib_bridge
{
    /** Non-blocking USB-CDC serial port.  Wraps an fd opened with
     *  O_NONBLOCK so a missing/dead device doesn't hang the main loop. */
    class SerialPort
    {
    public:
        SerialPort() = default;
        ~SerialPort();

        SerialPort(const SerialPort&) = delete;
        SerialPort& operator=(const SerialPort&) = delete;

        /** Sucht in /dev/serial/by-id/ nach einem STMicro Virtual-COM-Port
         *  oder fällt auf den ersten /dev/ttyACM* zurück.  std::nullopt wenn
         *  nichts gefunden. */
        static std::optional<std::string> autodetect();

        /** Öffnet den angegebenen Pfad.  Setzt raw mode, non-blocking.  Bei
         *  Erfolg true; bei Fehler false und errno bleibt für den Caller. */
        bool open(const std::string& path);

        /** Schließt den fd (idempotent). */
        void close();

        [[nodiscard]] bool is_open() const noexcept { return fd_ >= 0; }
        [[nodiscard]] const std::string& path() const noexcept { return path_; }

        /** Liest bis zu out.size() Bytes.  Return:
         *    >0 = Anzahl gelesener Bytes
         *     0 = nichts da (EAGAIN) — kein Fehler
         *    -1 = Fehler (Port ist tot → caller sollte close+reopen). */
        int read(std::span<uint8_t> out);

        /** Blocking-ish write — bei voller Output-Queue blockt's bis das
         *  Kernel-Buffer aufnimmt (in der Praxis < 1 ms bei FS-CDC).
         *  Return: tatsächlich geschriebene Bytes; -1 bei Fehler. */
        int write(std::span<const uint8_t> data);

    private:
        int         fd_ = -1;
        std::string path_;
    };
}
