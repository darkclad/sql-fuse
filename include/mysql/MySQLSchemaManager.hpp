#pragma once

/**
 * @file MySQLSchemaManager.hpp
 * @brief MySQL-specific database schema and metadata manager.
 *
 * This file provides the SchemaManager implementation for MySQL databases,
 * handling all metadata queries for databases, tables, views, procedures,
 * functions, triggers, and related database objects.
 */

#include "SchemaManager.hpp"
#include "MySQLConnectionPool.hpp"

namespace sqlfuse {

/**
 * @class MySQLSchemaManager
 * @brief SchemaManager implementation for MySQL databases.
 *
 * Queries MySQL's INFORMATION_SCHEMA views and system tables to provide
 * metadata about database objects. Also uses SHOW commands for certain
 * operations that are more efficient or provide additional information.
 *
 * System Catalog Queries Used:
 * - INFORMATION_SCHEMA.SCHEMATA: Database list
 * - INFORMATION_SCHEMA.TABLES: Table and view metadata
 * - INFORMATION_SCHEMA.COLUMNS: Column definitions
 * - INFORMATION_SCHEMA.STATISTICS: Index information
 * - INFORMATION_SCHEMA.VIEWS: View definitions
 * - INFORMATION_SCHEMA.ROUTINES: Stored procedures and functions
 * - INFORMATION_SCHEMA.TRIGGERS: Trigger definitions
 * - mysql.user: User account information
 * - SHOW CREATE TABLE/VIEW/PROCEDURE/FUNCTION/TRIGGER: DDL statements
 * - SHOW VARIABLES: Server configuration
 *
 * Thread Safety:
 * - All methods acquire connections from the pool and are thread-safe
 * - Each method call uses its own connection from the pool
 */
class MySQLSchemaManager : public SchemaManager {
public:
    /**
     * @brief Construct a MySQL schema manager.
     * @param pool Connection pool for database access.
     * @param cache Cache manager for storing query results.
     */
    MySQLSchemaManager(MySQLConnectionPool& pool, CacheManager& cache);

    ~MySQLSchemaManager() override = default;

    // ----- Database operations -----

    /**
     * @brief Get list of all databases on the server.
     * @return Vector of database names, excluding system databases.
     */
    std::vector<std::string> getDatabases() override;

    /**
     * @brief Check if a database exists.
     * @param database Database name to check.
     * @return true if the database exists.
     */
    bool databaseExists(const std::string& database) override;

    // ----- Table operations -----

    /**
     * @brief Get list of tables in a database.
     * @param database Database name.
     * @return Vector of table names.
     */
    std::vector<std::string> getTables(const std::string& database) override;

    /**
     * @brief Get detailed information about a table.
     * @param database Database name.
     * @param table Table name.
     * @return TableInfo including engine, row count, primary key, etc.
     */
    std::optional<TableInfo> getTableInfo(const std::string& database,
                                           const std::string& table) override;

    /**
     * @brief Get column definitions for a table.
     * @param database Database name.
     * @param table Table name.
     * @return Vector of ColumnInfo with types, nullability, defaults, keys.
     */
    std::vector<ColumnInfo> getColumns(const std::string& database,
                                        const std::string& table) override;

    /**
     * @brief Get index definitions for a table.
     * @param database Database name.
     * @param table Table name.
     * @return Vector of IndexInfo with columns and uniqueness.
     */
    std::vector<IndexInfo> getIndexes(const std::string& database,
                                       const std::string& table) override;

    /**
     * @brief Check if a table exists in a database.
     * @param database Database name.
     * @param table Table name.
     * @return true if table exists.
     */
    bool tableExists(const std::string& database, const std::string& table) override;

    // ----- View operations -----

    /**
     * @brief Get list of views in a database.
     * @param database Database name.
     * @return Vector of view names.
     */
    std::vector<std::string> getViews(const std::string& database) override;

    /**
     * @brief Get information about a view.
     * @param database Database name.
     * @param view View name.
     * @return ViewInfo with definition.
     */
    std::optional<ViewInfo> getViewInfo(const std::string& database,
                                         const std::string& view) override;

    // ----- Routine operations -----

    /**
     * @brief Get list of stored procedures in a database.
     * @param database Database name.
     * @return Vector of procedure names.
     */
    std::vector<std::string> getProcedures(const std::string& database) override;

    /**
     * @brief Get list of functions in a database.
     * @param database Database name.
     * @return Vector of function names.
     */
    std::vector<std::string> getFunctions(const std::string& database) override;

    /**
     * @brief Get information about a procedure or function.
     * @param database Database name.
     * @param name Routine name.
     * @param type "PROCEDURE" or "FUNCTION".
     * @return RoutineInfo with metadata and body.
     */
    std::optional<RoutineInfo> getRoutineInfo(const std::string& database,
                                               const std::string& name,
                                               const std::string& type) override;

    // ----- Trigger operations -----

    /**
     * @brief Get list of triggers in a database.
     * @param database Database name.
     * @return Vector of trigger names.
     */
    std::vector<std::string> getTriggers(const std::string& database) override;

    /**
     * @brief Get information about a trigger.
     * @param database Database name.
     * @param trigger Trigger name.
     * @return TriggerInfo with timing, event, table, and body.
     */
    std::optional<TriggerInfo> getTriggerInfo(const std::string& database,
                                               const std::string& trigger) override;

    // ----- DDL statements -----

    /**
     * @brief Get the CREATE statement for a database object.
     * @param database Database name.
     * @param object Object name.
     * @param type Object type ("TABLE", "VIEW", "PROCEDURE", "FUNCTION", "TRIGGER").
     * @return DDL statement from SHOW CREATE command.
     */
    std::string getCreateStatement(const std::string& database,
                                   const std::string& object,
                                   const std::string& type) override;

    // ----- Server info -----

    /**
     * @brief Get MySQL server version and status information.
     * @return ServerInfo with version string and server details.
     */
    ServerInfo getServerInfo() override;

    /**
     * @brief Get list of MySQL users.
     * @return Vector of UserInfo from mysql.user table.
     */
    std::vector<UserInfo> getUsers() override;

    /**
     * @brief Get MySQL global server variables.
     * @return Map of variable names to values from SHOW GLOBAL VARIABLES.
     */
    std::unordered_map<std::string, std::string> getGlobalVariables() override;

    /**
     * @brief Get MySQL session variables.
     * @return Map of variable names to values from SHOW SESSION VARIABLES.
     */
    std::unordered_map<std::string, std::string> getSessionVariables() override;

    // ----- Row operations -----

    /**
     * @brief Get primary key values for row-level access.
     * @param database Database name.
     * @param table Table name.
     * @param limit Maximum number of IDs to return.
     * @param offset Number of rows to skip.
     * @return Vector of primary key values as strings.
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
     * @brief Invalidate all cached data for a database.
     * @param database Database name.
     */
    void invalidateDatabase(const std::string& database) override;

    /**
     * @brief Clear all cached data.
     */
    void invalidateAll() override;

    /**
     * @brief Access the underlying connection pool.
     * @return Reference to the MySQLConnectionPool.
     */
    ConnectionPool& connectionPool() override { return m_pool; }

private:
    /**
     * @brief Escape an identifier for use in SQL (uses backticks).
     */
    std::string escapeIdentifier(const std::string& id) const;

    /**
     * @brief Escape a string value for use in SQL.
     */
    std::string escapeString(const std::string& str) const;

    MySQLConnectionPool& m_pool;  ///< Connection pool for queries
    CacheManager& m_cache;        ///< Cache for metadata
};

}  // namespace sqlfuse
