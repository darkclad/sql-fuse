# Architecture

This document describes the internal architecture and design of SQL-FUSE.

## Overview

SQL-FUSE is built with a modular architecture that separates concerns:

```
┌─────────────────────────────────────────────────────────────────┐
│                        FUSE Interface                            │
│                    (Linux Kernel / libfuse3)                     │
└─────────────────────────────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────┐
│                         SQLFuseFS                                │
│              (Main filesystem implementation)                    │
└─────────────────────────────────────────────────────────────────┘
                                │
        ┌───────────────────────┼───────────────────────┐
        ▼                       ▼                       ▼
┌───────────────┐      ┌───────────────┐      ┌───────────────┐
│  PathRouter   │      │ CacheManager  │      │ VirtualFile   │
│               │      │               │      │   Manager     │
└───────────────┘      └───────────────┘      └───────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────┐
│                       SchemaManager                              │
│            (Abstract interface for database schema)              │
└─────────────────────────────────────────────────────────────────┘
        │                       │                       │
        ▼                       ▼                       ▼
┌───────────────┐      ┌───────────────┐      ┌───────────────┐
│    MySQL      │      │  PostgreSQL   │      │    SQLite     │
│ SchemaManager │      │ SchemaManager │      │ SchemaManager │
└───────────────┘      └───────────────┘      └───────────────┘
        │                       │                       │
        ▼                       ▼                       ▼
┌───────────────┐      ┌───────────────┐      ┌───────────────┐
│    MySQL      │      │  PostgreSQL   │      │    SQLite     │
│ConnectionPool │      │ConnectionPool │      │ConnectionPool │
└───────────────┘      └───────────────┘      └───────────────┘
```

## Core Components

### SQLFuseFS

The main filesystem class that implements FUSE operations:

```cpp
class SQLFuseFS {
public:
    static SQLFuseFS& instance();  // Singleton

    int init(const Config& config);
    int run(int argc, char* argv[]);
    void shutdown();

    // FUSE operations
    int getattr(const char* path, struct stat* stbuf, fuse_file_info* fi);
    int readdir(const char* path, void* buf, fuse_fill_dir_t filler, ...);
    int open(const char* path, fuse_file_info* fi);
    int read(const char* path, char* buf, size_t size, off_t offset, ...);
    int write(const char* path, const char* buf, size_t size, off_t offset, ...);
    // ... more operations
};
```

Key responsibilities:
- Initialize database connections
- Route FUSE callbacks to appropriate handlers
- Manage component lifecycle

### PathRouter

Parses filesystem paths and determines the type of resource:

```cpp
struct ParsedPath {
    PathType type;           // ROOT, DATABASE, TABLES_DIR, TABLE_FILE, etc.
    std::string database;
    std::string object_name;
    std::string row_id;
    FileFormat format;       // CSV, JSON, SQL
};

class PathRouter {
public:
    ParsedPath parse(const std::string& path);
    std::string buildPath(const ParsedPath& parsed);
};
```

Path types include:
- `ROOT` - Mount point root
- `DATABASE` - Database directory
- `TABLES_DIR` - Tables listing
- `TABLE_FILE` - Table data file (.csv, .json, .sql)
- `TABLE_DIR` - Table detail directory
- `ROWS_DIR` - Rows listing
- `ROW_FILE` - Individual row
- `VIEWS_DIR`, `VIEW_FILE` - Views
- `FUNCTIONS_DIR`, `FUNCTION_FILE` - Functions
- `PROCEDURES_DIR`, `PROCEDURE_FILE` - Procedures
- `TRIGGERS_DIR`, `TRIGGER_FILE` - Triggers

### SchemaManager

Abstract interface for database schema operations:

