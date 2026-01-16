#include "PostgreSQLResultSet.hpp"

namespace sqlfuse {

PostgreSQLResultSet::PostgreSQLResultSet(PGresult* res) : m_res(res), m_currentRow(-1) {}

PostgreSQLResultSet::~PostgreSQLResultSet() {
    if (m_res) {
        PQclear(m_res);
    }
}

PostgreSQLResultSet::PostgreSQLResultSet(PostgreSQLResultSet&& other) noexcept
    : m_res(other.m_res), m_currentRow(other.m_currentRow) {
    other.m_res = nullptr;
    other.m_currentRow = -1;
}

PostgreSQLResultSet& PostgreSQLResultSet::operator=(PostgreSQLResultSet&& other) noexcept {
    if (this != &other) {
        if (m_res) {
            PQclear(m_res);
        }
        m_res = other.m_res;
        m_currentRow = other.m_currentRow;
        other.m_res = nullptr;
        other.m_currentRow = -1;
    }
    return *this;
}

bool PostgreSQLResultSet::isOk() const {
    if (!m_res) return false;
    ExecStatusType status = PQresultStatus(m_res);
    return status == PGRES_COMMAND_OK || status == PGRES_TUPLES_OK;
}

bool PostgreSQLResultSet::hasData() const {
    return m_res && PQresultStatus(m_res) == PGRES_TUPLES_OK;
}

ExecStatusType PostgreSQLResultSet::status() const {
    return m_res ? PQresultStatus(m_res) : PGRES_FATAL_ERROR;
}

const char* PostgreSQLResultSet::statusMessage() const {
    return m_res ? PQresStatus(PQresultStatus(m_res)) : "No result";
}

const char* PostgreSQLResultSet::errorMessage() const {
    return m_res ? PQresultErrorMessage(m_res) : "No result";
}

int PostgreSQLResultSet::numFields() const {
    return m_res ? PQnfields(m_res) : 0;
}

int PostgreSQLResultSet::numRows() const {
    return m_res ? PQntuples(m_res) : 0;
}

const char* PostgreSQLResultSet::getValue(int row, int col) const {
    if (!m_res) return nullptr;
    if (row < 0 || row >= numRows()) return nullptr;
    if (col < 0 || col >= numFields()) return nullptr;
    if (PQgetisnull(m_res, row, col)) return nullptr;
    return PQgetvalue(m_res, row, col);
}

bool PostgreSQLResultSet::isNull(int row, int col) const {
    if (!m_res) return true;
    if (row < 0 || row >= numRows()) return true;
    if (col < 0 || col >= numFields()) return true;
    return PQgetisnull(m_res, row, col) != 0;
}

int PostgreSQLResultSet::getLength(int row, int col) const {
    if (!m_res) return 0;
    if (row < 0 || row >= numRows()) return 0;
    if (col < 0 || col >= numFields()) return 0;
    return PQgetlength(m_res, row, col);
}

const char* PostgreSQLResultSet::fieldName(int col) const {
    if (!m_res || col < 0 || col >= numFields()) return nullptr;
    return PQfname(m_res, col);
}

Oid PostgreSQLResultSet::fieldType(int col) const {
    if (!m_res || col < 0 || col >= numFields()) return InvalidOid;
    return PQftype(m_res, col);
}

std::vector<std::string> PostgreSQLResultSet::getColumnNames() const {
    std::vector<std::string> names;
    if (!m_res) return names;

    int nFields = PQnfields(m_res);
    names.reserve(nFields);

    for (int i = 0; i < nFields; ++i) {
        const char* name = PQfname(m_res, i);
        names.emplace_back(name ? name : "");
    }

    return names;
}

bool PostgreSQLResultSet::fetchRow() {
    if (!m_res || !hasData()) return false;

    m_currentRow++;
    return m_currentRow < numRows();
}

const char* PostgreSQLResultSet::getField(int col) const {
    return getValue(m_currentRow, col);
}

bool PostgreSQLResultSet::isFieldNull(int col) const {
    return isNull(m_currentRow, col);
}

void PostgreSQLResultSet::reset(PGresult* res) {
    if (m_res) {
        PQclear(m_res);
    }
    m_res = res;
    m_currentRow = -1;
}

PGresult* PostgreSQLResultSet::release() {
    PGresult* res = m_res;
    m_res = nullptr;
    m_currentRow = -1;
    return res;
}

}  // namespace sqlfuse
