# SQL FUSE Filesystem

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)

Mount SQL databases as a virtual filesystem using FUSE (Filesystem in Userspace).

**Supported databases:**
- MySQL/MariaDB
- SQLite
- PostgreSQL

Oracle support planned for future releases.

## Documentation

Comprehensive documentation is available in the [docs/](docs/) directory:

- [Getting Started](docs/getting-started.md) - Installation and basic usage
- [Configuration](docs/configuration.md) - Configuration options and settings
- [Filesystem Structure](docs/filesystem-structure.md) - How databases map to files
- [Supported Databases](docs/supported-databases.md) - Database-specific features
- [Architecture](docs/architecture.md) - Technical design details
- [API Reference](docs/api-reference.md) - Internal API documentation
- [Contributing](docs/contributing.md) - How to contribute
- [Troubleshooting](docs/troubleshooting.md) - Common issues and solutions

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
- At least one database client library (MySQL, SQLite, and/or PostgreSQL)

### Ubuntu/Debian

```bash
# Core dependencies
sudo apt-get install libfuse3-dev cmake g++ pkg-config

# For MySQL support
sudo apt-get install libmysqlclient-dev mysql-client

# For SQLite support
sudo apt-get install libsqlite3-dev sqlite3

# For PostgreSQL support
sudo apt-get install libpq-dev postgresql-client
```

For running the MySQL test database, you also need MySQL server:
```bash
sudo apt-get install mysql-server
sudo systemctl start mysql
```

For running the PostgreSQL test database:
```bash
sudo apt-get install postgresql
sudo systemctl start postgresql
```

### Fedora/RHEL

```bash
# Core dependencies
sudo dnf install fuse3-devel cmake gcc-c++ pkgconfig

# For MySQL support
sudo dnf install mysql-devel mysql

# For SQLite support
sudo dnf install sqlite-devel sqlite

# For PostgreSQL support
sudo dnf install libpq-devel postgresql
```

For running the MySQL test database:
```bash
sudo dnf install mysql-server
sudo systemctl start mysqld
```

For running the PostgreSQL test database:
```bash
sudo dnf install postgresql-server
sudo postgresql-setup --initdb
sudo systemctl start postgresql
```

### Arch Linux

```bash
# Core dependencies
sudo pacman -S fuse3 cmake gcc pkgconf

# For MySQL/MariaDB support
sudo pacman -S mariadb-libs mariadb-clients

# For SQLite support
sudo pacman -S sqlite

# For PostgreSQL support
sudo pacman -S postgresql-libs
```

For running the MySQL test database:
```bash
sudo pacman -S mariadb
sudo mariadb-install-db --user=mysql --basedir=/usr --datadir=/var/lib/mysql
sudo systemctl start mariadb
```

For running the PostgreSQL test database:
```bash
sudo pacman -S postgresql
sudo -u postgres initdb -D /var/lib/postgres/data
sudo systemctl start postgresql
```

## Building

By default, MySQL, SQLite, and PostgreSQL support are enabled (if libraries are found).

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
# Build with all backends (default, if libraries found)
cmake ..

# Build with MySQL only
cmake -DWITH_MYSQL=ON -DWITH_SQLITE=OFF -DWITH_POSTGRESQL=OFF ..

# Build with SQLite only
cmake -DWITH_MYSQL=OFF -DWITH_SQLITE=ON -DWITH_POSTGRESQL=OFF ..

# Build with PostgreSQL only
cmake -DWITH_MYSQL=OFF -DWITH_SQLITE=OFF -DWITH_POSTGRESQL=ON ..

# Build with MySQL and PostgreSQL
cmake -DWITH_MYSQL=ON -DWITH_SQLITE=OFF -DWITH_POSTGRESQL=ON ..
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

### PostgreSQL Test Database

**Prerequisites:** PostgreSQL server must be running:
```bash
# Check if PostgreSQL is running
sudo systemctl status postgresql

# Start if not running
sudo systemctl start postgresql
```

**Setup:**
```bash
cd tests/postgresql

# Setup test database (creates 'fuse_test' database and user)
./setup_test_db.sh

# Verify setup
PGPASSWORD=fuse_test_password psql -h localhost -U fuse_test -d fuse_test -f verify_test_db.sql

# Teardown when done
./teardown_test_db.sh
```

The PostgreSQL test database includes:
- 8 tables (users, categories, products, orders, order_items, audit_log, settings)
- 5 views (v_active_users, v_product_catalog, v_order_summary, v_revenue_by_category, v_low_stock)
- 5 stored functions
- 7 triggers
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

### PostgreSQL Usage

```bash
# Mount PostgreSQL with username and password
sql-fuse -t postgresql -H localhost -u myuser -p mypassword /mnt/sql

# Mount with password from environment
export PGPASSWORD=mypassword
sql-fuse -t postgresql -u myuser /mnt/sql

# Mount with specific database
sql-fuse -t postgresql -H localhost -u myuser -p mypassword -D mydb /mnt/sql

# Mount with custom port
sql-fuse -t postgresql -H localhost -P 5432 -u myuser -p mypassword /mnt/sql

# Mount in foreground with debug output
sql-fuse -t postgresql -u myuser -H localhost -f -d /mnt/sql

# Mount as read-only
sql-fuse -t postgresql -u myuser --read-only /mnt/sql

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

### Using with PostgreSQL Test Database

```bash
# Create mount point
mkdir -p /tmp/sql-fuse

# First, create the test database
cd tests/postgresql && ./setup_test_db.sh && cd ../..

# Mount PostgreSQL test database
./sql-fuse -t postgresql -H localhost -u fuse_test -p fuse_test_password -D fuse_test -f /tmp/sql-fuse

# In another terminal, browse the filesystem
ls /tmp/sql-fuse/fuse_test/tables/
cat /tmp/sql-fuse/fuse_test/tables/users.csv
cat /tmp/sql-fuse/fuse_test/tables/products.json
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
  -t, --type <type>         Database type: mysql, sqlite, postgresql (default: mysql)

Connection Options:
  -H, --host <host>         Database server host (default: localhost)
                            For SQLite: path to database file
  -P, --port <port>         Database server port (default: 3306 MySQL, 5432 PostgreSQL)
  -u, --user <user>         Database username (required for MySQL/PostgreSQL)
  -p, --password <pass>     Database password (or use MYSQL_PWD/PGPASSWORD env)
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
    ├── test_path_router.cpp
    └── postgresql/               # PostgreSQL test database scripts
        ├── setup_test_db.sh
        ├── setup_test_db.sql
        ├── teardown_test_db.sh
        └── verify_test_db.sql
```

## Security Considerations

- Store passwords in environment variables (`MYSQL_PWD`, `PGPASSWORD`) rather than command line
- Use `--read-only` mode when you only need to browse data
- Create a database user with minimal required privileges
- Use the `--databases` option to limit exposed databases
- Run with a non-root user when possible
- Configuration files with passwords should have 600 permissions

## License

SQL-FUSE is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

See [LICENSE](LICENSE) for the full license text.
