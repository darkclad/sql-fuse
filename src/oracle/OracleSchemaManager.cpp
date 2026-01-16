#include "OracleSchemaManager.hpp"
#include "OracleResultSet.hpp"
#include "OracleFormatConverter.hpp"
#include <spdlog/spdlog.h>
#include <sstream>

namespace sqlfuse {

OracleSchemaManager::OracleSchemaManager(OracleConnectionPool& pool, CacheManager& cache)
    : m_pool(pool), m_cache(cache) {
}

std::string OracleSchemaManager::escapeIdentifier(const std::string& id) const {
    return OracleFormatConverter::escapeIdentifier(id);
}

std::string OracleSchemaManager::escapeString(const std::string& str) const {
    return OracleFormatConverter::escapeSQL(str);
}

std::string OracleSchemaManager::mapOracleType(const std::string& oracleType, int precision, int scale) const {
    // Map Oracle types to generic type names
    if (oracleType == "NUMBER") {
        if (scale == 0) {
            if (precision <= 10) return "INTEGER";
            return "BIGINT";
        }
        return "DECIMAL(" + std::to_string(precision) + "," + std::to_string(scale) + ")";
    }
    if (oracleType == "VARCHAR2" || oracleType == "NVARCHAR2") return "VARCHAR";
    if (oracleType == "CHAR" || oracleType == "NCHAR") return "CHAR";
    if (oracleType == "DATE") return "DATE";
    if (oracleType.find("TIMESTAMP") != std::string::npos) return "TIMESTAMP";
    if (oracleType == "CLOB" || oracleType == "NCLOB") return "TEXT";
    if (oracleType == "BLOB") return "BLOB";
    if (oracleType == "RAW") return "BINARY";
    if (oracleType == "FLOAT" || oracleType == "BINARY_FLOAT") return "FLOAT";
    if (oracleType == "BINARY_DOUBLE") return "DOUBLE";
    return oracleType;
}

std::vector<std::string> OracleSchemaManager::getDatabases() {
    // In Oracle, "databases" map to schemas/users
    std::vector<std::string> schemas;

    auto conn = m_pool.acquire();
    if (!conn) return schemas;

    // Get all accessible schemas (users with tables/views)
    std::string sql = R"(
        SELECT DISTINCT owner FROM all_tables
        WHERE owner NOT IN ('SYS', 'SYSTEM', 'OUTLN', 'DIP', 'ORACLE_OCM',
                            'DBSNMP', 'APPQOSSYS', 'WMSYS', 'EXFSYS', 'CTXSYS',
                            'XDB', 'ANONYMOUS', 'MDSYS', 'OLAPSYS', 'ORDDATA',
                            'ORDPLUGINS', 'ORDSYS', 'SI_INFORMTN_SCHEMA',
                            'LBACSYS', 'DVSYS', 'DVF', 'AUDSYS', 'GSMADMIN_INTERNAL',
                            'OJVMSYS', 'WKPROXY', 'WK_TEST')
        ORDER BY owner
    )";

    OCIStmt* stmt = conn->execute(sql);
    if (!stmt) return schemas;

    OracleResultSet result(stmt, conn->err(), conn->env());
    while (result.fetchRow()) {
        const char* schema = result.getValue(0);
        if (schema) {
            schemas.push_back(schema);
        }
    }

    return schemas;
}

bool OracleSchemaManager::databaseExists(const std::string& database) {
    auto conn = m_pool.acquire();
    if (!conn) return false;

    std::string sql = "SELECT 1 FROM all_users WHERE username = '" +
                      escapeString(database) + "'";

    OCIStmt* stmt = conn->execute(sql);
    if (!stmt) return false;

    OracleResultSet result(stmt, conn->err(), conn->env());
    return result.fetchRow();
}

std::vector<std::string> OracleSchemaManager::getTables(const std::string& database) {
    std::vector<std::string> tables;

    auto conn = m_pool.acquire();
    if (!conn) return tables;

    std::string sql = "SELECT table_name FROM all_tables WHERE owner = '" +
                      escapeString(database) + "' ORDER BY table_name";

    OCIStmt* stmt = conn->execute(sql);
    if (!stmt) return tables;

    OracleResultSet result(stmt, conn->err(), conn->env());
    while (result.fetchRow()) {
        const char* name = result.getValue(0);
        if (name) {
            tables.push_back(name);
        }
    }

    return tables;
}

