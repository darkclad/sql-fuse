#include "PostgreSQLConnection.hpp"
#include "PostgreSQLConnectionPool.hpp"
#include <cstring>

namespace sqlfuse {

PostgreSQLConnection::PostgreSQLConnection(PostgreSQLConnectionPool* pool, PGconn* conn)
    : m_pool(pool), m_conn(conn), m_released(false) {
}

PostgreSQLConnection::~PostgreSQLConnection() {
    if (!m_released && m_pool && m_conn) {
        release();
    }
}

PostgreSQLConnection::PostgreSQLConnection(PostgreSQLConnection&& other) noexcept
    : m_pool(other.m_pool), m_conn(other.m_conn), m_released(other.m_released) {
    other.m_pool = nullptr;
    other.m_conn = nullptr;
    other.m_released = true;
}

PostgreSQLConnection& PostgreSQLConnection::operator=(PostgreSQLConnection&& other) noexcept {
    if (this != &other) {
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

bool PostgreSQLConnection::isValid() const {
    return m_conn != nullptr && !m_released && PQstatus(m_conn) == CONNECTION_OK;
}

bool PostgreSQLConnection::ping() {
    if (!m_conn || m_released) return false;

    // Try a simple query to check connection
    PGresult* res = PQexec(m_conn, "SELECT 1");
    bool ok = res && PQresultStatus(res) == PGRES_TUPLES_OK;
    if (res) PQclear(res);
    return ok;
}

PGresult* PostgreSQLConnection::execute(const std::string& sql) {
    if (!isValid()) return nullptr;
    return PQexec(m_conn, sql.c_str());
}

PGresult* PostgreSQLConnection::executeParams(const std::string& sql,
                                               const char* const* paramValues,
                                               int nParams) {
    if (!isValid()) return nullptr;
    return PQexecParams(m_conn, sql.c_str(), nParams, nullptr,
                        paramValues, nullptr, nullptr, 0);
}

const char* PostgreSQLConnection::error() const {
    if (!m_conn) return "No connection";
    return PQerrorMessage(m_conn);
}

ConnStatusType PostgreSQLConnection::status() const {
    if (!m_conn) return CONNECTION_BAD;
    return PQstatus(m_conn);
}

uint64_t PostgreSQLConnection::affectedRows(PGresult* result) const {
    if (!result) return 0;
    const char* affected = PQcmdTuples(result);
    if (!affected || !*affected) return 0;
    return std::strtoull(affected, nullptr, 10);
}

std::string PostgreSQLConnection::escapeString(const std::string& str) const {
    if (!m_conn) return str;

    // PQescapeLiteral returns a malloc'd string that includes quotes
    char* escaped = PQescapeLiteral(m_conn, str.c_str(), str.size());
    if (!escaped) return "''";

    std::string result(escaped);
    PQfreemem(escaped);
    return result;
}

std::string PostgreSQLConnection::escapeIdentifier(const std::string& identifier) const {
    if (!m_conn) return "\"" + identifier + "\"";

    char* escaped = PQescapeIdentifier(m_conn, identifier.c_str(), identifier.size());
    if (!escaped) return "\"" + identifier + "\"";

    std::string result(escaped);
    PQfreemem(escaped);
    return result;
}

void PostgreSQLConnection::release() {
    if (!m_released && m_pool && m_conn) {
        m_pool->releaseConnection(m_conn);
        m_released = true;
        m_conn = nullptr;
    }
}

}  // namespace sqlfuse
