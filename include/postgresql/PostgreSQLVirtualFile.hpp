#pragma once

/**
 * @file PostgreSQLVirtualFile.hpp
 * @brief PostgreSQL-specific virtual file implementation for FUSE filesystem.
 *
 * This file provides the VirtualFile implementation for PostgreSQL databases,
 * handling read and write operations for tables, views, and other database
 * objects exposed through the FUSE filesystem.
 */

#include "VirtualFile.hpp"
#include "PostgreSQLConnectionPool.hpp"

namespace sqlfuse {

/**
 * @class PostgreSQLVirtualFile
 * @brief VirtualFile implementation for PostgreSQL database objects.
 *
 * Provides PostgreSQL-specific implementations for generating file content
 * (CSV, JSON, SQL) from database objects and handling write operations
 * (INSERT, UPDATE, DELETE) through the filesystem interface.
 *
 * File Types Supported:
 * - Table data: employees.csv, employees.json
 * - Row data: employees/rows/1.json (individual row by primary key)
 * - View data: v_summary.csv, v_summary.json
 * - DDL: employees.sql
 *
 * Write Operations:
 * - Writing to table files (CSV/JSON) inserts new rows
 * - Writing to row files updates the specific row
 * - Deleting row files removes the row from the table
 *
 * PostgreSQL-Specific Notes:
 * - Uses primary key for row identification (CTID as fallback)
 * - All SQL operations use double-quote identifier escaping
 * - String escaping uses doubled single quotes
 * - Supports PostgreSQL-specific types (arrays, JSON, etc.)
 *
 * @see VirtualFile for the base class interface
 * @see PostgreSQLFormatConverter for data conversion utilities
 */
class PostgreSQLVirtualFile : public VirtualFile {
public:
    /**
     * @brief Construct a PostgreSQL virtual file.
     * @param path Parsed path identifying the database object.
     * @param schema Schema manager for metadata queries.
     * @param cache Cache manager for content caching.
     * @param config Data configuration options (row limits, formatting, etc.).
     */
    PostgreSQLVirtualFile(const ParsedPath& path,
                     SchemaManager& schema,
                     CacheManager& cache,
                     const DataConfig& config);

    ~PostgreSQLVirtualFile() override = default;

protected:
    // ----- Content generation methods -----
    // These override VirtualFile methods to provide PostgreSQL-specific implementations

    /**
     * @brief Generate CSV content for a table.
     * @return CSV string with all rows from the table.
     *
     * Uses LIMIT/OFFSET for pagination.
     */
    std::string generateTableCSV() override;

    /**
     * @brief Generate JSON content for a table.
     * @return JSON array containing all rows as objects.
     *
     * PostgreSQL's strong typing allows accurate type mapping.
     */
    std::string generateTableJSON() override;

    /**
     * @brief Generate JSON content for a single row.
     * @return JSON object for the row identified by m_path.row_id.
     *
     * Uses primary key for lookup.
     */
    std::string generateRowJSON() override;

    /**
     * @brief Generate content for a view (CSV, JSON, or SQL).
     * @return View data in requested format, or CREATE VIEW statement for SQL.
     */
    std::string generateViewContent() override;

    /**
     * @brief Generate database information.
     * @return Text describing the database with table counts and size.
     */
    std::string generateDatabaseInfo() override;

    /**
     * @brief Generate user/role information.
     * @return Text listing PostgreSQL roles and their attributes.
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
     * @brief Get the PostgreSQL connection pool from the schema manager.
     * @return Pointer to PostgreSQLConnectionPool, or nullptr if wrong type.
     */
    PostgreSQLConnectionPool* getPool();
};

}  // namespace sqlfuse