```cpp
class SchemaManager {
public:
    virtual ~SchemaManager() = default;

    // Database operations
    virtual std::vector<std::string> getDatabases() = 0;
    virtual std::optional<DatabaseInfo> getDatabaseInfo(const std::string& db) = 0;

    // Table operations
    virtual std::vector<std::string> getTables(const std::string& db) = 0;
    virtual std::optional<TableInfo> getTableInfo(const std::string& db,
                                                   const std::string& table) = 0;
    virtual std::vector<ColumnInfo> getColumns(const std::string& db,
                                                const std::string& table) = 0;
    virtual std::string getTableData(const std::string& db, const std::string& table,
                                     FileFormat format) = 0;
    virtual std::string getCreateStatement(const std::string& db,
                                           const std::string& table) = 0;

    // Row operations
    virtual std::vector<std::string> getRowIds(const std::string& db,
                                                const std::string& table) = 0;
    virtual std::string getRow(const std::string& db, const std::string& table,
                               const std::string& rowId) = 0;

    // View operations
    virtual std::vector<ViewInfo> getViews(const std::string& db) = 0;
    virtual std::string getViewData(const std::string& db, const std::string& view,
                                    FileFormat format) = 0;

    // Routine operations
    virtual std::vector<RoutineInfo> getFunctions(const std::string& db) = 0;
    virtual std::vector<RoutineInfo> getProcedures(const std::string& db) = 0;
    virtual std::string getRoutineDefinition(const std::string& db,
                                             const std::string& name) = 0;

    // Trigger operations
    virtual std::vector<TriggerInfo> getTriggers(const std::string& db) = 0;
    virtual std::string getTriggerDefinition(const std::string& db,
                                             const std::string& name) = 0;
};
```

### CacheManager

Provides caching for frequently accessed data:

```cpp
class CacheManager {
public:
    CacheManager(size_t maxSize, std::chrono::seconds ttl);

    // Cache operations
    std::optional<std::string> get(const std::string& key);
    void put(const std::string& key, const std::string& value);
    void invalidate(const std::string& key);
    void invalidatePrefix(const std::string& prefix);
    void invalidateTable(const std::string& db, const std::string& table);
    void clear();

    // Statistics
    size_t size() const;
    size_t hits() const;
    size_t misses() const;
};
```

Cache key format: `{db}:{type}:{object}:{format}`

### ConnectionPool

Template for database connection pooling:

```cpp
template<typename Connection>
class ConnectionPool {
public:
    ConnectionPool(const ConnectionConfig& config, size_t poolSize);

    std::unique_ptr<Connection> acquire();
    void release(std::unique_ptr<Connection> conn);

    bool healthCheck();
    void drain();

    size_t available() const;
    size_t total() const;
};
```

Each database has its own connection pool implementation:
- `MySQLConnectionPool`
- `PostgreSQLConnectionPool`
- `SQLiteConnectionPool`

### VirtualFile

Handles file content generation and write operations:

```cpp
class VirtualFile {
public:
    VirtualFile(const ParsedPath& path, SchemaManager& schema);

    std::string read();
    int write(const std::string& data);
    size_t size();

    bool isReadable() const;
    bool isWritable() const;
};
```

### FormatConverter

Converts between data formats:

```cpp
class FormatConverter {
public:
    // Row data to output format
    static std::string toCSV(const std::vector<RowData>& rows,
                             const std::vector<std::string>& columns);
    static std::string toJSON(const std::vector<RowData>& rows);

    // Input format to row data
    static std::vector<RowData> parseCSV(const std::string& data,
                                          const CSVOptions& opts);
    static std::vector<RowData> parseJSON(const std::string& data);
};
```

Database-specific format converters handle SQL generation:
- `MySQLFormatConverter`
- `PostgreSQLFormatConverter`
- `SQLiteFormatConverter`

## Data Flow

### Read Operation

```
1. FUSE kernel calls read(path, buf, size, offset)
          │
          ▼
2. SQLFuseFS::read() receives the call
          │
          ▼
3. PathRouter::parse(path) determines resource type
          │
          ▼
4. CacheManager::get() checks for cached data
          │
          ├── Cache hit → Return cached data
          │
          └── Cache miss ↓
                         │
5. SchemaManager::getXxx() fetches from database
          │
          ▼
6. FormatConverter converts to requested format
          │
          ▼
7. CacheManager::put() stores result
          │
          ▼
8. Return data to FUSE
```

### Write Operation

```
1. FUSE kernel calls write(path, buf, size, offset)
          │
          ▼
2. SQLFuseFS::write() receives the call
          │
          ▼
3. PathRouter::parse(path) determines resource type
          │
          ▼
4. VirtualFile::write() buffers the data
          │
          ▼
5. On flush/release:
          │
          ▼
6. FormatConverter::parseXxx() parses input
          │
          ▼
7. SchemaManager or direct SQL execution
          │
          ▼
8. CacheManager::invalidate() clears cache
          │
          ▼
9. Return success/error to FUSE
```

