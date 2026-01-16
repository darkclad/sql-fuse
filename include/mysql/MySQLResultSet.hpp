#pragma once

/**
 * @file MySQLResultSet.hpp
 * @brief RAII wrapper for MySQL query result sets.
 *
 * This file provides a high-level interface for managing MySQL query results,
 * handling automatic cleanup of MYSQL_RES resources and providing convenient
 * access to rows and field metadata.
 */

#include <mysql/mysql.h>
#include <string>
#include <vector>

namespace sqlfuse {

/**
 * @class MySQLResultSet
 * @brief RAII wrapper for MySQL SELECT query results.
 *
 * MySQLResultSet manages a MYSQL_RES handle returned by mysql_store_result()
 * or mysql_use_result(). The result set is automatically freed when the
 * wrapper is destroyed.
 *
 * Usage:
 * @code
 *   auto conn = pool.acquire();
 *   if (conn->query("SELECT id, name FROM employees")) {
 *       MySQLResultSet result(conn->storeResult());
 *       MYSQL_ROW row;
 *       while ((row = result.fetchRow())) {
 *           const char* id = row[0];
 *           const char* name = row[1];
 *           // Process row...
 *       }
 *   }
 * @endcode
 *
 * Thread Safety:
 * - Not thread-safe; each thread should have its own result set.
 * - For results from mysql_use_result(), the connection cannot be used
 *   for other queries until all rows are fetched.
 */
class MySQLResultSet {
public:
    /**
     * @brief Construct a result set wrapper.
     * @param res MYSQL_RES handle to manage (takes ownership), or nullptr.
     */
    explicit MySQLResultSet(MYSQL_RES* res = nullptr);

    /**
     * @brief Destructor - frees the MYSQL_RES handle if still owned.
     */
    ~MySQLResultSet();

    // Non-copyable
    MySQLResultSet(const MySQLResultSet&) = delete;
    MySQLResultSet& operator=(const MySQLResultSet&) = delete;

    // Movable
    MySQLResultSet(MySQLResultSet&& other) noexcept;
    MySQLResultSet& operator=(MySQLResultSet&& other) noexcept;

    /**
     * @brief Get the underlying MYSQL_RES handle.
     * @return Raw MYSQL_RES* pointer (still owned by this object).
     */
    MYSQL_RES* get() const { return m_res; }

    /**
     * @brief Boolean conversion - true if result set is valid.
     */
    operator bool() const { return m_res != nullptr; }

    /**
     * @brief Fetch the next row from the result set.
     * @return MYSQL_ROW (array of char*) for the next row, or nullptr if done.
     *
     * Each call advances to the next row. NULL values in the result
     * are represented as nullptr entries in the returned array.
     */
    MYSQL_ROW fetchRow();

    /**
     * @brief Get the number of columns in the result set.
     * @return Column count.
     */
    unsigned int numFields() const;

    /**
     * @brief Get the number of rows in the result set.
     * @return Row count (only accurate for mysql_store_result results).
     *
     * For results from mysql_use_result(), this returns 0 until all
     * rows have been fetched.
     */
    uint64_t numRows() const;

    /**
     * @brief Get field metadata for all columns.
     * @return Array of MYSQL_FIELD structures.
     */
    MYSQL_FIELD* fetchFields() const;

    /**
     * @brief Get all column names as a vector.
     * @return Vector of column names in order.
     */
    std::vector<std::string> getColumnNames() const;

    /**
     * @brief Replace the managed result set.
     * @param res New MYSQL_RES handle (takes ownership), or nullptr to clear.
     *
     * Frees the current result set if one exists.
     */
    void reset(MYSQL_RES* res = nullptr);

    /**
     * @brief Release ownership of the MYSQL_RES handle.
     * @return MYSQL_RES* pointer; caller is responsible for freeing.
     *
     * After calling release(), this object no longer owns the result set.
     */
    MYSQL_RES* release();

private:
    MYSQL_RES* m_res;  ///< MySQL result set handle (owned)
};

}  // namespace sqlfuse
