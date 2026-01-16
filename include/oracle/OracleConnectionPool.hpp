#pragma once

/**
 * @file OracleConnectionPool.hpp
 * @brief Thread-safe connection pool for Oracle database connections.
 *
 * This file implements a connection pool specifically for Oracle databases using
 * the Oracle Call Interface (OCI). The pool manages a fixed number of connections
 * that are shared across multiple threads, reducing connection overhead.
 */

#include "ConnectionPool.hpp"
#include "Config.hpp"
#include "OracleConnection.hpp"
#include <oci.h>
#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace sqlfuse {

/**
 * @class OracleConnectionPool
 * @brief Thread-safe pool of reusable Oracle database connections.
 *
 * OracleConnectionPool manages a pool of OCI connections to an Oracle database.
 * Connections are pre-created at pool initialization and reused across requests,
 * avoiding the overhead of establishing new connections for each operation.
 *
 * Key features:
 * - Pre-created connections at startup for immediate availability
 * - Automatic connection validation using OCIPing before returning to caller
 * - Automatic reconnection if a pooled connection has become stale
 * - Thread-safe acquire/release with condition variable for waiting
 * - Configurable pool size and acquisition timeout
 * - Clean shutdown with connection draining
 *
 * Connection String Formats Supported:
 * - Easy Connect: "host:port/service_name" (e.g., "localhost:1521/XE")
 * - TNS Name: "MYDB" (requires tnsnames.ora configuration)
 * - Full Descriptor: "(DESCRIPTION=(ADDRESS=...))"
 *
 * Thread Safety:
 * - All public methods are thread-safe
 * - Individual OracleConnection objects should only be used by one thread
 *
 * @see OracleConnection for using acquired connections
 */
class OracleConnectionPool : public ConnectionPool {
public:
    /**
     * @brief Create a new Oracle connection pool.
     * @param config Connection configuration (host, port, user, password, etc.).
     * @param poolSize Number of connections to maintain in the pool (default: 10).
     * @throws std::runtime_error if the OCI environment cannot be initialized
     *         or if no connections can be created.
     *
     * The constructor attempts to create all poolSize connections. If some
     * connections fail but at least one succeeds, the pool will operate
     * with reduced capacity.
     */
    explicit OracleConnectionPool(const ConnectionConfig& config, size_t poolSize = 10);

    /**
     * @brief Destructor - drains the pool and cleans up OCI resources.
     */
    ~OracleConnectionPool() override;

    /**
     * @brief Acquire a connection from the pool, blocking if necessary.
     * @param timeout Maximum time to wait for a connection (default: 5 seconds).
     * @return unique_ptr to OracleConnection, or nullptr on timeout/shutdown.
     *
     * If the pool is empty, this method blocks until a connection becomes
     * available or the timeout expires. The returned connection is validated
     * before being returned; stale connections are automatically replaced.
     */
    std::unique_ptr<OracleConnection> acquire(
        std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));

    /**
     * @brief Try to acquire a connection without blocking.
     * @return unique_ptr to OracleConnection if available, nullptr otherwise.
     *
     * Immediately returns nullptr if no connections are available.
     * Does not validate the connection before returning.
     */
    std::unique_ptr<OracleConnection> tryAcquire();

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
     * @brief Create a VirtualFile for the Oracle backend.
     * @param path Parsed path identifying the database object.
     * @param schema Schema manager for metadata queries.
     * @param cache Cache manager for content caching.
     * @param config Data configuration options.
     * @return unique_ptr to OracleVirtualFile.
     */
    std::unique_ptr<VirtualFile> createVirtualFile(
        const ParsedPath& path,
        SchemaManager& schema,
        CacheManager& cache,
        const DataConfig& config) override;

    // ----- Oracle-specific methods -----

    /**
     * @brief Get the number of threads waiting for a connection.
     * @return Number of blocked acquire() calls.
     */
    size_t waitingCount() const;

    /**
     * @brief Get the shared OCI environment handle.
     * @return OCIEnv* used by all connections in this pool.
     */
    OCIEnv* getEnv() const { return m_env; }

private:
    friend class OracleConnection;  // For releaseConnection access

    /**
     * @struct PooledConnection
     * @brief Internal representation of a pooled connection's OCI handles.
     */
    struct PooledConnection {
        OCISvcCtx* svc;  ///< Service context handle
        OCIError* err;   ///< Error handle
    };

    /**
     * @brief Initialize the OCI environment.
     * @return true on success, false on failure.
     */
    bool initializeEnvironment();

    /**
     * @brief Create a new OCI connection with full session setup.
     * @return PooledConnection with initialized handles.
     * @throws std::runtime_error on connection failure.
     */
    PooledConnection createConnection();

    /**
     * @brief Return a connection to the pool.
     * @param svc Service context handle.
     * @param err Error handle.
     *
     * Called by OracleConnection destructor. If pool is shutting down,
     * the connection is destroyed instead of being returned.
     */
    void releaseConnection(OCISvcCtx* svc, OCIError* err);

    /**
     * @brief Properly close and free a connection's OCI handles.
     * @param svc Service context handle.
     * @param err Error handle.
     */
    void destroyConnection(OCISvcCtx* svc, OCIError* err);

    /**
     * @brief Check if a connection is still valid using OCIPing.
     * @param svc Service context handle.
     * @param err Error handle.
     * @return true if the connection responds to ping.
     */
    bool validateConnection(OCISvcCtx* svc, OCIError* err);

    /**
     * @brief Build an Oracle connection string from configuration.
     * @return Connection string suitable for OCIServerAttach.
     *
     * Supports Easy Connect format (host:port/service) and passes through
     * TNS names or full descriptors unchanged.
     */
    std::string buildConnectString() const;

    ConnectionConfig m_config;        ///< Connection parameters
    size_t m_poolSize;                ///< Target pool size

    OCIEnv* m_env = nullptr;          ///< OCI environment (shared by all connections)

    std::queue<PooledConnection> m_available;  ///< Idle connections
    std::atomic<size_t> m_createdCount{0};     ///< Total connections created
    std::atomic<size_t> m_waitingCount{0};     ///< Threads blocked on acquire()

    mutable std::mutex m_mutex;       ///< Protects m_available queue
    std::condition_variable m_cv;     ///< Signaled when connection available
    std::atomic<bool> m_shutdown{false};  ///< True if pool is draining
};

}  // namespace sqlfuse
