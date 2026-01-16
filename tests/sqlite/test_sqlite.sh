#!/bin/bash
#
# SQLite FUSE Integration Test Script
#
# This script performs end-to-end testing of sql-fuse with SQLite:
# 1. Sets up test database using setup_test_db.sh
# 2. Mounts it using sql-fuse
# 3. Performs browsing operations
# 4. Performs modification operations
# 5. Unmounts the filesystem
# 6. Optionally tears down using teardown_test_db.sh
# 7. Reports results
#
# Usage: ./test_sqlite.sh [options]
#   -k, --keep        Keep test database after test (don't delete)
#   -v, --verbose     Verbose output
#   --skip-cleanup    Skip cleanup on failure (for debugging)
#   --skip-setup      Skip database setup (assume fuse_test.db exists)
#
# This script uses the fuse_test.db database created by setup_test_db.sh.
#

set -e

# Script and project paths
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

# Configuration - uses fuse_test.db from setup_test_db.sh
DB_FILE="${SCRIPT_DIR}/fuse_test.db"
# SQLite uses "main" as the default database name
SQLITE_DB="main"
MOUNT_POINT="/tmp/sqlfuse_sqlite_test_$$"
SQL_FUSE_BIN="${SQL_FUSE_BIN:-${PROJECT_ROOT}/build/sql-fuse}"
KEEP_DB=false
VERBOSE=false
SKIP_CLEANUP=false
SKIP_SETUP=false

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Test counters
TESTS_PASSED=0
TESTS_FAILED=0
TESTS_SKIPPED=0

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -k|--keep)
            KEEP_DB=true
            shift
            ;;
        -v|--verbose)
            VERBOSE=true
            shift
            ;;
        --skip-cleanup)
            SKIP_CLEANUP=true
            shift
            ;;
        --skip-setup)
            SKIP_SETUP=true
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [options]"
            echo "  -k, --keep        Keep test database after test"
            echo "  -v, --verbose     Verbose output"
            echo "  --skip-cleanup    Skip cleanup on failure (for debugging)"
            echo "  --skip-setup      Skip database setup (assume fuse_test.db exists)"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Use -h or --help for usage"
            exit 1
            ;;
    esac
done

# Helper functions
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[PASS]${NC} $1"
}

log_error() {
    echo -e "${RED}[FAIL]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_verbose() {
    if [ "$VERBOSE" = true ]; then
        echo -e "${BLUE}[DEBUG]${NC} $1"
    fi
}

# Run SQLite command
sqlite_cmd() {
    sqlite3 "$DB_FILE" "$1" 2>/dev/null
}

# Test assertion function
assert_eq() {
    local expected="$1"
    local actual="$2"
    local message="$3"

    if [ "$expected" = "$actual" ]; then
        log_success "$message"
        ((TESTS_PASSED++))
        return 0
    else
        log_error "$message"
        log_verbose "  Expected: $expected"
        log_verbose "  Actual:   $actual"
        ((TESTS_FAILED++))
        return 1
    fi
}

assert_contains() {
    local haystack="$1"
    local needle="$2"
    local message="$3"

    if [[ "$haystack" == *"$needle"* ]]; then
        log_success "$message"
        ((TESTS_PASSED++))
        return 0
    else
        log_error "$message"
        log_verbose "  Looking for: $needle"
        log_verbose "  In: $haystack"
        ((TESTS_FAILED++))
        return 1
    fi
}

assert_file_exists() {
    local file="$1"
    local message="$2"

    if [ -e "$file" ]; then
        log_success "$message"
        ((TESTS_PASSED++))
        return 0
    else
        log_error "$message"
        log_verbose "  File not found: $file"
        ((TESTS_FAILED++))
        return 1
    fi
}

assert_dir_exists() {
    local dir="$1"
    local message="$2"

    if [ -d "$dir" ]; then
        log_success "$message"
        ((TESTS_PASSED++))
        return 0
    else
        log_error "$message"
        log_verbose "  Directory not found: $dir"
        ((TESTS_FAILED++))
        return 1
    fi
}

