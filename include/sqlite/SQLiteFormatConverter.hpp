#pragma once

#include "FormatConverter.hpp"

namespace sqlfuse {

// SQLite-specific format conversion methods
class SQLiteFormatConverter : public FormatConverter {
public:
    // Build SQL statements with SQLite-specific identifier escaping (double quotes)
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

    // SQLite-specific identifier escaping (uses double quotes)
    static std::string escapeIdentifier(const std::string& identifier);

    // SQLite-specific SQL escaping (doubles single quotes)
    static std::string escapeSQL(const std::string& value);
};

}  // namespace sqlfuse
