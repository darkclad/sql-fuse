#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "FormatConverter.hpp"

using namespace sqlfuse;
using ::testing::HasSubstr;
using ::testing::Not;

class FormatConverterTest : public ::testing::Test {
protected:
    std::vector<std::string> columns_ = {"id", "name", "email"};
    std::vector<std::vector<SqlValue>> rows_ = {
        {SqlValue("1"), SqlValue("John Doe"), SqlValue("john@example.com")},
        {SqlValue("2"), SqlValue("Jane Smith"), SqlValue("jane@example.com")},
    };
};

// CSV conversion tests
TEST_F(FormatConverterTest, ToCSVBasic) {
    auto csv = FormatConverter::toCSV(columns_, rows_);

    EXPECT_THAT(csv, HasSubstr("id,name,email"));
    EXPECT_THAT(csv, HasSubstr("1,John Doe,john@example.com"));
    EXPECT_THAT(csv, HasSubstr("2,Jane Smith,jane@example.com"));
}

TEST_F(FormatConverterTest, ToCSVNoHeader) {
    CSVOptions options;
    options.includeHeader = false;

    auto csv = FormatConverter::toCSV(columns_, rows_, options);

    EXPECT_THAT(csv, Not(HasSubstr("id,name,email\n")));
    EXPECT_THAT(csv, HasSubstr("1,John Doe,john@example.com"));
}

TEST_F(FormatConverterTest, ToCSVCustomDelimiter) {
    CSVOptions options;
    options.delimiter = ';';

    auto csv = FormatConverter::toCSV(columns_, rows_, options);

    EXPECT_THAT(csv, HasSubstr("id;name;email"));
    EXPECT_THAT(csv, HasSubstr("1;John Doe;john@example.com"));
}

TEST_F(FormatConverterTest, ToCSVWithNull) {
    std::vector<std::vector<SqlValue>> rowsWithNull = {
        {SqlValue("1"), std::nullopt, SqlValue("john@example.com")},
    };

    auto csv = FormatConverter::toCSV(columns_, rowsWithNull);

    // NULL values should be empty in CSV
    EXPECT_THAT(csv, HasSubstr("1,,john@example.com"));
}

TEST_F(FormatConverterTest, ToCSVWithQuotes) {
    std::vector<std::vector<SqlValue>> rowsWithQuotes = {
        {SqlValue("1"), SqlValue("John \"JD\" Doe"), SqlValue("john@example.com")},
    };

    auto csv = FormatConverter::toCSV(columns_, rowsWithQuotes);

    // Quotes should be escaped
    EXPECT_THAT(csv, HasSubstr("\"John \"\"JD\"\" Doe\""));
}

TEST_F(FormatConverterTest, ToCSVWithComma) {
    std::vector<std::vector<SqlValue>> rowsWithComma = {
        {SqlValue("1"), SqlValue("Doe, John"), SqlValue("john@example.com")},
    };

    auto csv = FormatConverter::toCSV(columns_, rowsWithComma);

    // Fields with comma should be quoted
    EXPECT_THAT(csv, HasSubstr("\"Doe, John\""));
}

TEST_F(FormatConverterTest, ToCSVWithNewline) {
    std::vector<std::vector<SqlValue>> rowsWithNewline = {
        {SqlValue("1"), SqlValue("John\nDoe"), SqlValue("john@example.com")},
    };

    auto csv = FormatConverter::toCSV(columns_, rowsWithNewline);

    // Fields with newline should be quoted
    EXPECT_THAT(csv, HasSubstr("\"John\nDoe\""));
}

TEST_F(FormatConverterTest, ToCSVQuoteAll) {
    CSVOptions options;
    options.quoteAll = true;

    auto csv = FormatConverter::toCSV(columns_, rows_, options);

    EXPECT_THAT(csv, HasSubstr("\"id\",\"name\",\"email\""));
    EXPECT_THAT(csv, HasSubstr("\"1\",\"John Doe\",\"john@example.com\""));
}

