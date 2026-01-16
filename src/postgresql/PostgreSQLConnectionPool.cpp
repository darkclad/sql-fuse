/**
 * @file PostgreSQLConnectionPool.cpp
 * @brief Implementation of thread-safe PostgreSQL connection pool.
 *
 * Implements the PostgreSQLConnectionPool class which manages a pool of PostgreSQL
 * connections for efficient reuse across concurrent operations. Uses libpq
 * connection strings for configuration and supports SSL connections.
 */

#include "PostgreSQLConnectionPool.hpp"
#include "PostgreSQLVirtualFile.hpp"
#include "ErrorHandler.hpp"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <sstream>

namespace sqlfuse {

// ============================================================================
// Construction and Destruction
// ============================================================================

PostgreSQLConnectionPool::PostgreSQLConnectionPool(const ConnectionConfig& config, size_t poolSize)
    : m_config(config), m_poolSize(poolSize) {

    // Pre-create some connections to reduce initial latency
    size_t initialCount = std::min(poolSize / 2, size_t(3));
    for (size_t i = 0; i < initialCount; ++i) {
        try {
            PGconn* conn = createConnection();
            if (conn) {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_available.push(conn);
            }
        } catch (const std::exception& e) {
            spdlog::warn("Failed to pre-create PostgreSQL connection: {}", e.what());
        }
    }

    spdlog::info("PostgreSQL connection pool initialized with {} connections", m_createdCount.load());
}

PostgreSQLConnectionPool::~PostgreSQLConnectionPool() {
    drain();
}

// ============================================================================
// Connection Creation and Validation
// ============================================================================

PGconn* PostgreSQLConnectionPool::createConnection() {
    // Build connection string
    std::ostringstream connInfo;

    connInfo << "host=" << m_config.host;
    connInfo << " port=" << m_config.port;

    if (!m_config.user.empty()) {
        connInfo << " user=" << m_config.user;
    }

    if (!m_config.password.empty()) {
        connInfo << " password=" << m_config.password;
    }

    if (!m_config.default_database.empty()) {
        connInfo << " dbname=" << m_config.default_database;
    }

    // Timeout in seconds
    connInfo << " connect_timeout=" << (m_config.connect_timeout.count() / 1000);

    // SSL options
    if (m_config.use_ssl) {
        connInfo << " sslmode=require";
        if (!m_config.ssl_ca.empty()) {
            connInfo << " sslrootcert=" << m_config.ssl_ca;
        }
        if (!m_config.ssl_cert.empty()) {
            connInfo << " sslcert=" << m_config.ssl_cert;
        }
        if (!m_config.ssl_key.empty()) {
            connInfo << " sslkey=" << m_config.ssl_key;
        }
    } else {
        connInfo << " sslmode=prefer";
    }

    // Application name for identification
    connInfo << " application_name=sql-fuse";

    PGconn* conn = PQconnectdb(connInfo.str().c_str());

    if (!conn) {
        throw std::runtime_error("Failed to allocate PostgreSQL connection");
    }

    if (PQstatus(conn) != CONNECTION_OK) {
        std::string errorMsg = PQerrorMessage(conn);
        PQfinish(conn);
        throw std::runtime_error("Failed to connect to PostgreSQL: " + errorMsg);
    }

    // Set client encoding to UTF-8
    PQsetClientEncoding(conn, "UTF8");

    m_createdCount++;
    spdlog::debug("Created new PostgreSQL connection (total: {})", m_createdCount.load());

    return conn;
}

// ============================================================================
// Connection Acquisition
// ============================================================================

std::unique_ptr<PostgreSQLConnection> PostgreSQLConnectionPool::acquire(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(m_mutex);

    m_waitingCount++;

    auto deadline = std::chrono::steady_clock::now() + timeout;

    while (m_available.empty() && !m_shutdown) {
        // Try to create a new connection if under limit
        if (m_createdCount < m_poolSize) {
            lock.unlock();
            try {
                PGconn* conn = createConnection();
                m_waitingCount--;
                return std::make_unique<PostgreSQLConnection>(this, conn);
            } catch (const std::exception& e) {
                spdlog::error("Failed to create PostgreSQL connection: {}", e.what());
                lock.lock();
            }
        }

        // Wait for a connection to be released
        if (m_cv.wait_until(lock, deadline) == std::cv_status::timeout) {
            m_waitingCount--;
            throw std::runtime_error("Timeout waiting for PostgreSQL connection");
        }
    }

    m_waitingCount--;

    if (m_shutdown) {
        throw std::runtime_error("PostgreSQL connection pool is shutting down");
    }

    PGconn* conn = m_available.front();
    m_available.pop();

    // Validate the connection
    if (!validateConnection(conn)) {
        destroyConnection(conn);
        lock.unlock();
        // Try to create a new one
        conn = createConnection();
    }

    return std::make_unique<PostgreSQLConnection>(this, conn);
}

std::unique_ptr<PostgreSQLConnection> PostgreSQLConnectionPool::tryAcquire() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_shutdown || m_available.empty()) {
        return nullptr;
    }

    PGconn* conn = m_available.front();
    m_available.pop();

    if (!validateConnection(conn)) {
        destroyConnection(conn);
        return nullptr;
    }

    return std::make_unique<PostgreSQLConnection>(this, conn);
}

