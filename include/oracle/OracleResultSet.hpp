#pragma once

#include <oci.h>
#include <string>
#include <vector>
#include <cstdint>

namespace sqlfuse {

class OracleResultSet {
public:
    OracleResultSet(OCIStmt* stmt, OCIError* err, OCIEnv* env);
    ~OracleResultSet();

    OracleResultSet(const OracleResultSet&) = delete;
    OracleResultSet& operator=(const OracleResultSet&) = delete;

    OracleResultSet(OracleResultSet&& other) noexcept;
    OracleResultSet& operator=(OracleResultSet&& other) noexcept;

    OCIStmt* get() const { return m_stmt; }
    operator bool() const { return m_stmt != nullptr && m_hasData; }

    // Check if result has data
    bool hasData() const { return m_hasData; }
    bool isOk() const { return !m_hasError; }
    const char* errorMessage() const { return m_errorMsg.c_str(); }

    // Fetch next row (returns false when no more rows)
    bool fetchRow();

    // Get value from current row
    const char* getValue(int col) const;
    bool isNull(int col) const;
    int getLength(int col) const;

    // Column metadata
    int numFields() const;
    int numRows() const;  // Only available after fetching all rows
    const char* fieldName(int col) const;
    int fieldType(int col) const;
    std::vector<std::string> getColumnNames() const;

    void reset();
    OCIStmt* release();

private:
    void describeColumns();
    void defineOutputVariables();

    OCIStmt* m_stmt;
    OCIError* m_err;
    OCIEnv* m_env;

    bool m_hasData = false;
    bool m_hasError = false;
    std::string m_errorMsg;
    int m_fetchedRows = 0;

    // Column metadata
    struct ColumnInfo {
        std::string name;
        ub2 type;
        ub4 size;
        sb2 precision;
        sb1 scale;
        // Output buffer
        std::vector<char> data;
        sb2 indicator;
        ub2 returnLen;
        OCIDefine* define = nullptr;
    };

    std::vector<ColumnInfo> m_columns;
    bool m_columnsDescribed = false;
};

}  // namespace sqlfuse
