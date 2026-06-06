#include "calib_bridge/Application.h"
#include "calib_bridge/Config.h"

#include <spdlog/spdlog.h>

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
    calib_bridge::Application* g_app = nullptr;

    void on_signal(int)
    {
        if (g_app) g_app->request_stop();
    }

    void set_log_level(const std::string& level)
    {
        if      (level == "trace") spdlog::set_level(spdlog::level::trace);
        else if (level == "debug") spdlog::set_level(spdlog::level::debug);
        else if (level == "info")  spdlog::set_level(spdlog::level::info);
        else if (level == "warn" ||
                 level == "warning") spdlog::set_level(spdlog::level::warn);
        else if (level == "error" ||
                 level == "err")   spdlog::set_level(spdlog::level::err);
    }

    void print_usage()
    {
        std::cout
            << "raccoon-calib-bridge — USB-CDC → raccoon-transport publisher\n"
               "\n"
               "Usage: raccoon-calib-bridge [--port PATH] [--log LEVEL]\n"
               "                            [--provider URL] [--version]\n"
               "\n"
               "ENV überschreibt CLI:\n"
               "  CALIB_BRIDGE_PORT          /dev/ttyACM0 (default: auto-detect via VID:PID)\n"
               "  CALIB_BRIDGE_LOG           trace|debug|info|warn|error\n"
               "  CALIB_BRIDGE_RECONNECT_MS  ms between port-open retries\n"
               "  CALIB_BRIDGE_STATUS_MS     ms between status publishes\n"
               "  CALIB_BRIDGE_SILENCE_MS    silence → force reopen\n"
               "  CALIB_BRIDGE_TRANSPORT     raccoon transport provider URL\n";
    }
}

int main(int argc, char** argv)
{
    calib_bridge::Config cfg = calib_bridge::Config::from_env();

    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        const auto next = [&](const char* what) -> std::string {
            if (i + 1 >= argc)
            {
                std::cerr << "missing value for " << what << "\n";
                std::exit(EXIT_FAILURE);
            }
            return argv[++i];
        };

        if      (a == "--port"     || a == "-p") cfg.serial_port = next("--port");
        else if (a == "--log"      || a == "-l") cfg.log_level = next("--log");
        else if (a == "--provider")              cfg.transport_provider = next("--provider");
        else if (a == "--version")               { std::cout << "raccoon-calib-bridge 0.1.0\n"; return 0; }
        else if (a == "--help"     || a == "-h") { print_usage(); return 0; }
        else
        {
            std::cerr << "unknown argument: " << a << "\n";
            print_usage();
            return EXIT_FAILURE;
        }
    }

    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    set_log_level(cfg.log_level);

    std::signal(SIGINT,  on_signal);
    std::signal(SIGTERM, on_signal);

    try
    {
        calib_bridge::Application app{std::move(cfg)};
        g_app = &app;
        const int rc = app.run();
        g_app = nullptr;
        return rc;
    }
    catch (const std::exception& e)
    {
        spdlog::error("fatal: {}", e.what());
        return EXIT_FAILURE;
    }
}
