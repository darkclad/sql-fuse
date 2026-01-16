# SQL FUSE Filesystem Driver - Design Document

## 1. Overview

**Project Name:** sql-fuse
**Language:** C++17
**Purpose:** Expose SQL database structures as a virtual filesystem using FUSE (Filesystem in Userspace)

### 1.1 Supported Databases
- **MySQL/MariaDB** - Full support
- **SQLite** - Full support
- **PostgreSQL** - Full support
- **Oracle** - Planned

### 1.2 Goals
- Mount SQL databases as a filesystem
- Navigate databases, tables, views, and other objects as directories/files
- Read table data as files (CSV, JSON formats)
- Support basic write operations (INSERT, UPDATE, DELETE via file operations)
- Provide metadata access (schema info, statistics)
- Unified interface across different database backends

### 1.3 Non-Goals (Initial Version)
- Stored procedure execution
- Real-time streaming of changes
- Multi-user concurrent write safety (beyond the database's own handling)

---

## 2. Filesystem Hierarchy

```
/mountpoint/
├── <database_1>/                    # For MySQL: database name; For SQLite: "main"
│   ├── tables/
│   │   ├── <table_1>.csv            # Table data as CSV
│   │   ├── <table_1>.json           # Table data as JSON
│   │   ├── <table_1>.sql            # CREATE TABLE statement
│   │   └── <table_1>/               # Directory view of table
│   │       ├── .schema              # Column definitions
│   │       ├── .indexes             # Index information
│   │       ├── .stats               # Table statistics
│   │       └── rows/                # Individual rows by PK
│   │           ├── 1.json
│   │           ├── 2.json
│   │           └── ...
│   ├── views/
│   │   ├── <view_1>.csv
│   │   ├── <view_1>.json
│   │   └── <view_1>.sql             # CREATE VIEW statement
│   ├── procedures/                  # MySQL/PostgreSQL only
│   │   └── <proc_1>.sql             # Procedure definition
│   ├── functions/                   # MySQL/PostgreSQL only
│   │   └── <func_1>.sql             # Function definition
│   ├── triggers/
│   │   └── <trigger_1>.sql          # Trigger definition
│   └── .info                        # Database metadata
├── <database_2>/
│   └── ...
├── .server_info                     # Server/database information
├── .users/                          # User accounts (MySQL only, read-only)
│   └── <user>@<host>.info
└── .variables/                      # Server variables (MySQL only)
    ├── global/
    └── session/
```

---

## 3. Architecture

### 3.1 Component Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                        User Applications                         │
│                    (ls, cat, vim, cp, etc.)                      │
└─────────────────────────────────────────────────────────────────┘
                                │
                                │ POSIX System Calls
                                ▼
┌─────────────────────────────────────────────────────────────────┐
│                         Linux Kernel VFS                         │
└─────────────────────────────────────────────────────────────────┘
                                │
                                │ FUSE Protocol
                                ▼
┌─────────────────────────────────────────────────────────────────┐
│                      libfuse3 / FUSE Library                     │
└─────────────────────────────────────────────────────────────────┘
                                │
                                │ C++ Callbacks
                                ▼
┌─────────────────────────────────────────────────────────────────┐
│                        sql-fuse Driver                           │
│  ┌───────────────┐  ┌───────────────┐  ┌───────────────────┐   │
│  │  PathRouter   │  │  SQLFuseFS    │  │  CacheManager     │   │
│  │               │  │  (FUSE ops)   │  │  (LRU + TTL)      │   │
│  └───────────────┘  └───────────────┘  └───────────────────┘   │
│  ┌───────────────┐  ┌───────────────┐  ┌───────────────────┐   │
│  │ Format        │  │ Schema        │  │  Connection       │   │
│  │ Converter     │  │ Manager       │  │  Pool             │   │
│  └───────────────┘  └───────────────┘  └───────────────────┘   │
│  ┌───────────────┐  ┌────────────────────────────────────────┐ │
│  │ VirtualFile   │  │    VirtualFileHandleManager            │ │
│  │ HandleManager │  │    (manages open file handles)         │ │
│  └───────────────┘  └────────────────────────────────────────┘ │
│                                                                  │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │              Database Backend Abstraction                  │  │
│  │  ┌─────────────────────────┐  ┌─────────────────────────┐ │  │
│  │  │   MySQL Backend   │ │  PostgreSQL Backend  │ │   SQLite Backend   │ │  │
│  │  │  ┌──────────────┐  │ │  ┌──────────────┐    │ │  ┌──────────────┐  │ │  │
│  │  │  │MySQLConn     │  │ │  │PostgreSQLConn│    │ │  │SQLiteConn    │  │ │  │
│  │  │  │Pool          │  │ │  │Pool          │    │ │  │Pool          │  │ │  │
│  │  │  ├──────────────┤  │ │  ├──────────────┤    │ │  ├──────────────┤  │ │  │
│  │  │  │MySQLSchema   │  │ │  │PostgreSQL    │    │ │  │SQLiteSchema  │  │ │  │
│  │  │  │Manager       │  │ │  │SchemaManager │    │ │  │Manager       │  │ │  │
│  │  │  ├──────────────┤  │ │  ├──────────────┤    │ │  ├──────────────┤  │ │  │
│  │  │  │MySQLVirtFile │  │ │  │PostgreSQLVF  │    │ │  │SQLiteVirtFile│  │ │  │
│  │  │  ├──────────────┤  │ │  ├──────────────┤    │ │  ├──────────────┤  │ │  │
│  │  │  │MySQLFormat   │  │ │  │PostgreSQL    │    │ │  │SQLiteFormat  │  │ │  │
│  │  │  │Converter     │  │ │  │FmtConverter  │    │ │  │Converter     │  │ │  │
│  │  │  └──────────────┘  │ │  └──────────────┘    │ │  └──────────────┘  │ │  │
│  │  └────────────────────┘ └──────────────────────┘ └────────────────────┘ │  │
│  └─────────────────────────────────────────────────────────────────────────┘  │
└───────────────────────────────────────────────────────────────────────────────┘
                                │
            ┌───────────────────┼───────────────────┐
            │                   │                   │
            ▼                   ▼                   ▼
┌─────────────────────┐ ┌─────────────────────┐ ┌─────────────────────┐
│   MySQL/MariaDB     │ │    PostgreSQL       │ │    SQLite File      │
│      Server         │ │       Server        │ │                     │
└─────────────────────┘ └─────────────────────┘ └─────────────────────┘
```

### 3.2 Abstract Base Classes

The project uses abstract base classes for database-independent operations:

```cpp
// Database type enumeration
enum class DatabaseType {
    MySQL,
    SQLite,
    PostgreSQL,
    Oracle       // Future
};

// Abstract base class for connection pool management
class ConnectionPool {
public:
    virtual ~ConnectionPool() = default;

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
};

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

    // Routine operations (MySQL/PostgreSQL)
    virtual std::vector<std::string> getProcedures(const std::string& database) = 0;
    virtual std::vector<std::string> getFunctions(const std::string& database) = 0;

    // Trigger operations
    virtual std::vector<std::string> getTriggers(const std::string& database) = 0;

    // DDL statements
    virtual std::string getCreateStatement(const std::string& database,
                                           const std::string& object,
                                           const std::string& type) = 0;

    // Row operations
    virtual std::vector<std::string> getRowIds(const std::string& database,
                                                const std::string& table,
                                                size_t limit, size_t offset) = 0;
    virtual uint64_t getRowCount(const std::string& database, const std::string& table) = 0;

    // Cache invalidation
    virtual void invalidateTable(const std::string& database, const std::string& table) = 0;
    virtual void invalidateDatabase(const std::string& database) = 0;
    virtual void invalidateAll() = 0;

    // Access connection pool
    virtual ConnectionPool& connectionPool() = 0;
};

