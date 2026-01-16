# Unit tests for sql-fuse

# Library with common source files (without main.cpp)
set(LIB_SOURCES
    ${CMAKE_SOURCE_DIR}/src/path_router.cpp
    ${CMAKE_SOURCE_DIR}/src/cache_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/format_converter.cpp
    ${CMAKE_SOURCE_DIR}/src/error_handler.cpp
    ${CMAKE_SOURCE_DIR}/src/config.cpp
    ${CMAKE_SOURCE_DIR}/src/connection_pool.cpp
    ${CMAKE_SOURCE_DIR}/src/schema_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/virtual_file.cpp
    ${CMAKE_SOURCE_DIR}/src/sql_fuse_fs.cpp
)

add_library(sql-fuse-lib STATIC ${LIB_SOURCES})

target_include_directories(sql-fuse-lib PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${FUSE3_INCLUDE_DIRS}
    ${MYSQL_INCLUDE_DIRS}
)

target_link_libraries(sql-fuse-lib PUBLIC
    ${FUSE3_LIBRARIES}
    ${MYSQL_LIBRARIES}
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    CLI11::CLI11
    pthread
)

target_compile_definitions(sql-fuse-lib PUBLIC
    FUSE_USE_VERSION=35
    _FILE_OFFSET_BITS=64
)

# Test executable
set(TEST_SOURCES
    test_main.cpp
    test_cache_manager.cpp
    test_path_router.cpp
    test_format_converter.cpp
    test_error_handler.cpp
    test_config.cpp
)

add_executable(sql-fuse-tests ${TEST_SOURCES})

target_link_libraries(sql-fuse-tests PRIVATE
    sql-fuse-lib
    GTest::gtest
    GTest::gmock
)

target_compile_options(sql-fuse-tests PRIVATE
    -Wall -Wextra -Wpedantic
)

# Register tests with CTest
include(GoogleTest)
gtest_discover_tests(sql-fuse-tests)
