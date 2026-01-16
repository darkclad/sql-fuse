#pragma once

/**
 * @file MySQLConnectionPool.hpp
 * @brief Thread-safe connection pool for MySQL database connections.
 *
 * This file implements a connection pool for MySQL databases using the
 * MySQL C API (libmysqlclient). The pool manages a fixed number of connections
 * that are shared across multiple threads, reducing connection overhead.
 */

#include "ConnectionPool.hpp"
#include "Config.hpp"
#include "MySQLConnection.hpp"
#include <mysql/mysql.h>
#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace sqlfuse {

/**
 * @class MySQLConnectionPool
 * @brief Thread-safe pool of reusable MySQL database connections.
 *
 * MySQLConnectionPool manages a pool of libmysqlclient connections to a MySQL database.
 * Connections are pre-created at pool initialization and reused across requests,
 * avoiding the overhead of establishing new connections for each operation.
 *
 * Key features:
 * - Pre-created connections at startup for immediate availability
 * - Automatic connection validation using mysql_ping before returning to caller
 * - Automatic reconnection if a pooled connection has become stale
 * - Thread-safe acquire/release with condition variable for waiting
 * - Configurable pool size and acquisition timeout
 * - Clean shutdown with connection draining
 *
 * Thread Safety:
 * - All public methods are thread-safe
 * - Individual MySQLConnection objects should only be used by one thread
 *
 * @see MySQLConnection for using acquired connections
 */
class MySQLConnectionPool : public ConnectionPool {
public:
    /**
     * @brief Create a new MySQL connection pool.
     * @param config Connection configuration (host, port, user, password, database).
     * @param poolSize Number of connections to maintain in the pool (default: 10).
     * @throws std::runtime_error if mysql_library_init fails or no connections can be created.
     *
     * The constructor attempts to create all poolSize connections. If some
     * connections fail but at least one succeeds, the pool will operate
     * with reduced capacity.
     */
    explicit MySQLConnectionPool(const ConnectionConfig& config, size_t poolSize = 10);

    /**
     * @brief Destructor - drains the pool and calls mysql_library_end.
     */
    ~MySQLConnectionPool() override;

    /**
     * @brief Acquire a connection from the pool, blocking if necessary.
     * @param timeout Maximum time to wait for a connection (default: 5 seconds).
     * @return unique_ptr to MySQLConnection, or nullptr on timeout/shutdown.
     *
     * If the pool is empty, this method blocks until a connection becomes
     * available or the timeout expires. The returned connection is validated
     * before being returned; stale connections are automatically replaced.
     */
    std::unique_ptr<MySQLConnection> acquire(
        std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));

    /**
     * @brief Try to acquire a connection without blocking.
     * @return unique_ptr to MySQLConnection if available, nullptr otherwise.
     *
     * Immediately returns nullptr if no connections are available.
     */
    std::unique_ptr<MySQLConnection> tryAcquire();

    // ----- ConnectionPool interface implementation -----

    /**
     * @brief Get the number of currently available (idle) connections.
     * @return Number of connections ready to be acquired.
     */
    size_t availableCount() const override;

    /**
     * @brief Get the total number of connections managed by the pool.
     * @return Total connection count (available + in-use).
     */
    size_t totalCount() const override;

    /**
     * @brief Check if the pool can provide working connections.
     * @return true if at least one connection is healthy.
     */
    bool healthCheck() override;

    /**
     * @brief Drain all connections and prepare for shutdown.
     *
     * After calling drain(), no new connections can be acquired.
     * All currently available connections are destroyed.
     * In-use connections will be destroyed when released.
     */
    void drain() override;

    /**
     * @brief Create a VirtualFile for the MySQL backend.
     * @param path Parsed path identifying the database object.
     * @param schema Schema manager for metadata queries.
     * @param cache Cache manager for content caching.
     * @param config Data configuration options.
     * @return unique_ptr to MySQLVirtualFile.
     */
    std::unique_ptr<VirtualFile> createVirtualFile(
        const ParsedPath& path,
        SchemaManager& schema,
        CacheManager& cache,
        const DataConfig& config) override;

    // ----- MySQL-specific methods -----

    /**
     * @brief Get the number of threads waiting for a connection.
     * @return Number of blocked acquire() calls.
     */
    size_t waitingCount() const;

private:
    friend class MySQLConnection;  // For releaseConnection access

    /**
     * @brief Create a new MySQL connection.
     * @return MYSQL* handle on success.
     * @throws std::runtime_error on connection failure.
     */
    MYSQL* createConnection();

    /**
     * @brief Return a connection to the pool.
     * @param conn MySQL connection handle.
     *
     * Called by MySQLConnection destructor. If pool is shutting down,
     * the connection is destroyed instead of being returned.
     */
    void releaseConnection(MYSQL* conn);

    /**
     * @brief Close and free a MySQL connection.
     * @param conn MySQL connection handle to destroy.
     */
    void destroyConnection(MYSQL* conn);

    /**
     * @brief Check if a connection is still valid using mysql_ping.
     * @param conn MySQL connection handle.
     * @return true if the connection is healthy.
     */
    bool validateConnection(MYSQL* conn);

    ConnectionConfig m_config;        ///< Connection parameters
    size_t m_poolSize;                ///< Target pool size

    std::queue<MYSQL*> m_available;   ///< Idle connections
    std::atomic<size_t> m_createdCount{0};   ///< Total connections created
    std::atomic<size_t> m_waitingCount{0};   ///< Threads blocked on acquire()

    mutable std::mutex m_mutex;       ///< Protects m_available queue
    std::condition_variable m_cv;     ///< Signaled when connection available
    std::atomic<bool> m_shutdown{false};  ///< True if pool is draining
};

}  // namespace sqlfuse
