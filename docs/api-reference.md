# API Reference

This document provides detailed API documentation for SQL-FUSE internal components.

## Core Types

### DatabaseType

```cpp
enum class DatabaseType {
    MySQL,
    PostgreSQL,
    SQLite,
    Oracle  // Planned
};
```

### FileFormat

```cpp
enum class FileFormat {
    CSV,
    JSON,
    SQL,
    Unknown
};
```

### PathType

```cpp
enum class PathType {
    ROOT,
    SERVER_INFO,
    USERS_DIR,
    USER_FILE,
    VARIABLES_DIR,
    VARIABLES_SCOPE_DIR,
    VARIABLE_FILE,
    DATABASE,
    DATABASE_INFO,
    TABLES_DIR,
    TABLE_FILE,
    TABLE_DIR,
    TABLE_SCHEMA,
    ROWS_DIR,
    ROW_FILE,
    VIEWS_DIR,
    VIEW_FILE,
    FUNCTIONS_DIR,
    FUNCTION_FILE,
    PROCEDURES_DIR,
    PROCEDURE_FILE,
    TRIGGERS_DIR,
    TRIGGER_FILE,
    UNKNOWN
};
```

---

## Data Structures

### ParsedPath

```cpp
struct ParsedPath {
    PathType type;
    std::string database;
    std::string object_name;
    std::string row_id;
    FileFormat format;
    std::string scope;        // For variables (global/session)
    std::string variable;     // Variable name
};
```

### ColumnInfo

```cpp
struct ColumnInfo {
    std::string name;
    std::string type;
    bool nullable;
    std::string key;          // PRI, UNI, MUL, or empty
    std::string defaultValue;
    std::string extra;        // auto_increment, etc.
    std::string comment;
};
```

### TableInfo

```cpp
struct TableInfo {
    std::string database;
    std::string name;
    std::string engine;
    size_t rowCount;
    size_t dataSize;
    size_t indexSize;
    std::string createTime;
    std::string updateTime;
    std::string collation;
    std::string comment;
    std::string primaryKeyColumn;
    std::vector<ColumnInfo> columns;
};
```

### ViewInfo

```cpp
struct ViewInfo {
    std::string database;
    std::string name;
    bool isUpdatable;
    std::string checkOption;
    std::string definer;
    std::string securityType;
};
```

### RoutineInfo

```cpp
struct RoutineInfo {
    std::string database;
    std::string name;
    std::string type;         // FUNCTION or PROCEDURE
    std::string returns;      // Return type for functions
    std::string dataAccess;
    std::string securityType;
    bool deterministic;
    std::string definer;
    std::string comment;
};
```

### TriggerInfo

```cpp
struct TriggerInfo {
    std::string database;
    std::string name;
    std::string event;        // INSERT, UPDATE, DELETE
    std::string timing;       // BEFORE, AFTER
    std::string table;
    std::string statement;
    std::string definer;
};
```

### DatabaseInfo

```cpp
struct DatabaseInfo {
    std::string name;
    std::string characterSet;
    std::string collation;
    size_t tableCount;
    size_t viewCount;
    size_t totalSize;
};
```

### RowData

```cpp
using RowData = std::map<std::string, std::string>;
```

---

## SchemaManager Interface

### Database Operations

```cpp
// Get list of all accessible databases
virtual std::vector<std::string> getDatabases() = 0;

// Get detailed information about a database
virtual std::optional<DatabaseInfo> getDatabaseInfo(
    const std::string& database
) = 0;
```

### Table Operations

```cpp
// Get list of tables in a database
virtual std::vector<std::string> getTables(
    const std::string& database
) = 0;

// Get detailed table information
virtual std::optional<TableInfo> getTableInfo(
    const std::string& database,
    const std::string& table
) = 0;

// Get column definitions for a table
virtual std::vector<ColumnInfo> getColumns(
    const std::string& database,
    const std::string& table
) = 0;

// Get table data in specified format
virtual std::string getTableData(
    const std::string& database,
    const std::string& table,
    FileFormat format,
    size_t limit = 0
) = 0;

// Get CREATE TABLE statement
virtual std::string getCreateStatement(
    const std::string& database,
    const std::string& table
) = 0;
```

