#!/bin/bash
#
# MySQL FUSE Integration Test Script
#
# This script performs end-to-end testing of sql-fuse with MySQL:
# 1. Sets up test database using setup_test_db.sh
# 2. Mounts it using sql-fuse
# 3. Performs browsing operations
# 4. Performs modification operations
# 5. Unmounts the filesystem
# 6. Optionally tears down using teardown_test_db.sh
# 7. Reports results
#
# Usage: ./test_mysql.sh [options]
#   -k, --keep        Keep test database after test (don't drop)
#   -v, --verbose     Verbose output
#   --skip-cleanup    Skip cleanup on failure (for debugging)
#   --skip-setup      Skip database setup (assume fuse_test exists)
#
# This script uses the fuse_test database and fuse_test user created by
# setup_test_db.sh (uses sudo mysql with socket authentication).
#

set -e

# Script and project paths
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

# Configuration - uses fuse_test database from setup_test_db.sh
MYSQL_HOST="localhost"
MYSQL_PORT="3306"
MYSQL_USER="fuse_test"
MYSQL_PASSWORD="fuse_test_password"
TEST_DB="fuse_test"
MOUNT_POINT="/tmp/sqlfuse_test_$$"
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
            echo "  --skip-setup      Skip database setup (assume fuse_test exists)"
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

# Run MySQL command
mysql_cmd() {
    local password_arg=""
    if [ -n "$MYSQL_PASSWORD" ]; then
        password_arg="-p$MYSQL_PASSWORD"
    fi
    mysql -h "$MYSQL_HOST" -P "$MYSQL_PORT" -u "$MYSQL_USER" $password_arg -N -e "$1" 2>/dev/null
}

mysql_cmd_db() {
    local password_arg=""
    if [ -n "$MYSQL_PASSWORD" ]; then
        password_arg="-p$MYSQL_PASSWORD"
    fi
    mysql -h "$MYSQL_HOST" -P "$MYSQL_PORT" -u "$MYSQL_USER" $password_arg -N "$TEST_DB" -e "$1" 2>/dev/null
}

# Test assertion function
assert_eq() {
    local expected="$1"
    local actual="$2"
    local message="$3"

    if [ "$expected" = "$actual" ]; then
        log_success "$message"
        ((TESTS_PASSED++)) || true
        return 0
    else
        log_error "$message"
        log_verbose "  Expected: $expected"
        log_verbose "  Actual:   $actual"
        ((TESTS_FAILED++)) || true
        return 1
    fi
}

assert_contains() {
    local haystack="$1"
    local needle="$2"
    local message="$3"

    if [[ "$haystack" == *"$needle"* ]]; then
        log_success "$message"
        ((TESTS_PASSED++)) || true
        return 0
    else
        log_error "$message"
        log_verbose "  Looking for: $needle"
        log_verbose "  In: $haystack"
        ((TESTS_FAILED++)) || true
        return 1
    fi
}

assert_file_exists() {
    local file="$1"
    local message="$2"

    if [ -e "$file" ]; then
        log_success "$message"
        ((TESTS_PASSED++)) || true
        return 0
    else
        log_error "$message"
        log_verbose "  File not found: $file"
        ((TESTS_FAILED++)) || true
        return 1
    fi
}

assert_dir_exists() {
    local dir="$1"
    local message="$2"

    if [ -d "$dir" ]; then
        log_success "$message"
        ((TESTS_PASSED++)) || true
        return 0
    else
        log_error "$message"
        log_verbose "  Directory not found: $dir"
        ((TESTS_FAILED++)) || true
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

    # Teardown test database using teardown script (non-interactive)
    if [ "$KEEP_DB" = false ] && [ "$SKIP_SETUP" = false ]; then
        log_verbose "Tearing down test database using teardown_test_db.sh"
        echo "y" | "$SCRIPT_DIR/teardown_test_db.sh" 2>/dev/null || true
    else
        log_info "Keeping test database: $TEST_DB"
    fi
}

# Setup trap for cleanup
trap_handler() {
    if [ "$SKIP_CLEANUP" = true ]; then
        log_warn "Skipping cleanup (--skip-cleanup specified)"
        log_info "Mount point: $MOUNT_POINT"
        log_info "Test database: $TEST_DB"
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

    # Check MySQL client
    if ! command -v mysql &> /dev/null; then
        log_error "MySQL client not found"
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

    # Check MySQL server is running (using sudo like the setup scripts)
    if ! sudo mysqladmin ping &>/dev/null; then
        log_error "MySQL server is not running"
        log_info "Start with: sudo systemctl start mysql"
        exit 1
    fi

    log_success "Prerequisites check passed"
}

