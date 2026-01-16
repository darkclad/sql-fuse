#include "SQLiteConnection.hpp"
#include <spdlog/spdlog.h>

namespace sqlfuse {

SQLiteConnection::SQLiteConnection(const std::string& dbPath) : m_path(dbPath) {
    int rc = sqlite3_open_v2(dbPath.c_str(), &m_db,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                             nullptr);
    if (rc != SQLITE_OK) {
        spdlog::error("Failed to open SQLite database '{}': {}", dbPath,
                      m_db ? sqlite3_errmsg(m_db) : "unknown error");
        if (m_db) {
            sqlite3_close(m_db);
            m_db = nullptr;
        }
    }
}

SQLiteConnection::~SQLiteConnection() {
    if (m_db) {
        sqlite3_close(m_db);
    }
}

SQLiteConnection::SQLiteConnection(SQLiteConnection&& other) noexcept
    : m_db(other.m_db), m_path(std::move(other.m_path)) {
    other.m_db = nullptr;
}

SQLiteConnection& SQLiteConnection::operator=(SQLiteConnection&& other) noexcept {
    if (this != &other) {
        if (m_db) {
            sqlite3_close(m_db);
        }
        m_db = other.m_db;
        m_path = std::move(other.m_path);
        other.m_db = nullptr;
    }
    return *this;
}

bool SQLiteConnection::execute(const std::string& sql) {
    char* errMsg = nullptr;
    int rc = sqlite3_exec(m_db, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        spdlog::error("SQLite exec failed: {}", errMsg ? errMsg : "unknown");
        if (errMsg) sqlite3_free(errMsg);
        return false;
    }
    return true;
}

sqlite3_stmt* SQLiteConnection::prepare(const std::string& sql) {
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        spdlog::error("SQLite prepare failed: {}", sqlite3_errmsg(m_db));
        return nullptr;
    }
    return stmt;
}

const char* SQLiteConnection::error() const {
    return m_db ? sqlite3_errmsg(m_db) : "no connection";
}

int SQLiteConnection::errorCode() const {
    return m_db ? sqlite3_errcode(m_db) : SQLITE_ERROR;
}

int64_t SQLiteConnection::lastInsertRowId() const {
    return m_db ? sqlite3_last_insert_rowid(m_db) : 0;
}

int SQLiteConnection::changes() const {
    return m_db ? sqlite3_changes(m_db) : 0;
}

}  // namespace sqlfuse
