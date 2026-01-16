#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <optional>
#include <filesystem>

namespace sqlfuse {

// Forward declaration
enum class DatabaseType;

struct ConnectionConfig {
    std::string host = "localhost";
    uint16_t port = 3306;
    std::string user;
    std::string password;
    std::string socket;
    std::string default_database;

    // SSL options
    bool use_ssl = false;
    std::string ssl_ca;
    std::string ssl_cert;
    std::string ssl_key;

    // Timeouts
    std::chrono::milliseconds connect_timeout{5000};
    std::chrono::milliseconds read_timeout{30000};
    std::chrono::milliseconds write_timeout{30000};
};

struct CacheConfig {
    size_t max_size_bytes = 100 * 1024 * 1024;  // 100 MB
    std::chrono::seconds data_ttl{30};
    std::chrono::seconds schema_ttl{300};
    std::chrono::seconds metadata_ttl{60};
    bool enabled = true;
};

struct DataConfig {
    size_t max_rows_per_file = 10000;
    size_t rows_per_page = 1000;
    bool pretty_json = true;
    bool include_csv_header = true;
    std::string default_format = "csv";
};

struct SecurityConfig {
    bool read_only = false;
    std::vector<std::string> allowed_databases;
    std::vector<std::string> denied_databases;
    bool expose_system_databases = false;
};

struct PerformanceConfig {
    size_t connection_pool_size = 10;
    size_t max_concurrent_queries = 20;
    size_t max_fuse_threads = 10;
    bool enable_query_cache = true;
};

struct Config {
    ConnectionConfig connection;
    CacheConfig cache;
    DataConfig data;
    SecurityConfig security;
    PerformanceConfig performance;

    std::string mountpoint;
    std::string database_type = "mysql";  // mysql, postgresql, oracle
    bool foreground = false;
    bool debug = false;
    bool allow_other = false;
    bool allow_root = false;

    // Load from file
    static std::optional<Config> loadFromFile(const std::filesystem::path& path);

    // Parse command line arguments
    static Config parseArgs(int argc, char* argv[]);

    // Validate configuration
    bool validate() const;

    // Get password from environment if not set
    void resolvePassword();
};

}  // namespace sqlfuse
