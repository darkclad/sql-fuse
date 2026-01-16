#pragma once

#include <sqlite3.h>
#include <string>
#include <cstdint>

namespace sqlfuse {

class SQLiteConnection {
public:
    explicit SQLiteConnection(const std::string& dbPath);
    ~SQLiteConnection();

    // Non-copyable
    SQLiteConnection(const SQLiteConnection&) = delete;
    SQLiteConnection& operator=(const SQLiteConnection&) = delete;

    // Movable
    SQLiteConnection(SQLiteConnection&& other) noexcept;
    SQLiteConnection& operator=(SQLiteConnection&& other) noexcept;

    sqlite3* get() const { return m_db; }
    bool isValid() const { return m_db != nullptr; }

    // Execute query
    bool execute(const std::string& sql);

    // Prepare statement
    sqlite3_stmt* prepare(const std::string& sql);

    const char* error() const;
    int errorCode() const;
    int64_t lastInsertRowId() const;
    int changes() const;

private:
    sqlite3* m_db = nullptr;
    std::string m_path;
};

}  // namespace sqlfuse
