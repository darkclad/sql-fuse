#include "SQLFuseFS.hpp"
#include "ErrorHandler.hpp"
#include "FormatConverter.hpp"
#include <spdlog/spdlog.h>
#include <cstring>
#include <ctime>

#ifdef WITH_MYSQL
#include "MySQLSchemaManager.hpp"
#include "MySQLFormatConverter.hpp"
#endif

#ifdef WITH_SQLITE
#include "SQLiteSchemaManager.hpp"
#include "SQLiteFormatConverter.hpp"
#endif

#ifdef WITH_POSTGRESQL
#include "PostgreSQLSchemaManager.hpp"
#include "PostgreSQLFormatConverter.hpp"
#include "PostgreSQLResultSet.hpp"
#endif

#ifdef WITH_ORACLE
#include "OracleSchemaManager.hpp"
#include "OracleFormatConverter.hpp"
#include "OracleResultSet.hpp"
#endif

namespace sqlfuse {

// Singleton instance
SQLFuseFS& SQLFuseFS::instance() {
    static SQLFuseFS instance;
    return instance;
}

int SQLFuseFS::init(const Config& config) {
    m_config = config;

    try {
        // Parse database type
        m_dbType = parseDatabaseType(m_config.database_type);
        spdlog::info("Database type: {}", databaseTypeToString(m_dbType));

        // Create cache manager
        m_cache = std::make_unique<CacheManager>(m_config.cache);

        // Create connection pool and schema manager based on database type
        switch (m_dbType) {
#ifdef WITH_MYSQL
            case DatabaseType::MySQL: {
                auto pool = std::make_unique<MySQLConnectionPool>(
                    m_config.connection,
                    m_config.performance.connection_pool_size);

                if (!pool->healthCheck()) {
                    spdlog::error("Failed to connect to MySQL server");
                    return -1;
                }

                m_pool = std::move(pool);

                m_schema = std::make_unique<MySQLSchemaManager>(
                    *std::get<std::unique_ptr<MySQLConnectionPool>>(m_pool), *m_cache);

                // Create file handle manager (uses schema manager for pool access)
                m_fileHandles = std::make_unique<VirtualFileHandleManager>(
                    *m_schema, *m_cache, m_config.data);
                break;
            }
#endif

#ifdef WITH_SQLITE
            case DatabaseType::SQLite: {
                // For SQLite, the "host" config is used as the database file path
                std::string dbPath = m_config.connection.host;
                if (dbPath == "localhost" || dbPath.empty()) {
                    // If no path specified, use default database name or error
                    if (!m_config.connection.default_database.empty()) {
                        dbPath = m_config.connection.default_database;
                    } else {
                        spdlog::error("SQLite requires a database file path. Use -H <path> or -D <path>");
                        return -1;
                    }
                }

                auto pool = std::make_unique<SQLiteConnectionPool>(
                    dbPath,
                    m_config.performance.connection_pool_size);

                if (!pool->healthCheck()) {
                    spdlog::error("Failed to open SQLite database: {}", dbPath);
                    return -1;
                }

                m_pool = std::move(pool);

                m_schema = std::make_unique<SQLiteSchemaManager>(
                    *std::get<std::unique_ptr<SQLiteConnectionPool>>(m_pool), *m_cache);

                // For SQLite, FileHandleManager doesn't need MySQL pool
                m_fileHandles = std::make_unique<VirtualFileHandleManager>(
                    *m_schema, *m_cache, m_config.data);

                spdlog::info("SQLite database opened: {}", dbPath);
                break;
            }
#endif

#ifdef WITH_POSTGRESQL
            case DatabaseType::PostgreSQL: {
                auto pool = std::make_unique<PostgreSQLConnectionPool>(
                    m_config.connection,
                    m_config.performance.connection_pool_size);

                if (!pool->healthCheck()) {
                    spdlog::error("Failed to connect to PostgreSQL server");
                    return -1;
                }

                m_pool = std::move(pool);

                m_schema = std::make_unique<PostgreSQLSchemaManager>(
                    *std::get<std::unique_ptr<PostgreSQLConnectionPool>>(m_pool), *m_cache);

                m_fileHandles = std::make_unique<VirtualFileHandleManager>(
                    *m_schema, *m_cache, m_config.data);

                spdlog::info("Connected to PostgreSQL server");
                break;
            }
#else
            case DatabaseType::PostgreSQL:
                throw std::runtime_error("PostgreSQL support is not compiled in");
#endif

#ifdef WITH_ORACLE
            case DatabaseType::Oracle: {
                auto pool = std::make_unique<OracleConnectionPool>(
                    m_config.connection,
                    m_config.performance.connection_pool_size);

                if (!pool->healthCheck()) {
                    spdlog::error("Failed to connect to Oracle server");
                    return -1;
                }

                m_pool = std::move(pool);

                m_schema = std::make_unique<OracleSchemaManager>(
                    *std::get<std::unique_ptr<OracleConnectionPool>>(m_pool), *m_cache);

                m_fileHandles = std::make_unique<VirtualFileHandleManager>(
                    *m_schema, *m_cache, m_config.data);

                spdlog::info("Connected to Oracle server");
                break;
            }
#else
            case DatabaseType::Oracle:
                throw std::runtime_error("Oracle support is not compiled in");
#endif

            default:
                throw std::invalid_argument("Unknown database type");
        }

        m_initialized = true;
        spdlog::info("SQL FUSE filesystem initialized");

        return 0;

    } catch (const std::exception& e) {
        spdlog::error("Initialization failed: {}", e.what());
        return -1;
    }
}

int SQLFuseFS::run(int argc, char* argv[]) {
    if (!m_initialized) {
        spdlog::error("Filesystem not initialized");
        return -1;
    }

    // Build FUSE arguments (only mount options, not -f/-d which are handled separately)
    std::vector<const char*> fuse_argv;
    fuse_argv.push_back(argv[0]);

    if (m_config.debug) {
        fuse_argv.push_back("-d");
    }

    if (m_config.allow_other) {
        fuse_argv.push_back("-o");
        fuse_argv.push_back("allow_other");
    }

    if (m_config.allow_root) {
        fuse_argv.push_back("-o");
        fuse_argv.push_back("allow_root");
    }

    // Get FUSE operations
    const fuse_operations& ops = getFuseOperations();

    // Parse arguments and create FUSE instance
    struct fuse_args args = FUSE_ARGS_INIT(static_cast<int>(fuse_argv.size()),
                                            const_cast<char**>(fuse_argv.data()));
    struct fuse* fuse = fuse_new(&args, &ops, sizeof(ops), nullptr);
    if (!fuse) {
        fuse_opt_free_args(&args);
        return -1;
    }

    // Mount the filesystem
    if (fuse_mount(fuse, m_config.mountpoint.c_str()) != 0) {
        fuse_destroy(fuse);
        fuse_opt_free_args(&args);
        return -1;
    }

    // Set up signal handlers
    struct fuse_session* se = fuse_get_session(fuse);
    if (fuse_set_signal_handlers(se) != 0) {
        fuse_unmount(fuse);
        fuse_destroy(fuse);
        fuse_opt_free_args(&args);
        return -1;
    }

    // Create loop config with proper max_threads
    struct fuse_loop_config* loop_config = fuse_loop_cfg_create();
    fuse_loop_cfg_set_max_threads(loop_config, static_cast<unsigned int>(m_config.performance.max_fuse_threads));

    // Run the event loop
    int ret = fuse_loop_mt(fuse, loop_config);

    // Cleanup
    fuse_loop_cfg_destroy(loop_config);
    fuse_remove_signal_handlers(se);
    fuse_unmount(fuse);
    fuse_destroy(fuse);
    fuse_opt_free_args(&args);

    return ret;
}

void SQLFuseFS::shutdown() {
    // Drain the appropriate connection pool
    std::visit([](auto&& pool) {
        using T = std::decay_t<decltype(pool)>;
        if constexpr (!std::is_same_v<T, std::monostate>) {
            if (pool) {
                pool->drain();
            }
        }
    }, m_pool);

    m_initialized = false;
    spdlog::info("SQL FUSE filesystem shutdown");
}

bool SQLFuseFS::isDatabaseAllowed(const std::string& database) const {
    // Check denied list first
    for (const auto& denied : m_config.security.denied_databases) {
        if (denied == database) {
            return false;
        }
    }

    // If allowed list is specified, database must be in it
    if (!m_config.security.allowed_databases.empty()) {
        for (const auto& allowed : m_config.security.allowed_databases) {
            if (allowed == database) {
                return true;
            }
        }
        return false;
    }

    // Filter system databases unless explicitly allowed
    if (!m_config.security.expose_system_databases) {
        if (database == "mysql" || database == "information_schema" ||
            database == "performance_schema" || database == "sys") {
            return false;
        }
    }

    return true;
}

int SQLFuseFS::fillStatForNode(const ParsedPath& parsed, struct stat* stbuf) {
    memset(stbuf, 0, sizeof(struct stat));

    time_t now = time(nullptr);
    stbuf->st_atime = now;
    stbuf->st_mtime = now;
    stbuf->st_ctime = now;
    stbuf->st_uid = getuid();
    stbuf->st_gid = getgid();

    if (parsed.isDirectory()) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
    } else {
        stbuf->st_mode = S_IFREG;
        if (parsed.isReadOnly() || m_config.security.read_only) {
            stbuf->st_mode |= 0444;
        } else {
            stbuf->st_mode |= 0644;
        }
        stbuf->st_nlink = 1;

        // Try to get size (this may involve querying the database)
        // For now, report a reasonable estimate
        stbuf->st_size = 4096;  // Will be updated on open/read
    }

