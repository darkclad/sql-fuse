#pragma once

/**
 * @file SQLiteSchemaManager.hpp
 * @brief SQLite-specific database schema and metadata manager.
 *
 * This file provides the SchemaManager implementation for SQLite databases,
 * handling metadata queries for tables, views, triggers, and other objects.
 * Note that SQLite has a simpler object model than server-based databases.
 */

#include "SchemaManager.hpp"
#include "SQLiteConnectionPool.hpp"
#include "SQLiteResultSet.hpp"

namespace sqlfuse {

// Forward declaration
class CacheManager;

/**
 * @class SQLiteSchemaManager
 * @brief SchemaManager implementation for SQLite databases.
 *
 * Queries SQLite's sqlite_master table and PRAGMA commands to provide
 * metadata about database objects.
 *
 * SQLite Differences from Server-Based Databases:
 * - Single database per file (getDatabases returns the filename)
 * - No stored procedures or functions (returns empty lists)
 * - No user management (returns empty user list)
 * - Uses PRAGMA commands for schema information
 * - Every table has an implicit rowid (unless WITHOUT ROWID)
 *
 * System Tables and PRAGMAs Used:
 * - sqlite_master: All schema objects (tables, views, indexes, triggers)
 * - PRAGMA table_info(table): Column definitions
 * - PRAGMA index_list(table): Index list
 * - PRAGMA index_info(index): Index columns
 * - PRAGMA compile_options: Build configuration
 *
 * Thread Safety:
 * - All methods acquire connections from the pool and are thread-safe
 * - SQLite allows concurrent reads
 */
class SQLiteSchemaManager : public SchemaManager {
public:
    /**
     * @brief Construct an SQLite schema manager.
     * @param pool Connection pool for database access.
     * @param cache Cache manager for storing query results.
     */
    SQLiteSchemaManager(SQLiteConnectionPool& pool, CacheManager& cache);

    ~SQLiteSchemaManager() override = default;

    // ----- Database operations -----
    // SQLite has a single database per file

    /**
     * @brief Get the database name (derived from file path).
     * @return Vector containing a single database name.
     */
    std::vector<std::string> getDatabases() override;

    /**
     * @brief Check if the database exists (always true for valid connections).
     * @param database Database name (typically "main" or the filename).
     * @return true if the database is accessible.
     */
    bool databaseExists(const std::string& database) override;

    // ----- Table operations -----

    /**
     * @brief Get list of tables in the database.
     * @param database Database name (ignored for SQLite, uses connected database).
     * @return Vector of table names from sqlite_master.
     */
    std::vector<std::string> getTables(const std::string& database) override;

    /**
     * @brief Get detailed information about a table.
     * @param database Database name.
     * @param table Table name.
     * @return TableInfo including row count and primary key.
     */
    std::optional<TableInfo> getTableInfo(const std::string& database,
                                           const std::string& table) override;

    /**
     * @brief Get column definitions for a table.
     * @param database Database name.
     * @param table Table name.
     * @return Vector of ColumnInfo from PRAGMA table_info.
     */
    std::vector<ColumnInfo> getColumns(const std::string& database,
                                        const std::string& table) override;

    /**
     * @brief Get index definitions for a table.
     * @param database Database name.
     * @param table Table name.
     * @return Vector of IndexInfo from PRAGMA index_list/index_info.
     */
    std::vector<IndexInfo> getIndexes(const std::string& database,
                                       const std::string& table) override;

    /**
     * @brief Check if a table exists.
     * @param database Database name.
     * @param table Table name.
     * @return true if table exists in sqlite_master.
     */
    bool tableExists(const std::string& database, const std::string& table) override;

    // ----- View operations -----

    /**
     * @brief Get list of views in the database.
     * @param database Database name.
     * @return Vector of view names from sqlite_master.
     */
    std::vector<std::string> getViews(const std::string& database) override;

    /**
     * @brief Get information about a view.
     * @param database Database name.
     * @param view View name.
     * @return ViewInfo (definition stored in sqlite_master.sql).
     */
    std::optional<ViewInfo> getViewInfo(const std::string& database,
                                         const std::string& view) override;

    // ----- Routine operations -----
    // SQLite doesn't support stored procedures or functions

