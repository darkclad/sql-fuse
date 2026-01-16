#include "Config.hpp"
#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>
#include <fstream>
#include <cstdlib>
#include <algorithm>

namespace sqlfuse {

namespace {

std::string trim(const std::string& str) {
    auto start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

std::vector<std::string> split(const std::string& str, char delimiter) {
    std::vector<std::string> result;
    std::string current;
    for (char c : str) {
        if (c == delimiter) {
            if (!current.empty()) {
                result.push_back(trim(current));
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        result.push_back(trim(current));
    }
    return result;
}

}  // namespace

std::optional<Config> Config::loadFromFile(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return std::nullopt;
    }

    Config config;
    std::string line;
    std::string current_section;

    while (std::getline(file, line)) {
        line = trim(line);

        // Skip empty lines and comments
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }

        // Section header
        if (line[0] == '[' && line.back() == ']') {
            current_section = line.substr(1, line.size() - 2);
            continue;
        }

        // Key-value pair
        auto eq_pos = line.find('=');
        if (eq_pos == std::string::npos) {
            continue;
        }

        std::string key = trim(line.substr(0, eq_pos));
        std::string value = trim(line.substr(eq_pos + 1));

        // Remove quotes if present
        if (value.size() >= 2 &&
            ((value.front() == '"' && value.back() == '"') ||
             (value.front() == '\'' && value.back() == '\''))) {
            value = value.substr(1, value.size() - 2);
        }

        // Apply to appropriate section
        if (current_section == "connection") {
            if (key == "host") config.connection.host = value;
            else if (key == "port") config.connection.port = static_cast<uint16_t>(std::stoi(value));
            else if (key == "user") config.connection.user = value;
            else if (key == "password") config.connection.password = value;
            else if (key == "socket") config.connection.socket = value;
            else if (key == "database") config.connection.default_database = value;
            else if (key == "use_ssl") config.connection.use_ssl = (value == "true" || value == "1");
            else if (key == "ssl_ca") config.connection.ssl_ca = value;
            else if (key == "ssl_cert") config.connection.ssl_cert = value;
            else if (key == "ssl_key") config.connection.ssl_key = value;
            else if (key == "connect_timeout")
                config.connection.connect_timeout = std::chrono::milliseconds(std::stoi(value));
            else if (key == "read_timeout")
                config.connection.read_timeout = std::chrono::milliseconds(std::stoi(value));
            else if (key == "write_timeout")
                config.connection.write_timeout = std::chrono::milliseconds(std::stoi(value));
        }
        else if (current_section == "cache") {
            if (key == "max_size_mb")
                config.cache.max_size_bytes = static_cast<size_t>(std::stoul(value)) * 1024 * 1024;
            else if (key == "data_ttl")
                config.cache.data_ttl = std::chrono::seconds(std::stoi(value));
            else if (key == "schema_ttl")
                config.cache.schema_ttl = std::chrono::seconds(std::stoi(value));
            else if (key == "metadata_ttl")
                config.cache.metadata_ttl = std::chrono::seconds(std::stoi(value));
            else if (key == "enabled")
                config.cache.enabled = (value == "true" || value == "1");
        }
        else if (current_section == "data") {
            if (key == "max_rows")
                config.data.max_rows_per_file = static_cast<size_t>(std::stoul(value));
            else if (key == "rows_per_page")
                config.data.rows_per_page = static_cast<size_t>(std::stoul(value));
            else if (key == "pretty_json")
                config.data.pretty_json = (value == "true" || value == "1");
            else if (key == "include_csv_header")
                config.data.include_csv_header = (value == "true" || value == "1");
            else if (key == "default_format")
                config.data.default_format = value;
        }
        else if (current_section == "security") {
            if (key == "read_only")
                config.security.read_only = (value == "true" || value == "1");
            else if (key == "allowed_databases")
                config.security.allowed_databases = split(value, ',');
            else if (key == "denied_databases")
                config.security.denied_databases = split(value, ',');
            else if (key == "expose_system_databases")
                config.security.expose_system_databases = (value == "true" || value == "1");
        }
        else if (current_section == "performance") {
            if (key == "connection_pool_size")
                config.performance.connection_pool_size = static_cast<size_t>(std::stoul(value));
            else if (key == "max_concurrent_queries")
                config.performance.max_concurrent_queries = static_cast<size_t>(std::stoul(value));
            else if (key == "max_fuse_threads")
                config.performance.max_fuse_threads = static_cast<size_t>(std::stoul(value));
            else if (key == "enable_query_cache")
                config.performance.enable_query_cache = (value == "true" || value == "1");
        }
    }

    return config;
}

Config Config::parseArgs(int argc, char* argv[]) {
    Config config;

    CLI::App app{"SQL FUSE Filesystem - Mount SQL databases as a filesystem"};

    // Database type option
    app.add_option("-t,--type", config.database_type,
                   "Database type (mysql, sqlite, postgresql, oracle)")
        ->default_val("mysql");

    // Connection options
    app.add_option("-H,--host", config.connection.host,
                   "Database server host (or SQLite file path)")
        ->default_val("localhost");
    app.add_option("-P,--port", config.connection.port, "Database server port")
        ->default_val(3306);
    app.add_option("-u,--user", config.connection.user, "Database username");
    app.add_option("-p,--password", config.connection.password, "Database password");
    app.add_option("-S,--socket", config.connection.socket, "Unix socket path");
    app.add_option("-D,--database", config.connection.default_database,
                   "Default database (or SQLite file path)");

    // SSL options
    app.add_flag("--ssl", config.connection.use_ssl, "Enable SSL connection");
    app.add_option("--ssl-ca", config.connection.ssl_ca, "SSL CA certificate file");
    app.add_option("--ssl-cert", config.connection.ssl_cert, "SSL client certificate file");
    app.add_option("--ssl-key", config.connection.ssl_key, "SSL client key file");

    // Cache options
    size_t cache_size_mb = config.cache.max_size_bytes / (1024 * 1024);
    app.add_option("--cache-size", cache_size_mb, "Maximum cache size in MB")
        ->default_val(100);
    int cache_ttl = static_cast<int>(config.cache.data_ttl.count());
    app.add_option("--cache-ttl", cache_ttl, "Default cache TTL in seconds")
        ->default_val(30);
    app.add_flag("--no-cache", [&config](int64_t) { config.cache.enabled = false; },
                 "Disable caching entirely");

    // Data options
    app.add_option("--max-rows", config.data.max_rows_per_file,
                   "Maximum rows in table files")
        ->default_val(10000);
    app.add_flag("--read-only", config.security.read_only, "Mount as read-only");

    std::string databases_str;
    app.add_option("--databases", databases_str,
                   "Comma-separated list of databases to expose");

    // FUSE options
    app.add_flag("-f,--foreground", config.foreground, "Run in foreground");
    app.add_flag("-d,--debug", config.debug, "Enable debug output");
    app.add_flag("--allow-other", config.allow_other, "Allow other users to access");
    app.add_flag("--allow-root", config.allow_root, "Allow root to access");
    app.add_option("--max-threads", config.performance.max_fuse_threads,
                   "Maximum FUSE worker threads")
        ->default_val(10);

    // Config file
    std::string config_file;
    app.add_option("-c,--config", config_file, "Path to configuration file");

    // Mountpoint (positional)
    app.add_option("mountpoint", config.mountpoint, "Mount point directory")
        ->required();

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        std::exit(app.exit(e));
    }

