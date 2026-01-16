#pragma once

#include <sqlite3.h>
#include <string>

namespace sqlfuse {

class SQLiteResultSet {
public:
    explicit SQLiteResultSet(sqlite3_stmt* stmt = nullptr);
    ~SQLiteResultSet();

    SQLiteResultSet(const SQLiteResultSet&) = delete;
    SQLiteResultSet& operator=(const SQLiteResultSet&) = delete;

    SQLiteResultSet(SQLiteResultSet&& other) noexcept;
    SQLiteResultSet& operator=(SQLiteResultSet&& other) noexcept;

    sqlite3_stmt* get() const { return m_stmt; }
    operator bool() const { return m_stmt != nullptr; }

    // Step to next row, returns true if row available
    bool step();

    // Get column count
    int columnCount() const;

    // Get column name
    std::string columnName(int index) const;

    // Get column value as string (handles NULL)
    std::string getString(int index) const;

    // Get column value as int64
    int64_t getInt64(int index) const;

    // Check if column is NULL
    bool isNull(int index) const;

    void reset();
    void finalize();

private:
    sqlite3_stmt* m_stmt;
};

}  // namespace sqlfuse
