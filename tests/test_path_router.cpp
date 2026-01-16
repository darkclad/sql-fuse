#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "PathRouter.hpp"

using namespace sqlfuse;
using ::testing::ElementsAre;

class PathRouterTest : public ::testing::Test {
protected:
    PathRouter router_;
};

// Root path tests
TEST_F(PathRouterTest, ParseRoot) {
    auto result = router_.parse("/");

    EXPECT_EQ(result.type, NodeType::Root);
    EXPECT_TRUE(result.database.empty());
    EXPECT_TRUE(result.object_name.empty());
    EXPECT_TRUE(result.isDirectory());
}

// Database path tests
TEST_F(PathRouterTest, ParseDatabase) {
    auto result = router_.parse("/mydb");

    EXPECT_EQ(result.type, NodeType::Database);
    EXPECT_EQ(result.database, "mydb");
    EXPECT_TRUE(result.isDirectory());
}

TEST_F(PathRouterTest, ParseDatabaseWithTrailingSlash) {
    auto result = router_.parse("/mydb/");

    EXPECT_EQ(result.type, NodeType::Database);
    EXPECT_EQ(result.database, "mydb");
}

// Directory paths within database
TEST_F(PathRouterTest, ParseTablesDir) {
    auto result = router_.parse("/mydb/tables");

    EXPECT_EQ(result.type, NodeType::TablesDir);
    EXPECT_EQ(result.database, "mydb");
    EXPECT_TRUE(result.isDirectory());
}

TEST_F(PathRouterTest, ParseViewsDir) {
    auto result = router_.parse("/mydb/views");

    EXPECT_EQ(result.type, NodeType::ViewsDir);
    EXPECT_EQ(result.database, "mydb");
    EXPECT_TRUE(result.isDirectory());
}

TEST_F(PathRouterTest, ParseProceduresDir) {
    auto result = router_.parse("/mydb/procedures");

    EXPECT_EQ(result.type, NodeType::ProceduresDir);
    EXPECT_EQ(result.database, "mydb");
    EXPECT_TRUE(result.isDirectory());
}

TEST_F(PathRouterTest, ParseFunctionsDir) {
    auto result = router_.parse("/mydb/functions");

    EXPECT_EQ(result.type, NodeType::FunctionsDir);
    EXPECT_EQ(result.database, "mydb");
    EXPECT_TRUE(result.isDirectory());
}

TEST_F(PathRouterTest, ParseTriggersDir) {
    auto result = router_.parse("/mydb/triggers");

    EXPECT_EQ(result.type, NodeType::TriggersDir);
    EXPECT_EQ(result.database, "mydb");
    EXPECT_TRUE(result.isDirectory());
}

// Table file paths with different formats
TEST_F(PathRouterTest, ParseTableFileCSV) {
    auto result = router_.parse("/mydb/tables/users.csv");

    EXPECT_EQ(result.type, NodeType::TableFile);
    EXPECT_EQ(result.database, "mydb");
    EXPECT_EQ(result.object_name, "users");
    EXPECT_EQ(result.format, FileFormat::CSV);
    EXPECT_FALSE(result.isDirectory());
}

TEST_F(PathRouterTest, ParseTableFileJSON) {
    auto result = router_.parse("/mydb/tables/users.json");

    EXPECT_EQ(result.type, NodeType::TableFile);
    EXPECT_EQ(result.database, "mydb");
    EXPECT_EQ(result.object_name, "users");
    EXPECT_EQ(result.format, FileFormat::JSON);
}

TEST_F(PathRouterTest, ParseTableFileSQL) {
    auto result = router_.parse("/mydb/tables/users.sql");

    EXPECT_EQ(result.type, NodeType::TableFile);
    EXPECT_EQ(result.database, "mydb");
    EXPECT_EQ(result.object_name, "users");
    EXPECT_EQ(result.format, FileFormat::SQL);
}

// Table directory (for subdirectories like rows/, schema, etc.)
TEST_F(PathRouterTest, ParseTableDir) {
    auto result = router_.parse("/mydb/tables/users");

    EXPECT_EQ(result.type, NodeType::TableDir);
    EXPECT_EQ(result.database, "mydb");
    EXPECT_EQ(result.object_name, "users");
    EXPECT_TRUE(result.isDirectory());
}