    return 0;
}

int SQLFuseFS::getattr(const char* path, struct stat* stbuf, fuse_file_info* fi) {
    (void)fi;  // Unused

    spdlog::debug("getattr: {}", path);

    ParsedPath parsed = m_router.parse(path);

    if (parsed.type == NodeType::NotFound) {
        return -ENOENT;
    }

    // Check database access
    if (!parsed.database.empty() && !isDatabaseAllowed(parsed.database)) {
        return -ENOENT;
    }

    // Verify object exists
    try {
        switch (parsed.type) {
            case NodeType::Database:
                if (!m_schema->databaseExists(parsed.database)) {
                    return -ENOENT;
                }
                break;

            case NodeType::TableFile:
            case NodeType::TableDir:
            case NodeType::TableSchema:
            case NodeType::TableIndexes:
            case NodeType::TableStats:
            case NodeType::TableRowsDir:
                if (!m_schema->tableExists(parsed.database, parsed.object_name)) {
                    return -ENOENT;
                }
                break;

            case NodeType::TableRowFile: {
                // Check if row exists
                auto info = m_schema->getTableInfo(parsed.database, parsed.object_name);
                if (!info || info->primaryKeyColumn.empty()) {
                    return -ENOENT;
                }
                // Could verify row exists, but that's expensive
                break;
            }

            default:
                break;
        }
    } catch (const std::exception& e) {
        spdlog::error("getattr error: {}", e.what());
        return -EIO;
    }

    return fillStatForNode(parsed, stbuf);
}

