# SQL-FUSE Documentation

SQL-FUSE is a FUSE (Filesystem in Userspace) implementation that exposes SQL databases as a virtual filesystem. This allows you to browse database schemas, read table data, and interact with database objects using standard filesystem tools.

## Table of Contents

- [Getting Started](getting-started.md) - Installation and basic usage
- [Configuration](configuration.md) - Configuration options and settings
- [Filesystem Structure](filesystem-structure.md) - How the database is mapped to files
- [Supported Databases](supported-databases.md) - MySQL, PostgreSQL, and SQLite specifics
- [Architecture](architecture.md) - Technical design and implementation details
- [API Reference](api-reference.md) - Internal API documentation
- [Contributing](contributing.md) - How to contribute to the project
- [Troubleshooting](troubleshooting.md) - Common issues and solutions

## Quick Start

```bash
# Mount a MySQL database
sql-fuse -t mysql -H localhost -u root -D mydb /mnt/mysql

# Mount a PostgreSQL database
PGPASSWORD=secret sql-fuse -t postgresql -H localhost -u postgres -D mydb /mnt/postgres

# Mount a SQLite database
sql-fuse -t sqlite -d /path/to/database.db /mnt/sqlite

# Browse the mounted filesystem
ls /mnt/mysql/mydb/tables/
cat /mnt/mysql/mydb/tables/users.csv
```

## Features

- **Multi-database support**: MySQL, PostgreSQL, and SQLite
- **Multiple output formats**: CSV, JSON, and SQL for table data
- **Schema browsing**: View tables, views, functions, procedures, and triggers
- **Row-level access**: Access individual rows as JSON files
- **Read/Write support**: Read data and insert/update records
- **Connection pooling**: Efficient database connection management
- **Caching**: Intelligent caching for improved performance
- **Daemon mode**: Run as a background service

## License

SQL-FUSE is licensed under the GNU General Public License v3.0. See [LICENSE](../LICENSE) for details.