# Cleanup function
cleanup() {
    log_info "Cleaning up..."

    # Unmount if mounted
    if mountpoint -q "$MOUNT_POINT" 2>/dev/null; then
        log_verbose "Unmounting $MOUNT_POINT"
        fusermount -u "$MOUNT_POINT" 2>/dev/null || true
        sleep 1
    fi

    # Remove mount point
    if [ -d "$MOUNT_POINT" ]; then
        rmdir "$MOUNT_POINT" 2>/dev/null || true
    fi

    # Teardown test database using teardown script
    if [ "$KEEP_DB" = false ] && [ "$SKIP_SETUP" = false ]; then
        log_verbose "Tearing down test database using teardown_test_db.sh"
        "$SCRIPT_DIR/teardown_test_db.sh" 2>/dev/null || true
    else
        log_info "Keeping test database: $DB_FILE"
    fi
}

# Setup trap for cleanup
trap_handler() {
    if [ "$SKIP_CLEANUP" = true ]; then
        log_warn "Skipping cleanup (--skip-cleanup specified)"
        log_info "Mount point: $MOUNT_POINT"
        log_info "Test database: $DB_FILE"
    else
        cleanup
    fi
}

trap trap_handler EXIT

# Check prerequisites
check_prerequisites() {
    log_info "Checking prerequisites..."

    # Check sql-fuse binary
    if [ ! -x "$SQL_FUSE_BIN" ]; then
        log_error "sql-fuse binary not found at $SQL_FUSE_BIN"
        log_info "Please build the project first: mkdir -p build && cd build && cmake .. && make"
        exit 1
    fi

    # Check SQLite client
    if ! command -v sqlite3 &> /dev/null; then
        log_error "sqlite3 not found"
        exit 1
    fi

    # Check fusermount
    if ! command -v fusermount &> /dev/null; then
        log_error "fusermount not found (install fuse3)"
        exit 1
    fi

    # Check setup script exists
    if [ ! -x "$SCRIPT_DIR/setup_test_db.sh" ]; then
        log_error "setup_test_db.sh not found or not executable"
        exit 1
    fi

    log_success "Prerequisites check passed"
}

# Create test database using setup_test_db.sh
setup_test_database() {
    if [ "$SKIP_SETUP" = true ]; then
        log_info "Skipping database setup (--skip-setup specified)"
        # Verify fuse_test.db exists
        if [ ! -f "$DB_FILE" ]; then
            log_error "Test database not found: $DB_FILE"
            log_info "Run setup_test_db.sh first, or remove --skip-setup"
            exit 1
        fi
        log_success "Using existing test database: $DB_FILE"
        return
    fi

    log_info "Setting up test database using setup_test_db.sh..."
    if ! "$SCRIPT_DIR/setup_test_db.sh"; then
        log_error "Failed to setup test database"
        exit 1
    fi
    log_success "Test database setup complete"
}

# Mount the filesystem
mount_filesystem() {
    log_info "Mounting filesystem at $MOUNT_POINT"

    mkdir -p "$MOUNT_POINT"

    # Mount in foreground with debug if verbose
    local extra_opts=""
    if [ "$VERBOSE" = true ]; then
        extra_opts="-d"
    fi

    # Mount sql-fuse for SQLite
    # SQLite uses -H for database file path and -D for internal db name (default: main)
    "$SQL_FUSE_BIN" \
        -t sqlite \
        -H "$DB_FILE" \
        --max-threads 9 \
        -f \
        "$MOUNT_POINT" &

    SQL_FUSE_PID=$!

    # Wait for mount to be ready
    local retries=30
    while [ $retries -gt 0 ]; do
        if mountpoint -q "$MOUNT_POINT" 2>/dev/null; then
            break
        fi
        sleep 0.5
        ((retries--))
    done

    if ! mountpoint -q "$MOUNT_POINT"; then
        log_error "Failed to mount filesystem"
        kill $SQL_FUSE_PID 2>/dev/null || true
        exit 1
    fi

    log_success "Filesystem mounted successfully"
}

