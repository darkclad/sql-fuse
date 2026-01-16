#pragma once

/**
 * @file OracleFormatConverter.hpp
 * @brief Oracle-specific data format conversion utilities for SQL-FUSE.
 *
 * This file provides format conversion functionality tailored for Oracle databases,
 * including result set serialization to CSV/JSON and SQL statement generation with
 * proper Oracle-specific escaping (double quotes for identifiers, doubled single
 * quotes for string literals).
 */

#include "FormatConverter.hpp"
#include "OracleResultSet.hpp"
#include <oci.h>

namespace sqlfuse {

/**
 * @class OracleFormatConverter
 * @brief Static utility class for Oracle-specific data format conversions.
 *
 * Provides methods to convert Oracle result sets to various formats (CSV, JSON)
 * and generate SQL statements (INSERT, UPDATE, DELETE) with proper Oracle escaping.
 * All methods are static as the class maintains no state.
 *
 * Key Oracle-specific behaviors:
 * - Identifiers are escaped with double quotes ("identifier")
 * - String literals use doubled single quotes for escaping ('O''Brien')
 * - Numeric types are preserved as numbers in JSON output
 * - LOB types are read with a size limit for safety
 *
 * Inherits from FormatConverter to access shared CSV field escaping utilities.
 */
class OracleFormatConverter : public FormatConverter {
public:
    /**
     * @brief Convert an Oracle result set to CSV format.
     * @param result The OracleResultSet to iterate and convert.
     * @param options CSV formatting options (delimiter, header, quoting, etc.).
     * @return CSV-formatted string with all rows from the result set.
     *
     * Fetches all remaining rows from the result set. After calling this method,
     * the result set cursor will be at the end.
     */
    static std::string toCSV(OracleResultSet& result, const CSVOptions& options = CSVOptions{});

    /**
     * @brief Convert an Oracle result set to JSON array format.
     * @param result The OracleResultSet to iterate and convert.
     * @param options JSON formatting options (pretty print, null handling, etc.).
     * @return JSON array string containing all rows as objects.
     *
     * Numeric Oracle types (NUMBER, INTEGER, FLOAT) are preserved as JSON numbers.
     * NULL values are included based on options.includeNull setting.
     */
    static std::string toJSON(OracleResultSet& result, const JSONOptions& options = JSONOptions{});

    /**
     * @brief Convert the current row of a result set to a JSON object.
     * @param result The OracleResultSet positioned at the row to convert.
     * @param options JSON formatting options.
     * @return JSON object string representing the current row.
     *
     * Unlike toJSON(), this method converts only the current row without
     * fetching additional rows. The caller must have already called fetchRow().
     */
    static std::string rowToJSON(OracleResultSet& result,
                                 const JSONOptions& options = JSONOptions{});

    /**
     * @brief Build an INSERT SQL statement with Oracle-specific escaping.
     * @param table Fully qualified table name (schema.table or just table).
     * @param row Map of column names to values.
     * @param escape If true, escape string values (default: true).
     * @return Generated INSERT INTO ... VALUES (...) statement.
     *
     * Example output: INSERT INTO "SCHEMA"."TABLE" ("COL1", "COL2") VALUES ('val1', 'val2')
     */
    static std::string buildInsert(const std::string& table,
                                   const RowData& row,
                                   bool escape = true);

    /**
     * @brief Build an UPDATE SQL statement with Oracle-specific escaping.
     * @param table Fully qualified table name.
     * @param row Map of column names to new values.
     * @param pkColumn Primary key column name for the WHERE clause.
     * @param pkValue Primary key value to match.
     * @param escape If true, escape string values (default: true).
     * @return Generated UPDATE ... SET ... WHERE statement.
     *
     * The primary key column is excluded from the SET clause.
     */
    static std::string buildUpdate(const std::string& table,
                                   const RowData& row,
                                   const std::string& pkColumn,
                                   const std::string& pkValue,
                                   bool escape = true);

    /**
     * @brief Build a DELETE SQL statement with Oracle-specific escaping.
     * @param table Fully qualified table name.
     * @param pkColumn Primary key column name for the WHERE clause.
     * @param pkValue Primary key value to match.
     * @param escape If true, escape the pk value (default: true).
     * @return Generated DELETE FROM ... WHERE statement.
     */
    static std::string buildDelete(const std::string& table,
                                   const std::string& pkColumn,
                                   const std::string& pkValue,
                                   bool escape = true);

    /**
     * @brief Escape an Oracle identifier (table, column, schema name).
     * @param identifier The identifier to escape.
     * @return Identifier wrapped in double quotes with internal quotes doubled.
     *
     * Handles schema.table format by escaping each part separately.
     * Example: "my table" -> "\"my table\"", schema.table -> "schema"."table"
     */
    static std::string escapeIdentifier(const std::string& identifier);

    /**
     * @brief Escape a string value for use in Oracle SQL.
     * @param value The string value to escape.
     * @return String with single quotes doubled (e.g., "O'Brien" -> "O''Brien").
     *
     * Does NOT add surrounding quotes - caller must add them.
     */
    static std::string escapeSQL(const std::string& value);

    /**
     * @brief Check if an OCI data type is numeric.
     * @param oracleType OCI type constant (SQLT_NUM, SQLT_INT, etc.).
     * @return true if the type represents a numeric value.
     */
    static bool isNumericType(int oracleType);

    /**
     * @brief Check if an OCI data type is a date/time type.
     * @param oracleType OCI type constant (SQLT_DAT, SQLT_TIMESTAMP, etc.).
     * @return true if the type represents a date, time, or timestamp.
     */
    static bool isDateTimeType(int oracleType);

    /**
     * @brief Check if an OCI data type is a LOB (Large Object) type.
     * @param oracleType OCI type constant (SQLT_CLOB, SQLT_BLOB, etc.).
     * @return true if the type represents a LOB.
     */
    static bool isLobType(int oracleType);
};

}  // namespace sqlfuse
