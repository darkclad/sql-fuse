#pragma once

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

class OracleConnectionPool : public ConnectionPool {
public:
    explicit OracleConnectionPool(const ConnectionConfig& config, size_t poolSize = 10);
    ~OracleConnectionPool() override;

    // Acquire a connection (blocks until available or timeout)
    std::unique_ptr<OracleConnection> acquire(
        std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));

    // Try to acquire without blocking
    std::unique_ptr<OracleConnection> tryAcquire();

    // ConnectionPool interface
    size_t availableCount() const override;
    size_t totalCount() const override;
    bool healthCheck() override;
    void drain() override;
    std::unique_ptr<VirtualFile> createVirtualFile(
        const ParsedPath& path,
        SchemaManager& schema,
        CacheManager& cache,
        const DataConfig& config) override;

    // Oracle-specific
    size_t waitingCount() const;
    OCIEnv* getEnv() const { return m_env; }

private:
    friend class OracleConnection;

    struct PooledConnection {
        OCISvcCtx* svc;
        OCIError* err;
    };

    bool initializeEnvironment();
    PooledConnection createConnection();
    void releaseConnection(OCISvcCtx* svc, OCIError* err);
    void destroyConnection(OCISvcCtx* svc, OCIError* err);
    bool validateConnection(OCISvcCtx* svc, OCIError* err);

    // Build connection string from config
    std::string buildConnectString() const;

    ConnectionConfig m_config;
    size_t m_poolSize;

    OCIEnv* m_env = nullptr;

    std::queue<PooledConnection> m_available;
    std::atomic<size_t> m_createdCount{0};
    std::atomic<size_t> m_waitingCount{0};

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::atomic<bool> m_shutdown{false};
};

}  // namespace sqlfuse
