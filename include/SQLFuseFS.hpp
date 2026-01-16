#pragma once

#define FUSE_USE_VERSION 35

#include "Config.hpp"
#include "PathRouter.hpp"
#include "CacheManager.hpp"
#include "SchemaManager.hpp"
#include "VirtualFile.hpp"
#include "VirtualFileHandleManager.hpp"

#ifdef WITH_MYSQL
#include "MySQLConnectionPool.hpp"
#endif

#ifdef WITH_SQLITE
#include "SQLiteConnectionPool.hpp"
#include "SQLiteSchemaManager.hpp"
#endif

#ifdef WITH_POSTGRESQL
#include "PostgreSQLConnectionPool.hpp"
#include "PostgreSQLSchemaManager.hpp"
#endif

#ifdef WITH_ORACLE
#include "OracleConnectionPool.hpp"
#include "OracleSchemaManager.hpp"
#endif

#include <fuse3/fuse.h>
#include <memory>
#include <string>
#include <variant>

namespace sqlfuse {

class SQLFuseFS {
public:
    // Singleton access
    static SQLFuseFS& instance();

    // Initialize with configuration
    int init(const Config& config);

    // Run the filesystem (blocks until unmounted)
    int run(int argc, char* argv[]);

    // Shutdown
    void shutdown();

    // Get configuration
    const Config& config() const { return m_config; }

    // FUSE operation handlers
    int getattr(const char* path, struct stat* stbuf, fuse_file_info* fi);
    int readdir(const char* path, void* buf, fuse_fill_dir_t filler,
                off_t offset, fuse_file_info* fi, fuse_readdir_flags flags);
    int open(const char* path, fuse_file_info* fi);
    int read(const char* path, char* buf, size_t size, off_t offset,
             fuse_file_info* fi);
    int write(const char* path, const char* buf, size_t size, off_t offset,
              fuse_file_info* fi);
    int create(const char* path, mode_t mode, fuse_file_info* fi);
    int unlink(const char* path);
    int truncate(const char* path, off_t size, fuse_file_info* fi);
    int release(const char* path, fuse_file_info* fi);
    int flush(const char* path, fuse_file_info* fi);
    int statfs(const char* path, struct statvfs* stbuf);
    int utimens(const char* path, const struct timespec tv[2], fuse_file_info* fi);

    // Access components
#ifdef WITH_MYSQL
    MySQLConnectionPool* mysqlConnectionPool() {
        if (auto* pool = std::get_if<std::unique_ptr<MySQLConnectionPool>>(&m_pool)) {
            return pool->get();
        }
        return nullptr;
    }
#endif
#ifdef WITH_SQLITE
    SQLiteConnectionPool* sqliteConnectionPool() {
        if (auto* pool = std::get_if<std::unique_ptr<SQLiteConnectionPool>>(&m_pool)) {
            return pool->get();
        }
        return nullptr;
    }
#endif
#ifdef WITH_POSTGRESQL
    PostgreSQLConnectionPool* postgresqlConnectionPool() {
        if (auto* pool = std::get_if<std::unique_ptr<PostgreSQLConnectionPool>>(&m_pool)) {
            return pool->get();
        }
        return nullptr;
    }
#endif
#ifdef WITH_ORACLE
    OracleConnectionPool* oracleConnectionPool() {
        if (auto* pool = std::get_if<std::unique_ptr<OracleConnectionPool>>(&m_pool)) {
            return pool->get();
        }
        return nullptr;
    }
#endif
    SchemaManager* schemaManager() { return m_schema.get(); }
    CacheManager* cacheManager() { return m_cache.get(); }
    PathRouter* pathRouter() { return &m_router; }

private:
    SQLFuseFS() = default;
    ~SQLFuseFS() = default;

    // Non-copyable
    SQLFuseFS(const SQLFuseFS&) = delete;
    SQLFuseFS& operator=(const SQLFuseFS&) = delete;

    // Fill directory entries based on node type
    int fillRootDir(void* buf, fuse_fill_dir_t filler);
    int fillDatabaseDir(void* buf, fuse_fill_dir_t filler, const std::string& database);
    int fillTablesDir(void* buf, fuse_fill_dir_t filler, const std::string& database);
    int fillTableDir(void* buf, fuse_fill_dir_t filler,
                     const std::string& database, const std::string& table);
    int fillRowsDir(void* buf, fuse_fill_dir_t filler,
                    const std::string& database, const std::string& table);
    int fillViewsDir(void* buf, fuse_fill_dir_t filler, const std::string& database);
    int fillProceduresDir(void* buf, fuse_fill_dir_t filler, const std::string& database);
    int fillFunctionsDir(void* buf, fuse_fill_dir_t filler, const std::string& database);
    int fillTriggersDir(void* buf, fuse_fill_dir_t filler, const std::string& database);
    int fillUsersDir(void* buf, fuse_fill_dir_t filler);
    int fillVariablesDir(void* buf, fuse_fill_dir_t filler, const std::string& scope);

    // Get file attributes based on node type
    int fillStatForNode(const ParsedPath& parsed, struct stat* stbuf);

    // Check if database is allowed
    bool isDatabaseAllowed(const std::string& database) const;

    Config m_config;
    DatabaseType m_dbType = DatabaseType::MySQL;
    PathRouter m_router;

    // Members ordered by dependency (dependencies first, destroyed last)
    std::unique_ptr<CacheManager> m_cache;

    // Connection pool variant to support different database backends
    std::variant<
        std::monostate
#ifdef WITH_MYSQL
        , std::unique_ptr<MySQLConnectionPool>
#endif
#ifdef WITH_SQLITE
        , std::unique_ptr<SQLiteConnectionPool>
#endif
#ifdef WITH_POSTGRESQL
        , std::unique_ptr<PostgreSQLConnectionPool>
#endif
#ifdef WITH_ORACLE
        , std::unique_ptr<OracleConnectionPool>
#endif
    > m_pool;

    std::unique_ptr<SchemaManager> m_schema;          // Depends on pool & cache
    std::unique_ptr<VirtualFileHandleManager> m_fileHandles;  // Depends on schema & cache

    bool m_initialized = false;
};

// Static C-style callbacks for FUSE
extern "C" {
    int sql_fuse_getattr(const char* path, struct stat* stbuf, fuse_file_info* fi);
    int sql_fuse_readdir(const char* path, void* buf, fuse_fill_dir_t filler,
                           off_t offset, fuse_file_info* fi, fuse_readdir_flags flags);
    int sql_fuse_open(const char* path, fuse_file_info* fi);
    int sql_fuse_read(const char* path, char* buf, size_t size, off_t offset,
                        fuse_file_info* fi);
    int sql_fuse_write(const char* path, const char* buf, size_t size, off_t offset,
                         fuse_file_info* fi);
    int sql_fuse_create(const char* path, mode_t mode, fuse_file_info* fi);
    int sql_fuse_unlink(const char* path);
    int sql_fuse_truncate(const char* path, off_t size, fuse_file_info* fi);
    int sql_fuse_release(const char* path, fuse_file_info* fi);
    int sql_fuse_flush(const char* path, fuse_file_info* fi);
    int sql_fuse_statfs(const char* path, struct statvfs* stbuf);
    int sql_fuse_utimens(const char* path, const struct timespec tv[2],
                           fuse_file_info* fi);
    void* sql_fuse_init(fuse_conn_info* conn, fuse_config* cfg);
    void sql_fuse_destroy(void* private_data);
}

// Get FUSE operations structure
const fuse_operations& getFuseOperations();

}  // namespace sqlfuse
