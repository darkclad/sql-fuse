#!/bin/bash
# SQLite Test Database Setup Script
# Creates a test SQLite database with sample data for testing sql-fuse

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DB_FILE="${SCRIPT_DIR}/fuse_test.db"

echo "Creating SQLite test database at: ${DB_FILE}"

# Remove existing database if it exists
rm -f "${DB_FILE}"

# Create database and populate with test data
sqlite3 "${DB_FILE}" < "${SCRIPT_DIR}/setup_test_db.sql"

echo "SQLite test database created successfully!"
echo ""
echo "To test with sql-fuse:"
echo "  mkdir -p /tmp/sql-fuse"
echo "  ./sql-fuse -t sqlite -H ${DB_FILE} -f /tmp/sql-fuse"
echo ""
echo "Then in another terminal:"
echo "  ls /tmp/sql-fuse/main/tables/"
echo "  cat /tmp/sql-fuse/main/tables/users.csv"
