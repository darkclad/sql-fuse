#pragma once

/**
 * @file PostgreSQLFormatConverter.hpp
 * @brief PostgreSQL-specific data format conversion utilities for SQL-FUSE.
 *
 * This file provides format conversion functionality tailored for PostgreSQL databases,
 * including result set to CSV/JSON conversion and SQL statement generation with
 * proper PostgreSQL-specific escaping (double quotes for identifiers, E'' syntax
 * or doubled single quotes for string literals).
 */

#include "FormatConverter.hpp"
#include "PostgreSQLResultSet.hpp"
#include <libpq-fe.h>

namespace sqlfuse {

/**
 * @class PostgreSQLFormatConverter
 * @brief Static utility class for PostgreSQL-specific data format conversions.
 *
 * Provides methods to convert PostgreSQL result sets (PGresult*) to CSV or JSON
 * format, and to generate SQL statements (INSERT, UPDATE, DELETE) with proper
 * PostgreSQL escaping. All methods are static as the class maintains no state.
 *
 * PostgreSQL Escaping Rules:
 * - Identifiers: Double quotes with internal quotes doubled ("table""name")
 * - Strings: Single quotes with internal quotes doubled ('O''Brien')
 * - Alternatively, E'...' syntax for escape strings with backslash
 *
 * PostgreSQL Type System:
 * - Uses Oid (Object Identifier) for type identification
 * - Strong typing with distinct numeric, boolean, text, etc. types
 * - Type information available via PQftype()
 *
 * Inherits from FormatConverter to access shared CSV field escaping utilities.
 *
 * @see PostgreSQLResultSet for result set wrapper
 */
class PostgreSQLFormatConverter : public FormatConverter {
public:
    // ----- Result Set to CSV Conversion -----

    /**
     * @brief Convert a raw PGresult to CSV format.
     * @param result PostgreSQL result handle.
     * @param options CSV formatting options (delimiter, quotes, etc.).
     * @return CSV string with header row and data rows.
     *
     * Iterates through all rows in the result and formats them as CSV.
     * NULL values are represented as empty fields.
     */
    static std::string toCSV(PGresult* result, const CSVOptions& options = CSVOptions{});

    /**
     * @brief Convert a PostgreSQLResultSet to CSV format.
     * @param result Result set wrapper (will be consumed/advanced).
     * @param options CSV formatting options.
     * @return CSV string with header row and data rows.
     */
    static std::string toCSV(PostgreSQLResultSet& result, const CSVOptions& options = CSVOptions{});

    // ----- Result Set to JSON Conversion -----

    /**
     * @brief Convert a raw PGresult to JSON array format.
     * @param result PostgreSQL result handle.
     * @param options JSON formatting options (pretty print, etc.).
     * @return JSON array containing row objects.
     *
     * Each row becomes a JSON object with column names as keys.
     * PostgreSQL types are mapped appropriately (numbers, booleans, strings).
     */
    static std::string toJSON(PGresult* result, const JSONOptions& options = JSONOptions{});

    /**
     * @brief Convert a PostgreSQLResultSet to JSON array format.
     * @param result Result set wrapper (will be consumed/advanced).
     * @param options JSON formatting options.
     * @return JSON array containing row objects.
     */
    static std::string toJSON(PostgreSQLResultSet& result, const JSONOptions& options = JSONOptions{});

    /**
     * @brief Convert a single row from a PGresult to JSON object.
     * @param result PostgreSQL result handle.
     * @param row Zero-based row index.
     * @param options JSON formatting options.
     * @return JSON object string for the specified row.
     */
    static std::string rowToJSON(PGresult* result, int row,
                                 const JSONOptions& options = JSONOptions{});

    // ----- SQL Statement Generation -----

    /**
     * @brief Build an INSERT SQL statement with PostgreSQL-specific escaping.
     * @param table Table name.
     * @param row Map of column names to values.
     * @param escape If true, escape string values (default: true).
     * @return Generated INSERT INTO ... VALUES (...) statement.
     *
     * Example output: INSERT INTO "table" ("col1", "col2") VALUES ('val1', 'val2')
     */
    static std::string buildInsert(const std::string& table,
                                   const RowData& row,
                                   bool escape = true);

    /**
     * @brief Build an UPDATE SQL statement with PostgreSQL-specific escaping.
     * @param table Table name.
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
     * @brief Build a DELETE SQL statement with PostgreSQL-specific escaping.
     * @param table Table name.
     * @param pkColumn Primary key column name for the WHERE clause.
     * @param pkValue Primary key value to match.
     * @param escape If true, escape the pk value (default: true).
     * @return Generated DELETE FROM ... WHERE statement.
     */
    static std::string buildDelete(const std::string& table,
                                   const std::string& pkColumn,
                                   const std::string& pkValue,
                                   bool escape = true);

    // ----- Escaping Utilities -----

    /**
     * @brief Escape a PostgreSQL identifier (table, column name).
     * @param identifier The identifier to escape.
     * @return Identifier wrapped in double quotes with internal quotes doubled.
     *
     * Example: my table -> "my table", col"name -> "col""name"
     */
    static std::string escapeIdentifier(const std::string& identifier);

    /**
     * @brief Escape a string value for use in PostgreSQL SQL.
     * @param value The string value to escape.
     * @return String with single quotes doubled (e.g., "O'Brien" -> "O''Brien").
     *
     * Does NOT add surrounding quotes - caller must add them.
     */
    static std::string escapeSQL(const std::string& value);

    // ----- PostgreSQL Type Handling -----

    /**
     * @brief Format a value from a result set appropriately for output.
     * @param result PostgreSQL result handle.
     * @param row Row index.
     * @param col Column index.
     * @return Formatted string value.
     *
     * Handles NULL values and applies appropriate formatting based on type.
     */
    static std::string formatValue(PGresult* result, int row, int col);

    /**
     * @brief Check if a PostgreSQL type Oid is numeric.
     * @param type The Oid to check.
     * @return true if the type is a numeric type (int, float, numeric, etc.).
     *
     * Numeric types: INT2, INT4, INT8, FLOAT4, FLOAT8, NUMERIC
     */
    static bool isNumericType(Oid type);

    /**
     * @brief Check if a PostgreSQL type Oid is boolean.
     * @param type The Oid to check.
     * @return true if the type is BOOL (Oid 16).
     */
    static bool isBooleanType(Oid type);
};

}  // namespace sqlfuse
