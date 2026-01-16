#pragma once

/**
 * @file OracleSchemaManager.hpp
 * @brief Oracle-specific database schema and metadata manager.
 *
 * This file provides the SchemaManager implementation for Oracle databases,
 * handling all metadata queries for schemas, tables, views, procedures,
 * functions, triggers, and related database objects.
 *
 * In Oracle terminology, what SQL-FUSE calls "databases" maps to Oracle
 * "schemas" (which are synonymous with "users" in Oracle). Each schema
 * contains tables, views, and other database objects.
 */

#include "SchemaManager.hpp"
#include "OracleConnectionPool.hpp"

namespace sqlfuse {

/**
 * @class OracleSchemaManager
 * @brief SchemaManager implementation for Oracle databases.
 *
 * Queries Oracle data dictionary views (ALL_TABLES, ALL_VIEWS, ALL_PROCEDURES,
 * ALL_TRIGGERS, etc.) to provide metadata about database objects. Also uses
 * DBMS_METADATA.GET_DDL for retrieving CREATE statements.
 *
 * Schema Mapping:
 * - getDatabases() returns a list of accessible schemas (users)
 * - System schemas (SYS, SYSTEM, etc.) are filtered out by default
 * - Tables are identified by schema.table_name combination
 *
 * Data Dictionary Views Used:
 * - ALL_TABLES: Table metadata
 * - ALL_TAB_COLUMNS: Column information
 * - ALL_INDEXES, ALL_IND_COLUMNS: Index information
 * - ALL_VIEWS: View definitions
 * - ALL_PROCEDURES: Stored procedures and functions
 * - ALL_TRIGGERS: Trigger definitions
 * - ALL_CONSTRAINTS, ALL_CONS_COLUMNS: Primary key information
 * - V$VERSION, V$INSTANCE: Server information
 * - V$PARAMETER: Configuration variables
 *
 * Thread Safety:
 * - All methods acquire connections from the pool and are thread-safe
 * - Each method call uses its own connection from the pool
 */
class OracleSchemaManager : public SchemaManager {
public:
    /**
     * @brief Construct an Oracle schema manager.
     * @param pool Connection pool for database access.
     * @param cache Cache manager for storing query results.
     */
    OracleSchemaManager(OracleConnectionPool& pool, CacheManager& cache);

    ~OracleSchemaManager() override = default;

    // ----- Database/Schema operations -----
    // Note: In Oracle, "database" parameter refers to schema/user name

    /**
     * @brief Get list of accessible schemas (Oracle users with tables).
     * @return Vector of schema names, excluding system schemas.
     */
    std::vector<std::string> getDatabases() override;

    /**
     * @brief Check if a schema exists.
     * @param database Schema name to check.
     * @return true if the schema exists in ALL_USERS.
     */
    bool databaseExists(const std::string& database) override;

    // ----- Table operations -----

    /**
     * @brief Get list of tables in a schema.
     * @param database Schema name.
     * @return Vector of table names from ALL_TABLES.
     */
    std::vector<std::string> getTables(const std::string& database) override;

    /**
     * @brief Get detailed information about a table.
     * @param database Schema name.
     * @param table Table name.
     * @return TableInfo including primary key and estimated row count.
     */
    std::optional<TableInfo> getTableInfo(const std::string& database,
                                           const std::string& table) override;

    /**
     * @brief Get column definitions for a table.
     * @param database Schema name.
     * @param table Table name.
     * @return Vector of ColumnInfo with types, nullability, defaults.
     */
    std::vector<ColumnInfo> getColumns(const std::string& database,
                                        const std::string& table) override;

    /**
     * @brief Get index definitions for a table.
     * @param database Schema name.
     * @param table Table name.
     * @return Vector of IndexInfo with columns and uniqueness.
     */
    std::vector<IndexInfo> getIndexes(const std::string& database,
                                       const std::string& table) override;

    /**
     * @brief Check if a table exists in a schema.
     * @param database Schema name.
     * @param table Table name.
     * @return true if table exists in ALL_TABLES.
     */
    bool tableExists(const std::string& database, const std::string& table) override;

    // ----- View operations -----

    /**
     * @brief Get list of views in a schema.
     * @param database Schema name.
     * @return Vector of view names from ALL_VIEWS.
     */
    std::vector<std::string> getViews(const std::string& database) override;

    /**
     * @brief Get information about a view.
     * @param database Schema name.
     * @param view View name.
     * @return ViewInfo (definition available via getCreateStatement).
     */
    std::optional<ViewInfo> getViewInfo(const std::string& database,
                                         const std::string& view) override;

