/**
 * @file OracleConnectionPool.cpp
 * @brief Implementation of the thread-safe Oracle connection pool.
 *
 * This file implements connection pooling for Oracle databases using OCI
 * (Oracle Call Interface). The pool manages a set of pre-established
 * connections that can be reused across multiple operations, reducing
 * the overhead of connection establishment.
 *
 * OCI Handle Hierarchy:
 * - OCIEnv: Environment handle (one per pool, shared by all connections)
 * - OCISvcCtx: Service context (one per connection, wraps server+session)
 * - OCIServer: Server handle (represents the database server attachment)
 * - OCISession: Session handle (represents the authenticated user session)
 * - OCIError: Error handle (one per connection for thread safety)
 */

#include "OracleConnectionPool.hpp"
#include "OracleVirtualFile.hpp"
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace sqlfuse {

// =============================================================================
// Constructor and Destructor
// =============================================================================

/**
 * Create a new connection pool with the specified configuration.
 *
 * Initializes the OCI environment and pre-creates the specified number
 * of connections. If any connections fail to create (except the first),
 * the pool continues with reduced capacity.
 *
 * @throws std::runtime_error if OCI environment fails to initialize
 *         or if no connections can be created
 */
OracleConnectionPool::OracleConnectionPool(const ConnectionConfig& config, size_t poolSize)
    : m_config(config), m_poolSize(poolSize) {

    // Initialize the OCI environment (required before any other OCI calls)
    if (!initializeEnvironment()) {
        throw std::runtime_error("Failed to initialize Oracle environment");
    }

    // Pre-create all connections for the pool
    for (size_t i = 0; i < poolSize; ++i) {
        try {
            auto conn = createConnection();
            m_available.push(conn);
            ++m_createdCount;
        } catch (const std::exception& e) {
            spdlog::error("Failed to create Oracle connection {}: {}", i, e.what());
            if (i == 0) {
                throw;  // Can't create even one connection - fatal error
            }
            // Continue with reduced pool size for subsequent failures
        }
    }

    spdlog::info("Oracle connection pool created with {} connections", m_createdCount.load());
}

/**
 * Destructor - drain all connections and free the OCI environment.
 */
OracleConnectionPool::~OracleConnectionPool() {
    drain();

    if (m_env) {
        OCIHandleFree(m_env, OCI_HTYPE_ENV);
        m_env = nullptr;
    }
}

// =============================================================================
// Environment Initialization
// =============================================================================

/**
 * Initialize the OCI environment handle.
 *
 * The environment is created with OCI_THREADED for multi-threaded safety
 * and OCI_OBJECT for object type support (not currently used but available).
 *
 * @return true if environment was created successfully
 */
bool OracleConnectionPool::initializeEnvironment() {
    sword status = OCIEnvCreate(&m_env, OCI_THREADED | OCI_OBJECT, nullptr,
                                nullptr, nullptr, nullptr, 0, nullptr);
    if (status != OCI_SUCCESS) {
        spdlog::error("Failed to create Oracle environment");
        return false;
    }
    return true;
}

/**
 * Build an Oracle connection string from the configuration.
 *
 * Supports three formats:
 * 1. Easy Connect: "host:port/service_name" - built from config components
 * 2. TNS Name: "MYDB" - used directly (requires tnsnames.ora)
 * 3. Full Descriptor: "(DESCRIPTION=...)" - used directly
 *
 * If the host already contains ':', '/', or "DESCRIPTION", it's assumed
 * to be a complete connection string and is used as-is.
 */
std::string OracleConnectionPool::buildConnectString() const {
    std::string connStr = m_config.host;

    // If host already contains a full connection string, use it directly
    if (connStr.find("DESCRIPTION") != std::string::npos ||
        connStr.find('/') != std::string::npos ||
        connStr.find(':') != std::string::npos) {
        return connStr;
    }

    // Build Easy Connect string: host:port/service_name
    if (m_config.port > 0) {
        connStr += ":" + std::to_string(m_config.port);
    } else {
        connStr += ":1521";  // Default Oracle port
    }

    // Append service name from default_database if provided
    if (!m_config.default_database.empty()) {
        connStr += "/" + m_config.default_database;
    }

    return connStr;
}

// =============================================================================
// Connection Lifecycle Management
// =============================================================================

/**
 * Create a new Oracle connection with full handle setup.
 *
 * The connection creation process involves multiple OCI calls:
 * 1. Allocate error handle for this connection
 * 2. Allocate server handle
 * 3. Attach to the database server
 * 4. Allocate service context to wrap the connection
 * 5. Allocate session handle for authentication
 * 6. Set username and password credentials
 * 7. Begin the authenticated session
 * 8. Link the session to the service context
 *
 * On any failure, all previously allocated handles are freed before throwing.
 *
 * @return PooledConnection with initialized svc and err handles
 * @throws std::runtime_error on any OCI failure
 */
OracleConnectionPool::PooledConnection OracleConnectionPool::createConnection() {
    PooledConnection conn{nullptr, nullptr};

    // Step 1: Allocate per-connection error handle
    sword status = OCIHandleAlloc(m_env, (void**)&conn.err, OCI_HTYPE_ERROR, 0, nullptr);
    if (status != OCI_SUCCESS) {
        throw std::runtime_error("Failed to allocate Oracle error handle");
    }

    // Step 2: Allocate server handle
    OCIServer* server = nullptr;
    OCISession* session = nullptr;

    status = OCIHandleAlloc(m_env, (void**)&server, OCI_HTYPE_SERVER, 0, nullptr);
    if (status != OCI_SUCCESS) {
        OCIHandleFree(conn.err, OCI_HTYPE_ERROR);
        throw std::runtime_error("Failed to allocate Oracle server handle");
    }

    // Step 3: Attach to the database server using connection string
    std::string connStr = buildConnectString();
    status = OCIServerAttach(server, conn.err, (const OraText*)connStr.c_str(),
                            connStr.length(), OCI_DEFAULT);
    if (status != OCI_SUCCESS) {
        sb4 errCode = 0;
        char errBuf[512];
        OCIErrorGet(conn.err, 1, nullptr, &errCode, (OraText*)errBuf, sizeof(errBuf), OCI_HTYPE_ERROR);
        OCIHandleFree(server, OCI_HTYPE_SERVER);
        OCIHandleFree(conn.err, OCI_HTYPE_ERROR);
        throw std::runtime_error("Failed to attach to Oracle server: " + std::string(errBuf));
    }

    // Step 4: Allocate service context handle (wraps server + session)
    status = OCIHandleAlloc(m_env, (void**)&conn.svc, OCI_HTYPE_SVCCTX, 0, nullptr);
    if (status != OCI_SUCCESS) {
        OCIServerDetach(server, conn.err, OCI_DEFAULT);
        OCIHandleFree(server, OCI_HTYPE_SERVER);
        OCIHandleFree(conn.err, OCI_HTYPE_ERROR);
        throw std::runtime_error("Failed to allocate Oracle service context");
    }

    // Link server to service context
    OCIAttrSet(conn.svc, OCI_HTYPE_SVCCTX, server, 0, OCI_ATTR_SERVER, conn.err);

    // Step 5: Allocate session handle for authentication
    status = OCIHandleAlloc(m_env, (void**)&session, OCI_HTYPE_SESSION, 0, nullptr);
    if (status != OCI_SUCCESS) {
        OCIServerDetach(server, conn.err, OCI_DEFAULT);
        OCIHandleFree(conn.svc, OCI_HTYPE_SVCCTX);
        OCIHandleFree(server, OCI_HTYPE_SERVER);
        OCIHandleFree(conn.err, OCI_HTYPE_ERROR);
        throw std::runtime_error("Failed to allocate Oracle session handle");
    }

    // Step 6: Set authentication credentials
    OCIAttrSet(session, OCI_HTYPE_SESSION,
               (void*)m_config.user.c_str(), m_config.user.length(),
               OCI_ATTR_USERNAME, conn.err);
    OCIAttrSet(session, OCI_HTYPE_SESSION,
               (void*)m_config.password.c_str(), m_config.password.length(),
               OCI_ATTR_PASSWORD, conn.err);

    // Step 7: Begin the authenticated session
    status = OCISessionBegin(conn.svc, conn.err, session, OCI_CRED_RDBMS, OCI_DEFAULT);
    if (status != OCI_SUCCESS) {
        sb4 errCode = 0;
        char errBuf[512];
        OCIErrorGet(conn.err, 1, nullptr, &errCode, (OraText*)errBuf, sizeof(errBuf), OCI_HTYPE_ERROR);
        OCIHandleFree(session, OCI_HTYPE_SESSION);
        OCIServerDetach(server, conn.err, OCI_DEFAULT);
        OCIHandleFree(conn.svc, OCI_HTYPE_SVCCTX);
        OCIHandleFree(server, OCI_HTYPE_SERVER);
        OCIHandleFree(conn.err, OCI_HTYPE_ERROR);
        throw std::runtime_error("Failed to begin Oracle session: " + std::string(errBuf));
    }

    // Step 8: Link session to service context
    OCIAttrSet(conn.svc, OCI_HTYPE_SVCCTX, session, 0, OCI_ATTR_SESSION, conn.err);

    return conn;
}

/**
 * Properly close and free all handles associated with a connection.
 *
 * Cleanup is performed in reverse order of creation:
 * 1. End the session (OCISessionEnd)
 * 2. Free the session handle
 * 3. Detach from the server
 * 4. Free the server handle
 * 5. Free the service context handle
 * 6. Free the error handle
 */
void OracleConnectionPool::destroyConnection(OCISvcCtx* svc, OCIError* err) {
    if (svc) {
        // Extract embedded handles from service context
        OCIServer* server = nullptr;
        OCISession* session = nullptr;

        OCIAttrGet(svc, OCI_HTYPE_SVCCTX, &session, nullptr, OCI_ATTR_SESSION, err);
        OCIAttrGet(svc, OCI_HTYPE_SVCCTX, &server, nullptr, OCI_ATTR_SERVER, err);

        // End the authenticated session
        if (session) {
            OCISessionEnd(svc, err, session, OCI_DEFAULT);
            OCIHandleFree(session, OCI_HTYPE_SESSION);
        }

        // Detach from the database server
        if (server) {
            OCIServerDetach(server, err, OCI_DEFAULT);
            OCIHandleFree(server, OCI_HTYPE_SERVER);
        }

        OCIHandleFree(svc, OCI_HTYPE_SVCCTX);
    }

    if (err) {
        OCIHandleFree(err, OCI_HTYPE_ERROR);
    }
}

/**
 * Validate that a connection is still alive using OCIPing.
 * This is more efficient than executing "SELECT 1 FROM dual".
 */
bool OracleConnectionPool::validateConnection(OCISvcCtx* svc, OCIError* err) {
    if (!svc || !err) return false;

    sword status = OCIPing(svc, err, OCI_DEFAULT);
    return status == OCI_SUCCESS;
}

// =============================================================================
// Connection Acquisition
// =============================================================================

/**
 * Acquire a connection from the pool, blocking if necessary.
 *
 * If no connections are available, blocks until one becomes available
 * or the timeout expires. Before returning a connection, validates it
 * using OCIPing. If validation fails, the stale connection is destroyed
 * and a new one is created.
 *
 * Thread Safety: Protected by m_mutex and m_cv condition variable.
 */
std::unique_ptr<OracleConnection> OracleConnectionPool::acquire(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(m_mutex);
    ++m_waitingCount;

    auto deadline = std::chrono::steady_clock::now() + timeout;

    // Wait for a connection to become available
    while (m_available.empty() && !m_shutdown) {
        if (m_cv.wait_until(lock, deadline) == std::cv_status::timeout) {
            --m_waitingCount;
            spdlog::warn("Oracle connection pool timeout after {}ms", timeout.count());
            return nullptr;
        }
    }

    --m_waitingCount;

    if (m_shutdown || m_available.empty()) {
        return nullptr;
    }

    auto conn = m_available.front();
    m_available.pop();

    // Validate connection before returning (outside the lock would be better
    // but requires restructuring - keeping simple for now)
    if (!validateConnection(conn.svc, conn.err)) {
        spdlog::warn("Oracle connection validation failed, destroying and creating new");
        destroyConnection(conn.svc, conn.err);
        --m_createdCount;

        try {
            conn = createConnection();
            ++m_createdCount;
        } catch (const std::exception& e) {
            spdlog::error("Failed to recreate Oracle connection: {}", e.what());
            return nullptr;
        }
    }

    return std::make_unique<OracleConnection>(this, m_env, conn.svc, conn.err);
}

/**
 * Try to acquire a connection without blocking.
 * Returns nullptr immediately if no connections are available.
 * Does not validate the connection (caller should handle stale connections).
 */
std::unique_ptr<OracleConnection> OracleConnectionPool::tryAcquire() {
    std::unique_lock<std::mutex> lock(m_mutex);

    if (m_available.empty() || m_shutdown) {
        return nullptr;
    }

    auto conn = m_available.front();
    m_available.pop();

    return std::make_unique<OracleConnection>(this, m_env, conn.svc, conn.err);
}

/**
 * Return a connection to the pool.
 *
 * Called by OracleConnection destructor. If the pool is shutting down,
 * the connection is destroyed instead of being returned. Otherwise, it's
 * added back to the available queue and waiting threads are notified.
 */
void OracleConnectionPool::releaseConnection(OCISvcCtx* svc, OCIError* err) {
    std::unique_lock<std::mutex> lock(m_mutex);

    if (m_shutdown) {
        // Pool is draining - destroy instead of returning
        destroyConnection(svc, err);
        --m_createdCount;
    } else {
        // Return to pool and notify waiting threads
        m_available.push({svc, err});
        m_cv.notify_one();
    }
}

// =============================================================================
// Pool Statistics
// =============================================================================

/**
 * Get the number of idle connections currently available.
 */
size_t OracleConnectionPool::availableCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_available.size();
}

