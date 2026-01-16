/**
 * @file MySQLFormatConverter.cpp
 * @brief Implementation of MySQL-specific data format conversion utilities.
 *
 * Implements format conversion methods for MySQL result sets to CSV/JSON
 * and SQL statement generation with proper MySQL escaping conventions.
 */

#include "MySQLFormatConverter.hpp"
#include <sstream>

namespace sqlfuse {

// ============================================================================
// Result Set to CSV Conversion
// ============================================================================

std::string MySQLFormatConverter::toCSV(MYSQL_RES* result, const CSVOptions& options) {
    if (!result) {
        return "";
    }

    std::ostringstream out;

    unsigned int num_fields = mysql_num_fields(result);
    MYSQL_FIELD* fields = mysql_fetch_fields(result);

    // Write header row with column names
    if (options.includeHeader) {
        for (unsigned int i = 0; i < num_fields; ++i) {
            if (i > 0) out << options.delimiter;
            out << escapeCSVField(fields[i].name, options);
        }
        out << options.lineEnding;
    }

    // Write data rows
    MYSQL_ROW row;
    unsigned long* lengths;

    while ((row = mysql_fetch_row(result))) {
        lengths = mysql_fetch_lengths(result);

        for (unsigned int i = 0; i < num_fields; ++i) {
            if (i > 0) out << options.delimiter;

            if (row[i]) {
                // Use length to handle binary data correctly
                std::string value(row[i], lengths[i]);
                out << escapeCSVField(value, options);
            }
            // NULL values are represented as empty fields
        }
        out << options.lineEnding;
    }

    return out.str();
}

// ============================================================================
// Result Set to JSON Conversion
// ============================================================================

std::string MySQLFormatConverter::toJSON(MYSQL_RES* result, const JSONOptions& options) {
    if (!result) {
        return options.arrayFormat ? "[]" : "{\"rows\": []}";
    }

    unsigned int num_fields = mysql_num_fields(result);
    MYSQL_FIELD* fields = mysql_fetch_fields(result);

    // Extract column names for JSON keys
    std::vector<std::string> column_names;
    column_names.reserve(num_fields);
    for (unsigned int i = 0; i < num_fields; ++i) {
        column_names.emplace_back(fields[i].name);
    }

    json arr = json::array();
    MYSQL_ROW row;
    unsigned long* lengths;

    while ((row = mysql_fetch_row(result))) {
        lengths = mysql_fetch_lengths(result);
        json obj = json::object();

        for (unsigned int i = 0; i < num_fields; ++i) {
            if (row[i]) {
                std::string value(row[i], lengths[i]);

                // Preserve numeric types in JSON output
                // IS_NUM macro checks MySQL field type flags
                if (IS_NUM(fields[i].type)) {
                    try {
                        if (fields[i].type == MYSQL_TYPE_FLOAT ||
                            fields[i].type == MYSQL_TYPE_DOUBLE ||
                            fields[i].type == MYSQL_TYPE_DECIMAL ||
                            fields[i].type == MYSQL_TYPE_NEWDECIMAL) {
                            obj[column_names[i]] = std::stod(value);
                        } else {
                            obj[column_names[i]] = std::stoll(value);
                        }
                    } catch (...) {
                        // Fall back to string if conversion fails
                        obj[column_names[i]] = value;
                    }
                } else {
                    obj[column_names[i]] = value;
                }
            } else if (options.includeNull) {
                obj[column_names[i]] = nullptr;
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

std::string MySQLFormatConverter::rowToJSON(MYSQL_ROW row, MYSQL_RES* result,
                                            const JSONOptions& options) {
    if (!row || !result) {
        return "{}";
    }

    unsigned int num_fields = mysql_num_fields(result);
    MYSQL_FIELD* fields = mysql_fetch_fields(result);
    unsigned long* lengths = mysql_fetch_lengths(result);

    json obj = json::object();

    for (unsigned int i = 0; i < num_fields; ++i) {
        if (row[i]) {
            std::string value(row[i], lengths[i]);

            // Apply same numeric type preservation as toJSON
            if (IS_NUM(fields[i].type)) {
                try {
                    if (fields[i].type == MYSQL_TYPE_FLOAT ||
                        fields[i].type == MYSQL_TYPE_DOUBLE ||
                        fields[i].type == MYSQL_TYPE_DECIMAL ||
                        fields[i].type == MYSQL_TYPE_NEWDECIMAL) {
                        obj[fields[i].name] = std::stod(value);
                    } else {
                        obj[fields[i].name] = std::stoll(value);
                    }
                } catch (...) {
                    obj[fields[i].name] = value;
                }
            } else {
                obj[fields[i].name] = value;
            }
        } else if (options.includeNull) {
            obj[fields[i].name] = nullptr;
        }
    }

    return options.pretty ? obj.dump(options.indent) : obj.dump();
}

// ============================================================================
// SQL Statement Generation
// ============================================================================

std::string MySQLFormatConverter::buildInsert(const std::string& table,
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

std::string MySQLFormatConverter::buildUpdate(const std::string& table,
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
        // Skip primary key column in SET clause
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

std::string MySQLFormatConverter::buildDelete(const std::string& table,
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
// MySQL-Specific Escaping
// ============================================================================

std::string MySQLFormatConverter::escapeIdentifier(const std::string& identifier) {
    // Handle schema.table format by escaping each part separately
    auto dot_pos = identifier.find('.');
    if (dot_pos != std::string::npos) {
        std::string db = identifier.substr(0, dot_pos);
        std::string table = identifier.substr(dot_pos + 1);
        return escapeIdentifier(db) + "." + escapeIdentifier(table);
    }

    // MySQL uses backticks for identifier quoting
    // Backticks within identifiers are doubled
    std::string result = "`";
    for (char c : identifier) {
        if (c == '`') {
            result += "``";
        } else {
            result += c;
        }
    }
    result += "`";
    return result;
}

std::string MySQLFormatConverter::escapeSQL(const std::string& value) {
    // MySQL uses backslash escaping for special characters
    std::string result;
    result.reserve(value.size() * 2);

    for (char c : value) {
        switch (c) {
            case '\0': result += "\\0"; break;    // NUL byte
            case '\n': result += "\\n"; break;    // Newline
            case '\r': result += "\\r"; break;    // Carriage return
            case '\\': result += "\\\\"; break;   // Backslash
            case '\'': result += "\\'"; break;    // Single quote
            case '"':  result += "\\\""; break;   // Double quote
            case '\x1a': result += "\\Z"; break;  // Ctrl+Z (Windows EOF)
            default:   result += c; break;
        }
    }

    return result;
}

}  // namespace sqlfuse
