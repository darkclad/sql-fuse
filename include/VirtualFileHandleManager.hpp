#pragma once

#include "Config.hpp"
#include <cstdint>
#include <memory>
#include <mutex>
#include <atomic>
#include <unordered_map>

namespace sqlfuse {

// Forward declarations
class VirtualFile;
class SchemaManager;
class CacheManager;
struct ParsedPath;

// Manages open virtual file handles
class VirtualFileHandleManager {
public:
    VirtualFileHandleManager(SchemaManager& schema,
                             CacheManager& cache,
                             const DataConfig& config);

    // Create a new file handle
    uint64_t create(const ParsedPath& path);

    // Get file by handle
    VirtualFile* get(uint64_t handle);

    // Release file handle
    void release(uint64_t handle);

    // Get number of open handles
    size_t openCount() const;

private:
    SchemaManager& m_schema;
    CacheManager& m_cache;
    DataConfig m_config;

    std::unordered_map<uint64_t, std::unique_ptr<VirtualFile>> m_handles;
    std::atomic<uint64_t> m_nextHandle{1};
    mutable std::mutex m_mutex;
};

}  // namespace sqlfuse
