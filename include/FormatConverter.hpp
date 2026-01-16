#pragma once

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <nlohmann/json.hpp>

namespace sqlfuse {

using json = nlohmann::json;

// Represents a single value that can be null
using SqlValue = std::optional<std::string>;

// Represents a row as column name -> value mapping
using RowData = std::map<std::string, SqlValue>;

// Options structs declared outside the class to avoid default argument issues
struct CSVOptions {
    char delimiter = ',';
    char quote = '"';
    char escape = '\\';
    std::string lineEnding = "\n";
    bool includeHeader = true;
    bool quoteAll = false;
};

struct JSONOptions {
    bool pretty = true;
    int indent = 2;
    bool includeNull = true;
    bool arrayFormat = true;  // true = array of objects, false = object with rows array
};

// Base class with database-independent format conversion methods
class FormatConverter {
public:
    virtual ~FormatConverter() = default;

    // Generic CSV conversion (from vectors)
    static std::string toCSV(const std::vector<std::string>& columns,
                            const std::vector<std::vector<SqlValue>>& rows,
                            const CSVOptions& options = CSVOptions{});

    // Generic JSON conversion (from vectors)
    static std::string toJSON(const std::vector<std::string>& columns,
                             const std::vector<std::vector<SqlValue>>& rows,
                             const JSONOptions& options = JSONOptions{});

    // Generic row to JSON (from vectors)
    static std::string rowToJSON(const std::vector<std::string>& columns,
                                const std::vector<SqlValue>& values,
                                const JSONOptions& options = JSONOptions{});
    static std::string rowToJSON(const RowData& row, const JSONOptions& options = JSONOptions{});

    // Parse CSV to rows
    static std::vector<RowData> parseCSV(const std::string& data,
                                         const CSVOptions& options = CSVOptions{});
    static std::vector<RowData> parseCSV(const std::string& data,
                                         const std::vector<std::string>& columns,
                                         const CSVOptions& options = CSVOptions{});

    // Parse JSON to rows
    static std::vector<RowData> parseJSON(const std::string& data);
    static RowData parseJSONRow(const std::string& data);

    // Escape values for SQL (generic - can be overridden for DB-specific escaping)
    static std::string escapeSQL(const std::string& value);

    // CSV utility functions
    static std::string escapeCSVField(const std::string& field,
                                      const CSVOptions& options = CSVOptions{});
    static std::vector<std::string> splitCSVLine(const std::string& line,
                                                  const CSVOptions& options = CSVOptions{});

protected:
    static std::string jsonValueToSQL(const json& value);
};

}  // namespace sqlfuse
