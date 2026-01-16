#pragma once

#include "VirtualFile.hpp"
#include "PostgreSQLConnectionPool.hpp"

namespace sqlfuse {

class PostgreSQLVirtualFile : public VirtualFile {
public:
    PostgreSQLVirtualFile(const ParsedPath& path,
                     SchemaManager& schema,
                     CacheManager& cache,
                     const DataConfig& config);
    ~PostgreSQLVirtualFile() override = default;

protected:
    // Database-dependent content generators
    std::string generateTableCSV() override;
    std::string generateTableJSON() override;
    std::string generateRowJSON() override;
    std::string generateViewContent() override;
    std::string generateDatabaseInfo() override;
    std::string generateUserInfo() override;

    // Database-dependent write handlers
    int handleTableWrite() override;
    int handleRowWrite() override;

private:
    PostgreSQLConnectionPool* getPool();
};

}  // namespace sqlfuse
