#!/bin/bash
# PostgreSQL FUSE Integration Test Script
# Tests the sql-fuse filesystem with PostgreSQL backend

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build"
MOUNT_POINT="/tmp/sql-fuse-test-pg"
PG_HOST="${PGHOST:-localhost}"
PG_PORT="${PGPORT:-5432}"
PG_USER="${PGUSER:-fuse_test}"
PG_PASSWORD="${PGPASSWORD:-fuse_test_password}"
PG_DATABASE="${PGDATABASE:-fuse_test}"

# Test counters
TESTS_PASSED=0
TESTS_FAILED=0

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

test_pass() {
    echo -e "${GREEN}[PASS]${NC} $1"
    ((TESTS_PASSED++)) || true
}

test_fail() {
    echo -e "${RED}[FAIL]${NC} $1"
    ((TESTS_FAILED++)) || true
}

cleanup() {
    log_info "Cleaning up..."
    if mountpoint -q "$MOUNT_POINT" 2>/dev/null; then
        fusermount3 -u "$MOUNT_POINT" 2>/dev/null || fusermount -u "$MOUNT_POINT" 2>/dev/null || true
    fi
    rmdir "$MOUNT_POINT" 2>/dev/null || true
}

trap cleanup EXIT

# Check prerequisites
check_prerequisites() {
    log_info "Checking prerequisites..."

    # Check if sql-fuse binary exists
    if [[ ! -x "$BUILD_DIR/sql-fuse" ]]; then
        log_error "sql-fuse binary not found at $BUILD_DIR/sql-fuse"
        log_error "Please build the project first: cd $PROJECT_DIR && mkdir -p build && cd build && cmake .. && make"
        exit 1
    fi

    # Check if PostgreSQL is accessible
    if ! PGPASSWORD="$PG_PASSWORD" psql -h "$PG_HOST" -p "$PG_PORT" -U "$PG_USER" -d "$PG_DATABASE" -c "SELECT 1" &>/dev/null; then
        log_error "Cannot connect to PostgreSQL server"
        log_error "Please ensure PostgreSQL is running and the test database is set up"
        log_error "Run: psql -U postgres -f $SCRIPT_DIR/setup_test_db.sql"
        exit 1
    fi

    # Check if fuse3 is available
    if ! command -v fusermount3 &>/dev/null && ! command -v fusermount &>/dev/null; then
        log_error "fusermount not found. Please install fuse3"
        exit 1
    fi

    log_info "Prerequisites check passed"
}

# Set up test environment
setup() {
    log_info "Setting up test environment..."

    # Create mount point
    mkdir -p "$MOUNT_POINT"

    # Mount the filesystem
    log_info "Mounting sql-fuse at $MOUNT_POINT..."
    PGPASSWORD="$PG_PASSWORD" "$BUILD_DIR/sql-fuse" \
        -t postgresql \
        -H "$PG_HOST" \
        -P "$PG_PORT" \
        -u "$PG_USER" \
        -D "$PG_DATABASE" \
        "$MOUNT_POINT" &

    # Wait for mount
    sleep 2

    if ! mountpoint -q "$MOUNT_POINT"; then
        log_error "Failed to mount sql-fuse filesystem"
        exit 1
    fi

    log_info "Filesystem mounted successfully"
}

# Test cases
test_root_listing() {
    log_info "Testing root directory listing..."

    if ls "$MOUNT_POINT" &>/dev/null; then
        test_pass "Root directory listing"
    else
        test_fail "Root directory listing"
        return
    fi

    # Check for database directory (in PostgreSQL, we connect to a specific database)
    # The current database should appear as the only database
    if [[ -d "$MOUNT_POINT/$PG_DATABASE" ]] || ls "$MOUNT_POINT" | grep -q "."; then
        test_pass "Database directory exists or root has content"
    else
        test_fail "No content in root directory"
    fi
}

