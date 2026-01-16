#include "OracleFormatConverter.hpp"
#include <sstream>
#include <oci.h>

namespace sqlfuse {

bool OracleFormatConverter::isNumericType(int oracleType) {
    return oracleType == SQLT_NUM || oracleType == SQLT_INT ||
           oracleType == SQLT_FLT || oracleType == SQLT_VNU ||
           oracleType == SQLT_PDN || oracleType == SQLT_BFLOAT ||
           oracleType == SQLT_BDOUBLE;
}

bool OracleFormatConverter::isDateTimeType(int oracleType) {
    return oracleType == SQLT_DAT || oracleType == SQLT_DATE ||
           oracleType == SQLT_TIMESTAMP || oracleType == SQLT_TIMESTAMP_TZ ||
           oracleType == SQLT_TIMESTAMP_LTZ || oracleType == SQLT_INTERVAL_YM ||
           oracleType == SQLT_INTERVAL_DS;
}

bool OracleFormatConverter::isLobType(int oracleType) {
    return oracleType == SQLT_CLOB || oracleType == SQLT_BLOB ||
           oracleType == SQLT_BFILE;
}

std::string OracleFormatConverter::toCSV(OracleResultSet& result, const CSVOptions& options) {
    std::ostringstream out;

    int numFields = result.numFields();
    if (numFields == 0) {
        return "";
    }

    // Header
    if (options.includeHeader) {
        for (int i = 0; i < numFields; ++i) {
            if (i > 0) out << options.delimiter;
            out << escapeCSVField(result.fieldName(i), options);
        }
        out << options.lineEnding;
    }

    // Rows
    while (result.fetchRow()) {
        for (int i = 0; i < numFields; ++i) {
            if (i > 0) out << options.delimiter;

            if (!result.isNull(i)) {
                const char* value = result.getValue(i);
                if (value) {
                    out << escapeCSVField(value, options);
                }
            }
            // NULL values are represented as empty
        }
        out << options.lineEnding;
    }

    return out.str();
}

std::string OracleFormatConverter::toJSON(OracleResultSet& result, const JSONOptions& options) {
    int numFields = result.numFields();
    if (numFields == 0) {
        return options.arrayFormat ? "[]" : "{\"rows\": []}";
    }

    std::vector<std::string> columnNames;
    std::vector<int> columnTypes;
    columnNames.reserve(numFields);
    columnTypes.reserve(numFields);

    for (int i = 0; i < numFields; ++i) {
        columnNames.push_back(result.fieldName(i));
        columnTypes.push_back(result.fieldType(i));
    }

    json arr = json::array();

    while (result.fetchRow()) {
        json obj = json::object();

        for (int i = 0; i < numFields; ++i) {
            if (!result.isNull(i)) {
                const char* value = result.getValue(i);
                if (value) {
                    // Try to preserve numeric types
                    if (isNumericType(columnTypes[i])) {
                        try {
                            std::string strVal(value);
                            if (strVal.find('.') != std::string::npos) {
                                obj[columnNames[i]] = std::stod(strVal);
                            } else {
                                obj[columnNames[i]] = std::stoll(strVal);
                            }
                        } catch (...) {
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

    if (options.arrayFormat) {
        return options.pretty ? arr.dump(options.indent) : arr.dump();
    } else {
        json wrapper = json::object();
        wrapper["rows"] = std::move(arr);
        return options.pretty ? wrapper.dump(options.indent) : wrapper.dump();
    }
}

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

std::string OracleFormatConverter::escapeIdentifier(const std::string& identifier) {
    // Handle schema.table format by escaping each part separately
    auto dot_pos = identifier.find('.');
    if (dot_pos != std::string::npos) {
        std::string schema = identifier.substr(0, dot_pos);
        std::string table = identifier.substr(dot_pos + 1);
        return escapeIdentifier(schema) + "." + escapeIdentifier(table);
    }

    // Oracle uses double quotes for identifiers
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

std::string OracleFormatConverter::escapeSQL(const std::string& value) {
    // Oracle uses doubled single quotes for escaping
    std::string result;
    result.reserve(value.size() * 2);

    for (char c : value) {
        if (c == '\'') {
            result += "''";
        } else {
            result += c;
        }
    }

    return result;
}

}  // namespace sqlfuse
