#pragma once

#include "FormatConverter.hpp"
#include "OracleResultSet.hpp"
#include <oci.h>

namespace sqlfuse {

// Oracle-specific format conversion methods
class OracleFormatConverter : public FormatConverter {
public:
    // Convert Oracle result set to CSV
    static std::string toCSV(OracleResultSet& result, const CSVOptions& options = CSVOptions{});

    // Convert Oracle result set to JSON
    static std::string toJSON(OracleResultSet& result, const JSONOptions& options = JSONOptions{});

    // Convert single Oracle row to JSON (current row in result set)
    static std::string rowToJSON(OracleResultSet& result,
                                 const JSONOptions& options = JSONOptions{});

    // Build SQL statements with Oracle-specific identifier escaping (double quotes)
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

    // Oracle-specific identifier escaping (uses double quotes)
    static std::string escapeIdentifier(const std::string& identifier);

    // Oracle-specific SQL escaping (doubles single quotes)
    static std::string escapeSQL(const std::string& value);

    // Oracle type handling
    static bool isNumericType(int oracleType);
    static bool isDateTimeType(int oracleType);
    static bool isLobType(int oracleType);
};

}  // namespace sqlfuse
