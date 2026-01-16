#include "OracleResultSet.hpp"
#include <spdlog/spdlog.h>
#include <cstring>

namespace sqlfuse {

OracleResultSet::OracleResultSet(OCIStmt* stmt, OCIError* err, OCIEnv* env)
    : m_stmt(stmt), m_err(err), m_env(env) {

    if (m_stmt && m_err) {
        // Check statement type
        ub2 stmtType = 0;
        OCIAttrGet(m_stmt, OCI_HTYPE_STMT, &stmtType, nullptr, OCI_ATTR_STMT_TYPE, m_err);

        if (stmtType == OCI_STMT_SELECT) {
            describeColumns();
            if (!m_hasError) {
                defineOutputVariables();
                m_hasData = true;
            }
        } else {
            // Non-SELECT statement (INSERT, UPDATE, DELETE, etc.)
            m_hasData = false;
            m_hasError = false;
        }
    }
}

OracleResultSet::~OracleResultSet() {
    reset();
}

OracleResultSet::OracleResultSet(OracleResultSet&& other) noexcept
    : m_stmt(other.m_stmt)
    , m_err(other.m_err)
    , m_env(other.m_env)
    , m_hasData(other.m_hasData)
    , m_hasError(other.m_hasError)
    , m_errorMsg(std::move(other.m_errorMsg))
    , m_fetchedRows(other.m_fetchedRows)
    , m_columns(std::move(other.m_columns))
    , m_columnsDescribed(other.m_columnsDescribed) {
    other.m_stmt = nullptr;
    other.m_err = nullptr;
    other.m_env = nullptr;
}

OracleResultSet& OracleResultSet::operator=(OracleResultSet&& other) noexcept {
    if (this != &other) {
        reset();
        m_stmt = other.m_stmt;
        m_err = other.m_err;
        m_env = other.m_env;
        m_hasData = other.m_hasData;
        m_hasError = other.m_hasError;
        m_errorMsg = std::move(other.m_errorMsg);
        m_fetchedRows = other.m_fetchedRows;
        m_columns = std::move(other.m_columns);
        m_columnsDescribed = other.m_columnsDescribed;

        other.m_stmt = nullptr;
        other.m_err = nullptr;
        other.m_env = nullptr;
    }
    return *this;
}

void OracleResultSet::describeColumns() {
    if (!m_stmt || !m_err) return;

    // Get column count
    ub4 colCount = 0;
    sword status = OCIAttrGet(m_stmt, OCI_HTYPE_STMT, &colCount, nullptr,
                              OCI_ATTR_PARAM_COUNT, m_err);
    if (status != OCI_SUCCESS) {
        m_hasError = true;
        sb4 errCode = 0;
        char errBuf[512];
        OCIErrorGet(m_err, 1, nullptr, &errCode, (OraText*)errBuf, sizeof(errBuf), OCI_HTYPE_ERROR);
        m_errorMsg = errBuf;
        return;
    }

    m_columns.resize(colCount);

    for (ub4 i = 0; i < colCount; ++i) {
        OCIParam* param = nullptr;
        status = OCIParamGet(m_stmt, OCI_HTYPE_STMT, m_err, (void**)&param, i + 1);
        if (status != OCI_SUCCESS) continue;

        // Get column name
        OraText* colName = nullptr;
        ub4 colNameLen = 0;
        OCIAttrGet(param, OCI_DTYPE_PARAM, &colName, &colNameLen, OCI_ATTR_NAME, m_err);
        m_columns[i].name = std::string((char*)colName, colNameLen);

        // Get data type
        OCIAttrGet(param, OCI_DTYPE_PARAM, &m_columns[i].type, nullptr, OCI_ATTR_DATA_TYPE, m_err);

        // Get data size
        OCIAttrGet(param, OCI_DTYPE_PARAM, &m_columns[i].size, nullptr, OCI_ATTR_DATA_SIZE, m_err);

        // Get precision and scale for numbers
        OCIAttrGet(param, OCI_DTYPE_PARAM, &m_columns[i].precision, nullptr, OCI_ATTR_PRECISION, m_err);
        OCIAttrGet(param, OCI_DTYPE_PARAM, &m_columns[i].scale, nullptr, OCI_ATTR_SCALE, m_err);

        OCIDescriptorFree(param, OCI_DTYPE_PARAM);
    }

    m_columnsDescribed = true;
}

void OracleResultSet::defineOutputVariables() {
    if (!m_stmt || !m_err || m_columns.empty()) return;

    for (size_t i = 0; i < m_columns.size(); ++i) {
        auto& col = m_columns[i];

        // Allocate buffer based on type
        // Convert most types to string for simplicity
        ub4 bufSize = 4000;  // Default buffer size

        switch (col.type) {
            case SQLT_CHR:  // VARCHAR2
            case SQLT_AFC:  // CHAR
            case SQLT_STR:  // STRING
            case SQLT_VCS:  // VARCHAR
                bufSize = col.size + 1;
                break;
            case SQLT_NUM:  // NUMBER
            case SQLT_INT:  // INTEGER
            case SQLT_FLT:  // FLOAT
            case SQLT_VNU:  // VARNUM
                bufSize = 128;
                break;
            case SQLT_DAT:  // DATE
            case SQLT_TIMESTAMP:
            case SQLT_TIMESTAMP_TZ:
            case SQLT_TIMESTAMP_LTZ:
                bufSize = 64;
                break;
            case SQLT_CLOB:
            case SQLT_BLOB:
                // For LOBs, we'll read a limited amount
                bufSize = 4000;
                break;
            default:
                bufSize = 4000;
                break;
        }

        col.data.resize(bufSize);
        col.indicator = 0;
        col.returnLen = 0;

        // Define output variable - fetch as string for simplicity
        sword status = OCIDefineByPos(m_stmt, &col.define, m_err, i + 1,
                                      col.data.data(), bufSize, SQLT_STR,
                                      &col.indicator, &col.returnLen, nullptr, OCI_DEFAULT);
        if (status != OCI_SUCCESS) {
            spdlog::warn("Failed to define output for column {}", col.name);
        }
    }
}

bool OracleResultSet::fetchRow() {
    if (!m_stmt || !m_err || !m_hasData) return false;

    sword status = OCIStmtFetch2(m_stmt, m_err, 1, OCI_FETCH_NEXT, 0, OCI_DEFAULT);

    if (status == OCI_SUCCESS || status == OCI_SUCCESS_WITH_INFO) {
        ++m_fetchedRows;
        return true;
    } else if (status == OCI_NO_DATA) {
        return false;
    } else {
        sb4 errCode = 0;
        char errBuf[512];
        OCIErrorGet(m_err, 1, nullptr, &errCode, (OraText*)errBuf, sizeof(errBuf), OCI_HTYPE_ERROR);
        spdlog::error("Oracle fetch error: {}", errBuf);
        return false;
    }
}

const char* OracleResultSet::getValue(int col) const {
    if (col < 0 || col >= static_cast<int>(m_columns.size())) return nullptr;
    if (m_columns[col].indicator == -1) return nullptr;  // NULL
    return m_columns[col].data.data();
}

bool OracleResultSet::isNull(int col) const {
    if (col < 0 || col >= static_cast<int>(m_columns.size())) return true;
    return m_columns[col].indicator == -1;
}

int OracleResultSet::getLength(int col) const {
    if (col < 0 || col >= static_cast<int>(m_columns.size())) return 0;
    if (m_columns[col].indicator == -1) return 0;
    return m_columns[col].returnLen;
}

int OracleResultSet::numFields() const {
    return static_cast<int>(m_columns.size());
}

int OracleResultSet::numRows() const {
    return m_fetchedRows;
}

const char* OracleResultSet::fieldName(int col) const {
    if (col < 0 || col >= static_cast<int>(m_columns.size())) return "";
    return m_columns[col].name.c_str();
}

int OracleResultSet::fieldType(int col) const {
    if (col < 0 || col >= static_cast<int>(m_columns.size())) return 0;
    return m_columns[col].type;
}

std::vector<std::string> OracleResultSet::getColumnNames() const {
    std::vector<std::string> names;
    names.reserve(m_columns.size());
    for (const auto& col : m_columns) {
        names.push_back(col.name);
    }
    return names;
}

void OracleResultSet::reset() {
    if (m_stmt) {
        OCIHandleFree(m_stmt, OCI_HTYPE_STMT);
        m_stmt = nullptr;
    }
    m_columns.clear();
    m_columnsDescribed = false;
    m_hasData = false;
    m_hasError = false;
    m_errorMsg.clear();
    m_fetchedRows = 0;
}

OCIStmt* OracleResultSet::release() {
    OCIStmt* stmt = m_stmt;
    m_stmt = nullptr;
    m_columns.clear();
    return stmt;
}

}  // namespace sqlfuse
