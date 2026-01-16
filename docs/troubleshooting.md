# Troubleshooting

This guide covers common issues and their solutions when using SQL-FUSE.

## Connection Issues

### Cannot Connect to Database

**Symptoms:**
- "Failed to connect" error message
- Filesystem fails to mount

**MySQL/MariaDB:**
```bash
# Check if MySQL is running
systemctl status mysql

# Test connection manually
mysql -h localhost -u myuser -p mydb

# Check if user can connect from current host
# In MySQL:
SELECT user, host FROM mysql.user WHERE user = 'myuser';
```

**PostgreSQL:**
```bash
# Check if PostgreSQL is running
systemctl status postgresql

# Test connection manually
PGPASSWORD=secret psql -h localhost -U myuser -d mydb

# Check pg_hba.conf for authentication rules
sudo cat /etc/postgresql/*/main/pg_hba.conf
```

**SQLite:**
```bash
# Check file exists and is readable
ls -la /path/to/database.db

# Check file isn't locked
fuser /path/to/database.db
```

### Authentication Failed

**Symptoms:**
- "Access denied" or "authentication failed" errors

**Solutions:**

1. **Verify credentials:**
   ```bash
   # MySQL
   mysql -h localhost -u myuser -p

   # PostgreSQL
   PGPASSWORD=secret psql -h localhost -U myuser -d mydb
   ```

2. **Check password is set correctly:**
   ```bash
   # MySQL - use environment variable
   MYSQL_PWD=secret sql-fuse -t mysql -H localhost -u myuser -D mydb /mnt/db

   # PostgreSQL
   PGPASSWORD=secret sql-fuse -t postgresql -H localhost -u myuser -D mydb /mnt/db
   ```

3. **Check user permissions:**
   ```sql
   -- MySQL
   SHOW GRANTS FOR 'myuser'@'localhost';

   -- PostgreSQL
   \du myuser
   ```

### Connection Timeout

**Symptoms:**
- Long delays before failing
- "Connection timed out" errors

**Solutions:**

1. **Check network connectivity:**
   ```bash
   ping database-host
   telnet database-host 3306
   ```

2. **Check firewall rules:**
   ```bash
   sudo iptables -L -n | grep 3306
   ```

3. **Adjust timeout settings:**
   ```bash
   sql-fuse --connect-timeout 30 ...
   ```

## Mount Issues

### Mount Point Already in Use

**Symptoms:**
- "mountpoint is not empty"
- "Device or resource busy"

**Solutions:**

1. **Check if already mounted:**
   ```bash
   mountpoint /mnt/db
   mount | grep sql-fuse
   ```

2. **Unmount existing:**
   ```bash
   fusermount3 -u /mnt/db
   # or force unmount
   fusermount3 -uz /mnt/db
   ```

3. **Use empty directory:**
   ```bash
   mkdir -p /mnt/db-new
   sql-fuse ... /mnt/db-new
   ```

### Permission Denied on Mount

**Symptoms:**
- "Permission denied" when mounting
- Cannot access mounted files

**Solutions:**

1. **Check FUSE permissions:**
   ```bash
   # User must be in 'fuse' group
   groups
   sudo usermod -a -G fuse $USER
   # Log out and back in
   ```

2. **Check /etc/fuse.conf:**
   ```bash
   # Enable user_allow_other if needed
   sudo nano /etc/fuse.conf
   # Uncomment: user_allow_other
   ```

3. **Use allow_other option:**
   ```bash
   sql-fuse -o allow_other ...
   ```

### Filesystem Becomes Unresponsive

**Symptoms:**
- Commands hang
- Cannot access or unmount filesystem

**Solutions:**

1. **Force unmount:**
   ```bash
   fusermount3 -uz /mnt/db
   ```

2. **Check sql-fuse process:**
   ```bash
   ps aux | grep sql-fuse
   # Kill if necessary
   kill -9 <pid>
   ```

3. **Check database connection:**
   - Database server may have restarted
   - Network connection may have dropped

4. **Check logs:**
   ```bash
   tail -100 ~/.sql-fuse.log
   ```

## Data Issues

### Empty Directories

**Symptoms:**
- Tables directory is empty
- Database shows no content

**Solutions:**

1. **Check database permissions:**
   ```sql
   -- MySQL
   SHOW GRANTS FOR 'myuser'@'localhost';
   GRANT SELECT ON mydb.* TO 'myuser'@'localhost';

   -- PostgreSQL
   GRANT SELECT ON ALL TABLES IN SCHEMA public TO myuser;
   ```

2. **Check database filter:**
   ```bash
   # If using allowed_databases, ensure your DB is listed
   sql-fuse --config /etc/sql-fuse.conf ...
   ```

3. **Verify database has tables:**
   ```sql
   -- MySQL
   SHOW TABLES FROM mydb;

   -- PostgreSQL
   \dt
   ```

### Truncated Data

**Symptoms:**
- Table data appears incomplete
- Large tables show partial content