// Abstract base class for virtual file operations
class VirtualFile {
public:
    virtual ~VirtualFile() = default;

    virtual std::string getContent() = 0;
    virtual size_t getSize() = 0;
    virtual int write(const char* data, size_t size, off_t offset) = 0;
    virtual int truncate(off_t size) = 0;
    virtual int flush() = 0;
    virtual bool isModified() const = 0;
};

// Base class with database-independent format conversion
class FormatConverter {
public:
    virtual ~FormatConverter() = default;

    // Generic CSV conversion
    static std::string toCSV(const std::vector<std::string>& columns,
                            const std::vector<std::vector<SqlValue>>& rows,
                            const CSVOptions& options = CSVOptions{});

    // Generic JSON conversion
    static std::string toJSON(const std::vector<std::string>& columns,
                             const std::vector<std::vector<SqlValue>>& rows,
                             const JSONOptions& options = JSONOptions{});

    // Parse CSV/JSON to rows
    static std::vector<RowData> parseCSV(const std::string& data, ...);
    static std::vector<RowData> parseJSON(const std::string& data);
};
```

### 3.3 Core Classes

```cpp
// Main FUSE filesystem class
class SQLFuseFS {
public:
    SQLFuseFS(const Config& config);

    bool initialize();
    int run(int argc, char* argv[]);
    void shutdown();

