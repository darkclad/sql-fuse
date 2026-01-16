#include "SQLiteResultSet.hpp"

namespace sqlfuse {

SQLiteResultSet::SQLiteResultSet(sqlite3_stmt* stmt) : m_stmt(stmt) {}

SQLiteResultSet::~SQLiteResultSet() {
    finalize();
}

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

bool SQLiteResultSet::step() {
    if (!m_stmt) return false;
    return sqlite3_step(m_stmt) == SQLITE_ROW;
}

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
