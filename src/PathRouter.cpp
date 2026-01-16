#include "PathRouter.hpp"
#include <algorithm>

namespace sqlfuse {

bool ParsedPath::isDirectory() const {
    switch (type) {
        case NodeType::Root:
        case NodeType::Database:
        case NodeType::TablesDir:
        case NodeType::ViewsDir:
        case NodeType::ProceduresDir:
        case NodeType::FunctionsDir:
        case NodeType::TriggersDir:
        case NodeType::TableDir:
        case NodeType::ViewDir:
        case NodeType::TableRowsDir:
        case NodeType::UsersDir:
        case NodeType::VariablesDir:
        case NodeType::GlobalVariablesDir:
        case NodeType::SessionVariablesDir:
            return true;
        default:
            return false;
    }
}

bool ParsedPath::isReadOnly() const {
    switch (type) {
        case NodeType::Root:
        case NodeType::Database:
        case NodeType::TablesDir:
        case NodeType::ViewsDir:
        case NodeType::ProceduresDir:
        case NodeType::FunctionsDir:
        case NodeType::TriggersDir:
        case NodeType::TableDir:
        case NodeType::TableRowsDir:
        case NodeType::ViewDir:
        case NodeType::TableSchema:
        case NodeType::TableIndexes:
        case NodeType::TableStats:
        case NodeType::ProcedureFile:
        case NodeType::FunctionFile:
        case NodeType::TriggerFile:
        case NodeType::ServerInfo:
        case NodeType::DatabaseInfo:
        case NodeType::UsersDir:
        case NodeType::UserFile:
        case NodeType::VariablesDir:
        case NodeType::GlobalVariablesDir:
        case NodeType::SessionVariablesDir:
        case NodeType::VariableFile:
            return true;
        case NodeType::TableFile:
        case NodeType::ViewFile:
        case NodeType::TableRowFile:
            return false;  // These can be written to
        default:
            return true;
    }
}

bool ParsedPath::isVirtual() const {
    // All nodes in this filesystem are virtual (generated on demand)
    return true;
}

std::string ParsedPath::getExtension() const {
    return PathRouter::formatToExtension(format);
}

std::vector<std::string> PathRouter::splitPath(std::string_view path) const {
    std::vector<std::string> parts;
    std::string current;

    for (char c : path) {
        if (c == '/') {
            if (!current.empty()) {
                parts.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }

    if (!current.empty()) {
        parts.push_back(current);
    }

    return parts;
}

FileFormat PathRouter::detectFormat(std::string_view filename) const {
    auto dot_pos = filename.rfind('.');
    if (dot_pos == std::string_view::npos) {
        return FileFormat::None;
    }

    auto ext = filename.substr(dot_pos + 1);
    return extensionToFormat(ext);
}

std::string PathRouter::stripExtension(std::string_view filename) const {
    auto dot_pos = filename.rfind('.');
    if (dot_pos == std::string_view::npos) {
        return std::string(filename);
    }
    return std::string(filename.substr(0, dot_pos));
}

ParsedPath PathRouter::parse(std::string_view path) const {
    ParsedPath result;
    auto parts = splitPath(path);

    if (parts.empty()) {
        result.type = NodeType::Root;
        return result;
    }

    // Check for special root-level files/dirs
    if (parts[0] == ".server_info") {
        result.type = NodeType::ServerInfo;
        return result;
    }

    if (parts[0] == ".users") {
        if (parts.size() == 1) {
            result.type = NodeType::UsersDir;
        } else {
            result.type = NodeType::UserFile;
            result.object_name = parts[1];
        }
        return result;
    }

    if (parts[0] == ".variables") {
        if (parts.size() == 1) {
            result.type = NodeType::VariablesDir;
        } else if (parts[1] == "global") {
            if (parts.size() == 2) {
                result.type = NodeType::GlobalVariablesDir;
            } else {
                result.type = NodeType::VariableFile;
                result.object_name = parts[2];
                result.extra = "global";
            }
        } else if (parts[1] == "session") {
            if (parts.size() == 2) {
                result.type = NodeType::SessionVariablesDir;
            } else {
                result.type = NodeType::VariableFile;
                result.object_name = parts[2];
                result.extra = "session";
            }
        }
        return result;
    }

    // First part should be database name
    result.database = parts[0];

    if (parts.size() == 1) {
        result.type = NodeType::Database;
        return result;
    }

    // Check for .info file in database
    if (parts[1] == ".info") {
        result.type = NodeType::DatabaseInfo;
        return result;
    }

    // Second part is the object type directory
    const std::string& object_type = parts[1];

    if (object_type == "tables") {
        if (parts.size() == 2) {
            result.type = NodeType::TablesDir;
            return result;
        }

        // parts[2] is either table.ext or table directory
        std::string table_part = parts[2];
        FileFormat format = detectFormat(table_part);

        if (format != FileFormat::None) {
            // It's a file like table.csv, table.json, table.sql
            result.type = NodeType::TableFile;
            result.object_name = stripExtension(table_part);
            result.format = format;
            return result;
        }

        // It's a table directory
        result.object_name = table_part;

        if (parts.size() == 3) {
            result.type = NodeType::TableDir;
            return result;
        }

        // Check subdirectories/files within table directory
        const std::string& sub = parts[3];

        if (sub == ".schema") {
            result.type = NodeType::TableSchema;
        } else if (sub == ".indexes") {
            result.type = NodeType::TableIndexes;
        } else if (sub == ".stats") {
            result.type = NodeType::TableStats;
        } else if (sub == "rows") {
            if (parts.size() == 4) {
                result.type = NodeType::TableRowsDir;
            } else {
                // parts[4] should be row_id.json
                result.type = NodeType::TableRowFile;
                std::string row_file = parts[4];
                result.format = detectFormat(row_file);
                result.row_id = stripExtension(row_file);
            }
        } else {
            result.type = NodeType::NotFound;
        }
        return result;
    }

    if (object_type == "views") {
        if (parts.size() == 2) {
            result.type = NodeType::ViewsDir;
            return result;
        }

        std::string view_part = parts[2];
        FileFormat format = detectFormat(view_part);

        if (format != FileFormat::None) {
            result.type = NodeType::ViewFile;
            result.object_name = stripExtension(view_part);
            result.format = format;
        } else {
            // View directory
            result.type = NodeType::ViewDir;
            result.object_name = view_part;
        }
        return result;
    }

    if (object_type == "procedures") {
        if (parts.size() == 2) {
            result.type = NodeType::ProceduresDir;
            return result;
        }
        result.type = NodeType::ProcedureFile;
        result.object_name = stripExtension(parts[2]);
        result.format = FileFormat::SQL;
        return result;
    }

    if (object_type == "functions") {
        if (parts.size() == 2) {
            result.type = NodeType::FunctionsDir;
            return result;
        }
        result.type = NodeType::FunctionFile;
        result.object_name = stripExtension(parts[2]);
        result.format = FileFormat::SQL;
        return result;
    }

    if (object_type == "triggers") {
        if (parts.size() == 2) {
            result.type = NodeType::TriggersDir;
            return result;
        }
        result.type = NodeType::TriggerFile;
        result.object_name = stripExtension(parts[2]);
        result.format = FileFormat::SQL;
        return result;
    }

    result.type = NodeType::NotFound;
    return result;
}

std::vector<std::string> PathRouter::getChildNames(const ParsedPath& parent) const {
    // This is a placeholder - actual implementation would query the database
    return {};
}

std::string PathRouter::nodeTypeToString(NodeType type) {
    switch (type) {
        case NodeType::Root: return "Root";
        case NodeType::Database: return "Database";
        case NodeType::TablesDir: return "TablesDir";
        case NodeType::ViewsDir: return "ViewsDir";
        case NodeType::ProceduresDir: return "ProceduresDir";
        case NodeType::FunctionsDir: return "FunctionsDir";
        case NodeType::TriggersDir: return "TriggersDir";
        case NodeType::TableFile: return "TableFile";
        case NodeType::TableDir: return "TableDir";
        case NodeType::TableSchema: return "TableSchema";
        case NodeType::TableIndexes: return "TableIndexes";
        case NodeType::TableStats: return "TableStats";
        case NodeType::TableRowsDir: return "TableRowsDir";
        case NodeType::TableRowFile: return "TableRowFile";
        case NodeType::ViewFile: return "ViewFile";
        case NodeType::ViewDir: return "ViewDir";
        case NodeType::ProcedureFile: return "ProcedureFile";
        case NodeType::FunctionFile: return "FunctionFile";
        case NodeType::TriggerFile: return "TriggerFile";
        case NodeType::ServerInfo: return "ServerInfo";
        case NodeType::UsersDir: return "UsersDir";
        case NodeType::UserFile: return "UserFile";
        case NodeType::VariablesDir: return "VariablesDir";
        case NodeType::GlobalVariablesDir: return "GlobalVariablesDir";
        case NodeType::SessionVariablesDir: return "SessionVariablesDir";
        case NodeType::VariableFile: return "VariableFile";
        case NodeType::DatabaseInfo: return "DatabaseInfo";
        case NodeType::NotFound: return "NotFound";
        default: return "Unknown";
    }
}

std::string PathRouter::formatToExtension(FileFormat format) {
    switch (format) {
        case FileFormat::CSV: return ".csv";
        case FileFormat::JSON: return ".json";
        case FileFormat::SQL: return ".sql";
        default: return "";
    }
}

FileFormat PathRouter::extensionToFormat(std::string_view ext) {
    if (ext == "csv") return FileFormat::CSV;
    if (ext == "json") return FileFormat::JSON;
    if (ext == "sql") return FileFormat::SQL;
    return FileFormat::None;
}

}  // namespace sqlfuse
