cmake_minimum_required(VERSION 3.16)
project(sql-fuse VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# Database backend options
option(WITH_MYSQL "Build with MySQL/MariaDB support" ON)
option(WITH_SQLITE "Build with SQLite support" ON)
option(WITH_POSTGRESQL "Build with PostgreSQL support (future)" OFF)
option(WITH_ORACLE "Build with Oracle support (future)" OFF)

# At least one database backend must be enabled
if(NOT WITH_MYSQL AND NOT WITH_SQLITE AND NOT WITH_POSTGRESQL AND NOT WITH_ORACLE)
    message(FATAL_ERROR "At least one database backend must be enabled")
endif()

# Find dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(FUSE3 REQUIRED fuse3)

# Database-specific dependencies
if(WITH_MYSQL)
    pkg_check_modules(MYSQL REQUIRED mysqlclient)
    message(STATUS "MySQL support: ENABLED")
else()
    message(STATUS "MySQL support: DISABLED")
endif()

if(WITH_SQLITE)
    pkg_check_modules(SQLITE3 REQUIRED sqlite3)
    message(STATUS "SQLite support: ENABLED")
else()
    message(STATUS "SQLite support: DISABLED")
endif()

if(WITH_POSTGRESQL)
    pkg_check_modules(PGSQL REQUIRED libpq)
    message(STATUS "PostgreSQL support: ENABLED (not yet implemented)")
else()
    message(STATUS "PostgreSQL support: DISABLED")
endif()

# Fetch header-only dependencies
include(FetchContent)

FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)

FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.13.0
    GIT_SHALLOW TRUE
)

FetchContent_Declare(cli11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.4.1
    GIT_SHALLOW TRUE
)

FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(json spdlog cli11)

# Only fetch googletest if building tests
option(BUILD_TESTS "Build tests" OFF)
if(BUILD_TESTS)
    FetchContent_MakeAvailable(googletest)
endif()

# Core source files (always included)
set(SOURCES
    src/main.cpp
    src/SQLFuseFS.cpp
    src/PathRouter.cpp
    src/CacheManager.cpp
    src/SchemaManager.cpp
    src/FormatConverter.cpp
    src/VirtualFile.cpp
    src/VirtualFileHandleManager.cpp
    src/ErrorHandler.cpp
    src/Config.cpp
)

# Database-specific source files
if(WITH_MYSQL)
    list(APPEND SOURCES
        src/mysql/MySQLConnection.cpp
        src/mysql/MySQLConnectionPool.cpp
        src/mysql/MySQLResultSet.cpp
        src/mysql/MySQLSchemaManager.cpp
        src/mysql/MySQLVirtualFile.cpp
        src/mysql/MySQLFormatConverter.cpp
    )
endif()

if(WITH_SQLITE)
    list(APPEND SOURCES
        src/sqlite/SQLiteConnection.cpp
        src/sqlite/SQLiteConnectionPool.cpp
        src/sqlite/SQLiteResultSet.cpp
        src/sqlite/SQLiteSchemaManager.cpp
        src/sqlite/SQLiteVirtualFile.cpp
        src/sqlite/SQLiteFormatConverter.cpp
    )
endif()

# Main executable
add_executable(sql-fuse ${SOURCES})

# Include directories
target_include_directories(sql-fuse PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${FUSE3_INCLUDE_DIRS}
)

if(WITH_MYSQL)
    target_include_directories(sql-fuse PRIVATE
        ${CMAKE_SOURCE_DIR}/include/mysql
        ${MYSQL_INCLUDE_DIRS}
    )
endif()

if(WITH_SQLITE)
    target_include_directories(sql-fuse PRIVATE
        ${CMAKE_SOURCE_DIR}/include/sqlite
        ${SQLITE3_INCLUDE_DIRS}
    )
endif()

# Link libraries
target_link_libraries(sql-fuse PRIVATE
    ${FUSE3_LIBRARIES}
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    CLI11::CLI11
    pthread
)

if(WITH_MYSQL)
    target_link_libraries(sql-fuse PRIVATE ${MYSQL_LIBRARIES})
endif()

if(WITH_SQLITE)
    target_link_libraries(sql-fuse PRIVATE ${SQLITE3_LIBRARIES})
endif()

# Compile definitions
target_compile_definitions(sql-fuse PRIVATE
    FUSE_USE_VERSION=35
    _FILE_OFFSET_BITS=64
)

if(WITH_MYSQL)
    target_compile_definitions(sql-fuse PRIVATE WITH_MYSQL)
endif()

if(WITH_SQLITE)
    target_compile_definitions(sql-fuse PRIVATE WITH_SQLITE)
endif()

if(WITH_POSTGRESQL)
    target_compile_definitions(sql-fuse PRIVATE WITH_POSTGRESQL)
endif()

if(WITH_ORACLE)
    target_compile_definitions(sql-fuse PRIVATE WITH_ORACLE)
endif()

# Compiler warnings
target_compile_options(sql-fuse PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-fsanitize=address,undefined>
)

target_link_options(sql-fuse PRIVATE
    $<$<CONFIG:Debug>:-fsanitize=address,undefined>
)

# Install
install(TARGETS sql-fuse RUNTIME DESTINATION bin)
install(FILES sql-fuse.conf.example DESTINATION /etc OPTIONAL)

# Testing
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Print configuration summary
message(STATUS "")
message(STATUS "sql-fuse configuration summary:")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  MySQL support: ${WITH_MYSQL}")
message(STATUS "  SQLite support: ${WITH_SQLITE}")
message(STATUS "  PostgreSQL support: ${WITH_POSTGRESQL}")
message(STATUS "  Oracle support: ${WITH_ORACLE}")
message(STATUS "")