    // FUSE operation handlers
    int getattr(const char* path, struct stat* stbuf, fuse_file_info* fi);
    int readdir(const char* path, void* buf, fuse_fill_dir_t filler, ...);
    int open(const char* path, fuse_file_info* fi);
    int read(const char* path, char* buf, size_t size, off_t offset, fuse_file_info* fi);
    int write(const char* path, const char* buf, size_t size, off_t offset, fuse_file_info* fi);
    int release(const char* path, fuse_file_info* fi);
    // ... other FUSE operations

private:
    Config m_config;
    DatabaseType m_dbType;
    PathRouter m_router;
    std::unique_ptr<CacheManager> m_cache;

    // Connection pool as variant (supports compile-time database selection)
    std::variant<
        std::monostate,
        std::unique_ptr<MySQLConnectionPool>,
        std::unique_ptr<SQLiteConnectionPool>,
        std::unique_ptr<PostgreSQLConnectionPool>
    > m_pool;

    std::unique_ptr<SchemaManager> m_schema;
    std::unique_ptr<VirtualFileHandleManager> m_fileHandles;
};

// Path parsing and routing
class PathRouter {
public:
    enum class NodeType {
        Root, Database, TablesDir, ViewsDir, ProceduresDir, FunctionsDir,
        TriggersDir, TableFile, TableDir, TableSchema, TableIndexes,
        TableStats, RowsDir, RowFile, ViewFile, ProcedureFile, FunctionFile,
        TriggerFile, ServerInfo, UsersDir, UserFile, VariablesDir,
        VariableFile, NotFound
    };

    struct ParsedPath {
        NodeType type;
        std::string database;
        std::string object_name;
        std::string format;     // csv, json, sql
        std::string row_id;
        std::string extra;
    };

    ParsedPath parse(const std::string& path) const;
};

// LRU + TTL cache
class CacheManager {
public:
    CacheManager(const CacheConfig& config);

    std::optional<std::string> get(const std::string& key);
    void put(const std::string& key, std::string data,
             std::optional<std::chrono::seconds> ttl = std::nullopt);
    void invalidate(const std::string& pattern);
    void clear();
};

// Manages open file handles
class VirtualFileHandleManager {
public:
    VirtualFileHandleManager(SchemaManager& schema, CacheManager& cache,
                             const DataConfig& config);

    uint64_t create(const ParsedPath& path);
    VirtualFile* get(uint64_t handle);
    void release(uint64_t handle);
    size_t openCount() const;
};
```

---

## 4. Database-Specific Implementations

### 4.1 MySQL Backend

```cpp
class MySQLConnectionPool : public ConnectionPool {
public:
    MySQLConnectionPool(const ConnectionConfig& config, size_t poolSize = 10);

    // Acquires a connection from the pool (RAII wrapper)
    class ScopedConnection {
        MYSQL* get() const;
        // Auto-returns to pool on destruction
    };

    ScopedConnection acquire(std::chrono::milliseconds timeout = 5000ms);
};