    // Load config file if specified
    if (!config_file.empty()) {
        auto file_config = loadFromFile(config_file);
        if (file_config) {
            // Command line args override file config
            // Merge: file_config values are used where command line didn't specify
            if (config.connection.host == "localhost" && file_config->connection.host != "localhost") {
                config.connection.host = file_config->connection.host;
            }
            if (config.connection.password.empty() && !file_config->connection.password.empty()) {
                config.connection.password = file_config->connection.password;
            }
            // ... more merging could be added
        } else {
            spdlog::warn("Could not load config file: {}", config_file);
        }
    }

    // Apply parsed values
    config.cache.max_size_bytes = cache_size_mb * 1024 * 1024;
    config.cache.data_ttl = std::chrono::seconds(cache_ttl);

    if (!databases_str.empty()) {
        config.security.allowed_databases = split(databases_str, ',');
    }

    // Resolve password from environment if not set
    config.resolvePassword();

    return config;
}

bool Config::validate() const {
    // Username is required for MySQL, optional for SQLite
    if (database_type != "sqlite" && database_type != "sqlite3" && connection.user.empty()) {
        spdlog::error("Database username is required (use -u option)");
        return false;
    }

    if (mountpoint.empty()) {
        spdlog::error("Mountpoint is required");
        return false;
    }

    if (!std::filesystem::exists(mountpoint)) {
        spdlog::error("Mountpoint does not exist: {}", mountpoint);
        return false;
    }

    if (!std::filesystem::is_directory(mountpoint)) {
        spdlog::error("Mountpoint is not a directory: {}", mountpoint);
        return false;
    }

    if (connection.use_ssl) {
        if (!connection.ssl_ca.empty() && !std::filesystem::exists(connection.ssl_ca)) {
            spdlog::error("SSL CA file not found: {}", connection.ssl_ca);
            return false;
        }
        if (!connection.ssl_cert.empty() && !std::filesystem::exists(connection.ssl_cert)) {
            spdlog::error("SSL certificate file not found: {}", connection.ssl_cert);
            return false;
        }
        if (!connection.ssl_key.empty() && !std::filesystem::exists(connection.ssl_key)) {
            spdlog::error("SSL key file not found: {}", connection.ssl_key);
            return false;
        }
    }

    return true;
}

void Config::resolvePassword() {
    if (connection.password.empty()) {
        const char* env_pwd = std::getenv("MYSQL_PWD");
        if (env_pwd) {
            connection.password = env_pwd;
        }
    }
}

}  // namespace sqlfuse
