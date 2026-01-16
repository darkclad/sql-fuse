/**
 * @file OracleFormatConverter.cpp
 * @brief Implementation of Oracle-specific data format conversion utilities.
 *
 * This file contains the implementation for converting Oracle result sets to
 * CSV and JSON formats, as well as generating SQL statements with proper
 * Oracle escaping conventions.
 */

#include "OracleFormatConverter.hpp"
#include <sstream>
#include <oci.h>

namespace sqlfuse {

// =============================================================================
// Type Classification Methods
// =============================================================================

/**
 * Check if an OCI type code represents a numeric value.
 * Used to preserve numeric types in JSON output (as numbers, not strings).
 */
bool OracleFormatConverter::isNumericType(int oracleType) {
    return oracleType == SQLT_NUM ||     // Oracle NUMBER
           oracleType == SQLT_INT ||     // Integer
           oracleType == SQLT_FLT ||     // Float
           oracleType == SQLT_VNU ||     // VARNUM
           oracleType == SQLT_PDN ||     // Packed decimal
           oracleType == SQLT_BFLOAT ||  // BINARY_FLOAT
           oracleType == SQLT_BDOUBLE;   // BINARY_DOUBLE
}

/**
 * Check if an OCI type code represents a date/time value.
 * Includes dates, timestamps with various timezone handling, and intervals.
 */
bool OracleFormatConverter::isDateTimeType(int oracleType) {
    return oracleType == SQLT_DAT ||           // DATE (legacy)
           oracleType == SQLT_DATE ||          // ANSI DATE
           oracleType == SQLT_TIMESTAMP ||     // TIMESTAMP
           oracleType == SQLT_TIMESTAMP_TZ ||  // TIMESTAMP WITH TIME ZONE
           oracleType == SQLT_TIMESTAMP_LTZ || // TIMESTAMP WITH LOCAL TIME ZONE
           oracleType == SQLT_INTERVAL_YM ||   // INTERVAL YEAR TO MONTH
           oracleType == SQLT_INTERVAL_DS;     // INTERVAL DAY TO SECOND
}

/**
 * Check if an OCI type code represents a Large Object (LOB).
 * LOBs require special handling and may be truncated in output.
 */
bool OracleFormatConverter::isLobType(int oracleType) {
    return oracleType == SQLT_CLOB ||  // Character LOB
           oracleType == SQLT_BLOB ||  // Binary LOB
           oracleType == SQLT_BFILE;   // External binary file
}

// =============================================================================
// Result Set to CSV Conversion
// =============================================================================

/**
 * Convert an Oracle result set to CSV format.
 *
 * Iterates through all rows in the result set, formatting each value
 * according to CSV conventions. Uses the base class escapeCSVField()
 * method for proper quoting of fields containing special characters.
 *
 * NULL values are represented as empty fields (no quotes, no content).
 */
std::string OracleFormatConverter::toCSV(OracleResultSet& result, const CSVOptions& options) {
    std::ostringstream out;

    int numFields = result.numFields();
    if (numFields == 0) {
        return "";
    }

    // Write header row with column names
    if (options.includeHeader) {
        for (int i = 0; i < numFields; ++i) {
            if (i > 0) out << options.delimiter;
            out << escapeCSVField(result.fieldName(i), options);
        }
        out << options.lineEnding;
    }

    // Write data rows
    while (result.fetchRow()) {
        for (int i = 0; i < numFields; ++i) {
            if (i > 0) out << options.delimiter;

            if (!result.isNull(i)) {
                const char* value = result.getValue(i);
                if (value) {
                    out << escapeCSVField(value, options);
                }
            }
            // NULL values are represented as empty (no output)
        }
        out << options.lineEnding;
    }

    return out.str();
}

// =============================================================================
// Result Set to JSON Conversion
// =============================================================================

/**
 * Convert an Oracle result set to JSON array format.
 *
 * Each row becomes a JSON object with column names as keys.
 * Numeric Oracle types are converted to JSON numbers to preserve type info.
 * Non-numeric types are stored as JSON strings.
 *
 * The output format depends on options.arrayFormat:
 * - true: Returns a plain JSON array: [{"col": "val"}, ...]
 * - false: Returns wrapped format: {"rows": [...]}
 */
std::string OracleFormatConverter::toJSON(OracleResultSet& result, const JSONOptions& options) {
    int numFields = result.numFields();
    if (numFields == 0) {
        return options.arrayFormat ? "[]" : "{\"rows\": []}";
    }

    // Cache column metadata for efficient access during row iteration
    std::vector<std::string> columnNames;
    std::vector<int> columnTypes;
    columnNames.reserve(numFields);
    columnTypes.reserve(numFields);

    for (int i = 0; i < numFields; ++i) {
        columnNames.push_back(result.fieldName(i));
        columnTypes.push_back(result.fieldType(i));
    }

    json arr = json::array();

    // Process each row
    while (result.fetchRow()) {
        json obj = json::object();

        for (int i = 0; i < numFields; ++i) {
            if (!result.isNull(i)) {
                const char* value = result.getValue(i);
                if (value) {
                    // Preserve numeric types as JSON numbers
                    if (isNumericType(columnTypes[i])) {
                        try {
                            std::string strVal(value);
                            // Use floating point for decimals, integer for whole numbers
                            if (strVal.find('.') != std::string::npos) {
                                obj[columnNames[i]] = std::stod(strVal);
                            } else {
                                obj[columnNames[i]] = std::stoll(strVal);
                            }
                        } catch (...) {
                            // Fall back to string if number parsing fails
                            obj[columnNames[i]] = value;
                        }
                    } else {
                        obj[columnNames[i]] = value;
                    }
                }
            } else if (options.includeNull) {
                obj[columnNames[i]] = nullptr;
            }
        }

        arr.push_back(std::move(obj));
    }

    // Format output based on options
    if (options.arrayFormat) {
        return options.pretty ? arr.dump(options.indent) : arr.dump();
    } else {
        json wrapper = json::object();
        wrapper["rows"] = std::move(arr);
        return options.pretty ? wrapper.dump(options.indent) : wrapper.dump();
    }
}

/**
 * Convert the current row of a result set to a JSON object.
 *
 * Unlike toJSON(), this only converts the row currently positioned
 * in the result set (after a fetchRow() call). This is used for
 * individual row file access (e.g., /schema/tables/TABLE/rows/1.json).
 */
std::string OracleFormatConverter::rowToJSON(OracleResultSet& result, const JSONOptions& options) {
    int numFields = result.numFields();
    if (numFields == 0) {
        return "{}";
    }

    json obj = json::object();

    for (int i = 0; i < numFields; ++i) {
        const char* fieldName = result.fieldName(i);
        int fieldType = result.fieldType(i);

        if (!result.isNull(i)) {
            const char* value = result.getValue(i);
            if (value) {
                // Preserve numeric types as JSON numbers
                if (isNumericType(fieldType)) {
                    try {
                        std::string strVal(value);
                        if (strVal.find('.') != std::string::npos) {
                            obj[fieldName] = std::stod(strVal);
                        } else {
                            obj[fieldName] = std::stoll(strVal);
                        }
                    } catch (...) {
                        obj[fieldName] = value;
                    }
                } else {
                    obj[fieldName] = value;
                }
            }
        } else if (options.includeNull) {
            obj[fieldName] = nullptr;
        }
    }

    return options.pretty ? obj.dump(options.indent) : obj.dump();
}

// =============================================================================
// SQL Statement Builders
// =============================================================================

/**
 * Build an INSERT statement for Oracle.
 *
 * Generates: INSERT INTO "schema"."table" ("col1", "col2") VALUES ('val1', 'val2')
 *
 * All identifiers are escaped with double quotes.
 * String values are escaped by doubling single quotes.
 * NULL values in the row data result in SQL NULL keywords.
 */
std::string OracleFormatConverter::buildInsert(const std::string& table,
                                                 const RowData& row,
                                                 bool escape) {
    if (row.empty()) {
        return "";
    }

    std::ostringstream sql;
    sql << "INSERT INTO " << escapeIdentifier(table) << " (";

    std::ostringstream values;
    values << " VALUES (";

    bool first = true;
    for (const auto& [col, val] : row) {
        if (!first) {
            sql << ", ";
            values << ", ";
        }
        first = false;

        sql << escapeIdentifier(col);

        if (val.has_value()) {
            if (escape) {
                values << "'" << escapeSQL(val.value()) << "'";
            } else {
                values << "'" << val.value() << "'";
            }
        } else {
            values << "NULL";
        }
    }

    sql << ")" << values.str() << ")";
    return sql.str();
}

/**
 * Build an UPDATE statement for Oracle.
 *
 * Generates: UPDATE "schema"."table" SET "col1" = 'val1' WHERE "pk" = 'pkval'
 *
 * The primary key column is excluded from the SET clause to avoid updating
 * the row identifier. Only columns present in the row data are updated.
 */
std::string OracleFormatConverter::buildUpdate(const std::string& table,
                                                 const RowData& row,
                                                 const std::string& pkColumn,
                                                 const std::string& pkValue,
                                                 bool escape) {
    if (row.empty()) {
        return "";
    }

    std::ostringstream sql;
    sql << "UPDATE " << escapeIdentifier(table) << " SET ";

    bool first = true;
    for (const auto& [col, val] : row) {
        // Skip primary key in SET clause - we don't want to change the row ID
        if (col == pkColumn) continue;

        if (!first) {
            sql << ", ";
        }
        first = false;

        sql << escapeIdentifier(col) << " = ";

        if (val.has_value()) {
            if (escape) {
                sql << "'" << escapeSQL(val.value()) << "'";
            } else {
                sql << "'" << val.value() << "'";
            }
        } else {
            sql << "NULL";
        }
    }

    // Add WHERE clause for primary key
    sql << " WHERE " << escapeIdentifier(pkColumn) << " = ";
    if (escape) {
        sql << "'" << escapeSQL(pkValue) << "'";
    } else {
        sql << "'" << pkValue << "'";
    }

    return sql.str();
}

/**
 * Build a DELETE statement for Oracle.
 *
 * Generates: DELETE FROM "schema"."table" WHERE "pk" = 'pkval'
 *
 * Only deletes the single row matching the primary key value.
 */
std::string OracleFormatConverter::buildDelete(const std::string& table,
                                                 const std::string& pkColumn,
                                                 const std::string& pkValue,
                                                 bool escape) {
    std::ostringstream sql;
    sql << "DELETE FROM " << escapeIdentifier(table)
        << " WHERE " << escapeIdentifier(pkColumn) << " = ";

    if (escape) {
        sql << "'" << escapeSQL(pkValue) << "'";
    } else {
        sql << "'" << pkValue << "'";
    }

    return sql.str();
}

// =============================================================================
// Escaping Utilities
// =============================================================================

/**
 * Escape an Oracle identifier (table, column, schema name).
 *
 * Oracle uses double quotes for identifier quoting, which allows:
 * - Case-sensitive names (Oracle normally uppercases unquoted identifiers)
 * - Reserved words as identifiers
 * - Special characters in names
 *
 * Double quotes within the identifier are doubled ("name""with""quotes").
 *
 * For schema.table format, each part is escaped separately:
 * "schema"."table"
 */
std::string OracleFormatConverter::escapeIdentifier(const std::string& identifier) {
    // Handle schema.table format by recursively escaping each part
    auto dot_pos = identifier.find('.');
    if (dot_pos != std::string::npos) {
        std::string schema = identifier.substr(0, dot_pos);
        std::string table = identifier.substr(dot_pos + 1);
        return escapeIdentifier(schema) + "." + escapeIdentifier(table);
    }

    // Escape single identifier with double quotes
    std::string result = "\"";
    for (char c : identifier) {
        if (c == '"') {
            result += "\"\"";  // Double the quote to escape it
        } else {
            result += c;
        }
    }
    result += "\"";
    return result;
}

/**
 * Escape a string value for use in Oracle SQL.
 *
 * Oracle uses single quotes for string literals and escapes embedded
 * single quotes by doubling them: 'O''Brien' represents O'Brien.
 *
 * This function does NOT add the surrounding quotes - the caller
 * must add them: "'" + escapeSQL(value) + "'"
 */
std::string OracleFormatConverter::escapeSQL(const std::string& value) {
    std::string result;
    result.reserve(value.size() * 2);  // Worst case: all quotes

    for (char c : value) {
        if (c == '\'') {
            result += "''";  // Double single quote to escape
        } else {
            result += c;
        }
    }

    return result;
}

}  // namespace sqlfuse
