#include "MySQLConnectionPool.hpp"
#include "MySQLVirtualFile.hpp"
#include "ErrorHandler.hpp"
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace sqlfuse {

MySQLConnectionPool::MySQLConnectionPool(const ConnectionConfig& config, size_t poolSize)
    : m_config(config), m_poolSize(poolSize) {

    // Initialize MySQL library (thread-safe)
    static std::once_flag mysqlInitFlag;
    std::call_once(mysqlInitFlag, []() {
        mysql_library_init(0, nullptr, nullptr);
    });

    // Pre-create some connections
    size_t initialCount = std::min(poolSize / 2, size_t(3));
    for (size_t i = 0; i < initialCount; ++i) {
        try {
            MYSQL* conn = createConnection();
            if (conn) {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_available.push(conn);
            }
        } catch (const std::exception& e) {
            spdlog::warn("Failed to pre-create connection: {}", e.what());
        }
    }

    spdlog::info("MySQL connection pool initialized with {} connections", m_createdCount.load());
}

MySQLConnectionPool::~MySQLConnectionPool() {
    drain();
}

MYSQL* MySQLConnectionPool::createConnection() {
    MYSQL* conn = mysql_init(nullptr);
    if (!conn) {
        throw MySQLException(0, "Failed to initialize MySQL connection");
    }

    // Set options
    unsigned int timeout = static_cast<unsigned int>(
        m_config.connect_timeout.count() / 1000);
    mysql_options(conn, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);

    unsigned int readTimeout = static_cast<unsigned int>(
        m_config.read_timeout.count() / 1000);
    mysql_options(conn, MYSQL_OPT_READ_TIMEOUT, &readTimeout);

    unsigned int writeTimeout = static_cast<unsigned int>(
        m_config.write_timeout.count() / 1000);
    mysql_options(conn, MYSQL_OPT_WRITE_TIMEOUT, &writeTimeout);

    // Enable auto-reconnect
    bool reconnect = true;
    mysql_options(conn, MYSQL_OPT_RECONNECT, &reconnect);

    // SSL options
    if (m_config.use_ssl) {
        mysql_ssl_set(conn,
                     m_config.ssl_key.empty() ? nullptr : m_config.ssl_key.c_str(),
                     m_config.ssl_cert.empty() ? nullptr : m_config.ssl_cert.c_str(),
                     m_config.ssl_ca.empty() ? nullptr : m_config.ssl_ca.c_str(),
                     nullptr, nullptr);
    }

    // Connect
    const char* socket = m_config.socket.empty() ? nullptr : m_config.socket.c_str();
    const char* db = m_config.default_database.empty() ? nullptr : m_config.default_database.c_str();

    if (!mysql_real_connect(conn,
                            m_config.host.c_str(),
                            m_config.user.c_str(),
                            m_config.password.c_str(),
                            db,
                            m_config.port,
                            socket,
                            CLIENT_MULTI_STATEMENTS)) {
        unsigned int err = mysql_errno(conn);
        std::string msg = mysql_error(conn);
        mysql_close(conn);
        throw MySQLException(err, "Failed to connect to MySQL: " + msg);
    }

    // Set character set to UTF-8
    mysql_set_character_set(conn, "utf8mb4");

    m_createdCount++;
    spdlog::debug("Created new MySQL connection (total: {})", m_createdCount.load());

    return conn;
}

std::unique_ptr<MySQLConnection> MySQLConnectionPool::acquire(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(m_mutex);

    m_waitingCount++;

    auto deadline = std::chrono::steady_clock::now() + timeout;

    while (m_available.empty() && !m_shutdown) {
        // Try to create a new connection if under limit
        if (m_createdCount < m_poolSize) {
            lock.unlock();
            try {
                MYSQL* conn = createConnection();
                m_waitingCount--;
                return std::make_unique<MySQLConnection>(this, conn);
            } catch (const std::exception& e) {
                spdlog::error("Failed to create connection: {}", e.what());
                lock.lock();
            }
        }

        // Wait for a connection to be released
        if (m_cv.wait_until(lock, deadline) == std::cv_status::timeout) {
            m_waitingCount--;
            throw MySQLException(CR_CONNECTION_ERROR,
                "Timeout waiting for database connection");
        }
    }

    m_waitingCount--;

    if (m_shutdown) {
        throw MySQLException(CR_CONNECTION_ERROR, "Connection pool is shutting down");
    }

    MYSQL* conn = m_available.front();
    m_available.pop();

    // Validate the connection
    if (!validateConnection(conn)) {
        destroyConnection(conn);
        lock.unlock();
        // Try to create a new one
        conn = createConnection();
    }

    return std::make_unique<MySQLConnection>(this, conn);
}

std::unique_ptr<MySQLConnection> MySQLConnectionPool::tryAcquire() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_shutdown || m_available.empty()) {
        return nullptr;
    }

    MYSQL* conn = m_available.front();
    m_available.pop();

    if (!validateConnection(conn)) {
        destroyConnection(conn);
        return nullptr;
    }

    return std::make_unique<MySQLConnection>(this, conn);
}

void MySQLConnectionPool::releaseConnection(MYSQL* conn) {
    if (!conn) return;

    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_shutdown) {
        destroyConnection(conn);
        return;
    }

    m_available.push(conn);
    m_cv.notify_one();
}

void MySQLConnectionPool::destroyConnection(MYSQL* conn) {
    if (conn) {
        mysql_close(conn);
        m_createdCount--;
        spdlog::debug("Destroyed MySQL connection (remaining: {})", m_createdCount.load());
    }
}

bool MySQLConnectionPool::validateConnection(MYSQL* conn) {
    if (!conn) return false;

    // Quick ping to check if connection is alive
    if (mysql_ping(conn) != 0) {
        spdlog::debug("Connection validation failed: {}", mysql_error(conn));
        return false;
    }

    return true;
}

size_t MySQLConnectionPool::availableCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_available.size();
}

size_t MySQLConnectionPool::totalCount() const {
    return m_createdCount.load();
}

size_t MySQLConnectionPool::waitingCount() const {
    return m_waitingCount.load();
}

bool MySQLConnectionPool::healthCheck() {
    try {
        auto conn = acquire(std::chrono::milliseconds(1000));
        return conn && conn->ping();
    } catch (...) {
        return false;
    }
}

void MySQLConnectionPool::drain() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_shutdown = true;

    while (!m_available.empty()) {
        MYSQL* conn = m_available.front();
        m_available.pop();
        if (conn) {
            mysql_close(conn);
            m_createdCount--;
        }
    }

    m_cv.notify_all();
    spdlog::info("MySQL connection pool drained");
}

std::unique_ptr<VirtualFile> MySQLConnectionPool::createVirtualFile(
    const ParsedPath& path,
    SchemaManager& schema,
    CacheManager& cache,
    const DataConfig& config) {
    return std::make_unique<MySQLVirtualFile>(path, schema, cache, config);
}

}  // namespace sqlfuse
