#include "SQLiteVirtualFile.hpp"
#include "SQLiteResultSet.hpp"
#include "SQLiteFormatConverter.hpp"
#include "FormatConverter.hpp"
#include "ErrorHandler.hpp"
#include <spdlog/spdlog.h>
#include <sstream>
#include <nlohmann/json.hpp>

namespace sqlfuse {

SQLiteVirtualFile::SQLiteVirtualFile(const ParsedPath& path,
                                     SchemaManager& schema,
                                     CacheManager& cache,
                                     const DataConfig& config)
    : VirtualFile(path, schema, cache, config) {
}

SQLiteConnectionPool* SQLiteVirtualFile::getPool() {
    return dynamic_cast<SQLiteConnectionPool*>(&m_schema.connectionPool());
}

std::string SQLiteVirtualFile::generateTableCSV() {
    auto* pool = getPool();
    if (!pool) {
        m_lastError = "No SQLite connection pool";
        return "";
    }

    auto conn = pool->acquire();

    std::string sql = "SELECT * FROM \"" + m_path.object_name + "\"";
    if (m_config.max_rows_per_file > 0) {
        sql += " LIMIT " + std::to_string(m_config.max_rows_per_file);
    }

    sqlite3_stmt* stmt = conn->prepare(sql);
    if (!stmt) {
        m_lastError = conn->error();
        return "";
    }

    SQLiteResultSet result(stmt);
    std::ostringstream out;

    // Header
    if (m_config.include_csv_header) {
        int colCount = result.columnCount();
        for (int i = 0; i < colCount; ++i) {
            if (i > 0) out << ",";
            out << "\"" << result.columnName(i) << "\"";
        }
        out << "\n";
    }

    // Rows
    while (result.step()) {
        int colCount = result.columnCount();
        for (int i = 0; i < colCount; ++i) {
            if (i > 0) out << ",";
            if (result.isNull(i)) {
                out << "";
            } else {
                std::string val = result.getString(i);
                // Escape quotes and wrap in quotes if contains special chars
                if (val.find(',') != std::string::npos ||
                    val.find('"') != std::string::npos ||
                    val.find('\n') != std::string::npos) {
                    // Replace " with ""
                    size_t pos = 0;
                    while ((pos = val.find('"', pos)) != std::string::npos) {
                        val.replace(pos, 1, "\"\"");
                        pos += 2;
                    }
                    out << "\"" << val << "\"";
                } else {
                    out << val;
                }
            }
        }
        out << "\n";
    }

    return out.str();
}

std::string SQLiteVirtualFile::generateTableJSON() {
    auto* pool = getPool();
    if (!pool) {
        m_lastError = "No SQLite connection pool";
        return "[]";
    }

    auto conn = pool->acquire();

    std::string sql = "SELECT * FROM \"" + m_path.object_name + "\"";
    if (m_config.max_rows_per_file > 0) {
        sql += " LIMIT " + std::to_string(m_config.max_rows_per_file);
    }

    sqlite3_stmt* stmt = conn->prepare(sql);
    if (!stmt) {
        m_lastError = conn->error();
        return "[]";
    }

    SQLiteResultSet result(stmt);
    nlohmann::json arr = nlohmann::json::array();

    while (result.step()) {
        nlohmann::json row;
        int colCount = result.columnCount();
        for (int i = 0; i < colCount; ++i) {
            std::string name = result.columnName(i);
            if (result.isNull(i)) {
                row[name] = nullptr;
            } else {
                row[name] = result.getString(i);
            }
        }
        arr.push_back(row);
    }

    if (m_config.pretty_json) {
        return arr.dump(2) + "\n";
    }
    return arr.dump() + "\n";
}

std::string SQLiteVirtualFile::generateRowJSON() {
    auto* pool = getPool();
    if (!pool) {
        m_lastError = "No SQLite connection pool";
        return "{}";
    }

    auto table_info = m_schema.getTableInfo(m_path.database, m_path.object_name);
    if (!table_info || table_info->primaryKeyColumn.empty()) {
        return "{}";
    }

    auto conn = pool->acquire();

    std::string sql = "SELECT * FROM \"" + m_path.object_name +
                      "\" WHERE \"" + table_info->primaryKeyColumn + "\" = '" +
                      SQLiteFormatConverter::escapeSQL(m_path.row_id) + "'";

    sqlite3_stmt* stmt = conn->prepare(sql);
    if (!stmt) {
        m_lastError = conn->error();
        return "{}";
    }

    SQLiteResultSet result(stmt);

    if (!result.step()) {
        return "{}";
    }

    nlohmann::json row;
    int colCount = result.columnCount();
    for (int i = 0; i < colCount; ++i) {
        std::string name = result.columnName(i);
        if (result.isNull(i)) {
            row[name] = nullptr;
        } else {
            row[name] = result.getString(i);
        }
    }

    if (m_config.pretty_json) {
        return row.dump(2) + "\n";
    }
    return row.dump() + "\n";
}

std::string SQLiteVirtualFile::generateViewContent() {
    if (m_path.format == FileFormat::SQL) {
        return m_schema.getCreateStatement(m_path.database, m_path.object_name, "VIEW") + ";\n";
    }

    auto* pool = getPool();
    if (!pool) {
        m_lastError = "No SQLite connection pool";
        return "";
    }

    auto conn = pool->acquire();

    std::string sql = "SELECT * FROM \"" + m_path.object_name + "\"";
    if (m_config.max_rows_per_file > 0) {
        sql += " LIMIT " + std::to_string(m_config.max_rows_per_file);
    }

    sqlite3_stmt* stmt = conn->prepare(sql);
    if (!stmt) {
        m_lastError = conn->error();
        return "";
    }

    SQLiteResultSet result(stmt);

    if (m_path.format == FileFormat::CSV) {
        std::ostringstream out;

        // Header
        if (m_config.include_csv_header) {
            int colCount = result.columnCount();
            for (int i = 0; i < colCount; ++i) {
                if (i > 0) out << ",";
                out << "\"" << result.columnName(i) << "\"";
            }
            out << "\n";
        }

        // Rows
        while (result.step()) {
            int colCount = result.columnCount();
            for (int i = 0; i < colCount; ++i) {
                if (i > 0) out << ",";
                if (!result.isNull(i)) {
                    out << result.getString(i);
                }
            }
            out << "\n";
        }

        return out.str();
    } else {
        // JSON
        nlohmann::json arr = nlohmann::json::array();

        while (result.step()) {
            nlohmann::json row;
            int colCount = result.columnCount();
            for (int i = 0; i < colCount; ++i) {
                std::string name = result.columnName(i);
                if (result.isNull(i)) {
                    row[name] = nullptr;
                } else {
                    row[name] = result.getString(i);
                }
            }
            arr.push_back(row);
        }

        if (m_config.pretty_json) {
            return arr.dump(2) + "\n";
        }
        return arr.dump() + "\n";
    }
}

std::string SQLiteVirtualFile::generateDatabaseInfo() {
    std::ostringstream out;

    out << "Database: " << m_path.database << "\n";

    // SQLite version
    auto* pool = getPool();
    if (pool) {
        auto conn = pool->acquire();
        sqlite3_stmt* stmt = conn->prepare("SELECT sqlite_version()");
        if (stmt) {
            SQLiteResultSet result(stmt);
            if (result.step()) {
                out << "SQLite Version: " << result.getString(0) << "\n";
            }
        }
    }

    // Count objects via schema manager
    auto tables = m_schema.getTables(m_path.database);
    auto views = m_schema.getViews(m_path.database);
    auto triggers = m_schema.getTriggers(m_path.database);

    out << "\nObjects:\n";
    out << "  Tables: " << tables.size() << "\n";
    out << "  Views: " << views.size() << "\n";
    out << "  Triggers: " << triggers.size() << "\n";

    return out.str();
}

std::string SQLiteVirtualFile::generateUserInfo() {
    // SQLite doesn't have user management
    return "User management is not available in SQLite\n";
}

int SQLiteVirtualFile::handleTableWrite() {
    if (m_writeBuffer.empty()) {
        return 0;
    }

    auto* pool = getPool();
    if (!pool) {
        m_lastError = "No SQLite connection pool";
        return -EIO;
    }

    try {
        std::vector<RowData> rows;

        if (m_path.format == FileFormat::CSV) {
            CSVOptions opts;
            opts.includeHeader = true;
            rows = FormatConverter::parseCSV(m_writeBuffer, opts);

        } else if (m_path.format == FileFormat::JSON) {
            rows = FormatConverter::parseJSON(m_writeBuffer);
        } else {
            return -EINVAL;
        }

        auto conn = pool->acquire();

        for (const auto& row : rows) {
            // Build INSERT OR REPLACE statement
            std::ostringstream sql;
            sql << "INSERT OR REPLACE INTO \"" << m_path.object_name << "\" (";

            bool first = true;
            for (const auto& [key, value] : row) {
                if (!first) sql << ", ";
                sql << "\"" << key << "\"";
                first = false;
            }

            sql << ") VALUES (";

            first = true;
            for (const auto& [key, value] : row) {
                if (!first) sql << ", ";
                if (value.has_value()) {
                    sql << "'" << SQLiteFormatConverter::escapeSQL(value.value()) << "'";
                } else {
                    sql << "NULL";
                }
                first = false;
            }

            sql << ")";

            if (!conn->execute(sql.str())) {
                m_lastError = conn->error();
                return -EIO;
            }
        }

        return 0;

    } catch (const std::exception& e) {
        m_lastError = e.what();
        return -EINVAL;
    }
}

int SQLiteVirtualFile::handleRowWrite() {
    if (m_writeBuffer.empty()) {
        return 0;
    }

    auto* pool = getPool();
    if (!pool) {
        m_lastError = "No SQLite connection pool";
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
                                   SQLiteFormatConverter::escapeSQL(m_path.row_id) + "'";
            sqlite3_stmt* stmt = conn->prepare(checkSql);
            if (stmt) {
                SQLiteResultSet result(stmt);
                rowExists = result.step();
            }
        }

        std::ostringstream sql;

        if (rowExists) {
            // UPDATE existing row
            sql << "UPDATE \"" << m_path.object_name << "\" SET ";

            bool first = true;
            for (const auto& [key, value] : row) {
                if (key == table_info->primaryKeyColumn) continue;
                if (!first) sql << ", ";
                if (value.has_value()) {
                    sql << "\"" << key << "\" = '" << SQLiteFormatConverter::escapeSQL(value.value()) << "'";
                } else {
                    sql << "\"" << key << "\" = NULL";
                }
                first = false;
            }

            sql << " WHERE \"" << table_info->primaryKeyColumn << "\" = '"
                << SQLiteFormatConverter::escapeSQL(m_path.row_id) << "'";
        } else {
            // INSERT new row
            sql << "INSERT INTO \"" << m_path.object_name << "\" (";

            bool first = true;
            for (const auto& [key, value] : row) {
                if (!first) sql << ", ";
                sql << "\"" << key << "\"";
                first = false;
            }

            sql << ") VALUES (";

            first = true;
            for (const auto& [key, value] : row) {
                if (!first) sql << ", ";
                if (value.has_value()) {
                    sql << "'" << SQLiteFormatConverter::escapeSQL(value.value()) << "'";
                } else {
                    sql << "NULL";
                }
                first = false;
            }

            sql << ")";
        }

        if (!conn->execute(sql.str())) {
            m_lastError = conn->error();
            return -EIO;
        }

        return 0;

    } catch (const std::exception& e) {
        m_lastError = e.what();
        return -EINVAL;
    }
}

}  // namespace sqlfuse
