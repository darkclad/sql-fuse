#pragma once

/**
 * @file MySQLFormatConverter.hpp
 * @brief MySQL-specific data format conversion utilities for SQL-FUSE.
 *
 * This file provides format conversion functionality tailored for MySQL databases,
 * including result set serialization to CSV/JSON and SQL statement generation with
 * proper MySQL-specific escaping (backticks for identifiers, backslash escaping
 * for string literals).
 */

#include "FormatConverter.hpp"
#include <mysql/mysql.h>

namespace sqlfuse {

/**
 * @class MySQLFormatConverter
 * @brief Static utility class for MySQL-specific data format conversions.
 *
 * Provides methods to convert MySQL result sets to various formats (CSV, JSON)
 * and generate SQL statements (INSERT, UPDATE, DELETE) with proper MySQL escaping.
 * All methods are static as the class maintains no state.
 *
 * Key MySQL-specific behaviors:
 * - Identifiers are escaped with backticks (`identifier`)
 * - String literals use backslash escaping for special characters
 * - Field types from MYSQL_FIELD are used to determine JSON number vs string
 * - NULL values are handled according to MySQL conventions
 *
 * Inherits from FormatConverter to access shared CSV field escaping utilities.
 */
class MySQLFormatConverter : public FormatConverter {
public:
    /**
     * @brief Convert a MySQL result set to CSV format.
     * @param result The MYSQL_RES* to iterate and convert.
     * @param options CSV formatting options (delimiter, header, quoting, etc.).
     * @return CSV-formatted string with all rows from the result set.
     *
     * Resets the result set position to the beginning before iterating.
     * Uses mysql_fetch_row() to iterate through all rows.
     */
    static std::string toCSV(MYSQL_RES* result, const CSVOptions& options = CSVOptions{});

    /**
     * @brief Convert a MySQL result set to JSON array format.
     * @param result The MYSQL_RES* to iterate and convert.
     * @param options JSON formatting options (pretty print, null handling, etc.).
     * @return JSON array string containing all rows as objects.
     *
     * Numeric MySQL types (INT, FLOAT, DOUBLE, DECIMAL) are preserved as JSON numbers.
     * NULL values are included based on options.includeNull setting.
     */
    static std::string toJSON(MYSQL_RES* result, const JSONOptions& options = JSONOptions{});

    /**
     * @brief Convert a single MySQL row to a JSON object.
     * @param row The MYSQL_ROW data array.
     * @param result The MYSQL_RES* for field metadata.
     * @param options JSON formatting options.
     * @return JSON object string representing the row.
     *
     * Used for individual row file access (e.g., /database/tables/TABLE/rows/1.json).
     */
    static std::string rowToJSON(MYSQL_ROW row, MYSQL_RES* result,
                                 const JSONOptions& options = JSONOptions{});

    /**
     * @brief Build an INSERT SQL statement with MySQL-specific escaping.
     * @param table Fully qualified table name (database.table or just table).
     * @param row Map of column names to values.
     * @param escape If true, escape string values (default: true).
     * @return Generated INSERT INTO ... VALUES (...) statement.
     *
     * Example output: INSERT INTO `database`.`table` (`col1`, `col2`) VALUES ('val1', 'val2')
     */
    static std::string buildInsert(const std::string& table,
                                   const RowData& row,
                                   bool escape = true);

    /**
     * @brief Build an UPDATE SQL statement with MySQL-specific escaping.
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
     * @brief Build a DELETE SQL statement with MySQL-specific escaping.
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
     * @brief Escape a MySQL identifier (table, column, database name).
     * @param identifier The identifier to escape.
     * @return Identifier wrapped in backticks with internal backticks doubled.
     *
     * Handles database.table format by escaping each part separately.
     * Example: "my table" -> "`my table`", database.table -> "`database`.`table`"
     */
    static std::string escapeIdentifier(const std::string& identifier);

    /**
     * @brief Escape a string value for use in MySQL SQL.
     * @param value The string value to escape.
     * @return String with special characters escaped using backslash.
     *
     * Escapes: ' " \ NUL newline carriage-return and other special characters.
     * Does NOT add surrounding quotes - caller must add them.
     */
    static std::string escapeSQL(const std::string& value);
};

}  // namespace sqlfuse