### Row Operations

```cpp
// Get list of row identifiers (primary keys)
virtual std::vector<std::string> getRowIds(
    const std::string& database,
    const std::string& table,
    size_t limit = 10000
) = 0;

// Get single row as JSON
virtual std::string getRow(
    const std::string& database,
    const std::string& table,
    const std::string& rowId
) = 0;

// Insert a new row
virtual bool insertRow(
    const std::string& database,
    const std::string& table,
    const RowData& data
) = 0;

// Update an existing row
virtual bool updateRow(
    const std::string& database,
    const std::string& table,
    const std::string& rowId,
    const RowData& data
) = 0;

// Delete a row
virtual bool deleteRow(
    const std::string& database,
    const std::string& table,
    const std::string& rowId
) = 0;
```

### View Operations

```cpp
// Get list of views
virtual std::vector<ViewInfo> getViews(
    const std::string& database
) = 0;

// Get view data in specified format
virtual std::string getViewData(
    const std::string& database,
    const std::string& view,
    FileFormat format,
    size_t limit = 0
) = 0;

// Get CREATE VIEW statement
virtual std::string getViewDefinition(
    const std::string& database,
    const std::string& view
) = 0;
```

### Routine Operations

```cpp
// Get list of functions
virtual std::vector<RoutineInfo> getFunctions(
    const std::string& database
) = 0;

// Get list of procedures
virtual std::vector<RoutineInfo> getProcedures(
    const std::string& database
) = 0;

// Get routine definition (CREATE FUNCTION/PROCEDURE)
virtual std::string getRoutineDefinition(
    const std::string& database,
    const std::string& name,
    const std::string& type  // "FUNCTION" or "PROCEDURE"
) = 0;
```

### Trigger Operations

```cpp
// Get list of triggers
virtual std::vector<TriggerInfo> getTriggers(
    const std::string& database
) = 0;

// Get trigger definition
virtual std::string getTriggerDefinition(
    const std::string& database,
    const std::string& name
) = 0;
```

### Server Operations

```cpp
// Get server version and info
virtual std::string getServerInfo() = 0;

// Get list of users (if permitted)
virtual std::vector<std::string> getUsers() = 0;

// Get user details
virtual std::string getUserInfo(const std::string& user) = 0;

// Get server variables
virtual std::map<std::string, std::string> getVariables(
    const std::string& scope  // "global" or "session"
) = 0;
```

---

## ConnectionPool Interface

```cpp
template<typename ConnectionType>
class ConnectionPool {
public:
    // Constructor
    ConnectionPool(
        const ConnectionConfig& config,
        size_t poolSize
    );

    // Destructor - drains all connections
    ~ConnectionPool();

    // Acquire a connection (blocks if none available)
    std::unique_ptr<ConnectionType> acquire();

    // Acquire with timeout
    std::unique_ptr<ConnectionType> acquire(
        std::chrono::milliseconds timeout
    );

    // Release connection back to pool
    void release(std::unique_ptr<ConnectionType> conn);

    // Check if connections are healthy
    bool healthCheck();

    // Close all connections
    void drain();

    // Pool statistics
    size_t available() const;
    size_t total() const;
    size_t waiting() const;
};
```

---

## CacheManager Interface

```cpp
class CacheManager {
public:
    // Constructor
    CacheManager(
        size_t maxSize,
        std::chrono::seconds ttl
    );

    // Get cached value
    std::optional<std::string> get(const std::string& key);

    // Store value in cache
    void put(const std::string& key, const std::string& value);

    // Remove specific key
    void invalidate(const std::string& key);

    // Remove all keys with prefix
    void invalidatePrefix(const std::string& prefix);

    // Convenience method for table invalidation
    void invalidateTable(
        const std::string& database,
        const std::string& table
    );

    // Clear entire cache
    void clear();

    // Statistics
    size_t size() const;
    size_t maxSize() const;
    size_t hits() const;
    size_t misses() const;
    double hitRate() const;
};
```

