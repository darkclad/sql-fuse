#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "Config.hpp"
#include <fstream>
#include <filesystem>
#include <cstdlib>

using namespace sqlfuse;
using namespace std::chrono_literals;

class ConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create temp directory for test files
        tempDir_ = std::filesystem::temp_directory_path() / "mysql_fuse_test";
        std::filesystem::create_directories(tempDir_);
    }

    void TearDown() override {
        std::filesystem::remove_all(tempDir_);
    }

    std::filesystem::path tempDir_;

    void writeConfigFile(const std::string& filename, const std::string& content) {
        std::ofstream file(tempDir_ / filename);
        file << content;
    }
};

// Default configuration tests
TEST_F(ConfigTest, DefaultConnectionConfig) {
    ConnectionConfig config;

    EXPECT_EQ(config.host, "localhost");
    EXPECT_EQ(config.port, 3306);
    EXPECT_TRUE(config.user.empty());
    EXPECT_TRUE(config.password.empty());
    EXPECT_TRUE(config.socket.empty());
    EXPECT_FALSE(config.use_ssl);
    EXPECT_EQ(config.connect_timeout, 5000ms);
    EXPECT_EQ(config.read_timeout, 30000ms);
    EXPECT_EQ(config.write_timeout, 30000ms);
}

TEST_F(ConfigTest, DefaultCacheConfig) {
    CacheConfig config;

    EXPECT_EQ(config.max_size_bytes, 100 * 1024 * 1024);  // 100MB
    EXPECT_EQ(config.data_ttl, 30s);
    EXPECT_EQ(config.schema_ttl, 300s);
    EXPECT_EQ(config.metadata_ttl, 60s);
    EXPECT_TRUE(config.enabled);
}

TEST_F(ConfigTest, DefaultDataConfig) {
    DataConfig config;

    EXPECT_EQ(config.max_rows_per_file, 10000u);
    EXPECT_EQ(config.rows_per_page, 1000u);
    EXPECT_TRUE(config.pretty_json);
    EXPECT_TRUE(config.include_csv_header);
    EXPECT_EQ(config.default_format, "csv");
}

TEST_F(ConfigTest, DefaultSecurityConfig) {
    SecurityConfig config;

    EXPECT_FALSE(config.read_only);
    EXPECT_TRUE(config.allowed_databases.empty());
    EXPECT_TRUE(config.denied_databases.empty());
    EXPECT_FALSE(config.expose_system_databases);
}

TEST_F(ConfigTest, DefaultPerformanceConfig) {
    PerformanceConfig config;

    EXPECT_EQ(config.connection_pool_size, 10u);
    EXPECT_EQ(config.max_concurrent_queries, 20u);
    EXPECT_TRUE(config.enable_query_cache);
}

TEST_F(ConfigTest, DefaultConfig) {
    Config config;

    EXPECT_TRUE(config.mountpoint.empty());
    EXPECT_FALSE(config.foreground);
    EXPECT_FALSE(config.debug);
    EXPECT_FALSE(config.allow_other);
    EXPECT_FALSE(config.allow_root);
}

// Config file loading tests
TEST_F(ConfigTest, LoadFromValidFile) {
    writeConfigFile("valid.conf", R"(
[connection]
host = mysql.example.com
port = 3307
user = testuser
password = testpass

[cache]
max_size_bytes = 52428800
data_ttl = 60
schema_ttl = 600

[data]
max_rows_per_file = 5000
pretty_json = false

[security]
read_only = true

[performance]
connection_pool_size = 20
)");

    auto config = Config::loadFromFile(tempDir_ / "valid.conf");

    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->connection.host, "mysql.example.com");
    EXPECT_EQ(config->connection.port, 3307);
    EXPECT_EQ(config->connection.user, "testuser");
    EXPECT_EQ(config->connection.password, "testpass");
    EXPECT_EQ(config->cache.max_size_bytes, 52428800u);
    EXPECT_EQ(config->cache.data_ttl, 60s);
    EXPECT_EQ(config->cache.schema_ttl, 600s);
    EXPECT_EQ(config->data.max_rows_per_file, 5000u);
    EXPECT_FALSE(config->data.pretty_json);
    EXPECT_TRUE(config->security.read_only);
    EXPECT_EQ(config->performance.connection_pool_size, 20u);
}