test_database_structure() {
    log_info "Testing database structure..."

    # For PostgreSQL, find the database directory
    DB_DIR=""
    for dir in "$MOUNT_POINT"/*/; do
        if [[ -d "$dir" && ! "$dir" =~ ^\. ]]; then
            DB_DIR="${dir%/}"
            break
        fi
    done

    if [[ -z "$DB_DIR" ]]; then
        # Try the connected database name
        DB_DIR="$MOUNT_POINT/fuse_test"
    fi

    if [[ -d "$DB_DIR/tables" ]]; then
        test_pass "Tables directory exists"
    else
        log_warn "Tables directory not found at $DB_DIR/tables"
        # List what we have
        ls -la "$MOUNT_POINT/" 2>/dev/null || true
        test_fail "Tables directory exists"
    fi

    if [[ -d "$DB_DIR/views" ]]; then
        test_pass "Views directory exists"
    else
        test_fail "Views directory exists"
    fi

    if [[ -d "$DB_DIR/functions" ]]; then
        test_pass "Functions directory exists"
    else
        test_fail "Functions directory exists"
    fi

    if [[ -d "$DB_DIR/triggers" ]]; then
        test_pass "Triggers directory exists"
    else
        test_fail "Triggers directory exists"
    fi
}

test_tables_listing() {
    log_info "Testing tables listing..."

    # Find database directory
    DB_DIR=""
    for dir in "$MOUNT_POINT"/*/; do
        if [[ -d "${dir}tables" ]]; then
            DB_DIR="${dir%/}"
            break
        fi
    done

    if [[ -z "$DB_DIR" ]]; then
        DB_DIR="$MOUNT_POINT/fuse_test"
    fi

    TABLES_DIR="$DB_DIR/tables"

    if [[ ! -d "$TABLES_DIR" ]]; then
        test_fail "Tables directory access"
        return
    fi

    # Check for expected tables
    if ls "$TABLES_DIR" | grep -q "users"; then
        test_pass "Users table found"
    else
        test_fail "Users table found"
    fi

    if ls "$TABLES_DIR" | grep -q "products"; then
        test_pass "Products table found"
    else
        test_fail "Products table found"
    fi

    if ls "$TABLES_DIR" | grep -q "orders"; then
        test_pass "Orders table found"
    else
        test_fail "Orders table found"
    fi
}

test_table_files() {
    log_info "Testing table file formats..."

    # Find database directory
    DB_DIR=""
    for dir in "$MOUNT_POINT"/*/; do
        if [[ -d "${dir}tables" ]]; then
            DB_DIR="${dir%/}"
            break
        fi
    done

    if [[ -z "$DB_DIR" ]]; then
        DB_DIR="$MOUNT_POINT/fuse_test"
    fi

    TABLES_DIR="$DB_DIR/tables"

    # Test CSV file
    if [[ -f "$TABLES_DIR/users.csv" ]]; then
        if head -1 "$TABLES_DIR/users.csv" | grep -q "id"; then
            test_pass "Users CSV file readable with headers"
        else
            test_fail "Users CSV file headers"
        fi
    else
        test_fail "Users CSV file exists"
    fi

    # Test JSON file
    if [[ -f "$TABLES_DIR/users.json" ]]; then
        if cat "$TABLES_DIR/users.json" | head -c 100 | grep -qE '^\[|\{'; then
            test_pass "Users JSON file readable"
        else
            test_fail "Users JSON file format"
        fi
    else
        test_fail "Users JSON file exists"
    fi

    # Test SQL file (CREATE statement)
    if [[ -f "$TABLES_DIR/users.sql" ]]; then
        if cat "$TABLES_DIR/users.sql" | grep -qi "CREATE"; then
            test_pass "Users SQL file contains CREATE statement"
        else
            test_fail "Users SQL file content"
        fi
    else
        test_fail "Users SQL file exists"
    fi
}

test_table_directory() {
    log_info "Testing table directory structure..."

    # Find database directory
    DB_DIR=""
    for dir in "$MOUNT_POINT"/*/; do
        if [[ -d "${dir}tables" ]]; then
            DB_DIR="${dir%/}"
            break
        fi
    done

    if [[ -z "$DB_DIR" ]]; then
        DB_DIR="$MOUNT_POINT/fuse_test"
    fi

    TABLE_DIR="$DB_DIR/tables/users"

    if [[ -d "$TABLE_DIR" ]]; then
        test_pass "Table directory exists"
    else
        test_fail "Table directory exists"
        return
    fi

    # Check for schema file
    if [[ -f "$TABLE_DIR/.schema" ]]; then
        test_pass "Schema file exists"
    else
        test_fail "Schema file exists"
    fi

    # Check for rows directory
    if [[ -d "$TABLE_DIR/rows" ]]; then
        test_pass "Rows directory exists"
    else
        test_fail "Rows directory exists"
    fi
}

