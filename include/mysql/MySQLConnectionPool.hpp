#pragma once

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

class MySQLConnectionPool : public ConnectionPool {
public:
    explicit MySQLConnectionPool(const ConnectionConfig& config, size_t poolSize = 10);
    ~MySQLConnectionPool() override;

    // Acquire a connection (blocks until available or timeout)
    std::unique_ptr<MySQLConnection> acquire(
        std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));

    // Try to acquire without blocking
    std::unique_ptr<MySQLConnection> tryAcquire();

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

    // MySQL-specific
    size_t waitingCount() const;

private:
    friend class MySQLConnection;

    MYSQL* createConnection();
    void releaseConnection(MYSQL* conn);
    void destroyConnection(MYSQL* conn);
    bool validateConnection(MYSQL* conn);

    ConnectionConfig m_config;
    size_t m_poolSize;

    std::queue<MYSQL*> m_available;
    std::atomic<size_t> m_createdCount{0};
    std::atomic<size_t> m_waitingCount{0};

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::atomic<bool> m_shutdown{false};
};

}  // namespace sqlfuse
