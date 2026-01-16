/**
 * @file OracleConnection.cpp
 * @brief Implementation of the RAII Oracle connection wrapper.
 *
 * This file implements the OracleConnection class which provides a safe,
 * pooled connection to Oracle databases using OCI (Oracle Call Interface).
 * Connections are automatically returned to the pool when destroyed.
 */

#include "OracleConnection.hpp"
#include "OracleConnectionPool.hpp"
#include <spdlog/spdlog.h>

namespace sqlfuse {

// =============================================================================
// Constructor and Destructor
// =============================================================================

/**
 * Initialize a connection wrapper with OCI handles.
 * Called by the connection pool when handing out a connection.
 */
OracleConnection::OracleConnection(OracleConnectionPool* pool, OCIEnv* env, OCISvcCtx* svc, OCIError* err)
    : m_pool(pool), m_env(env), m_svc(svc), m_err(err) {
}

/**
 * Destructor - return connection to pool if not already released.
 * The pool may reuse this connection for future requests.
 */
OracleConnection::~OracleConnection() {
    if (!m_released && m_pool && m_svc) {
        release();
    }
}

// =============================================================================
// Move Operations
// =============================================================================

/**
 * Move constructor - transfer ownership of the connection.
 * The source connection is marked as released to prevent double-return.
 */
OracleConnection::OracleConnection(OracleConnection&& other) noexcept
    : m_pool(other.m_pool)
    , m_env(other.m_env)
    , m_svc(other.m_svc)
    , m_err(other.m_err)
    , m_released(other.m_released)
    , m_lastErrorCode(other.m_lastErrorCode)
    , m_affectedRows(other.m_affectedRows) {
    // Null out source to prevent it from releasing
    other.m_pool = nullptr;
    other.m_env = nullptr;
    other.m_svc = nullptr;
    other.m_err = nullptr;
    other.m_released = true;
}

/**
 * Move assignment - release current connection (if any) and take over new one.
 */
OracleConnection& OracleConnection::operator=(OracleConnection&& other) noexcept {
    if (this != &other) {
        // Release our current connection first
        if (!m_released && m_pool && m_svc) {
            release();
        }

        // Take over the other connection's handles
        m_pool = other.m_pool;
        m_env = other.m_env;
        m_svc = other.m_svc;
        m_err = other.m_err;
        m_released = other.m_released;
        m_lastErrorCode = other.m_lastErrorCode;
        m_affectedRows = other.m_affectedRows;

        // Null out source
        other.m_pool = nullptr;
        other.m_env = nullptr;
        other.m_svc = nullptr;
        other.m_err = nullptr;
        other.m_released = true;
    }
    return *this;
}

// =============================================================================
// Connection Status
// =============================================================================

/**
 * Check if the connection is usable.
 * Returns false if handles are null or connection was already released.
 */
bool OracleConnection::isValid() const {
    return m_svc != nullptr && m_err != nullptr && !m_released;
}

/**
 * Ping the database to verify the connection is alive.
 * Uses OCI's built-in ping functionality for efficient checking.
 */
bool OracleConnection::ping() {
    if (!isValid()) return false;

    sword status = OCIPing(m_svc, m_err, OCI_DEFAULT);
    return status == OCI_SUCCESS;
}

// =============================================================================
// SQL Execution
// =============================================================================

/**
 * Execute a SQL statement and return the statement handle.
 *
 * This method handles the full OCI statement lifecycle:
 * 1. Allocate a statement handle
 * 2. Prepare the SQL text
 * 3. Determine statement type (SELECT vs DML/DDL)
 * 4. Execute with appropriate iteration count
 * 5. Capture affected rows for DML statements
 *
 * For SELECT statements, the caller receives an executed statement ready
 * for fetching. The caller is responsible for freeing the handle when done,
 * typically by passing it to OracleResultSet which takes ownership.
 *
 * For DML/DDL statements, prefer executeNonQuery() which handles cleanup.
 */
OCIStmt* OracleConnection::execute(const std::string& sql) {
    if (!isValid()) {
        spdlog::error("Oracle connection not valid");
        return nullptr;
    }

    OCIStmt* stmt = nullptr;
    sword status;

    // Step 1: Allocate statement handle from the environment
    status = OCIHandleAlloc(m_env, (void**)&stmt, OCI_HTYPE_STMT, 0, nullptr);
    if (status != OCI_SUCCESS) {
        spdlog::error("Failed to allocate Oracle statement handle");
        return nullptr;
    }

    // Step 2: Prepare (parse) the SQL statement
    status = OCIStmtPrepare(stmt, m_err, (const OraText*)sql.c_str(), sql.length(),
                           OCI_NTV_SYNTAX, OCI_DEFAULT);
    if (status != OCI_SUCCESS) {
        spdlog::error("Failed to prepare Oracle statement: {}", getError());
        OCIHandleFree(stmt, OCI_HTYPE_STMT);
        return nullptr;
    }

    // Step 3: Determine statement type to set correct iteration count
    // SELECT needs iters=0 (we'll fetch rows later)
    // DML/DDL needs iters=1 (execute once)
    ub2 stmtType = 0;
    OCIAttrGet(stmt, OCI_HTYPE_STMT, &stmtType, nullptr, OCI_ATTR_STMT_TYPE, m_err);

    // Step 4: Execute the statement
    ub4 iters = (stmtType == OCI_STMT_SELECT) ? 0 : 1;
    status = OCIStmtExecute(m_svc, stmt, m_err, iters, 0, nullptr, nullptr, OCI_DEFAULT);

    if (status != OCI_SUCCESS && status != OCI_SUCCESS_WITH_INFO) {
        m_lastErrorCode = getErrorCode();
        spdlog::error("Failed to execute Oracle statement: {}", getError());
        OCIHandleFree(stmt, OCI_HTYPE_STMT);
        return nullptr;
    }

    // Step 5: Get affected row count for DML statements
    if (stmtType != OCI_STMT_SELECT) {
        ub4 rowCount = 0;
        OCIAttrGet(stmt, OCI_HTYPE_STMT, &rowCount, nullptr, OCI_ATTR_ROW_COUNT, m_err);
        m_affectedRows = rowCount;
    }

    return stmt;
}

/**
 * Execute a non-query SQL statement (INSERT, UPDATE, DELETE, DDL).
 *
 * This is a convenience wrapper around execute() that automatically
 * frees the statement handle. Use this for statements that don't
 * return result sets.
 *
 * After calling, use affectedRows() to get the number of modified rows.
 */
bool OracleConnection::executeNonQuery(const std::string& sql) {
    OCIStmt* stmt = execute(sql);
    if (stmt) {
        OCIHandleFree(stmt, OCI_HTYPE_STMT);
        return true;
    }
    return false;
}

// =============================================================================
// Transaction Control
// =============================================================================

/**
 * Commit the current transaction.
 * All changes made since the last commit/rollback are made permanent.
 */
bool OracleConnection::commit() {
    if (!isValid()) return false;
    sword status = OCITransCommit(m_svc, m_err, OCI_DEFAULT);
    return status == OCI_SUCCESS;
}

/**
 * Rollback the current transaction.
 * All changes made since the last commit/rollback are discarded.
 */
bool OracleConnection::rollback() {
    if (!isValid()) return false;
    sword status = OCITransRollback(m_svc, m_err, OCI_DEFAULT);
    return status == OCI_SUCCESS;
}

// =============================================================================
// Utility Methods
// =============================================================================

/**
 * Escape a string value for safe use in SQL statements.
 * Oracle uses doubled single quotes: O'Brien becomes O''Brien
 */
std::string OracleConnection::escapeString(const std::string& value) const {
    std::string result;
    result.reserve(value.size() * 2);  // Worst case: all quotes
    for (char c : value) {
        if (c == '\'') {
            result += "''";
        } else {
            result += c;
        }
    }
    return result;
}

/**
 * Get the last OCI error message.
 * Retrieves the error text from the error handle and caches the error code.
 */
std::string OracleConnection::getError() const {
    if (!m_err) return "No error handle";

    sb4 errCode = 0;
    char errBuf[512];
    OCIErrorGet(m_err, 1, nullptr, &errCode, (OraText*)errBuf, sizeof(errBuf), OCI_HTYPE_ERROR);
    m_lastErrorCode = errCode;
    return std::string(errBuf);
}

/**
 * Get the last OCI error code (e.g., 1 for ORA-00001).
 */
int OracleConnection::getErrorCode() const {
    if (!m_err) return 0;

    sb4 errCode = 0;
    char errBuf[512];
    OCIErrorGet(m_err, 1, nullptr, &errCode, (OraText*)errBuf, sizeof(errBuf), OCI_HTYPE_ERROR);
    return errCode;
}

/**
 * Get the number of rows affected by the last DML statement.
 */
uint64_t OracleConnection::affectedRows() const {
    return m_affectedRows;
}

/**
 * Release the connection back to the pool.
 * Called automatically by destructor, but can be called early to return
 * the connection while keeping the wrapper object alive.
 */
void OracleConnection::release() {
    if (!m_released && m_pool) {
        m_pool->releaseConnection(m_svc, m_err);
        m_released = true;
    }
}

}  // namespace sqlfuse
