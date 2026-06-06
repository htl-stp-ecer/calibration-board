#include "calib_bridge/Config.h"

#include <cstdlib>
#include <string>

namespace calib_bridge
{
    namespace
    {
        std::optional<std::string> env(const char* name)
        {
            const char* v = std::getenv(name);
            if (!v || *v == '\0') return std::nullopt;
            return std::string(v);
        }

        std::optional<int> env_int(const char* name)
        {
            auto s = env(name);
            if (!s) return std::nullopt;
            try { return std::stoi(*s); } catch (...) { return std::nullopt; }
        }
    }

    Config Config::from_env()
    {
        Config c{};
        if (auto v = env("CALIB_BRIDGE_PORT"))               c.serial_port = *v;
        if (auto v = env_int("CALIB_BRIDGE_BAUD"))           c.baud = *v;
        if (auto v = env_int("CALIB_BRIDGE_RECONNECT_MS"))   c.reconnect_interval = std::chrono::milliseconds(*v);
        if (auto v = env_int("CALIB_BRIDGE_STATUS_MS"))      c.status_publish_interval = std::chrono::milliseconds(*v);
        if (auto v = env_int("CALIB_BRIDGE_SILENCE_MS"))     c.icm_silence_timeout = std::chrono::milliseconds(*v);
        if (auto v = env("CALIB_BRIDGE_LOG"))                c.log_level = *v;
        if (auto v = env("CALIB_BRIDGE_TRANSPORT"))          c.transport_provider = *v;
        return c;
    }
}
