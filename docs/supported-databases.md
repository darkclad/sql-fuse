# Supported Databases

SQL-FUSE supports multiple database backends. This document covers the specifics of each supported database.

## MySQL / MariaDB

### Connection

```bash
# Basic connection
sql-fuse -t mysql -H localhost -P 3306 -u myuser -D mydb /mnt/mysql

# With password via environment variable
MYSQL_PWD=secret sql-fuse -t mysql -H localhost -u myuser -D mydb /mnt/mysql

# Connect to MariaDB (same syntax)
sql-fuse -t mysql -H localhost -u myuser -D mydb /mnt/mariadb
```

### Supported Features

| Feature | Status | Notes |
|---------|--------|-------|
| Tables | Full | Read/Write supported |
| Views | Full | Read-only |
| Functions | Full | Definition only |
| Procedures | Full | Definition only |
| Triggers | Full | Definition only |
| Users | Partial | Requires SUPER privilege |
| Variables | Full | Global and session |

### MySQL-Specific Objects

- **Storage Engines**: Shown in table schema
- **Character Sets**: Displayed in database info
- **Collations**: Displayed in table schema
- **Auto Increment**: Properly handled in inserts
- **Foreign Keys**: Shown in schema

### Data Types

| MySQL Type | JSON Representation |
|------------|---------------------|
| INT, BIGINT | Number |
| FLOAT, DOUBLE, DECIMAL | Number (string for DECIMAL) |
| VARCHAR, TEXT | String |
| DATE | String (YYYY-MM-DD) |
| DATETIME, TIMESTAMP | String (ISO 8601) |
| BLOB, BINARY | Base64 encoded string |
| JSON | Nested JSON object |
| ENUM | String |
| SET | Comma-separated string |

### Connection Options

```ini
[connection]
type = mysql
host = localhost
port = 3306
user = myuser
database = mydb
# MySQL-specific options
charset = utf8mb4
connect_timeout = 10
read_timeout = 30
```

### Required Privileges

Minimum privileges for read-only access:
```sql
GRANT SELECT ON mydb.* TO 'myuser'@'localhost';
```

For full functionality:
```sql
GRANT SELECT, INSERT, UPDATE, DELETE ON mydb.* TO 'myuser'@'localhost';
GRANT SHOW DATABASES ON *.* TO 'myuser'@'localhost';
GRANT SHOW VIEW ON mydb.* TO 'myuser'@'localhost';
```

---

## PostgreSQL

### Connection

```bash
# Basic connection
PGPASSWORD=secret sql-fuse -t postgresql -H localhost -P 5432 -u myuser -D mydb /mnt/postgres

# Using .pgpass file (recommended)
sql-fuse -t postgresql -H localhost -u myuser -D mydb /mnt/postgres
```

### Supported Features

| Feature | Status | Notes |
|---------|--------|-------|
| Tables | Full | Read/Write supported |
| Views | Full | Read-only |
| Functions | Full | Definition only |
| Procedures | Full | PostgreSQL 11+ |
| Triggers | Full | Definition only |
| Schemas | Partial | Public schema by default |
| Extensions | Info | Listed in database info |

### PostgreSQL-Specific Objects

- **Schemas**: Tables from public schema shown by default
- **Sequences**: Accessible via tables directory
- **Indexes**: Shown in table schema
- **Constraints**: Displayed in schema
- **Custom Types**: Handled transparently

### Data Types

| PostgreSQL Type | JSON Representation |
|-----------------|---------------------|
| INTEGER, BIGINT | Number |
| REAL, DOUBLE PRECISION | Number |
| NUMERIC, DECIMAL | String (preserves precision) |
| VARCHAR, TEXT | String |
| DATE | String (YYYY-MM-DD) |
| TIMESTAMP | String (ISO 8601) |
| TIMESTAMPTZ | String (ISO 8601 with timezone) |
| BYTEA | Base64 encoded string |
| JSON, JSONB | Nested JSON object |
| ARRAY | JSON array |
| UUID | String |
| BOOLEAN | Boolean |
| ENUM | String |