class MySQLSchemaManager : public SchemaManager {
    // Uses INFORMATION_SCHEMA and SHOW commands
    // Caches results with configurable TTL
};

class MySQLVirtualFile : public VirtualFile {
    // Handles MySQL-specific query generation and result formatting
};

class MySQLFormatConverter : public FormatConverter {
    // Converts MYSQL_RES to CSV/JSON
    static std::string toCSV(MYSQL_RES* result, const CSVOptions& options);
    static std::string toJSON(MYSQL_RES* result, const JSONOptions& options);
};

class MySQLResultSet {
    // Wrapper around MYSQL_RES with iterator support
};
```

### 4.2 SQLite Backend

```cpp
class SQLiteConnectionPool : public ConnectionPool {
    // SQLite uses a single connection with mutex protection
    // (SQLite allows multiple readers, single writer)
};

class SQLiteSchemaManager : public SchemaManager {
    // Uses sqlite_master and PRAGMA commands
    // Note: SQLite has a single "main" database
};

class SQLiteVirtualFile : public VirtualFile {
    // Handles SQLite-specific query generation
};

class SQLiteFormatConverter : public FormatConverter {
    // Converts sqlite3_stmt results to CSV/JSON
};

class SQLiteResultSet {
    // Wrapper around sqlite3_stmt with iterator support
};
```

### 4.3 PostgreSQL Backend

```cpp
class PostgreSQLConnectionPool : public ConnectionPool {
public:
    PostgreSQLConnectionPool(const ConnectionConfig& config, size_t poolSize = 5);

    // Acquires a connection from the pool
    std::unique_ptr<PostgreSQLConnection> acquire();
    void release(std::unique_ptr<PostgreSQLConnection> conn);
};

class PostgreSQLSchemaManager : public SchemaManager {
    // Uses information_schema and pg_catalog
    // Caches results with configurable TTL
};

class PostgreSQLVirtualFile : public VirtualFile {
    // Handles PostgreSQL-specific query generation and result formatting
};

class PostgreSQLFormatConverter : public FormatConverter {
    // Converts PGresult to CSV/JSON
    static std::string toCSV(PGresult* result, const CSVOptions& options);
    static std::string toJSON(PGresult* result, const JSONOptions& options);
};

class PostgreSQLResultSet {
    // Wrapper around PGresult with iterator support
};
```

### 4.4 Database Feature Matrix

| Feature | MySQL | SQLite | PostgreSQL | Oracle |
|---------|-------|--------|------------|--------|
| Multiple databases | Yes | No (main only) | Yes | Planned |
| Stored procedures | Yes | No | Yes | Planned |
| User-defined functions | Yes | No | Yes | Planned |
| Triggers | Yes | Yes | Yes | Planned |
| Views | Yes | Yes | Yes | Planned |
| Foreign keys | Yes | Yes | Yes | Planned |
| Connection pooling | Yes | Limited | Yes | Planned |
| SSL/TLS | Yes | N/A | Yes | Planned |

---

## 5. Data Structures

### 5.1 Schema Information

```cpp
struct ColumnInfo {
    std::string name;
    std::string type;
    std::string fullType;
    bool nullable = true;
    std::string defaultValue;
    std::string extra;          // AUTO_INCREMENT, etc.
    std::string key;            // PRI, UNI, MUL
    std::string collation;
    std::string comment;
    int ordinalPosition = 0;
};

struct IndexInfo {
    std::string name;
    bool unique = false;
    bool primary = false;
    std::vector<std::string> columns;
    std::string type;           // BTREE, HASH, FULLTEXT
    uint64_t cardinality = 0;
};

struct TableInfo {
    std::string name;
    std::string database;
    std::string engine;
    std::string collation;
    uint64_t rowsEstimate = 0;
    uint64_t dataLength = 0;
    uint64_t indexLength = 0;
    std::vector<ColumnInfo> columns;
    std::vector<IndexInfo> indexes;
    std::string primaryKeyColumn;
};

struct ViewInfo {
    std::string name;
    std::string database;
    std::string definer;
    bool isUpdatable = false;
};