// ============================================================================
// Connection Release
// ============================================================================

void PostgreSQLConnectionPool::releaseConnection(PGconn* conn) {
    if (!conn) return;

    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_shutdown) {
        destroyConnection(conn);
        return;
    }

    m_available.push(conn);
    m_cv.notify_one();
}

void PostgreSQLConnectionPool::destroyConnection(PGconn* conn) {
    if (conn) {
        PQfinish(conn);
        m_createdCount--;
        spdlog::debug("Destroyed PostgreSQL connection (remaining: {})", m_createdCount.load());
    }
}

bool PostgreSQLConnectionPool::validateConnection(PGconn* conn) {
    if (!conn) return false;

    // Check connection status
    if (PQstatus(conn) != CONNECTION_OK) {
        spdlog::debug("PostgreSQL connection validation failed: bad status");
        return false;
    }

    // Try a simple query
    PGresult* res = PQexec(conn, "SELECT 1");
    bool ok = res && PQresultStatus(res) == PGRES_TUPLES_OK;
    if (res) PQclear(res);

    if (!ok) {
        spdlog::debug("PostgreSQL connection validation failed: ping query failed");
    }

    return ok;
}

// ============================================================================
// Pool Statistics and Management
// ============================================================================

size_t PostgreSQLConnectionPool::availableCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_available.size();
}

size_t PostgreSQLConnectionPool::totalCount() const {
    return m_createdCount.load();
}

size_t PostgreSQLConnectionPool::waitingCount() const {
    return m_waitingCount.load();
}

bool PostgreSQLConnectionPool::healthCheck() {
    try {
        auto conn = acquire(std::chrono::milliseconds(1000));
        return conn && conn->ping();
    } catch (...) {
        return false;
    }
}

void PostgreSQLConnectionPool::drain() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_shutdown = true;

    while (!m_available.empty()) {
        PGconn* conn = m_available.front();
        m_available.pop();
        if (conn) {
            PQfinish(conn);
            m_createdCount--;
        }
    }

    m_cv.notify_all();
    spdlog::info("PostgreSQL connection pool drained");
}

// ============================================================================
// Virtual File Factory
// ============================================================================

std::unique_ptr<VirtualFile> PostgreSQLConnectionPool::createVirtualFile(
    const ParsedPath& path,
    SchemaManager& schema,
    CacheManager& cache,
    const DataConfig& config) {
    return std::make_unique<PostgreSQLVirtualFile>(path, schema, cache, config);
}

}  // namespace sqlfuse