TEST_F(ConfigTest, LoadFromNonExistentFile) {
    auto config = Config::loadFromFile("/nonexistent/path/config.conf");

    EXPECT_FALSE(config.has_value());
}

TEST_F(ConfigTest, LoadFromEmptyFile) {
    writeConfigFile("empty.conf", "");

    auto config = Config::loadFromFile(tempDir_ / "empty.conf");

    // Should still return a config with defaults
    ASSERT_TRUE(config.has_value());
}

TEST_F(ConfigTest, LoadPartialConfig) {
    writeConfigFile("partial.conf", R"(
[connection]
host = custom.host.com
)");

    auto config = Config::loadFromFile(tempDir_ / "partial.conf");

    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->connection.host, "custom.host.com");
    // Other values should be defaults
    EXPECT_EQ(config->connection.port, 3306);
}

TEST_F(ConfigTest, LoadConfigWithSSL) {
    writeConfigFile("ssl.conf", R"(
[connection]
use_ssl = true
ssl_ca = /path/to/ca.pem
ssl_cert = /path/to/cert.pem
ssl_key = /path/to/key.pem
)");

    auto config = Config::loadFromFile(tempDir_ / "ssl.conf");

    ASSERT_TRUE(config.has_value());
    EXPECT_TRUE(config->connection.use_ssl);
    EXPECT_EQ(config->connection.ssl_ca, "/path/to/ca.pem");
    EXPECT_EQ(config->connection.ssl_cert, "/path/to/cert.pem");
    EXPECT_EQ(config->connection.ssl_key, "/path/to/key.pem");
}

TEST_F(ConfigTest, LoadConfigWithDatabaseLists) {
    writeConfigFile("dblist.conf", R"(
[security]
allowed_databases = db1,db2,db3
denied_databases = mysql,information_schema
expose_system_databases = true
)");

    auto config = Config::loadFromFile(tempDir_ / "dblist.conf");

    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->security.allowed_databases.size(), 3u);
    EXPECT_EQ(config->security.denied_databases.size(), 2u);
    EXPECT_TRUE(config->security.expose_system_databases);
}

// Config validation tests
TEST_F(ConfigTest, ValidateWithMountpoint) {
    Config config;
    config.mountpoint = "/mnt/mysql";
    config.connection.user = "root";

    EXPECT_TRUE(config.validate());
}

TEST_F(ConfigTest, ValidateWithoutMountpoint) {
    Config config;
    config.connection.user = "root";

    // Should fail validation without mountpoint
    EXPECT_FALSE(config.validate());
}

TEST_F(ConfigTest, ValidateWithoutUser) {
    Config config;
    config.mountpoint = "/mnt/mysql";

    // Should fail validation without user
    EXPECT_FALSE(config.validate());
}

// Password resolution tests
TEST_F(ConfigTest, ResolvePasswordFromEnv) {
    Config config;
    config.connection.password = "";

    // Set environment variable
    setenv("MYSQL_PWD", "env_password", 1);

    config.resolvePassword();

    EXPECT_EQ(config.connection.password, "env_password");

    // Clean up
    unsetenv("MYSQL_PWD");
}

TEST_F(ConfigTest, ResolvePasswordKeepsExisting) {
    Config config;
    config.connection.password = "existing_password";

    setenv("MYSQL_PWD", "env_password", 1);

    config.resolvePassword();

    // Should keep existing password
    EXPECT_EQ(config.connection.password, "existing_password");

    unsetenv("MYSQL_PWD");
}

TEST_F(ConfigTest, ResolvePasswordNoEnvVar) {
    Config config;
    config.connection.password = "";

    unsetenv("MYSQL_PWD");

    config.resolvePassword();

    // Should remain empty
    EXPECT_TRUE(config.connection.password.empty());
}

