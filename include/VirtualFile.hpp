#pragma once

#include "PathRouter.hpp"
#include "SchemaManager.hpp"
#include "CacheManager.hpp"
#include "Config.hpp"
#include <string>
#include <memory>
#include <mutex>

namespace sqlfuse {

// Abstract base class for virtual files
class VirtualFile {
public:
    VirtualFile(const ParsedPath& path,
                SchemaManager& schema,
                CacheManager& cache,
                const DataConfig& config);
    virtual ~VirtualFile() = default;

    // Get file content (generates on demand if not cached)
    std::string getContent();

    // Get file size (may trigger content generation)
    size_t getSize();

    // Check if content is available
    bool hasContent() const;

    // Write operations
    int write(const char* data, size_t size, off_t offset);
    int truncate(off_t size);
    int flush();

    // Status
    bool isModified() const { return m_modified; }
    bool isReadOnly() const;

    // Get last error message
    const std::string& lastError() const { return m_lastError; }

protected:
    // Database-independent content generators (implemented in base class)
    std::string generateTableSQL();
    std::string generateTableSchema();
    std::string generateTableIndexes();
    std::string generateTableStats();
    std::string generateProcedureSQL();
    std::string generateFunctionSQL();
    std::string generateTriggerSQL();
    std::string generateServerInfo();
    std::string generateVariableContent();

    // Database-dependent content generators (pure virtual)
    virtual std::string generateTableCSV() = 0;
    virtual std::string generateTableJSON() = 0;
    virtual std::string generateRowJSON() = 0;
    virtual std::string generateViewContent() = 0;
    virtual std::string generateDatabaseInfo() = 0;
    virtual std::string generateUserInfo() = 0;

    // Database-dependent write handlers (pure virtual)
    virtual int handleTableWrite() = 0;
    virtual int handleRowWrite() = 0;

    // Helper methods
    std::string getCacheKey() const;
    void loadContent();

    ParsedPath m_path;              // Stored by value (caller's path is temporary)
    SchemaManager& m_schema;
    CacheManager& m_cache;
    const DataConfig& m_config;

    std::string m_content;
    std::string m_writeBuffer;
    bool m_contentLoaded = false;
    bool m_modified = false;
    std::string m_lastError;

    mutable std::mutex m_mutex;
};

}  // namespace sqlfuse
