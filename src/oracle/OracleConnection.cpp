#include "OracleConnection.hpp"
#include "OracleConnectionPool.hpp"
#include <spdlog/spdlog.h>

namespace sqlfuse {

OracleConnection::OracleConnection(OracleConnectionPool* pool, OCIEnv* env, OCISvcCtx* svc, OCIError* err)
    : m_pool(pool), m_env(env), m_svc(svc), m_err(err) {
}

OracleConnection::~OracleConnection() {
    if (!m_released && m_pool && m_svc) {
        release();
    }
}

OracleConnection::OracleConnection(OracleConnection&& other) noexcept
    : m_pool(other.m_pool)
    , m_env(other.m_env)
    , m_svc(other.m_svc)
    , m_err(other.m_err)
    , m_released(other.m_released)
    , m_lastErrorCode(other.m_lastErrorCode)
    , m_affectedRows(other.m_affectedRows) {
    other.m_pool = nullptr;
    other.m_env = nullptr;
    other.m_svc = nullptr;
    other.m_err = nullptr;
    other.m_released = true;
}

OracleConnection& OracleConnection::operator=(OracleConnection&& other) noexcept {
    if (this != &other) {
        if (!m_released && m_pool && m_svc) {
            release();
        }
        m_pool = other.m_pool;
        m_env = other.m_env;
        m_svc = other.m_svc;
        m_err = other.m_err;
        m_released = other.m_released;
        m_lastErrorCode = other.m_lastErrorCode;
        m_affectedRows = other.m_affectedRows;

        other.m_pool = nullptr;
        other.m_env = nullptr;
        other.m_svc = nullptr;
        other.m_err = nullptr;
        other.m_released = true;
    }
    return *this;
}

bool OracleConnection::isValid() const {
    return m_svc != nullptr && m_err != nullptr && !m_released;
}

bool OracleConnection::ping() {
    if (!isValid()) return false;

    sword status = OCIPing(m_svc, m_err, OCI_DEFAULT);
    return status == OCI_SUCCESS;
}

OCIStmt* OracleConnection::execute(const std::string& sql) {
    if (!isValid()) {
        spdlog::error("Oracle connection not valid");
        return nullptr;
    }

    OCIStmt* stmt = nullptr;
    sword status;

    // Allocate statement handle
    status = OCIHandleAlloc(m_env, (void**)&stmt, OCI_HTYPE_STMT, 0, nullptr);
    if (status != OCI_SUCCESS) {
        spdlog::error("Failed to allocate Oracle statement handle");
        return nullptr;
    }

    // Prepare statement
    status = OCIStmtPrepare(stmt, m_err, (const OraText*)sql.c_str(), sql.length(),
                           OCI_NTV_SYNTAX, OCI_DEFAULT);
    if (status != OCI_SUCCESS) {
        spdlog::error("Failed to prepare Oracle statement: {}", getError());
        OCIHandleFree(stmt, OCI_HTYPE_STMT);
        return nullptr;
    }

    // Determine statement type
    ub2 stmtType = 0;
    OCIAttrGet(stmt, OCI_HTYPE_STMT, &stmtType, nullptr, OCI_ATTR_STMT_TYPE, m_err);

    // Execute statement
    ub4 iters = (stmtType == OCI_STMT_SELECT) ? 0 : 1;
    status = OCIStmtExecute(m_svc, stmt, m_err, iters, 0, nullptr, nullptr, OCI_DEFAULT);

    if (status != OCI_SUCCESS && status != OCI_SUCCESS_WITH_INFO) {
        m_lastErrorCode = getErrorCode();
        spdlog::error("Failed to execute Oracle statement: {}", getError());
        OCIHandleFree(stmt, OCI_HTYPE_STMT);
        return nullptr;
    }

    // Get affected rows for DML statements
    if (stmtType != OCI_STMT_SELECT) {
        ub4 rowCount = 0;
        OCIAttrGet(stmt, OCI_HTYPE_STMT, &rowCount, nullptr, OCI_ATTR_ROW_COUNT, m_err);
        m_affectedRows = rowCount;
    }

    return stmt;
}

bool OracleConnection::executeNonQuery(const std::string& sql) {
    OCIStmt* stmt = execute(sql);
    if (stmt) {
        OCIHandleFree(stmt, OCI_HTYPE_STMT);
        return true;
    }
    return false;
}

bool OracleConnection::commit() {
    if (!isValid()) return false;
    sword status = OCITransCommit(m_svc, m_err, OCI_DEFAULT);
    return status == OCI_SUCCESS;
}

bool OracleConnection::rollback() {
    if (!isValid()) return false;
    sword status = OCITransRollback(m_svc, m_err, OCI_DEFAULT);
    return status == OCI_SUCCESS;
}

std::string OracleConnection::escapeString(const std::string& value) const {
    // Oracle uses doubled single quotes for escaping
    std::string result;
    result.reserve(value.size() * 2);
    for (char c : value) {
        if (c == '\'') {
            result += "''";
        } else {
            result += c;
        }
    }
    return result;
}

std::string OracleConnection::getError() const {
    if (!m_err) return "No error handle";

    sb4 errCode = 0;
    char errBuf[512];
    OCIErrorGet(m_err, 1, nullptr, &errCode, (OraText*)errBuf, sizeof(errBuf), OCI_HTYPE_ERROR);
    m_lastErrorCode = errCode;
    return std::string(errBuf);
}

int OracleConnection::getErrorCode() const {
    if (!m_err) return 0;

    sb4 errCode = 0;
    char errBuf[512];
    OCIErrorGet(m_err, 1, nullptr, &errCode, (OraText*)errBuf, sizeof(errBuf), OCI_HTYPE_ERROR);
    return errCode;
}

uint64_t OracleConnection::affectedRows() const {
    return m_affectedRows;
}

void OracleConnection::release() {
    if (!m_released && m_pool) {
        m_pool->releaseConnection(m_svc, m_err);
        m_released = true;
    }
}

}  // namespace sqlfuse
