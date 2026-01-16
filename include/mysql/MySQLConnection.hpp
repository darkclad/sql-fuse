#pragma once

#include <mysql/mysql.h>
#include <string>
#include <cstdint>

namespace sqlfuse {

class MySQLConnectionPool;

class MySQLConnection {
public:
    MySQLConnection(MySQLConnectionPool* pool, MYSQL* conn);
    ~MySQLConnection();

    // Non-copyable
    MySQLConnection(const MySQLConnection&) = delete;
    MySQLConnection& operator=(const MySQLConnection&) = delete;

    // Movable
    MySQLConnection(MySQLConnection&& other) noexcept;
    MySQLConnection& operator=(MySQLConnection&& other) noexcept;

    MYSQL* get() const { return m_conn; }
    MYSQL* operator->() const { return m_conn; }

    bool isValid() const;
    bool ping();

    // Execute query with automatic reconnection
    bool query(const std::string& sql);
    MYSQL_RES* storeResult();
    MYSQL_RES* useResult();

    // Prepared statement helpers
    MYSQL_STMT* prepareStatement(const std::string& sql);

    const char* error() const;
    unsigned int errorNumber() const;
    uint64_t affectedRows() const;
    uint64_t insertId() const;

private:
    friend class MySQLConnectionPool;
    void release();

    MySQLConnectionPool* m_pool;
    MYSQL* m_conn;
    bool m_released = false;
};

}  // namespace sqlfuse