struct TriggerInfo {
    std::string name;
    std::string database;
    std::string table;
    std::string event;          // INSERT, UPDATE, DELETE
    std::string timing;         // BEFORE, AFTER
    std::string statement;
};

struct ServerInfo {
    std::string version;
    std::string versionComment;
    uint64_t uptime = 0;
    std::string hostname;
    uint16_t port = 0;
};
```

### 5.2 Format Conversion Types

```cpp
using SqlValue = std::optional<std::string>;
using RowData = std::map<std::string, SqlValue>;

struct CSVOptions {
    char delimiter = ',';
    char quote = '"';
    char escape = '\\';
    std::string lineEnding = "\n";
    bool includeHeader = true;
    bool quoteAll = false;
};

struct JSONOptions {
    bool pretty = true;
    int indent = 2;
    bool includeNull = true;
    bool arrayFormat = true;    // array of objects vs object with rows
};
```

---

## 6. Configuration

### 6.1 Command Line Options

```bash
sql-fuse [options] <mountpoint>

Database Options:
  -t, --type <type>       Database type: mysql, sqlite, postgresql, oracle
                          (default: mysql)

Connection Options (MySQL/PostgreSQL/Oracle):
  -H, --host <host>       Database server host (default: localhost)
  -P, --port <port>       Database server port (default: 3306 for MySQL)
  -u, --user <user>       Database username
  -p, --password <pass>   Database password (or use MYSQL_PWD env)
  -S, --socket <path>     Unix socket path
  -D, --database <name>   Default database

Connection Options (SQLite):
  -H, --host <path>       Path to SQLite database file

SSL Options:
  --ssl                   Enable SSL connection
  --ssl-ca <file>         SSL CA certificate file
  --ssl-cert <file>       SSL client certificate file
  --ssl-key <file>        SSL client key file

Cache Options:
  --cache-size <MB>       Maximum cache size (default: 100)
  --cache-ttl <seconds>   Default cache TTL (default: 30)
  --no-cache              Disable caching entirely

Data Options:
  --max-rows <N>          Max rows in table files (default: 10000)
  --read-only             Mount as read-only
  --databases <list>      Comma-separated list of databases to expose

FUSE Options:
  -f, --foreground        Run in foreground
  -d, --debug             Enable debug output
  --allow-other           Allow other users to access
  --allow-root            Allow root to access
  --max-threads <N>       Maximum FUSE worker threads (default: 10)

Other:
  -c, --config <file>     Path to configuration file
```

### 6.2 Configuration File

```ini
# /etc/sql-fuse.conf or ~/.sql-fuse.conf

[connection]
type = mysql                    # mysql, sqlite, postgresql, oracle
host = localhost                # Server host or SQLite file path
port = 3306
user = readonly_user
# password = (use MYSQL_PWD environment variable)
socket = /var/run/mysqld/mysqld.sock
database = mydb

[ssl]
use_ssl = false
ssl_ca = /path/to/ca.pem
ssl_cert = /path/to/client-cert.pem
ssl_key = /path/to/client-key.pem

[cache]
max_size_mb = 100
data_ttl = 30
schema_ttl = 300
metadata_ttl = 60
enabled = true

[data]
max_rows = 10000
rows_per_page = 1000
default_format = csv
pretty_json = true
include_csv_header = true

[security]
read_only = false
allowed_databases = db1,db2,db3
# denied_databases = mysql,information_schema
expose_system_databases = false

[performance]
connection_pool_size = 10
max_concurrent_queries = 20
max_fuse_threads = 10
enable_query_cache = true
```

### 6.3 Configuration Structures

```cpp
struct ConnectionConfig {
    std::string host = "localhost";
    uint16_t port = 3306;
    std::string user;
    std::string password;
    std::string socket;
    std::string default_database;
    bool use_ssl = false;
    std::string ssl_ca, ssl_cert, ssl_key;
    std::chrono::milliseconds connect_timeout{5000};
    std::chrono::milliseconds read_timeout{30000};
    std::chrono::milliseconds write_timeout{30000};
};

