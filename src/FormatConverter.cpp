#include "FormatConverter.hpp"
#include <sstream>
#include <algorithm>
#include <stdexcept>

namespace sqlfuse {

std::string FormatConverter::toCSV(const std::vector<std::string>& columns,
                                   const std::vector<std::vector<SqlValue>>& rows,
                                   const CSVOptions& options) {
    std::ostringstream out;

    // Header
    if (options.includeHeader) {
        for (size_t i = 0; i < columns.size(); ++i) {
            if (i > 0) out << options.delimiter;
            out << escapeCSVField(columns[i], options);
        }
        out << options.lineEnding;
    }

    // Rows
    for (const auto& row : rows) {
        for (size_t i = 0; i < row.size(); ++i) {
            if (i > 0) out << options.delimiter;

            if (row[i].has_value()) {
                out << escapeCSVField(row[i].value(), options);
            }
        }
        out << options.lineEnding;
    }

    return out.str();
}

std::string FormatConverter::toJSON(const std::vector<std::string>& columns,
                                    const std::vector<std::vector<SqlValue>>& rows,
                                    const JSONOptions& options) {
    json arr = json::array();

    for (const auto& row : rows) {
        json obj = json::object();

        for (size_t i = 0; i < std::min(columns.size(), row.size()); ++i) {
            if (row[i].has_value()) {
                obj[columns[i]] = row[i].value();
            } else if (options.includeNull) {
                obj[columns[i]] = nullptr;
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

std::string FormatConverter::rowToJSON(const std::vector<std::string>& columns,
                                       const std::vector<SqlValue>& values,
                                       const JSONOptions& options) {
    json obj = json::object();

    for (size_t i = 0; i < std::min(columns.size(), values.size()); ++i) {
        if (values[i].has_value()) {
            obj[columns[i]] = values[i].value();
        } else if (options.includeNull) {
            obj[columns[i]] = nullptr;
        }
    }

    return options.pretty ? obj.dump(options.indent) : obj.dump();
}

std::string FormatConverter::rowToJSON(const RowData& row, const JSONOptions& options) {
    json obj = json::object();

    for (const auto& [key, value] : row) {
        if (value.has_value()) {
            obj[key] = value.value();
        } else if (options.includeNull) {
            obj[key] = nullptr;
        }
    }

    return options.pretty ? obj.dump(options.indent) : obj.dump();
}

std::vector<RowData> FormatConverter::parseCSV(const std::string& data,
                                                const CSVOptions& options) {
    std::vector<RowData> result;

    if (data.empty()) {
        return result;
    }

    std::istringstream stream(data);
    std::string line;
    std::vector<std::string> headers;
    bool firstLine = true;

    while (std::getline(stream, line)) {
        // Remove trailing \r if present
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.empty()) continue;

        auto fields = splitCSVLine(line, options);

        if (firstLine && options.includeHeader) {
            headers = std::move(fields);
            firstLine = false;
            continue;
        }

        if (headers.empty()) {
            // No headers - use column indices
            for (size_t i = 0; i < fields.size(); ++i) {
                headers.push_back("col" + std::to_string(i));
            }
            firstLine = false;
        }

        RowData row;
        for (size_t i = 0; i < std::min(headers.size(), fields.size()); ++i) {
            if (fields[i].empty()) {
                row[headers[i]] = std::nullopt;
            } else {
                row[headers[i]] = fields[i];
            }
        }

        result.push_back(std::move(row));
    }

    return result;
}

std::vector<RowData> FormatConverter::parseCSV(const std::string& data,
                                                const std::vector<std::string>& columns,
                                                const CSVOptions& options) {
    std::vector<RowData> result;

    if (data.empty()) {
        return result;
    }

    std::istringstream stream(data);
    std::string line;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.empty()) continue;

        auto fields = splitCSVLine(line, options);

        RowData row;
        for (size_t i = 0; i < std::min(columns.size(), fields.size()); ++i) {
            if (fields[i].empty()) {
                row[columns[i]] = std::nullopt;
            } else {
                row[columns[i]] = fields[i];
            }
        }

        result.push_back(std::move(row));
    }

    return result;
}

std::vector<RowData> FormatConverter::parseJSON(const std::string& data) {
    std::vector<RowData> result;

    if (data.empty()) {
        return result;
    }

    try {
        json parsed = json::parse(data);

        json rows_array;
        if (parsed.is_array()) {
            rows_array = parsed;
        } else if (parsed.is_object() && parsed.contains("rows")) {
            rows_array = parsed["rows"];
        } else if (parsed.is_object()) {
            // Single object - treat as one row
            rows_array = json::array({parsed});
        } else {
            return result;
        }

        for (const auto& item : rows_array) {
            if (!item.is_object()) continue;

            RowData row;
            for (auto& [key, value] : item.items()) {
                if (value.is_null()) {
                    row[key] = std::nullopt;
                } else if (value.is_string()) {
                    row[key] = value.get<std::string>();
                } else {
                    row[key] = value.dump();
                }
            }

            result.push_back(std::move(row));
        }
    } catch (const json::exception& e) {
        throw std::runtime_error("JSON parse error: " + std::string(e.what()));
    }

    return result;
}

RowData FormatConverter::parseJSONRow(const std::string& data) {
    RowData row;

    if (data.empty()) {
        return row;
    }

    try {
        json parsed = json::parse(data);

        if (!parsed.is_object()) {
            throw std::runtime_error("Expected JSON object");
        }

        for (auto& [key, value] : parsed.items()) {
            if (value.is_null()) {
                row[key] = std::nullopt;
            } else if (value.is_string()) {
                row[key] = value.get<std::string>();
            } else {
                row[key] = value.dump();
            }
        }
    } catch (const json::exception& e) {
        throw std::runtime_error("JSON parse error: " + std::string(e.what()));
    }

    return row;
}

std::string FormatConverter::escapeSQL(const std::string& value) {
    std::string result;
    result.reserve(value.size() * 2);

    for (char c : value) {
        switch (c) {
            case '\0': result += "\\0"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\\': result += "\\\\"; break;
            case '\'': result += "''"; break;  // Standard SQL escape
            case '"':  result += "\""; break;
            default:   result += c; break;
        }
    }

    return result;
}

std::string FormatConverter::escapeCSVField(const std::string& field,
                                             const CSVOptions& options) {
    bool needs_quoting = options.quoteAll;

    if (!needs_quoting) {
        for (char c : field) {
            if (c == options.delimiter || c == options.quote ||
                c == '\n' || c == '\r') {
                needs_quoting = true;
                break;
            }
        }
    }

    if (!needs_quoting) {
        return field;
    }

    std::string result;
    result.reserve(field.size() + 2);
    result += options.quote;

    for (char c : field) {
        if (c == options.quote) {
            result += options.quote;  // Double the quote
        }
        result += c;
    }

    result += options.quote;
    return result;
}

std::vector<std::string> FormatConverter::splitCSVLine(const std::string& line,
                                                        const CSVOptions& options) {
    std::vector<std::string> fields;
    std::string current;
    bool in_quotes = false;
    size_t i = 0;

    while (i < line.size()) {
        char c = line[i];

        if (in_quotes) {
            if (c == options.quote) {
                // Check for escaped quote
                if (i + 1 < line.size() && line[i + 1] == options.quote) {
                    current += options.quote;
                    i += 2;
                    continue;
                } else {
                    in_quotes = false;
                }
            } else {
                current += c;
            }
        } else {
            if (c == options.quote) {
                in_quotes = true;
            } else if (c == options.delimiter) {
                fields.push_back(current);
                current.clear();
            } else {
                current += c;
            }
        }

        ++i;
    }

    fields.push_back(current);
    return fields;
}

std::string FormatConverter::jsonValueToSQL(const json& value) {
    if (value.is_null()) {
        return "NULL";
    } else if (value.is_string()) {
        return "'" + escapeSQL(value.get<std::string>()) + "'";
    } else if (value.is_number_integer()) {
        return std::to_string(value.get<int64_t>());
    } else if (value.is_number_float()) {
        return std::to_string(value.get<double>());
    } else if (value.is_boolean()) {
        return value.get<bool>() ? "TRUE" : "FALSE";
    } else {
        return "'" + escapeSQL(value.dump()) + "'";
    }
}

}  // namespace sqlfuse
