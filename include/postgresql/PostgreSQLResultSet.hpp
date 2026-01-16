#pragma once

/**
 * @file PostgreSQLResultSet.hpp
 * @brief RAII wrapper for PostgreSQL query results.
 *
 * This file provides a high-level interface for working with PostgreSQL
 * query results (PGresult*), handling automatic cleanup and providing
 * convenient access to column values and metadata.
 */

#include <libpq-fe.h>
#include <string>
#include <vector>
#include <cstdint>

namespace sqlfuse {

/**
 * @class PostgreSQLResultSet
 * @brief RAII wrapper for PostgreSQL query results.
 *
 * PostgreSQLResultSet manages a PGresult* handle, providing methods to
 * access row/column data and metadata. The result is automatically
 * cleared (PQclear) when the wrapper is destroyed.
 *
 * PostgreSQL Result Access:
 * Unlike MySQL's row-by-row fetching, PostgreSQL loads the entire result
 * into memory at once. This class provides two access patterns:
 * 1. Direct access: getValue(row, col) for random access
 * 2. Iterator-style: fetchRow() + getField(col) for sequential access
 *
 * Result Status:
 * - PGRES_TUPLES_OK: SELECT query with data
 * - PGRES_COMMAND_OK: DML/DDL completed successfully
 * - PGRES_EMPTY_QUERY: Empty query string
 * - PGRES_FATAL_ERROR: Error occurred
 *
 * Usage:
 * @code
 *   auto conn = pool.acquire();
 *   PostgreSQLResultSet result(conn->execute("SELECT id, name FROM employees"));
 *   if (result.hasData()) {
 *       for (int row = 0; row < result.numRows(); ++row) {
 *           std::string name = result.getValue(row, 1);
 *           // Process row...
 *       }
 *   }
 * @endcode
 *
 * Thread Safety:
 * - Not thread-safe; each thread should have its own result set.
 */
class PostgreSQLResultSet {
public:
    /**
     * @brief Construct a result set wrapper.
     * @param res PGresult handle to manage (takes ownership), or nullptr.
     */
    explicit PostgreSQLResultSet(PGresult* res = nullptr);

    /**
     * @brief Destructor - clears the result if still owned.
     */
    ~PostgreSQLResultSet();

    // Non-copyable
    PostgreSQLResultSet(const PostgreSQLResultSet&) = delete;
    PostgreSQLResultSet& operator=(const PostgreSQLResultSet&) = delete;

    // Movable
    PostgreSQLResultSet(PostgreSQLResultSet&& other) noexcept;
    PostgreSQLResultSet& operator=(PostgreSQLResultSet&& other) noexcept;

    /**
     * @brief Get the underlying PGresult handle.
     * @return Raw PGresult* pointer (still owned by this object).
     */
    PGresult* get() const { return m_res; }

    /**
     * @brief Boolean conversion - true if result has data.
     *
     * Returns true only if result is valid AND status is PGRES_TUPLES_OK.
     */
    operator bool() const { return m_res != nullptr && PQresultStatus(m_res) == PGRES_TUPLES_OK; }

    // ----- Status checking -----

    /**
     * @brief Check if the result status indicates success.
     * @return true if status is PGRES_TUPLES_OK or PGRES_COMMAND_OK.
     */
    bool isOk() const;

    /**
     * @brief Check if the result contains data rows.
     * @return true if status is PGRES_TUPLES_OK.
     *
     * Note: A SELECT with no matching rows still returns TUPLES_OK
     * but with numRows() == 0.
     */
    bool hasData() const;

    /**
     * @brief Get the result status code.
     * @return ExecStatusType enum value.
     */
    ExecStatusType status() const;

    /**
     * @brief Get the result status as a string.
     * @return Status string (e.g., "PGRES_TUPLES_OK").
     */
    const char* statusMessage() const;

    /**
     * @brief Get the error message if the query failed.
     * @return Error message string, or empty if no error.
     */
    const char* errorMessage() const;

    // ----- Row and column counts -----

    /**
     * @brief Get the number of columns in the result.
     * @return Column count.
     */
    int numFields() const;

    /**
     * @brief Get the number of rows in the result.
     * @return Row count.
     */
    int numRows() const;

    // ----- Direct value access -----

    /**
     * @brief Get a value at a specific row and column.
     * @param row Zero-based row index.
     * @param col Zero-based column index.
     * @return Value as C string, or empty string if NULL.
     */
    const char* getValue(int row, int col) const;

    /**
     * @brief Check if a value is NULL.
     * @param row Zero-based row index.
     * @param col Zero-based column index.
     * @return true if the value is NULL.
     */
    bool isNull(int row, int col) const;

    /**
     * @brief Get the length of a value in bytes.
     * @param row Zero-based row index.
     * @param col Zero-based column index.
     * @return Length in bytes (0 for NULL).
     */
    int getLength(int row, int col) const;

    // ----- Column metadata -----

    /**
     * @brief Get a column name by index.
     * @param col Zero-based column index.
     * @return Column name.
     */
    const char* fieldName(int col) const;

    /**
     * @brief Get a column's type Oid.
     * @param col Zero-based column index.
     * @return PostgreSQL type Oid.
     *
     * Common Oids: 16=bool, 23=int4, 25=text, 1043=varchar
     */
    Oid fieldType(int col) const;

    /**
     * @brief Get all column names as a vector.
     * @return Vector of column name strings.
     */
    std::vector<std::string> getColumnNames() const;

    // ----- Iterator-style access (MySQL compatibility) -----

    /**
     * @brief Advance to the next row.
     * @return true if a row is available, false when past last row.
     *
     * Call before accessing fields with getField(). The first call
     * positions on row 0.
     */
    bool fetchRow();

    /**
     * @brief Get a field from the current row.
     * @param col Zero-based column index.
     * @return Field value as C string, or empty if NULL.
     *
     * Must call fetchRow() first to position on a valid row.
     */
    const char* getField(int col) const;

    /**
     * @brief Check if a field in the current row is NULL.
     * @param col Zero-based column index.
     * @return true if the field is NULL.
     */
    bool isFieldNull(int col) const;

    /**
     * @brief Get the current row index.
     * @return Zero-based row index, or -1 if not positioned.
     */
    int currentRow() const { return m_currentRow; }

    // ----- Resource management -----

    /**
     * @brief Reset the wrapper with a new result.
     * @param res New PGresult to manage (or nullptr).
     *
     * Clears the current result first if any.
     */
    void reset(PGresult* res = nullptr);

    /**
     * @brief Release ownership of the result.
     * @return Raw PGresult* pointer (caller takes ownership).
     *
     * After calling, this wrapper no longer owns the result.
     */
    PGresult* release();

private:
    PGresult* m_res;        ///< PostgreSQL result handle (owned)
    int m_currentRow = -1;  ///< Current row for iterator-style access
};

}  // namespace sqlfuse