int SQLFuseFS::readdir(const char* path, void* buf, fuse_fill_dir_t filler,
                         off_t offset, fuse_file_info* fi, fuse_readdir_flags flags) {
    (void)offset;
    (void)fi;
    (void)flags;

    spdlog::debug("readdir: {}", path);

    ParsedPath parsed = m_router.parse(path);

    // Add . and ..
    filler(buf, ".", nullptr, 0, static_cast<fuse_fill_dir_flags>(0));
    filler(buf, "..", nullptr, 0, static_cast<fuse_fill_dir_flags>(0));

    try {
        switch (parsed.type) {
            case NodeType::Root:
                return fillRootDir(buf, filler);

            case NodeType::Database:
                return fillDatabaseDir(buf, filler, parsed.database);

            case NodeType::TablesDir:
                return fillTablesDir(buf, filler, parsed.database);

            case NodeType::TableDir:
                return fillTableDir(buf, filler, parsed.database, parsed.object_name);

            case NodeType::TableRowsDir:
                return fillRowsDir(buf, filler, parsed.database, parsed.object_name);

            case NodeType::ViewsDir:
                return fillViewsDir(buf, filler, parsed.database);

            case NodeType::ProceduresDir:
                return fillProceduresDir(buf, filler, parsed.database);

            case NodeType::FunctionsDir:
                return fillFunctionsDir(buf, filler, parsed.database);

            case NodeType::TriggersDir:
                return fillTriggersDir(buf, filler, parsed.database);

            case NodeType::UsersDir:
                return fillUsersDir(buf, filler);

            case NodeType::VariablesDir:
                filler(buf, "global", nullptr, 0, static_cast<fuse_fill_dir_flags>(0));
                filler(buf, "session", nullptr, 0, static_cast<fuse_fill_dir_flags>(0));
                return 0;

            case NodeType::GlobalVariablesDir:
                return fillVariablesDir(buf, filler, "global");

            case NodeType::SessionVariablesDir:
                return fillVariablesDir(buf, filler, "session");

            default:
                return -ENOTDIR;
        }
    } catch (const std::exception& e) {
        spdlog::error("readdir error: {}", e.what());
        return -EIO;
    }
}