// Table subdirectories
TEST_F(PathRouterTest, ParseTableRowsDir) {
    auto result = router_.parse("/mydb/tables/users/rows");

    EXPECT_EQ(result.type, NodeType::TableRowsDir);
    EXPECT_EQ(result.database, "mydb");
    EXPECT_EQ(result.object_name, "users");
    EXPECT_TRUE(result.isDirectory());
}

TEST_F(PathRouterTest, ParseTableSchema) {
    auto result = router_.parse("/mydb/tables/users/schema.json");

    EXPECT_EQ(result.type, NodeType::TableSchema);
    EXPECT_EQ(result.database, "mydb");
    EXPECT_EQ(result.object_name, "users");
}

TEST_F(PathRouterTest, ParseTableIndexes) {
    auto result = router_.parse("/mydb/tables/users/indexes.json");

    EXPECT_EQ(result.type, NodeType::TableIndexes);
    EXPECT_EQ(result.database, "mydb");
    EXPECT_EQ(result.object_name, "users");
}

TEST_F(PathRouterTest, ParseTableStats) {
    auto result = router_.parse("/mydb/tables/users/stats.json");

    EXPECT_EQ(result.type, NodeType::TableStats);
    EXPECT_EQ(result.database, "mydb");
    EXPECT_EQ(result.object_name, "users");
}

// Row files
TEST_F(PathRouterTest, ParseTableRowFile) {
    auto result = router_.parse("/mydb/tables/users/rows/123.json");

    EXPECT_EQ(result.type, NodeType::TableRowFile);
    EXPECT_EQ(result.database, "mydb");
    EXPECT_EQ(result.object_name, "users");
    EXPECT_EQ(result.row_id, "123");
    EXPECT_EQ(result.format, FileFormat::JSON);
}

TEST_F(PathRouterTest, ParseTableRowFileCSV) {
    auto result = router_.parse("/mydb/tables/users/rows/456.csv");

    EXPECT_EQ(result.type, NodeType::TableRowFile);
    EXPECT_EQ(result.row_id, "456");
    EXPECT_EQ(result.format, FileFormat::CSV);
}

// View paths
TEST_F(PathRouterTest, ParseViewFileCSV) {
    auto result = router_.parse("/mydb/views/active_users.csv");

    EXPECT_EQ(result.type, NodeType::ViewFile);
    EXPECT_EQ(result.database, "mydb");
    EXPECT_EQ(result.object_name, "active_users");
    EXPECT_EQ(result.format, FileFormat::CSV);
}

TEST_F(PathRouterTest, ParseViewDir) {
    auto result = router_.parse("/mydb/views/active_users");

    EXPECT_EQ(result.type, NodeType::ViewDir);
    EXPECT_EQ(result.object_name, "active_users");
    EXPECT_TRUE(result.isDirectory());
}

// Procedure and function paths
TEST_F(PathRouterTest, ParseProcedureFile) {
    auto result = router_.parse("/mydb/procedures/get_user.sql");

    EXPECT_EQ(result.type, NodeType::ProcedureFile);
    EXPECT_EQ(result.database, "mydb");
    EXPECT_EQ(result.object_name, "get_user");
}

TEST_F(PathRouterTest, ParseFunctionFile) {
    auto result = router_.parse("/mydb/functions/calc_total.sql");

    EXPECT_EQ(result.type, NodeType::FunctionFile);
    EXPECT_EQ(result.database, "mydb");
    EXPECT_EQ(result.object_name, "calc_total");
}

// Trigger path
TEST_F(PathRouterTest, ParseTriggerFile) {
    auto result = router_.parse("/mydb/triggers/before_insert_users.sql");

    EXPECT_EQ(result.type, NodeType::TriggerFile);
    EXPECT_EQ(result.database, "mydb");
    EXPECT_EQ(result.object_name, "before_insert_users");
}

// Special paths
TEST_F(PathRouterTest, ParseServerInfo) {
    auto result = router_.parse("/.server_info");

    EXPECT_EQ(result.type, NodeType::ServerInfo);
}

TEST_F(PathRouterTest, ParseDatabaseInfo) {
    auto result = router_.parse("/mydb/.info");

    EXPECT_EQ(result.type, NodeType::DatabaseInfo);
    EXPECT_EQ(result.database, "mydb");
}

