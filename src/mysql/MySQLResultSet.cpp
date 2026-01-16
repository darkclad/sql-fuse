#include "MySQLResultSet.hpp"

namespace sqlfuse {

MySQLResultSet::MySQLResultSet(MYSQL_RES* res) : m_res(res) {}

MySQLResultSet::~MySQLResultSet() {
    if (m_res) {
        mysql_free_result(m_res);
    }
}

MySQLResultSet::MySQLResultSet(MySQLResultSet&& other) noexcept : m_res(other.m_res) {
    other.m_res = nullptr;
}

MySQLResultSet& MySQLResultSet::operator=(MySQLResultSet&& other) noexcept {
    if (this != &other) {
        if (m_res) {
            mysql_free_result(m_res);
        }
        m_res = other.m_res;
        other.m_res = nullptr;
    }
    return *this;
}

MYSQL_ROW MySQLResultSet::fetchRow() {
    return m_res ? mysql_fetch_row(m_res) : nullptr;
}

unsigned int MySQLResultSet::numFields() const {
    return m_res ? mysql_num_fields(m_res) : 0;
}

uint64_t MySQLResultSet::numRows() const {
    return m_res ? mysql_num_rows(m_res) : 0;
}

MYSQL_FIELD* MySQLResultSet::fetchFields() const {
    return m_res ? mysql_fetch_fields(m_res) : nullptr;
}

std::vector<std::string> MySQLResultSet::getColumnNames() const {
    std::vector<std::string> names;
    if (!m_res) return names;

    unsigned int numFields = mysql_num_fields(m_res);
    MYSQL_FIELD* fields = mysql_fetch_fields(m_res);

    names.reserve(numFields);
    for (unsigned int i = 0; i < numFields; ++i) {
        names.emplace_back(fields[i].name);
    }

    return names;
}

void MySQLResultSet::reset(MYSQL_RES* res) {
    if (m_res) {
        mysql_free_result(m_res);
    }
    m_res = res;
}

MYSQL_RES* MySQLResultSet::release() {
    MYSQL_RES* res = m_res;
    m_res = nullptr;
    return res;
}

}  // namespace sqlfuse