**Solutions:**

1. **Increase row limit:**
   ```bash
   sql-fuse --row-limit 100000 ...
   ```

2. **Check configuration:**
   ```ini
   [data]
   row_limit = 100000
   ```

### Encoding Issues

**Symptoms:**
- Special characters display incorrectly
- Unicode text is garbled

**Solutions:**

1. **Check database encoding:**
   ```sql
   -- MySQL
   SHOW VARIABLES LIKE 'character_set%';

   -- PostgreSQL
   SHOW client_encoding;
   ```

2. **Set connection charset:**
   ```bash
   # MySQL
   sql-fuse --charset utf8mb4 ...
   ```

3. **Check terminal encoding:**
   ```bash
   echo $LANG
   locale
   ```

### Stale Data

**Symptoms:**
- Data doesn't reflect recent changes
- Old values shown after updates

**Solutions:**

1. **Clear cache:**
   ```bash
   # Unmount and remount
   fusermount3 -u /mnt/db
   sql-fuse ... /mnt/db
   ```

2. **Disable caching:**
   ```bash
   sql-fuse --no-cache ...
   ```

3. **Reduce cache TTL:**
   ```bash
   sql-fuse --cache-ttl 10 ...
   ```

## Performance Issues

### Slow File Listing

**Symptoms:**
- `ls` commands take long time
- Directory listing is slow

**Solutions:**

1. **Enable caching:**
   ```bash
   sql-fuse --cache-ttl 300 ...
   ```

2. **Increase connection pool:**
   ```bash
   sql-fuse --pool-size 10 ...
   ```

3. **Check database performance:**
   ```sql
   -- MySQL
   SHOW PROCESSLIST;
   SHOW STATUS LIKE 'Threads%';

   -- PostgreSQL
   SELECT * FROM pg_stat_activity;
   ```

### Slow File Reads

**Symptoms:**
- Reading large files is slow
- `cat` commands hang

**Solutions:**

1. **Limit data returned:**
   ```bash
   sql-fuse --row-limit 10000 ...
   ```

2. **Check network latency:**
   ```bash
   ping database-host
   ```

3. **Optimize database queries:**
   - Add appropriate indexes
   - Check query execution plans

### High Memory Usage

**Symptoms:**
- sql-fuse process uses excessive memory
- System becomes slow

**Solutions:**

1. **Reduce cache size:**
   ```ini
   [performance]
   max_cache_size = 50
   ```

2. **Limit row counts:**
   ```bash
   sql-fuse --row-limit 5000 ...
   ```

3. **Reduce connection pool:**
   ```bash
   sql-fuse --pool-size 3 ...
   ```

## Write Operation Issues

### Cannot Create Files

**Symptoms:**
- "Read-only file system" error
- Permission denied on write

**Solutions:**

1. **Check database permissions:**
   ```sql
   -- MySQL
   GRANT INSERT, UPDATE, DELETE ON mydb.* TO 'myuser'@'localhost';

   -- PostgreSQL
   GRANT INSERT, UPDATE, DELETE ON ALL TABLES IN SCHEMA public TO myuser;
   ```

2. **Check security configuration:**
   ```ini
   [security]
   allow_write = true
   ```

### Write Operations Fail Silently

**Symptoms:**
- No error but data not saved
- File appears written but data not in database

**Solutions:**

1. **Check logs for errors:**
   ```bash
   tail -f ~/.sql-fuse.log
   ```

2. **Verify data format:**
   - CSV must have correct headers
   - JSON must be valid

3. **Check constraints:**
   - Primary key violations
   - Foreign key constraints
   - NOT NULL constraints

## Debugging

### Enable Debug Logging

```bash
sql-fuse --log-level debug ...

# Or in config file
[logging]
level = debug
file = /tmp/sql-fuse-debug.log
```

### Run in Foreground

```bash
sql-fuse -f -t mysql -H localhost -u user -D db /mnt/db
```

### Check FUSE Debug Output

```bash
sql-fuse -o debug -f ...
```

### Trace System Calls

```bash
strace -f sql-fuse ...
```

## Getting Help

If you can't resolve an issue:

1. **Check existing issues:**
   https://github.com/darkclad/sql-fuse/issues

2. **Collect information:**
   - SQL-FUSE version (`sql-fuse --version`)
   - Operating system and version
   - Database type and version
   - Full error message
   - Relevant log output
   - Steps to reproduce

3. **Open an issue:**
   Include all collected information

## Common Error Messages

| Error | Cause | Solution |
|-------|-------|----------|
| `Transport endpoint is not connected` | FUSE crashed | Force unmount, restart |
| `No such file or directory` | Path doesn't exist | Check path, permissions |
| `Input/output error` | Database error | Check logs, database |
| `Permission denied` | Access issue | Check file/DB permissions |
| `Connection refused` | DB not running | Start database server |
| `Too many open files` | Resource limit | Increase ulimit |
| `Cannot allocate memory` | Memory exhausted | Reduce cache/limits |
