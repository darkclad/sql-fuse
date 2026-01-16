#include "MySQLSchemaManager.hpp"
#include "MySQLResultSet.hpp"
#include "ErrorHandler.hpp"
#include <spdlog/spdlog.h>
#include <sstream>
#include <map>

namespace sqlfuse {

MySQLSchemaManager::MySQLSchemaManager(MySQLConnectionPool& pool, CacheManager& cache)
    : m_pool(pool), m_cache(cache) {
}

std::string MySQLSchemaManager::escapeIdentifier(const std::string& id) const {
    std::string result = "`";
    for (char c : id) {
        if (c == '`') result += "``";
        else result += c;
    }
    result += "`";
    return result;
}

std::string MySQLSchemaManager::escapeString(const std::string& str) const {
    std::string result;
    result.reserve(str.size() * 2);
    for (char c : str) {
        switch (c) {
            case '\0': result += "\\0"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\\': result += "\\\\"; break;
            case '\'': result += "\\'"; break;
            case '"':  result += "\\\""; break;
            default:   result += c; break;
        }
    }
    return result;
}

std::vector<std::string> MySQLSchemaManager::getDatabases() {
    std::string cache_key = "databases";

    if (auto cached = m_cache.get(cache_key)) {
        // Parse cached value (newline-separated)
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
    if (!conn->query("SHOW DATABASES")) {
        throw MySQLException(conn->get());
    }

    MySQLResultSet result(conn->storeResult());
    MYSQL_ROW row;

    while ((row = result.fetchRow())) {
        if (row[0]) {
            databases.emplace_back(row[0]);
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

bool MySQLSchemaManager::databaseExists(const std::string& database) {
    auto databases = getDatabases();
    return std::find(databases.begin(), databases.end(), database) != databases.end();
}

std::vector<std::string> MySQLSchemaManager::getTables(const std::string& database) {
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
    std::string sql = "SHOW FULL TABLES FROM " + escapeIdentifier(database) +
                      " WHERE Table_type = 'BASE TABLE'";

    if (!conn->query(sql)) {
        throw MySQLException(conn->get());
    }

    MySQLResultSet result(conn->storeResult());
    MYSQL_ROW row;

    while ((row = result.fetchRow())) {
        if (row[0]) {
            tables.emplace_back(row[0]);
        }
    }

    std::ostringstream oss;
    for (const auto& t : tables) {
        oss << t << "\n";
    }
    m_cache.put(cache_key, oss.str(), CacheManager::Category::Schema);

    return tables;
}

bool MySQLSchemaManager::tableExists(const std::string& database, const std::string& table) {
    auto tables = getTables(database);
    return std::find(tables.begin(), tables.end(), table) != tables.end();
}

std::vector<ColumnInfo> MySQLSchemaManager::getColumns(const std::string& database,
                                                        const std::string& table) {
    std::vector<ColumnInfo> columns;

    auto conn = m_pool.acquire();
    std::string sql = "SELECT COLUMN_NAME, DATA_TYPE, COLUMN_TYPE, IS_NULLABLE, "
                      "COLUMN_DEFAULT, EXTRA, COLUMN_KEY, COLLATION_NAME, COLUMN_COMMENT, "
                      "ORDINAL_POSITION "
                      "FROM INFORMATION_SCHEMA.COLUMNS "
                      "WHERE TABLE_SCHEMA = '" + escapeString(database) + "' "
                      "AND TABLE_NAME = '" + escapeString(table) + "' "
                      "ORDER BY ORDINAL_POSITION";

    if (!conn->query(sql)) {
        throw MySQLException(conn->get());
    }

    MySQLResultSet result(conn->storeResult());
    MYSQL_ROW row;

    while ((row = result.fetchRow())) {
        ColumnInfo col;
        col.name = row[0] ? row[0] : "";
        col.type = row[1] ? row[1] : "";
        col.fullType = row[2] ? row[2] : "";
        col.nullable = row[3] && std::string(row[3]) == "YES";
        col.defaultValue = row[4] ? row[4] : "";
        col.extra = row[5] ? row[5] : "";
        col.key = row[6] ? row[6] : "";
        col.collation = row[7] ? row[7] : "";
        col.comment = row[8] ? row[8] : "";
        col.ordinalPosition = row[9] ? std::stoi(row[9]) : 0;

        columns.push_back(std::move(col));
    }

    return columns;
}

std::vector<IndexInfo> MySQLSchemaManager::getIndexes(const std::string& database,
                                                       const std::string& table) {
    std::vector<IndexInfo> indexes;

    auto conn = m_pool.acquire();
    std::string sql = "SHOW INDEX FROM " + escapeIdentifier(database) + "." +
                      escapeIdentifier(table);

    if (!conn->query(sql)) {
        throw MySQLException(conn->get());
    }

    MySQLResultSet result(conn->storeResult());
    MYSQL_ROW row;

    std::map<std::string, IndexInfo> index_map;

    while ((row = result.fetchRow())) {
        std::string key_name = row[2] ? row[2] : "";

        if (index_map.find(key_name) == index_map.end()) {
            IndexInfo idx;
            idx.name = key_name;
            idx.unique = row[1] && std::string(row[1]) == "0";
            idx.primary = key_name == "PRIMARY";
            idx.type = row[10] ? row[10] : "BTREE";
            idx.comment = row[11] ? row[11] : "";
            idx.cardinality = row[6] ? std::stoull(row[6]) : 0;
            index_map[key_name] = std::move(idx);
        }

        if (row[4]) {
            index_map[key_name].columns.emplace_back(row[4]);
        }
    }

    for (auto& [name, idx] : index_map) {
        indexes.push_back(std::move(idx));
    }

    return indexes;
}

std::optional<TableInfo> MySQLSchemaManager::getTableInfo(const std::string& database,
                                                           const std::string& table) {
    auto conn = m_pool.acquire();
    std::string sql = "SELECT TABLE_NAME, ENGINE, TABLE_COLLATION, TABLE_COMMENT, "
                      "CREATE_TIME, UPDATE_TIME, TABLE_ROWS, DATA_LENGTH, INDEX_LENGTH, "
                      "AUTO_INCREMENT "
                      "FROM INFORMATION_SCHEMA.TABLES "
                      "WHERE TABLE_SCHEMA = '" + escapeString(database) + "' "
                      "AND TABLE_NAME = '" + escapeString(table) + "'";

    if (!conn->query(sql)) {
        throw MySQLException(conn->get());
    }

    MySQLResultSet result(conn->storeResult());
    MYSQL_ROW row = result.fetchRow();

    if (!row) {
        return std::nullopt;
    }

    TableInfo info;
    info.database = database;
    info.name = row[0] ? row[0] : "";
    info.engine = row[1] ? row[1] : "";
    info.collation = row[2] ? row[2] : "";
    info.comment = row[3] ? row[3] : "";
    info.createTime = row[4] ? row[4] : "";
    info.updateTime = row[5] ? row[5] : "";
    info.rowsEstimate = row[6] ? std::stoull(row[6]) : 0;
    info.dataLength = row[7] ? std::stoull(row[7]) : 0;
    info.indexLength = row[8] ? std::stoull(row[8]) : 0;
    info.autoIncrement = row[9] ? std::stoull(row[9]) : 0;

    info.columns = getColumns(database, table);
    info.indexes = getIndexes(database, table);

    // Find primary key column
    for (const auto& col : info.columns) {
        if (col.key == "PRI") {
            info.primaryKeyColumn = col.name;
            break;
        }
    }

    return info;
}

std::vector<std::string> MySQLSchemaManager::getViews(const std::string& database) {
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
    std::string sql = "SHOW FULL TABLES FROM " + escapeIdentifier(database) +
                      " WHERE Table_type = 'VIEW'";

    if (!conn->query(sql)) {
        throw MySQLException(conn->get());
    }

    MySQLResultSet result(conn->storeResult());
    MYSQL_ROW row;

    while ((row = result.fetchRow())) {
        if (row[0]) {
            views.emplace_back(row[0]);
        }
    }

    std::ostringstream oss;
    for (const auto& v : views) {
        oss << v << "\n";
    }
    m_cache.put(cache_key, oss.str(), CacheManager::Category::Schema);

    return views;
}

std::optional<ViewInfo> MySQLSchemaManager::getViewInfo(const std::string& database,
                                                         const std::string& view) {
    auto conn = m_pool.acquire();
    std::string sql = "SELECT TABLE_NAME, DEFINER, SECURITY_TYPE, IS_UPDATABLE, CHECK_OPTION "
                      "FROM INFORMATION_SCHEMA.VIEWS "
                      "WHERE TABLE_SCHEMA = '" + escapeString(database) + "' "
                      "AND TABLE_NAME = '" + escapeString(view) + "'";

    if (!conn->query(sql)) {
        throw MySQLException(conn->get());
    }

    MySQLResultSet result(conn->storeResult());
    MYSQL_ROW row = result.fetchRow();

    if (!row) {
        return std::nullopt;
    }

    ViewInfo info;
    info.database = database;
    info.name = row[0] ? row[0] : "";
    info.definer = row[1] ? row[1] : "";
    info.securityType = row[2] ? row[2] : "";
    info.isUpdatable = row[3] && std::string(row[3]) == "YES";
    info.checkOption = row[4] ? row[4] : "";

    return info;
}

std::vector<std::string> MySQLSchemaManager::getProcedures(const std::string& database) {
    std::vector<std::string> procedures;

    auto conn = m_pool.acquire();
    std::string sql = "SELECT ROUTINE_NAME FROM INFORMATION_SCHEMA.ROUTINES "
                      "WHERE ROUTINE_SCHEMA = '" + escapeString(database) + "' "
                      "AND ROUTINE_TYPE = 'PROCEDURE'";

    if (!conn->query(sql)) {
        throw MySQLException(conn->get());
    }

    MySQLResultSet result(conn->storeResult());
    MYSQL_ROW row;

    while ((row = result.fetchRow())) {
        if (row[0]) {
            procedures.emplace_back(row[0]);
        }
    }

    return procedures;
}

std::vector<std::string> MySQLSchemaManager::getFunctions(const std::string& database) {
    std::vector<std::string> functions;

    auto conn = m_pool.acquire();
    std::string sql = "SELECT ROUTINE_NAME FROM INFORMATION_SCHEMA.ROUTINES "
                      "WHERE ROUTINE_SCHEMA = '" + escapeString(database) + "' "
                      "AND ROUTINE_TYPE = 'FUNCTION'";

    if (!conn->query(sql)) {
        throw MySQLException(conn->get());
    }

    MySQLResultSet result(conn->storeResult());
    MYSQL_ROW row;

    while ((row = result.fetchRow())) {
        if (row[0]) {
            functions.emplace_back(row[0]);
        }
    }

    return functions;
}

std::optional<RoutineInfo> MySQLSchemaManager::getRoutineInfo(const std::string& database,
                                                               const std::string& name,
                                                               const std::string& type) {
    auto conn = m_pool.acquire();
    std::string sql = "SELECT ROUTINE_NAME, ROUTINE_TYPE, DEFINER, DATA_TYPE, "
                      "SQL_DATA_ACCESS, SECURITY_TYPE, IS_DETERMINISTIC, ROUTINE_COMMENT, "
                      "CREATED, LAST_ALTERED "
                      "FROM INFORMATION_SCHEMA.ROUTINES "
                      "WHERE ROUTINE_SCHEMA = '" + escapeString(database) + "' "
                      "AND ROUTINE_NAME = '" + escapeString(name) + "' "
                      "AND ROUTINE_TYPE = '" + escapeString(type) + "'";

    if (!conn->query(sql)) {
        throw MySQLException(conn->get());
    }

    MySQLResultSet result(conn->storeResult());
    MYSQL_ROW row = result.fetchRow();

    if (!row) {
        return std::nullopt;
    }

    RoutineInfo info;
    info.database = database;
    info.name = row[0] ? row[0] : "";
    info.type = row[1] ? row[1] : "";
    info.definer = row[2] ? row[2] : "";
    info.returns = row[3] ? row[3] : "";
    info.dataAccess = row[4] ? row[4] : "";
    info.securityType = row[5] ? row[5] : "";
    info.deterministic = row[6] && std::string(row[6]) == "YES";
    info.comment = row[7] ? row[7] : "";
    info.created = row[8] ? row[8] : "";
    info.modified = row[9] ? row[9] : "";

    return info;
}

std::vector<std::string> MySQLSchemaManager::getTriggers(const std::string& database) {
    std::vector<std::string> triggers;

    auto conn = m_pool.acquire();
    std::string sql = "SELECT TRIGGER_NAME FROM INFORMATION_SCHEMA.TRIGGERS "
                      "WHERE TRIGGER_SCHEMA = '" + escapeString(database) + "'";

    if (!conn->query(sql)) {
        throw MySQLException(conn->get());
    }

    MySQLResultSet result(conn->storeResult());
    MYSQL_ROW row;

    while ((row = result.fetchRow())) {
        if (row[0]) {
            triggers.emplace_back(row[0]);
        }
    }

    return triggers;
}

std::optional<TriggerInfo> MySQLSchemaManager::getTriggerInfo(const std::string& database,
                                                               const std::string& trigger) {
    auto conn = m_pool.acquire();
    std::string sql = "SELECT TRIGGER_NAME, EVENT_OBJECT_TABLE, EVENT_MANIPULATION, "
                      "ACTION_TIMING, ACTION_STATEMENT, DEFINER, CREATED "
                      "FROM INFORMATION_SCHEMA.TRIGGERS "
                      "WHERE TRIGGER_SCHEMA = '" + escapeString(database) + "' "
                      "AND TRIGGER_NAME = '" + escapeString(trigger) + "'";

    if (!conn->query(sql)) {
        throw MySQLException(conn->get());
    }

    MySQLResultSet result(conn->storeResult());
    MYSQL_ROW row = result.fetchRow();

    if (!row) {
        return std::nullopt;
    }

    TriggerInfo info;
    info.database = database;
    info.name = row[0] ? row[0] : "";
    info.table = row[1] ? row[1] : "";
    info.event = row[2] ? row[2] : "";
    info.timing = row[3] ? row[3] : "";
    info.statement = row[4] ? row[4] : "";
    info.definer = row[5] ? row[5] : "";
    info.created = row[6] ? row[6] : "";

    return info;
}

std::string MySQLSchemaManager::getCreateStatement(const std::string& database,
                                                    const std::string& object,
                                                    const std::string& type) {
    auto conn = m_pool.acquire();
    std::string sql;

    if (type == "TABLE") {
        sql = "SHOW CREATE TABLE " + escapeIdentifier(database) + "." +
              escapeIdentifier(object);
    } else if (type == "VIEW") {
        sql = "SHOW CREATE VIEW " + escapeIdentifier(database) + "." +
              escapeIdentifier(object);
    } else if (type == "PROCEDURE") {
        sql = "SHOW CREATE PROCEDURE " + escapeIdentifier(database) + "." +
              escapeIdentifier(object);
    } else if (type == "FUNCTION") {
        sql = "SHOW CREATE FUNCTION " + escapeIdentifier(database) + "." +
              escapeIdentifier(object);
    } else if (type == "TRIGGER") {
        sql = "SHOW CREATE TRIGGER " + escapeIdentifier(database) + "." +
              escapeIdentifier(object);
    } else {
        return "";
    }

    if (!conn->query(sql)) {
        throw MySQLException(conn->get());
    }

    MySQLResultSet result(conn->storeResult());
    MYSQL_ROW row = result.fetchRow();

    if (!row) {
        return "";
    }

    // The CREATE statement is in different columns depending on type
    int col_idx = 1;  // Usually column 1 for most types
    if (type == "PROCEDURE" || type == "FUNCTION") {
        col_idx = 2;  // Column 2 for routines
    }

    unsigned int num_fields = result.numFields();
    if (static_cast<unsigned int>(col_idx) < num_fields && row[col_idx]) {
        return row[col_idx];
    }

    return "";
}

ServerInfo MySQLSchemaManager::getServerInfo() {
    ServerInfo info;

    auto conn = m_pool.acquire();

    // Get version info
    if (conn->query("SELECT VERSION(), @@hostname, @@port")) {
        MySQLResultSet result(conn->storeResult());
        MYSQL_ROW row = result.fetchRow();
        if (row) {
            info.version = row[0] ? row[0] : "";
            info.hostname = row[1] ? row[1] : "";
            info.port = row[2] ? static_cast<uint16_t>(std::stoi(row[2])) : 0;
        }
    }

    // Get status variables
    if (conn->query("SHOW GLOBAL STATUS WHERE Variable_name IN "
                    "('Uptime', 'Threads_connected', 'Threads_running', "
                    "'Questions', 'Slow_queries')")) {
        MySQLResultSet result(conn->storeResult());
        MYSQL_ROW row;
        while ((row = result.fetchRow())) {
            if (!row[0] || !row[1]) continue;
            std::string name = row[0];
            std::string value = row[1];

            if (name == "Uptime") info.uptime = std::stoull(value);
            else if (name == "Threads_connected") info.threadsConnected = std::stoull(value);
            else if (name == "Threads_running") info.threadsRunning = std::stoull(value);
            else if (name == "Questions") info.questions = std::stoull(value);
            else if (name == "Slow_queries") info.slowQueries = std::stoull(value);
        }
    }

    // Get version comment
    if (conn->query("SELECT @@version_comment")) {
        MySQLResultSet result(conn->storeResult());
        MYSQL_ROW row = result.fetchRow();
        if (row && row[0]) {
            info.versionComment = row[0];
        }
    }

    return info;
}

std::vector<UserInfo> MySQLSchemaManager::getUsers() {
    std::vector<UserInfo> users;

    auto conn = m_pool.acquire();
    std::string sql = "SELECT User, Host, account_locked, password_expired "
                      "FROM mysql.user ORDER BY User, Host";

    if (!conn->query(sql)) {
        // May not have permission - return empty
        spdlog::debug("Cannot query users: {}", conn->error());
        return users;
    }

    MySQLResultSet result(conn->storeResult());
    MYSQL_ROW row;

    while ((row = result.fetchRow())) {
        UserInfo user;
        user.user = row[0] ? row[0] : "";
        user.host = row[1] ? row[1] : "";
        user.accountLocked = row[2] && std::string(row[2]) == "Y";
        user.passwordExpired = row[3] ? row[3] : "";
        users.push_back(std::move(user));
    }

    return users;
}

std::unordered_map<std::string, std::string> MySQLSchemaManager::getGlobalVariables() {
    std::unordered_map<std::string, std::string> vars;

    auto conn = m_pool.acquire();
    if (!conn->query("SHOW GLOBAL VARIABLES")) {
        throw MySQLException(conn->get());
    }

    MySQLResultSet result(conn->storeResult());
    MYSQL_ROW row;

    while ((row = result.fetchRow())) {
        if (row[0]) {
            vars[row[0]] = row[1] ? row[1] : "";
        }
    }

    return vars;
}

std::unordered_map<std::string, std::string> MySQLSchemaManager::getSessionVariables() {
    std::unordered_map<std::string, std::string> vars;

    auto conn = m_pool.acquire();
    if (!conn->query("SHOW SESSION VARIABLES")) {
        throw MySQLException(conn->get());
    }

    MySQLResultSet result(conn->storeResult());
    MYSQL_ROW row;

    while ((row = result.fetchRow())) {
        if (row[0]) {
            vars[row[0]] = row[1] ? row[1] : "";
        }
    }

    return vars;
}

std::vector<std::string> MySQLSchemaManager::getRowIds(const std::string& database,
                                                        const std::string& table,
                                                        size_t limit,
                                                        size_t offset) {
    std::vector<std::string> ids;

    // Get primary key column
    auto table_info = getTableInfo(database, table);
    if (!table_info || table_info->primaryKeyColumn.empty()) {
        return ids;
    }

    auto conn = m_pool.acquire();
    std::string sql = "SELECT " + escapeIdentifier(table_info->primaryKeyColumn) +
                      " FROM " + escapeIdentifier(database) + "." + escapeIdentifier(table) +
                      " ORDER BY " + escapeIdentifier(table_info->primaryKeyColumn) +
                      " LIMIT " + std::to_string(limit) +
                      " OFFSET " + std::to_string(offset);

    if (!conn->query(sql)) {
        throw MySQLException(conn->get());
    }

    MySQLResultSet result(conn->storeResult());
    MYSQL_ROW row;

    while ((row = result.fetchRow())) {
        if (row[0]) {
            ids.emplace_back(row[0]);
        }
    }

    return ids;
}

uint64_t MySQLSchemaManager::getRowCount(const std::string& database, const std::string& table) {
    auto conn = m_pool.acquire();
    std::string sql = "SELECT COUNT(*) FROM " + escapeIdentifier(database) + "." +
                      escapeIdentifier(table);

    if (!conn->query(sql)) {
        throw MySQLException(conn->get());
    }

    MySQLResultSet result(conn->storeResult());
    MYSQL_ROW row = result.fetchRow();

    if (row && row[0]) {
        return std::stoull(row[0]);
    }

    return 0;
}

void MySQLSchemaManager::invalidateTable(const std::string& database, const std::string& table) {
    m_cache.invalidateTable(database, table);
}

void MySQLSchemaManager::invalidateDatabase(const std::string& database) {
    m_cache.invalidateDatabase(database);
}

void MySQLSchemaManager::invalidateAll() {
    m_cache.clear();
}

}  // namespace sqlfuse