std::optional<TableInfo> OracleSchemaManager::getTableInfo(const std::string& database,
                                                            const std::string& table) {
    auto conn = m_pool.acquire();
    if (!conn) return std::nullopt;

    // Get table info
    std::string sql = "SELECT table_name, num_rows, blocks, avg_row_len "
                      "FROM all_tables WHERE owner = '" + escapeString(database) +
                      "' AND table_name = '" + escapeString(table) + "'";

    OCIStmt* stmt = conn->execute(sql);
    if (!stmt) return std::nullopt;

    OracleResultSet result(stmt, conn->err(), conn->env());
    if (!result.fetchRow()) {
        return std::nullopt;
    }

    TableInfo info;
    info.name = table;
    info.database = database;

    const char* rows = result.getValue(1);
    info.rowsEstimate = rows ? std::stoull(rows) : 0;

    // Get primary key
    std::string pkSql = R"(
        SELECT cols.column_name
        FROM all_constraints cons
        JOIN all_cons_columns cols ON cons.constraint_name = cols.constraint_name
            AND cons.owner = cols.owner
        WHERE cons.constraint_type = 'P'
            AND cons.owner = ')" + escapeString(database) + R"('
            AND cons.table_name = ')" + escapeString(table) + R"('
        ORDER BY cols.position
    )";

    OCIStmt* pkStmt = conn->execute(pkSql);
    if (pkStmt) {
        OracleResultSet pkResult(pkStmt, conn->err(), conn->env());
        if (pkResult.fetchRow()) {
            const char* pkCol = pkResult.getValue(0);
            if (pkCol) {
                info.primaryKeyColumn = pkCol;
            }
        }
    }

    return info;
}

std::vector<ColumnInfo> OracleSchemaManager::getColumns(const std::string& database,
                                                         const std::string& table) {
    std::vector<ColumnInfo> columns;

    auto conn = m_pool.acquire();
    if (!conn) return columns;

    std::string sql = R"(
        SELECT column_name, data_type, data_length, data_precision, data_scale,
               nullable, column_id, data_default
        FROM all_tab_columns
        WHERE owner = ')" + escapeString(database) + R"('
            AND table_name = ')" + escapeString(table) + R"('
        ORDER BY column_id
    )";

    OCIStmt* stmt = conn->execute(sql);
    if (!stmt) return columns;

    OracleResultSet result(stmt, conn->err(), conn->env());
    while (result.fetchRow()) {
        ColumnInfo col;
        col.name = result.getValue(0) ? result.getValue(0) : "";
        std::string oracleType = result.getValue(1) ? result.getValue(1) : "";
        int precision = result.getValue(3) ? std::stoi(result.getValue(3)) : 0;
        int scale = result.getValue(4) ? std::stoi(result.getValue(4)) : 0;
        col.type = mapOracleType(oracleType, precision, scale);
        col.fullType = oracleType;
        if (precision > 0) {
            col.fullType += "(" + std::to_string(precision);
            if (scale > 0) col.fullType += "," + std::to_string(scale);
            col.fullType += ")";
        }
        col.nullable = result.getValue(5) && std::string(result.getValue(5)) == "Y";
        col.ordinalPosition = result.getValue(6) ? std::stoi(result.getValue(6)) : 0;
        col.defaultValue = result.getValue(7) ? result.getValue(7) : "";

        columns.push_back(std::move(col));
    }

    return columns;
}

std::vector<IndexInfo> OracleSchemaManager::getIndexes(const std::string& database,
                                                        const std::string& table) {
    std::vector<IndexInfo> indexes;

    auto conn = m_pool.acquire();
    if (!conn) return indexes;

    std::string sql = R"(
        SELECT i.index_name, i.uniqueness, ic.column_name, ic.column_position
        FROM all_indexes i
        JOIN all_ind_columns ic ON i.index_name = ic.index_name AND i.owner = ic.index_owner
        WHERE i.owner = ')" + escapeString(database) + R"('
            AND i.table_name = ')" + escapeString(table) + R"('
        ORDER BY i.index_name, ic.column_position
    )";

    OCIStmt* stmt = conn->execute(sql);
    if (!stmt) return indexes;

    OracleResultSet result(stmt, conn->err(), conn->env());

    std::string currentIndex;
    IndexInfo* currentInfo = nullptr;

    while (result.fetchRow()) {
        std::string indexName = result.getValue(0) ? result.getValue(0) : "";
        std::string uniqueness = result.getValue(1) ? result.getValue(1) : "";
        std::string colName = result.getValue(2) ? result.getValue(2) : "";

        if (indexName != currentIndex) {
            indexes.emplace_back();
            currentInfo = &indexes.back();
            currentInfo->name = indexName;
            currentInfo->unique = (uniqueness == "UNIQUE");
            currentIndex = indexName;
        }

        if (currentInfo) {
            currentInfo->columns.push_back(colName);
        }
    }

    return indexes;
}