---

## FormatConverter Interface

### Output Conversion

```cpp
class FormatConverter {
public:
    // Convert rows to CSV
    static std::string toCSV(
        const std::vector<RowData>& rows,
        const std::vector<std::string>& columnOrder,
        const CSVOptions& options = {}
    );

    // Convert rows to JSON array
    static std::string toJSON(
        const std::vector<RowData>& rows,
        bool pretty = false
    );

    // Convert single row to JSON object
    static std::string rowToJSON(
        const RowData& row,
        bool pretty = false
    );
};
```

### Input Parsing

```cpp
class FormatConverter {
public:
    // Parse CSV to rows
    static std::vector<RowData> parseCSV(
        const std::string& data,
        const CSVOptions& options = {}
    );

    // Parse JSON array to rows
    static std::vector<RowData> parseJSON(
        const std::string& data
    );

    // Parse JSON object to single row
    static RowData parseJSONObject(
        const std::string& data
    );
};
```

### CSVOptions

```cpp
struct CSVOptions {
    char delimiter = ',';
    char quote = '"';
    char escape = '\\';
    bool includeHeader = true;
    std::string lineEnding = "\n";
    std::string nullValue = "";
};
```

---

## PathRouter Interface

```cpp
class PathRouter {
public:
    // Parse a filesystem path
    ParsedPath parse(const std::string& path);

    // Build a path from components
    std::string buildPath(const ParsedPath& parsed);

    // Utility methods
    static FileFormat parseFormat(const std::string& extension);
    static std::string formatExtension(FileFormat format);
    static bool isSpecialFile(const std::string& name);
};
```

---

## VirtualFile Interface

```cpp
class VirtualFile {
public:
    VirtualFile(
        const ParsedPath& path,
        SchemaManager& schema,
        CacheManager& cache
    );

    // Read entire file content
    std::string read();

    // Read with offset and size
    std::string read(off_t offset, size_t size);

    // Write data (buffered)
    int write(const char* data, size_t size, off_t offset);

    // Flush buffered writes to database
    int flush();

    // Get file size
    size_t size();

    // Check permissions
    bool isReadable() const;
    bool isWritable() const;

    // Get last error
    const std::string& lastError() const;
};
```

---

## ErrorHandler Interface

```cpp
class ErrorHandler {
public:
    // Convert MySQL error to errno
    static int mysqlToErrno(int mysqlError);

    // Convert PostgreSQL SQLSTATE to errno
    static int postgresqlToErrno(const char* sqlstate);

    // Convert SQLite error to errno
    static int sqliteToErrno(int sqliteError);

    // Get error message
    static std::string getMessage(int errnum);
};
```

---

## Config Structure

```cpp
struct Config {
    struct Connection {
        DatabaseType type = DatabaseType::MySQL;
        std::string host = "localhost";
        int port = 0;  // 0 means use default
        std::string user;
        std::string password;
        std::string database;
        std::string socket;        // Unix socket path
        std::string sslMode;       // For PostgreSQL
        int connectTimeout = 10;
        int readTimeout = 30;
    } connection;

    struct Performance {
        size_t connection_pool_size = 5;
        std::chrono::seconds cache_ttl{300};
        size_t max_cache_size = 100;
        bool enable_cache = true;
    } performance;

    struct Data {
        FileFormat default_format = FileFormat::CSV;
        size_t row_limit = 10000;
        bool include_blobs = false;
        bool pretty_json = false;
    } data;

    struct Security {
        std::vector<std::string> allowed_databases;
        bool allow_write = true;
    } security;

    struct Logging {
        std::string level = "info";
        std::string file;
    } logging;

    std::string mountpoint;
    bool foreground = false;

    // Load configuration
    static Config load(int argc, char* argv[]);
    static Config loadFromFile(const std::string& path);
};
```