test_row_access() {
    log_info "Testing individual row access..."

    # Find database directory
    DB_DIR=""
    for dir in "$MOUNT_POINT"/*/; do
        if [[ -d "${dir}tables" ]]; then
            DB_DIR="${dir%/}"
            break
        fi
    done

    if [[ -z "$DB_DIR" ]]; then
        DB_DIR="$MOUNT_POINT/fuse_test"
    fi

    ROWS_DIR="$DB_DIR/tables/users/rows"

    if [[ ! -d "$ROWS_DIR" ]]; then
        test_fail "Rows directory access"
        return
    fi

    # Check if row files exist
    ROW_COUNT=$(ls "$ROWS_DIR" 2>/dev/null | wc -l)
    if [[ $ROW_COUNT -gt 0 ]]; then
        test_pass "Row files present ($ROW_COUNT rows)"

        # Try to read the first row
        FIRST_ROW=$(ls "$ROWS_DIR" | head -1)
        if [[ -n "$FIRST_ROW" ]] && cat "$ROWS_DIR/$FIRST_ROW" | grep -q "username"; then
            test_pass "Row content readable"
        else
            test_fail "Row content readable"
        fi
    else
        test_fail "Row files present"
    fi
}

test_views() {
    log_info "Testing views..."

    # Find database directory
    DB_DIR=""
    for dir in "$MOUNT_POINT"/*/; do
        if [[ -d "${dir}views" ]]; then
            DB_DIR="${dir%/}"
            break
        fi
    done

    if [[ -z "$DB_DIR" ]]; then
        DB_DIR="$MOUNT_POINT/fuse_test"
    fi

    VIEWS_DIR="$DB_DIR/views"

    if [[ ! -d "$VIEWS_DIR" ]]; then
        test_fail "Views directory access"
        return
    fi

    # Check for expected views
    if ls "$VIEWS_DIR" 2>/dev/null | grep -q "v_active_users"; then
        test_pass "v_active_users view found"
    else
        test_fail "v_active_users view found"
    fi

    # Test view file
    if [[ -f "$VIEWS_DIR/v_active_users.csv" ]]; then
        if cat "$VIEWS_DIR/v_active_users.csv" | head -1 | grep -q "id\|username"; then
            test_pass "View CSV readable"
        else
            test_fail "View CSV content"
        fi
    else
        test_fail "View CSV file exists"
    fi
}

test_special_files() {
    log_info "Testing special files..."

    # Test server info
    if [[ -f "$MOUNT_POINT/.server_info" ]]; then
        if cat "$MOUNT_POINT/.server_info" | grep -qi "PostgreSQL\|version"; then
            test_pass "Server info file readable"
        else
            test_fail "Server info content"
        fi
    else
        test_fail "Server info file exists"
    fi

    # Test database info
    DB_DIR=""
    for dir in "$MOUNT_POINT"/*/; do
        if [[ -d "${dir}tables" ]]; then
            DB_DIR="${dir%/}"
            break
        fi
    done

    if [[ -n "$DB_DIR" && -f "$DB_DIR/.info" ]]; then
        if cat "$DB_DIR/.info" | grep -qi "database\|encoding"; then
            test_pass "Database info file readable"
        else
            test_fail "Database info content"
        fi
    else
        test_fail "Database info file exists"
    fi
}

test_data_integrity() {
    log_info "Testing data integrity..."

    # Find database directory
    DB_DIR=""
    for dir in "$MOUNT_POINT"/*/; do
        if [[ -d "${dir}tables" ]]; then
            DB_DIR="${dir%/}"
            break
        fi
    done

    if [[ -z "$DB_DIR" ]]; then
        DB_DIR="$MOUNT_POINT/fuse_test"
    fi

    # Compare row count from FUSE with actual database
    FUSE_COUNT=$(wc -l < "$DB_DIR/tables/users.csv" 2>/dev/null | tr -d ' ')
    # Subtract 1 for header
    FUSE_COUNT=$((FUSE_COUNT - 1))

    DB_COUNT=$(PGPASSWORD="$PG_PASSWORD" psql -h "$PG_HOST" -p "$PG_PORT" -U "$PG_USER" -d "$PG_DATABASE" \
        -t -c "SELECT COUNT(*) FROM users" 2>/dev/null | tr -d ' ')

    if [[ "$FUSE_COUNT" -eq "$DB_COUNT" ]]; then
        test_pass "Row count matches ($FUSE_COUNT rows)"
    else
        test_fail "Row count mismatch (FUSE: $FUSE_COUNT, DB: $DB_COUNT)"
    fi

    # Check specific data value
    ADMIN_EMAIL=$(PGPASSWORD="$PG_PASSWORD" psql -h "$PG_HOST" -p "$PG_PORT" -U "$PG_USER" -d "$PG_DATABASE" \
        -t -c "SELECT email FROM users WHERE username='admin'" 2>/dev/null | tr -d ' ')

    if cat "$DB_DIR/tables/users.csv" | grep -q "$ADMIN_EMAIL"; then
        test_pass "Specific data value found"
    else
        test_fail "Specific data value not found"
    fi
}

# Main test execution
main() {
    echo "========================================"
    echo "PostgreSQL FUSE Integration Tests"
    echo "========================================"
    echo ""

    check_prerequisites
    setup

    echo ""
    echo "Running tests..."
    echo ""

    test_root_listing
    test_database_structure
    test_tables_listing
    test_table_files
    test_table_directory
    test_row_access
    test_views
    test_special_files
    test_data_integrity

    echo ""
    echo "========================================"
    echo "Test Results"
    echo "========================================"
    echo -e "Passed: ${GREEN}$TESTS_PASSED${NC}"
    echo -e "Failed: ${RED}$TESTS_FAILED${NC}"
    echo ""

    if [[ $TESTS_FAILED -gt 0 ]]; then
        log_error "Some tests failed!"
        exit 1
    else
        log_info "All tests passed!"
        exit 0
    fi
}

main "$@"