bool OracleSchemaManager::tableExists(const std::string& database, const std::string& table) {
    auto conn = m_pool.acquire();
    if (!conn) return false;

    std::string sql = "SELECT 1 FROM all_tables WHERE owner = '" +
                      escapeString(database) + "' AND table_name = '" +
                      escapeString(table) + "'";

    OCIStmt* stmt = conn->execute(sql);
    if (!stmt) return false;

    OracleResultSet result(stmt, conn->err(), conn->env());
    return result.fetchRow();
}

std::vector<std::string> OracleSchemaManager::getViews(const std::string& database) {
    std::vector<std::string> views;

    auto conn = m_pool.acquire();
    if (!conn) return views;

    std::string sql = "SELECT view_name FROM all_views WHERE owner = '" +
                      escapeString(database) + "' ORDER BY view_name";

    OCIStmt* stmt = conn->execute(sql);
    if (!stmt) return views;

    OracleResultSet result(stmt, conn->err(), conn->env());
    while (result.fetchRow()) {
        const char* name = result.getValue(0);
        if (name) {
            views.push_back(name);
        }
    }

    return views;
}

std::optional<ViewInfo> OracleSchemaManager::getViewInfo(const std::string& database,
                                                          const std::string& view) {
    auto conn = m_pool.acquire();
    if (!conn) return std::nullopt;

    std::string sql = "SELECT view_name, text FROM all_views WHERE owner = '" +
                      escapeString(database) + "' AND view_name = '" +
                      escapeString(view) + "'";

    OCIStmt* stmt = conn->execute(sql);
    if (!stmt) return std::nullopt;

    OracleResultSet result(stmt, conn->err(), conn->env());
    if (!result.fetchRow()) {
        return std::nullopt;
    }

    ViewInfo info;
    info.name = result.getValue(0) ? result.getValue(0) : "";
    info.database = database;
    // Note: View definition stored via getCreateStatement(), ViewInfo doesn't have definition field

    return info;
}

std::vector<std::string> OracleSchemaManager::getProcedures(const std::string& database) {
    std::vector<std::string> procedures;

    auto conn = m_pool.acquire();
    if (!conn) return procedures;

    std::string sql = "SELECT object_name FROM all_procedures WHERE owner = '" +
                      escapeString(database) + "' AND object_type = 'PROCEDURE' ORDER BY object_name";

    OCIStmt* stmt = conn->execute(sql);
    if (!stmt) return procedures;

    OracleResultSet result(stmt, conn->err(), conn->env());
    while (result.fetchRow()) {
        const char* name = result.getValue(0);
        if (name) {
            procedures.push_back(name);
        }
    }

    return procedures;
}

std::vector<std::string> OracleSchemaManager::getFunctions(const std::string& database) {
    std::vector<std::string> functions;

    auto conn = m_pool.acquire();
    if (!conn) return functions;

    std::string sql = "SELECT object_name FROM all_procedures WHERE owner = '" +
                      escapeString(database) + "' AND object_type = 'FUNCTION' ORDER BY object_name";

    OCIStmt* stmt = conn->execute(sql);
    if (!stmt) return functions;

    OracleResultSet result(stmt, conn->err(), conn->env());
    while (result.fetchRow()) {
        const char* name = result.getValue(0);
        if (name) {
            functions.push_back(name);
        }
    }

    return functions;
}

std::optional<RoutineInfo> OracleSchemaManager::getRoutineInfo(const std::string& database,
                                                                const std::string& name,
                                                                const std::string& type) {
    auto conn = m_pool.acquire();
    if (!conn) return std::nullopt;

    std::string sql = "SELECT object_name, object_type, created, last_ddl_time "
                      "FROM all_objects WHERE owner = '" + escapeString(database) +
                      "' AND object_name = '" + escapeString(name) +
                      "' AND object_type = '" + escapeString(type) + "'";

    OCIStmt* stmt = conn->execute(sql);
    if (!stmt) return std::nullopt;

    OracleResultSet result(stmt, conn->err(), conn->env());
    if (!result.fetchRow()) {
        return std::nullopt;
    }

    RoutineInfo info;
    info.name = result.getValue(0) ? result.getValue(0) : "";
    info.database = database;
    info.type = result.getValue(1) ? result.getValue(1) : "";

    return info;
}

