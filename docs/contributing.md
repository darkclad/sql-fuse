# Contributing to SQL-FUSE

Thank you for your interest in contributing to SQL-FUSE! This document provides guidelines and instructions for contributing.

## Code of Conduct

By participating in this project, you agree to maintain a respectful and inclusive environment. We expect all contributors to:

- Be respectful and considerate in all communications
- Accept constructive criticism gracefully
- Focus on what is best for the community
- Show empathy towards other community members

## Ways to Contribute

### Reporting Bugs

Before submitting a bug report:

1. Check the [issue tracker](https://github.com/darkclad/sql-fuse/issues) for existing reports
2. Try to reproduce the issue with the latest version
3. Collect relevant information (OS, database version, error messages)

When submitting a bug report, include:

- Clear, descriptive title
- Steps to reproduce the issue
- Expected behavior vs actual behavior
- System information (OS, SQL-FUSE version, database type/version)
- Relevant log output
- Minimal test case if possible

### Suggesting Features

Feature suggestions are welcome! Please:

1. Check existing issues for similar suggestions
2. Describe the use case clearly
3. Explain why this would benefit other users
4. Consider implementation complexity

### Code Contributions

#### Getting Started

1. Fork the repository
2. Clone your fork:
   ```bash
   git clone https://github.com/YOUR_USERNAME/sql-fuse.git
   cd sql-fuse
   ```
3. Add upstream remote:
   ```bash
   git remote add upstream https://github.com/darkclad/sql-fuse.git
   ```
4. Create a branch:
   ```bash
   git checkout -b feature/your-feature-name
   ```

#### Development Setup

```bash
# Install dependencies (Ubuntu/Debian)
sudo apt-get install build-essential cmake pkg-config \
    libfuse3-dev libmysqlclient-dev libpq-dev libsqlite3-dev

# Build with all features and debug symbols
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug \
    -DWITH_MYSQL=ON \
    -DWITH_POSTGRESQL=ON \
    -DWITH_SQLITE=ON \
    -DBUILD_TESTS=ON
make -j$(nproc)

# Run tests
ctest --output-on-failure
```

#### Code Style

SQL-FUSE follows these coding conventions:

**General:**
- Use C++17 features appropriately
- Prefer `std::` types over C equivalents
- Use smart pointers (`std::unique_ptr`, `std::shared_ptr`)
- Avoid raw `new`/`delete`

**Naming:**
- Classes: `PascalCase`
- Functions/Methods: `camelCase`
- Variables: `camelCase`
- Constants: `SCREAMING_SNAKE_CASE`
- Member variables: `m_` prefix (e.g., `m_connection`)
- Private methods: no prefix

**Formatting:**
- 4-space indentation (no tabs)
- Opening braces on same line
- Max line length: 100 characters
- Use `#pragma once` for header guards

**Example:**
```cpp
class MyClass {
public:
    MyClass(int value);

    void doSomething();
    int getValue() const { return m_value; }

private:
    void helperFunction();

    int m_value;
    std::string m_name;
};

void MyClass::doSomething() {
    if (m_value > 0) {
        helperFunction();
    } else {
        // Handle other case
    }
}
```

**Comments:**
- Use `//` for single-line comments
- Use `/* */` for multi-line comments
- Document public APIs with Doxygen-style comments
- Avoid obvious comments

**Error Handling:**
- Use exceptions for exceptional conditions
- Return `std::optional` for "not found" cases
- Use error codes for FUSE interface compatibility

#### Testing

All contributions should include appropriate tests:

**Unit Tests:**
- Located in `tests/`
- Use the Catch2 framework
- Name test files `test_<component>.cpp`

**Integration Tests:**
- Located in `tests/<database>/`
- Shell scripts for end-to-end testing
- Include setup and teardown scripts

Example unit test:
```cpp
#include <catch2/catch.hpp>
#include "PathRouter.hpp"

TEST_CASE("PathRouter parses table files", "[path]") {
    PathRouter router;

    SECTION("CSV file") {
        auto parsed = router.parse("/mydb/tables/users.csv");
        REQUIRE(parsed.type == PathType::TABLE_FILE);
        REQUIRE(parsed.database == "mydb");
        REQUIRE(parsed.object_name == "users");
        REQUIRE(parsed.format == FileFormat::CSV);
    }

    SECTION("JSON file") {
        auto parsed = router.parse("/mydb/tables/users.json");
        REQUIRE(parsed.format == FileFormat::JSON);
    }
}
```

#### Commit Messages

Follow conventional commits format:

```
type(scope): short description

Longer description if needed.

Fixes #123
```

Types:
- `feat`: New feature
- `fix`: Bug fix
- `docs`: Documentation changes
- `style`: Code style changes (formatting, etc.)
- `refactor`: Code refactoring
- `test`: Adding or updating tests
- `chore`: Maintenance tasks

Examples:
```
feat(postgresql): add support for JSONB columns

fix(mysql): handle NULL values in BLOB columns
Fixes #45

docs: update installation instructions for Fedora

test(sqlite): add integration tests for views
```

#### Pull Request Process

1. Update documentation if needed
2. Add/update tests for your changes
3. Ensure all tests pass
4. Update CHANGELOG.md if applicable
5. Submit PR against `main` branch
6. Fill out the PR template completely
7. Request review from maintainers

PR title should follow commit message format.

### Documentation

Documentation improvements are always welcome:

- Fix typos or clarify confusing sections
- Add examples for common use cases
- Improve API documentation
- Translate documentation

## Development Guidelines

### Adding Database Support

To add support for a new database:

1. **Create headers** in `include/<database>/`:
   - `<Database>Connection.hpp`
   - `<Database>ConnectionPool.hpp`
   - `<Database>SchemaManager.hpp`
   - `<Database>FormatConverter.hpp`
   - `<Database>VirtualFile.hpp`

2. **Implement sources** in `src/<database>/`:
   - Follow existing patterns from MySQL/PostgreSQL/SQLite
   - Implement all SchemaManager virtual methods
   - Handle database-specific data types

3. **Update build system**:
   - Add `WITH_<DATABASE>` option to CMakeLists.txt
   - Add conditional compilation blocks
   - Link required libraries

4. **Update SQLFuseFS**:
   - Add include guards for new headers
   - Add case in database type switch
   - Add connection pool variant

5. **Add tests**:
   - Unit tests for format converter
   - Integration tests in `tests/<database>/`
   - Setup/teardown scripts

6. **Update documentation**:
   - Add to supported-databases.md
   - Update getting-started.md
   - Add any special configuration

### Performance Considerations

- Use connection pooling efficiently
- Implement appropriate caching strategies
- Avoid unnecessary data copies
- Profile before optimizing
- Document any performance-critical code

### Security Considerations

- Never log passwords or sensitive data
- Validate and sanitize all inputs
- Use parameterized queries
- Follow principle of least privilege
- Document security implications of features

## Release Process

1. Update version in CMakeLists.txt
2. Update CHANGELOG.md
3. Create release branch
4. Run full test suite
5. Create GitHub release
6. Tag the release

## Getting Help

- Open an issue for questions
- Join discussions in GitHub Discussions
- Check existing documentation

## License

By contributing, you agree that your contributions will be licensed under the GPLv3 license.

## Recognition

Contributors are recognized in:
- CONTRIBUTORS.md file
- Release notes
- Project documentation

Thank you for contributing to SQL-FUSE!