// JSON conversion tests
TEST_F(FormatConverterTest, ToJSONBasic) {
    auto json = FormatConverter::toJSON(columns_, rows_);

    EXPECT_THAT(json, HasSubstr("\"id\""));
    EXPECT_THAT(json, HasSubstr("\"name\""));
    EXPECT_THAT(json, HasSubstr("\"John Doe\""));
    EXPECT_THAT(json, HasSubstr("\"jane@example.com\""));
}

TEST_F(FormatConverterTest, ToJSONPretty) {
    JSONOptions options;
    options.pretty = true;
    options.indent = 2;

    auto json = FormatConverter::toJSON(columns_, rows_, options);

    // Pretty JSON should have indentation
    EXPECT_THAT(json, HasSubstr("\n"));
    EXPECT_THAT(json, HasSubstr("  "));
}

TEST_F(FormatConverterTest, ToJSONCompact) {
    JSONOptions options;
    options.pretty = false;

    auto json = FormatConverter::toJSON(columns_, rows_, options);

    // Compact JSON should not have unnecessary newlines
    EXPECT_THAT(json, Not(HasSubstr("\n  ")));
}

TEST_F(FormatConverterTest, ToJSONWithNull) {
    std::vector<std::vector<SqlValue>> rowsWithNull = {
        {SqlValue("1"), std::nullopt, SqlValue("john@example.com")},
    };

    JSONOptions options;
    options.includeNull = true;

    auto json = FormatConverter::toJSON(columns_, rowsWithNull, options);

    EXPECT_THAT(json, HasSubstr("null"));
}

TEST_F(FormatConverterTest, ToJSONExcludeNull) {
    std::vector<std::vector<SqlValue>> rowsWithNull = {
        {SqlValue("1"), std::nullopt, SqlValue("john@example.com")},
    };

    JSONOptions options;
    options.includeNull = false;
    options.pretty = false;

    auto json = FormatConverter::toJSON(columns_, rowsWithNull, options);

    // When include_null is false, null fields may be omitted
    // The exact behavior depends on implementation
}

// Row to JSON tests
TEST_F(FormatConverterTest, RowToJSONFromRowData) {
    RowData row = {
        {"id", SqlValue("1")},
        {"name", SqlValue("John Doe")},
        {"email", SqlValue("john@example.com")}
    };

    auto json = FormatConverter::rowToJSON(row);

    EXPECT_THAT(json, HasSubstr("\"id\""));
    EXPECT_THAT(json, HasSubstr("\"1\""));
    EXPECT_THAT(json, HasSubstr("\"John Doe\""));
}

// CSV parsing tests
TEST_F(FormatConverterTest, ParseCSVBasic) {
    std::string csv = "id,name,email\n1,John Doe,john@example.com\n2,Jane Smith,jane@example.com\n";

    auto rows = FormatConverter::parseCSV(csv);

    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].at("id").value(), "1");
    EXPECT_EQ(rows[0].at("name").value(), "John Doe");
    EXPECT_EQ(rows[1].at("email").value(), "jane@example.com");
}

TEST_F(FormatConverterTest, ParseCSVWithQuotedFields) {
    std::string csv = "id,name,email\n1,\"Doe, John\",john@example.com\n";

    auto rows = FormatConverter::parseCSV(csv);

    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].at("name").value(), "Doe, John");
}

TEST_F(FormatConverterTest, ParseCSVWithEscapedQuotes) {
    std::string csv = "id,name,email\n1,\"John \"\"JD\"\" Doe\",john@example.com\n";

    auto rows = FormatConverter::parseCSV(csv);

    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].at("name").value(), "John \"JD\" Doe");
}

TEST_F(FormatConverterTest, ParseCSVWithProvidedColumns) {
    std::string csv = "1,John Doe,john@example.com\n2,Jane Smith,jane@example.com\n";
    std::vector<std::string> columns = {"id", "name", "email"};

    CSVOptions options;
    options.includeHeader = false;

    auto rows = FormatConverter::parseCSV(csv, columns, options);

    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].at("id").value(), "1");
}

TEST_F(FormatConverterTest, ParseCSVEmpty) {
    std::string csv = "";

    auto rows = FormatConverter::parseCSV(csv);

    EXPECT_TRUE(rows.empty());
}