int SQLFuseFS::fillRootDir(void* buf, fuse_fill_dir_t filler) {
    // List databases
    auto databases = m_schema->getDatabases();

    for (const auto& db : databases) {
        if (isDatabaseAllowed(db)) {
            filler(buf, db.c_str(), nullptr, 0, static_cast<fuse_fill_dir_flags>(0));
        }
    }

    // Add special entries
    filler(buf, ".server_info", nullptr, 0, static_cast<fuse_fill_dir_flags>(0));
    filler(buf, ".users", nullptr, 0, static_cast<fuse_fill_dir_flags>(0));
    filler(buf, ".variables", nullptr, 0, static_cast<fuse_fill_dir_flags>(0));

    return 0;
}

int SQLFuseFS::fillDatabaseDir(void* buf, fuse_fill_dir_t filler,
                                  const std::string& database) {
    filler(buf, "tables", nullptr, 0, static_cast<fuse_fill_dir_flags>(0));
    filler(buf, "views", nullptr, 0, static_cast<fuse_fill_dir_flags>(0));
    filler(buf, "procedures", nullptr, 0, static_cast<fuse_fill_dir_flags>(0));
    filler(buf, "functions", nullptr, 0, static_cast<fuse_fill_dir_flags>(0));
    filler(buf, "triggers", nullptr, 0, static_cast<fuse_fill_dir_flags>(0));
    filler(buf, ".info", nullptr, 0, static_cast<fuse_fill_dir_flags>(0));

    return 0;
}

int SQLFuseFS::fillTablesDir(void* buf, fuse_fill_dir_t filler,
                                const std::string& database) {
    auto tables = m_schema->getTables(database);

    for (const auto& table : tables) {
        // Add directory entry for each table
        filler(buf, table.c_str(), nullptr, 0, static_cast<fuse_fill_dir_flags>(0));

        // Add file entries
        std::string csv = table + ".csv";
        std::string json = table + ".json";
        std::string sql = table + ".sql";

        filler(buf, csv.c_str(), nullptr, 0, static_cast<fuse_fill_dir_flags>(0));
        filler(buf, json.c_str(), nullptr, 0, static_cast<fuse_fill_dir_flags>(0));
        filler(buf, sql.c_str(), nullptr, 0, static_cast<fuse_fill_dir_flags>(0));
    }

    return 0;
}

