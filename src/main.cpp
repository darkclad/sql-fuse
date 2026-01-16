#include "SQLFuseFS.hpp"
#include "Config.hpp"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <iostream>
#include <csignal>
#include <cstdlib>
#include <vector>

using namespace sqlfuse;

namespace {

volatile std::sig_atomic_t g_signal_received = 0;

void signalHandler(int signal) {
    g_signal_received = signal;
}

void setupSignalHandlers() {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
}

void setupLogging(bool debug, bool foreground, const std::string& logFile = "") {
    try {
        std::vector<spdlog::sink_ptr> sinks;

        if (foreground) {
            // Console logging for foreground mode
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console_sink->set_level(debug ? spdlog::level::debug : spdlog::level::info);
            sinks.push_back(console_sink);
        }

        // File logging (always for daemon, optional for foreground)
        std::string log_path = logFile.empty() ? "/var/log/sql-fuse.log" : logFile;
        try {
            auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_path, false);
            file_sink->set_level(debug ? spdlog::level::debug : spdlog::level::info);
            sinks.push_back(file_sink);
        } catch (const spdlog::spdlog_ex&) {
            // Fall back to user's home directory if /var/log is not writable
            const char* home = std::getenv("HOME");
            if (home) {
                log_path = std::string(home) + "/.sql-fuse.log";
                try {
                    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_path, false);
                    file_sink->set_level(debug ? spdlog::level::debug : spdlog::level::info);
                    sinks.push_back(file_sink);
                } catch (...) {
                    // No file logging available
                }
            }
        }

        if (sinks.empty()) {
            // Fallback to console if no sinks available
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console_sink->set_level(debug ? spdlog::level::debug : spdlog::level::info);
            sinks.push_back(console_sink);
        }

        auto logger = std::make_shared<spdlog::logger>("sql-fuse", sinks.begin(), sinks.end());
        logger->set_level(debug ? spdlog::level::debug : spdlog::level::info);

        spdlog::set_default_logger(logger);
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");

    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Log initialization failed: " << ex.what() << std::endl;
    }
}

void printBanner() {
    std::cout << R"(
  ____   ___  _       _____ _   _ ____  _____
 / ___| / _ \| |     |  ___| | | / ___|| ____|
 \___ \| | | | |     | |_  | | | \___ \|  _|
  ___) | |_| | |___  |  _| | |_| |___) | |___
 |____/ \__\_\_____| |_|    \___/|____/|_____|

 SQL FUSE Filesystem v1.0.0
 Mount SQL databases as a filesystem

)" << std::endl;
}

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " [options] <mountpoint>\n\n";
    std::cout << "Database Type:\n";
    std::cout << "  -t, --type <type>      Database type: mysql, sqlite, postgresql, oracle (default: mysql)\n";
    std::cout << "\nConnection Options:\n";
    std::cout << "  -H, --host <host>      Database server host (default: localhost)\n";
    std::cout << "                         For SQLite: path to database file\n";
    std::cout << "  -P, --port <port>      Database server port (default: 3306, ignored for SQLite)\n";
    std::cout << "  -u, --user <user>      Database username (required for MySQL, ignored for SQLite)\n";
    std::cout << "  -p, --password <pass>  Database password (or use MYSQL_PWD env)\n";
    std::cout << "  -S, --socket <path>    Unix socket path\n";
    std::cout << "  -D, --database <db>    Default database (or SQLite file path)\n";
    std::cout << "\nSSL Options:\n";
    std::cout << "  --ssl                  Enable SSL connection\n";
    std::cout << "  --ssl-ca <file>        SSL CA certificate file\n";
    std::cout << "  --ssl-cert <file>      SSL client certificate file\n";
    std::cout << "  --ssl-key <file>       SSL client key file\n";
    std::cout << "\nCache Options:\n";
    std::cout << "  --cache-size <MB>      Maximum cache size (default: 100)\n";
    std::cout << "  --cache-ttl <seconds>  Default cache TTL (default: 30)\n";
    std::cout << "  --no-cache             Disable caching entirely\n";
    std::cout << "\nData Options:\n";
    std::cout << "  --max-rows <N>         Max rows in table files (default: 10000)\n";
    std::cout << "  --read-only            Mount as read-only\n";
    std::cout << "  --databases <list>     Comma-separated list of databases to expose\n";
    std::cout << "\nFUSE Options:\n";
    std::cout << "  -f, --foreground       Run in foreground\n";
    std::cout << "  -d, --debug            Enable debug output\n";
    std::cout << "  --allow-other          Allow other users to access\n";
    std::cout << "  --allow-root           Allow root to access\n";
    std::cout << "  --max-threads <N>      Maximum FUSE worker threads (default: 10)\n";
    std::cout << "\nConfiguration:\n";
    std::cout << "  -c, --config <file>    Path to configuration file\n";
    std::cout << "\nOther Options:\n";
    std::cout << "  -h, --help             Show this help message\n";
    std::cout << "  -V, --version          Show version information\n";
    std::cout << "\nExamples:\n";
    std::cout << "  " << program << " -t mysql -u root -p password /mnt/sql\n";
    std::cout << "  " << program << " -t sqlite -H /path/to/database.db /mnt/sql\n";
    std::cout << "  " << program << " -u reader --read-only /mnt/sql\n";
    std::cout << "  " << program << " -u user -H remotehost -P 3307 /mnt/sql\n";
    std::cout << "  " << program << " -c /etc/sql-fuse.conf /mnt/sql\n";
    std::cout << std::endl;
}

}  // namespace

int main(int argc, char* argv[]) {
    // Quick help check
    if (argc < 2) {
        printBanner();
        printUsage(argv[0]);
        return 1;
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printBanner();
            printUsage(argv[0]);
            return 0;
        }
        if (arg == "-V" || arg == "--version") {
            std::cout << "sql-fuse version 1.0.0" << std::endl;
            return 0;
        }
    }

    // Parse configuration
    Config config;
    try {
        config = Config::parseArgs(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    // Setup logging
    setupLogging(config.debug, config.foreground);

    // Print banner in foreground mode
    if (config.foreground) {
        printBanner();
    } else {
        std::cout << "sql-fuse: mounting " << config.mountpoint << " (daemon mode)" << std::endl;
        std::cout << "sql-fuse: logs at ~/.sql-fuse.log or /var/log/sql-fuse.log" << std::endl;
    }

    spdlog::info("Starting SQL FUSE filesystem");
    spdlog::info("Connecting to {}:{} as {}", config.connection.host,
                 config.connection.port, config.connection.user);
    spdlog::info("Mountpoint: {}", config.mountpoint);

    // Validate configuration
    if (!config.validate()) {
        return 1;
    }

    // Setup signal handlers
    setupSignalHandlers();

    // Initialize filesystem
    SQLFuseFS& fs = SQLFuseFS::instance();

    int result = fs.init(config);
    if (result != 0) {
        spdlog::error("Failed to initialize filesystem");
        return 1;
    }

    spdlog::info("Filesystem initialized, starting FUSE");

    // Run FUSE main loop
    result = fs.run(argc, argv);

    if (g_signal_received) {
        spdlog::info("Received signal {}, shutting down", g_signal_received);
    }

    fs.shutdown();

    spdlog::info("SQL FUSE filesystem stopped");

    return result;
}
