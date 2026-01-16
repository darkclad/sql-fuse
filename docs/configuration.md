# Configuration

SQL-FUSE can be configured through command-line arguments, environment variables, and configuration files.

## Configuration File

SQL-FUSE looks for configuration in the following locations (in order):
1. Path specified with `--config` option
2. `./sql-fuse.conf`
3. `~/.config/sql-fuse/config`
4. `/etc/sql-fuse/config`

### Configuration File Format

```ini
# sql-fuse.conf example

[connection]
type = mysql
host = localhost
port = 3306
user = myuser
database = mydb
# password can be set here but environment variable is recommended
# password = secret

[performance]
connection_pool_size = 5
cache_ttl = 300
max_cache_size = 100

[data]
default_format = csv
row_limit = 10000
include_blobs = false

[security]
allowed_databases = db1,db2,db3
# empty means all databases are allowed

[logging]
level = info
file = ~/.sql-fuse.log
```

## Environment Variables

| Variable | Description |
|----------|-------------|
| `MYSQL_PWD` | MySQL password |
| `PGPASSWORD` | PostgreSQL password |
| `SQL_FUSE_CONFIG` | Path to configuration file |
| `SQL_FUSE_LOG_LEVEL` | Logging level (debug, info, warn, error) |

## Command Line Options Reference

### Connection Options

```
-t, --type <type>       Database type: mysql, postgresql, sqlite
-H, --host <host>       Database server hostname (default: localhost)
-P, --port <port>       Database server port
-u, --user <user>       Database username
-D, --database <db>     Database name to connect to
-d, --db-path <path>    Path to SQLite database file
```

### Performance Options

```
--pool-size <n>         Connection pool size (default: 5)
--cache-ttl <seconds>   Cache time-to-live in seconds (default: 300)
--no-cache              Disable caching
```

### Output Options

```
--format <fmt>          Default output format: csv, json, sql
--row-limit <n>         Maximum rows to return (default: 10000)
--include-blobs         Include BLOB data in output
```

### FUSE Options

```
-f, --foreground        Run in foreground (don't daemonize)
-o <options>            FUSE mount options (comma-separated)
```

Common FUSE options:
- `allow_other` - Allow other users to access the mount
- `allow_root` - Allow root to access the mount
- `default_permissions` - Enable permission checking
- `nonempty` - Allow mounting over non-empty directory

### Logging Options

```
--log-level <level>     Log level: debug, info, warn, error
--log-file <path>       Log file path
```

## Performance Tuning

### Connection Pool Size

The connection pool maintains a set of pre-established database connections. Adjust based on:

- **Small/personal use**: 2-3 connections
- **Medium workload**: 5-10 connections
- **Heavy concurrent access**: 10-20 connections

```bash
sql-fuse -t mysql -H localhost -u user -D db --pool-size 10 /mnt/mysql
```

### Cache Configuration

Caching improves performance by storing frequently accessed data:

- **cache_ttl**: How long cached data remains valid (seconds)
- **max_cache_size**: Maximum number of cached entries

For read-heavy workloads with stable data:
```ini
[performance]
cache_ttl = 600
max_cache_size = 500
```

For frequently changing data:
```ini
[performance]
cache_ttl = 30
max_cache_size = 50
```

### Row Limits

To prevent memory issues with large tables:

```bash
sql-fuse -t mysql -H localhost -u user -D db --row-limit 5000 /mnt/mysql
```

## Security Considerations

### Password Handling

**Recommended**: Use environment variables for passwords:

```bash
MYSQL_PWD=secret sql-fuse -t mysql -H localhost -u user -D db /mnt/mysql
```

**Avoid**: Passing passwords on command line (visible in process list)

### Database Access Restrictions

Limit which databases can be accessed:

```ini
[security]
allowed_databases = production_readonly,analytics
```

### File Permissions

The mounted filesystem inherits permissions from the mounting user. Use FUSE options for additional control:

```bash
# Allow other users (requires /etc/fuse.conf: user_allow_other)
sql-fuse -t mysql -H localhost -u user -D db -o allow_other /mnt/mysql

# Use default permission checking
sql-fuse -t mysql -H localhost -u user -D db -o default_permissions /mnt/mysql
```

### Network Security

For remote databases:
- Use SSL/TLS connections when available
- Consider SSH tunneling for additional security
- Restrict database user permissions to minimum required

## Example Configurations

### Development Setup

```ini
[connection]
type = mysql
host = localhost
user = developer
database = dev_db

[performance]
connection_pool_size = 3
cache_ttl = 60

[logging]
level = debug
```

### Production Read-Only Access

```ini
[connection]
type = postgresql
host = db.example.com
port = 5432
user = readonly_user
database = production

[performance]
connection_pool_size = 10
cache_ttl = 300
max_cache_size = 200

[data]
row_limit = 50000

[security]
allowed_databases = production

[logging]
level = warn
file = /var/log/sql-fuse.log
```

### SQLite Local Database

```ini
[connection]
type = sqlite

[data]
default_format = json
include_blobs = false

[logging]
level = info
```
