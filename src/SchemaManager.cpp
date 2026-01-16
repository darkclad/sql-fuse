#include "SchemaManager.hpp"
#include <stdexcept>
#include <algorithm>

#ifdef WITH_MYSQL
#include "MySQLSchemaManager.hpp"
#endif

#ifdef WITH_SQLITE
#include "SQLiteSchemaManager.hpp"
#endif

namespace sqlfuse {

DatabaseType parseDatabaseType(const std::string& type) {
    std::string lower = type;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower == "mysql" || lower == "mariadb") {
#ifdef WITH_MYSQL
        return DatabaseType::MySQL;
#else
        throw std::runtime_error("MySQL support not compiled in. Rebuild with -DWITH_MYSQL=ON");
#endif
    } else if (lower == "sqlite" || lower == "sqlite3") {
#ifdef WITH_SQLITE
        return DatabaseType::SQLite;
#else
        throw std::runtime_error("SQLite support not compiled in. Rebuild with -DWITH_SQLITE=ON");
#endif
    } else if (lower == "postgresql" || lower == "postgres" || lower == "pgsql") {
        return DatabaseType::PostgreSQL;
    } else if (lower == "oracle") {
        return DatabaseType::Oracle;
    }

    throw std::invalid_argument("Unknown database type: " + type);
}

std::string databaseTypeToString(DatabaseType type) {
    switch (type) {
        case DatabaseType::MySQL:
            return "mysql";
        case DatabaseType::SQLite:
            return "sqlite";
        case DatabaseType::PostgreSQL:
            return "postgresql";
        case DatabaseType::Oracle:
            return "oracle";
        default:
            return "unknown";
    }
}

// Note: The factory method is now implemented in sql_fuse_fs.cpp because it needs
// access to the appropriate connection pool type for each database backend.
// This file only contains the type parsing and conversion utilities.

}  // namespace sqlfuse