/**
 * Get the total number of connections managed by the pool.
 */
size_t OracleConnectionPool::totalCount() const {
    return m_createdCount.load();
}

/**
 * Get the number of threads currently waiting for a connection.
 */
size_t OracleConnectionPool::waitingCount() const {
    return m_waitingCount.load();
}

/**
 * Check pool health by acquiring and pinging a connection.
 */
bool OracleConnectionPool::healthCheck() {
    auto conn = tryAcquire();
    if (!conn) return false;

    bool healthy = conn->ping();
    return healthy;  // Connection automatically returned to pool when destroyed
}

// =============================================================================
// Pool Shutdown
// =============================================================================

/**
 * Drain all connections from the pool.
 *
 * Sets the shutdown flag to prevent new acquisitions, wakes up all
 * waiting threads, and destroys all available connections. Connections
 * currently in use will be destroyed when released.
 */
void OracleConnectionPool::drain() {
    m_shutdown = true;
    m_cv.notify_all();  // Wake up any waiting threads

    std::unique_lock<std::mutex> lock(m_mutex);

    // Destroy all available connections
    while (!m_available.empty()) {
        auto conn = m_available.front();
        m_available.pop();
        destroyConnection(conn.svc, conn.err);
        --m_createdCount;
    }
}

// =============================================================================
// Factory Method
// =============================================================================

/**
 * Create an Oracle-specific virtual file handler.
 * Implements the ConnectionPool interface for the FUSE filesystem.
 */
std::unique_ptr<VirtualFile> OracleConnectionPool::createVirtualFile(
    const ParsedPath& path,
    SchemaManager& schema,
    CacheManager& cache,
    const DataConfig& config) {
    return std::make_unique<OracleVirtualFile>(path, schema, cache, config);
}

}  // namespace sqlfuse
