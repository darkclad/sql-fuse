#include "PostgreSQLFormatConverter.hpp"
#include <sstream>

namespace sqlfuse {

// PostgreSQL OID constants for common types
constexpr Oid BOOLOID = 16;
constexpr Oid INT2OID = 21;
constexpr Oid INT4OID = 23;
constexpr Oid INT8OID = 20;
constexpr Oid FLOAT4OID = 700;
constexpr Oid FLOAT8OID = 701;
constexpr Oid NUMERICOID = 1700;

bool PostgreSQLFormatConverter::isNumericType(Oid type) {
    return type == INT2OID || type == INT4OID || type == INT8OID ||
           type == FLOAT4OID || type == FLOAT8OID || type == NUMERICOID;
}

bool PostgreSQLFormatConverter::isBooleanType(Oid type) {
    return type == BOOLOID;
}

std::string PostgreSQLFormatConverter::formatValue(PGresult* result, int row, int col) {
    if (PQgetisnull(result, row, col)) {
        return "";
    }
    return PQgetvalue(result, row, col);
}

std::string PostgreSQLFormatConverter::toCSV(PGresult* result, const CSVOptions& options) {
    if (!result || PQresultStatus(result) != PGRES_TUPLES_OK) {
        return "";
    }

    std::ostringstream out;

    int num_fields = PQnfields(result);
    int num_rows = PQntuples(result);

    // Header
    if (options.includeHeader) {
        for (int i = 0; i < num_fields; ++i) {
            if (i > 0) out << options.delimiter;
            out << escapeCSVField(PQfname(result, i), options);
        }
        out << options.lineEnding;
    }

    // Rows
    for (int row = 0; row < num_rows; ++row) {
        for (int col = 0; col < num_fields; ++col) {
            if (col > 0) out << options.delimiter;

            if (!PQgetisnull(result, row, col)) {
                std::string value = PQgetvalue(result, row, col);
                out << escapeCSVField(value, options);
            }
            // NULL values are represented as empty
        }
        out << options.lineEnding;
    }

    return out.str();
}

std::string PostgreSQLFormatConverter::toCSV(PostgreSQLResultSet& result, const CSVOptions& options) {
    return toCSV(result.get(), options);
}

std::string PostgreSQLFormatConverter::toJSON(PGresult* result, const JSONOptions& options) {
    if (!result || PQresultStatus(result) != PGRES_TUPLES_OK) {
        return options.arrayFormat ? "[]" : "{\"rows\": []}";
    }

    int num_fields = PQnfields(result);
    int num_rows = PQntuples(result);

    std::vector<std::string> column_names;
    std::vector<Oid> column_types;
    column_names.reserve(num_fields);
    column_types.reserve(num_fields);

    for (int i = 0; i < num_fields; ++i) {
        column_names.emplace_back(PQfname(result, i));
        column_types.push_back(PQftype(result, i));
    }

    json arr = json::array();

    for (int row = 0; row < num_rows; ++row) {
        json obj = json::object();

        for (int col = 0; col < num_fields; ++col) {
            if (!PQgetisnull(result, row, col)) {
                std::string value = PQgetvalue(result, row, col);

                // Try to preserve numeric types
                if (isNumericType(column_types[col])) {
                    try {
                        if (column_types[col] == FLOAT4OID ||
                            column_types[col] == FLOAT8OID ||
                            column_types[col] == NUMERICOID) {
                            obj[column_names[col]] = std::stod(value);
                        } else {
                            obj[column_names[col]] = std::stoll(value);
                        }
                    } catch (...) {
                        obj[column_names[col]] = value;
                    }
                } else if (isBooleanType(column_types[col])) {
                    obj[column_names[col]] = (value == "t" || value == "true" || value == "1");
                } else {
                    obj[column_names[col]] = value;
                }
            } else if (options.includeNull) {
                obj[column_names[col]] = nullptr;
            }
        }

        arr.push_back(std::move(obj));
    }

    if (options.arrayFormat) {
        return options.pretty ? arr.dump(options.indent) : arr.dump();
    } else {
        json wrapper = json::object();
        wrapper["rows"] = std::move(arr);
        return options.pretty ? wrapper.dump(options.indent) : wrapper.dump();
    }
}

std::string PostgreSQLFormatConverter::toJSON(PostgreSQLResultSet& result, const JSONOptions& options) {
    return toJSON(result.get(), options);
}

std::string PostgreSQLFormatConverter::rowToJSON(PGresult* result, int row,
                                                  const JSONOptions& options) {
    if (!result || PQresultStatus(result) != PGRES_TUPLES_OK || row < 0 || row >= PQntuples(result)) {
        return "{}";
    }

    int num_fields = PQnfields(result);

    json obj = json::object();

    for (int col = 0; col < num_fields; ++col) {
        const char* field_name = PQfname(result, col);
        Oid field_type = PQftype(result, col);

        if (!PQgetisnull(result, row, col)) {
            std::string value = PQgetvalue(result, row, col);

            if (isNumericType(field_type)) {
                try {
                    if (field_type == FLOAT4OID ||
                        field_type == FLOAT8OID ||
                        field_type == NUMERICOID) {
                        obj[field_name] = std::stod(value);
                    } else {
                        obj[field_name] = std::stoll(value);
                    }
                } catch (...) {
                    obj[field_name] = value;
                }
            } else if (isBooleanType(field_type)) {
                obj[field_name] = (value == "t" || value == "true" || value == "1");
            } else {
                obj[field_name] = value;
            }
        } else if (options.includeNull) {
            obj[field_name] = nullptr;
        }
    }

    return options.pretty ? obj.dump(options.indent) : obj.dump();
}

std::string PostgreSQLFormatConverter::buildInsert(const std::string& table,
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

std::string PostgreSQLFormatConverter::buildUpdate(const std::string& table,
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

std::string PostgreSQLFormatConverter::buildDelete(const std::string& table,
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

std::string PostgreSQLFormatConverter::escapeIdentifier(const std::string& identifier) {
    // PostgreSQL uses double quotes for identifiers
    std::string result = "\"";
    for (char c : identifier) {
        if (c == '"') {
            result += "\"\"";
        } else {
            result += c;
        }
    }
    result += "\"";
    return result;
}

std::string PostgreSQLFormatConverter::escapeSQL(const std::string& value) {
    // PostgreSQL standard SQL escaping uses doubled single quotes
    std::string result;
    result.reserve(value.size() * 2);

    for (char c : value) {
        if (c == '\'') {
            result += "''";
        } else if (c == '\\') {
            // PostgreSQL with standard_conforming_strings=on (default since 9.1)
            // doesn't need backslash escaping, but we include it for compatibility
            result += "\\\\";
        } else {
            result += c;
        }
    }

    return result;
}

}  // namespace sqlfuse