int SQLFuseFS::fillTableDir(void* buf, fuse_fill_dir_t filler,
                               const std::string& database, const std::string& table) {
    filler(buf, ".schema", nullptr, 0, static_cast<fuse_fill_dir_flags>(0));
    filler(buf, ".indexes", nullptr, 0, static_cast<fuse_fill_dir_flags>(0));
    filler(buf, ".stats", nullptr, 0, static_cast<fuse_fill_dir_flags>(0));
    filler(buf, "rows", nullptr, 0, static_cast<fuse_fill_dir_flags>(0));

    return 0;
}

int SQLFuseFS::fillRowsDir(void* buf, fuse_fill_dir_t filler,
                              const std::string& database, const std::string& table) {
    auto row_ids = m_schema->getRowIds(database, table, m_config.data.rows_per_page, 0);

    for (const auto& id : row_ids) {
        std::string filename = id + ".json";
        filler(buf, filename.c_str(), nullptr, 0, static_cast<fuse_fill_dir_flags>(0));
    }

    return 0;
}

int SQLFuseFS::fillViewsDir(void* buf, fuse_fill_dir_t filler,
                               const std::string& database) {
    auto views = m_schema->getViews(database);

    for (const auto& view : views) {
        std::string csv = view + ".csv";
        std::string json = view + ".json";
        std::string sql = view + ".sql";

        filler(buf, csv.c_str(), nullptr, 0, static_cast<fuse_fill_dir_flags>(0));
        filler(buf, json.c_str(), nullptr, 0, static_cast<fuse_fill_dir_flags>(0));
        filler(buf, sql.c_str(), nullptr, 0, static_cast<fuse_fill_dir_flags>(0));
    }

    return 0;
}

int SQLFuseFS::fillProceduresDir(void* buf, fuse_fill_dir_t filler,
                                    const std::string& database) {
    auto procedures = m_schema->getProcedures(database);

    for (const auto& proc : procedures) {
        std::string filename = proc + ".sql";
        filler(buf, filename.c_str(), nullptr, 0, static_cast<fuse_fill_dir_flags>(0));
    }

    return 0;
}

int SQLFuseFS::fillFunctionsDir(void* buf, fuse_fill_dir_t filler,
                                   const std::string& database) {
    auto functions = m_schema->getFunctions(database);

    for (const auto& func : functions) {
        std::string filename = func + ".sql";
        filler(buf, filename.c_str(), nullptr, 0, static_cast<fuse_fill_dir_flags>(0));
    }

    return 0;
}

int SQLFuseFS::fillTriggersDir(void* buf, fuse_fill_dir_t filler,
                                  const std::string& database) {
    auto triggers = m_schema->getTriggers(database);

    for (const auto& trigger : triggers) {
        std::string filename = trigger + ".sql";
        filler(buf, filename.c_str(), nullptr, 0, static_cast<fuse_fill_dir_flags>(0));
    }

    return 0;
}

int SQLFuseFS::fillUsersDir(void* buf, fuse_fill_dir_t filler) {
    auto users = m_schema->getUsers();

    for (const auto& user : users) {
        std::string filename = user.user + "@" + user.host + ".info";
        filler(buf, filename.c_str(), nullptr, 0, static_cast<fuse_fill_dir_flags>(0));
    }

    return 0;
}

int SQLFuseFS::fillVariablesDir(void* buf, fuse_fill_dir_t filler,
                                   const std::string& scope) {
    std::unordered_map<std::string, std::string> vars;

    if (scope == "global") {
        vars = m_schema->getGlobalVariables();
    } else {
        vars = m_schema->getSessionVariables();
    }

    for (const auto& [name, value] : vars) {
        filler(buf, name.c_str(), nullptr, 0, static_cast<fuse_fill_dir_flags>(0));
    }

    return 0;
}

