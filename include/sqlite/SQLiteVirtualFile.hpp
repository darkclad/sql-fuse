#pragma once

#include "VirtualFile.hpp"
#include "SQLiteConnectionPool.hpp"

namespace sqlfuse {

class SQLiteVirtualFile : public VirtualFile {
public:
    SQLiteVirtualFile(const ParsedPath& path,
                      SchemaManager& schema,
                      CacheManager& cache,
                      const DataConfig& config);
    ~SQLiteVirtualFile() override = default;

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
    SQLiteConnectionPool* getPool();
};

}  // namespace sqlfuse
