/**
 * @file MySQLConnection.cpp
 * @brief Implementation of RAII MySQL connection wrapper.
 *
 * Implements the MySQLConnection class which provides a safe wrapper around
 * MYSQL handles with automatic connection pool integration.
 */

#include "MySQLConnection.hpp"
#include "MySQLConnectionPool.hpp"

namespace sqlfuse {

// ============================================================================
// Construction and Destruction
// ============================================================================

MySQLConnection::MySQLConnection(MySQLConnectionPool* pool, MYSQL* conn)
    : m_pool(pool), m_conn(conn), m_released(false) {
}

MySQLConnection::~MySQLConnection() {
    if (!m_released && m_pool && m_conn) {
        release();
    }
}

// ============================================================================
// Move Operations
// ============================================================================

MySQLConnection::MySQLConnection(MySQLConnection&& other) noexcept
    : m_pool(other.m_pool), m_conn(other.m_conn), m_released(other.m_released) {
    other.m_pool = nullptr;
    other.m_conn = nullptr;
    other.m_released = true;
}

MySQLConnection& MySQLConnection::operator=(MySQLConnection&& other) noexcept {
    if (this != &other) {
        // Release current connection before taking ownership of new one
        if (!m_released && m_pool && m_conn) {
            release();
        }
        m_pool = other.m_pool;
        m_conn = other.m_conn;
        m_released = other.m_released;
        other.m_pool = nullptr;
        other.m_conn = nullptr;
        other.m_released = true;
    }
    return *this;
}

// ============================================================================
// Connection State
// ============================================================================

bool MySQLConnection::isValid() const {
    return m_conn != nullptr && !m_released;
}

bool MySQLConnection::ping() {
    if (!isValid()) return false;
    // mysql_ping() returns 0 on success, non-zero on failure
    // Also attempts automatic reconnection if connection was lost
    return mysql_ping(m_conn) == 0;
}

// ============================================================================
// Query Execution
// ============================================================================

bool MySQLConnection::query(const std::string& sql) {
    if (!isValid()) return false;
    // mysql_real_query() is preferred over mysql_query() for binary safety
    return mysql_real_query(m_conn, sql.c_str(), sql.size()) == 0;
}

MYSQL_RES* MySQLConnection::storeResult() {
    if (!isValid()) return nullptr;
    // Fetches entire result set into client memory
    return mysql_store_result(m_conn);
}

MYSQL_RES* MySQLConnection::useResult() {
    if (!isValid()) return nullptr;
    // Initiates row-by-row retrieval from server
    // More memory efficient but locks connection until complete
    return mysql_use_result(m_conn);
}

MYSQL_STMT* MySQLConnection::prepareStatement(const std::string& sql) {
    if (!isValid()) return nullptr;

    MYSQL_STMT* stmt = mysql_stmt_init(m_conn);
    if (!stmt) return nullptr;

    if (mysql_stmt_prepare(stmt, sql.c_str(), sql.size()) != 0) {
        mysql_stmt_close(stmt);
        return nullptr;
    }

    return stmt;
}

// ============================================================================
// Error and Status Information
// ============================================================================

const char* MySQLConnection::error() const {
    if (!m_conn) return "No connection";
    return mysql_error(m_conn);
}

unsigned int MySQLConnection::errorNumber() const {
    if (!m_conn) return 0;
    return mysql_errno(m_conn);
}

uint64_t MySQLConnection::affectedRows() const {
    if (!m_conn) return 0;
    return mysql_affected_rows(m_conn);
}

uint64_t MySQLConnection::insertId() const {
    if (!m_conn) return 0;
    return mysql_insert_id(m_conn);
}

// ============================================================================
// Connection Pool Integration
// ============================================================================

void MySQLConnection::release() {
    if (!m_released && m_pool && m_conn) {
        m_pool->releaseConnection(m_conn);
        m_released = true;
        m_conn = nullptr;
    }
}

}  // namespace sqlfuse