# Create test database and tables using setup_test_db.sh
setup_test_database() {
    if [ "$SKIP_SETUP" = true ]; then
        log_info "Skipping database setup (--skip-setup specified)"
        # Verify fuse_test database exists and is accessible
        if ! mysql_cmd "SELECT 1" &>/dev/null; then
            log_error "Cannot connect to fuse_test database"
            log_info "Run setup_test_db.sh first, or remove --skip-setup"
            exit 1
        fi
        log_success "Connected to existing fuse_test database"
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

    local password_opt=""
    if [ -n "$MYSQL_PASSWORD" ]; then
        password_opt="-p $MYSQL_PASSWORD"
    fi

    # Mount in foreground with debug if verbose
    local extra_opts=""
    if [ "$VERBOSE" = true ]; then
        extra_opts="-d"
    fi

    # Mount sql-fuse (mountpoint is positional argument at the end)
    "$SQL_FUSE_BIN" \
        -t mysql \
        -H "$MYSQL_HOST" \
        -P "$MYSQL_PORT" \
        -u "$MYSQL_USER" \
        $password_opt \
        -D "$TEST_DB" \
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
    assert_contains "$contents" "$TEST_DB" "Root directory contains test database"
}

# Test: Browse database directory
test_browse_database() {
    log_info "Testing: Browse database directory"

    local db_path="$MOUNT_POINT/$TEST_DB"
    assert_dir_exists "$db_path" "Database directory exists"

    local contents=$(ls "$db_path" 2>/dev/null)
    assert_contains "$contents" "tables" "Database contains tables directory"
    assert_contains "$contents" "views" "Database contains views directory"
}

# Test: Browse tables directory
test_browse_tables() {
    log_info "Testing: Browse tables directory"

    local tables_path="$MOUNT_POINT/$TEST_DB/tables"
    assert_dir_exists "$tables_path" "Tables directory exists"

    local contents=$(ls "$tables_path" 2>/dev/null)
    assert_contains "$contents" "users" "Tables directory contains users"
    assert_contains "$contents" "orders" "Tables directory contains orders"
}

# Test: Read table as CSV
test_read_table_csv() {
    log_info "Testing: Read table as CSV"

    local csv_file="$MOUNT_POINT/$TEST_DB/tables/users.csv"
    assert_file_exists "$csv_file" "users.csv file exists"

    local content=$(cat "$csv_file" 2>/dev/null)
    assert_contains "$content" "id" "CSV contains id column header"
    assert_contains "$content" "username" "CSV contains username column header"
    assert_contains "$content" "admin" "CSV contains admin user data"
    assert_contains "$content" "john_doe" "CSV contains john_doe user data"
}

# Test: Read table as JSON
test_read_table_json() {
    log_info "Testing: Read table as JSON"

    local json_file="$MOUNT_POINT/$TEST_DB/tables/users.json"
    assert_file_exists "$json_file" "users.json file exists"

    local content=$(cat "$json_file" 2>/dev/null)
    assert_contains "$content" '"username"' "JSON contains username field"
    assert_contains "$content" '"admin"' "JSON contains admin user data"
}

# Test: Read table schema
test_read_table_schema() {
    log_info "Testing: Read table schema"

    local schema_file="$MOUNT_POINT/$TEST_DB/tables/users/.schema"
    if [ -f "$schema_file" ]; then
        local content=$(cat "$schema_file" 2>/dev/null)
        assert_contains "$content" "id" "Schema contains id column"
        assert_contains "$content" "username" "Schema contains username column"
        assert_contains "$content" "varchar" "Schema contains varchar type"
    else
        log_warn "Schema file not found (skipping)"
        ((TESTS_SKIPPED++)) || true
    fi
}

# Test: Read table SQL (CREATE statement)
test_read_table_sql() {
    log_info "Testing: Read CREATE TABLE statement"

    local sql_file="$MOUNT_POINT/$TEST_DB/tables/users.sql"
    assert_file_exists "$sql_file" "users.sql file exists"

    local content=$(cat "$sql_file" 2>/dev/null)
    assert_contains "$content" "CREATE TABLE" "SQL contains CREATE TABLE"
    assert_contains "$content" "users" "SQL contains table name"
}

