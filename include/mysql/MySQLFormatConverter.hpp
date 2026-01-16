#pragma once

#include "FormatConverter.hpp"
#include <mysql/mysql.h>

namespace sqlfuse {

// MySQL-specific format conversion methods
class MySQLFormatConverter : public FormatConverter {
public:
    // Convert MySQL result set to CSV
    static std::string toCSV(MYSQL_RES* result, const CSVOptions& options = CSVOptions{});

    // Convert MySQL result set to JSON
    static std::string toJSON(MYSQL_RES* result, const JSONOptions& options = JSONOptions{});

    // Convert single MySQL row to JSON
    static std::string rowToJSON(MYSQL_ROW row, MYSQL_RES* result,
                                 const JSONOptions& options = JSONOptions{});

    // Build SQL statements with MySQL-specific identifier escaping (backticks)
    static std::string buildInsert(const std::string& table,
                                   const RowData& row,
                                   bool escape = true);
    static std::string buildUpdate(const std::string& table,
                                   const RowData& row,
                                   const std::string& pkColumn,
                                   const std::string& pkValue,
                                   bool escape = true);
    static std::string buildDelete(const std::string& table,
                                   const std::string& pkColumn,
                                   const std::string& pkValue,
                                   bool escape = true);

    // MySQL-specific identifier escaping (uses backticks)
    static std::string escapeIdentifier(const std::string& identifier);

    // MySQL-specific SQL escaping
    static std::string escapeSQL(const std::string& value);
};

}  // namespace sqlfuse
