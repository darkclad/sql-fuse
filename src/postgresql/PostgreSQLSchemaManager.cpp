/**
 * @file PostgreSQLSchemaManager.cpp
 * @brief Implementation of PostgreSQL-specific database schema manager.
 *
 * Implements the PostgreSQLSchemaManager class which queries PostgreSQL's system
 * catalogs (pg_catalog) and information_schema to provide metadata about database
 * objects including tables, views, functions, procedures, and triggers.
 *
 * PostgreSQL System Catalogs Used:
 * - pg_database: Database list
 * - pg_class: Tables, views, indexes, sequences
 * - pg_namespace: Schemas (namespaces)
 * - pg_index: Index definitions
 * - pg_proc: Functions and procedures
 * - pg_user: User information
 * - information_schema.*: SQL-standard metadata views
 *
 * PostgreSQL-Specific Features:
 * - Schemas (namespaces) for organizing objects within a database
 * - Rich function support (FUNCTION, PROCEDURE, AGGREGATE)
 * - CTID as system row identifier
 * - Dollar-quoted strings for procedure bodies
 */

#include "PostgreSQLSchemaManager.hpp"
#include "PostgreSQLResultSet.hpp"
#include "ErrorHandler.hpp"
#include <spdlog/spdlog.h>
#include <sstream>
#include <map>

