#pragma once

#include "VirtualFile.hpp"
#include "MySQLConnectionPool.hpp"

namespace sqlfuse {

class MySQLVirtualFile : public VirtualFile {
public:
    MySQLVirtualFile(const ParsedPath& path,
                     SchemaManager& schema,
                     CacheManager& cache,
                     const DataConfig& config);
    ~MySQLVirtualFile() override = default;

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
    MySQLConnectionPool* getPool();
};

}  // namespace sqlfuse
