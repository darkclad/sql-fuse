/**
 * @file SQLiteFormatConverter.cpp
 * @brief Implementation of SQLite-specific SQL statement generation utilities.
 *
 * Implements the SQLiteFormatConverter class which provides static methods
 * for building INSERT, UPDATE, and DELETE SQL statements with proper escaping
 * for SQLite databases.
 *
 * SQLite Escaping Conventions:
 * - Identifiers: Double quotes with doubled double-quotes for escaping
 *   (e.g., "column""name" for a column containing a double quote)
 * - Strings: Single quotes with doubled single-quotes for escaping
 *   (e.g., 'O''Brien' for the string O'Brien)
 */

#include "SQLiteFormatConverter.hpp"
#include <sstream>

namespace sqlfuse {

// ============================================================================
// SQL Statement Builders
// ============================================================================

std::string SQLiteFormatConverter::buildInsert(const std::string& table,
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

std::string SQLiteFormatConverter::buildUpdate(const std::string& table,
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
        // Skip primary key in SET clause
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

    sql << " WHERE " << escapeIdentifier(pkColumn) << " = ";
    if (escape) {
        sql << "'" << escapeSQL(pkValue) << "'";
    } else {
        sql << "'" << pkValue << "'";
    }

    return sql.str();
}

std::string SQLiteFormatConverter::buildDelete(const std::string& table,
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

// ============================================================================
// Escaping Utilities
// ============================================================================

std::string SQLiteFormatConverter::escapeIdentifier(const std::string& identifier) {
    // SQLite uses double quotes for identifiers
    std::string result = "\"";
    for (char c : identifier) {
        if (c == '"') {
            result += "\"\"";  // Double the quote
        } else {
            result += c;
        }
    }
    result += "\"";
    return result;
}

std::string SQLiteFormatConverter::escapeSQL(const std::string& value) {
    // SQLite uses '' to escape single quotes (standard SQL)
    std::string result;
    result.reserve(value.size() * 2);

    for (char c : value) {
        if (c == '\'') {
            result += "''";  // Double the single quote
        } else {
            result += c;
        }
    }

    return result;
}

}  // namespace sqlfuse
