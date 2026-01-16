#pragma once

/**
 * @file OracleResultSet.hpp
 * @brief RAII wrapper for Oracle query result sets.
 *
 * This file provides a high-level interface for iterating through Oracle query
 * results, handling column metadata, data fetching, and type conversion from
 * OCI native types to string representations.
 */

#include <oci.h>
#include <string>
#include <vector>
#include <cstdint>

namespace sqlfuse {

/**
 * @class OracleResultSet
 * @brief RAII wrapper for Oracle SELECT query results.
 *
 * OracleResultSet manages an OCI statement handle after query execution,
 * providing methods to iterate through rows and access column values.
 * The statement handle is automatically freed when the result set is destroyed.
 *
 * All column values are fetched as strings (SQLT_STR) for simplicity,
 * with numeric type information preserved for JSON output where appropriate.
 *
 * Usage:
 * @code
 *   OCIStmt* stmt = conn->execute("SELECT id, name FROM employees");
 *   OracleResultSet result(stmt, conn->err(), conn->env());
 *
 *   while (result.fetchRow()) {
 *       const char* id = result.getValue(0);
 *       const char* name = result.getValue(1);
 *       // Process row...
 *   }
 * @endcode
 *
 * Thread Safety:
 * - Not thread-safe; each thread should have its own result set.
 * - The underlying connection should not be used while iterating.
 */
class OracleResultSet {
public:
    /**
     * @brief Construct a result set from an executed statement.
     * @param stmt OCI statement handle (takes ownership, will be freed on destruction).
     * @param err OCI error handle (borrowed, not freed).
     * @param env OCI environment handle (borrowed, not freed).
     *
     * Automatically describes columns and sets up output buffers for SELECT statements.
     * For non-SELECT statements, hasData() returns false.
     */
    OracleResultSet(OCIStmt* stmt, OCIError* err, OCIEnv* env);

    /**
     * @brief Destructor - frees the statement handle if still owned.
     */
    ~OracleResultSet();

    // Non-copyable
    OracleResultSet(const OracleResultSet&) = delete;
    OracleResultSet& operator=(const OracleResultSet&) = delete;

    // Movable
    OracleResultSet(OracleResultSet&& other) noexcept;
    OracleResultSet& operator=(OracleResultSet&& other) noexcept;

    /**
     * @brief Get the underlying OCI statement handle.
     * @return Raw OCIStmt pointer (still owned by this object).
     */
    OCIStmt* get() const { return m_stmt; }

    /**
     * @brief Boolean conversion - true if result set is valid and has data.
     */
    operator bool() const { return m_stmt != nullptr && m_hasData; }

    /**
     * @brief Check if this result set contains fetchable data (SELECT result).
     * @return true for SELECT statements, false for DML/DDL.
     */
    bool hasData() const { return m_hasData; }

    /**
     * @brief Check if the result set was created without errors.
     * @return true if no errors occurred during setup.
     */
    bool isOk() const { return !m_hasError; }

    /**
     * @brief Get the error message if isOk() returns false.
     * @return Error description or empty string if no error.
     */
    const char* errorMessage() const { return m_errorMsg.c_str(); }

    /**
     * @brief Fetch the next row from the result set.
     * @return true if a row was fetched, false if no more rows or error.
     *
     * After calling fetchRow(), use getValue() to access column data.
     * Continue calling until it returns false to iterate through all rows.
     */
    bool fetchRow();

    /**
     * @brief Get a column value from the current row.
     * @param col Zero-based column index.
     * @return Null-terminated string value, or nullptr if NULL or invalid index.
     *
     * The returned pointer is valid until the next fetchRow() call.
     */
    const char* getValue(int col) const;

    /**
     * @brief Check if a column value is NULL.
     * @param col Zero-based column index.
     * @return true if the value is NULL or index is invalid.
     */
    bool isNull(int col) const;

    /**
     * @brief Get the length of a column value in bytes.
     * @param col Zero-based column index.
     * @return String length, or 0 if NULL or invalid index.
     */
    int getLength(int col) const;

    /**
     * @brief Get the number of columns in the result set.
     * @return Column count.
     */
    int numFields() const;

    /**
     * @brief Get the number of rows fetched so far.
     * @return Cumulative count of rows fetched via fetchRow().
     *
     * @note This returns the count of fetched rows, not total rows in result.
     */
    int numRows() const;

    /**
     * @brief Get a column name by index.
     * @param col Zero-based column index.
     * @return Column name, or empty string if invalid index.
     */
    const char* fieldName(int col) const;

    /**
     * @brief Get the OCI data type of a column.
     * @param col Zero-based column index.
     * @return OCI type constant (SQLT_*), or 0 if invalid index.
     */
    int fieldType(int col) const;

    /**
     * @brief Get all column names as a vector.
     * @return Vector of column names in order.
     */
    std::vector<std::string> getColumnNames() const;

    /**
     * @brief Free the statement handle and reset state.
     *
     * After calling reset(), the result set cannot be used.
     */
    void reset();

    /**
     * @brief Release ownership of the statement handle.
     * @return OCIStmt pointer; caller is responsible for freeing.
     *
     * After calling release(), the result set no longer owns the statement.
     */
    OCIStmt* release();

private:
    /**
     * @brief Query column metadata from the statement.
     *
     * Called automatically in constructor for SELECT statements.
     */
    void describeColumns();

    /**
     * @brief Allocate output buffers and define OCI output variables.
     *
     * Sets up buffers for fetching column values as strings.
     */
    void defineOutputVariables();

    OCIStmt* m_stmt;      ///< OCI statement handle (owned)
    OCIError* m_err;      ///< OCI error handle (borrowed)
    OCIEnv* m_env;        ///< OCI environment handle (borrowed)

    bool m_hasData = false;     ///< True if SELECT with columns
    bool m_hasError = false;    ///< True if setup failed
    std::string m_errorMsg;     ///< Error message if m_hasError
    int m_fetchedRows = 0;      ///< Count of fetched rows

    /**
     * @struct ColumnInfo
     * @brief Metadata and buffer for a single result column.
     */
    struct ColumnInfo {
        std::string name;       ///< Column name
        ub2 type;               ///< OCI data type (SQLT_*)
        ub4 size;               ///< Maximum data size in bytes
        sb2 precision;          ///< Numeric precision
        sb1 scale;              ///< Numeric scale
        std::vector<char> data; ///< Output buffer for fetched value
        sb2 indicator;          ///< NULL indicator (-1 = NULL)
        ub2 returnLen;          ///< Actual length of fetched value
        OCIDefine* define = nullptr;  ///< OCI define handle for this column
    };

    std::vector<ColumnInfo> m_columns;  ///< Column metadata and buffers
    bool m_columnsDescribed = false;    ///< True after describeColumns()
};

}  // namespace sqlfuse
