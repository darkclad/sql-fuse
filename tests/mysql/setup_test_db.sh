#!/bin/bash
# SQL FUSE Test Database Setup Script
# Usage: ./setup_test_db.sh
#
# This script uses sudo to connect to MySQL as root (socket authentication)
# which is the default on fresh Ubuntu/Debian MySQL installations.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "=== SQL FUSE Test Database Setup ==="
echo ""

# Check if mysql client is available
if ! command -v mysql &> /dev/null; then
    echo "Error: mysql client not found. Please install MySQL client."
    echo "  Ubuntu/Debian: sudo apt-get install mysql-client"
    exit 1
fi

# Check if MySQL server is running
if ! sudo mysqladmin ping &> /dev/null; then
    echo "Error: MySQL server is not running."
    echo "  Start with: sudo systemctl start mysql"
    exit 1
fi

# Run the setup SQL
echo "Creating test database and objects..."
sudo mysql < "$SCRIPT_DIR/setup_test_db.sql"

# Create test user
echo "Creating test user 'fuse_test'..."
sudo mysql <<EOF
CREATE USER IF NOT EXISTS 'fuse_test'@'localhost' IDENTIFIED BY 'fuse_test_password';
CREATE USER IF NOT EXISTS 'fuse_test'@'%' IDENTIFIED BY 'fuse_test_password';
GRANT ALL PRIVILEGES ON fuse_test.* TO 'fuse_test'@'localhost';
GRANT ALL PRIVILEGES ON fuse_test.* TO 'fuse_test'@'%';
FLUSH PRIVILEGES;
EOF

echo ""
echo "=== Setup Complete ==="
echo ""
echo "Test database: fuse_test"
echo "Test user:     fuse_test"
echo "Test password: fuse_test_password"
echo ""
echo "You can now run sql-fuse with:"
echo "  sql-fuse -u fuse_test -p fuse_test_password -H localhost /mnt/sql"
