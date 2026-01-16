#include "MySQLVirtualFile.hpp"
#include "MySQLResultSet.hpp"
#include "MySQLFormatConverter.hpp"
#include "FormatConverter.hpp"
#include "ErrorHandler.hpp"
#include <spdlog/spdlog.h>
#include <sstream>

namespace sqlfuse {

MySQLVirtualFile::MySQLVirtualFile(const ParsedPath& path,
                                   SchemaManager& schema,
                                   CacheManager& cache,
                                   const DataConfig& config)
    : VirtualFile(path, schema, cache, config) {
}

MySQLConnectionPool* MySQLVirtualFile::getPool() {
    return dynamic_cast<MySQLConnectionPool*>(&m_schema.connectionPool());
}

std::string MySQLVirtualFile::generateTableCSV() {
    auto* pool = getPool();
    if (!pool) {
        m_lastError = "No MySQL connection pool";
        return "";
    }
    auto conn = pool->acquire();

    std::string sql = "SELECT * FROM `" + m_path.database + "`.`" + m_path.object_name + "`";
    if (m_config.max_rows_per_file > 0) {
        sql += " LIMIT " + std::to_string(m_config.max_rows_per_file);
    }

    if (!conn->query(sql)) {
        throw MySQLException(conn->get());
    }

    MySQLResultSet result(conn->storeResult());

    CSVOptions opts;
    opts.includeHeader = m_config.include_csv_header;

    return MySQLFormatConverter::toCSV(result.get(), opts);
}

std::string MySQLVirtualFile::generateTableJSON() {
    auto* pool = getPool();
    if (!pool) {
        m_lastError = "No MySQL connection pool";
        return "";
    }
    auto conn = pool->acquire();

    std::string sql = "SELECT * FROM `" + m_path.database + "`.`" + m_path.object_name + "`";
    if (m_config.max_rows_per_file > 0) {
        sql += " LIMIT " + std::to_string(m_config.max_rows_per_file);
    }

    if (!conn->query(sql)) {
        throw MySQLException(conn->get());
    }

    MySQLResultSet result(conn->storeResult());

    JSONOptions opts;
    opts.pretty = m_config.pretty_json;

    return MySQLFormatConverter::toJSON(result.get(), opts);
}

std::string MySQLVirtualFile::generateRowJSON() {
    auto* pool = getPool();
    if (!pool) {
        m_lastError = "No MySQL connection pool";
        return "{}";
    }

    auto table_info = m_schema.getTableInfo(m_path.database, m_path.object_name);
    if (!table_info || table_info->primaryKeyColumn.empty()) {
        return "{}";
    }

    auto conn = pool->acquire();

    std::string sql = "SELECT * FROM `" + m_path.database + "`.`" + m_path.object_name +
                      "` WHERE `" + table_info->primaryKeyColumn + "` = '" +
                      MySQLFormatConverter::escapeSQL(m_path.row_id) + "'";

    if (!conn->query(sql)) {
        throw MySQLException(conn->get());
    }

    MySQLResultSet result(conn->storeResult());
    MYSQL_ROW row = result.fetchRow();

    if (!row) {
        return "{}";
    }

    JSONOptions opts;
    opts.pretty = m_config.pretty_json;

    return MySQLFormatConverter::rowToJSON(row, result.get(), opts) + "\n";
}

std::string MySQLVirtualFile::generateViewContent() {
    if (m_path.format == FileFormat::SQL) {
        return m_schema.getCreateStatement(m_path.database, m_path.object_name, "VIEW") + ";\n";
    }

    auto* pool = getPool();
    if (!pool) {
        m_lastError = "No MySQL connection pool";
        return "";
    }

    // Generate data from view
    auto conn = pool->acquire();

    std::string sql = "SELECT * FROM `" + m_path.database + "`.`" + m_path.object_name + "`";
    if (m_config.max_rows_per_file > 0) {
        sql += " LIMIT " + std::to_string(m_config.max_rows_per_file);
    }

    if (!conn->query(sql)) {
        throw MySQLException(conn->get());
    }

    MySQLResultSet result(conn->storeResult());

    if (m_path.format == FileFormat::CSV) {
        CSVOptions opts;
        opts.includeHeader = m_config.include_csv_header;
        return MySQLFormatConverter::toCSV(result.get(), opts);
    } else {
        JSONOptions opts;
        opts.pretty = m_config.pretty_json;
        return MySQLFormatConverter::toJSON(result.get(), opts);
    }
}

std::string MySQLVirtualFile::generateDatabaseInfo() {
    auto* pool = getPool();
    if (!pool) {
        m_lastError = "No MySQL connection pool";
        return "";
    }

    auto conn = pool->acquire();

    std::string sql = "SELECT SCHEMA_NAME, DEFAULT_CHARACTER_SET_NAME, DEFAULT_COLLATION_NAME "
                      "FROM INFORMATION_SCHEMA.SCHEMATA "
                      "WHERE SCHEMA_NAME = '" + MySQLFormatConverter::escapeSQL(m_path.database) + "'";

    if (!conn->query(sql)) {
        throw MySQLException(conn->get());
    }

    MySQLResultSet result(conn->storeResult());
    MYSQL_ROW row = result.fetchRow();

    if (!row) {
        return "Database not found\n";
    }

    std::ostringstream out;

    out << "Database: " << (row[0] ? row[0] : "") << "\n";
    out << "Character Set: " << (row[1] ? row[1] : "") << "\n";
    out << "Collation: " << (row[2] ? row[2] : "") << "\n";

    // Count objects
    auto tables = m_schema.getTables(m_path.database);
    auto views = m_schema.getViews(m_path.database);
    auto procedures = m_schema.getProcedures(m_path.database);
    auto functions = m_schema.getFunctions(m_path.database);
    auto triggers = m_schema.getTriggers(m_path.database);

    out << "\nObjects:\n";
    out << "  Tables: " << tables.size() << "\n";
    out << "  Views: " << views.size() << "\n";
    out << "  Procedures: " << procedures.size() << "\n";
    out << "  Functions: " << functions.size() << "\n";
    out << "  Triggers: " << triggers.size() << "\n";

    return out.str();
}

std::string MySQLVirtualFile::generateUserInfo() {
    auto* pool = getPool();
    if (!pool) {
        m_lastError = "No MySQL connection pool";
        return "";
    }

    // m_path.object_name contains "user@host.info"
    std::string user_host = m_path.object_name;

    // Remove .info suffix if present
    if (user_host.size() > 5 && user_host.substr(user_host.size() - 5) == ".info") {
        user_host = user_host.substr(0, user_host.size() - 5);
    }

    // Split user@host
    auto at_pos = user_host.find('@');
    if (at_pos == std::string::npos) {
        return "Invalid user format\n";
    }

    std::string user = user_host.substr(0, at_pos);
    std::string host = user_host.substr(at_pos + 1);

    auto conn = pool->acquire();

    std::string sql = "SELECT User, Host, account_locked, password_expired, "
                      "max_connections, max_user_connections "
                      "FROM mysql.user "
                      "WHERE User = '" + MySQLFormatConverter::escapeSQL(user) + "' "
                      "AND Host = '" + MySQLFormatConverter::escapeSQL(host) + "'";

    if (!conn->query(sql)) {
        return "Cannot query user information\n";
    }

    MySQLResultSet result(conn->storeResult());
    MYSQL_ROW row = result.fetchRow();

    if (!row) {
        return "User not found\n";
    }

    std::ostringstream out;

    out << "User: " << (row[0] ? row[0] : "") << "@" << (row[1] ? row[1] : "") << "\n";
    out << "Account Locked: " << (row[2] && std::string(row[2]) == "Y" ? "Yes" : "No") << "\n";
    out << "Password Expired: " << (row[3] ? row[3] : "N") << "\n";
    out << "Max Connections: " << (row[4] ? row[4] : "0") << "\n";
    out << "Max User Connections: " << (row[5] ? row[5] : "0") << "\n";

    return out.str();
}

int MySQLVirtualFile::handleTableWrite() {
    if (m_writeBuffer.empty()) {
        return 0;
    }

    auto* pool = getPool();
    if (!pool) {
        m_lastError = "No MySQL connection pool";
        return -EIO;
    }

    try {
        std::vector<RowData> rows;

        if (m_path.format == FileFormat::CSV) {
            CSVOptions opts;
            opts.includeHeader = true;  // Assume header in written data
            rows = FormatConverter::parseCSV(m_writeBuffer, opts);

        } else if (m_path.format == FileFormat::JSON) {
            rows = FormatConverter::parseJSON(m_writeBuffer);
        } else {
            return -EINVAL;
        }

        auto conn = pool->acquire();

        for (const auto& row : rows) {
            std::string sql = MySQLFormatConverter::buildInsert(
                m_path.database + "." + m_path.object_name, row, true);

            if (!conn->query(sql)) {
                m_lastError = conn->error();
                return -ErrorHandler::mysqlToErrno(conn->errorNumber());
            }
        }

        return 0;

    } catch (const std::exception& e) {
        m_lastError = e.what();
        return -EINVAL;
    }
}

int MySQLVirtualFile::handleRowWrite() {
    if (m_writeBuffer.empty()) {
        return 0;
    }

    auto* pool = getPool();
    if (!pool) {
        m_lastError = "No MySQL connection pool";
        return -EIO;
    }

    try {
        auto table_info = m_schema.getTableInfo(m_path.database, m_path.object_name);
        if (!table_info || table_info->primaryKeyColumn.empty()) {
            m_lastError = "No primary key defined";
            return -EINVAL;
        }

        RowData row = FormatConverter::parseJSONRow(m_writeBuffer);

        auto conn = pool->acquire();

        // Check if row exists (by checking if row_id is numeric and exists)
        bool rowExists = false;
        bool isNumericId = !m_path.row_id.empty() &&
                          m_path.row_id.find_first_not_of("0123456789") == std::string::npos;

        if (isNumericId) {
            std::string checkSql = "SELECT 1 FROM `" + m_path.database + "`.`" + m_path.object_name +
                                   "` WHERE `" + table_info->primaryKeyColumn + "` = '" +
                                   MySQLFormatConverter::escapeSQL(m_path.row_id) + "' LIMIT 1";
            if (conn->query(checkSql)) {
                MySQLResultSet result(conn->storeResult());
                rowExists = (result.fetchRow() != nullptr);
            }
        }

        std::string sql;

        if (rowExists) {
            // UPDATE existing row
            sql = MySQLFormatConverter::buildUpdate(
                m_path.database + "." + m_path.object_name,
                row,
                table_info->primaryKeyColumn,
                m_path.row_id,
                true);
        } else {
            // INSERT new row
            sql = MySQLFormatConverter::buildInsert(
                m_path.database + "." + m_path.object_name,
                row,
                true);
        }

        if (!conn->query(sql)) {
            m_lastError = conn->error();
            return -ErrorHandler::mysqlToErrno(conn->errorNumber());
        }

        return 0;

    } catch (const std::exception& e) {
        m_lastError = e.what();
        return -EINVAL;
    }
}

}  // namespace sqlfuse