int SQLFuseFS::open(const char* path, fuse_file_info* fi) {
    spdlog::debug("open: {}", path);

    ParsedPath parsed = m_router.parse(path);

    if (parsed.type == NodeType::NotFound) {
        return -ENOENT;
    }

    if (parsed.isDirectory()) {
        return -EISDIR;
    }

    // Check write access
    if ((fi->flags & O_ACCMODE) != O_RDONLY) {
        if (m_config.security.read_only || parsed.isReadOnly()) {
            return -EROFS;
        }
    }

    // Create file handle
    uint64_t handle = m_fileHandles->create(parsed);
    fi->fh = handle;

    return 0;
}

int SQLFuseFS::read(const char* path, char* buf, size_t size, off_t offset,
                      fuse_file_info* fi) {
    spdlog::debug("read: {} (size={}, offset={})", path, size, offset);

    VirtualFile* file = m_fileHandles->get(fi->fh);
    if (!file) {
        return -EBADF;
    }

    try {
        std::string content = file->getContent();

        if (offset >= static_cast<off_t>(content.size())) {
            return 0;
        }

        size_t available = content.size() - static_cast<size_t>(offset);
        size_t to_read = std::min(size, available);

        memcpy(buf, content.data() + offset, to_read);

        return static_cast<int>(to_read);

    } catch (const std::exception& e) {
        spdlog::error("read error: {}", e.what());
        return -EIO;
    }
}

int SQLFuseFS::write(const char* path, const char* buf, size_t size, off_t offset,
                       fuse_file_info* fi) {
    spdlog::debug("write: {} (size={}, offset={})", path, size, offset);

    if (m_config.security.read_only) {
        return -EROFS;
    }

    VirtualFile* file = m_fileHandles->get(fi->fh);
    if (!file) {
        return -EBADF;
    }

    return file->write(buf, size, offset);
}

int SQLFuseFS::create(const char* path, mode_t mode, fuse_file_info* fi) {
    spdlog::debug("create: {}", path);

    (void)mode;

    if (m_config.security.read_only) {
        return -EROFS;
    }

    ParsedPath parsed = m_router.parse(path);

    // Only allow creating row files
    if (parsed.type != NodeType::TableRowFile) {
        return -EACCES;
    }

    uint64_t handle = m_fileHandles->create(parsed);
    fi->fh = handle;

    return 0;
}

