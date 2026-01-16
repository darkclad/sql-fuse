#pragma once

#include "ConnectionPool.hpp"
#include "SQLiteConnection.hpp"
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>

namespace sqlfuse {

class SQLiteConnectionPool : public ConnectionPool {
public:
    explicit SQLiteConnectionPool(const std::string& dbPath, size_t poolSize = 5);
    ~SQLiteConnectionPool() override;

    std::unique_ptr<SQLiteConnection> acquire();
    void release(std::unique_ptr<SQLiteConnection> conn);

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

private:
    std::string m_dbPath;
    size_t m_poolSize;
    std::vector<std::unique_ptr<SQLiteConnection>> m_available;
    std::atomic<size_t> m_createdCount{0};
    mutable std::mutex m_mutex;
};

}  // namespace sqlfuse
