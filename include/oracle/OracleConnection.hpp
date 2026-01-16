#pragma once

/**
 * @file OracleConnection.hpp
 * @brief RAII wrapper for an Oracle database connection from the connection pool.
 *
 * This class provides a safe, pooled Oracle connection that automatically returns
 * itself to the pool when destroyed. It wraps the OCI (Oracle Call Interface)
 * handles required for database operations.
 */

#include <oci.h>
#include <string>
#include <cstdint>

namespace sqlfuse {

class OracleConnectionPool;

/**
 * @class OracleConnection
 * @brief RAII wrapper for a pooled Oracle database connection.
 *
 * OracleConnection manages an Oracle connection obtained from OracleConnectionPool.
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
 *       OCIStmt* stmt = conn->execute("SELECT * FROM employees");
 *       if (stmt) {
 *           OracleResultSet result(stmt, conn->err(), conn->env());
 *           // ... use result
 *       }
 *       conn->commit();
 *   }  // Connection automatically returned to pool here
 * @endcode
 */
class OracleConnection {
public:
    /**
     * @brief Construct a connection wrapper.
     * @param pool Owning connection pool (for returning the connection).
     * @param env OCI environment handle (shared across all connections).
     * @param svc OCI service context handle (represents the connection session).
     * @param err OCI error handle for this connection.
     *
     * @note This constructor is typically only called by OracleConnectionPool.
     */
    OracleConnection(OracleConnectionPool* pool, OCIEnv* env, OCISvcCtx* svc, OCIError* err);

    /**
     * @brief Destructor - automatically releases connection back to pool.
     */
    ~OracleConnection();

    // Non-copyable to prevent double-release
    OracleConnection(const OracleConnection&) = delete;
    OracleConnection& operator=(const OracleConnection&) = delete;

    // Movable for transfer of ownership
    OracleConnection(OracleConnection&& other) noexcept;
    OracleConnection& operator=(OracleConnection&& other) noexcept;

    /** @brief Get the OCI environment handle. */
    OCIEnv* env() const { return m_env; }

    /** @brief Get the OCI service context handle. */
    OCISvcCtx* svc() const { return m_svc; }

    /** @brief Get the OCI error handle. */
    OCIError* err() const { return m_err; }

    /**
     * @brief Check if the connection is valid and usable.
     * @return true if connection handles are valid and not released.
     */
    bool isValid() const;

    /**
     * @brief Ping the database to verify connectivity.
     * @return true if the database responds to the ping.
     */
    bool ping();

    /**
     * @brief Execute a SQL query and return the statement handle.
     * @param sql The SQL statement to execute.
     * @return OCIStmt* on success (caller must free with OCIHandleFree), nullptr on error.
     *
     * For SELECT statements, the returned handle can be used with OracleResultSet.
     * For DML statements, use executeNonQuery() instead for simpler handling.
     */
    OCIStmt* execute(const std::string& sql);

    /**
     * @brief Execute a non-query SQL statement (INSERT, UPDATE, DELETE, DDL).
     * @param sql The SQL statement to execute.
     * @return true on success, false on error (check getError() for details).
     *
     * This method automatically frees the statement handle after execution.
     * Use affectedRows() to get the number of rows affected.
     */
    bool executeNonQuery(const std::string& sql);

    /**
     * @brief Commit the current transaction.
     * @return true on success, false on error.
     */
    bool commit();

    /**
     * @brief Rollback the current transaction.
     * @return true on success, false on error.
     */
    bool rollback();

    /**
     * @brief Escape a string value for safe use in SQL.
     * @param value The string to escape.
     * @return String with single quotes doubled (Oracle escaping convention).
     *
     * Example: "O'Brien" becomes "O''Brien"
     */
    std::string escapeString(const std::string& value) const;

    /**
     * @brief Get the last OCI error message.
     * @return Human-readable error message from the last failed operation.
     */
    std::string getError() const;

    /**
     * @brief Get the last OCI error code.
     * @return Oracle error code (e.g., ORA-00001 returns 1).
     */
    int getErrorCode() const;

    /**
     * @brief Get the number of rows affected by the last DML statement.
     * @return Row count from last INSERT, UPDATE, or DELETE.
     */
    uint64_t affectedRows() const;

private:
    friend class OracleConnectionPool;

    /**
     * @brief Release the connection back to the pool.
     * Called automatically by destructor; can only be called once.
     */
    void release();

    OracleConnectionPool* m_pool;     ///< Owning connection pool
    OCIEnv* m_env;                    ///< OCI environment handle (shared)
    OCISvcCtx* m_svc;                 ///< OCI service context (this connection's session)
    OCIError* m_err;                  ///< OCI error handle (per-connection)
    bool m_released = false;          ///< Flag to prevent double-release
    mutable int m_lastErrorCode = 0;  ///< Cached error code from last operation
    mutable uint64_t m_affectedRows = 0;  ///< Row count from last DML
};

}  // namespace sqlfuse