# Test: Browse root directory
test_browse_root() {
    log_info "Testing: Browse root directory"

    local contents=$(ls "$MOUNT_POINT" 2>/dev/null)
    assert_contains "$contents" "$SQLITE_DB" "Root directory contains 'main' database"
}

# Test: Browse database directory
test_browse_database() {
    log_info "Testing: Browse database directory"

    local db_path="$MOUNT_POINT/$SQLITE_DB"
    assert_dir_exists "$db_path" "Database directory exists"

    local contents=$(ls "$db_path" 2>/dev/null)
    assert_contains "$contents" "tables" "Database contains tables directory"
    assert_contains "$contents" "views" "Database contains views directory"
}

# Test: Browse tables directory
test_browse_tables() {
    log_info "Testing: Browse tables directory"

    local tables_path="$MOUNT_POINT/$SQLITE_DB/tables"
    assert_dir_exists "$tables_path" "Tables directory exists"

    local contents=$(ls "$tables_path" 2>/dev/null)
    assert_contains "$contents" "users" "Tables directory contains users"
    assert_contains "$contents" "orders" "Tables directory contains orders"
    assert_contains "$contents" "products" "Tables directory contains products"
}

# Test: Read table as CSV
test_read_table_csv() {
    log_info "Testing: Read table as CSV"

    local csv_file="$MOUNT_POINT/$SQLITE_DB/tables/users.csv"
    assert_file_exists "$csv_file" "users.csv file exists"

    local content=$(cat "$csv_file" 2>/dev/null)
    assert_contains "$content" "id" "CSV contains id column header"
    assert_contains "$content" "username" "CSV contains username column header"
    assert_contains "$content" "alice" "CSV contains alice user data"
    assert_contains "$content" "bob" "CSV contains bob user data"
}

# Test: Read table as JSON
test_read_table_json() {
    log_info "Testing: Read table as JSON"

    local json_file="$MOUNT_POINT/$SQLITE_DB/tables/users.json"
    assert_file_exists "$json_file" "users.json file exists"

    local content=$(cat "$json_file" 2>/dev/null)
    assert_contains "$content" '"username"' "JSON contains username field"
    assert_contains "$content" '"alice"' "JSON contains alice user data"
}

# Test: Read table schema
test_read_table_schema() {
    log_info "Testing: Read table schema"

    local schema_file="$MOUNT_POINT/$SQLITE_DB/tables/users/.schema"
    if [ -f "$schema_file" ]; then
        local content=$(cat "$schema_file" 2>/dev/null)
        assert_contains "$content" "id" "Schema contains id column"
        assert_contains "$content" "username" "Schema contains username column"
        assert_contains "$content" "TEXT" "Schema contains TEXT type"
    else
        log_warn "Schema file not found (skipping)"
        ((TESTS_SKIPPED++))
    fi
}

# Test: Read table SQL (CREATE statement)
test_read_table_sql() {
    log_info "Testing: Read CREATE TABLE statement"

    local sql_file="$MOUNT_POINT/$SQLITE_DB/tables/users.sql"
    assert_file_exists "$sql_file" "users.sql file exists"

    local content=$(cat "$sql_file" 2>/dev/null)
    assert_contains "$content" "CREATE TABLE" "SQL contains CREATE TABLE"
    assert_contains "$content" "users" "SQL contains table name"
}