struct CacheConfig {
    size_t max_size_bytes = 100 * 1024 * 1024;
    std::chrono::seconds data_ttl{30};
    std::chrono::seconds schema_ttl{300};
    std::chrono::seconds metadata_ttl{60};
    bool enabled = true;
};

struct DataConfig {
    size_t max_rows_per_file = 10000;
    size_t rows_per_page = 1000;
    bool pretty_json = true;
    bool include_csv_header = true;
    std::string default_format = "csv";
};

struct SecurityConfig {
    bool read_only = false;
    std::vector<std::string> allowed_databases;
    std::vector<std::string> denied_databases;
    bool expose_system_databases = false;
};

struct PerformanceConfig {
    size_t connection_pool_size = 10;
    size_t max_concurrent_queries = 20;
    size_t max_fuse_threads = 10;
    bool enable_query_cache = true;
};

struct Config {
    ConnectionConfig connection;
    CacheConfig cache;
    DataConfig data;
    SecurityConfig security;
    PerformanceConfig performance;
    std::string mountpoint;
    std::string database_type = "mysql";
    bool foreground = false;
    bool debug = false;
    bool allow_other = false;
    bool allow_root = false;
};
```

---

## 7. Error Handling

### 7.1 Error Code Mapping

| Database Error | POSIX Error | Description |
|----------------|-------------|-------------|
| Connection failed | `ENOENT` | Server/file not found |
| Access denied | `EACCES` | Permission denied |
| Unknown database | `ENOENT` | Database not found |
| Unknown table | `ENOENT` | Table not found |
| Query timeout | `ETIMEDOUT` | Operation timed out |
| Duplicate entry | `EEXIST` | Row already exists |
| Foreign key violation | `EINVAL` | Invalid operation |
| Syntax error | `EINVAL` | Invalid file content |
| Connection lost | `EIO` | I/O error |
| Database locked (SQLite) | `EAGAIN` | Try again |

### 7.2 Error Handler

```cpp
class ErrorHandler {
public:
    // Convert database-specific error to errno
    static int toErrno(int db_error, DatabaseType type = DatabaseType::MySQL);

    // Check if error is retryable
    static bool isRetryable(int db_error, DatabaseType type = DatabaseType::MySQL);

    // Execute with retry logic
    template<typename Func>
    static int withRetry(Func&& func, int maxRetries = 3,
                         std::chrono::milliseconds delay = 100ms);