    // ----- Routine operations -----

    /**
     * @brief Get list of stored procedures in a schema.
     * @param database Schema name.
     * @return Vector of procedure names.
     */
    std::vector<std::string> getProcedures(const std::string& database) override;

    /**
     * @brief Get list of functions in a schema.
     * @param database Schema name.
     * @return Vector of function names.
     */
    std::vector<std::string> getFunctions(const std::string& database) override;

    /**
     * @brief Get information about a procedure or function.
     * @param database Schema name.
     * @param name Routine name.
     * @param type "PROCEDURE" or "FUNCTION".
     * @return RoutineInfo with metadata.
     */
    std::optional<RoutineInfo> getRoutineInfo(const std::string& database,
                                               const std::string& name,
                                               const std::string& type) override;

    // ----- Trigger operations -----

    /**
     * @brief Get list of triggers in a schema.
     * @param database Schema name.
     * @return Vector of trigger names.
     */
    std::vector<std::string> getTriggers(const std::string& database) override;

    /**
     * @brief Get information about a trigger.
     * @param database Schema name.
     * @param trigger Trigger name.
     * @return TriggerInfo with timing, event, and body.
     */
    std::optional<TriggerInfo> getTriggerInfo(const std::string& database,
                                               const std::string& trigger) override;

    // ----- DDL statements -----

    /**
     * @brief Get the CREATE statement for a database object.
     * @param database Schema name.
     * @param object Object name (table, view, procedure, etc.).
     * @param type Object type ("TABLE", "VIEW", "PROCEDURE", "FUNCTION", "TRIGGER").
     * @return DDL statement from DBMS_METADATA.GET_DDL.
     */
    std::string getCreateStatement(const std::string& database,
                                   const std::string& object,
                                   const std::string& type) override;

    // ----- Server info -----

    /**
     * @brief Get Oracle server version and instance information.
     * @return ServerInfo from V$VERSION and V$INSTANCE.
     */
    ServerInfo getServerInfo() override;

    /**
     * @brief Get list of database users.
     * @return Vector of UserInfo from ALL_USERS.
     */
    std::vector<UserInfo> getUsers() override;

    /**
     * @brief Get Oracle initialization parameters.
     * @return Map of parameter names to values from V$PARAMETER.
     */
    std::unordered_map<std::string, std::string> getGlobalVariables() override;

    /**
     * @brief Get session-level parameters (same as global for Oracle).
     * @return Map of parameter names to values from V$PARAMETER.
     */
    std::unordered_map<std::string, std::string> getSessionVariables() override;

    // ----- Row operations -----

    /**
     * @brief Get primary key values for row-level access.
     * @param database Schema name.
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
     * @param database Schema name.
     * @param table Table name.
     * @return Row count from COUNT(*) query.
     */
    uint64_t getRowCount(const std::string& database, const std::string& table) override;

    // ----- Cache invalidation -----

    /**
     * @brief Invalidate cached data for a specific table.
     * @param database Schema name.
     * @param table Table name.
     */
    void invalidateTable(const std::string& database, const std::string& table) override;

    /**
     * @brief Invalidate all cached data for a schema.
     * @param database Schema name.
     */
    void invalidateDatabase(const std::string& database) override;

    /**
     * @brief Clear all cached data.
     */
    void invalidateAll() override;

    /**
     * @brief Access the underlying connection pool.
     * @return Reference to the OracleConnectionPool.
     */
    ConnectionPool& connectionPool() override { return m_pool; }

private:
    /**
     * @brief Escape an identifier for use in SQL (delegates to OracleFormatConverter).
     */
    std::string escapeIdentifier(const std::string& id) const;

    /**
     * @brief Escape a string value for use in SQL (delegates to OracleFormatConverter).
     */
    std::string escapeString(const std::string& str) const;

    /**
     * @brief Map Oracle data types to generic SQL type names.
     * @param oracleType Oracle type name (NUMBER, VARCHAR2, etc.).
     * @param precision Numeric precision.
     * @param scale Numeric scale.
     * @return Mapped type name (INTEGER, VARCHAR, etc.).
     */
    std::string mapOracleType(const std::string& oracleType, int precision, int scale) const;

    OracleConnectionPool& m_pool;  ///< Connection pool for queries
    CacheManager& m_cache;         ///< Cache for metadata
};

}  // namespace sqlfuse