int SQLFuseFS::unlink(const char* path) {
    spdlog::debug("unlink: {}", path);

    if (m_config.security.read_only) {
        return -EROFS;
    }

    ParsedPath parsed = m_router.parse(path);

    // Only allow deleting row files
    if (parsed.type != NodeType::TableRowFile) {
        return -EACCES;
    }

    try {
        auto table_info = m_schema->getTableInfo(parsed.database, parsed.object_name);
        if (!table_info || table_info->primaryKeyColumn.empty()) {
            return -EINVAL;
        }

        int affected_rows = 0;

#ifdef WITH_MYSQL
        if (m_dbType == DatabaseType::MySQL) {
            std::string sql = MySQLFormatConverter::buildDelete(
                parsed.database + "." + parsed.object_name,
                table_info->primaryKeyColumn,
                parsed.row_id,
                true);
            auto* pool = std::get_if<std::unique_ptr<MySQLConnectionPool>>(&m_pool);
            if (!pool || !*pool) {
                return -EIO;
            }
            auto conn = (*pool)->acquire();
            if (!conn->query(sql)) {
                return -ErrorHandler::mysqlToErrno(conn->errorNumber());
            }
            affected_rows = static_cast<int>(conn->affectedRows());
        }
#endif

#ifdef WITH_SQLITE
        if (m_dbType == DatabaseType::SQLite) {
            std::string sql = SQLiteFormatConverter::buildDelete(
                parsed.object_name,
                table_info->primaryKeyColumn,
                parsed.row_id,
                true);
            auto* pool = std::get_if<std::unique_ptr<SQLiteConnectionPool>>(&m_pool);
            if (!pool || !*pool) {
                return -EIO;
            }
            auto conn = (*pool)->acquire();
            if (!conn || !conn->execute(sql)) {
                spdlog::error("SQLite delete error: {}", conn ? conn->error() : "null connection");
                return -EIO;
            }
            affected_rows = conn->changes();
            (*pool)->release(std::move(conn));
        }
#endif

#ifdef WITH_POSTGRESQL
        if (m_dbType == DatabaseType::PostgreSQL) {
            std::string sql = PostgreSQLFormatConverter::buildDelete(
                parsed.object_name,
                table_info->primaryKeyColumn,
                parsed.row_id,
                true);
            auto* pool = std::get_if<std::unique_ptr<PostgreSQLConnectionPool>>(&m_pool);
            if (!pool || !*pool) {
                return -EIO;
            }
            auto conn = (*pool)->acquire();
            PostgreSQLResultSet result(conn->execute(sql));
            if (!result.isOk()) {
                spdlog::error("PostgreSQL delete error: {}", result.errorMessage());
                return -EIO;
            }
            affected_rows = static_cast<int>(conn->affectedRows(result.get()));
        }
#endif

#ifdef WITH_ORACLE
        if (m_dbType == DatabaseType::Oracle) {
            std::string sql = OracleFormatConverter::buildDelete(
                parsed.database + "." + parsed.object_name,
                table_info->primaryKeyColumn,
                parsed.row_id,
                true);
            auto* pool = std::get_if<std::unique_ptr<OracleConnectionPool>>(&m_pool);
            if (!pool || !*pool) {
                return -EIO;
            }
            auto conn = (*pool)->acquire();
            if (!conn->executeNonQuery(sql)) {
                spdlog::error("Oracle delete error: {}", conn->getError());
                return -ErrorHandler::oracleToErrno(conn->getErrorCode());
            }
            affected_rows = static_cast<int>(conn->affectedRows());
            conn->commit();
        }
#endif

        if (affected_rows == 0) {
            return -ENOENT;
        }

        // Invalidate cache
        m_cache->invalidateTable(parsed.database, parsed.object_name);

        return 0;

    } catch (const std::exception& e) {
        spdlog::error("unlink error: {}", e.what());
        return -EIO;
    }
}

int SQLFuseFS::truncate(const char* path, off_t size, fuse_file_info* fi) {
    spdlog::debug("truncate: {} (size={})", path, size);

    if (m_config.security.read_only) {
        return -EROFS;
    }

    if (fi && fi->fh) {
        VirtualFile* file = m_fileHandles->get(fi->fh);
        if (file) {
            return file->truncate(size);
        }
    }

    // Truncating a table to 0 could mean TRUNCATE TABLE, but that's dangerous
    // For now, just return success for size 0 (will be handled on write)
    if (size == 0) {
        return 0;
    }

    return -EACCES;
}

int SQLFuseFS::release(const char* path, fuse_file_info* fi) {
    spdlog::debug("release: {}", path);

    VirtualFile* file = m_fileHandles->get(fi->fh);
    if (file) {
        // Flush any pending writes
        if (file->isModified()) {
            int result = file->flush();
            if (result != 0) {
                spdlog::error("Failed to flush writes: {}", file->lastError());
            }
        }
    }

    m_fileHandles->release(fi->fh);

    return 0;
}

int SQLFuseFS::flush(const char* path, fuse_file_info* fi) {
    spdlog::debug("flush: {}", path);

    VirtualFile* file = m_fileHandles->get(fi->fh);
    if (file && file->isModified()) {
        return file->flush();
    }

    return 0;
}