namespace sqlfuse {

// ============================================================================
// Construction
// ============================================================================

PostgreSQLSchemaManager::PostgreSQLSchemaManager(PostgreSQLConnectionPool& pool, CacheManager& cache)
    : m_pool(pool), m_cache(cache) {
}

// ============================================================================
// Escaping Utilities
// ============================================================================

std::string PostgreSQLSchemaManager::escapeIdentifier(const std::string& id) const {
    // PostgreSQL uses double quotes for identifiers
    std::string result = "\"";
    for (char c : id) {
        if (c == '"') result += "\"\"";
        else result += c;
    }
    result += "\"";
    return result;
}

std::string PostgreSQLSchemaManager::escapeString(const std::string& str) const {
    std::string result;
    result.reserve(str.size() * 2);
    for (char c : str) {
        if (c == '\'') result += "''";
        else result += c;
    }
    return result;
}

// ============================================================================
// Database Operations
// ============================================================================

std::vector<std::string> PostgreSQLSchemaManager::getDatabases() {
    std::string cache_key = "databases";

    if (auto cached = m_cache.get(cache_key)) {
        std::vector<std::string> result;
        std::istringstream iss(*cached);
        std::string line;
        while (std::getline(iss, line)) {
            if (!line.empty()) result.push_back(line);
        }
        return result;
    }

    std::vector<std::string> databases;

    auto conn = m_pool.acquire();
    PostgreSQLResultSet result(conn->execute(
        "SELECT datname FROM pg_database WHERE datistemplate = false ORDER BY datname"));

    if (!result.hasData()) {
        throw std::runtime_error(std::string("Failed to get databases: ") + result.errorMessage());
    }

    while (result.fetchRow()) {
        const char* name = result.getField(0);
        if (name) {
            databases.emplace_back(name);
        }
    }

    // Cache the result
    std::ostringstream oss;
    for (const auto& db : databases) {
        oss << db << "\n";
    }
    m_cache.put(cache_key, oss.str(), CacheManager::Category::Schema);

    return databases;
}

bool PostgreSQLSchemaManager::databaseExists(const std::string& database) {
    auto databases = getDatabases();
    return std::find(databases.begin(), databases.end(), database) != databases.end();
}

std::vector<std::string> PostgreSQLSchemaManager::getSchemas(const std::string& database) {
    std::vector<std::string> schemas;

    auto conn = m_pool.acquire();
    PostgreSQLResultSet result(conn->execute(
        "SELECT schema_name FROM information_schema.schemata "
        "WHERE catalog_name = current_database() "
        "AND schema_name NOT IN ('pg_catalog', 'information_schema', 'pg_toast') "
        "ORDER BY schema_name"));

    if (!result.hasData()) {
        return schemas;
    }

    while (result.fetchRow()) {
        const char* name = result.getField(0);
        if (name) {
            schemas.emplace_back(name);
        }
    }

    return schemas;
}

// ============================================================================
// Table Operations
// ============================================================================

std::vector<std::string> PostgreSQLSchemaManager::getTables(const std::string& database) {
    std::string cache_key = CacheManager::makeKey(database, "tables");

    if (auto cached = m_cache.get(cache_key)) {
        std::vector<std::string> result;
        std::istringstream iss(*cached);
        std::string line;
        while (std::getline(iss, line)) {
            if (!line.empty()) result.push_back(line);
        }
        return result;
    }

    std::vector<std::string> tables;

    auto conn = m_pool.acquire();
    // Get tables from public schema by default
    PostgreSQLResultSet result(conn->execute(
        "SELECT table_name FROM information_schema.tables "
        "WHERE table_schema = 'public' "
        "AND table_type = 'BASE TABLE' "
        "ORDER BY table_name"));

    if (!result.hasData()) {
        spdlog::debug("Failed to get tables or no tables found: {}", result.errorMessage());
        return tables;
    }

    while (result.fetchRow()) {
        const char* name = result.getField(0);
        if (name) {
            tables.emplace_back(name);
        }
    }

    std::ostringstream oss;
    for (const auto& t : tables) {
        oss << t << "\n";
    }
    m_cache.put(cache_key, oss.str(), CacheManager::Category::Schema);

    return tables;
}

bool PostgreSQLSchemaManager::tableExists(const std::string& database, const std::string& table) {
    auto tables = getTables(database);
    return std::find(tables.begin(), tables.end(), table) != tables.end();
}

std::vector<ColumnInfo> PostgreSQLSchemaManager::getColumns(const std::string& database,
                                                              const std::string& table) {
    std::vector<ColumnInfo> columns;

    auto conn = m_pool.acquire();
    std::string sql =
        "SELECT c.column_name, c.data_type, c.udt_name, c.is_nullable, "
        "c.column_default, c.character_maximum_length, c.numeric_precision, "
        "c.numeric_scale, c.ordinal_position, "
        "CASE WHEN pk.column_name IS NOT NULL THEN 'PRI' ELSE '' END as column_key "
        "FROM information_schema.columns c "
        "LEFT JOIN ("
        "  SELECT kcu.column_name "
        "  FROM information_schema.table_constraints tc "
        "  JOIN information_schema.key_column_usage kcu "
        "    ON tc.constraint_name = kcu.constraint_name "
        "  WHERE tc.table_schema = 'public' "
        "    AND tc.table_name = '" + escapeString(table) + "' "
        "    AND tc.constraint_type = 'PRIMARY KEY'"
        ") pk ON c.column_name = pk.column_name "
        "WHERE c.table_schema = 'public' "
        "AND c.table_name = '" + escapeString(table) + "' "
        "ORDER BY c.ordinal_position";

    PostgreSQLResultSet result(conn->execute(sql));

    if (!result.hasData()) {
        return columns;
    }

    while (result.fetchRow()) {
        ColumnInfo col;
        col.name = result.getField(0) ? result.getField(0) : "";
        col.type = result.getField(1) ? result.getField(1) : "";
        col.fullType = result.getField(2) ? result.getField(2) : col.type;
        col.nullable = result.getField(3) && std::string(result.getField(3)) == "YES";
        col.defaultValue = result.getField(4) ? result.getField(4) : "";
        col.key = result.getField(9) ? result.getField(9) : "";
        col.ordinalPosition = result.getField(8) ? std::stoi(result.getField(8)) : 0;

        // Build full type with precision info
        if (result.getField(5)) {  // character_maximum_length
            col.fullType = col.type + "(" + std::string(result.getField(5)) + ")";
        } else if (result.getField(6)) {  // numeric_precision
            col.fullType = col.type + "(" + std::string(result.getField(6));
            if (result.getField(7)) {  // numeric_scale
                col.fullType += "," + std::string(result.getField(7));
            }
            col.fullType += ")";
        }

        columns.push_back(std::move(col));
    }

    return columns;
}

std::vector<IndexInfo> PostgreSQLSchemaManager::getIndexes(const std::string& database,
                                                            const std::string& table) {
    std::vector<IndexInfo> indexes;

    auto conn = m_pool.acquire();
    std::string sql =
        "SELECT i.relname as index_name, "
        "       ix.indisunique as is_unique, "
        "       ix.indisprimary as is_primary, "
        "       am.amname as index_type, "
        "       array_to_string(array_agg(a.attname ORDER BY k.n), ',') as columns "
        "FROM pg_index ix "
        "JOIN pg_class t ON t.oid = ix.indrelid "
        "JOIN pg_class i ON i.oid = ix.indexrelid "
        "JOIN pg_am am ON i.relam = am.oid "
        "JOIN pg_namespace n ON n.oid = t.relnamespace "
        "JOIN generate_subscripts(ix.indkey, 1) k(n) ON true "
        "JOIN pg_attribute a ON a.attrelid = t.oid AND a.attnum = ix.indkey[k.n] "
        "WHERE n.nspname = 'public' "
        "AND t.relname = '" + escapeString(table) + "' "
        "GROUP BY i.relname, ix.indisunique, ix.indisprimary, am.amname "
        "ORDER BY i.relname";

    PostgreSQLResultSet result(conn->execute(sql));

    if (!result.hasData()) {
        return indexes;
    }

    while (result.fetchRow()) {
        IndexInfo idx;
        idx.name = result.getField(0) ? result.getField(0) : "";
        idx.unique = result.getField(1) && std::string(result.getField(1)) == "t";
        idx.primary = result.getField(2) && std::string(result.getField(2)) == "t";
        idx.type = result.getField(3) ? result.getField(3) : "btree";

        // Parse comma-separated column names
        if (result.getField(4)) {
            std::string cols = result.getField(4);
            std::istringstream iss(cols);
            std::string col;
            while (std::getline(iss, col, ',')) {
                idx.columns.push_back(col);
            }
        }

        indexes.push_back(std::move(idx));
    }

    return indexes;
}

std::string PostgreSQLSchemaManager::getPrimaryKeyColumn(const std::string& database,
                                                          const std::string& table) {
    auto conn = m_pool.acquire();
    std::string sql =
        "SELECT a.attname "
        "FROM pg_index i "
        "JOIN pg_attribute a ON a.attrelid = i.indrelid AND a.attnum = ANY(i.indkey) "
        "JOIN pg_class c ON c.oid = i.indrelid "
        "JOIN pg_namespace n ON n.oid = c.relnamespace "
        "WHERE i.indisprimary "
        "AND n.nspname = 'public' "
        "AND c.relname = '" + escapeString(table) + "' "
        "LIMIT 1";

    PostgreSQLResultSet result(conn->execute(sql));

    if (result.hasData() && result.fetchRow()) {
        const char* col = result.getField(0);
        if (col) return col;
    }

    return "";
}

std::optional<TableInfo> PostgreSQLSchemaManager::getTableInfo(const std::string& database,
                                                                 const std::string& table) {
    auto conn = m_pool.acquire();
    std::string sql =
        "SELECT t.table_name, "
        "       pg_size_pretty(pg_total_relation_size(quote_ident(t.table_name))) as total_size, "
        "       pg_total_relation_size(quote_ident(t.table_name)) as data_length, "
        "       c.reltuples::bigint as row_estimate "
        "FROM information_schema.tables t "
        "JOIN pg_class c ON c.relname = t.table_name "
        "JOIN pg_namespace n ON n.oid = c.relnamespace AND n.nspname = t.table_schema "
        "WHERE t.table_schema = 'public' "
        "AND t.table_name = '" + escapeString(table) + "'";

    PostgreSQLResultSet result(conn->execute(sql));

    if (!result.hasData() || !result.fetchRow()) {
        return std::nullopt;
    }

    TableInfo info;
    info.database = database;
    info.name = result.getField(0) ? result.getField(0) : "";
    info.engine = "PostgreSQL";  // No engine concept in PostgreSQL
    info.dataLength = result.getField(2) ? std::stoull(result.getField(2)) : 0;
    info.rowsEstimate = result.getField(3) ? std::stoull(result.getField(3)) : 0;

    info.columns = getColumns(database, table);
    info.indexes = getIndexes(database, table);

    // Find primary key column
    info.primaryKeyColumn = getPrimaryKeyColumn(database, table);

    return info;
}

// ============================================================================
// View Operations
// ============================================================================

std::vector<std::string> PostgreSQLSchemaManager::getViews(const std::string& database) {
    std::string cache_key = CacheManager::makeKey(database, "views");

    if (auto cached = m_cache.get(cache_key)) {
        std::vector<std::string> result;
        std::istringstream iss(*cached);
        std::string line;
        while (std::getline(iss, line)) {
            if (!line.empty()) result.push_back(line);
        }
        return result;
    }

    std::vector<std::string> views;

    auto conn = m_pool.acquire();
    PostgreSQLResultSet result(conn->execute(
        "SELECT table_name FROM information_schema.views "
        "WHERE table_schema = 'public' "
        "ORDER BY table_name"));

    if (!result.hasData()) {
        return views;
    }

    while (result.fetchRow()) {
        const char* name = result.getField(0);
        if (name) {
            views.emplace_back(name);
        }
    }

    std::ostringstream oss;
    for (const auto& v : views) {
        oss << v << "\n";
    }
    m_cache.put(cache_key, oss.str(), CacheManager::Category::Schema);

    return views;
}

std::optional<ViewInfo> PostgreSQLSchemaManager::getViewInfo(const std::string& database,
                                                               const std::string& view) {
    auto conn = m_pool.acquire();
    std::string sql =
        "SELECT table_name, view_definition, is_updatable, check_option "
        "FROM information_schema.views "
        "WHERE table_schema = 'public' "
        "AND table_name = '" + escapeString(view) + "'";

    PostgreSQLResultSet result(conn->execute(sql));

    if (!result.hasData() || !result.fetchRow()) {
        return std::nullopt;
    }

    ViewInfo info;
    info.database = database;
    info.name = result.getField(0) ? result.getField(0) : "";
    // Note: view_definition is available but not in ViewInfo struct
    info.isUpdatable = result.getField(2) && std::string(result.getField(2)) == "YES";
    info.checkOption = result.getField(3) ? result.getField(3) : "";

    return info;
}

// ============================================================================
// Routine Operations (Procedures and Functions)
// ============================================================================

std::vector<std::string> PostgreSQLSchemaManager::getProcedures(const std::string& database) {
    std::vector<std::string> procedures;

    auto conn = m_pool.acquire();
    PostgreSQLResultSet result(conn->execute(
        "SELECT routine_name FROM information_schema.routines "
        "WHERE routine_schema = 'public' "
        "AND routine_type = 'PROCEDURE' "
        "ORDER BY routine_name"));

    if (!result.hasData()) {
        return procedures;
    }

    while (result.fetchRow()) {
        const char* name = result.getField(0);
        if (name) {
            procedures.emplace_back(name);
        }
    }

    return procedures;
}

std::vector<std::string> PostgreSQLSchemaManager::getFunctions(const std::string& database) {
    std::vector<std::string> functions;

    auto conn = m_pool.acquire();
    PostgreSQLResultSet result(conn->execute(
        "SELECT routine_name FROM information_schema.routines "
        "WHERE routine_schema = 'public' "
        "AND routine_type = 'FUNCTION' "
        "ORDER BY routine_name"));

    if (!result.hasData()) {
        return functions;
    }

    while (result.fetchRow()) {
        const char* name = result.getField(0);
        if (name) {
            functions.emplace_back(name);
        }
    }

    return functions;
}

std::optional<RoutineInfo> PostgreSQLSchemaManager::getRoutineInfo(const std::string& database,
                                                                     const std::string& name,
                                                                     const std::string& type) {
    auto conn = m_pool.acquire();
    std::string sql =
        "SELECT routine_name, routine_type, data_type, security_type, "
        "       is_deterministic, routine_definition "
        "FROM information_schema.routines "
        "WHERE routine_schema = 'public' "
        "AND routine_name = '" + escapeString(name) + "' "
        "AND routine_type = '" + escapeString(type) + "'";

    PostgreSQLResultSet result(conn->execute(sql));

    if (!result.hasData() || !result.fetchRow()) {
        return std::nullopt;
    }

    RoutineInfo info;
    info.database = database;
    info.name = result.getField(0) ? result.getField(0) : "";
    info.type = result.getField(1) ? result.getField(1) : "";
    info.returns = result.getField(2) ? result.getField(2) : "";
    info.securityType = result.getField(3) ? result.getField(3) : "";
    info.deterministic = result.getField(4) && std::string(result.getField(4)) == "YES";
    // Note: routine_definition is available but stored in comment for compatibility
    info.comment = result.getField(5) ? result.getField(5) : "";

    return info;
}

// ============================================================================
// Trigger Operations
// ============================================================================

std::vector<std::string> PostgreSQLSchemaManager::getTriggers(const std::string& database) {
    std::vector<std::string> triggers;

    auto conn = m_pool.acquire();
    PostgreSQLResultSet result(conn->execute(
        "SELECT trigger_name FROM information_schema.triggers "
        "WHERE trigger_schema = 'public' "
        "ORDER BY trigger_name"));

    if (!result.hasData()) {
        return triggers;
    }

    while (result.fetchRow()) {
        const char* name = result.getField(0);
        if (name) {
            triggers.emplace_back(name);
        }
    }

    return triggers;
}

std::optional<TriggerInfo> PostgreSQLSchemaManager::getTriggerInfo(const std::string& database,
                                                                     const std::string& trigger) {
    auto conn = m_pool.acquire();
    std::string sql =
        "SELECT trigger_name, event_object_table, event_manipulation, "
        "       action_timing, action_statement "
        "FROM information_schema.triggers "
        "WHERE trigger_schema = 'public' "
        "AND trigger_name = '" + escapeString(trigger) + "'";

    PostgreSQLResultSet result(conn->execute(sql));

    if (!result.hasData() || !result.fetchRow()) {
        return std::nullopt;
    }

    TriggerInfo info;
    info.database = database;
    info.name = result.getField(0) ? result.getField(0) : "";
    info.table = result.getField(1) ? result.getField(1) : "";
    info.event = result.getField(2) ? result.getField(2) : "";
    info.timing = result.getField(3) ? result.getField(3) : "";
    info.statement = result.getField(4) ? result.getField(4) : "";

    return info;
}

// ============================================================================
// DDL Statement Generation
// ============================================================================

std::string PostgreSQLSchemaManager::getCreateStatement(const std::string& database,
                                                          const std::string& object,
                                                          const std::string& type) {
    auto conn = m_pool.acquire();
    std::string sql;

    if (type == "TABLE") {
        // PostgreSQL doesn't have a simple SHOW CREATE TABLE
        // We need to reconstruct it from information_schema
        sql = "SELECT 'CREATE TABLE ' || quote_ident(table_name) || ' (' || "
              "string_agg(column_name || ' ' || data_type || "
              "  CASE WHEN character_maximum_length IS NOT NULL "
              "       THEN '(' || character_maximum_length || ')' ELSE '' END || "
              "  CASE WHEN is_nullable = 'NO' THEN ' NOT NULL' ELSE '' END, ', ') || "
              "');' "
              "FROM information_schema.columns "
              "WHERE table_schema = 'public' "
              "AND table_name = '" + escapeString(object) + "' "
              "GROUP BY table_name";
    } else if (type == "VIEW") {
        sql = "SELECT 'CREATE VIEW ' || quote_ident(table_name) || ' AS ' || view_definition "
              "FROM information_schema.views "
              "WHERE table_schema = 'public' "
              "AND table_name = '" + escapeString(object) + "'";
    } else if (type == "FUNCTION") {
        sql = "SELECT pg_get_functiondef(p.oid) "
              "FROM pg_proc p "
              "JOIN pg_namespace n ON n.oid = p.pronamespace "
              "WHERE n.nspname = 'public' "
              "AND p.proname = '" + escapeString(object) + "' "
              "LIMIT 1";
    } else {
        return "";
    }

    PostgreSQLResultSet result(conn->execute(sql));

    if (!result.hasData() || !result.fetchRow()) {
        return "";
    }

    const char* stmt = result.getField(0);
    return stmt ? stmt : "";
}

// ============================================================================
// Server Information
// ============================================================================

ServerInfo PostgreSQLSchemaManager::getServerInfo() {
    ServerInfo info;

    auto conn = m_pool.acquire();

    // Get version info
    PostgreSQLResultSet result(conn->execute(
        "SELECT version(), inet_server_addr()::text, inet_server_port()"));

    if (result.hasData() && result.fetchRow()) {
        info.version = result.getField(0) ? result.getField(0) : "";
        info.hostname = result.getField(1) ? result.getField(1) : "";
        info.port = result.getField(2) ? static_cast<uint16_t>(std::stoi(result.getField(2))) : 5432;
    }

    // Get uptime (approximate from pg_postmaster_start_time)
    PostgreSQLResultSet uptimeResult(conn->execute(
        "SELECT EXTRACT(EPOCH FROM (now() - pg_postmaster_start_time()))::bigint"));

    if (uptimeResult.hasData() && uptimeResult.fetchRow()) {
        info.uptime = uptimeResult.getField(0) ? std::stoull(uptimeResult.getField(0)) : 0;
    }

    // Get connection stats
    PostgreSQLResultSet statsResult(conn->execute(
        "SELECT count(*) FROM pg_stat_activity WHERE state IS NOT NULL"));

    if (statsResult.hasData() && statsResult.fetchRow()) {
        info.threadsConnected = statsResult.getField(0) ? std::stoull(statsResult.getField(0)) : 0;
    }

    return info;
}

std::vector<UserInfo> PostgreSQLSchemaManager::getUsers() {
    std::vector<UserInfo> users;

    auto conn = m_pool.acquire();
    PostgreSQLResultSet result(conn->execute(
        "SELECT usename, CASE WHEN passwd IS NULL THEN 'N' ELSE 'Y' END as has_pass, "
        "       valuntil "
        "FROM pg_user ORDER BY usename"));

    if (!result.hasData()) {
        return users;
    }

    while (result.fetchRow()) {
        UserInfo user;
        user.user = result.getField(0) ? result.getField(0) : "";
        user.host = "%";  // PostgreSQL doesn't have host-based user distinction like MySQL
        users.push_back(std::move(user));
    }

    return users;
}

// ============================================================================
// Server Variables
// ============================================================================

std::unordered_map<std::string, std::string> PostgreSQLSchemaManager::getGlobalVariables() {
    std::unordered_map<std::string, std::string> vars;

    auto conn = m_pool.acquire();
    PostgreSQLResultSet result(conn->execute("SHOW ALL"));

    if (!result.hasData()) {
        return vars;
    }

    while (result.fetchRow()) {
        const char* name = result.getField(0);
        const char* value = result.getField(1);
        if (name) {
            vars[name] = value ? value : "";
        }
    }

    return vars;
}

std::unordered_map<std::string, std::string> PostgreSQLSchemaManager::getSessionVariables() {
    // In PostgreSQL, session variables are the same as SHOW ALL for current session
    return getGlobalVariables();
}

// ============================================================================
// Row Operations
// ============================================================================

std::vector<std::string> PostgreSQLSchemaManager::getRowIds(const std::string& database,
                                                              const std::string& table,
                                                              size_t limit,
                                                              size_t offset) {
    std::vector<std::string> ids;

    // Get primary key column
    std::string pk_col = getPrimaryKeyColumn(database, table);
    if (pk_col.empty()) {
        return ids;
    }

    auto conn = m_pool.acquire();
    std::string sql = "SELECT " + escapeIdentifier(pk_col) +
                      " FROM " + escapeIdentifier(table) +
                      " ORDER BY " + escapeIdentifier(pk_col) +
                      " LIMIT " + std::to_string(limit) +
                      " OFFSET " + std::to_string(offset);

    PostgreSQLResultSet result(conn->execute(sql));

    if (!result.hasData()) {
        return ids;
    }

    while (result.fetchRow()) {
        const char* id = result.getField(0);
        if (id) {
            ids.emplace_back(id);
        }
    }

    return ids;
}

uint64_t PostgreSQLSchemaManager::getRowCount(const std::string& database, const std::string& table) {
    auto conn = m_pool.acquire();
    std::string sql = "SELECT COUNT(*) FROM " + escapeIdentifier(table);

    PostgreSQLResultSet result(conn->execute(sql));

    if (result.hasData() && result.fetchRow()) {
        const char* count = result.getField(0);
        if (count) {
            return std::stoull(count);
        }
    }

    return 0;
}

// ============================================================================
// Cache Invalidation
// ============================================================================

void PostgreSQLSchemaManager::invalidateTable(const std::string& database, const std::string& table) {
    m_cache.invalidateTable(database, table);
}

void PostgreSQLSchemaManager::invalidateDatabase(const std::string& database) {
    m_cache.invalidateDatabase(database);
}

void PostgreSQLSchemaManager::invalidateAll() {
    m_cache.clear();
}

}  // namespace sqlfuse
