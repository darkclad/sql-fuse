#!/bin/bash
# SQLite Test Database Teardown Script
# Removes the test database file

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DB_FILE="${SCRIPT_DIR}/fuse_test.db"

if [ -f "${DB_FILE}" ]; then
    echo "Removing SQLite test database: ${DB_FILE}"
    rm -f "${DB_FILE}"
    echo "Done."
else
    echo "Test database not found: ${DB_FILE}"
fi
