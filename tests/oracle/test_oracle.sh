#!/bin/bash

# Oracle backend test script for sql-fuse
# Usage: ./test_oracle.sh [oracle_host] [oracle_port] [service_name]

set -e

# Configuration
ORACLE_HOST="${1:-localhost}"
ORACLE_PORT="${2:-1521}"
ORACLE_SERVICE="${3:-XE}"
ORACLE_USER="sqlfuse_test"
ORACLE_PASS="testpass123"
MOUNT_POINT="/tmp/sql-fuse-oracle-test"
SQL_FUSE_BIN="${SQL_FUSE_BIN:-../../build/sql-fuse}"

# Set Oracle Instant Client library path
# Find the instantclient directory (version may vary)
ORACLE_INSTANT_CLIENT=$(ls -d /opt/oracle/instantclient_* 2>/dev/null | head -1)
if [ -n "$ORACLE_INSTANT_CLIENT" ]; then
    export LD_LIBRARY_PATH="${ORACLE_INSTANT_CLIENT}:${LD_LIBRARY_PATH}"
fi

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Test counters
TESTS_PASSED=0
TESTS_FAILED=0

# Cleanup function
cleanup() {
    echo "Cleaning up..."
    if mountpoint -q "$MOUNT_POINT" 2>/dev/null; then
        fusermount3 -u "$MOUNT_POINT" 2>/dev/null || fusermount -u "$MOUNT_POINT" 2>/dev/null || true
    fi
    rmdir "$MOUNT_POINT" 2>/dev/null || true
}

# Set trap for cleanup
trap cleanup EXIT

# Test function
run_test() {
    local test_name="$1"
    local test_cmd="$2"

    printf "  %-50s" "$test_name"

    if eval "$test_cmd" > /dev/null 2>&1; then
        echo -e "${GREEN}PASSED${NC}"
        ((TESTS_PASSED++)) || true
        return 0
    else
        echo -e "${RED}FAILED${NC}"
        ((TESTS_FAILED++)) || true
        return 1
    fi
}

# Test function with expected output
run_test_output() {
    local test_name="$1"
    local test_cmd="$2"
    local expected="$3"

    printf "  %-50s" "$test_name"

    local output
    output=$(eval "$test_cmd" 2>/dev/null)

    if echo "$output" | grep -q "$expected"; then
        echo -e "${GREEN}PASSED${NC}"
        ((TESTS_PASSED++)) || true
        return 0
    else
        echo -e "${RED}FAILED${NC}"
        echo "    Expected: $expected"
        echo "    Got: $output"
        ((TESTS_FAILED++)) || true
        return 1
    fi
}

echo "=================================================="
echo "SQL-FUSE Oracle Backend Tests"
echo "=================================================="
echo ""
echo "Configuration:"
echo "  Oracle Host: $ORACLE_HOST:$ORACLE_PORT/$ORACLE_SERVICE"
echo "  User: $ORACLE_USER"
echo "  Mount Point: $MOUNT_POINT"
echo ""

# Check if sql-fuse binary exists
if [ ! -x "$SQL_FUSE_BIN" ]; then
    echo -e "${RED}Error: sql-fuse binary not found at $SQL_FUSE_BIN${NC}"
    echo "Build the project first with: cmake -DWITH_ORACLE=ON .. && make"
    exit 1
fi

# Check if built with Oracle support
if ! "$SQL_FUSE_BIN" --help 2>&1 | grep -q "oracle"; then
    echo -e "${RED}Error: sql-fuse was not built with Oracle support${NC}"
    echo "Rebuild with: cmake -DWITH_ORACLE=ON .. && make"
    exit 1
fi

# Create mount point
mkdir -p "$MOUNT_POINT"

# Clean up any existing mount
if mountpoint -q "$MOUNT_POINT" 2>/dev/null; then
    fusermount3 -u "$MOUNT_POINT" 2>/dev/null || fusermount -u "$MOUNT_POINT" 2>/dev/null
fi

echo "Starting sql-fuse with Oracle backend..."
# Connection string format: host:port/service_name
CONN_STRING="$ORACLE_HOST:$ORACLE_PORT/$ORACLE_SERVICE"
"$SQL_FUSE_BIN" -t oracle -H "$CONN_STRING" -u "$ORACLE_USER" -p "$ORACLE_PASS" "$MOUNT_POINT" &
SQL_FUSE_PID=$!

# Wait for filesystem to mount
sleep 3

# Check if mounted
if ! mountpoint -q "$MOUNT_POINT" 2>/dev/null; then
    echo -e "${RED}Error: Failed to mount filesystem${NC}"
    exit 1