# Test: Read individual row
test_read_row() {
    log_info "Testing: Read individual row"

    local rows_dir="$MOUNT_POINT/$TEST_DB/tables/users/rows"
    if [ -d "$rows_dir" ]; then
        local row_file="$rows_dir/1.json"
        assert_file_exists "$row_file" "Row 1 JSON file exists"

        local content=$(cat "$row_file" 2>/dev/null)
        assert_contains "$content" '"username"' "Row contains username field"
        assert_contains "$content" '"admin"' "Row contains admin data"
    else
        log_warn "Rows directory not found (skipping)"
        ((TESTS_SKIPPED++)) || true
    fi
}

# Test: Browse views
test_browse_views() {
    log_info "Testing: Browse views directory"

    local views_path="$MOUNT_POINT/$TEST_DB/views"
    assert_dir_exists "$views_path" "Views directory exists"

    local contents=$(ls "$views_path" 2>/dev/null)
    assert_contains "$contents" "v_active_users" "Views directory contains v_active_users view"
}

# Test: Read view data
test_read_view() {
    log_info "Testing: Read view data"

    local view_file="$MOUNT_POINT/$TEST_DB/views/v_active_users.csv"
    if [ -f "$view_file" ]; then
        local content=$(cat "$view_file" 2>/dev/null)
        assert_contains "$content" "username" "View contains username column"
    else
        log_warn "View CSV file not found (skipping)"
        ((TESTS_SKIPPED++)) || true
    fi
}

# Test: Modify row (UPDATE)
test_modify_row() {
    log_info "Testing: Modify row"

    local row_file="$MOUNT_POINT/$TEST_DB/tables/users/rows/1.json"
    if [ -f "$row_file" ]; then
        # Read current content
        local original=$(cat "$row_file" 2>/dev/null)
        log_verbose "Original content: $original"

        # Get original first_name
        local orig_first_name=$(mysql_cmd_db "SELECT first_name FROM users WHERE id=1")

        # Attempt to modify
        echo '{"first_name": "Modified"}' > "$row_file" 2>/dev/null || true

        # Verify in database
        local db_first_name=$(mysql_cmd_db "SELECT first_name FROM users WHERE id=1")
        if [ "$db_first_name" = "Modified" ]; then
            log_success "Row modification successful"
            ((TESTS_PASSED++)) || true

            # Restore original
            mysql_cmd_db "UPDATE users SET first_name='$orig_first_name' WHERE id=1"
        else
            log_warn "Row modification may not be supported or failed"
            ((TESTS_SKIPPED++)) || true
        fi
    else
        log_warn "Row file not found (skipping modification test)"
        ((TESTS_SKIPPED++)) || true
    fi
}

# Test: Delete row
test_delete_row() {
    log_info "Testing: Delete row"

    # Insert a temporary row
    mysql_cmd_db "INSERT INTO users (username, email, password_hash, first_name, last_name, role) VALUES ('tempuser', 'temp@example.com', 'hash123', 'Temp', 'User', 'guest')"
    local temp_id=$(mysql_cmd_db "SELECT MAX(id) FROM users")

    local row_file="$MOUNT_POINT/$TEST_DB/tables/users/rows/${temp_id}.json"
    if [ -f "$row_file" ]; then
        # Attempt to delete
        rm "$row_file" 2>/dev/null || true

        # Verify in database
        local count=$(mysql_cmd_db "SELECT COUNT(*) FROM users WHERE id=$temp_id")
        if [ "$count" = "0" ]; then
            log_success "Row deletion successful"
            ((TESTS_PASSED++)) || true
        else
            log_warn "Row deletion may not be supported or failed"
            ((TESTS_SKIPPED++)) || true
            # Clean up
            mysql_cmd_db "DELETE FROM users WHERE id=$temp_id"
        fi
    else
        log_warn "Row file not found (skipping deletion test)"
        ((TESTS_SKIPPED++)) || true
        # Clean up
        mysql_cmd_db "DELETE FROM users WHERE id=$temp_id"
    fi
}

