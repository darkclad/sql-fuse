#include "SQLiteSchemaManager.hpp"
#include "CacheManager.hpp"
#include <spdlog/spdlog.h>
#include <sstream>
#include <filesystem>
#include <algorithm>

namespace sqlfuse {

// SQLiteSchemaManager implementation
SQLiteSchemaManager::SQLiteSchemaManager(SQLiteConnectionPool& pool, CacheManager& cache)
    : m_pool(pool), m_cache(cache) {
    // Database name is derived from the connection - we'll use "main" as SQLite's default
    m_databaseName = "main";
}

std::string SQLiteSchemaManager::escapeIdentifier(const std::string& id) const {
    // SQLite uses double quotes for identifiers
    std::string result = "\"";
    for (char c : id) {
        if (c == '"') result += "\"\"";
        else result += c;
    }
    result += "\"";
    return result;
}

std::string SQLiteSchemaManager::escapeString(const std::string& str) const {
    std::string result;
    result.reserve(str.size() * 2);
    for (char c : str) {
        if (c == '\'') result += "''";
        else result += c;
    }
    return result;
}

std::vector<std::string> SQLiteSchemaManager::getDatabases() {
    // SQLite has a single "main" database per file, plus any attached databases
    std::vector<std::string> databases;
    databases.push_back("main");

    auto conn = m_pool.acquire();
    if (!conn) return databases;

    sqlite3_stmt* stmt = conn->prepare("PRAGMA database_list");
    if (stmt) {
        SQLiteResultSet rs(stmt);
        while (rs.step()) {
            std::string name = rs.getString(1);  // Column 1 is the database name
            if (name != "main" && name != "temp") {
                databases.push_back(name);
            }
        }
    }

    return databases;
}

bool SQLiteSchemaManager::databaseExists(const std::string& database) {
    auto databases = getDatabases();
    return std::find(databases.begin(), databases.end(), database) != databases.end();
}

std::vector<std::string> SQLiteSchemaManager::getTables(const std::string& database) {
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
    if (!conn) return tables;

    std::string sql = "SELECT name FROM " + escapeIdentifier(database) +
                      ".sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%' ORDER BY name";

    sqlite3_stmt* stmt = conn->prepare(sql);
    if (stmt) {
        SQLiteResultSet rs(stmt);
        while (rs.step()) {
            tables.push_back(rs.getString(0));
        }
    }

    std::ostringstream oss;
    for (const auto& t : tables) {
        oss << t << "\n";
    }
    m_cache.put(cache_key, oss.str(), CacheManager::Category::Schema);

    return tables;
}

bool SQLiteSchemaManager::tableExists(const std::string& database, const std::string& table) {
    auto tables = getTables(database);
    return std::find(tables.begin(), tables.end(), table) != tables.end();
}

std::vector<ColumnInfo> SQLiteSchemaManager::getColumns(const std::string& database,
                                                         const std::string& table) {
    std::vector<ColumnInfo> columns;
    auto conn = m_pool.acquire();
    if (!conn) return columns;

    std::string sql = "PRAGMA " + escapeIdentifier(database) + ".table_info(" +
                      escapeIdentifier(table) + ")";

    sqlite3_stmt* stmt = conn->prepare(sql);
    if (stmt) {
        SQLiteResultSet rs(stmt);
        while (rs.step()) {
            ColumnInfo col;
            col.ordinalPosition = static_cast<int>(rs.getInt64(0)) + 1;  // cid is 0-based
            col.name = rs.getString(1);
            col.type = rs.getString(2);
            col.fullType = col.type;
            col.nullable = rs.getInt64(3) == 0;  // notnull column
            col.defaultValue = rs.getString(4);
            col.key = rs.getInt64(5) > 0 ? "PRI" : "";  // pk column
            columns.push_back(std::move(col));
        }
    }

    return columns;
}

std::vector<IndexInfo> SQLiteSchemaManager::getIndexes(const std::string& database,
                                                        const std::string& table) {
    std::vector<IndexInfo> indexes;
    auto conn = m_pool.acquire();
    if (!conn) return indexes;

    std::string sql = "PRAGMA " + escapeIdentifier(database) + ".index_list(" +
                      escapeIdentifier(table) + ")";

    sqlite3_stmt* stmt = conn->prepare(sql);
    if (stmt) {
        SQLiteResultSet rs(stmt);
        while (rs.step()) {
            IndexInfo idx;
            idx.name = rs.getString(1);
            idx.unique = rs.getInt64(2) != 0;
            idx.primary = idx.name.find("autoindex") != std::string::npos &&
                          idx.name.find("PRIMARY") != std::string::npos;
            idx.type = rs.getString(3);  // origin: c=CREATE INDEX, u=UNIQUE, pk=PRIMARY KEY

            // Get columns for this index
            std::string idx_sql = "PRAGMA " + escapeIdentifier(database) + ".index_info(" +
                                  escapeIdentifier(idx.name) + ")";
            sqlite3_stmt* idx_stmt = conn->prepare(idx_sql);
            if (idx_stmt) {
                SQLiteResultSet idx_rs(idx_stmt);
                while (idx_rs.step()) {
                    idx.columns.push_back(idx_rs.getString(2));  // Column name
                }
            }

            indexes.push_back(std::move(idx));
        }
    }

    return indexes;
}

std::string SQLiteSchemaManager::getPrimaryKeyColumn(const std::string& table) {
    auto conn = m_pool.acquire();
    if (!conn) return "";

    std::string sql = "PRAGMA table_info(" + escapeIdentifier(table) + ")";
    sqlite3_stmt* stmt = conn->prepare(sql);
    if (stmt) {
        SQLiteResultSet rs(stmt);
        while (rs.step()) {
            if (rs.getInt64(5) > 0) {  // pk column
                return rs.getString(1);  // name column
            }
        }
    }

    // If no explicit PK, check for rowid
    return "rowid";
}

std::optional<TableInfo> SQLiteSchemaManager::getTableInfo(const std::string& database,
                                                            const std::string& table) {
    if (!tableExists(database, table)) {
        return std::nullopt;
    }

    TableInfo info;
    info.database = database;
    info.name = table;
    info.engine = "SQLite";
    info.columns = getColumns(database, table);
    info.indexes = getIndexes(database, table);
    info.primaryKeyColumn = getPrimaryKeyColumn(table);

    // Get row count estimate
    auto conn = m_pool.acquire();
    if (conn) {
        std::string sql = "SELECT COUNT(*) FROM " + escapeIdentifier(database) + "." +
                          escapeIdentifier(table);
        sqlite3_stmt* stmt = conn->prepare(sql);
        if (stmt) {
            SQLiteResultSet rs(stmt);
            if (rs.step()) {
                info.rowsEstimate = static_cast<uint64_t>(rs.getInt64(0));
            }
        }
    }

    return info;
}

std::vector<std::string> SQLiteSchemaManager::getViews(const std::string& database) {
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
    if (!conn) return views;

    std::string sql = "SELECT name FROM " + escapeIdentifier(database) +
                      ".sqlite_master WHERE type='view' ORDER BY name";

    sqlite3_stmt* stmt = conn->prepare(sql);
    if (stmt) {
        SQLiteResultSet rs(stmt);
        while (rs.step()) {
            views.push_back(rs.getString(0));
        }
    }

    std::ostringstream oss;
    for (const auto& v : views) {
        oss << v << "\n";
    }
    m_cache.put(cache_key, oss.str(), CacheManager::Category::Schema);

    return views;
}

std::optional<ViewInfo> SQLiteSchemaManager::getViewInfo(const std::string& database,
                                                          const std::string& view) {
    auto conn = m_pool.acquire();
    if (!conn) return std::nullopt;

    std::string sql = "SELECT name, sql FROM " + escapeIdentifier(database) +
                      ".sqlite_master WHERE type='view' AND name='" + escapeString(view) + "'";

    sqlite3_stmt* stmt = conn->prepare(sql);
    if (!stmt) return std::nullopt;

    SQLiteResultSet rs(stmt);
    if (!rs.step()) return std::nullopt;

    ViewInfo info;
    info.database = database;
    info.name = rs.getString(0);
    info.isUpdatable = false;  // SQLite views are generally not updatable

    return info;
}

std::vector<std::string> SQLiteSchemaManager::getProcedures(const std::string& /*database*/) {
    // SQLite doesn't have stored procedures
    return {};
}

std::vector<std::string> SQLiteSchemaManager::getFunctions(const std::string& /*database*/) {
    // SQLite doesn't have user-defined stored functions (only C extensions)
    return {};
}

std::optional<RoutineInfo> SQLiteSchemaManager::getRoutineInfo(const std::string& /*database*/,
                                                                const std::string& /*name*/,
                                                                const std::string& /*type*/) {
    // SQLite doesn't have stored routines
    return std::nullopt;
}

std::vector<std::string> SQLiteSchemaManager::getTriggers(const std::string& database) {
    std::vector<std::string> triggers;
    auto conn = m_pool.acquire();
    if (!conn) return triggers;

    std::string sql = "SELECT name FROM " + escapeIdentifier(database) +
                      ".sqlite_master WHERE type='trigger' ORDER BY name";

    sqlite3_stmt* stmt = conn->prepare(sql);
    if (stmt) {
        SQLiteResultSet rs(stmt);
        while (rs.step()) {
            triggers.push_back(rs.getString(0));
        }
    }

    return triggers;
}

std::optional<TriggerInfo> SQLiteSchemaManager::getTriggerInfo(const std::string& database,
                                                                const std::string& trigger) {
    auto conn = m_pool.acquire();
    if (!conn) return std::nullopt;

    std::string sql = "SELECT name, tbl_name, sql FROM " + escapeIdentifier(database) +
                      ".sqlite_master WHERE type='trigger' AND name='" + escapeString(trigger) + "'";

    sqlite3_stmt* stmt = conn->prepare(sql);
    if (!stmt) return std::nullopt;

    SQLiteResultSet rs(stmt);
    if (!rs.step()) return std::nullopt;

    TriggerInfo info;
    info.database = database;
    info.name = rs.getString(0);
    info.table = rs.getString(1);
    info.statement = rs.getString(2);

    // Parse timing and event from the SQL (simplified)
    std::string triggerSql = info.statement;
    std::transform(triggerSql.begin(), triggerSql.end(), triggerSql.begin(), ::toupper);

    if (triggerSql.find("BEFORE") != std::string::npos) info.timing = "BEFORE";
    else if (triggerSql.find("AFTER") != std::string::npos) info.timing = "AFTER";
    else if (triggerSql.find("INSTEAD OF") != std::string::npos) info.timing = "INSTEAD OF";

    if (triggerSql.find("INSERT") != std::string::npos) info.event = "INSERT";
    else if (triggerSql.find("UPDATE") != std::string::npos) info.event = "UPDATE";
    else if (triggerSql.find("DELETE") != std::string::npos) info.event = "DELETE";

    return info;
}

std::string SQLiteSchemaManager::getCreateStatement(const std::string& database,
                                                     const std::string& object,
                                                     const std::string& type) {
    auto conn = m_pool.acquire();
    if (!conn) return "";

    std::string objType;
    if (type == "TABLE") objType = "table";
    else if (type == "VIEW") objType = "view";
    else if (type == "TRIGGER") objType = "trigger";
    else if (type == "INDEX") objType = "index";
    else return "";

    std::string sql = "SELECT sql FROM " + escapeIdentifier(database) +
                      ".sqlite_master WHERE type='" + objType +
                      "' AND name='" + escapeString(object) + "'";

    sqlite3_stmt* stmt = conn->prepare(sql);
    if (!stmt) return "";

    SQLiteResultSet rs(stmt);
    if (rs.step()) {
        return rs.getString(0);
    }

    return "";
}

ServerInfo SQLiteSchemaManager::getServerInfo() {
    ServerInfo info;

    info.version = sqlite3_libversion();
    info.versionComment = "SQLite";
    info.hostname = "localhost";
    info.port = 0;  // SQLite is file-based

    return info;
}

std::vector<UserInfo> SQLiteSchemaManager::getUsers() {
    // SQLite doesn't have users
    return {};
}

std::unordered_map<std::string, std::string> SQLiteSchemaManager::getGlobalVariables() {
    std::unordered_map<std::string, std::string> vars;
    auto conn = m_pool.acquire();
    if (!conn) return vars;

    // Get compile options
    sqlite3_stmt* stmt = conn->prepare("PRAGMA compile_options");
    if (stmt) {
        SQLiteResultSet rs(stmt);
        while (rs.step()) {
            std::string opt = rs.getString(0);
            vars["compile_" + opt] = "enabled";
        }
    }

    // Get some common pragmas
    const std::vector<std::string> pragmas = {
        "auto_vacuum", "cache_size", "encoding", "journal_mode",
        "page_size", "synchronous", "temp_store", "wal_autocheckpoint"
    };

    for (const auto& pragma : pragmas) {
        stmt = conn->prepare("PRAGMA " + pragma);
        if (stmt) {
            SQLiteResultSet rs(stmt);
            if (rs.step()) {
                vars[pragma] = rs.getString(0);
            }
        }
    }

    return vars;
}

std::unordered_map<std::string, std::string> SQLiteSchemaManager::getSessionVariables() {
    // SQLite doesn't distinguish between global and session variables
    return getGlobalVariables();
}

std::vector<std::string> SQLiteSchemaManager::getRowIds(const std::string& database,
                                                         const std::string& table,
                                                         size_t limit,
                                                         size_t offset) {
    std::vector<std::string> ids;

    std::string pkColumn = getPrimaryKeyColumn(table);
    if (pkColumn.empty()) pkColumn = "rowid";

    auto conn = m_pool.acquire();
    if (!conn) return ids;

    std::string sql = "SELECT " + escapeIdentifier(pkColumn) +
                      " FROM " + escapeIdentifier(database) + "." + escapeIdentifier(table) +
                      " ORDER BY " + escapeIdentifier(pkColumn) +
                      " LIMIT " + std::to_string(limit) +
                      " OFFSET " + std::to_string(offset);

    sqlite3_stmt* stmt = conn->prepare(sql);
    if (stmt) {
        SQLiteResultSet rs(stmt);
        while (rs.step()) {
            ids.push_back(rs.getString(0));
        }
    }

    return ids;
}

uint64_t SQLiteSchemaManager::getRowCount(const std::string& database, const std::string& table) {
    auto conn = m_pool.acquire();
    if (!conn) return 0;

    std::string sql = "SELECT COUNT(*) FROM " + escapeIdentifier(database) + "." +
                      escapeIdentifier(table);

    sqlite3_stmt* stmt = conn->prepare(sql);
    if (stmt) {
        SQLiteResultSet rs(stmt);
        if (rs.step()) {
            return static_cast<uint64_t>(rs.getInt64(0));
        }
    }

    return 0;
}

void SQLiteSchemaManager::invalidateTable(const std::string& database, const std::string& table) {
    m_cache.invalidateTable(database, table);
}

void SQLiteSchemaManager::invalidateDatabase(const std::string& database) {
    m_cache.invalidateDatabase(database);
}

void SQLiteSchemaManager::invalidateAll() {
    m_cache.clear();
}

}  // namespace sqlfuse
