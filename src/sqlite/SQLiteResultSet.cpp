/**
 * @file SQLiteResultSet.cpp
 * @brief Implementation of RAII SQLite result set wrapper.
 *
 * Implements the SQLiteResultSet class which provides a safe wrapper around
 * sqlite3_stmt prepared statement handles with automatic finalization.
 * Uses the step() iteration pattern to retrieve rows one at a time.
 */

#include "SQLiteResultSet.hpp"

namespace sqlfuse {

// ============================================================================
// Construction and Destruction
// ============================================================================

SQLiteResultSet::SQLiteResultSet(sqlite3_stmt* stmt) : m_stmt(stmt) {}

SQLiteResultSet::~SQLiteResultSet() {
    finalize();
}

// ============================================================================
// Move Operations
// ============================================================================

SQLiteResultSet::SQLiteResultSet(SQLiteResultSet&& other) noexcept
    : m_stmt(other.m_stmt) {
    other.m_stmt = nullptr;
}

SQLiteResultSet& SQLiteResultSet::operator=(SQLiteResultSet&& other) noexcept {
    if (this != &other) {
        finalize();
        m_stmt = other.m_stmt;
        other.m_stmt = nullptr;
    }
    return *this;
}

// ============================================================================
// Row Iteration
// ============================================================================

bool SQLiteResultSet::step() {
    if (!m_stmt) return false;
    // SQLITE_ROW indicates a row is available; SQLITE_DONE means no more rows
    return sqlite3_step(m_stmt) == SQLITE_ROW;
}

// ============================================================================
// Column Access
// ============================================================================

int SQLiteResultSet::columnCount() const {
    return m_stmt ? sqlite3_column_count(m_stmt) : 0;
}

std::string SQLiteResultSet::columnName(int index) const {
    if (!m_stmt) return "";
    const char* name = sqlite3_column_name(m_stmt, index);
    return name ? name : "";
}

std::string SQLiteResultSet::getString(int index) const {
    if (!m_stmt || isNull(index)) return "";
    const unsigned char* text = sqlite3_column_text(m_stmt, index);
    return text ? reinterpret_cast<const char*>(text) : "";
}

int64_t SQLiteResultSet::getInt64(int index) const {
    if (!m_stmt) return 0;
    return sqlite3_column_int64(m_stmt, index);
}

bool SQLiteResultSet::isNull(int index) const {
    if (!m_stmt) return true;
    return sqlite3_column_type(m_stmt, index) == SQLITE_NULL;
}

// ============================================================================
// Statement Management
// ============================================================================

void SQLiteResultSet::reset() {
    if (m_stmt) {
        sqlite3_reset(m_stmt);
    }
}

void SQLiteResultSet::finalize() {
    if (m_stmt) {
        sqlite3_finalize(m_stmt);
        m_stmt = nullptr;
    }
}

}  // namespace sqlfuse
