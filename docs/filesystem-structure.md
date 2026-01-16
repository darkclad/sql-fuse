# Filesystem Structure

SQL-FUSE maps database objects to a hierarchical filesystem structure. This document describes how databases, tables, and other objects are represented as files and directories.

## Overview

```
/mountpoint/
├── .server_info          # Server version and connection info
├── .users/               # Database users (if accessible)
├── .variables/           # Server variables
│   ├── global/
│   └── session/
├── database1/
│   ├── .info             # Database metadata
│   ├── tables/
│   │   ├── users.csv     # Table data as CSV
│   │   ├── users.json    # Table data as JSON
│   │   ├── users.sql     # CREATE TABLE statement
│   │   └── users/        # Table directory
│   │       ├── .schema   # Column definitions
│   │       └── rows/     # Individual rows
│   │           ├── 1.json
│   │           ├── 2.json
│   │           └── ...
│   ├── views/
│   │   ├── active_users.csv
│   │   ├── active_users.json
│   │   └── active_users.sql
│   ├── functions/
│   │   └── calculate_total.sql
│   ├── procedures/
│   │   └── process_order.sql
│   └── triggers/
│       └── audit_log.sql
└── database2/
    └── ...
```

## Root Directory

The root of the mounted filesystem contains:

### `.server_info`
A read-only file containing server information:
```
Server: MySQL 8.0.32
Host: localhost
Port: 3306
User: myuser
Connection ID: 12345
```

### `.users/`
Directory containing database users (requires appropriate permissions):
```
.users/
├── root.json
├── myuser.json
└── readonly.json
```

### `.variables/`
Server variables organized by scope:
```
.variables/
├── global/
│   ├── max_connections
│   ├── innodb_buffer_pool_size
│   └── ...
└── session/
    ├── sql_mode
    ├── autocommit
    └── ...
```

### Database Directories
Each accessible database appears as a directory at the root level.

## Database Directory

Each database directory contains:

### `.info`
Database metadata file:
```
Database: mydb
Character Set: utf8mb4
Collation: utf8mb4_unicode_ci
Tables: 15
Views: 3
Size: 1.2 GB
```

### `tables/`
Contains all tables in the database.

### `views/`
Contains all views in the database.

### `functions/`
Contains user-defined functions.

### `procedures/`
Contains stored procedures.

### `triggers/`
Contains database triggers.

## Tables Directory

The tables directory provides multiple representations of each table:

### Table Files

For each table, three files are available:

| File | Description |
|------|-------------|
| `tablename.csv` | Table data in CSV format with headers |
| `tablename.json` | Table data as JSON array of objects |
| `tablename.sql` | CREATE TABLE statement |

Example `users.csv`:
```csv
id,username,email,created_at
1,john,john@example.com,2024-01-15 10:30:00
2,jane,jane@example.com,2024-01-16 14:22:00
```

Example `users.json`:
```json
[
  {"id": 1, "username": "john", "email": "john@example.com", "created_at": "2024-01-15 10:30:00"},
  {"id": 2, "username": "jane", "email": "jane@example.com", "created_at": "2024-01-16 14:22:00"}
]
```

Example `users.sql`:
```sql
CREATE TABLE `users` (
  `id` int NOT NULL AUTO_INCREMENT,
  `username` varchar(50) NOT NULL,
  `email` varchar(100) NOT NULL,
  `created_at` timestamp DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  UNIQUE KEY `username` (`username`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
```

### Table Directory

Each table also has a corresponding directory for detailed access:

```
tables/users/
├── .schema           # Column definitions
└── rows/             # Individual row files
    ├── 1.json
    ├── 2.json
    └── ...
```

#### `.schema` File
Contains column information:
```json
{
  "table": "users",
  "columns": [
    {"name": "id", "type": "int", "nullable": false, "key": "PRI", "default": null, "extra": "auto_increment"},
    {"name": "username", "type": "varchar(50)", "nullable": false, "key": "UNI", "default": null},
    {"name": "email", "type": "varchar(100)", "nullable": false, "key": "", "default": null},
    {"name": "created_at", "type": "timestamp", "nullable": true, "key": "", "default": "CURRENT_TIMESTAMP"}
  ],
  "primary_key": "id",
  "engine": "InnoDB",
  "row_count": 1000
}
```

#### `rows/` Directory
Contains individual rows as JSON files, named by primary key:
```json
// rows/1.json
{
  "id": 1,
  "username": "john",
  "email": "john@example.com",
  "created_at": "2024-01-15 10:30:00"
}
```

## Views Directory

Views are represented similarly to tables:

```
views/
├── active_users.csv
├── active_users.json
└── active_users.sql    # CREATE VIEW statement
```

The `.sql` file contains the view definition:
```sql
CREATE VIEW `active_users` AS
SELECT id, username, email
FROM users
WHERE status = 'active';
```

## Functions Directory

Each function has a `.sql` file with its definition:

```sql
-- functions/calculate_total.sql
CREATE FUNCTION `calculate_total`(order_id INT)
RETURNS DECIMAL(10,2)
DETERMINISTIC
BEGIN
  DECLARE total DECIMAL(10,2);
  SELECT SUM(price * quantity) INTO total
  FROM order_items WHERE order_id = order_id;
  RETURN COALESCE(total, 0);
END
```

## Procedures Directory

Each procedure has a `.sql` file:

```sql
-- procedures/process_order.sql
CREATE PROCEDURE `process_order`(IN order_id INT)
BEGIN
  UPDATE orders SET status = 'processing' WHERE id = order_id;
  -- Additional logic...
END
```

## Triggers Directory

Each trigger has a `.sql` file:

```sql
-- triggers/audit_log.sql
CREATE TRIGGER `audit_log`
AFTER UPDATE ON users
FOR EACH ROW
BEGIN
  INSERT INTO audit_log (table_name, action, old_data, new_data)
  VALUES ('users', 'UPDATE', OLD.id, NEW.id);
END
```

## File Permissions

All files in the virtual filesystem have the following default permissions:

| Type | Permission | Description |
|------|------------|-------------|
| Directories | `drwxr-xr-x` (755) | Read and execute for all |
| Data files (.csv, .json) | `-rw-r--r--` (644) | Read for all, write for owner |
| Schema files (.sql) | `-r--r--r--` (444) | Read-only |
| Special files (.info, .schema) | `-r--r--r--` (444) | Read-only |

## File Sizes

File sizes shown by `ls -l` are:
- **Data files**: Actual size of the content
- **Schema files**: Actual size of the definition
- **Directories**: Always shown as 0

Note: Large tables may show an estimated size before the file is read.

## Write Operations

SQL-FUSE supports limited write operations:

### Creating New Rows
Write JSON to create a new row:
```bash
echo '{"username": "newuser", "email": "new@example.com"}' > /mnt/mysql/mydb/tables/users/rows/new.json
```

### Updating Existing Rows
Overwrite a row file to update:
```bash
echo '{"id": 1, "username": "updated", "email": "updated@example.com"}' > /mnt/mysql/mydb/tables/users/rows/1.json
```

### Deleting Rows
Remove a row file to delete:
```bash
rm /mnt/mysql/mydb/tables/users/rows/1.json
```

### Appending to Tables
Append CSV or JSON to table files:
```bash
echo 'newuser,new@example.com' >> /mnt/mysql/mydb/tables/users.csv
```

## Limitations

- Maximum file size for reads is limited by available memory
- Very large tables may be truncated (configurable via `--row-limit`)
- BLOB/BINARY data is Base64 encoded in JSON output
- Some special characters in names may be escaped or replaced
- Write operations require appropriate database permissions
