# SQL FUSE Filesystem

Mount SQL databases as a virtual filesystem using FUSE (Filesystem in Userspace).

**Supported databases:**
- MySQL/MariaDB
- SQLite

PostgreSQL and Oracle support planned for future releases.

## Features

- Browse databases, tables, views, procedures, functions, and triggers as directories/files
- Read table data as CSV or JSON files
- View CREATE statements as .sql files
- Access individual rows via the `rows/` subdirectory
- Write support: INSERT, UPDATE, DELETE via file operations
- Configurable caching with TTL
- Connection pooling for performance
- Read-only mode for safety
- Modular architecture - build with only the database backends you need

## Requirements

- Linux with FUSE 3.x support
- C++20 compiler (GCC 10+, Clang 12+)
- CMake 3.16+
- At least one database client library (MySQL and/or SQLite)

### Ubuntu/Debian

```bash
# Core dependencies
sudo apt-get install libfuse3-dev cmake g++ pkg-config

# For MySQL support
sudo apt-get install libmysqlclient-dev mysql-client

# For SQLite support
sudo apt-get install libsqlite3-dev sqlite3
```

For running the MySQL test database, you also need MySQL server:
```bash
sudo apt-get install mysql-server
sudo systemctl start mysql
```

### Fedora/RHEL

```bash
# Core dependencies
sudo dnf install fuse3-devel cmake gcc-c++ pkgconfig

# For MySQL support
sudo dnf install mysql-devel mysql

# For SQLite support
sudo dnf install sqlite-devel sqlite
```

For running the MySQL test database:
```bash
sudo dnf install mysql-server
sudo systemctl start mysqld
```

### Arch Linux

```bash
# Core dependencies
sudo pacman -S fuse3 cmake gcc pkgconf

# For MySQL/MariaDB support
sudo pacman -S mariadb-libs mariadb-clients

# For SQLite support
sudo pacman -S sqlite
```

For running the MySQL test database:
```bash
sudo pacman -S mariadb
sudo mariadb-install-db --user=mysql --basedir=/usr --datadir=/var/lib/mysql
sudo systemctl start mariadb
```

## Building

By default, both MySQL and SQLite support are enabled.

```bash
git clone <repository-url>
cd sql-fuse
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Build Options

You can enable/disable specific database backends:

```bash
# Build with both MySQL and SQLite (default)
cmake ..

# Build with MySQL only
cmake -DWITH_MYSQL=ON -DWITH_SQLITE=OFF ..

# Build with SQLite only
cmake -DWITH_MYSQL=OFF -DWITH_SQLITE=ON ..
```

For debug build:
```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
make -j$(nproc)
```

### Building with Tests

```bash
cmake -DBUILD_TESTS=ON ..
make -j$(nproc)

# Run tests
ctest --output-on-failure
# Or run directly
./tests/sql-fuse-tests
```

## Installation

```bash
sudo make install
```

## Test Database Setup

Test databases with sample data are provided for development and testing.

### MySQL Test Database

**Prerequisites:** MySQL/MariaDB server must be running:
```bash
# Check if MySQL is running
sudo systemctl status mysql

# Start if not running
sudo systemctl start mysql
```

**Setup:**
```bash
cd tests/mysql

# Setup test database (creates 'fuse_test' database and user)
# Uses sudo to connect as MySQL root via socket authentication
./setup_test_db.sh

# Verify setup
mysql -u fuse_test -pfuse_test_password fuse_test < verify_test_db.sql

# Teardown when done
./teardown_test_db.sh
```

The MySQL test database includes:
- 8 tables (users, categories, products, orders, order_items, audit_log, settings)
- 5 views (v_active_users, v_product_catalog, v_order_summary, v_revenue_by_category, v_low_stock)
- 5 stored procedures
- 4 stored functions
- 7 triggers
- 2 scheduled events
- Sample data for testing

### SQLite Test Database

**Setup:**
```bash
cd tests/sqlite

# Create test database
./setup_test_db.sh

# Verify setup
sqlite3 fuse_test.db < verify_test_db.sql

# Teardown when done
./teardown_test_db.sh
```

The SQLite test database includes:
- 7 tables (users, categories, products, orders, order_items, audit_log, settings)
- 4 views (v_active_users, v_product_catalog, v_order_summary, v_low_stock)
- 5 triggers
- Sample data for testing

## Usage

### MySQL Usage

```bash
# Mount MySQL with username and password
sql-fuse -t mysql -u myuser -p mypassword /mnt/sql

