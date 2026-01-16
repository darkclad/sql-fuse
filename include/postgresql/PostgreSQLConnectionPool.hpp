#pragma once

/**
 * @file PostgreSQLConnectionPool.hpp
 * @brief Thread-safe connection pool for PostgreSQL database connections.
 *
 * This file implements a connection pool that manages PGconn* handles
 * for efficient reuse across multiple operations. The pool handles
 * connection lifecycle, validation, and thread-safe distribution.
 */

#include "ConnectionPool.hpp"
#include "Config.hpp"
#include "PostgreSQLConnection.hpp"
#include <libpq-fe.h>
#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace sqlfuse {

/**
 * @class PostgreSQLConnectionPool
 * @brief Thread-safe pool of PostgreSQL database connections.
 *
 * Manages a pool of PGconn* handles, providing efficient connection reuse.
 * Connections are validated before being handed out and can be created
 * on-demand up to the configured pool size.
 *
 * PostgreSQL Connection Management:
 * - Uses libpq's PQconnectdb() for connection establishment
 * - Connection string format: "host=X dbname=Y user=Z password=W"
 * - Validates connections with a simple query before reuse
 *
 * Pool Behavior:
 * - acquire() blocks until a connection is available or timeout
 * - tryAcquire() returns immediately with nullptr if none available
 * - Released connections are validated before being returned to pool
 * - Invalid connections are destroyed and replaced
 *
 * Thread Safety:
 * - All public methods are thread-safe
 * - Uses mutex and condition_variable for synchronization
 * - Atomic counters for statistics
 *
 * @see PostgreSQLConnection for connection wrapper usage
 */
class PostgreSQLConnectionPool : public ConnectionPool {
public:
    /**
     * @brief Create a new PostgreSQL connection pool.
     * @param config Connection configuration (host, port, database, credentials).
     * @param poolSize Maximum number of connections to maintain (default: 10).
     *
     * Does not immediately create connections; they are created on-demand.
     */
    explicit PostgreSQLConnectionPool(const ConnectionConfig& config, size_t poolSize = 10);

    /**
     * @brief Destructor - closes all connections.
     *
     * Calls drain() to close all pooled connections.
     */
    ~PostgreSQLConnectionPool() override;

    /**
     * @brief Acquire a connection from the pool.
     * @param timeout Maximum time to wait for a connection.
     * @return unique_ptr to PostgreSQLConnection, or nullptr on timeout.
     *
     * If no connections are available and pool is not at capacity,
     * creates a new connection. Otherwise waits for a connection
     * to be released or timeout.
     */
    std::unique_ptr<PostgreSQLConnection> acquire(
        std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));

    /**
     * @brief Try to acquire a connection without blocking.
     * @return unique_ptr to PostgreSQLConnection, or nullptr if none available.
     *
     * Returns immediately with nullptr if no connections are available
     * in the pool and creating a new one would exceed pool size.
     */
    std::unique_ptr<PostgreSQLConnection> tryAcquire();

    // ----- ConnectionPool interface implementation -----

    /**
     * @brief Get the number of available connections in the pool.
     * @return Number of idle connections ready for use.
     */
    size_t availableCount() const override;

    /**
     * @brief Get the total number of connections created.
     * @return Total connections (available + in-use).
     */
    size_t totalCount() const override;

    /**
     * @brief Check if the pool can provide working connections.
     * @return true if at least one valid connection can be established.
     *
     * Attempts to acquire and validate a connection.
     */
    bool healthCheck() override;

    /**
     * @brief Close all connections in the pool.
     *
     * Closes all idle connections and sets shutdown flag.
     * In-use connections will be destroyed when released.
     */
    void drain() override;

    /**
     * @brief Create a VirtualFile for the PostgreSQL backend.
     * @param path Parsed path identifying the database object.
     * @param schema Schema manager for metadata queries.
     * @param cache Cache manager for content caching.
     * @param config Data configuration options.
     * @return unique_ptr to PostgreSQLVirtualFile.
     */
    std::unique_ptr<VirtualFile> createVirtualFile(
        const ParsedPath& path,
        SchemaManager& schema,
        CacheManager& cache,
        const DataConfig& config) override;

    // ----- PostgreSQL-specific methods -----

    /**
     * @brief Get the number of threads waiting for connections.
     * @return Current wait queue size.
     *
     * Useful for monitoring pool saturation.
     */
    size_t waitingCount() const;

private:
    friend class PostgreSQLConnection;

    /**
     * @brief Create a new PostgreSQL connection.
     * @return Raw PGconn* handle, or nullptr on failure.
     *
     * Builds connection string from config and calls PQconnectdb().
     */
    PGconn* createConnection();

    /**
     * @brief Return a connection to the pool.
     * @param conn Connection to release.
     *
     * Validates the connection; if invalid, destroys it instead.
     */
    void releaseConnection(PGconn* conn);

    /**
     * @brief Destroy a connection.
     * @param conn Connection to close.
     *
     * Calls PQfinish() and decrements created count.
     */
    void destroyConnection(PGconn* conn);

    /**
     * @brief Validate a connection before reuse.
     * @param conn Connection to validate.
     * @return true if connection is valid and responsive.
     *
     * Checks PQstatus() and executes a simple query.
     */
    bool validateConnection(PGconn* conn);

    ConnectionConfig m_config;    ///< Connection parameters
    size_t m_poolSize;            ///< Maximum pool size

    std::queue<PGconn*> m_available;      ///< Idle connections ready for use
    std::atomic<size_t> m_createdCount{0}; ///< Total connections created
    std::atomic<size_t> m_waitingCount{0}; ///< Threads waiting for connections

    mutable std::mutex m_mutex;            ///< Protects m_available queue
    std::condition_variable m_cv;          ///< Signals when connections available
    std::atomic<bool> m_shutdown{false};   ///< Shutdown flag
};

}  // namespace sqlfuse
