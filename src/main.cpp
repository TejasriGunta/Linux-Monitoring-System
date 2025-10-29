#include "../include/monitor.h"
#include <iostream>
#include <getopt.h>

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [OPTIONS]\n"
              << "Terminal-based activity monitor for Linux.\n\n"
              << "Options:\n"
              << "  -r, --refresh-rate=MS    Set refresh rate in milliseconds (default: 1000)\n"
              << "  -t, --threshold=PERCENT  Set CPU threshold for alerts (default: 80.0)\n"
              << "  -a, --no-alert           Disable CPU threshold alerts\n"
              << "  -n, --no-notify          Disable system desktop notifications\n"
              << "  -d, --debug              Enable debug output\n"
              << "  -o, --debug-only         Run in debug-only mode (no UI)\n"
              << "  -h, --help               Display help and exit\n"
              << std::endl;
}

int main(int argc, char* argv[]) {
    MonitorConfig config;

    static struct option long_options[] = {
        {"refresh-rate", required_argument, 0, 'r'},
        {"threshold",    required_argument, 0, 't'},
        {"no-alert",     no_argument,       0, 'a'},
        {"no-notify",    no_argument,       0, 'n'},
        {"debug",        no_argument,       0, 'd'},
        {"debug-only",   no_argument,       0, 'o'},
        {"help",         no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;
    while ((opt = getopt_long(argc, argv, "r:t:andoh", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'r': config.refresh_rate_ms = std::stoi(optarg); break;
            case 't': config.cpu_threshold = std::stof(optarg); break;
            case 'a': config.show_alert = false; break;
            case 'n': config.system_notifications = false; break;
            case 'd': config.debug_mode = true; break;
            case 'o': config.debug_mode = true; config.debug_only_mode = true; break;
            case 'h': printUsage(argv[0]); return 0;
            default: printUsage(argv[0]); return 1;
        }
    }

    try {
        ActivityMonitor monitor;
        monitor.setConfig(config);

        if (config.debug_only_mode) {
            monitor.runDebugMode();
        } else {
            monitor.run();
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