# Mount with password from environment
export MYSQL_PWD=mypassword
sql-fuse -u myuser /mnt/sql

# Mount in foreground with debug output
sql-fuse -u myuser -H localhost -f -d /mnt/sql

# Mount as daemon (background - default mode)
sql-fuse -u myuser -p mypassword /mnt/sql
# Logs written to ~/.sql-fuse.log or /var/log/sql-fuse.log

# Mount as read-only
sql-fuse -u myuser --read-only /mnt/sql

# Unmount
fusermount -u /mnt/sql
```

### SQLite Usage

```bash
# Mount SQLite database file
sql-fuse -t sqlite -H /path/to/database.db /mnt/sql

# Or use -D for database path
sql-fuse -t sqlite -D /path/to/database.db /mnt/sql

# Mount in foreground with debug output
sql-fuse -t sqlite -H /path/to/database.db -f -d /mnt/sql

# Mount as read-only
sql-fuse -t sqlite -H /path/to/database.db --read-only /mnt/sql

# Unmount
fusermount -u /mnt/sql
```

### Using with MySQL Test Database

```bash
# Create mount point
mkdir -p /tmp/sql-fuse

# Mount MySQL test database
./sql-fuse -u fuse_test -p fuse_test_password -H localhost -f /tmp/sql-fuse

# In another terminal, browse the filesystem
ls /tmp/sql-fuse/fuse_test/tables/
cat /tmp/sql-fuse/fuse_test/tables/users.csv
cat /tmp/sql-fuse/fuse_test/tables/products.json
```

### Using with SQLite Test Database

```bash
# Create mount point
mkdir -p /tmp/sql-fuse

# First, create the test database
cd tests/sqlite && ./setup_test_db.sh && cd ../..

# Mount SQLite test database
./sql-fuse -t sqlite -H tests/sqlite/fuse_test.db -f /tmp/sql-fuse

# In another terminal, browse the filesystem
ls /tmp/sql-fuse/main/tables/
cat /tmp/sql-fuse/main/tables/users.csv
cat /tmp/sql-fuse/main/tables/products.json
```

### Configuration File

```bash
# Create config file
sudo cp sql-fuse.conf.example /etc/sql-fuse.conf
sudo chmod 600 /etc/sql-fuse.conf
sudo nano /etc/sql-fuse.conf

# Mount using config file
sql-fuse -c /etc/sql-fuse.conf /mnt/sql
```

## Filesystem Structure

```
/mnt/sql/
├── <database>/
│   ├── tables/
│   │   ├── <table>.csv          # Table data as CSV
│   │   ├── <table>.json         # Table data as JSON
│   │   ├── <table>.sql          # CREATE TABLE statement
│   │   └── <table>/             # Table directory
│   │       ├── schema.json      # Column information
│   │       ├── indexes.json     # Index information
│   │       ├── stats.json       # Table statistics
│   │       └── rows/            # Individual rows
│   │           ├── <pk>.json    # Row by primary key
│   │           └── ...
│   ├── views/
│   │   ├── <view>.csv
│   │   ├── <view>.json
│   │   └── <view>.sql
│   ├── procedures/
│   │   └── <proc>.sql
│   ├── functions/
│   │   └── <func>.sql
│   ├── triggers/
│   │   └── <trigger>.sql
│   └── .info                    # Database information
├── .server_info                 # Server information
├── .users/                      # User accounts
│   └── <user>@<host>.info
└── .variables/                  # Server variables
    ├── global/
    └── session/
```

## Examples

### Reading Data

```bash
# List databases
ls /mnt/sql/

# List tables in a database
ls /mnt/sql/mydb/tables/

# Read table as CSV
cat /mnt/sql/mydb/tables/users.csv

# Read table as JSON
cat /mnt/sql/mydb/tables/users.json

# View CREATE TABLE statement
cat /mnt/sql/mydb/tables/users.sql

# View table schema
cat /mnt/sql/mydb/tables/users/schema.json

# View table indexes
cat /mnt/sql/mydb/tables/users/indexes.json

