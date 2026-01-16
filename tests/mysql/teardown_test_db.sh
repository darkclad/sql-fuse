#!/bin/bash
# SQL FUSE Test Database Teardown Script
# Usage: ./teardown_test_db.sh
#
# This script uses sudo to connect to MySQL as root (socket authentication)

set -e

echo "=== SQL FUSE Test Database Teardown ==="
echo ""

# Check if mysql client is available
if ! command -v mysql &> /dev/null; then
    echo "Error: mysql client not found. Please install MySQL client."
    exit 1
fi

# Confirm
read -p "This will DROP the fuse_test database and user. Continue? [y/N] " -n 1 -r
echo ""
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "Aborted."
    exit 0
fi

echo "Dropping test database and user..."
sudo mysql <<EOF
DROP DATABASE IF EXISTS fuse_test;
DROP USER IF EXISTS 'fuse_test'@'localhost';
DROP USER IF EXISTS 'fuse_test'@'%';
FLUSH PRIVILEGES;
EOF

echo ""
echo "=== Teardown Complete ==="