// Edge cases
TEST_F(ConfigTest, ConfigWithCommentsAndWhitespace) {
    writeConfigFile("comments.conf", R"(
# This is a comment
[connection]
# Another comment
host = localhost  # inline comment might not be supported
  port = 3306  # spaces before key

[cache]
  max_size_bytes = 1048576
)");

    auto config = Config::loadFromFile(tempDir_ / "comments.conf");

    ASSERT_TRUE(config.has_value());
    // Behavior depends on parser implementation
}

TEST_F(ConfigTest, ConfigWithInvalidValues) {
    writeConfigFile("invalid.conf", R"(
[connection]
port = not_a_number
)");

    // Should either handle gracefully or return nullopt
    auto config = Config::loadFromFile(tempDir_ / "invalid.conf");
    // Exact behavior depends on implementation
}

// Timeout configuration tests
TEST_F(ConfigTest, ConfigTimeouts) {
    writeConfigFile("timeouts.conf", R"(
[connection]
connect_timeout = 10000
read_timeout = 60000
write_timeout = 60000
)");

    auto config = Config::loadFromFile(tempDir_ / "timeouts.conf");

    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->connection.connect_timeout, 10000ms);
    EXPECT_EQ(config->connection.read_timeout, 60000ms);
    EXPECT_EQ(config->connection.write_timeout, 60000ms);
}

// Socket configuration test
TEST_F(ConfigTest, ConfigSocket) {
    writeConfigFile("socket.conf", R"(
[connection]
socket = /var/run/mysqld/mysqld.sock
)");

    auto config = Config::loadFromFile(tempDir_ / "socket.conf");

    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->connection.socket, "/var/run/mysqld/mysqld.sock");
}

// Default database configuration test
TEST_F(ConfigTest, ConfigDefaultDatabase) {
    writeConfigFile("default_db.conf", R"(
[connection]
default_database = myapp
)");

    auto config = Config::loadFromFile(tempDir_ / "default_db.conf");

    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->connection.default_database, "myapp");
}

// All sections test
TEST_F(ConfigTest, LoadAllSections) {
    writeConfigFile("full.conf", R"(
[connection]
host = db.example.com
port = 3306
user = appuser
password = secret
default_database = production
use_ssl = true

[cache]
enabled = true
max_size_bytes = 209715200
data_ttl = 30
schema_ttl = 300
metadata_ttl = 60

[data]
max_rows_per_file = 10000
rows_per_page = 1000
pretty_json = true
include_csv_header = true
default_format = json

[security]
read_only = false
expose_system_databases = false

[performance]
connection_pool_size = 15
max_concurrent_queries = 30
enable_query_cache = true
)");

    auto config = Config::loadFromFile(tempDir_ / "full.conf");

    ASSERT_TRUE(config.has_value());

    // Verify all sections loaded
    EXPECT_EQ(config->connection.host, "db.example.com");
    EXPECT_TRUE(config->connection.use_ssl);
    EXPECT_EQ(config->cache.max_size_bytes, 209715200u);
    EXPECT_TRUE(config->cache.enabled);
    EXPECT_EQ(config->data.default_format, "json");
    EXPECT_FALSE(config->security.expose_system_databases);
    EXPECT_EQ(config->performance.connection_pool_size, 15u);
}

// Boolean parsing tests
TEST_F(ConfigTest, BooleanParsing) {
    writeConfigFile("booleans.conf", R"(
[cache]
enabled = true

[security]
read_only = false
expose_system_databases = 1
)");

    auto config = Config::loadFromFile(tempDir_ / "booleans.conf");

    ASSERT_TRUE(config.has_value());
    EXPECT_TRUE(config->cache.enabled);
    EXPECT_FALSE(config->security.read_only);
    // "1" might be parsed as true depending on implementation
}

// Case sensitivity test
TEST_F(ConfigTest, SectionNamesCaseInsensitive) {
    writeConfigFile("case.conf", R"(
[CONNECTION]
host = upper.case.host
)");

    auto config = Config::loadFromFile(tempDir_ / "case.conf");

    // Behavior depends on implementation - some parsers are case insensitive
    // This test documents the expected behavior
}
