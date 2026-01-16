# Getting Started

This guide will help you install and configure SQL-FUSE for your first use.

## Prerequisites

### Build Dependencies

- C++17 compatible compiler (GCC 8+ or Clang 7+)
- CMake 3.14 or higher
- FUSE 3.x library and headers
- pkg-config

### Database Client Libraries

Depending on which databases you want to support:

- **MySQL**: libmysqlclient-dev (MySQL) or libmariadb-dev (MariaDB)
- **PostgreSQL**: libpq-dev
- **SQLite**: libsqlite3-dev

### Installing Dependencies

#### Ubuntu/Debian

```bash
# Core dependencies
sudo apt-get install build-essential cmake pkg-config libfuse3-dev

# MySQL support
sudo apt-get install libmysqlclient-dev

# PostgreSQL support
sudo apt-get install libpq-dev

# SQLite support
sudo apt-get install libsqlite3-dev
```

#### Fedora/RHEL

```bash
# Core dependencies
sudo dnf install gcc-c++ cmake pkgconfig fuse3-devel

# MySQL support
sudo dnf install mysql-devel

# PostgreSQL support
sudo dnf install libpq-devel

# SQLite support
sudo dnf install sqlite-devel
```

#### Arch Linux

```bash
# Core dependencies
sudo pacman -S base-devel cmake pkgconf fuse3

# MySQL support
sudo pacman -S mariadb-libs

# PostgreSQL support
sudo pacman -S postgresql-libs

# SQLite support
sudo pacman -S sqlite
```

## Building from Source

```bash
# Clone the repository
git clone https://github.com/darkclad/sql-fuse.git
cd sql-fuse

# Create build directory
mkdir build && cd build

# Configure with desired database support
cmake .. -DWITH_MYSQL=ON -DWITH_POSTGRESQL=ON -DWITH_SQLITE=ON

# Build
make -j$(nproc)

# Install (optional)
sudo make install
```

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `WITH_MYSQL` | ON | Enable MySQL/MariaDB support |
| `WITH_POSTGRESQL` | OFF | Enable PostgreSQL support |
| `WITH_SQLITE` | ON | Enable SQLite support |
| `BUILD_TESTS` | ON | Build unit tests |
| `CMAKE_BUILD_TYPE` | Release | Build type (Debug/Release) |

## Basic Usage

### Mounting a MySQL Database

```bash
# Using command line arguments
sql-fuse -t mysql -H localhost -P 3306 -u myuser -D mydb /mnt/mysql

# Using environment variable for password
MYSQL_PWD=secret sql-fuse -t mysql -H localhost -u myuser -D mydb /mnt/mysql

# Foreground mode (for debugging)
sql-fuse -t mysql -H localhost -u myuser -D mydb -f /mnt/mysql
```

### Mounting a PostgreSQL Database

```bash
# Using environment variable for password
PGPASSWORD=secret sql-fuse -t postgresql -H localhost -P 5432 -u myuser -D mydb /mnt/postgres

# With all options
PGPASSWORD=secret sql-fuse -t postgresql \
    -H localhost \
    -P 5432 \
    -u myuser \
    -D mydb \
    /mnt/postgres
```

### Mounting a SQLite Database

```bash
# SQLite uses the -d option for the database file path
sql-fuse -t sqlite -d /path/to/database.db /mnt/sqlite
```

### Unmounting

```bash
# Standard unmount
fusermount3 -u /mnt/mysql

# Force unmount if busy
fusermount3 -uz /mnt/mysql
```

## Command Line Options

| Option | Long Form | Description |
|--------|-----------|-------------|
| `-t` | `--type` | Database type: mysql, postgresql, sqlite |
| `-H` | `--host` | Database host (default: localhost) |
| `-P` | `--port` | Database port |
| `-u` | `--user` | Database username |
| `-D` | `--database` | Database name |
| `-d` | `--db-path` | SQLite database file path |
| `-f` | `--foreground` | Run in foreground (don't daemonize) |
| `-o` | | FUSE mount options |
| `-h` | `--help` | Show help message |
| `-v` | `--version` | Show version |

## Verifying Installation

After mounting, you should be able to browse the filesystem:

```bash
# List databases (or the connected database)
ls /mnt/mysql/

# List database objects
ls /mnt/mysql/mydb/

# List tables
ls /mnt/mysql/mydb/tables/

# View table data as CSV
cat /mnt/mysql/mydb/tables/users.csv

# View table data as JSON
cat /mnt/mysql/mydb/tables/users.json
```

## Next Steps

- Read the [Configuration Guide](configuration.md) for advanced options
- Learn about the [Filesystem Structure](filesystem-structure.md)
- Check [Supported Databases](supported-databases.md) for database-specific features
