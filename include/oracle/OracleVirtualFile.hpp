#pragma once

#include "VirtualFile.hpp"
#include "OracleConnectionPool.hpp"

namespace sqlfuse {

class OracleVirtualFile : public VirtualFile {
public:
    OracleVirtualFile(const ParsedPath& path,
                     SchemaManager& schema,
                     CacheManager& cache,
                     const DataConfig& config);
    ~OracleVirtualFile() override = default;

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
    OracleConnectionPool* getPool();
};

}  // namespace sqlfuse