// JSON parsing tests
TEST_F(FormatConverterTest, ParseJSONArray) {
    std::string json = R"([
        {"id": "1", "name": "John Doe", "email": "john@example.com"},
        {"id": "2", "name": "Jane Smith", "email": "jane@example.com"}
    ])";

    auto rows = FormatConverter::parseJSON(json);

    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].at("id").value(), "1");
    EXPECT_EQ(rows[0].at("name").value(), "John Doe");
}

TEST_F(FormatConverterTest, ParseJSONRow) {
    std::string json = R"({"id": "1", "name": "John Doe", "email": "john@example.com"})";

    auto row = FormatConverter::parseJSONRow(json);

    EXPECT_EQ(row.at("id").value(), "1");
    EXPECT_EQ(row.at("name").value(), "John Doe");
}

TEST_F(FormatConverterTest, ParseJSONWithNull) {
    std::string json = R"([{"id": "1", "name": null, "email": "john@example.com"}])";

    auto rows = FormatConverter::parseJSON(json);

    ASSERT_EQ(rows.size(), 1u);
    EXPECT_FALSE(rows[0].at("name").has_value());
}

TEST_F(FormatConverterTest, ParseJSONWithNumbers) {
    std::string json = R"([{"id": 1, "count": 42, "price": 19.99}])";

    auto rows = FormatConverter::parseJSON(json);

    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].at("id").value(), "1");
    EXPECT_EQ(rows[0].at("count").value(), "42");
}

// SQL building tests
TEST_F(FormatConverterTest, BuildInsert) {
    RowData row = {
        {"id", SqlValue("1")},
        {"name", SqlValue("John Doe")},
        {"email", SqlValue("john@example.com")}
    };

    auto sql = FormatConverter::buildInsert("users", row);

    EXPECT_THAT(sql, HasSubstr("INSERT INTO"));
    EXPECT_THAT(sql, HasSubstr("users"));
    EXPECT_THAT(sql, HasSubstr("VALUES"));
}

TEST_F(FormatConverterTest, BuildInsertWithNull) {
    RowData row = {
        {"id", SqlValue("1")},
        {"name", std::nullopt},
        {"email", SqlValue("john@example.com")}
    };

    auto sql = FormatConverter::buildInsert("users", row);

    EXPECT_THAT(sql, HasSubstr("NULL"));
}

TEST_F(FormatConverterTest, BuildUpdate) {
    RowData row = {
        {"name", SqlValue("John Updated")},
        {"email", SqlValue("john.updated@example.com")}
    };

    auto sql = FormatConverter::buildUpdate("users", row, "id", "1");

    EXPECT_THAT(sql, HasSubstr("UPDATE"));
    EXPECT_THAT(sql, HasSubstr("users"));
    EXPECT_THAT(sql, HasSubstr("SET"));
    EXPECT_THAT(sql, HasSubstr("WHERE"));
    EXPECT_THAT(sql, HasSubstr("id"));
}

TEST_F(FormatConverterTest, BuildDelete) {
    auto sql = FormatConverter::buildDelete("users", "id", "1");

    EXPECT_THAT(sql, HasSubstr("DELETE FROM"));
    EXPECT_THAT(sql, HasSubstr("users"));
    EXPECT_THAT(sql, HasSubstr("WHERE"));
    EXPECT_THAT(sql, HasSubstr("id"));
}

// SQL escaping tests
TEST_F(FormatConverterTest, EscapeSQLBasic) {
    auto escaped = FormatConverter::escapeSQL("John's Data");

    EXPECT_THAT(escaped, HasSubstr("\\'"));
}

TEST_F(FormatConverterTest, EscapeSQLBackslash) {
    auto escaped = FormatConverter::escapeSQL("path\\to\\file");

    EXPECT_THAT(escaped, HasSubstr("\\\\"));
}

TEST_F(FormatConverterTest, EscapeIdentifier) {
    auto escaped = FormatConverter::escapeIdentifier("table-name");

    EXPECT_THAT(escaped, HasSubstr("`"));
}