std::vector<std::string> OracleSchemaManager::getTriggers(const std::string& database) {
    std::vector<std::string> triggers;

    auto conn = m_pool.acquire();
    if (!conn) return triggers;

    std::string sql = "SELECT trigger_name FROM all_triggers WHERE owner = '" +
                      escapeString(database) + "' ORDER BY trigger_name";

    OCIStmt* stmt = conn->execute(sql);
    if (!stmt) return triggers;

    OracleResultSet result(stmt, conn->err(), conn->env());
    while (result.fetchRow()) {
        const char* name = result.getValue(0);
        if (name) {
            triggers.push_back(name);
        }
    }

    return triggers;
}

std::optional<TriggerInfo> OracleSchemaManager::getTriggerInfo(const std::string& database,
                                                                const std::string& trigger) {
    auto conn = m_pool.acquire();
    if (!conn) return std::nullopt;

    std::string sql = "SELECT trigger_name, trigger_type, triggering_event, table_name, "
                      "status, trigger_body FROM all_triggers WHERE owner = '" +
                      escapeString(database) + "' AND trigger_name = '" +
                      escapeString(trigger) + "'";

    OCIStmt* stmt = conn->execute(sql);
    if (!stmt) return std::nullopt;

    OracleResultSet result(stmt, conn->err(), conn->env());
    if (!result.fetchRow()) {
        return std::nullopt;
    }

    TriggerInfo info;
    info.name = result.getValue(0) ? result.getValue(0) : "";
    info.database = database;
    info.timing = result.getValue(1) ? result.getValue(1) : "";
    info.event = result.getValue(2) ? result.getValue(2) : "";
    info.table = result.getValue(3) ? result.getValue(3) : "";
    info.statement = result.getValue(5) ? result.getValue(5) : "";

    return info;
}

std::string OracleSchemaManager::getCreateStatement(const std::string& database,
                                                     const std::string& object,
                                                     const std::string& type) {
    auto conn = m_pool.acquire();
    if (!conn) return "";

    // Use DBMS_METADATA to get DDL
    std::string objType = type;
    if (type == "PROCEDURE" || type == "FUNCTION") {
        objType = type;
    } else if (type == "VIEW") {
        objType = "VIEW";
    } else if (type == "TRIGGER") {
        objType = "TRIGGER";
    } else {
        objType = "TABLE";
    }

    std::string sql = "SELECT DBMS_METADATA.GET_DDL('" + objType + "', '" +
                      escapeString(object) + "', '" + escapeString(database) + "') FROM dual";

    OCIStmt* stmt = conn->execute(sql);
    if (!stmt) {
        // Fallback for cases where DBMS_METADATA is not available
        if (type == "VIEW") {
            sql = "SELECT text FROM all_views WHERE owner = '" + escapeString(database) +
                  "' AND view_name = '" + escapeString(object) + "'";
            stmt = conn->execute(sql);
            if (!stmt) return "";

            OracleResultSet result(stmt, conn->err(), conn->env());
            if (result.fetchRow()) {
                return "CREATE OR REPLACE VIEW " + escapeIdentifier(database) + "." +
                       escapeIdentifier(object) + " AS\n" +
                       (result.getValue(0) ? result.getValue(0) : "");
            }
        }
        return "";
    }

    OracleResultSet result(stmt, conn->err(), conn->env());
    if (result.fetchRow()) {
        return result.getValue(0) ? result.getValue(0) : "";
    }

    return "";
}

ServerInfo OracleSchemaManager::getServerInfo() {
    ServerInfo info;
    info.versionComment = "Oracle Database";

    auto conn = m_pool.acquire();
    if (!conn) return info;

    // Get version
    std::string sql = "SELECT banner FROM v$version WHERE ROWNUM = 1";
    OCIStmt* stmt = conn->execute(sql);
    if (stmt) {
        OracleResultSet result(stmt, conn->err(), conn->env());
        if (result.fetchRow()) {
            info.version = result.getValue(0) ? result.getValue(0) : "";
        }
    }

    // Get instance name
    sql = "SELECT instance_name, host_name FROM v$instance";
    stmt = conn->execute(sql);
    if (stmt) {
        OracleResultSet result(stmt, conn->err(), conn->env());
        if (result.fetchRow()) {
            info.hostname = result.getValue(1) ? result.getValue(1) : "";
        }
    }

    return info;
}

