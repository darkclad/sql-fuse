#pragma once

/**
 * @file PostgreSQLConnection.hpp
 * @brief RAII wrapper for pooled PostgreSQL database connections.
 *
 * This file provides a connection wrapper that integrates with the
 * PostgreSQLConnectionPool for automatic connection lifecycle management.
 * When a PostgreSQLConnection goes out of scope, the underlying PGconn
 * is automatically returned to the pool for reuse.
 */

#include <libpq-fe.h>
#include <string>
#include <cstdint>

namespace sqlfuse {

// Forward declaration
class PostgreSQLConnectionPool;

/**
 * @class PostgreSQLConnection
 * @brief RAII wrapper for a PostgreSQL connection from the pool.
 *
 * This class wraps a PGconn* handle obtained from PostgreSQLConnectionPool,
 * providing automatic connection return when destroyed. The wrapper provides
 * convenience methods for query execution while ensuring proper resource management.
 *
 * PostgreSQL libpq API Usage:
 * - PQexec() for simple queries returning PGresult*
 * - PQexecParams() for parameterized queries (SQL injection safe)
 * - PQstatus() for connection state checking
 * - PQerrorMessage() for error details
 * - PQescapeStringConn() for string escaping
 * - PQescapeIdentifier() for identifier escaping
 *
 * Connection Validation:
 * - isValid() checks if connection is established and not in error state
 * - ping() performs a query to verify server responsiveness
 *
 * Thread Safety:
 * - Individual connections should not be shared between threads
 * - The pool handles thread-safe connection distribution
 *
 * @see PostgreSQLConnectionPool for connection acquisition
 */
class PostgreSQLConnection {
public:
    /**
     * @brief Construct a connection wrapper.
     * @param pool Pointer to the owning connection pool.
     * @param conn Raw PGconn* handle to wrap.
     *
     * The connection will be returned to the pool upon destruction.
     */
    PostgreSQLConnection(PostgreSQLConnectionPool* pool, PGconn* conn);

    /**
     * @brief Destructor - returns connection to pool.
     *
     * Calls release() to return the connection for reuse.
     */
    ~PostgreSQLConnection();

    // Non-copyable (connection ownership semantics)
    PostgreSQLConnection(const PostgreSQLConnection&) = delete;
    PostgreSQLConnection& operator=(const PostgreSQLConnection&) = delete;

    // Movable (transfer ownership)
    PostgreSQLConnection(PostgreSQLConnection&& other) noexcept;
    PostgreSQLConnection& operator=(PostgreSQLConnection&& other) noexcept;

    /**
     * @brief Get the underlying PGconn handle.
     * @return Raw PGconn* pointer (still owned by this wrapper).
     */
    PGconn* get() const { return m_conn; }

    /**
     * @brief Arrow operator for direct libpq function calls.
     * @return Raw PGconn* pointer.
     *
     * Note: PGconn is an opaque struct, so this is mainly for
     * passing to libpq functions that take PGconn*.
     */
    PGconn* operator->() const { return m_conn; }

    /**
     * @brief Check if the connection is valid.
     * @return true if connection is established and not in error state.
     *
     * Checks PQstatus() == CONNECTION_OK.
     */
    bool isValid() const;

    /**
     * @brief Test connection by executing a simple query.
     * @return true if the server responds successfully.
     *
     * Executes "SELECT 1" to verify the connection is active.
     * This is more thorough than isValid() which only checks local state.
     */
    bool ping();

    /**
     * @brief Execute a SQL query.
     * @param sql The SQL statement to execute.
     * @return PGresult* handle (caller must PQclear() when done).
     *
     * Returns the result handle regardless of success/failure.
     * Caller should check PQresultStatus() and call PQclear().
     * For queries returning data, wrap result in PostgreSQLResultSet.
     */
    PGresult* execute(const std::string& sql);

    /**
     * @brief Execute a parameterized SQL query.
     * @param sql SQL statement with $1, $2, etc. placeholders.
     * @param paramValues Array of parameter value strings.
     * @param nParams Number of parameters.
     * @return PGresult* handle (caller must PQclear() when done).
     *
     * Parameterized queries prevent SQL injection by separating
     * SQL structure from data values. Use this for user-provided data.
     */
    PGresult* executeParams(const std::string& sql,
                            const char* const* paramValues,
                            int nParams);

    /**
     * @brief Get the last error message.
     * @return Error message string from PQerrorMessage().
     */
    const char* error() const;

    /**
     * @brief Get the connection status.
     * @return ConnStatusType enum value (CONNECTION_OK, CONNECTION_BAD, etc.).
     */
    ConnStatusType status() const;

    /**
     * @brief Get the number of rows affected by the last command.
     * @param result The PGresult from the command.
     * @return Number of affected rows (INSERT/UPDATE/DELETE).
     *
     * Parses the string from PQcmdTuples() and converts to uint64_t.
     */
    uint64_t affectedRows(PGresult* result) const;

    /**
     * @brief Escape a string value for safe SQL interpolation.
     * @param str The string to escape.
     * @return Escaped string (without surrounding quotes).
     *
     * Uses PQescapeStringConn() which is connection-aware and handles
     * the server's character encoding. Returns string with special
     * characters escaped (e.g., single quotes doubled).
     */
    std::string escapeString(const std::string& str) const;

    /**
     * @brief Escape an identifier (table name, column name).
     * @param identifier The identifier to escape.
     * @return Escaped identifier wrapped in double quotes.
     *
     * Uses PQescapeIdentifier() which properly handles double quotes
     * and unicode characters in identifiers.
     */
    std::string escapeIdentifier(const std::string& identifier) const;

private:
    friend class PostgreSQLConnectionPool;

    /**
     * @brief Return the connection to the pool.
     *
     * Called by destructor or explicitly to release the connection
     * back to the pool for reuse by other threads.
     */
    void release();

    PostgreSQLConnectionPool* m_pool;  ///< Owning connection pool
    PGconn* m_conn;                    ///< PostgreSQL connection handle
    bool m_released = false;           ///< Whether connection has been returned
};

}  // namespace sqlfuse