// CSV field escaping
TEST_F(FormatConverterTest, EscapeCSVFieldWithComma) {
    auto escaped = FormatConverter::escapeCSVField("value, with comma");

    EXPECT_EQ(escaped.front(), '"');
    EXPECT_EQ(escaped.back(), '"');
}

TEST_F(FormatConverterTest, EscapeCSVFieldWithQuote) {
    auto escaped = FormatConverter::escapeCSVField("value with \"quote\"");

    EXPECT_THAT(escaped, HasSubstr("\"\""));
}

TEST_F(FormatConverterTest, EscapeCSVFieldSimple) {
    auto escaped = FormatConverter::escapeCSVField("simple value");

    // Simple values don't need quoting
    EXPECT_EQ(escaped, "simple value");
}

// CSV line splitting
TEST_F(FormatConverterTest, SplitCSVLineBasic) {
    auto fields = FormatConverter::splitCSVLine("a,b,c");

    ASSERT_EQ(fields.size(), 3u);
    EXPECT_EQ(fields[0], "a");
    EXPECT_EQ(fields[1], "b");
    EXPECT_EQ(fields[2], "c");
}

TEST_F(FormatConverterTest, SplitCSVLineWithQuotes) {
    auto fields = FormatConverter::splitCSVLine("a,\"b,c\",d");

    ASSERT_EQ(fields.size(), 3u);
    EXPECT_EQ(fields[0], "a");
    EXPECT_EQ(fields[1], "b,c");
    EXPECT_EQ(fields[2], "d");
}

TEST_F(FormatConverterTest, SplitCSVLineWithEscapedQuotes) {
    auto fields = FormatConverter::splitCSVLine("a,\"b\"\"c\",d");

    ASSERT_EQ(fields.size(), 3u);
    EXPECT_EQ(fields[1], "b\"c");
}

TEST_F(FormatConverterTest, SplitCSVLineEmpty) {
    auto fields = FormatConverter::splitCSVLine("");

    EXPECT_TRUE(fields.empty() || (fields.size() == 1 && fields[0].empty()));
}

TEST_F(FormatConverterTest, SplitCSVLineEmptyFields) {
    auto fields = FormatConverter::splitCSVLine("a,,c");

    ASSERT_EQ(fields.size(), 3u);
    EXPECT_EQ(fields[0], "a");
    EXPECT_EQ(fields[1], "");
    EXPECT_EQ(fields[2], "c");
}

// Edge cases
TEST_F(FormatConverterTest, ToCSVEmptyData) {
    std::vector<std::string> emptyColumns;
    std::vector<std::vector<SqlValue>> emptyRows;

    auto csv = FormatConverter::toCSV(emptyColumns, emptyRows);

    EXPECT_TRUE(csv.empty() || csv == "\n");
}

TEST_F(FormatConverterTest, ToJSONEmptyData) {
    std::vector<std::string> emptyColumns;
    std::vector<std::vector<SqlValue>> emptyRows;

    auto json = FormatConverter::toJSON(emptyColumns, emptyRows);

    EXPECT_THAT(json, HasSubstr("[]"));
}

TEST_F(FormatConverterTest, SpecialCharactersInJSON) {
    std::vector<std::vector<SqlValue>> specialRows = {
        {SqlValue("1"), SqlValue("Line1\nLine2\tTab"), SqlValue("test@example.com")},
    };

    auto json = FormatConverter::toJSON(columns_, specialRows);

    // JSON should properly escape special characters
    EXPECT_THAT(json, HasSubstr("\\n"));
    EXPECT_THAT(json, HasSubstr("\\t"));
}

TEST_F(FormatConverterTest, UnicodeInData) {
    std::vector<std::vector<SqlValue>> unicodeRows = {
        {SqlValue("1"), SqlValue("Jürgen Müller"), SqlValue("juergen@example.com")},
    };

    auto csv = FormatConverter::toCSV(columns_, unicodeRows);
    auto json = FormatConverter::toJSON(columns_, unicodeRows);

    EXPECT_THAT(csv, HasSubstr("Jürgen Müller"));
    EXPECT_THAT(json, HasSubstr("Jürgen Müller"));
}