    // Log error with context
    static void logError(const std::string& context, int db_error,
                         DatabaseType type = DatabaseType::MySQL);
};
```

---

## 8. Build System

### 8.1 Dependencies

| Dependency | Required For | Notes |
|------------|--------------|-------|
| libfuse3 | All | FUSE library (version 3.x) |
| libmysqlclient | MySQL | MySQL C connector |
| libsqlite3 | SQLite | SQLite3 library |
| libpq | PostgreSQL | PostgreSQL client library |
| nlohmann/json | All | JSON parsing (header-only, fetched) |
| spdlog | All | Logging (fetched) |
| CLI11 | All | Command-line parsing (header-only, fetched) |
| Google Test | Tests | Unit testing (fetched) |

### 8.2 CMake Options

```cmake
option(WITH_MYSQL "Build with MySQL support" ON)
option(WITH_SQLITE "Build with SQLite support" ON)
option(WITH_POSTGRESQL "Build with PostgreSQL support" OFF)
option(WITH_ORACLE "Build with Oracle support" OFF)
option(BUILD_TESTS "Build unit tests" ON)
```

---

## 9. Project Structure

```
sql-fuse/
├── CMakeLists.txt
├── README.md
├── DESIGN.md                        # This document
├── LICENSE
├── sql-fuse.conf.example
├── .gitignore
├── include/
│   ├── CacheManager.hpp
│   ├── Config.hpp
│   ├── ConnectionPool.hpp           # Abstract base class
│   ├── ErrorHandler.hpp
│   ├── FormatConverter.hpp          # Base class with static methods
│   ├── PathRouter.hpp
│   ├── SQLFuseFS.hpp
│   ├── SchemaManager.hpp            # Abstract base class
│   ├── VirtualFile.hpp              # Abstract base class
│   ├── VirtualFileHandleManager.hpp
│   ├── mysql/
│   │   ├── MySQLConnection.hpp
│   │   ├── MySQLConnectionPool.hpp
│   │   ├── MySQLFormatConverter.hpp
│   │   ├── MySQLResultSet.hpp
│   │   ├── MySQLSchemaManager.hpp
│   │   └── MySQLVirtualFile.hpp
│   ├── postgresql/
│   │   ├── PostgreSQLConnection.hpp
│   │   ├── PostgreSQLConnectionPool.hpp
│   │   ├── PostgreSQLFormatConverter.hpp
│   │   ├── PostgreSQLResultSet.hpp
│   │   ├── PostgreSQLSchemaManager.hpp
│   │   └── PostgreSQLVirtualFile.hpp
│   └── sqlite/
│       ├── SQLiteConnection.hpp
│       ├── SQLiteConnectionPool.hpp
│       ├── SQLiteFormatConverter.hpp
│       ├── SQLiteResultSet.hpp
│       ├── SQLiteSchemaManager.hpp
│       └── SQLiteVirtualFile.hpp
├── src/
│   ├── main.cpp
│   ├── CacheManager.cpp
│   ├── Config.cpp
│   ├── ErrorHandler.cpp
│   ├── FormatConverter.cpp
│   ├── PathRouter.cpp
│   ├── SchemaManager.cpp
│   ├── SQLFuseFS.cpp
│   ├── VirtualFile.cpp
│   ├── VirtualFileHandleManager.cpp
│   ├── mysql/
│   │   ├── MySQLConnection.cpp
│   │   ├── MySQLConnectionPool.cpp
│   │   ├── MySQLFormatConverter.cpp
│   │   ├── MySQLResultSet.cpp
│   │   ├── MySQLSchemaManager.cpp
│   │   └── MySQLVirtualFile.cpp
│   ├── postgresql/
│   │   ├── PostgreSQLConnection.cpp
│   │   ├── PostgreSQLConnectionPool.cpp
│   │   ├── PostgreSQLFormatConverter.cpp
│   │   ├── PostgreSQLResultSet.cpp
│   │   ├── PostgreSQLSchemaManager.cpp
│   │   └── PostgreSQLVirtualFile.cpp
│   └── sqlite/
│       ├── SQLiteConnection.cpp
│       ├── SQLiteConnectionPool.cpp
│       ├── SQLiteFormatConverter.cpp
│       ├── SQLiteResultSet.cpp
│       ├── SQLiteSchemaManager.cpp
│       └── SQLiteVirtualFile.cpp
├── docs/
│   ├── index.md
│   ├── getting-started.md
│   ├── configuration.md
│   ├── filesystem-structure.md
│   ├── supported-databases.md
│   ├── architecture.md
│   ├── api-reference.md
│   ├── contributing.md
│   └── troubleshooting.md
└── tests/
    ├── CMakeLists.txt
    ├── test_main.cpp
    ├── test_cache_manager.cpp
    ├── test_config.cpp
    ├── test_error_handler.cpp
    ├── test_format_converter.cpp
    ├── test_path_router.cpp
    ├── mysql/
    │   ├── setup_test_db.sh
    │   ├── teardown_test_db.sh
    │   └── test_mysql.sh            # Integration tests
    ├── postgresql/
    │   ├── setup_test_db.sql
    │   ├── teardown_test_db.sql
    │   └── test_postgresql.sh       # Integration tests
    └── sqlite/
        ├── setup_test_db.sh
        ├── setup_test_db.sql
        ├── teardown_test_db.sh
        └── test_sqlite.sh           # Integration tests
