#include "calib_bridge/SerialPort.h"

#include <cerrno>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <regex>
#include <termios.h>
#include <unistd.h>

namespace calib_bridge
{
    namespace fs = std::filesystem;

    SerialPort::~SerialPort()
    {
        close();
    }

    std::optional<std::string> SerialPort::autodetect()
    {
        // by-id is the stable identifier — survives ttyACM number shuffling
        // when multiple CDC devices are plugged in.
        const fs::path by_id_dir{"/dev/serial/by-id"};
        if (fs::exists(by_id_dir) && fs::is_directory(by_id_dir))
        {
            std::error_code ec;
            for (const auto& entry : fs::directory_iterator(by_id_dir, ec))
            {
                if (ec) break;
                const auto name = entry.path().filename().string();
                // STMicro CDC enumerates as "...STMicroelectronics_STM32_Virtual_ComPort..."
                // (case may vary across udev versions).
                std::string lower = name;
                for (auto& c : lower) c = static_cast<char>(std::tolower(c));
                if (lower.find("stmicro") != std::string::npos &&
                    lower.find("virtual_comport") != std::string::npos)
                {
                    return fs::canonical(entry.path(), ec).string();
                }
            }
        }

        // Fallback: erster /dev/ttyACM*
        const std::regex acm{"ttyACM[0-9]+"};
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator("/dev", ec))
        {
            if (ec) break;
            const auto name = entry.path().filename().string();
            if (std::regex_match(name, acm))
            {
                return entry.path().string();
            }
        }
        return std::nullopt;
    }

    bool SerialPort::open(const std::string& path)
    {
        close();

        // O_NOCTTY damit der Port nicht versehentlich Controlling-Terminal wird.
        // O_NONBLOCK damit read() sofort EAGAIN liefert wenn nichts da ist.
        const int fd = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd < 0) return false;

        termios tio{};
        if (::tcgetattr(fd, &tio) != 0)
        {
            ::close(fd);
            return false;
        }

        // Raw mode — kein Echo, kein canonical, kein Signal-Mapping, kein
        // CR/LF-Mangeling.  Wir transportieren binäre Frames.
        cfmakeraw(&tio);
        // VMIN=0 + VTIME=0: non-blocking semantics, read() liefert was da ist.
        tio.c_cc[VMIN]  = 0;
        tio.c_cc[VTIME] = 0;
        // CDC ignoriert die Baud-Settings, aber wir setzen sie für die Form.
        cfsetispeed(&tio, B921600);
        cfsetospeed(&tio, B921600);
        tio.c_cflag |= (CLOCAL | CREAD);
        tio.c_cflag &= ~CRTSCTS;

        if (::tcsetattr(fd, TCSANOW, &tio) != 0)
        {
            ::close(fd);
            return false;
        }

        // Den Rest der Buffer leeren — alte Garbage vom letzten Lauf.
        ::tcflush(fd, TCIOFLUSH);

        fd_   = fd;
        path_ = path;
        return true;
    }

    void SerialPort::close()
    {
        if (fd_ >= 0)
        {
            ::close(fd_);
            fd_ = -1;
        }
        path_.clear();
    }

    int SerialPort::read(std::span<uint8_t> out)
    {
        if (fd_ < 0) return -1;
        const ssize_t n = ::read(fd_, out.data(), out.size());
        if (n > 0) return static_cast<int>(n);
        if (n == 0) return 0;  // technisch EOF, bei CDC praktisch unmöglich
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        return -1;  // ENXIO / EIO → Port ist tot
    }

    int SerialPort::write(std::span<const uint8_t> data)
    {
        if (fd_ < 0) return -1;
        std::size_t off = 0;
        while (off < data.size())
        {
            const ssize_t n = ::write(fd_, data.data() + off, data.size() - off);
            if (n > 0) { off += static_cast<std::size_t>(n); continue; }
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                // Kurz pollen — FS-CDC drained alle 1 ms.
                struct timespec ts{0, 500'000};  // 500 µs
                nanosleep(&ts, nullptr);
                continue;
            }
            return -1;
        }
        return static_cast<int>(off);
    }
}
