#pragma once

#include <mysql/mysql.h>
#include <string>
#include <vector>

namespace sqlfuse {

class MySQLResultSet {
public:
    explicit MySQLResultSet(MYSQL_RES* res = nullptr);
    ~MySQLResultSet();

    MySQLResultSet(const MySQLResultSet&) = delete;
    MySQLResultSet& operator=(const MySQLResultSet&) = delete;

    MySQLResultSet(MySQLResultSet&& other) noexcept;
    MySQLResultSet& operator=(MySQLResultSet&& other) noexcept;

    MYSQL_RES* get() const { return m_res; }
    operator bool() const { return m_res != nullptr; }

    MYSQL_ROW fetchRow();
    unsigned int numFields() const;
    uint64_t numRows() const;
    MYSQL_FIELD* fetchFields() const;
    std::vector<std::string> getColumnNames() const;

    void reset(MYSQL_RES* res = nullptr);
    MYSQL_RES* release();

private:
    MYSQL_RES* m_res;
};

}  // namespace sqlfuse