int SQLFuseFS::statfs(const char* path, struct statvfs* stbuf) {
    (void)path;

    memset(stbuf, 0, sizeof(struct statvfs));

    stbuf->f_bsize = 4096;
    stbuf->f_frsize = 4096;
    stbuf->f_blocks = 1000000;
    stbuf->f_bfree = 500000;
    stbuf->f_bavail = 500000;
    stbuf->f_files = 100000;
    stbuf->f_ffree = 50000;
    stbuf->f_namemax = 255;

    return 0;
}

int SQLFuseFS::utimens(const char* path, const struct timespec tv[2],
                         fuse_file_info* fi) {
    (void)path;
    (void)tv;
    (void)fi;

    // We don't support setting times on virtual files
    return 0;
}

// C-style FUSE callbacks

extern "C" {

int sql_fuse_getattr(const char* path, struct stat* stbuf, fuse_file_info* fi) {
    return SQLFuseFS::instance().getattr(path, stbuf, fi);
}

int sql_fuse_readdir(const char* path, void* buf, fuse_fill_dir_t filler,
                       off_t offset, fuse_file_info* fi, fuse_readdir_flags flags) {
    return SQLFuseFS::instance().readdir(path, buf, filler, offset, fi, flags);
}

int sql_fuse_open(const char* path, fuse_file_info* fi) {
    return SQLFuseFS::instance().open(path, fi);
}

int sql_fuse_read(const char* path, char* buf, size_t size, off_t offset,
                    fuse_file_info* fi) {
    return SQLFuseFS::instance().read(path, buf, size, offset, fi);
}

int sql_fuse_write(const char* path, const char* buf, size_t size, off_t offset,
                     fuse_file_info* fi) {
    return SQLFuseFS::instance().write(path, buf, size, offset, fi);
}

int sql_fuse_create(const char* path, mode_t mode, fuse_file_info* fi) {
    return SQLFuseFS::instance().create(path, mode, fi);
}

int sql_fuse_unlink(const char* path) {
    return SQLFuseFS::instance().unlink(path);
}

int sql_fuse_truncate(const char* path, off_t size, fuse_file_info* fi) {
    return SQLFuseFS::instance().truncate(path, size, fi);
}

int sql_fuse_release(const char* path, fuse_file_info* fi) {
    return SQLFuseFS::instance().release(path, fi);
}

int sql_fuse_flush(const char* path, fuse_file_info* fi) {
    return SQLFuseFS::instance().flush(path, fi);
}

int sql_fuse_statfs(const char* path, struct statvfs* stbuf) {
    return SQLFuseFS::instance().statfs(path, stbuf);
}

int sql_fuse_utimens(const char* path, const struct timespec tv[2],
                       fuse_file_info* fi) {
    return SQLFuseFS::instance().utimens(path, tv, fi);
}

void* sql_fuse_init(fuse_conn_info* conn, fuse_config* cfg) {
    (void)conn;

    cfg->kernel_cache = 1;
    cfg->auto_cache = 1;
    cfg->entry_timeout = 1.0;
    cfg->attr_timeout = 1.0;
    cfg->negative_timeout = 1.0;

    return nullptr;
}

void sql_fuse_destroy(void* private_data) {
    (void)private_data;
    SQLFuseFS::instance().shutdown();
}

}  // extern "C"

const fuse_operations& getFuseOperations() {
    static fuse_operations ops = {};

    ops.getattr = sql_fuse_getattr;
    ops.readdir = sql_fuse_readdir;
    ops.open = sql_fuse_open;
    ops.read = sql_fuse_read;
    ops.write = sql_fuse_write;
    ops.create = sql_fuse_create;
    ops.unlink = sql_fuse_unlink;
    ops.truncate = sql_fuse_truncate;
    ops.release = sql_fuse_release;
    ops.flush = sql_fuse_flush;
    ops.statfs = sql_fuse_statfs;
    ops.utimens = sql_fuse_utimens;
    ops.init = sql_fuse_init;
    ops.destroy = sql_fuse_destroy;

    return ops;
}

}  // namespace sqlfuse