```

---

## 10. Implementation Status

### Phase 1: Core Infrastructure - Complete
- [x] Project setup with CMake
- [x] Abstract base classes for database backends
- [x] Path router implementation
- [x] Basic FUSE operations (getattr, readdir, open, read, release)
- [x] Mount/unmount functionality
- [x] Configuration file and CLI parsing

### Phase 2: MySQL Backend - Complete
- [x] MySQL connection pool with RAII
- [x] MySQL schema manager with caching
- [x] Database and table listing
- [x] Table data as CSV/JSON
- [x] CREATE statement files (.sql)
- [x] Integration tests

### Phase 3: SQLite Backend - Complete
- [x] SQLite connection with mutex protection
- [x] SQLite schema manager
- [x] Single-file database support ("main" database)
- [x] Integration tests

### Phase 4: Caching - Complete
- [x] LRU cache implementation
- [x] TTL-based expiration
- [x] Per-category TTL (schema, metadata, data)
- [x] Pattern-based invalidation

### Phase 5: Write Operations - Complete (SQLite, MySQL, PostgreSQL)
- [x] Single row UPDATE via file write
- [x] Row DELETE via file unlink
- [x] Row INSERT via file create
- [x] Bulk CSV/JSON import
- [x] Integration tests (SQLite: 40 tests, MySQL: 33 tests, PostgreSQL: 28 tests)

### Phase 6: PostgreSQL Backend - Complete
- [x] PostgreSQL connection pool with libpq
- [x] PostgreSQL schema manager with information_schema
- [x] Database and table listing
- [x] Table data as CSV/JSON
- [x] CREATE statement files (.sql)
- [x] Views, functions, procedures, triggers support
- [x] Integration tests (23 tests passing)

### Phase 8: Additional Backends - Future
- [ ] Oracle backend

### Phase 9: Polish - Ongoing
- [x] Basic error handling
- [ ] Comprehensive error recovery
- [ ] Performance optimization
- [ ] Full documentation

---

## 11. Example Usage

### MySQL

```bash
# Mount MySQL database
sql-fuse -t mysql -H localhost -u myuser -p mypassword /mnt/mysql

# List databases
ls /mnt/mysql
# Output: mydb  testdb  production

# List tables
ls /mnt/mysql/mydb/tables
# Output: users.csv  users.json  users.sql  users/  orders.csv  ...

# Read table as CSV
cat /mnt/mysql/mydb/tables/users.csv

# Read individual row
cat /mnt/mysql/mydb/tables/users/rows/1.json

# View table schema
cat /mnt/mysql/mydb/tables/users/.schema

# Unmount
fusermount -u /mnt/mysql
```

### PostgreSQL

```bash
# Mount PostgreSQL database
sql-fuse -t postgresql -H localhost -u myuser -D mydb /mnt/postgres

# Or use environment variable for password
PGPASSWORD=secret sql-fuse -t postgresql -H localhost -u myuser /mnt/postgres

# List databases
ls /mnt/postgres
# Output: mydb  testdb  template1

# List tables
ls /mnt/postgres/mydb/tables
# Output: users.csv  users.json  users.sql  users/  orders.csv  ...

# Read table as CSV
cat /mnt/postgres/mydb/tables/users.csv

# View stored procedures
ls /mnt/postgres/mydb/procedures
# Output: process_order.sql  validate_user.sql

# View functions
ls /mnt/postgres/mydb/functions
# Output: calculate_total.sql  get_user_name.sql

# Unmount
fusermount -u /mnt/postgres
```

### SQLite

```bash
# Mount SQLite database
sql-fuse -t sqlite -H /path/to/database.db /mnt/sqlite

# SQLite has a single "main" database
ls /mnt/sqlite
# Output: main

# List tables
ls /mnt/sqlite/main/tables
# Output: users.csv  users.json  users.sql  products.csv  ...

# Read table as JSON
cat /mnt/sqlite/main/tables/users.json

# Unmount
fusermount -u /mnt/sqlite
```

---

## 12. Future Enhancements

- **Oracle backend**: Additional database support
- **Query interface**: Special file to execute arbitrary SQL
- **Watch mode**: inotify-like notifications for data changes
- **Compression**: On-the-fly compression for large results
- **Encryption**: Client-side encryption for cached data
- **Multiple formats**: Parquet, Avro, XML support
- **Replication awareness**: Read from replicas, write to primary (MySQL)
- **Attached databases**: Support SQLite ATTACH DATABASE
- **Virtual tables**: Support for MySQL/SQLite virtual tables