fi

echo -e "${GREEN}Filesystem mounted successfully${NC}"
echo ""

# ==========================================
# Phase 1: Directory Structure Tests
# ==========================================
echo "Phase 1: Directory Structure Tests"
echo "------------------------------------------"

run_test "Root directory exists" "[ -d '$MOUNT_POINT' ]"
run_test "Schema directory exists" "[ -d '$MOUNT_POINT/SQLFUSE_TEST' ]"
run_test "Tables directory exists" "[ -d '$MOUNT_POINT/SQLFUSE_TEST/tables' ]"
run_test "Views directory exists" "[ -d '$MOUNT_POINT/SQLFUSE_TEST/views' ]"
run_test "Procedures directory exists" "[ -d '$MOUNT_POINT/SQLFUSE_TEST/procedures' ]"
run_test "Functions directory exists" "[ -d '$MOUNT_POINT/SQLFUSE_TEST/functions' ]"
run_test "Triggers directory exists" "[ -d '$MOUNT_POINT/SQLFUSE_TEST/triggers' ]"

echo ""

# ==========================================
# Phase 2: Table Data Tests
# ==========================================
echo "Phase 2: Table Data Tests"
echo "------------------------------------------"

run_test "Employees table CSV exists" "[ -f '$MOUNT_POINT/SQLFUSE_TEST/tables/EMPLOYEES.csv' ]"
run_test "Employees table JSON exists" "[ -f '$MOUNT_POINT/SQLFUSE_TEST/tables/EMPLOYEES.json' ]"
run_test "Employees table SQL exists" "[ -f '$MOUNT_POINT/SQLFUSE_TEST/tables/EMPLOYEES.sql' ]"
run_test "Departments table exists" "[ -f '$MOUNT_POINT/SQLFUSE_TEST/tables/DEPARTMENTS.csv' ]"
run_test "Projects table exists" "[ -f '$MOUNT_POINT/SQLFUSE_TEST/tables/PROJECTS.csv' ]"

echo ""

# ==========================================
# Phase 3: CSV Content Tests
# ==========================================
echo "Phase 3: CSV Content Tests"
echo "------------------------------------------"

run_test_output "CSV has header row" "head -1 '$MOUNT_POINT/SQLFUSE_TEST/tables/EMPLOYEES.csv'" "ID"
run_test_output "CSV contains John Doe" "cat '$MOUNT_POINT/SQLFUSE_TEST/tables/EMPLOYEES.csv'" "John Doe"
run_test_output "CSV contains Jane Smith" "cat '$MOUNT_POINT/SQLFUSE_TEST/tables/EMPLOYEES.csv'" "Jane Smith"
run_test_output "Departments CSV contains Engineering" "cat '$MOUNT_POINT/SQLFUSE_TEST/tables/DEPARTMENTS.csv'" "Engineering"

echo ""

# ==========================================
# Phase 4: JSON Content Tests
# ==========================================
echo "Phase 4: JSON Content Tests"
echo "------------------------------------------"

run_test_output "JSON is valid array" "cat '$MOUNT_POINT/SQLFUSE_TEST/tables/EMPLOYEES.json'" "\\["
run_test_output "JSON contains employee data" "cat '$MOUNT_POINT/SQLFUSE_TEST/tables/EMPLOYEES.json'" "john.doe@example.com"
run_test_output "JSON contains salary" "cat '$MOUNT_POINT/SQLFUSE_TEST/tables/EMPLOYEES.json'" "85000"

echo ""

# ==========================================
# Phase 5: View Tests
# ==========================================
echo "Phase 5: View Tests"
echo "------------------------------------------"

run_test "Employee details view CSV exists" "[ -f '$MOUNT_POINT/SQLFUSE_TEST/views/V_EMPLOYEE_DETAILS.csv' ]"
run_test "Employee details view JSON exists" "[ -f '$MOUNT_POINT/SQLFUSE_TEST/views/V_EMPLOYEE_DETAILS.json' ]"
run_test_output "View contains joined data" "cat '$MOUNT_POINT/SQLFUSE_TEST/views/V_EMPLOYEE_DETAILS.csv'" "Building"

echo ""

# ==========================================
# Phase 6: Procedure/Function Tests
# ==========================================
echo "Phase 6: Procedure/Function Tests"
echo "------------------------------------------"

