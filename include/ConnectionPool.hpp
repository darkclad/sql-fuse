#pragma once

#include <cstddef>
#include <memory>

namespace sqlfuse {

// Forward declarations
class VirtualFile;
class SchemaManager;
class CacheManager;
struct ParsedPath;
struct DataConfig;

class ConnectionPool {
public:
    virtual ~ConnectionPool() = default;

    // Non-copyable, non-movable
    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;

    // Pool statistics
    virtual size_t availableCount() const = 0;
    virtual size_t totalCount() const = 0;

    // Health check
    virtual bool healthCheck() = 0;

    // Drain all connections
    virtual void drain() = 0;

    // Factory method for creating database-specific virtual files
    virtual std::unique_ptr<VirtualFile> createVirtualFile(
        const ParsedPath& path,
        SchemaManager& schema,
        CacheManager& cache,
        const DataConfig& config) = 0;

protected:
    ConnectionPool() = default;
};

}  // namespace sqlfuse
