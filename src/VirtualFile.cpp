#include "VirtualFile.hpp"
#include "FormatConverter.hpp"
#include "ErrorHandler.hpp"
#include <spdlog/spdlog.h>
#include <sstream>
#include <iomanip>

namespace sqlfuse {

VirtualFile::VirtualFile(const ParsedPath& path,
                         SchemaManager& schema,
                         CacheManager& cache,
                         const DataConfig& config)
    : m_path(path), m_schema(schema), m_cache(cache), m_config(config) {
}

std::string VirtualFile::getContent() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_contentLoaded) {
        loadContent();
    }

    return m_content;
}

size_t VirtualFile::getSize() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_contentLoaded) {
        loadContent();
    }

    return m_content.size();
}

bool VirtualFile::hasContent() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_contentLoaded;
}

int VirtualFile::write(const char* data, size_t size, off_t offset) {
    if (isReadOnly()) {
        return -EROFS;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    // Ensure buffer is large enough
    if (static_cast<size_t>(offset) + size > m_writeBuffer.size()) {
        m_writeBuffer.resize(static_cast<size_t>(offset) + size);
    }

    std::memcpy(m_writeBuffer.data() + offset, data, size);
    m_modified = true;

    return static_cast<int>(size);
}

int VirtualFile::truncate(off_t size) {
    if (isReadOnly()) {
        return -EROFS;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    m_writeBuffer.resize(static_cast<size_t>(size));
    m_modified = true;

    return 0;
}

int VirtualFile::flush() {
    if (!m_modified) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    int result = 0;

    switch (m_path.type) {
        case NodeType::TableFile:
            result = handleTableWrite();
            break;
        case NodeType::TableRowFile:
            result = handleRowWrite();
            break;
        default:
            result = -EROFS;
            break;
    }

    if (result == 0) {
        m_modified = false;
        m_writeBuffer.clear();

        // Invalidate cache for this table
        if (!m_path.database.empty() && !m_path.object_name.empty()) {
            m_cache.invalidateTable(m_path.database, m_path.object_name);
        }
    }

    return result;
}

bool VirtualFile::isReadOnly() const {
    return m_path.isReadOnly();
}

std::string VirtualFile::getCacheKey() const {
    std::string key = m_path.database;

    if (!m_path.object_name.empty()) {
        key += "/" + m_path.object_name;
    }

    if (m_path.format != FileFormat::None) {
        key += PathRouter::formatToExtension(m_path.format);
    }

    if (!m_path.row_id.empty()) {
        key += "/rows/" + m_path.row_id;
    }

    return key;
}

void VirtualFile::loadContent() {
    // Try cache first
    std::string cache_key = getCacheKey();
    if (auto cached = m_cache.get(cache_key)) {
        m_content = std::move(*cached);
        m_contentLoaded = true;
        return;
    }

    // Generate content based on node type
    try {
        switch (m_path.type) {
            case NodeType::TableFile:
                switch (m_path.format) {
                    case FileFormat::CSV:
                        m_content = generateTableCSV();
                        break;
                    case FileFormat::JSON:
                        m_content = generateTableJSON();
                        break;
                    case FileFormat::SQL:
                        m_content = generateTableSQL();
                        break;
                    default:
                        m_content = "";
                }
                break;

            case NodeType::TableSchema:
                m_content = generateTableSchema();
                break;

            case NodeType::TableIndexes:
                m_content = generateTableIndexes();
                break;

            case NodeType::TableStats:
                m_content = generateTableStats();
                break;

            case NodeType::TableRowFile:
                m_content = generateRowJSON();
                break;

            case NodeType::ViewFile:
                m_content = generateViewContent();
                break;

            case NodeType::ProcedureFile:
                m_content = generateProcedureSQL();
                break;

            case NodeType::FunctionFile:
                m_content = generateFunctionSQL();
                break;

            case NodeType::TriggerFile:
                m_content = generateTriggerSQL();
                break;

            case NodeType::ServerInfo:
                m_content = generateServerInfo();
                break;

            case NodeType::DatabaseInfo:
                m_content = generateDatabaseInfo();
                break;

            case NodeType::UserFile:
                m_content = generateUserInfo();
                break;

            case NodeType::VariableFile:
                m_content = generateVariableContent();
                break;

            default:
                m_content = "";
        }

        m_contentLoaded = true;

        // Cache the content
        if (!m_content.empty()) {
            m_cache.put(cache_key, m_content, CacheManager::Category::Data);
        }

    } catch (const std::exception& e) {
        m_lastError = e.what();
        spdlog::error("Failed to load content for {}: {}", cache_key, e.what());
        m_content = "";
        m_contentLoaded = true;
    }
}

// Database-independent generators

std::string VirtualFile::generateTableSQL() {
    return m_schema.getCreateStatement(m_path.database, m_path.object_name, "TABLE") + ";\n";
}

std::string VirtualFile::generateTableSchema() {
    auto columns = m_schema.getColumns(m_path.database, m_path.object_name);

    std::ostringstream out;

    // Header
    out << std::left
        << std::setw(30) << "Column"
        << std::setw(30) << "Type"
        << std::setw(8) << "Null"
        << std::setw(8) << "Key"
        << std::setw(20) << "Default"
        << "Extra" << "\n";

    out << std::string(100, '-') << "\n";

    for (const auto& col : columns) {
        out << std::left
            << std::setw(30) << col.name
            << std::setw(30) << col.fullType
            << std::setw(8) << (col.nullable ? "YES" : "NO")
            << std::setw(8) << col.key
            << std::setw(20) << (col.defaultValue.empty() ? "NULL" : col.defaultValue)
            << col.extra << "\n";
    }

    return out.str();
}

std::string VirtualFile::generateTableIndexes() {
    auto indexes = m_schema.getIndexes(m_path.database, m_path.object_name);

    std::ostringstream out;

    out << std::left
        << std::setw(30) << "Index"
        << std::setw(10) << "Unique"
        << std::setw(15) << "Type"
        << "Columns" << "\n";

    out << std::string(80, '-') << "\n";

    for (const auto& idx : indexes) {
        out << std::left
            << std::setw(30) << idx.name
            << std::setw(10) << (idx.unique ? "YES" : "NO")
            << std::setw(15) << idx.type;

        bool first = true;
        for (const auto& col : idx.columns) {
            if (!first) out << ", ";
            out << col;
            first = false;
        }
        out << "\n";
    }

    return out.str();
}

std::string VirtualFile::generateTableStats() {
    auto info = m_schema.getTableInfo(m_path.database, m_path.object_name);
    if (!info) {
        return "Table not found\n";
    }

    std::ostringstream out;

    out << "Table: " << info->name << "\n";
    out << "Database: " << info->database << "\n";
    out << "Engine: " << info->engine << "\n";
    out << "Collation: " << info->collation << "\n";
    out << "Rows (estimate): " << info->rowsEstimate << "\n";
    out << "Data length: " << info->dataLength << " bytes\n";
    out << "Index length: " << info->indexLength << " bytes\n";
    out << "Auto increment: " << info->autoIncrement << "\n";
    out << "Created: " << info->createTime << "\n";
    out << "Updated: " << info->updateTime << "\n";

    if (!info->comment.empty()) {
        out << "Comment: " << info->comment << "\n";
    }

    return out.str();
}

std::string VirtualFile::generateProcedureSQL() {
    return m_schema.getCreateStatement(m_path.database, m_path.object_name, "PROCEDURE") + ";\n";
}

std::string VirtualFile::generateFunctionSQL() {
    return m_schema.getCreateStatement(m_path.database, m_path.object_name, "FUNCTION") + ";\n";
}

std::string VirtualFile::generateTriggerSQL() {
    return m_schema.getCreateStatement(m_path.database, m_path.object_name, "TRIGGER") + ";\n";
}

std::string VirtualFile::generateServerInfo() {
    auto info = m_schema.getServerInfo();

    std::ostringstream out;

    out << "Server Information\n";
    out << std::string(40, '=') << "\n\n";
    out << "Version: " << info.version << "\n";
    out << "Version Comment: " << info.versionComment << "\n";
    out << "Hostname: " << info.hostname << "\n";
    out << "Port: " << info.port << "\n";
    out << "Uptime: " << info.uptime << " seconds\n";
    out << "Threads Connected: " << info.threadsConnected << "\n";
    out << "Threads Running: " << info.threadsRunning << "\n";
    out << "Questions: " << info.questions << "\n";
    out << "Slow Queries: " << info.slowQueries << "\n";

    return out.str();
}

std::string VirtualFile::generateVariableContent() {
    std::unordered_map<std::string, std::string> vars;

    if (m_path.extra == "global") {
        vars = m_schema.getGlobalVariables();
    } else {
        vars = m_schema.getSessionVariables();
    }

    auto it = vars.find(m_path.object_name);
    if (it == vars.end()) {
        return "";
    }

    return it->second + "\n";
}

}  // namespace sqlfuse
