#include "PostgreSQLVirtualFile.hpp"
#include "PostgreSQLResultSet.hpp"
#include "PostgreSQLFormatConverter.hpp"
#include "FormatConverter.hpp"
#include "ErrorHandler.hpp"
#include <spdlog/spdlog.h>
#include <sstream>

namespace sqlfuse {

PostgreSQLVirtualFile::PostgreSQLVirtualFile(const ParsedPath& path,
                                   SchemaManager& schema,
                                   CacheManager& cache,
                                   const DataConfig& config)
    : VirtualFile(path, schema, cache, config) {
}

PostgreSQLConnectionPool* PostgreSQLVirtualFile::getPool() {
    return dynamic_cast<PostgreSQLConnectionPool*>(&m_schema.connectionPool());
}

std::string PostgreSQLVirtualFile::generateTableCSV() {
    auto* pool = getPool();
    if (!pool) {
        m_lastError = "No PostgreSQL connection pool";
        return "";
    }
    auto conn = pool->acquire();

    std::string sql = "SELECT * FROM \"" + m_path.object_name + "\"";
    if (m_config.max_rows_per_file > 0) {
        sql += " LIMIT " + std::to_string(m_config.max_rows_per_file);
    }

    PostgreSQLResultSet result(conn->execute(sql));

    if (!result.hasData()) {
        m_lastError = result.errorMessage();
        return "";
    }

    CSVOptions opts;
    opts.includeHeader = m_config.include_csv_header;

    return PostgreSQLFormatConverter::toCSV(result.get(), opts);
}

std::string PostgreSQLVirtualFile::generateTableJSON() {
    auto* pool = getPool();
    if (!pool) {
        m_lastError = "No PostgreSQL connection pool";
        return "";
    }
    auto conn = pool->acquire();

    std::string sql = "SELECT * FROM \"" + m_path.object_name + "\"";
    if (m_config.max_rows_per_file > 0) {
        sql += " LIMIT " + std::to_string(m_config.max_rows_per_file);
    }

    PostgreSQLResultSet result(conn->execute(sql));

    if (!result.hasData()) {
        m_lastError = result.errorMessage();
        return "[]";
    }

    JSONOptions opts;
    opts.pretty = m_config.pretty_json;

    return PostgreSQLFormatConverter::toJSON(result.get(), opts);
}

std::string PostgreSQLVirtualFile::generateRowJSON() {
    auto* pool = getPool();
    if (!pool) {
        m_lastError = "No PostgreSQL connection pool";
        return "{}";
    }

    auto table_info = m_schema.getTableInfo(m_path.database, m_path.object_name);
    if (!table_info || table_info->primaryKeyColumn.empty()) {
        return "{}";
    }

    auto conn = pool->acquire();

    std::string sql = "SELECT * FROM \"" + m_path.object_name +
                      "\" WHERE \"" + table_info->primaryKeyColumn + "\" = " +
                      conn->escapeString(m_path.row_id);

    PostgreSQLResultSet result(conn->execute(sql));

    if (!result.hasData() || result.numRows() == 0) {
        return "{}";
    }

    JSONOptions opts;
    opts.pretty = m_config.pretty_json;

    return PostgreSQLFormatConverter::rowToJSON(result.get(), 0, opts) + "\n";
}

std::string PostgreSQLVirtualFile::generateViewContent() {
    if (m_path.format == FileFormat::SQL) {
        return m_schema.getCreateStatement(m_path.database, m_path.object_name, "VIEW") + ";\n";
    }

    auto* pool = getPool();
    if (!pool) {
        m_lastError = "No PostgreSQL connection pool";
        return "";
    }

    // Generate data from view
    auto conn = pool->acquire();

    std::string sql = "SELECT * FROM \"" + m_path.object_name + "\"";
    if (m_config.max_rows_per_file > 0) {
        sql += " LIMIT " + std::to_string(m_config.max_rows_per_file);
    }

    PostgreSQLResultSet result(conn->execute(sql));

    if (!result.hasData()) {
        m_lastError = result.errorMessage();
        return "";
    }

    if (m_path.format == FileFormat::CSV) {
        CSVOptions opts;
        opts.includeHeader = m_config.include_csv_header;
        return PostgreSQLFormatConverter::toCSV(result.get(), opts);
    } else {
        JSONOptions opts;
        opts.pretty = m_config.pretty_json;
        return PostgreSQLFormatConverter::toJSON(result.get(), opts);
    }
}

std::string PostgreSQLVirtualFile::generateDatabaseInfo() {
    auto* pool = getPool();
    if (!pool) {
        m_lastError = "No PostgreSQL connection pool";
        return "";
    }

    auto conn = pool->acquire();

    std::string sql = "SELECT datname, pg_encoding_to_char(encoding), datcollate "
                      "FROM pg_database WHERE datname = current_database()";

    PostgreSQLResultSet result(conn->execute(sql));

    if (!result.hasData() || !result.fetchRow()) {
        return "Database not found\n";
    }

    std::ostringstream out;

    out << "Database: " << (result.getField(0) ? result.getField(0) : "") << "\n";
    out << "Encoding: " << (result.getField(1) ? result.getField(1) : "") << "\n";
    out << "Collation: " << (result.getField(2) ? result.getField(2) : "") << "\n";

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

std::string PostgreSQLVirtualFile::generateUserInfo() {
    auto* pool = getPool();
    if (!pool) {
        m_lastError = "No PostgreSQL connection pool";
        return "";
    }

    // m_path.object_name contains "user.info" or just "user"
    std::string username = m_path.object_name;

    // Remove .info suffix if present
    if (username.size() > 5 && username.substr(username.size() - 5) == ".info") {
        username = username.substr(0, username.size() - 5);
    }

    // PostgreSQL doesn't have host in user like MySQL, so just use username
    auto at_pos = username.find('@');
    if (at_pos != std::string::npos) {
        username = username.substr(0, at_pos);
    }

    auto conn = pool->acquire();

    std::string sql = "SELECT usename, usesysid, usecreatedb, usesuper, useconfig "
                      "FROM pg_user WHERE usename = " + conn->escapeString(username);

    PostgreSQLResultSet result(conn->execute(sql));

    if (!result.hasData() || !result.fetchRow()) {
        return "User not found\n";
    }

    std::ostringstream out;

    out << "User: " << (result.getField(0) ? result.getField(0) : "") << "\n";
    out << "User ID: " << (result.getField(1) ? result.getField(1) : "") << "\n";
    out << "Can Create DB: " << (result.getField(2) && std::string(result.getField(2)) == "t" ? "Yes" : "No") << "\n";
    out << "Superuser: " << (result.getField(3) && std::string(result.getField(3)) == "t" ? "Yes" : "No") << "\n";

    return out.str();
}

int PostgreSQLVirtualFile::handleTableWrite() {
    if (m_writeBuffer.empty()) {
        return 0;
    }

    auto* pool = getPool();
    if (!pool) {
        m_lastError = "No PostgreSQL connection pool";
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
            std::string sql = PostgreSQLFormatConverter::buildInsert(
                m_path.object_name, row, true);

            PostgreSQLResultSet result(conn->execute(sql));
            if (!result.isOk()) {
                m_lastError = result.errorMessage();
                return -EIO;
            }
        }

        return 0;

    } catch (const std::exception& e) {
        m_lastError = e.what();
        return -EINVAL;
    }
}

int PostgreSQLVirtualFile::handleRowWrite() {
    if (m_writeBuffer.empty()) {
        return 0;
    }

    auto* pool = getPool();
    if (!pool) {
        m_lastError = "No PostgreSQL connection pool";
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
            std::string checkSql = "SELECT 1 FROM \"" + m_path.object_name +
                                   "\" WHERE \"" + table_info->primaryKeyColumn + "\" = '" +
                                   PostgreSQLFormatConverter::escapeSQL(m_path.row_id) + "' LIMIT 1";
            PostgreSQLResultSet checkResult(conn->execute(checkSql));
            rowExists = checkResult.hasData() && checkResult.numRows() > 0;
        }

        std::string sql;

        if (rowExists) {
            // UPDATE existing row
            sql = PostgreSQLFormatConverter::buildUpdate(
                m_path.object_name,
                row,
                table_info->primaryKeyColumn,
                m_path.row_id,
                true);
        } else {
            // INSERT new row
            sql = PostgreSQLFormatConverter::buildInsert(
                m_path.object_name,
                row,
                true);
        }

        PostgreSQLResultSet result(conn->execute(sql));
        if (!result.isOk()) {
            m_lastError = result.errorMessage();
            return -EIO;
        }

        return 0;

    } catch (const std::exception& e) {
        m_lastError = e.what();
        return -EINVAL;
    }
}

}  // namespace sqlfuse
