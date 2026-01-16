#pragma once

#include "SchemaManager.hpp"
#include "PostgreSQLConnectionPool.hpp"

namespace sqlfuse {

class PostgreSQLSchemaManager : public SchemaManager {
public:
    PostgreSQLSchemaManager(PostgreSQLConnectionPool& pool, CacheManager& cache);
    ~PostgreSQLSchemaManager() override = default;

    // Database operations
    std::vector<std::string> getDatabases() override;
    bool databaseExists(const std::string& database) override;

    // Table operations
    std::vector<std::string> getTables(const std::string& database) override;
    std::optional<TableInfo> getTableInfo(const std::string& database,
                                           const std::string& table) override;
    std::vector<ColumnInfo> getColumns(const std::string& database,
                                        const std::string& table) override;
    std::vector<IndexInfo> getIndexes(const std::string& database,
                                       const std::string& table) override;
    bool tableExists(const std::string& database, const std::string& table) override;

    // View operations
    std::vector<std::string> getViews(const std::string& database) override;
    std::optional<ViewInfo> getViewInfo(const std::string& database,
                                         const std::string& view) override;

    // Routine operations
    std::vector<std::string> getProcedures(const std::string& database) override;
    std::vector<std::string> getFunctions(const std::string& database) override;
    std::optional<RoutineInfo> getRoutineInfo(const std::string& database,
                                               const std::string& name,
                                               const std::string& type) override;

    // Trigger operations
    std::vector<std::string> getTriggers(const std::string& database) override;
    std::optional<TriggerInfo> getTriggerInfo(const std::string& database,
                                               const std::string& trigger) override;

    // DDL statements
    std::string getCreateStatement(const std::string& database,
                                   const std::string& object,
                                   const std::string& type) override;

    // Server info
    ServerInfo getServerInfo() override;
    std::vector<UserInfo> getUsers() override;
    std::unordered_map<std::string, std::string> getGlobalVariables() override;
    std::unordered_map<std::string, std::string> getSessionVariables() override;

    // Row operations
    std::vector<std::string> getRowIds(const std::string& database,
                                        const std::string& table,
                                        size_t limit = 1000,
                                        size_t offset = 0) override;
    uint64_t getRowCount(const std::string& database, const std::string& table) override;

    // Cache invalidation
    void invalidateTable(const std::string& database, const std::string& table) override;
    void invalidateDatabase(const std::string& database) override;
    void invalidateAll() override;

    // Access connection pool
    ConnectionPool& connectionPool() override { return m_pool; }

    // PostgreSQL-specific: get schemas within a database
    std::vector<std::string> getSchemas(const std::string& database);

private:
    std::string escapeIdentifier(const std::string& id) const;
    std::string escapeString(const std::string& str) const;
    std::string getPrimaryKeyColumn(const std::string& database, const std::string& table);

    PostgreSQLConnectionPool& m_pool;
    CacheManager& m_cache;
};

}  // namespace sqlfuse
