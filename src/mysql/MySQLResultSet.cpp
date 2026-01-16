/**
 * @file MySQLResultSet.cpp
 * @brief Implementation of RAII MySQL result set wrapper.
 *
 * Implements the MySQLResultSet class which provides safe management of
 * MYSQL_RES handles with automatic cleanup on destruction.
 */

#include "MySQLResultSet.hpp"

namespace sqlfuse {

// ============================================================================
// Construction and Destruction
// ============================================================================

MySQLResultSet::MySQLResultSet(MYSQL_RES* res) : m_res(res) {}

MySQLResultSet::~MySQLResultSet() {
    if (m_res) {
        mysql_free_result(m_res);
    }
}

// ============================================================================
// Move Operations
// ============================================================================

MySQLResultSet::MySQLResultSet(MySQLResultSet&& other) noexcept : m_res(other.m_res) {
    other.m_res = nullptr;
}

MySQLResultSet& MySQLResultSet::operator=(MySQLResultSet&& other) noexcept {
    if (this != &other) {
        // Free current result before taking ownership of new one
        if (m_res) {
            mysql_free_result(m_res);
        }
        m_res = other.m_res;
        other.m_res = nullptr;
    }
    return *this;
}

// ============================================================================
// Row and Field Access
// ============================================================================

MYSQL_ROW MySQLResultSet::fetchRow() {
    // Returns next row as array of char*, or nullptr when done
    return m_res ? mysql_fetch_row(m_res) : nullptr;
}

unsigned int MySQLResultSet::numFields() const {
    return m_res ? mysql_num_fields(m_res) : 0;
}

uint64_t MySQLResultSet::numRows() const {
    // Only accurate for mysql_store_result() results
    // Returns 0 for mysql_use_result() until all rows fetched
    return m_res ? mysql_num_rows(m_res) : 0;
}

MYSQL_FIELD* MySQLResultSet::fetchFields() const {
    // Returns array of field metadata structures
    return m_res ? mysql_fetch_fields(m_res) : nullptr;
}

std::vector<std::string> MySQLResultSet::getColumnNames() const {
    std::vector<std::string> names;
    if (!m_res) return names;

    unsigned int numFields = mysql_num_fields(m_res);
    MYSQL_FIELD* fields = mysql_fetch_fields(m_res);

    names.reserve(numFields);
    for (unsigned int i = 0; i < numFields; ++i) {
        names.emplace_back(fields[i].name);
    }

    return names;
}

// ============================================================================
// Resource Management
// ============================================================================

void MySQLResultSet::reset(MYSQL_RES* res) {
    // Free current result and take ownership of new one
    if (m_res) {
        mysql_free_result(m_res);
    }
    m_res = res;
}

MYSQL_RES* MySQLResultSet::release() {
    // Transfer ownership to caller
    MYSQL_RES* res = m_res;
    m_res = nullptr;
    return res;
}

}  // namespace sqlfuse
