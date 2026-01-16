#pragma once

/**
 * @file SQLiteResultSet.hpp
 * @brief RAII wrapper for SQLite prepared statement results.
 *
 * This file provides a high-level interface for iterating through SQLite
 * query results, handling automatic cleanup of sqlite3_stmt resources and
 * providing convenient access to column values.
 */

#include <sqlite3.h>
#include <string>

namespace sqlfuse {

/**
 * @class SQLiteResultSet
 * @brief RAII wrapper for SQLite prepared statement results.
 *
 * SQLiteResultSet manages a sqlite3_stmt handle, providing methods to
 * step through rows and access column values. The statement is automatically
 * finalized when the wrapper is destroyed.
 *
 * SQLite Result Iteration:
 * Unlike MySQL/PostgreSQL which separate query execution from result retrieval,
 * SQLite uses step() to both execute and fetch rows. Each call to step()
 * advances to the next row (or completes the statement for DML).
 *
 * Usage:
 * @code
 *   auto stmt = conn.prepare("SELECT id, name FROM employees");
 *   SQLiteResultSet result(stmt);
 *   while (result.step()) {
 *       int64_t id = result.getInt64(0);
 *       std::string name = result.getString(1);
 *       // Process row...
 *   }
 * @endcode
 *
 * Thread Safety:
 * - Not thread-safe; each thread should have its own result set.
 */
class SQLiteResultSet {
public:
    /**
     * @brief Construct a result set wrapper.
     * @param stmt sqlite3_stmt handle to manage (takes ownership), or nullptr.
     */
    explicit SQLiteResultSet(sqlite3_stmt* stmt = nullptr);

    /**
     * @brief Destructor - finalizes the statement if still owned.
     */
    ~SQLiteResultSet();

    // Non-copyable
    SQLiteResultSet(const SQLiteResultSet&) = delete;
    SQLiteResultSet& operator=(const SQLiteResultSet&) = delete;

    // Movable
    SQLiteResultSet(SQLiteResultSet&& other) noexcept;
    SQLiteResultSet& operator=(SQLiteResultSet&& other) noexcept;

    /**
     * @brief Get the underlying sqlite3_stmt handle.
     * @return Raw sqlite3_stmt* pointer (still owned by this object).
     */
    sqlite3_stmt* get() const { return m_stmt; }

    /**
     * @brief Boolean conversion - true if statement is valid.
     */
    operator bool() const { return m_stmt != nullptr; }

    /**
     * @brief Step to the next row.
     * @return true if a row is available, false if done or error.
     *
     * Returns true when sqlite3_step() returns SQLITE_ROW.
     * Returns false when SQLITE_DONE (no more rows) or on error.
     */
    bool step();

    /**
     * @brief Get the number of columns in the result.
     * @return Column count.
     */
    int columnCount() const;

    /**
     * @brief Get a column name by index.
     * @param index Zero-based column index.
     * @return Column name.
     */
    std::string columnName(int index) const;

    /**
     * @brief Get a column value as a string.
     * @param index Zero-based column index.
     * @return String value, or empty string if NULL.
     *
     * SQLite's dynamic typing means any column can be retrieved as text.
     */
    std::string getString(int index) const;

    /**
     * @brief Get a column value as a 64-bit integer.
     * @param index Zero-based column index.
     * @return Integer value, or 0 if NULL or non-numeric.
     */
    int64_t getInt64(int index) const;

    /**
     * @brief Check if a column value is NULL.
     * @param index Zero-based column index.
     * @return true if the column value is NULL.
     */
    bool isNull(int index) const;

    /**
     * @brief Reset the statement for re-execution.
     *
     * Resets the statement so it can be stepped through again.
     * Bound parameters are retained.
     */
    void reset();

    /**
     * @brief Finalize the statement and release resources.
     *
     * After calling finalize(), the statement cannot be used.
     * This is called automatically by the destructor.
     */
    void finalize();

private:
    sqlite3_stmt* m_stmt;  ///< SQLite prepared statement handle (owned)
};

}  // namespace sqlfuse