std::vector<UserInfo> OracleSchemaManager::getUsers() {
    std::vector<UserInfo> users;

    auto conn = m_pool.acquire();
    if (!conn) return users;

    std::string sql = "SELECT username, account_status, created, lock_date, expiry_date "
                      "FROM all_users ORDER BY username";

    OCIStmt* stmt = conn->execute(sql);
    if (!stmt) return users;

    OracleResultSet result(stmt, conn->err(), conn->env());
    while (result.fetchRow()) {
        UserInfo user;
        user.user = result.getValue(0) ? result.getValue(0) : "";
        user.host = "localhost";  // Oracle doesn't have host-based users like MySQL

        users.push_back(std::move(user));
    }

    return users;
}

std::unordered_map<std::string, std::string> OracleSchemaManager::getGlobalVariables() {
    std::unordered_map<std::string, std::string> vars;

    auto conn = m_pool.acquire();
    if (!conn) return vars;

    std::string sql = "SELECT name, value FROM v$parameter ORDER BY name";

    OCIStmt* stmt = conn->execute(sql);
    if (!stmt) return vars;

    OracleResultSet result(stmt, conn->err(), conn->env());
    while (result.fetchRow()) {
        const char* name = result.getValue(0);
        const char* value = result.getValue(1);
        if (name) {
            vars[name] = value ? value : "";
        }
    }

    return vars;
}

std::unordered_map<std::string, std::string> OracleSchemaManager::getSessionVariables() {
    std::unordered_map<std::string, std::string> vars;

    auto conn = m_pool.acquire();
    if (!conn) return vars;

    std::string sql = "SELECT name, value FROM v$parameter ORDER BY name";

    OCIStmt* stmt = conn->execute(sql);
    if (!stmt) return vars;

    OracleResultSet result(stmt, conn->err(), conn->env());
    while (result.fetchRow()) {
        const char* name = result.getValue(0);
        const char* value = result.getValue(1);
        if (name) {
            vars[name] = value ? value : "";
        }
    }

    return vars;
}

std::vector<std::string> OracleSchemaManager::getRowIds(const std::string& database,
                                                         const std::string& table,
                                                         size_t limit,
                                                         size_t offset) {
    std::vector<std::string> ids;

    auto tableInfo = getTableInfo(database, table);
    if (!tableInfo || tableInfo->primaryKeyColumn.empty()) {
        return ids;
    }

    auto conn = m_pool.acquire();
    if (!conn) return ids;

    // Oracle pagination using OFFSET/FETCH (12c+) or ROWNUM
    std::string sql = "SELECT " + escapeIdentifier(tableInfo->primaryKeyColumn) +
                      " FROM " + escapeIdentifier(database) + "." + escapeIdentifier(table) +
                      " ORDER BY " + escapeIdentifier(tableInfo->primaryKeyColumn) +
                      " OFFSET " + std::to_string(offset) + " ROWS FETCH NEXT " +
                      std::to_string(limit) + " ROWS ONLY";

    OCIStmt* stmt = conn->execute(sql);
    if (!stmt) return ids;

    OracleResultSet result(stmt, conn->err(), conn->env());
    while (result.fetchRow()) {
        const char* id = result.getValue(0);
        if (id) {
            ids.push_back(id);
        }
    }

    return ids;
}

uint64_t OracleSchemaManager::getRowCount(const std::string& database, const std::string& table) {
    auto conn = m_pool.acquire();
    if (!conn) return 0;

    std::string sql = "SELECT COUNT(*) FROM " + escapeIdentifier(database) + "." +
                      escapeIdentifier(table);

    OCIStmt* stmt = conn->execute(sql);
    if (!stmt) return 0;

    OracleResultSet result(stmt, conn->err(), conn->env());
    if (result.fetchRow()) {
        const char* count = result.getValue(0);
        return count ? std::stoull(count) : 0;
    }

    return 0;
}

void OracleSchemaManager::invalidateTable(const std::string& database, const std::string& table) {
    // Cache invalidation - delegate to cache manager
    m_cache.invalidate("table:" + database + "." + table + "*");
}

void OracleSchemaManager::invalidateDatabase(const std::string& database) {
    m_cache.invalidate("*:" + database + ".*");
}

void OracleSchemaManager::invalidateAll() {
    m_cache.clear();
}

}  // namespace sqlfuse
