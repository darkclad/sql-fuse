#pragma once

/**
 * @file PostgreSQLSchemaManager.hpp
 * @brief PostgreSQL-specific database schema and metadata manager.
 *
 * This file provides the SchemaManager implementation for PostgreSQL databases,
 * handling metadata queries for databases, schemas, tables, views, functions,
 * procedures, triggers, and other objects using PostgreSQL system catalogs.
 */

#include "SchemaManager.hpp"
#include "PostgreSQLConnectionPool.hpp"

namespace sqlfuse {

/**
 * @class PostgreSQLSchemaManager
 * @brief SchemaManager implementation for PostgreSQL databases.
 *
 * Queries PostgreSQL's system catalogs (pg_catalog) and information_schema
 * to provide metadata about database objects.
 *
 * PostgreSQL Object Model:
 * - Server can contain multiple databases
 * - Each database contains schemas (namespaces)
 * - Default schema is "public"
 * - Objects are referenced as schema.object or just object (uses search_path)
 *
 * System Catalogs Used:
 * - pg_database: Database list
 * - pg_namespace: Schema list (pg_catalog, public, user schemas)
 * - pg_class: Tables, views, indexes, sequences (with pg_tables, pg_views)
 * - pg_attribute: Column definitions
 * - pg_index: Index information
 * - pg_proc: Functions and procedures
 * - pg_trigger: Trigger definitions
 * - pg_roles: User/role information
 * - information_schema.*: SQL-standard metadata views
 *
 * PostgreSQL-Specific Features:
 * - Schemas (namespaces) for organizing objects within a database
 * - Rich function support (FUNCTION, PROCEDURE, AGGREGATE)
 * - CTID as system row identifier (similar to Oracle's ROWID)
 * - Dollar-quoted strings for procedure bodies ($$...$$)
 *
 * Thread Safety:
 * - All methods acquire connections from the pool and are thread-safe
 *
 * @see SchemaManager for the base class interface
 */
class PostgreSQLSchemaManager : public SchemaManager {
public:
    /**
     * @brief Construct a PostgreSQL schema manager.
     * @param pool Connection pool for database access.
     * @param cache Cache manager for storing query results.
     */
    PostgreSQLSchemaManager(PostgreSQLConnectionPool& pool, CacheManager& cache);

    ~PostgreSQLSchemaManager() override = default;

    // ----- Database operations -----

    /**
     * @brief Get list of databases on the server.
     * @return Vector of database names from pg_database.
     *
     * Excludes template databases (template0, template1).
     */
    std::vector<std::string> getDatabases() override;

    /**
     * @brief Check if a database exists.
     * @param database Database name.
     * @return true if database exists in pg_database.
     */
    bool databaseExists(const std::string& database) override;

    // ----- Table operations -----

    /**
     * @brief Get list of tables in the database.
     * @param database Database name (uses connected database's public schema).
     * @return Vector of table names from pg_tables.
     *
     * Returns tables from the public schema by default.
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
     * @return Vector of ColumnInfo from information_schema.columns.
     */
    std::vector<ColumnInfo> getColumns(const std::string& database,
                                        const std::string& table) override;

    /**
     * @brief Get index definitions for a table.
     * @param database Database name.
     * @param table Table name.
     * @return Vector of IndexInfo from pg_indexes.
     */
    std::vector<IndexInfo> getIndexes(const std::string& database,
                                       const std::string& table) override;

    /**
     * @brief Check if a table exists.
     * @param database Database name.
     * @param table Table name.
     * @return true if table exists in information_schema.tables.
     */
    bool tableExists(const std::string& database, const std::string& table) override;

    // ----- View operations -----

    /**
     * @brief Get list of views in the database.
     * @param database Database name.
     * @return Vector of view names from pg_views.
     */
    std::vector<std::string> getViews(const std::string& database) override;

    /**
     * @brief Get information about a view.
     * @param database Database name.
     * @param view View name.
     * @return ViewInfo including definition from pg_views.
     */
    std::optional<ViewInfo> getViewInfo(const std::string& database,
                                         const std::string& view) override;

