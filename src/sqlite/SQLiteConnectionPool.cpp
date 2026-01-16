#include "SQLiteConnectionPool.hpp"
#include "SQLiteVirtualFile.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace sqlfuse {

SQLiteConnectionPool::SQLiteConnectionPool(const std::string& dbPath, size_t poolSize)
    : m_dbPath(dbPath), m_poolSize(poolSize) {
    // Pre-create some connections
    for (size_t i = 0; i < std::min(poolSize, size_t(3)); ++i) {
        auto conn = std::make_unique<SQLiteConnection>(dbPath);
        if (conn->isValid()) {
            m_available.push_back(std::move(conn));
            ++m_createdCount;
        }
    }
    spdlog::info("SQLite connection pool initialized with {} connections", m_available.size());
}

SQLiteConnectionPool::~SQLiteConnectionPool() {
    drain();
}

std::unique_ptr<SQLiteConnection> SQLiteConnectionPool::acquire() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_available.empty()) {
        auto conn = std::move(m_available.back());
        m_available.pop_back();
        return conn;
    }

    // Create new connection if under limit
    if (m_createdCount < m_poolSize) {
        auto conn = std::make_unique<SQLiteConnection>(m_dbPath);
        if (conn->isValid()) {
            ++m_createdCount;
            return conn;
        }
    }

    // Pool exhausted, create anyway (SQLite handles this via locking)
    return std::make_unique<SQLiteConnection>(m_dbPath);
}

void SQLiteConnectionPool::release(std::unique_ptr<SQLiteConnection> conn) {
    if (!conn || !conn->isValid()) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_available.size() < m_poolSize) {
        m_available.push_back(std::move(conn));
    }
    // Otherwise let it be destroyed
}

bool SQLiteConnectionPool::healthCheck() {
    auto conn = acquire();
    if (!conn || !conn->isValid()) return false;

    sqlite3_stmt* stmt = conn->prepare("SELECT 1");
    if (!stmt) return false;

    bool ok = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);

    release(std::move(conn));
    return ok;
}

void SQLiteConnectionPool::drain() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_available.clear();
    m_createdCount = 0;
    spdlog::info("SQLite connection pool drained");
}

size_t SQLiteConnectionPool::availableCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_available.size();
}

size_t SQLiteConnectionPool::totalCount() const {
    return m_createdCount;
}

std::unique_ptr<VirtualFile> SQLiteConnectionPool::createVirtualFile(
    const ParsedPath& path,
    SchemaManager& schema,
    CacheManager& cache,
    const DataConfig& config) {
    return std::make_unique<SQLiteVirtualFile>(path, schema, cache, config);
}

}  // namespace sqlfuse
