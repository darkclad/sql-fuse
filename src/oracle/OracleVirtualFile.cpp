#include "OracleVirtualFile.hpp"
#include "OracleResultSet.hpp"
#include "OracleFormatConverter.hpp"
#include "FormatConverter.hpp"
#include "ErrorHandler.hpp"
#include <spdlog/spdlog.h>
#include <sstream>

namespace sqlfuse {

OracleVirtualFile::OracleVirtualFile(const ParsedPath& path,
                                     SchemaManager& schema,
                                     CacheManager& cache,
                                     const DataConfig& config)
    : VirtualFile(path, schema, cache, config) {
}

OracleConnectionPool* OracleVirtualFile::getPool() {
    return dynamic_cast<OracleConnectionPool*>(&m_schema.connectionPool());
}

std::string OracleVirtualFile::generateTableCSV() {
    auto* pool = getPool();
    if (!pool) {
        m_lastError = "No Oracle connection pool";
        return "";
    }
    auto conn = pool->acquire();

    // Oracle uses schema.table format (database maps to schema)
    std::string sql = "SELECT * FROM " +
                      OracleFormatConverter::escapeIdentifier(m_path.database) + "." +
                      OracleFormatConverter::escapeIdentifier(m_path.object_name);
    if (m_config.max_rows_per_file > 0) {
        sql += " FETCH FIRST " + std::to_string(m_config.max_rows_per_file) + " ROWS ONLY";
    }

    OCIStmt* stmt = conn->execute(sql);
    if (!stmt) {
        throw std::runtime_error("Oracle query failed: " + conn->getError());
    }

    OracleResultSet result(stmt, conn->err(), conn->env());

    CSVOptions opts;
    opts.includeHeader = m_config.include_csv_header;

    return OracleFormatConverter::toCSV(result, opts);
}

std::string OracleVirtualFile::generateTableJSON() {
    auto* pool = getPool();
    if (!pool) {
        m_lastError = "No Oracle connection pool";
        return "";
    }
    auto conn = pool->acquire();

    std::string sql = "SELECT * FROM " +
                      OracleFormatConverter::escapeIdentifier(m_path.database) + "." +
                      OracleFormatConverter::escapeIdentifier(m_path.object_name);
    if (m_config.max_rows_per_file > 0) {
        sql += " FETCH FIRST " + std::to_string(m_config.max_rows_per_file) + " ROWS ONLY";
    }

    OCIStmt* stmt = conn->execute(sql);
    if (!stmt) {
        throw std::runtime_error("Oracle query failed: " + conn->getError());
    }

    OracleResultSet result(stmt, conn->err(), conn->env());

    JSONOptions opts;
    opts.pretty = m_config.pretty_json;

    return OracleFormatConverter::toJSON(result, opts);
}

std::string OracleVirtualFile::generateRowJSON() {
    auto* pool = getPool();
    if (!pool) {
        m_lastError = "No Oracle connection pool";
        return "{}";
    }

    auto table_info = m_schema.getTableInfo(m_path.database, m_path.object_name);
    if (!table_info || table_info->primaryKeyColumn.empty()) {
        return "{}";
    }

    auto conn = pool->acquire();

    std::string sql = "SELECT * FROM " +
                      OracleFormatConverter::escapeIdentifier(m_path.database) + "." +
                      OracleFormatConverter::escapeIdentifier(m_path.object_name) +
                      " WHERE " + OracleFormatConverter::escapeIdentifier(table_info->primaryKeyColumn) +
                      " = '" + OracleFormatConverter::escapeSQL(m_path.row_id) + "'";

    OCIStmt* stmt = conn->execute(sql);
    if (!stmt) {
        throw std::runtime_error("Oracle query failed: " + conn->getError());
    }

    OracleResultSet result(stmt, conn->err(), conn->env());

    if (!result.fetchRow()) {
        return "{}";
    }

    JSONOptions opts;
    opts.pretty = m_config.pretty_json;

    return OracleFormatConverter::rowToJSON(result, opts) + "\n";
}

std::string OracleVirtualFile::generateViewContent() {
    if (m_path.format == FileFormat::SQL) {
        return m_schema.getCreateStatement(m_path.database, m_path.object_name, "VIEW") + ";\n";
    }

    auto* pool = getPool();
    if (!pool) {
        m_lastError = "No Oracle connection pool";
        return "";
    }

    // Generate data from view
    auto conn = pool->acquire();

    std::string sql = "SELECT * FROM " +
                      OracleFormatConverter::escapeIdentifier(m_path.database) + "." +
                      OracleFormatConverter::escapeIdentifier(m_path.object_name);
    if (m_config.max_rows_per_file > 0) {
        sql += " FETCH FIRST " + std::to_string(m_config.max_rows_per_file) + " ROWS ONLY";
    }

    OCIStmt* stmt = conn->execute(sql);
    if (!stmt) {
        throw std::runtime_error("Oracle query failed: " + conn->getError());
    }

    OracleResultSet result(stmt, conn->err(), conn->env());

    if (m_path.format == FileFormat::CSV) {
        CSVOptions opts;
        opts.includeHeader = m_config.include_csv_header;
        return OracleFormatConverter::toCSV(result, opts);
    } else {
        JSONOptions opts;
        opts.pretty = m_config.pretty_json;
        return OracleFormatConverter::toJSON(result, opts);
    }
}

std::string OracleVirtualFile::generateDatabaseInfo() {
    auto* pool = getPool();
    if (!pool) {
        m_lastError = "No Oracle connection pool";
        return "";
    }

    auto conn = pool->acquire();

    // In Oracle, database maps to schema/user
    std::string sql = "SELECT username, account_status, created, default_tablespace, "
                      "temporary_tablespace FROM all_users WHERE username = '" +
                      OracleFormatConverter::escapeSQL(m_path.database) + "'";

    OCIStmt* stmt = conn->execute(sql);
    if (!stmt) {
        return "Schema not found\n";
    }

    OracleResultSet result(stmt, conn->err(), conn->env());

    if (!result.fetchRow()) {
        return "Schema not found\n";
    }

    std::ostringstream out;

    out << "Schema: " << (result.isNull(0) ? "" : result.getValue(0)) << "\n";
    out << "Account Status: " << (result.isNull(1) ? "" : result.getValue(1)) << "\n";
    out << "Created: " << (result.isNull(2) ? "" : result.getValue(2)) << "\n";
    out << "Default Tablespace: " << (result.isNull(3) ? "" : result.getValue(3)) << "\n";
    out << "Temporary Tablespace: " << (result.isNull(4) ? "" : result.getValue(4)) << "\n";

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

std::string OracleVirtualFile::generateUserInfo() {
    auto* pool = getPool();
    if (!pool) {
        m_lastError = "No Oracle connection pool";
        return "";
    }

    // m_path.object_name contains "username.info"
    std::string username = m_path.object_name;

    // Remove .info suffix if present
    if (username.size() > 5 && username.substr(username.size() - 5) == ".info") {
        username = username.substr(0, username.size() - 5);
    }

    auto conn = pool->acquire();

    std::string sql = "SELECT username, user_id, account_status, lock_date, expiry_date, "
                      "default_tablespace, temporary_tablespace, created, profile "
                      "FROM dba_users WHERE username = '" +
                      OracleFormatConverter::escapeSQL(username) + "'";

    OCIStmt* stmt = conn->execute(sql);
    if (!stmt) {
        // Try all_users if dba_users is not accessible
        sql = "SELECT username, user_id, account_status, created, default_tablespace "
              "FROM all_users WHERE username = '" +
              OracleFormatConverter::escapeSQL(username) + "'";
        stmt = conn->execute(sql);
        if (!stmt) {
            return "Cannot query user information\n";
        }

        OracleResultSet result(stmt, conn->err(), conn->env());
        if (!result.fetchRow()) {
            return "User not found\n";
        }

        std::ostringstream out;
        out << "User: " << (result.isNull(0) ? "" : result.getValue(0)) << "\n";
        out << "User ID: " << (result.isNull(1) ? "" : result.getValue(1)) << "\n";
        out << "Account Status: " << (result.isNull(2) ? "" : result.getValue(2)) << "\n";
        out << "Created: " << (result.isNull(3) ? "" : result.getValue(3)) << "\n";
        out << "Default Tablespace: " << (result.isNull(4) ? "" : result.getValue(4)) << "\n";
        return out.str();
    }

    OracleResultSet result(stmt, conn->err(), conn->env());
    if (!result.fetchRow()) {
        return "User not found\n";
    }

    std::ostringstream out;

    out << "User: " << (result.isNull(0) ? "" : result.getValue(0)) << "\n";
    out << "User ID: " << (result.isNull(1) ? "" : result.getValue(1)) << "\n";
    out << "Account Status: " << (result.isNull(2) ? "" : result.getValue(2)) << "\n";
    out << "Lock Date: " << (result.isNull(3) ? "" : result.getValue(3)) << "\n";
    out << "Expiry Date: " << (result.isNull(4) ? "" : result.getValue(4)) << "\n";
    out << "Default Tablespace: " << (result.isNull(5) ? "" : result.getValue(5)) << "\n";
    out << "Temporary Tablespace: " << (result.isNull(6) ? "" : result.getValue(6)) << "\n";
    out << "Created: " << (result.isNull(7) ? "" : result.getValue(7)) << "\n";
    out << "Profile: " << (result.isNull(8) ? "" : result.getValue(8)) << "\n";

    return out.str();
}

int OracleVirtualFile::handleTableWrite() {
    if (m_writeBuffer.empty()) {
        return 0;
    }

    auto* pool = getPool();
    if (!pool) {
        m_lastError = "No Oracle connection pool";
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
            std::string sql = OracleFormatConverter::buildInsert(
                m_path.database + "." + m_path.object_name, row, true);

            if (!conn->executeNonQuery(sql)) {
                m_lastError = conn->getError();
                return -ErrorHandler::oracleToErrno(conn->getErrorCode());
            }
        }

        // Commit the transaction
        conn->commit();

        return 0;

    } catch (const std::exception& e) {
        m_lastError = e.what();
        return -EINVAL;
    }
}

int OracleVirtualFile::handleRowWrite() {
    if (m_writeBuffer.empty()) {
        return 0;
    }

    auto* pool = getPool();
    if (!pool) {
        m_lastError = "No Oracle connection pool";
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
            std::string checkSql = "SELECT 1 FROM " +
                                   OracleFormatConverter::escapeIdentifier(m_path.database) + "." +
                                   OracleFormatConverter::escapeIdentifier(m_path.object_name) +
                                   " WHERE " + OracleFormatConverter::escapeIdentifier(table_info->primaryKeyColumn) +
                                   " = '" + OracleFormatConverter::escapeSQL(m_path.row_id) +
                                   "' FETCH FIRST 1 ROWS ONLY";
            OCIStmt* stmt = conn->execute(checkSql);
            if (stmt) {
                OracleResultSet result(stmt, conn->err(), conn->env());
                rowExists = result.fetchRow();
            }
        }

        std::string sql;

        if (rowExists) {
            // UPDATE existing row
            sql = OracleFormatConverter::buildUpdate(
                m_path.database + "." + m_path.object_name,
                row,
                table_info->primaryKeyColumn,
                m_path.row_id,
                true);
        } else {
            // INSERT new row
            sql = OracleFormatConverter::buildInsert(
                m_path.database + "." + m_path.object_name,
                row,
                true);
        }

        if (!conn->executeNonQuery(sql)) {
            m_lastError = conn->getError();
            return -ErrorHandler::oracleToErrno(conn->getErrorCode());
        }

        // Commit the transaction
        conn->commit();

        return 0;

    } catch (const std::exception& e) {
        m_lastError = e.what();
        return -EINVAL;
    }
}

}  // namespace sqlfuse