    // ----- Routine operations -----

    /**
     * @brief Get list of stored procedures.
     * @param database Database name.
     * @return Vector of procedure names from pg_proc.
     *
     * PostgreSQL 11+ distinguishes PROCEDURE from FUNCTION.
     */
    std::vector<std::string> getProcedures(const std::string& database) override;

    /**
     * @brief Get list of functions.
     * @param database Database name.
     * @return Vector of function names from pg_proc.
     */
    std::vector<std::string> getFunctions(const std::string& database) override;

    /**
     * @brief Get information about a function or procedure.
     * @param database Database name.
     * @param name Routine name.
     * @param type Routine type ("FUNCTION" or "PROCEDURE").
     * @return RoutineInfo including parameter list and body.
     */
    std::optional<RoutineInfo> getRoutineInfo(const std::string& database,
                                               const std::string& name,
                                               const std::string& type) override;

    // ----- Trigger operations -----

    /**
     * @brief Get list of triggers in the database.
     * @param database Database name.
     * @return Vector of trigger names from information_schema.triggers.
     */
    std::vector<std::string> getTriggers(const std::string& database) override;

    /**
     * @brief Get information about a trigger.
     * @param database Database name.
     * @param trigger Trigger name.
     * @return TriggerInfo including timing, event, and table.
     */
    std::optional<TriggerInfo> getTriggerInfo(const std::string& database,
                                               const std::string& trigger) override;

    // ----- DDL statements -----

    /**
     * @brief Get the CREATE statement for a database object.
     * @param database Database name.
     * @param object Object name.
     * @param type Object type ("TABLE", "VIEW", "INDEX", "FUNCTION", etc.).
     * @return DDL statement reconstructed from system catalogs.
     *
     * PostgreSQL doesn't store original DDL, so statements are reconstructed.
     */
    std::string getCreateStatement(const std::string& database,
                                   const std::string& object,
                                   const std::string& type) override;

    // ----- Server info -----

    /**
     * @brief Get PostgreSQL server version and info.
     * @return ServerInfo with version string from version().
     */
    ServerInfo getServerInfo() override;

    /**
     * @brief Get list of database users/roles.
     * @return Vector of UserInfo from pg_roles.
     */
    std::vector<UserInfo> getUsers() override;

    /**
     * @brief Get server configuration parameters.
     * @return Map of settings from pg_settings.
     */
    std::unordered_map<std::string, std::string> getGlobalVariables() override;

    /**
     * @brief Get session configuration parameters.
     * @return Map of current session settings from pg_settings.
     */
    std::unordered_map<std::string, std::string> getSessionVariables() override;

    // ----- Row operations -----

    /**
     * @brief Get primary key values for row-level access.
     * @param database Database name.
     * @param table Table name.
     * @param limit Maximum number of IDs to return.
     * @param offset Number of rows to skip.
     * @return Vector of primary key values.
     *
     * Uses CTID if no primary key is defined.
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
     *
     * For large tables, consider pg_class.reltuples for estimates.
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
     * @return Reference to the PostgreSQLConnectionPool.
     */
    ConnectionPool& connectionPool() override { return m_pool; }

    // ----- PostgreSQL-specific operations -----

    /**
     * @brief Get list of schemas within a database.
     * @param database Database name.
     * @return Vector of schema names from pg_namespace.
     *
     * Excludes system schemas (pg_*, information_schema) unless requested.
     */
    std::vector<std::string> getSchemas(const std::string& database);

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
     * @param database Database name.
     * @param table Table name.
     * @return Primary key column name, or empty if none defined.
     */
    std::string getPrimaryKeyColumn(const std::string& database, const std::string& table);

    PostgreSQLConnectionPool& m_pool;  ///< Connection pool for queries
    CacheManager& m_cache;             ///< Cache for metadata
};

}  // namespace sqlfuse