    /**
     * @brief Get stored procedures (not supported in SQLite).
     * @param database Database name.
     * @return Empty vector (SQLite has no stored procedures).
     */
    std::vector<std::string> getProcedures(const std::string& database) override;

    /**
     * @brief Get functions (not supported in SQLite).
     * @param database Database name.
     * @return Empty vector (SQLite has no user-defined functions via SQL).
     */
    std::vector<std::string> getFunctions(const std::string& database) override;

    /**
     * @brief Get routine info (not supported in SQLite).
     * @return std::nullopt always.
     */
    std::optional<RoutineInfo> getRoutineInfo(const std::string& database,
                                               const std::string& name,
                                               const std::string& type) override;

    // ----- Trigger operations -----

    /**
     * @brief Get list of triggers in the database.
     * @param database Database name.
     * @return Vector of trigger names from sqlite_master.
     */
    std::vector<std::string> getTriggers(const std::string& database) override;

    /**
     * @brief Get information about a trigger.
     * @param database Database name.
     * @param trigger Trigger name.
     * @return TriggerInfo parsed from sqlite_master.sql.
     */
    std::optional<TriggerInfo> getTriggerInfo(const std::string& database,
                                               const std::string& trigger) override;

    // ----- DDL statements -----

    /**
     * @brief Get the CREATE statement for a database object.
     * @param database Database name.
     * @param object Object name.
     * @param type Object type ("TABLE", "VIEW", "INDEX", "TRIGGER").
     * @return DDL statement from sqlite_master.sql column.
     */
    std::string getCreateStatement(const std::string& database,
                                   const std::string& object,
                                   const std::string& type) override;

    // ----- Server info -----

    /**
     * @brief Get SQLite version information.
     * @return ServerInfo with SQLite version string.
     */
    ServerInfo getServerInfo() override;

    /**
     * @brief Get users (not applicable to SQLite).
     * @return Empty vector (SQLite has no user management).
     */
    std::vector<UserInfo> getUsers() override;

    /**
     * @brief Get SQLite compile options as "global variables".
     * @return Map of compile options from PRAGMA compile_options.
     */
    std::unordered_map<std::string, std::string> getGlobalVariables() override;

    /**
     * @brief Get session variables (same as global for SQLite).
     * @return Map of compile options.
     */
    std::unordered_map<std::string, std::string> getSessionVariables() override;

    // ----- Row operations -----

    /**
     * @brief Get primary key values for row-level access.
     * @param database Database name.
     * @param table Table name.
     * @param limit Maximum number of IDs to return.
     * @param offset Number of rows to skip.
     * @return Vector of primary key values (or rowid if no PK).
     */
    std::vector<std::string> getRowIds(const std::string& database,
                                        const std::string& table,
                                        size_t limit = 1000,
                                        size_t offset = 0) override;

    /**
     * @brief Get exact row count for a table.
     * @param database Database name.
     * @param table Table name.
     * @return Row count from COUNT(*) query.
     */
    uint64_t getRowCount(const std::string& database, const std::string& table) override;

    // ----- Cache invalidation -----

    /**
     * @brief Invalidate cached data for a specific table.
     * @param database Database name.
     * @param table Table name.
     */
    void invalidateTable(const std::string& database, const std::string& table) override;

    /**
     * @brief Invalidate all cached data for the database.
     * @param database Database name.
     */
    void invalidateDatabase(const std::string& database) override;

    /**
     * @brief Clear all cached data.
     */
    void invalidateAll() override;

    /**
     * @brief Access the underlying connection pool.
     * @return Reference to the SQLiteConnectionPool.
     */
    ConnectionPool& connectionPool() override { return m_pool; }

private:
    /**
     * @brief Escape an identifier for use in SQL (double quotes).
     */
    std::string escapeIdentifier(const std::string& id) const;

    /**
     * @brief Escape a string value for use in SQL.
     */
    std::string escapeString(const std::string& str) const;

    /**
     * @brief Get the primary key column for a table.
     * @param table Table name.
     * @return Primary key column name, or "rowid" if none defined.
     */
    std::string getPrimaryKeyColumn(const std::string& table);

    SQLiteConnectionPool& m_pool;  ///< Connection pool for queries
    CacheManager& m_cache;         ///< Cache for metadata
    std::string m_databaseName;    ///< Database name (derived from file path)
};

}  // namespace sqlfuse
