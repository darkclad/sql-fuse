#pragma once

/**
 * @file MySQLConnection.hpp
 * @brief RAII wrapper for a MySQL database connection from the connection pool.
 *
 * This class provides a safe, pooled MySQL connection that automatically returns
 * itself to the pool when destroyed. It wraps the MYSQL handle from libmysqlclient
 * and provides convenient methods for query execution.
 */

#include <mysql/mysql.h>
#include <string>
#include <cstdint>

namespace sqlfuse {

class MySQLConnectionPool;

/**
 * @class MySQLConnection
 * @brief RAII wrapper for a pooled MySQL database connection.
 *
 * MySQLConnection manages a MySQL connection obtained from MySQLConnectionPool.
 * When the connection goes out of scope, it is automatically returned to the pool
 * for reuse, avoiding the overhead of creating new connections.
 *
 * Thread Safety:
 * - Individual connections are NOT thread-safe; each thread should acquire its own.
 * - The connection pool itself is thread-safe for acquiring/releasing connections.
 *
 * Usage:
 * @code
 *   auto conn = pool.acquire();
 *   if (conn) {
 *       if (conn->query("SELECT * FROM employees")) {
 *           MySQLResultSet result(conn->storeResult());
 *           // ... use result
 *       }
 *   }  // Connection automatically returned to pool here
 * @endcode
 */
class MySQLConnection {
public:
    /**
     * @brief Construct a connection wrapper.
     * @param pool Owning connection pool (for returning the connection).
     * @param conn The MySQL connection handle.
     *
     * @note This constructor is typically only called by MySQLConnectionPool.
     */
    MySQLConnection(MySQLConnectionPool* pool, MYSQL* conn);

    /**
     * @brief Destructor - automatically releases connection back to pool.
     */
    ~MySQLConnection();

    // Non-copyable to prevent double-release
    MySQLConnection(const MySQLConnection&) = delete;
    MySQLConnection& operator=(const MySQLConnection&) = delete;

    // Movable for transfer of ownership
    MySQLConnection(MySQLConnection&& other) noexcept;
    MySQLConnection& operator=(MySQLConnection&& other) noexcept;

    /**
     * @brief Get the underlying MySQL connection handle.
     * @return Raw MYSQL* pointer (still owned by this object).
     */
    MYSQL* get() const { return m_conn; }

    /**
     * @brief Arrow operator for direct access to MySQL handle.
     * @return Raw MYSQL* pointer for mysql_* function calls.
     */
    MYSQL* operator->() const { return m_conn; }

    /**
     * @brief Check if the connection is valid and usable.
     * @return true if connection handle is valid and not released.
     */
    bool isValid() const;

    /**
     * @brief Ping the database to verify connectivity.
     * @return true if the database responds to the ping.
     *
     * Uses mysql_ping() which also attempts automatic reconnection
     * if the connection was lost.
     */
    bool ping();

    /**
     * @brief Execute a SQL query.
     * @param sql The SQL statement to execute.
     * @return true on success, false on error (check error() for details).
     *
     * For SELECT statements, use storeResult() or useResult() after
     * a successful query to retrieve the result set.
     */
    bool query(const std::string& sql);

    /**
     * @brief Store the complete result set in memory.
     * @return MYSQL_RES* on success (caller must free with mysql_free_result), nullptr on error.
     *
     * Fetches all rows from the server immediately. Best for small to medium result sets.
     * The caller is responsible for freeing the result, typically via MySQLResultSet.
     */
    MYSQL_RES* storeResult();

    /**
     * @brief Use the result set row-by-row from the server.
     * @return MYSQL_RES* on success (caller must free), nullptr on error.
     *
     * Rows are fetched on demand as you iterate. More memory efficient for large
     * result sets, but the connection is blocked until all rows are fetched or
     * the result is freed.
     */
    MYSQL_RES* useResult();

    /**
     * @brief Prepare a SQL statement for execution.
     * @param sql The SQL statement with ? placeholders.
     * @return MYSQL_STMT* on success (caller must free with mysql_stmt_close), nullptr on error.
     */
    MYSQL_STMT* prepareStatement(const std::string& sql);

    /**
     * @brief Get the last MySQL error message.
     * @return Error description from the last failed operation.
     */
    const char* error() const;

    /**
     * @brief Get the last MySQL error number.
     * @return MySQL error code (e.g., ER_DUP_ENTRY is 1062).
     */
    unsigned int errorNumber() const;

    /**
     * @brief Get the number of rows affected by the last INSERT/UPDATE/DELETE.
     * @return Row count from last DML statement.
     */
    uint64_t affectedRows() const;

    /**
     * @brief Get the auto-generated ID from the last INSERT.
     * @return Last insert ID, or 0 if no auto-increment column.
     */
    uint64_t insertId() const;

private:
    friend class MySQLConnectionPool;

    /**
     * @brief Release the connection back to the pool.
     * Called automatically by destructor; can only be called once.
     */
    void release();

    MySQLConnectionPool* m_pool;  ///< Owning connection pool
    MYSQL* m_conn;                ///< MySQL connection handle
    bool m_released = false;      ///< Flag to prevent double-release
};

}  // namespace sqlfuse
