#pragma once

#include <libpq-fe.h>
#include <string>
#include <cstdint>

namespace sqlfuse {

class PostgreSQLConnectionPool;

class PostgreSQLConnection {
public:
    PostgreSQLConnection(PostgreSQLConnectionPool* pool, PGconn* conn);
    ~PostgreSQLConnection();

    // Non-copyable
    PostgreSQLConnection(const PostgreSQLConnection&) = delete;
    PostgreSQLConnection& operator=(const PostgreSQLConnection&) = delete;

    // Movable
    PostgreSQLConnection(PostgreSQLConnection&& other) noexcept;
    PostgreSQLConnection& operator=(PostgreSQLConnection&& other) noexcept;

    PGconn* get() const { return m_conn; }
    PGconn* operator->() const { return m_conn; }

    bool isValid() const;
    bool ping();

    // Execute query
    PGresult* execute(const std::string& sql);
    PGresult* executeParams(const std::string& sql,
                            const char* const* paramValues,
                            int nParams);

    const char* error() const;
    ConnStatusType status() const;
    uint64_t affectedRows(PGresult* result) const;

    // Escape string for use in queries
    std::string escapeString(const std::string& str) const;
    std::string escapeIdentifier(const std::string& identifier) const;

private:
    friend class PostgreSQLConnectionPool;
    void release();

    PostgreSQLConnectionPool* m_pool;
    PGconn* m_conn;
    bool m_released = false;
};

}  // namespace sqlfuse
