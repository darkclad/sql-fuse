#pragma once

#include <libpq-fe.h>
#include <string>
#include <vector>
#include <cstdint>

namespace sqlfuse {

class PostgreSQLResultSet {
public:
    explicit PostgreSQLResultSet(PGresult* res = nullptr);
    ~PostgreSQLResultSet();

    PostgreSQLResultSet(const PostgreSQLResultSet&) = delete;
    PostgreSQLResultSet& operator=(const PostgreSQLResultSet&) = delete;

    PostgreSQLResultSet(PostgreSQLResultSet&& other) noexcept;
    PostgreSQLResultSet& operator=(PostgreSQLResultSet&& other) noexcept;

    PGresult* get() const { return m_res; }
    operator bool() const { return m_res != nullptr && PQresultStatus(m_res) == PGRES_TUPLES_OK; }

    // Check various result statuses
    bool isOk() const;
    bool hasData() const;
    ExecStatusType status() const;
    const char* statusMessage() const;
    const char* errorMessage() const;

    // Row/column access
    int numFields() const;
    int numRows() const;

    // Get value at specific row/column
    const char* getValue(int row, int col) const;
    bool isNull(int row, int col) const;
    int getLength(int row, int col) const;

    // Column metadata
    const char* fieldName(int col) const;
    Oid fieldType(int col) const;
    std::vector<std::string> getColumnNames() const;

    // Iterator-like access for compatibility with MySQL-style row fetching
    bool fetchRow();  // Advances current row, returns false when no more rows
    const char* getField(int col) const;  // Get field from current row
    bool isFieldNull(int col) const;  // Check if field in current row is null
    int currentRow() const { return m_currentRow; }

    void reset(PGresult* res = nullptr);
    PGresult* release();

private:
    PGresult* m_res;
    int m_currentRow = -1;  // For iterator-style access
};

}  // namespace sqlfuse
