#pragma once

/**
 * @file OracleVirtualFile.hpp
 * @brief Oracle-specific virtual file implementation for FUSE filesystem.
 *
 * This file provides the VirtualFile implementation for Oracle databases,
 * handling read and write operations for tables, views, and other database
 * objects exposed through the FUSE filesystem.
 */

#include "VirtualFile.hpp"
#include "OracleConnectionPool.hpp"

namespace sqlfuse {

/**
 * @class OracleVirtualFile
 * @brief VirtualFile implementation for Oracle database objects.
 *
 * Provides Oracle-specific implementations for generating file content
 * (CSV, JSON, SQL) from database objects and handling write operations
 * (INSERT, UPDATE, DELETE) through the filesystem interface.
 *
 * File Types Supported:
 * - Table data: EMPLOYEES.csv, EMPLOYEES.json
 * - Row data: EMPLOYEES/rows/1.json (individual row by primary key)
 * - View data: V_EMPLOYEE_DETAILS.csv, V_EMPLOYEE_DETAILS.json
 * - DDL: EMPLOYEES.sql, GET_EMPLOYEE_COUNT.sql
 *
 * Write Operations:
 * - Writing to table files (CSV/JSON) inserts new rows
 * - Writing to row files updates the specific row
 * - Deleting row files removes the row from the table
 *
 * All SQL operations use proper Oracle escaping:
 * - Double quotes for identifiers: "SCHEMA"."TABLE"
 * - Doubled single quotes for strings: 'O''Brien'
 *
 * @see VirtualFile for the base class interface
 * @see OracleFormatConverter for data conversion utilities
 */
class OracleVirtualFile : public VirtualFile {
public:
    /**
     * @brief Construct an Oracle virtual file.
     * @param path Parsed path identifying the database object.
     * @param schema Schema manager for metadata queries.
     * @param cache Cache manager for content caching.
     * @param config Data configuration options (row limits, formatting, etc.).
     */
    OracleVirtualFile(const ParsedPath& path,
                     SchemaManager& schema,
                     CacheManager& cache,
                     const DataConfig& config);

    ~OracleVirtualFile() override = default;

protected:
    // ----- Content generation methods -----
    // These override VirtualFile methods to provide Oracle-specific implementations

    /**
     * @brief Generate CSV content for a table.
     * @return CSV string with all rows from the table.
     *
     * Uses Oracle's FETCH FIRST N ROWS ONLY for row limiting.
     */
    std::string generateTableCSV() override;

    /**
     * @brief Generate JSON content for a table.
     * @return JSON array containing all rows as objects.
     *
     * Numeric types are preserved as JSON numbers.
     */
    std::string generateTableJSON() override;

    /**
     * @brief Generate JSON content for a single row.
     * @return JSON object for the row identified by m_path.row_id.
     *
     * Uses primary key lookup to fetch the specific row.
     */
    std::string generateRowJSON() override;

    /**
     * @brief Generate content for a view (CSV, JSON, or SQL).
     * @return View data in requested format, or CREATE VIEW statement for SQL.
     */
    std::string generateViewContent() override;

    /**
     * @brief Generate schema/database information.
     * @return Text describing the schema with object counts.
     */
    std::string generateDatabaseInfo() override;

    /**
     * @brief Generate user account information.
     * @return Text with user details from DBA_USERS or ALL_USERS.
     */
    std::string generateUserInfo() override;

    // ----- Write operation handlers -----

    /**
     * @brief Handle writes to table data files (bulk insert).
     * @return 0 on success, negative errno on failure.
     *
     * Parses the write buffer as CSV or JSON and inserts all rows.
     */
    int handleTableWrite() override;

    /**
     * @brief Handle writes to individual row files (insert/update).
     * @return 0 on success, negative errno on failure.
     *
     * If the row exists (by primary key), updates it; otherwise inserts.
     */
    int handleRowWrite() override;

private:
    /**
     * @brief Get the Oracle connection pool from the schema manager.
     * @return Pointer to OracleConnectionPool, or nullptr if wrong type.
     */
    OracleConnectionPool* getPool();
};

}  // namespace sqlfuse
