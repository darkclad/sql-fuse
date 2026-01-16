# sql-fuse

**Mount SQL databases as a filesystem**

A FUSE-based virtual filesystem that exposes SQL databases as browsable directories and files. Navigate tables, views, and schemas using standard Unix commands like `ls`, `cat`, and `find`.

## Features

- **Multi-database support**: MySQL/MariaDB and SQLite (PostgreSQL and Oracle planned)
- **Read table data** as CSV or JSON files
- **Browse schema** information (columns, indexes, triggers, views)
- **Access individual rows** via `rows/<id>.json` files
- **Configurable caching** with LRU eviction and TTL expiration
- **Connection pooling** for efficient database access

## Quick Start

```bash
# Mount a MySQL database
sql-fuse -t mysql -H localhost -u user -p password /mnt/db

# Mount a SQLite database
sql-fuse -t sqlite -H /path/to/database.db /mnt/db

# Browse and query
ls /mnt/db/mydb/tables/
cat /mnt/db/mydb/tables/users.csv
cat /mnt/db/mydb/tables/users/rows/1.json
```

## Requirements

- Linux with FUSE 3.x
- C++17 compiler
- Database client libraries (libmysqlclient, libsqlite3)

## License

MIT
