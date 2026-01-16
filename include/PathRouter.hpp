#pragma once

#include <string>
#include <string_view>
#include <optional>
#include <vector>
#include <regex>

namespace sqlfuse {

enum class NodeType {
    Root,
    Database,
    TablesDir,
    ViewsDir,
    ProceduresDir,
    FunctionsDir,
    TriggersDir,
    TableFile,
    TableDir,
    TableSchema,
    TableIndexes,
    TableStats,
    TableRowsDir,
    TableRowFile,
    ViewFile,
    ViewDir,
    ProcedureFile,
    FunctionFile,
    TriggerFile,
    ServerInfo,
    UsersDir,
    UserFile,
    VariablesDir,
    GlobalVariablesDir,
    SessionVariablesDir,
    VariableFile,
    DatabaseInfo,
    NotFound
};

enum class FileFormat {
    None,
    CSV,
    JSON,
    SQL
};

struct ParsedPath {
    NodeType type = NodeType::NotFound;
    std::string database;
    std::string object_name;
    FileFormat format = FileFormat::None;
    std::string row_id;
    std::string extra;

    bool isDirectory() const;
    bool isReadOnly() const;
    bool isVirtual() const;
    std::string getExtension() const;
};

class PathRouter {
public:
    PathRouter() = default;

    ParsedPath parse(std::string_view path) const;

    // Build paths for listing
    std::vector<std::string> getChildNames(const ParsedPath& parent) const;

    // Convert node type to string for debugging
    static std::string nodeTypeToString(NodeType type);

    // Get file extension for format
    static std::string formatToExtension(FileFormat format);
    static FileFormat extensionToFormat(std::string_view ext);

private:
    std::vector<std::string> splitPath(std::string_view path) const;
    FileFormat detectFormat(std::string_view filename) const;
    std::string stripExtension(std::string_view filename) const;
};

}  // namespace sqlfuse
