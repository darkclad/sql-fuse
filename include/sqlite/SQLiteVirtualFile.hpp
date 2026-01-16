#pragma once

/**
 * @file SQLiteVirtualFile.hpp
 * @brief SQLite-specific virtual file implementation for FUSE filesystem.
 *
 * This file provides the VirtualFile implementation for SQLite databases,
 * handling read and write operations for tables, views, and other database
 * objects exposed through the FUSE filesystem.
 */

#include "VirtualFile.hpp"
#include "SQLiteConnectionPool.hpp"

namespace sqlfuse {

/**
 * @class SQLiteVirtualFile
 * @brief VirtualFile implementation for SQLite database objects.
 *
 * Provides SQLite-specific implementations for generating file content
 * (CSV, JSON, SQL) from database objects and handling write operations
 * (INSERT, UPDATE, DELETE) through the filesystem interface.
 *
 * File Types Supported:
 * - Table data: employees.csv, employees.json
 * - Row data: employees/rows/1.json (individual row by primary key or rowid)
 * - View data: v_summary.csv, v_summary.json
 * - DDL: employees.sql
 *
 * Write Operations:
 * - Writing to table files (CSV/JSON) inserts new rows
 * - Writing to row files updates the specific row
 * - Deleting row files removes the row from the table
 *
 * SQLite-Specific Notes:
 * - Uses rowid for tables without explicit primary keys
 * - All SQL operations use double-quote identifier escaping
 * - String escaping uses doubled single quotes
 *
 * @see VirtualFile for the base class interface
 * @see SQLiteFormatConverter for data conversion utilities
 */
class SQLiteVirtualFile : public VirtualFile {
public:
    /**
     * @brief Construct an SQLite virtual file.
     * @param path Parsed path identifying the database object.
     * @param schema Schema manager for metadata queries.
     * @param cache Cache manager for content caching.
     * @param config Data configuration options (row limits, formatting, etc.).
     */
    SQLiteVirtualFile(const ParsedPath& path,
                      SchemaManager& schema,
                      CacheManager& cache,
                      const DataConfig& config);

    ~SQLiteVirtualFile() override = default;

protected:
    // ----- Content generation methods -----
    // These override VirtualFile methods to provide SQLite-specific implementations

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
     * SQLite's dynamic typing means type inference is best-effort.
     */
    std::string generateTableJSON() override;

    /**
     * @brief Generate JSON content for a single row.
     * @return JSON object for the row identified by m_path.row_id.
     *
     * Uses primary key or rowid for lookup.
     */
    std::string generateRowJSON() override;

    /**
     * @brief Generate content for a view (CSV, JSON, or SQL).
     * @return View data in requested format, or CREATE VIEW statement for SQL.
     */
    std::string generateViewContent() override;

    /**
     * @brief Generate database information.
     * @return Text describing the database with table counts and file size.
     */
    std::string generateDatabaseInfo() override;

    /**
     * @brief Generate user information (not applicable to SQLite).
     * @return Text indicating SQLite has no user management.
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
     * If the row exists (by primary key/rowid), updates it; otherwise inserts.
     */
    int handleRowWrite() override;

private:
    /**
     * @brief Get the SQLite connection pool from the schema manager.
     * @return Pointer to SQLiteConnectionPool, or nullptr if wrong type.
     */
    SQLiteConnectionPool* getPool();
};

}  // namespace sqlfuse