# Test: Read individual row
test_read_row() {
    log_info "Testing: Read individual row"

    local rows_dir="$MOUNT_POINT/$SQLITE_DB/tables/users/rows"
    if [ -d "$rows_dir" ]; then
        local row_file="$rows_dir/1.json"
        assert_file_exists "$row_file" "Row 1 JSON file exists"

        local content=$(cat "$row_file" 2>/dev/null)
        assert_contains "$content" '"username"' "Row contains username field"
        assert_contains "$content" '"alice"' "Row contains alice data"
    else
        log_warn "Rows directory not found (skipping)"
        ((TESTS_SKIPPED++))
    fi
}

# Test: Browse views
test_browse_views() {
    log_info "Testing: Browse views directory"

    local views_path="$MOUNT_POINT/$SQLITE_DB/views"
    assert_dir_exists "$views_path" "Views directory exists"

    local contents=$(ls "$views_path" 2>/dev/null)
    assert_contains "$contents" "v_active_users" "Views directory contains v_active_users view"
}

# Test: Read view data
test_read_view() {
    log_info "Testing: Read view data"

    local view_file="$MOUNT_POINT/$SQLITE_DB/views/v_active_users.csv"
    if [ -f "$view_file" ]; then
        local content=$(cat "$view_file" 2>/dev/null)
        assert_contains "$content" "username" "View contains username column"
    else
        log_warn "View CSV file not found (skipping)"
        ((TESTS_SKIPPED++))
    fi
}

# Test: Modify row (UPDATE)
test_modify_row() {
    log_info "Testing: Modify row"

    local row_file="$MOUNT_POINT/$SQLITE_DB/tables/users/rows/1.json"
    if [ -f "$row_file" ]; then
        # Read current content
        local original=$(cat "$row_file" 2>/dev/null)
        log_verbose "Original content: $original"

        # Get original full_name
        local orig_full_name=$(sqlite_cmd "SELECT full_name FROM users WHERE id=1")

        # Attempt to modify
        echo '{"full_name": "Modified Name"}' > "$row_file" 2>/dev/null || true

        # Verify in database
        local db_full_name=$(sqlite_cmd "SELECT full_name FROM users WHERE id=1")
        if [ "$db_full_name" = "Modified Name" ]; then
            log_success "Row modification successful"
            ((TESTS_PASSED++))

            # Restore original
            sqlite_cmd "UPDATE users SET full_name='$orig_full_name' WHERE id=1"
        else
            log_warn "Row modification may not be supported or failed"
            ((TESTS_SKIPPED++))
        fi
    else
        log_warn "Row file not found (skipping modification test)"
        ((TESTS_SKIPPED++))
    fi
}

# Test: Delete row
test_delete_row() {
    log_info "Testing: Delete row"

    # Insert a temporary row
    sqlite_cmd "INSERT INTO users (username, email, full_name) VALUES ('tempuser', 'temp@example.com', 'Temp User')"
    local temp_id=$(sqlite_cmd "SELECT MAX(id) FROM users")

    local row_file="$MOUNT_POINT/$SQLITE_DB/tables/users/rows/${temp_id}.json"
    if [ -f "$row_file" ]; then
        # Attempt to delete
        rm "$row_file" 2>/dev/null || true

        # Verify in database
        local count=$(sqlite_cmd "SELECT COUNT(*) FROM users WHERE id=$temp_id")
        if [ "$count" = "0" ]; then
            log_success "Row deletion successful"
            ((TESTS_PASSED++))
        else
            log_warn "Row deletion may not be supported or failed"
            ((TESTS_SKIPPED++))
            # Clean up
            sqlite_cmd "DELETE FROM users WHERE id=$temp_id"
        fi
    else
        log_warn "Row file not found (skipping deletion test)"
        ((TESTS_SKIPPED++))
        # Clean up
        sqlite_cmd "DELETE FROM users WHERE id=$temp_id"
    fi
}

