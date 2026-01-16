#pragma once

#include "CacheManager.hpp"
#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <unordered_map>

namespace sqlfuse {

// Forward declaration
class ConnectionPool;

struct ColumnInfo {
    std::string name;
    std::string type;
    std::string fullType;
    bool nullable = true;
    std::string defaultValue;
    std::string extra;
    std::string key;  // PRI, UNI, MUL
    std::string collation;
    std::string comment;
    int ordinalPosition = 0;
};

struct IndexInfo {
    std::string name;
    bool unique = false;
    bool primary = false;
    std::vector<std::string> columns;
    std::string type;  // BTREE, HASH, FULLTEXT
    std::string comment;
    uint64_t cardinality = 0;
};

struct TableInfo {
    std::string name;
    std::string database;
    std::string engine;
    std::string collation;
    std::string comment;
    std::string createTime;
    std::string updateTime;
    uint64_t rowsEstimate = 0;
    uint64_t dataLength = 0;
    uint64_t indexLength = 0;
    uint64_t autoIncrement = 0;
    std::vector<ColumnInfo> columns;
    std::vector<IndexInfo> indexes;
    std::string primaryKeyColumn;
};

struct ViewInfo {
    std::string name;
    std::string database;
    std::string definer;
    std::string securityType;
    bool isUpdatable = false;
    std::string checkOption;
};

struct RoutineInfo {
    std::string name;
    std::string database;
    std::string type;  // PROCEDURE or FUNCTION
    std::string definer;
    std::string returns;  // For functions
    std::string dataAccess;
    std::string securityType;
    bool deterministic = false;
    std::string comment;
    std::string created;
    std::string modified;
};

struct TriggerInfo {
    std::string name;
    std::string database;
    std::string table;
    std::string event;  // INSERT, UPDATE, DELETE
    std::string timing; // BEFORE, AFTER
    std::string statement;
    std::string definer;
    std::string created;
};

struct UserInfo {
    std::string user;
    std::string host;
    bool accountLocked = false;
    std::string passwordExpired;
};

struct ServerInfo {
    std::string version;
    std::string versionComment;
    uint64_t uptime = 0;
    uint64_t threadsConnected = 0;
    uint64_t threadsRunning = 0;
    uint64_t questions = 0;
    uint64_t slowQueries = 0;
    std::string hostname;
    uint16_t port = 0;
};

// Database type enumeration
enum class DatabaseType {
    MySQL,
    SQLite,
    PostgreSQL,  // Future
    Oracle       // Future
};

// Convert string to DatabaseType
DatabaseType parseDatabaseType(const std::string& type);

// Convert DatabaseType to string
std::string databaseTypeToString(DatabaseType type);

// Abstract base class for schema management
class SchemaManager {
public:
    virtual ~SchemaManager() = default;

    // Database operations
    virtual std::vector<std::string> getDatabases() = 0;
    virtual bool databaseExists(const std::string& database) = 0;

    // Table operations
    virtual std::vector<std::string> getTables(const std::string& database) = 0;
    virtual std::optional<TableInfo> getTableInfo(const std::string& database,
                                                   const std::string& table) = 0;
    virtual std::vector<ColumnInfo> getColumns(const std::string& database,
                                                const std::string& table) = 0;
    virtual std::vector<IndexInfo> getIndexes(const std::string& database,
                                               const std::string& table) = 0;
    virtual bool tableExists(const std::string& database, const std::string& table) = 0;

    // View operations
    virtual std::vector<std::string> getViews(const std::string& database) = 0;
    virtual std::optional<ViewInfo> getViewInfo(const std::string& database,
                                                 const std::string& view) = 0;

    // Routine operations
    virtual std::vector<std::string> getProcedures(const std::string& database) = 0;
    virtual std::vector<std::string> getFunctions(const std::string& database) = 0;
    virtual std::optional<RoutineInfo> getRoutineInfo(const std::string& database,
                                                       const std::string& name,
                                                       const std::string& type) = 0;

    // Trigger operations
    virtual std::vector<std::string> getTriggers(const std::string& database) = 0;
    virtual std::optional<TriggerInfo> getTriggerInfo(const std::string& database,
                                                       const std::string& trigger) = 0;

    // DDL statements
    virtual std::string getCreateStatement(const std::string& database,
                                           const std::string& object,
                                           const std::string& type) = 0;  // TABLE, VIEW, PROCEDURE, etc.

    // Server info
    virtual ServerInfo getServerInfo() = 0;
    virtual std::vector<UserInfo> getUsers() = 0;
    virtual std::unordered_map<std::string, std::string> getGlobalVariables() = 0;
    virtual std::unordered_map<std::string, std::string> getSessionVariables() = 0;

    // Row operations
    virtual std::vector<std::string> getRowIds(const std::string& database,
                                                const std::string& table,
                                                size_t limit = 1000,
                                                size_t offset = 0) = 0;
    virtual uint64_t getRowCount(const std::string& database, const std::string& table) = 0;

    // Cache invalidation
    virtual void invalidateTable(const std::string& database, const std::string& table) = 0;
    virtual void invalidateDatabase(const std::string& database) = 0;
    virtual void invalidateAll() = 0;

    // Access connection pool
    virtual ConnectionPool& connectionPool() = 0;

protected:
    SchemaManager() = default;
};

}  // namespace sqlfuse
