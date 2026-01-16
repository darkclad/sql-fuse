#pragma once

#include "FormatConverter.hpp"
#include "PostgreSQLResultSet.hpp"
#include <libpq-fe.h>

namespace sqlfuse {

// PostgreSQL-specific format conversion methods
class PostgreSQLFormatConverter : public FormatConverter {
public:
    // Convert PostgreSQL result set to CSV
    static std::string toCSV(PGresult* result, const CSVOptions& options = CSVOptions{});
    static std::string toCSV(PostgreSQLResultSet& result, const CSVOptions& options = CSVOptions{});

    // Convert PostgreSQL result set to JSON
    static std::string toJSON(PGresult* result, const JSONOptions& options = JSONOptions{});
    static std::string toJSON(PostgreSQLResultSet& result, const JSONOptions& options = JSONOptions{});

    // Convert single PostgreSQL row to JSON
    static std::string rowToJSON(PGresult* result, int row,
                                 const JSONOptions& options = JSONOptions{});

    // Build SQL statements with PostgreSQL-specific identifier escaping (double quotes)
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

    // PostgreSQL-specific identifier escaping (uses double quotes)
    static std::string escapeIdentifier(const std::string& identifier);

    // PostgreSQL-specific SQL escaping
    static std::string escapeSQL(const std::string& value);

    // PostgreSQL type handling
    static std::string formatValue(PGresult* result, int row, int col);
    static bool isNumericType(Oid type);
    static bool isBooleanType(Oid type);
};

}  // namespace sqlfuse