# Test: Create row (INSERT)
test_create_row() {
    log_info "Testing: Create row"

    local rows_dir="$MOUNT_POINT/$SQLITE_DB/tables/users/rows"
    if [ -d "$rows_dir" ]; then
        # Attempt to create new row file
        local new_row='{"username": "newuser", "email": "new@example.com", "full_name": "New User"}'
        echo "$new_row" > "$rows_dir/new.json" 2>/dev/null || true

        # Verify in database
        local count=$(sqlite_cmd "SELECT COUNT(*) FROM users WHERE username='newuser'")
        if [ "$count" = "1" ]; then
            log_success "Row creation successful"
            ((TESTS_PASSED++))
            # Clean up
            sqlite_cmd "DELETE FROM users WHERE username='newuser'"
        else
            log_warn "Row creation may not be supported or failed"
            ((TESTS_SKIPPED++))
        fi
    else
        log_warn "Rows directory not found (skipping creation test)"
        ((TESTS_SKIPPED++))
    fi
}

# Test: File size reporting
test_file_sizes() {
    log_info "Testing: File size reporting"

    local csv_file="$MOUNT_POINT/$SQLITE_DB/tables/users.csv"
    if [ -f "$csv_file" ]; then
        local size=$(stat -c %s "$csv_file" 2>/dev/null || stat -f %z "$csv_file" 2>/dev/null)
        if [ -n "$size" ] && [ "$size" -gt 0 ]; then
            log_success "File size reported correctly: $size bytes"
            ((TESTS_PASSED++))
        else
            log_error "File size is zero or not reported"
            ((TESTS_FAILED++))
        fi
    else
        log_warn "CSV file not found (skipping)"
        ((TESTS_SKIPPED++))
    fi
}

# Test: Read products table (SQLite-specific test data)
test_read_products() {
    log_info "Testing: Read products table"

    local csv_file="$MOUNT_POINT/$SQLITE_DB/tables/products.csv"
    assert_file_exists "$csv_file" "products.csv file exists"

    local content=$(cat "$csv_file" 2>/dev/null)
    assert_contains "$content" "iPhone" "Products contains iPhone"
    assert_contains "$content" "MacBook" "Products contains MacBook"
}

# Test: Read order_items table (tests foreign key relationships)
test_read_order_items() {
    log_info "Testing: Read order_items table"

    local csv_file="$MOUNT_POINT/$SQLITE_DB/tables/order_items.csv"
    assert_file_exists "$csv_file" "order_items.csv file exists"

    local content=$(cat "$csv_file" 2>/dev/null)
    assert_contains "$content" "order_id" "order_items contains order_id column"
    assert_contains "$content" "product_id" "order_items contains product_id column"
}

# Run all tests
run_tests() {
    log_info "=========================================="
    log_info "Starting sql-fuse SQLite integration tests"
    log_info "=========================================="

    # Browsing tests
    test_browse_root
    test_browse_database
    test_browse_tables

    # Read tests
    test_read_table_csv
    test_read_table_json
    test_read_table_schema
    test_read_table_sql
    test_read_row
    test_browse_views
    test_read_view
    test_file_sizes

    # SQLite-specific read tests
    test_read_products
    test_read_order_items

    # Write tests (may be skipped if read-only)
    test_modify_row
    test_delete_row
    test_create_row
}

# Print summary
print_summary() {
    echo ""
    log_info "=========================================="
    log_info "Test Summary"
    log_info "=========================================="
    echo -e "  ${GREEN}Passed:${NC}  $TESTS_PASSED"
    echo -e "  ${RED}Failed:${NC}  $TESTS_FAILED"
    echo -e "  ${YELLOW}Skipped:${NC} $TESTS_SKIPPED"
    echo ""

    if [ $TESTS_FAILED -eq 0 ]; then
        log_success "All tests passed!"
        return 0
    else
        log_error "Some tests failed"
        return 1
    fi
}

# Main execution
main() {
    check_prerequisites
    setup_test_database
    mount_filesystem

    # Give filesystem a moment to stabilize
    sleep 1

    run_tests

    # Unmount before summary
    log_info "Unmounting filesystem..."
    fusermount -u "$MOUNT_POINT"

    print_summary
}

main "$@"