## Threading Model

SQL-FUSE uses the FUSE multi-threaded model:

- FUSE spawns threads for concurrent operations
- Connection pools are thread-safe
- Cache operations are protected by mutexes
- Each request gets its own connection from the pool

```cpp
// Thread-safe connection acquisition
auto conn = pool->acquire();  // Blocks if pool exhausted
// ... use connection ...
pool->release(std::move(conn));  // Return to pool
```

## Error Handling

Errors are mapped to POSIX errno values:

| Database Error | POSIX Error |
|---------------|-------------|
| Connection failed | EIO |
| Not found | ENOENT |
| Permission denied | EACCES |
| Invalid query | EINVAL |
| Timeout | ETIMEDOUT |

```cpp
class ErrorHandler {
public:
    static int mysqlToErrno(int mysqlError);
    static int postgresqlToErrno(const char* sqlstate);
    static int sqliteToErrno(int sqliteError);
};
```

## Configuration Loading

Configuration is loaded in priority order:

1. Command-line arguments (highest priority)
2. Environment variables
3. Configuration file
4. Default values (lowest priority)

```cpp
class Config {
public:
    static Config load(int argc, char* argv[]);

    struct Connection {
        DatabaseType type;
        std::string host;
        int port;
        std::string user;
        std::string password;
        std::string database;
    } connection;

    struct Performance {
        size_t connection_pool_size;
        std::chrono::seconds cache_ttl;
        size_t max_cache_size;
    } performance;

    struct Data {
        FileFormat default_format;
        size_t row_limit;
        bool include_blobs;
    } data;
};
```

## Build System

CMake handles conditional compilation:

```cmake
option(WITH_MYSQL "Enable MySQL support" ON)
option(WITH_POSTGRESQL "Enable PostgreSQL support" OFF)
option(WITH_SQLITE "Enable SQLite support" ON)

if(WITH_MYSQL)
    add_definitions(-DWITH_MYSQL)
    # ... MySQL sources and libraries
endif()
```

Source files are organized by database:
```
src/
├── mysql/
│   ├── MySQLConnection.cpp
│   ├── MySQLConnectionPool.cpp
│   ├── MySQLSchemaManager.cpp
│   └── MySQLFormatConverter.cpp
├── postgresql/
│   └── ...
├── sqlite/
│   └── ...
└── (common files)
```

## Testing

### Unit Tests

Located in `tests/`:
- `test_path_router.cpp` - Path parsing tests
- `test_format_converter.cpp` - Format conversion tests
- `test_cache_manager.cpp` - Cache operation tests
- `test_config.cpp` - Configuration tests

### Integration Tests

Located in `tests/{database}/`:
- `setup_test_db.sql` - Create test database
- `test_{database}.sh` - Run integration tests
- `teardown_test_db.sh` - Cleanup

Run tests:
```bash
cd build
ctest --output-on-failure
```

## Extending SQL-FUSE

### Adding a New Database

1. Create header in `include/{database}/`:
   - `{Database}Connection.hpp`
   - `{Database}ConnectionPool.hpp`
   - `{Database}SchemaManager.hpp`
   - `{Database}FormatConverter.hpp`

2. Implement in `src/{database}/`:
   - Inherit from base interfaces
   - Implement all virtual methods

3. Update `CMakeLists.txt`:
   - Add `WITH_{DATABASE}` option
   - Add source files conditionally
   - Link required libraries

4. Update `SQLFuseFS.cpp`:
   - Add case for new database type
   - Include new headers conditionally

5. Add tests in `tests/{database}/`

### Adding a New File Format

1. Add format enum in `PathRouter.hpp`:
   ```cpp
   enum class FileFormat { CSV, JSON, SQL, XML /* new */ };
   ```

2. Update `FormatConverter`:
   ```cpp
   static std::string toXML(const std::vector<RowData>& rows);
   static std::vector<RowData> parseXML(const std::string& data);
   ```

3. Update path parsing to recognize new extension

4. Add tests for new format