### Connection Options

```ini
[connection]
type = postgresql
host = localhost
port = 5432
user = myuser
database = mydb
# PostgreSQL-specific options
sslmode = prefer
connect_timeout = 10
application_name = sql-fuse
```

### Required Privileges

Minimum privileges for read-only access:
```sql
GRANT CONNECT ON DATABASE mydb TO myuser;
GRANT USAGE ON SCHEMA public TO myuser;
GRANT SELECT ON ALL TABLES IN SCHEMA public TO myuser;
```

For full functionality:
```sql
GRANT ALL PRIVILEGES ON DATABASE mydb TO myuser;
GRANT ALL PRIVILEGES ON ALL TABLES IN SCHEMA public TO myuser;
GRANT ALL PRIVILEGES ON ALL SEQUENCES IN SCHEMA public TO myuser;
GRANT EXECUTE ON ALL FUNCTIONS IN SCHEMA public TO myuser;
```

### SSL/TLS Configuration

PostgreSQL SSL modes:
- `disable` - No SSL
- `allow` - Try non-SSL, then SSL
- `prefer` - Try SSL, then non-SSL (default)
- `require` - SSL required
- `verify-ca` - SSL with CA verification
- `verify-full` - SSL with full verification

---

## SQLite

### Connection

```bash
# Connect to a database file
sql-fuse -t sqlite -d /path/to/database.db /mnt/sqlite

# In-memory database (useful for testing)
sql-fuse -t sqlite -d :memory: /mnt/sqlite
```

### Supported Features

| Feature | Status | Notes |
|---------|--------|-------|
| Tables | Full | Read/Write supported |
| Views | Full | Read-only |
| Triggers | Full | Definition only |
| Indexes | Info | Shown in schema |
| Virtual Tables | Partial | FTS5, R-Tree supported |

### SQLite-Specific Features

- **No Server**: Direct file access
- **Single Writer**: Only one write operation at a time
- **WAL Mode**: Supported for better concurrency
- **Attached Databases**: Not currently supported

### Data Types

SQLite uses dynamic typing. JSON representation:

| SQLite Affinity | JSON Representation |
|-----------------|---------------------|
| INTEGER | Number |
| REAL | Number |
| TEXT | String |
| BLOB | Base64 encoded string |
| NULL | null |

### Connection Options

```ini
[connection]
type = sqlite
# SQLite-specific options
journal_mode = WAL
synchronous = NORMAL
cache_size = 2000
foreign_keys = ON
```

### File Permissions

SQLite requires:
- Read permission on the database file for read operations
- Write permission on the database file AND directory for write operations

```bash
# Ensure proper permissions
chmod 644 /path/to/database.db
chmod 755 /path/to/
```

---

## Database Comparison

| Feature | MySQL | PostgreSQL | SQLite |
|---------|-------|------------|--------|
| Connection Pooling | Yes | Yes | Yes |
| Transactions | Yes | Yes | Yes |
| Stored Procedures | Yes | Yes | No |
| User-Defined Functions | Yes | Yes | No |
| Multiple Databases | Yes | Yes | No |
| Remote Connection | Yes | Yes | No |
| Authentication | Yes | Yes | File-based |
| Max Table Size | Unlimited* | Unlimited* | 281 TB |

*Subject to system and configuration limits

## Unsupported Databases

The following databases are not currently supported but planned for future releases:

- **Oracle Database** - Planned
- **Microsoft SQL Server** - Under consideration
- **MongoDB** - Under consideration

## Adding Database Support

SQL-FUSE is designed to be extensible. To add support for a new database:

1. Implement `Connection` interface
2. Implement `ConnectionPool` interface
3. Implement `SchemaManager` interface
4. Implement `FormatConverter` for database-specific SQL
5. Add CMake build option
6. Add integration tests

See [Architecture](architecture.md) for implementation details.
