#include "OracleConnectionPool.hpp"
#include "OracleVirtualFile.hpp"
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace sqlfuse {

OracleConnectionPool::OracleConnectionPool(const ConnectionConfig& config, size_t poolSize)
    : m_config(config), m_poolSize(poolSize) {

    if (!initializeEnvironment()) {
        throw std::runtime_error("Failed to initialize Oracle environment");
    }

    // Pre-create connections
    for (size_t i = 0; i < poolSize; ++i) {
        try {
            auto conn = createConnection();
            m_available.push(conn);
            ++m_createdCount;
        } catch (const std::exception& e) {
            spdlog::error("Failed to create Oracle connection {}: {}", i, e.what());
            if (i == 0) {
                throw;  // Can't create even one connection
            }
        }
    }

    spdlog::info("Oracle connection pool created with {} connections", m_createdCount.load());
}

OracleConnectionPool::~OracleConnectionPool() {
    drain();

    if (m_env) {
        OCIHandleFree(m_env, OCI_HTYPE_ENV);
        m_env = nullptr;
    }
}

bool OracleConnectionPool::initializeEnvironment() {
    sword status = OCIEnvCreate(&m_env, OCI_THREADED | OCI_OBJECT, nullptr,
                                nullptr, nullptr, nullptr, 0, nullptr);
    if (status != OCI_SUCCESS) {
        spdlog::error("Failed to create Oracle environment");
        return false;
    }
    return true;
}

std::string OracleConnectionPool::buildConnectString() const {
    // Support various connection string formats:
    // 1. Easy Connect: host:port/service_name
    // 2. TNS name: MYDB
    // 3. Full descriptor: (DESCRIPTION=...)

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

    if (!m_config.default_database.empty()) {
        connStr += "/" + m_config.default_database;
    }

    return connStr;
}

OracleConnectionPool::PooledConnection OracleConnectionPool::createConnection() {
    PooledConnection conn{nullptr, nullptr};

    // Allocate error handle
    sword status = OCIHandleAlloc(m_env, (void**)&conn.err, OCI_HTYPE_ERROR, 0, nullptr);
    if (status != OCI_SUCCESS) {
        throw std::runtime_error("Failed to allocate Oracle error handle");
    }

    // Allocate service context
    OCIServer* server = nullptr;
    OCISession* session = nullptr;

    status = OCIHandleAlloc(m_env, (void**)&server, OCI_HTYPE_SERVER, 0, nullptr);
    if (status != OCI_SUCCESS) {
        OCIHandleFree(conn.err, OCI_HTYPE_ERROR);
        throw std::runtime_error("Failed to allocate Oracle server handle");
    }

    // Attach to server
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

    // Allocate service context handle
    status = OCIHandleAlloc(m_env, (void**)&conn.svc, OCI_HTYPE_SVCCTX, 0, nullptr);
    if (status != OCI_SUCCESS) {
        OCIServerDetach(server, conn.err, OCI_DEFAULT);
        OCIHandleFree(server, OCI_HTYPE_SERVER);
        OCIHandleFree(conn.err, OCI_HTYPE_ERROR);
        throw std::runtime_error("Failed to allocate Oracle service context");
    }

    // Set server in service context
    OCIAttrSet(conn.svc, OCI_HTYPE_SVCCTX, server, 0, OCI_ATTR_SERVER, conn.err);

    // Allocate session handle
    status = OCIHandleAlloc(m_env, (void**)&session, OCI_HTYPE_SESSION, 0, nullptr);
    if (status != OCI_SUCCESS) {
        OCIServerDetach(server, conn.err, OCI_DEFAULT);
        OCIHandleFree(conn.svc, OCI_HTYPE_SVCCTX);
        OCIHandleFree(server, OCI_HTYPE_SERVER);
        OCIHandleFree(conn.err, OCI_HTYPE_ERROR);
        throw std::runtime_error("Failed to allocate Oracle session handle");
    }

    // Set username and password
    OCIAttrSet(session, OCI_HTYPE_SESSION,
               (void*)m_config.user.c_str(), m_config.user.length(),
               OCI_ATTR_USERNAME, conn.err);
    OCIAttrSet(session, OCI_HTYPE_SESSION,
               (void*)m_config.password.c_str(), m_config.password.length(),
               OCI_ATTR_PASSWORD, conn.err);

    // Begin session
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

    // Set session in service context
    OCIAttrSet(conn.svc, OCI_HTYPE_SVCCTX, session, 0, OCI_ATTR_SESSION, conn.err);

    return conn;
}

void OracleConnectionPool::destroyConnection(OCISvcCtx* svc, OCIError* err) {
    if (svc) {
        // Get server and session handles
        OCIServer* server = nullptr;
        OCISession* session = nullptr;

        OCIAttrGet(svc, OCI_HTYPE_SVCCTX, &session, nullptr, OCI_ATTR_SESSION, err);
        OCIAttrGet(svc, OCI_HTYPE_SVCCTX, &server, nullptr, OCI_ATTR_SERVER, err);

        // End session
        if (session) {
            OCISessionEnd(svc, err, session, OCI_DEFAULT);
            OCIHandleFree(session, OCI_HTYPE_SESSION);
        }

        // Detach from server
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

bool OracleConnectionPool::validateConnection(OCISvcCtx* svc, OCIError* err) {
    if (!svc || !err) return false;

    sword status = OCIPing(svc, err, OCI_DEFAULT);
    return status == OCI_SUCCESS;
}

std::unique_ptr<OracleConnection> OracleConnectionPool::acquire(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(m_mutex);
    ++m_waitingCount;

    auto deadline = std::chrono::steady_clock::now() + timeout;

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

    // Validate connection
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

std::unique_ptr<OracleConnection> OracleConnectionPool::tryAcquire() {
    std::unique_lock<std::mutex> lock(m_mutex);

    if (m_available.empty() || m_shutdown) {
        return nullptr;
    }

    auto conn = m_available.front();
    m_available.pop();

    return std::make_unique<OracleConnection>(this, m_env, conn.svc, conn.err);
}

void OracleConnectionPool::releaseConnection(OCISvcCtx* svc, OCIError* err) {
    std::unique_lock<std::mutex> lock(m_mutex);

    if (m_shutdown) {
        destroyConnection(svc, err);
        --m_createdCount;
    } else {
        m_available.push({svc, err});
        m_cv.notify_one();
    }
}

size_t OracleConnectionPool::availableCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_available.size();
}

size_t OracleConnectionPool::totalCount() const {
    return m_createdCount.load();
}

size_t OracleConnectionPool::waitingCount() const {
    return m_waitingCount.load();
}

bool OracleConnectionPool::healthCheck() {
    auto conn = tryAcquire();
    if (!conn) return false;

    bool healthy = conn->ping();
    return healthy;
}

void OracleConnectionPool::drain() {
    m_shutdown = true;
    m_cv.notify_all();

    std::unique_lock<std::mutex> lock(m_mutex);

    while (!m_available.empty()) {
        auto conn = m_available.front();
        m_available.pop();
        destroyConnection(conn.svc, conn.err);
        --m_createdCount;
    }
}

std::unique_ptr<VirtualFile> OracleConnectionPool::createVirtualFile(
    const ParsedPath& path,
    SchemaManager& schema,
    CacheManager& cache,
    const DataConfig& config) {
    return std::make_unique<OracleVirtualFile>(path, schema, cache, config);
}

}  // namespace sqlfuse