run_test "Procedure SQL file exists" "[ -f '$MOUNT_POINT/SQLFUSE_TEST/procedures/GET_EMPLOYEE_COUNT.sql' ]"
run_test "Function SQL file exists" "[ -f '$MOUNT_POINT/SQLFUSE_TEST/functions/GET_DEPARTMENT_BUDGET.sql' ]"
run_test_output "Procedure contains PL/SQL" "cat '$MOUNT_POINT/SQLFUSE_TEST/procedures/GET_EMPLOYEE_COUNT.sql'" "PROCEDURE"
run_test_output "Function contains RETURN" "cat '$MOUNT_POINT/SQLFUSE_TEST/functions/GET_DEPARTMENT_BUDGET.sql'" "RETURN"

echo ""

# ==========================================
# Phase 7: Trigger Tests
# ==========================================
echo "Phase 7: Trigger Tests"
echo "------------------------------------------"

run_test "Trigger SQL file exists" "[ -f '$MOUNT_POINT/SQLFUSE_TEST/triggers/TRG_EMPLOYEES_AUDIT.sql' ]"
run_test_output "Trigger contains trigger code" "cat '$MOUNT_POINT/SQLFUSE_TEST/triggers/TRG_EMPLOYEES_AUDIT.sql'" "TRIGGER"

echo ""

# ==========================================
# Phase 8: Row-Level Access Tests
# ==========================================
echo "Phase 8: Row-Level Access Tests"
echo "------------------------------------------"

run_test "Rows directory exists" "[ -d '$MOUNT_POINT/SQLFUSE_TEST/tables/EMPLOYEES/rows' ]"
run_test "Individual row file exists" "[ -f '$MOUNT_POINT/SQLFUSE_TEST/tables/EMPLOYEES/rows/1.json' ]"
run_test_output "Row JSON contains data" "cat '$MOUNT_POINT/SQLFUSE_TEST/tables/EMPLOYEES/rows/1.json'" "John Doe"

echo ""

# ==========================================
# Phase 9: Table Metadata Tests
# ==========================================
echo "Phase 9: Table Metadata Tests"
echo "------------------------------------------"

run_test "Table schema file exists" "[ -f '$MOUNT_POINT/SQLFUSE_TEST/tables/EMPLOYEES/.schema' ]"
run_test "Table indexes file exists" "[ -f '$MOUNT_POINT/SQLFUSE_TEST/tables/EMPLOYEES/.indexes' ]"
run_test_output "Schema shows column types" "cat '$MOUNT_POINT/SQLFUSE_TEST/tables/EMPLOYEES/.schema'" "VARCHAR2"

echo ""

# ==========================================
# Phase 10: Write Operation Tests
# ==========================================
echo "Phase 10: Write Operation Tests"
echo "------------------------------------------"

# Test INSERT via new row file
NEW_ROW='{"ID": 100, "NAME": "Test User", "EMAIL": "test@example.com", "DEPARTMENT": "Engineering", "SALARY": 70000}'
run_test "Create new row file" "echo '$NEW_ROW' > '$MOUNT_POINT/SQLFUSE_TEST/tables/EMPLOYEES/rows/100.json'"

# Wait for write to be flushed
sleep 2

# Verify the insert worked
run_test_output "New row exists in table" "cat '$MOUNT_POINT/SQLFUSE_TEST/tables/EMPLOYEES.json'" "Test User"

# Test UPDATE via existing row file
UPDATED_ROW='{"ID": 100, "NAME": "Updated User", "EMAIL": "updated@example.com", "DEPARTMENT": "Marketing", "SALARY": 75000}'
run_test "Update existing row" "echo '$UPDATED_ROW' > '$MOUNT_POINT/SQLFUSE_TEST/tables/EMPLOYEES/rows/100.json'"

# Wait for write to be flushed
sleep 2

# Verify the update worked
run_test_output "Row was updated" "cat '$MOUNT_POINT/SQLFUSE_TEST/tables/EMPLOYEES/rows/100.json'" "Updated User"

# Test DELETE via rm
run_test "Delete row file" "rm '$MOUNT_POINT/SQLFUSE_TEST/tables/EMPLOYEES/rows/100.json'"

# Wait for delete to be processed
sleep 2

# Verify the delete worked (row should no longer exist)
run_test "Row no longer exists" "! cat '$MOUNT_POINT/SQLFUSE_TEST/tables/EMPLOYEES.json' | grep -q 'Updated User'"

echo ""

# ==========================================
# Summary
# ==========================================
echo "=================================================="
echo "Test Summary"
echo "=================================================="
echo -e "Tests Passed: ${GREEN}$TESTS_PASSED${NC}"
echo -e "Tests Failed: ${RED}$TESTS_FAILED${NC}"
echo ""

if [ $TESTS_FAILED -eq 0 ]; then
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "${RED}Some tests failed.${NC}"
    exit 1
fi
