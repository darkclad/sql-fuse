#pragma once

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

class PostgreSQLConnectionPool : public ConnectionPool {
public:
    explicit PostgreSQLConnectionPool(const ConnectionConfig& config, size_t poolSize = 10);
    ~PostgreSQLConnectionPool() override;

    // Acquire a connection (blocks until available or timeout)
    std::unique_ptr<PostgreSQLConnection> acquire(
        std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));

    // Try to acquire without blocking
    std::unique_ptr<PostgreSQLConnection> tryAcquire();

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

    // PostgreSQL-specific
    size_t waitingCount() const;

private:
    friend class PostgreSQLConnection;

    PGconn* createConnection();
    void releaseConnection(PGconn* conn);
    void destroyConnection(PGconn* conn);
    bool validateConnection(PGconn* conn);

    ConnectionConfig m_config;
    size_t m_poolSize;

    std::queue<PGconn*> m_available;
    std::atomic<size_t> m_createdCount{0};
    std::atomic<size_t> m_waitingCount{0};

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::atomic<bool> m_shutdown{false};
};

}  // namespace sqlfuse