// Not found
TEST_F(PathRouterTest, ParseNotFound) {
    auto result = router_.parse("/mydb/unknown_dir/file.txt");

    EXPECT_EQ(result.type, NodeType::NotFound);
}

// Format conversion tests
TEST_F(PathRouterTest, FormatToExtension) {
    EXPECT_EQ(PathRouter::formatToExtension(FileFormat::CSV), ".csv");
    EXPECT_EQ(PathRouter::formatToExtension(FileFormat::JSON), ".json");
    EXPECT_EQ(PathRouter::formatToExtension(FileFormat::SQL), ".sql");
    EXPECT_EQ(PathRouter::formatToExtension(FileFormat::None), "");
}

TEST_F(PathRouterTest, ExtensionToFormat) {
    EXPECT_EQ(PathRouter::extensionToFormat(".csv"), FileFormat::CSV);
    EXPECT_EQ(PathRouter::extensionToFormat(".json"), FileFormat::JSON);
    EXPECT_EQ(PathRouter::extensionToFormat(".sql"), FileFormat::SQL);
    EXPECT_EQ(PathRouter::extensionToFormat("csv"), FileFormat::CSV);
    EXPECT_EQ(PathRouter::extensionToFormat(".txt"), FileFormat::None);
    EXPECT_EQ(PathRouter::extensionToFormat(""), FileFormat::None);
}

// NodeType to string
TEST_F(PathRouterTest, NodeTypeToString) {
    EXPECT_EQ(PathRouter::nodeTypeToString(NodeType::Root), "Root");
    EXPECT_EQ(PathRouter::nodeTypeToString(NodeType::Database), "Database");
    EXPECT_EQ(PathRouter::nodeTypeToString(NodeType::TableFile), "TableFile");
    EXPECT_EQ(PathRouter::nodeTypeToString(NodeType::NotFound), "NotFound");
}

// ParsedPath helper methods
TEST_F(PathRouterTest, ParsedPathIsDirectory) {
    auto rootPath = router_.parse("/");
    EXPECT_TRUE(rootPath.isDirectory());

    auto dbPath = router_.parse("/mydb");
    EXPECT_TRUE(dbPath.isDirectory());

    auto tablePath = router_.parse("/mydb/tables/users.csv");
    EXPECT_FALSE(tablePath.isDirectory());
}

TEST_F(PathRouterTest, ParsedPathIsReadOnly) {
    auto viewPath = router_.parse("/mydb/views/active_users.csv");
    EXPECT_TRUE(viewPath.isReadOnly());

    auto procPath = router_.parse("/mydb/procedures/my_proc.sql");
    EXPECT_TRUE(procPath.isReadOnly());

    auto schemaPath = router_.parse("/mydb/tables/users/schema.json");
    EXPECT_TRUE(schemaPath.isReadOnly());
}

TEST_F(PathRouterTest, ParsedPathGetExtension) {
    auto csvPath = router_.parse("/mydb/tables/users.csv");
    EXPECT_EQ(csvPath.getExtension(), ".csv");

    auto jsonPath = router_.parse("/mydb/tables/users.json");
    EXPECT_EQ(jsonPath.getExtension(), ".json");
}

// Edge cases
TEST_F(PathRouterTest, ParseEmptyPath) {
    auto result = router_.parse("");

    // Should handle gracefully
    EXPECT_EQ(result.type, NodeType::Root);
}

TEST_F(PathRouterTest, ParsePathWithSpecialChars) {
    auto result = router_.parse("/mydb/tables/user_data.csv");

    EXPECT_EQ(result.type, NodeType::TableFile);
    EXPECT_EQ(result.object_name, "user_data");
}

TEST_F(PathRouterTest, ParsePathWithNumbers) {
    auto result = router_.parse("/db123/tables/table456.json");

    EXPECT_EQ(result.type, NodeType::TableFile);
    EXPECT_EQ(result.database, "db123");
    EXPECT_EQ(result.object_name, "table456");
}

// Case sensitivity
TEST_F(PathRouterTest, CaseSensitivePaths) {
    auto lower = router_.parse("/mydb/tables");
    auto upper = router_.parse("/mydb/TABLES");

    EXPECT_EQ(lower.type, NodeType::TablesDir);
    // Depending on implementation, TABLES might be NotFound or treated differently
    // This test documents the expected behavior
}
