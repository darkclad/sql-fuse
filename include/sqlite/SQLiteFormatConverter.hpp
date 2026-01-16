#pragma once

/**
 * @file SQLiteFormatConverter.hpp
 * @brief SQLite-specific data format conversion utilities for SQL-FUSE.
 *
 * This file provides format conversion functionality tailored for SQLite databases,
 * including SQL statement generation with proper SQLite-specific escaping
 * (double quotes for identifiers, doubled single quotes for string literals).
 */

#include "FormatConverter.hpp"

namespace sqlfuse {

/**
 * @class SQLiteFormatConverter
 * @brief Static utility class for SQLite-specific data format conversions.
 *
 * Provides methods to generate SQL statements (INSERT, UPDATE, DELETE) with
 * proper SQLite escaping. All methods are static as the class maintains no state.
 *
 * Key SQLite-specific behaviors:
 * - Identifiers are escaped with double quotes ("identifier")
 * - String literals use doubled single quotes for escaping ('O''Brien')
 * - SQLite is more permissive with types (dynamic typing)
 *
 * SQLite Escaping Rules:
 * - Identifiers: Double quotes with internal quotes doubled ("table""name")
 * - Strings: Single quotes with internal quotes doubled ('value''s')
 *
 * Inherits from FormatConverter to access shared CSV field escaping utilities.
 * Note: Unlike MySQL/Oracle converters, this doesn't have toCSV/toJSON methods
 * since SQLite result sets use a different API (sqlite3_stmt vs result sets).
 */
class SQLiteFormatConverter : public FormatConverter {
public:
    /**
     * @brief Build an INSERT SQL statement with SQLite-specific escaping.
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
     * @brief Build an UPDATE SQL statement with SQLite-specific escaping.
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
     * @brief Build a DELETE SQL statement with SQLite-specific escaping.
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

    /**
     * @brief Escape an SQLite identifier (table, column name).
     * @param identifier The identifier to escape.
     * @return Identifier wrapped in double quotes with internal quotes doubled.
     *
     * Example: "my table" -> "\"my table\"", col"name -> "\"col\"\"name\""
     */
    static std::string escapeIdentifier(const std::string& identifier);

    /**
     * @brief Escape a string value for use in SQLite SQL.
     * @param value The string value to escape.
     * @return String with single quotes doubled (e.g., "O'Brien" -> "O''Brien").
     *
     * Does NOT add surrounding quotes - caller must add them.
     */
    static std::string escapeSQL(const std::string& value);
};

}  // namespace sqlfuse
