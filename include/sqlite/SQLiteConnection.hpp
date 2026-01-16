#pragma once

/**
 * @file SQLiteConnection.hpp
 * @brief RAII wrapper for an SQLite database connection.
 *
 * This class provides a safe wrapper for SQLite database connections,
 * handling automatic cleanup when the connection goes out of scope.
 * Unlike MySQL and Oracle, SQLite connections are typically not pooled
 * in the same way since SQLite is a file-based database.
 */

#include <sqlite3.h>
#include <string>
#include <cstdint>

namespace sqlfuse {

/**
 * @class SQLiteConnection
 * @brief RAII wrapper for an SQLite database file connection.
 *
 * SQLiteConnection manages a connection to an SQLite database file.
 * The connection is automatically closed when the object is destroyed.
 *
 * SQLite Characteristics:
 * - File-based: One database per file
 * - Serverless: No separate server process
 * - Self-contained: Entire database in a single file
 * - Thread-safe with proper configuration (SQLITE_OPEN_FULLMUTEX)
 *
 * Usage:
 * @code
 *   SQLiteConnection conn("/path/to/database.db");
 *   if (conn.isValid()) {
 *       if (conn.execute("CREATE TABLE IF NOT EXISTS test (id INTEGER)")) {
 *           // Table created
 *       }
 *       auto stmt = conn.prepare("SELECT * FROM test");
 *       // Use statement...
 *   }
 * @endcode
 *
 * Thread Safety:
 * - SQLite supports multiple concurrent readers but only one writer
 * - Connections are opened with SQLITE_OPEN_FULLMUTEX for serialized mode
 */
class SQLiteConnection {
public:
    /**
     * @brief Open a connection to an SQLite database file.
     * @param dbPath Path to the SQLite database file.
     *
     * Creates the database file if it doesn't exist.
     * Uses SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX.
     */
    explicit SQLiteConnection(const std::string& dbPath);

    /**
     * @brief Destructor - closes the database connection.
     */
    ~SQLiteConnection();

    // Non-copyable
    SQLiteConnection(const SQLiteConnection&) = delete;
    SQLiteConnection& operator=(const SQLiteConnection&) = delete;

    // Movable
    SQLiteConnection(SQLiteConnection&& other) noexcept;
    SQLiteConnection& operator=(SQLiteConnection&& other) noexcept;

    /**
     * @brief Get the underlying sqlite3 handle.
     * @return Raw sqlite3* pointer (still owned by this object).
     */
    sqlite3* get() const { return m_db; }

    /**
     * @brief Check if the connection is valid and open.
     * @return true if the database handle is valid.
     */
    bool isValid() const { return m_db != nullptr; }

    /**
     * @brief Execute a SQL statement without returning results.
     * @param sql The SQL statement to execute.
     * @return true on success, false on error (check error() for details).
     *
     * Use this for DDL, INSERT, UPDATE, DELETE statements.
     * For SELECT statements, use prepare() and step through results.
     */
    bool execute(const std::string& sql);

    /**
     * @brief Prepare a SQL statement for execution.
     * @param sql The SQL statement to prepare.
     * @return sqlite3_stmt* on success (caller must finalize), nullptr on error.
     *
     * The caller is responsible for finalizing the statement, typically
     * by wrapping it in SQLiteResultSet.
     */
    sqlite3_stmt* prepare(const std::string& sql);

    /**
     * @brief Get the last SQLite error message.
     * @return Error description from the last failed operation.
     */
    const char* error() const;

    /**
     * @brief Get the last SQLite error code.
     * @return SQLite error code (SQLITE_OK = 0, SQLITE_ERROR = 1, etc.).
     */
    int errorCode() const;

    /**
     * @brief Get the rowid of the last inserted row.
     * @return Last insert rowid, or 0 if no inserts performed.
     */
    int64_t lastInsertRowId() const;

    /**
     * @brief Get the number of rows changed by the last statement.
     * @return Number of rows inserted, updated, or deleted.
     */
    int changes() const;

private:
    sqlite3* m_db = nullptr;  ///< SQLite database handle
    std::string m_path;       ///< Path to database file
};

}  // namespace sqlfuse