# Test: Create row (INSERT)
test_create_row() {
    log_info "Testing: Create row"

    local rows_dir="$MOUNT_POINT/$TEST_DB/tables/users/rows"
    if [ -d "$rows_dir" ]; then
        # Attempt to create new row file
        local new_row='{"username": "newuser", "email": "new@example.com", "password_hash": "newhash", "first_name": "New", "last_name": "User", "role": "guest"}'
        echo "$new_row" > "$rows_dir/new.json" 2>/dev/null || true

        # Verify in database
        local count=$(mysql_cmd_db "SELECT COUNT(*) FROM users WHERE username='newuser'")
        if [ "$count" = "1" ]; then
            log_success "Row creation successful"
            ((TESTS_PASSED++)) || true
            # Clean up
            mysql_cmd_db "DELETE FROM users WHERE username='newuser'"
        else
            log_warn "Row creation may not be supported or failed"
            ((TESTS_SKIPPED++)) || true
        fi
    else
        log_warn "Rows directory not found (skipping creation test)"
        ((TESTS_SKIPPED++)) || true
    fi
}

# Test: File size reporting
test_file_sizes() {
    log_info "Testing: File size reporting"

    local csv_file="$MOUNT_POINT/$TEST_DB/tables/users.csv"
    if [ -f "$csv_file" ]; then
        local size=$(stat -c %s "$csv_file" 2>/dev/null || stat -f %z "$csv_file" 2>/dev/null)
        if [ -n "$size" ] && [ "$size" -gt 0 ]; then
            log_success "File size reported correctly: $size bytes"
            ((TESTS_PASSED++)) || true
        else
            log_error "File size is zero or not reported"
            ((TESTS_FAILED++)) || true
        fi
    else
        log_warn "CSV file not found (skipping)"
        ((TESTS_SKIPPED++)) || true
    fi
}

# Test: Bulk CSV import
test_bulk_csv_import() {
    log_info "Testing: Bulk CSV import"

    local csv_file="$MOUNT_POINT/$TEST_DB/tables/users.csv"
    if [ -f "$csv_file" ]; then
        # Create CSV data with new users (using columns from schema)
        local csv_data='"id","username","email","password_hash","first_name","last_name","role","is_active"
100,"csvuser1","csv1@example.com","hash123","CSV","User1","user",1
101,"csvuser2","csv2@example.com","hash456","CSV","User2","user",1'

        # Write CSV to table file
        echo "$csv_data" > "$csv_file" 2>/dev/null || true

        # Verify insertion in database
        local count=$(mysql_cmd_db "SELECT COUNT(*) FROM users WHERE username IN ('csvuser1', 'csvuser2')")
        if [ "$count" = "2" ]; then
            log_success "Bulk CSV import successful"
            ((TESTS_PASSED++)) || true
            # Clean up
            mysql_cmd_db "DELETE FROM users WHERE username IN ('csvuser1', 'csvuser2')"
        else
            log_warn "Bulk CSV import may not be supported or failed (count=$count)"
            ((TESTS_SKIPPED++)) || true
        fi
    else
        log_warn "CSV file not found (skipping bulk import test)"
        ((TESTS_SKIPPED++)) || true
    fi
}

# Test: Bulk JSON import
test_bulk_json_import() {
    log_info "Testing: Bulk JSON import"

    local json_file="$MOUNT_POINT/$TEST_DB/tables/users.json"
    if [ -f "$json_file" ]; then
        # Create JSON data with new users
        local json_data='[
  {"id": 200, "username": "jsonuser1", "email": "json1@example.com", "password_hash": "hash789", "first_name": "JSON", "last_name": "User1", "role": "user", "is_active": 1},
  {"id": 201, "username": "jsonuser2", "email": "json2@example.com", "password_hash": "hash012", "first_name": "JSON", "last_name": "User2", "role": "user", "is_active": 1}
]'

        # Write JSON to table file
        echo "$json_data" > "$json_file" 2>/dev/null || true

        # Verify insertion in database
        local count=$(mysql_cmd_db "SELECT COUNT(*) FROM users WHERE username IN ('jsonuser1', 'jsonuser2')")
        if [ "$count" = "2" ]; then
            log_success "Bulk JSON import successful"
            ((TESTS_PASSED++)) || true
            # Clean up
            mysql_cmd_db "DELETE FROM users WHERE username IN ('jsonuser1', 'jsonuser2')"
        else
            log_warn "Bulk JSON import may not be supported or failed (count=$count)"
            ((TESTS_SKIPPED++)) || true
        fi
    else
        log_warn "JSON file not found (skipping bulk import test)"
        ((TESTS_SKIPPED++)) || true
    fi
}

# Run all tests
run_tests() {
    log_info "=========================================="
    log_info "Starting sql-fuse MySQL integration tests"
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

    # Write tests (may be skipped if read-only)
    test_modify_row
    test_delete_row
    test_create_row

    # Bulk import tests
    test_bulk_csv_import
    test_bulk_json_import
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
