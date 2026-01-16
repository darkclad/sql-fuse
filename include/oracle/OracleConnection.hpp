#pragma once

#include <oci.h>
#include <string>
#include <cstdint>

namespace sqlfuse {

class OracleConnectionPool;

class OracleConnection {
public:
    OracleConnection(OracleConnectionPool* pool, OCIEnv* env, OCISvcCtx* svc, OCIError* err);
    ~OracleConnection();

    // Non-copyable
    OracleConnection(const OracleConnection&) = delete;
    OracleConnection& operator=(const OracleConnection&) = delete;

    // Movable
    OracleConnection(OracleConnection&& other) noexcept;
    OracleConnection& operator=(OracleConnection&& other) noexcept;

    OCIEnv* env() const { return m_env; }
    OCISvcCtx* svc() const { return m_svc; }
    OCIError* err() const { return m_err; }

    bool isValid() const;
    bool ping();

    // Execute query and return statement handle (caller must free)
    OCIStmt* execute(const std::string& sql);

    // Execute non-query statement (INSERT, UPDATE, DELETE, DDL)
    bool executeNonQuery(const std::string& sql);

    // Commit/rollback
    bool commit();
    bool rollback();

    // Escape string for SQL (doubles single quotes)
    std::string escapeString(const std::string& value) const;

    // Get last error message
    std::string getError() const;
    int getErrorCode() const;

    uint64_t affectedRows() const;

private:
    friend class OracleConnectionPool;
    void release();

    OracleConnectionPool* m_pool;
    OCIEnv* m_env;
    OCISvcCtx* m_svc;
    OCIError* m_err;
    bool m_released = false;
    mutable int m_lastErrorCode = 0;
    mutable uint64_t m_affectedRows = 0;
};

}  // namespace sqlfuse