# Read a specific row
cat /mnt/sql/mydb/tables/users/rows/1.json
```

### Writing Data

```bash
# Insert a new row (create new file in rows directory)
echo '{"name": "John", "email": "john@example.com"}' > /mnt/sql/mydb/tables/users/rows/100.json

# Update a row
echo '{"name": "John Doe"}' > /mnt/sql/mydb/tables/users/rows/1.json

# Delete a row
rm /mnt/sql/mydb/tables/users/rows/1.json
```

### Server Information

```bash
# View server status
cat /mnt/sql/.server_info

# List users
ls /mnt/sql/.users/

# View global variables
ls /mnt/sql/.variables/global/
cat /mnt/sql/.variables/global/max_connections
```

## Command Line Options

```
Database Type:
  -t, --type <type>         Database type: mysql, sqlite (default: mysql)

Connection Options:
  -H, --host <host>         Database server host (default: localhost)
                            For SQLite: path to database file
  -P, --port <port>         Database server port (default: 3306)
  -u, --user <user>         Database username (required for MySQL)
  -p, --password <pass>     Database password (or use MYSQL_PWD env)
  -S, --socket <path>       Unix socket path
  -D, --database <db>       Default database (or SQLite file path)

Cache Options:
  --cache-size <MB>         Maximum cache size (default: 100)
  --cache-ttl <seconds>     Default cache TTL (default: 30)
  --no-cache                Disable caching entirely

Data Options:
  --max-rows <N>            Max rows in table files (default: 10000)
  --read-only               Mount as read-only
  --databases <list>        Comma-separated list of databases to expose

FUSE Options:
  -f, --foreground          Run in foreground
  -d, --debug               Enable debug output
  --allow-other             Allow other users to access
  --allow-root              Allow root to access

Configuration:
  -c, --config <file>       Path to configuration file

Other Options:
  -h, --help                Show help message
  -V, --version             Show version information
```

## Project Structure

```
sql-fuse/
├── CMakeLists.txt           # Build configuration
├── README.md                # This file
├── DESIGN.md                # Detailed design document
├── sql-fuse.conf.example    # Example configuration file
├── include/                 # Header files
│   ├── cache_manager.hpp
│   ├── config.hpp
│   ├── connection_pool.hpp       # MySQL connection pool
│   ├── error_handler.hpp
│   ├── format_converter.hpp
│   ├── mysql_schema_manager.hpp  # MySQL-specific schema manager
│   ├── sqlite_schema_manager.hpp # SQLite-specific schema manager
│   ├── sql_fuse_fs.hpp
│   ├── path_router.hpp
│   ├── schema_manager.hpp        # Abstract schema manager base
│   └── virtual_file.hpp
├── src/                     # Source files
│   ├── main.cpp
│   ├── cache_manager.cpp
│   ├── config.cpp
│   ├── connection_pool.cpp       # MySQL connection pool
│   ├── error_handler.cpp
│   ├── format_converter.cpp
│   ├── mysql_schema_manager.cpp  # MySQL implementation
│   ├── sqlite_schema_manager.cpp # SQLite implementation
│   ├── sql_fuse_fs.cpp
│   ├── path_router.cpp
│   ├── schema_manager.cpp        # Factory and type helpers
│   └── virtual_file.cpp
└── tests/                   # Test files
    ├── CMakeLists.txt
    ├── mysql/                    # MySQL test database scripts
    │   ├── setup_test_db.sh
    │   ├── setup_test_db.sql
    │   ├── teardown_test_db.sh
    │   └── verify_test_db.sql
    ├── sqlite/                   # SQLite test database scripts
    │   ├── setup_test_db.sh
    │   ├── setup_test_db.sql
    │   ├── teardown_test_db.sh
    │   └── verify_test_db.sql
    ├── test_main.cpp
    ├── test_cache_manager.cpp
    ├── test_config.cpp
    ├── test_error_handler.cpp
    ├── test_format_converter.cpp
    └── test_path_router.cpp
```

## Security Considerations

- Store passwords in the `MYSQL_PWD` environment variable rather than command line
- Use `--read-only` mode when you only need to browse data
- Create a database user with minimal required privileges
- Use the `--databases` option to limit exposed databases
- Run with a non-root user when possible
- Configuration files with passwords should have 600 permissions

## License

MIT License
