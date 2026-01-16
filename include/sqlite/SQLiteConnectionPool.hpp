#pragma once

/**
 * @file SQLiteConnectionPool.hpp
 * @brief Simple connection pool for SQLite database connections.
 *
 * This file implements a connection pool for SQLite databases. While SQLite
 * doesn't strictly require connection pooling (since it's a file-based
 * database), pooling can still help by reusing prepared statements and
 * avoiding repeated file opens.
 */

#include "ConnectionPool.hpp"
#include "SQLiteConnection.hpp"
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>

namespace sqlfuse {

/**
 * @class SQLiteConnectionPool
 * @brief Simple pool of SQLite database connections.
 *
 * Unlike server-based databases, SQLite doesn't have a server to connect to.
 * Each "connection" is simply an open file handle. This pool manages multiple
 * open handles to the same database file, which can be useful for:
 * - Concurrent read operations (SQLite supports multiple readers)
 * - Reusing prepared statement caches
 * - Consistent interface with other database backends
 *
 * SQLite Concurrency Notes:
 * - Multiple connections can read simultaneously
 * - Only one connection can write at a time (database-level locking)
 * - WAL mode allows concurrent reads during writes
 *
 * @see SQLiteConnection for individual connection usage
 */
class SQLiteConnectionPool : public ConnectionPool {
public:
    /**
     * @brief Create a new SQLite connection pool.
     * @param dbPath Path to the SQLite database file.
     * @param poolSize Number of connections to maintain (default: 5).
     *
     * Creates the database file if it doesn't exist.
     * For SQLite, a smaller pool size is often sufficient since
     * connections are just file handles.
     */
    explicit SQLiteConnectionPool(const std::string& dbPath, size_t poolSize = 5);

    /**
     * @brief Destructor - closes all connections.
     */
    ~SQLiteConnectionPool() override;

    /**
     * @brief Acquire a connection from the pool.
     * @return unique_ptr to SQLiteConnection, or nullptr if pool is exhausted.
     *
     * Note: Unlike MySQL/PostgreSQL pools, this doesn't block - it creates
     * a new connection if the pool is empty (up to poolSize limit).
     */
    std::unique_ptr<SQLiteConnection> acquire();

    /**
     * @brief Return a connection to the pool.
     * @param conn Connection to return.
     *
     * The connection is added back to the available pool for reuse.
     */
    void release(std::unique_ptr<SQLiteConnection> conn);

    // ----- ConnectionPool interface implementation -----

    /**
     * @brief Get the number of currently available connections.
     * @return Number of connections ready to be acquired.
     */
    size_t availableCount() const override;

    /**
     * @brief Get the total number of connections created.
     * @return Total connection count.
     */
    size_t totalCount() const override;

    /**
     * @brief Check if the pool can provide working connections.
     * @return true if the database file is accessible.
     */
    bool healthCheck() override;

    /**
     * @brief Close all connections in the pool.
     */
    void drain() override;

    /**
     * @brief Create a VirtualFile for the SQLite backend.
     * @param path Parsed path identifying the database object.
     * @param schema Schema manager for metadata queries.
     * @param cache Cache manager for content caching.
     * @param config Data configuration options.
     * @return unique_ptr to SQLiteVirtualFile.
     */
    std::unique_ptr<VirtualFile> createVirtualFile(
        const ParsedPath& path,
        SchemaManager& schema,
        CacheManager& cache,
        const DataConfig& config) override;

private:
    std::string m_dbPath;         ///< Path to SQLite database file
    size_t m_poolSize;            ///< Maximum pool size
    std::vector<std::unique_ptr<SQLiteConnection>> m_available;  ///< Idle connections
    std::atomic<size_t> m_createdCount{0};   ///< Total connections created
    mutable std::mutex m_mutex;   ///< Protects m_available
};

}  // namespace sqlfuse
