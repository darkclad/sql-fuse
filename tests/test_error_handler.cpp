#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "ErrorHandler.hpp"
#include <mysql/errmsg.h>
#include <mysql/mysqld_error.h>
#include <cerrno>

using namespace sqlfuse;

class ErrorHandlerTest : public ::testing::Test {
};

// MySQL to errno mapping tests
TEST_F(ErrorHandlerTest, SuccessReturnsZero) {
    EXPECT_EQ(ErrorHandler::mysqlToErrno(0), 0);
}

TEST_F(ErrorHandlerTest, ConnectionErrorsMapToENOENT) {
    EXPECT_EQ(ErrorHandler::mysqlToErrno(CR_CONNECTION_ERROR), ENOENT);
    EXPECT_EQ(ErrorHandler::mysqlToErrno(CR_CONN_HOST_ERROR), ENOENT);
    EXPECT_EQ(ErrorHandler::mysqlToErrno(CR_UNKNOWN_HOST), ENOENT);
    EXPECT_EQ(ErrorHandler::mysqlToErrno(CR_SERVER_GONE_ERROR), ENOENT);
    EXPECT_EQ(ErrorHandler::mysqlToErrno(CR_SERVER_LOST), ENOENT);
}

TEST_F(ErrorHandlerTest, AuthenticationErrorsMapToEACCES) {
    EXPECT_EQ(ErrorHandler::mysqlToErrno(ER_ACCESS_DENIED_ERROR), EACCES);
    EXPECT_EQ(ErrorHandler::mysqlToErrno(ER_DBACCESS_DENIED_ERROR), EACCES);
    EXPECT_EQ(ErrorHandler::mysqlToErrno(ER_TABLEACCESS_DENIED_ERROR), EACCES);
    EXPECT_EQ(ErrorHandler::mysqlToErrno(ER_COLUMNACCESS_DENIED_ERROR), EACCES);
}

TEST_F(ErrorHandlerTest, NotFoundErrorsMapToENOENT) {
    EXPECT_EQ(ErrorHandler::mysqlToErrno(ER_BAD_DB_ERROR), ENOENT);
    EXPECT_EQ(ErrorHandler::mysqlToErrno(ER_NO_SUCH_TABLE), ENOENT);
    EXPECT_EQ(ErrorHandler::mysqlToErrno(ER_BAD_TABLE_ERROR), ENOENT);
}

TEST_F(ErrorHandlerTest, ExistsErrorsMapToEEXIST) {
    EXPECT_EQ(ErrorHandler::mysqlToErrno(ER_DB_CREATE_EXISTS), EEXIST);
    EXPECT_EQ(ErrorHandler::mysqlToErrno(ER_TABLE_EXISTS_ERROR), EEXIST);
    EXPECT_EQ(ErrorHandler::mysqlToErrno(ER_DUP_ENTRY), EEXIST);
    EXPECT_EQ(ErrorHandler::mysqlToErrno(ER_DUP_KEY), EEXIST);
}

TEST_F(ErrorHandlerTest, InvalidInputErrorsMapToEINVAL) {
    EXPECT_EQ(ErrorHandler::mysqlToErrno(ER_PARSE_ERROR), EINVAL);
    EXPECT_EQ(ErrorHandler::mysqlToErrno(ER_SYNTAX_ERROR), EINVAL);
    EXPECT_EQ(ErrorHandler::mysqlToErrno(ER_BAD_FIELD_ERROR), EINVAL);
    EXPECT_EQ(ErrorHandler::mysqlToErrno(ER_WRONG_VALUE_COUNT), EINVAL);
    EXPECT_EQ(ErrorHandler::mysqlToErrno(ER_DATA_TOO_LONG), EINVAL);
}

TEST_F(ErrorHandlerTest, LockErrorsMapToETIMEDOUT) {
    EXPECT_EQ(ErrorHandler::mysqlToErrno(ER_LOCK_WAIT_TIMEOUT), ETIMEDOUT);
    EXPECT_EQ(ErrorHandler::mysqlToErrno(ER_LOCK_DEADLOCK), ETIMEDOUT);
}

TEST_F(ErrorHandlerTest, ReadOnlyErrorsMapToEROFS) {
    EXPECT_EQ(ErrorHandler::mysqlToErrno(ER_OPTION_PREVENTS_STATEMENT), EROFS);
    EXPECT_EQ(ErrorHandler::mysqlToErrno(ER_READ_ONLY_MODE), EROFS);
}

TEST_F(ErrorHandlerTest, DiskErrorsMapToENOSPC) {
    EXPECT_EQ(ErrorHandler::mysqlToErrno(ER_RECORD_FILE_FULL), ENOSPC);
}

TEST_F(ErrorHandlerTest, BusyErrorsMapToEBUSY) {
    EXPECT_EQ(ErrorHandler::mysqlToErrno(ER_LOCK_TABLE_FULL), EBUSY);
    EXPECT_EQ(ErrorHandler::mysqlToErrno(ER_TOO_MANY_CONCURRENT_TRXS), EBUSY);
}

TEST_F(ErrorHandlerTest, UnknownErrorMapsToEIO) {
    EXPECT_EQ(ErrorHandler::mysqlToErrno(99999), EIO);
}

// Retryable error tests
TEST_F(ErrorHandlerTest, ServerGoneIsRetryable) {
    EXPECT_TRUE(ErrorHandler::isRetryable(CR_SERVER_GONE_ERROR));
    EXPECT_TRUE(ErrorHandler::isRetryable(CR_SERVER_LOST));
}

TEST_F(ErrorHandlerTest, LockTimeoutIsRetryable) {
    EXPECT_TRUE(ErrorHandler::isRetryable(ER_LOCK_WAIT_TIMEOUT));
    EXPECT_TRUE(ErrorHandler::isRetryable(ER_LOCK_DEADLOCK));
}

TEST_F(ErrorHandlerTest, TooManyConcurrentTrxIsRetryable) {
    EXPECT_TRUE(ErrorHandler::isRetryable(ER_TOO_MANY_CONCURRENT_TRXS));
}

TEST_F(ErrorHandlerTest, AccessDeniedIsNotRetryable) {
    EXPECT_FALSE(ErrorHandler::isRetryable(ER_ACCESS_DENIED_ERROR));
}

TEST_F(ErrorHandlerTest, SyntaxErrorIsNotRetryable) {
    EXPECT_FALSE(ErrorHandler::isRetryable(ER_SYNTAX_ERROR));
}

TEST_F(ErrorHandlerTest, UnknownErrorIsNotRetryable) {
    EXPECT_FALSE(ErrorHandler::isRetryable(0));
    EXPECT_FALSE(ErrorHandler::isRetryable(99999));
}

// Connection error tests
TEST_F(ErrorHandlerTest, IsConnectionError) {
    EXPECT_TRUE(ErrorHandler::isConnectionError(CR_CONNECTION_ERROR));
    EXPECT_TRUE(ErrorHandler::isConnectionError(CR_CONN_HOST_ERROR));
    EXPECT_TRUE(ErrorHandler::isConnectionError(CR_UNKNOWN_HOST));
    EXPECT_TRUE(ErrorHandler::isConnectionError(CR_SERVER_GONE_ERROR));
    EXPECT_TRUE(ErrorHandler::isConnectionError(CR_SERVER_LOST));
    EXPECT_TRUE(ErrorHandler::isConnectionError(CR_COMMANDS_OUT_OF_SYNC));
    EXPECT_TRUE(ErrorHandler::isConnectionError(CR_SOCKET_CREATE_ERROR));
}

TEST_F(ErrorHandlerTest, IsNotConnectionError) {
    EXPECT_FALSE(ErrorHandler::isConnectionError(ER_ACCESS_DENIED_ERROR));
    EXPECT_FALSE(ErrorHandler::isConnectionError(ER_SYNTAX_ERROR));
    EXPECT_FALSE(ErrorHandler::isConnectionError(ER_NO_SUCH_TABLE));
    EXPECT_FALSE(ErrorHandler::isConnectionError(0));
}

// Error message tests
TEST_F(ErrorHandlerTest, GetErrorMessageSuccess) {
    EXPECT_EQ(ErrorHandler::getErrorMessage(0u), "Success");
}

TEST_F(ErrorHandlerTest, GetErrorMessageKnownErrors) {
    EXPECT_EQ(ErrorHandler::getErrorMessage(CR_CONNECTION_ERROR), "Connection error");
    EXPECT_EQ(ErrorHandler::getErrorMessage(CR_UNKNOWN_HOST), "Unknown host");
    EXPECT_EQ(ErrorHandler::getErrorMessage(CR_SERVER_GONE_ERROR), "MySQL server has gone away");
    EXPECT_EQ(ErrorHandler::getErrorMessage(CR_SERVER_LOST), "Lost connection to MySQL server");
    EXPECT_EQ(ErrorHandler::getErrorMessage(ER_ACCESS_DENIED_ERROR), "Access denied");
    EXPECT_EQ(ErrorHandler::getErrorMessage(ER_BAD_DB_ERROR), "Unknown database");
    EXPECT_EQ(ErrorHandler::getErrorMessage(ER_NO_SUCH_TABLE), "Table does not exist");
    EXPECT_EQ(ErrorHandler::getErrorMessage(ER_DUP_ENTRY), "Duplicate entry");
    EXPECT_EQ(ErrorHandler::getErrorMessage(ER_PARSE_ERROR), "SQL parse error");
    EXPECT_EQ(ErrorHandler::getErrorMessage(ER_LOCK_WAIT_TIMEOUT), "Lock wait timeout");
    EXPECT_EQ(ErrorHandler::getErrorMessage(ER_LOCK_DEADLOCK), "Deadlock detected");
}

TEST_F(ErrorHandlerTest, GetErrorMessageUnknown) {
    auto msg = ErrorHandler::getErrorMessage(99999);
    EXPECT_THAT(msg, ::testing::HasSubstr("MySQL error 99999"));
}

TEST_F(ErrorHandlerTest, GetErrorMessageNullConnection) {
    EXPECT_EQ(ErrorHandler::getErrorMessage(nullptr), "No connection");
}

// Error constants tests
TEST_F(ErrorHandlerTest, ErrorConstants) {
    EXPECT_EQ(ErrorHandler::SUCCESS, 0);
    EXPECT_EQ(ErrorHandler::ERR_NOT_FOUND, -ENOENT);
    EXPECT_EQ(ErrorHandler::ERR_ACCESS_DENIED, -EACCES);
    EXPECT_EQ(ErrorHandler::ERR_IO, -EIO);
    EXPECT_EQ(ErrorHandler::ERR_INVALID, -EINVAL);
    EXPECT_EQ(ErrorHandler::ERR_EXISTS, -EEXIST);
    EXPECT_EQ(ErrorHandler::ERR_NOT_DIR, -ENOTDIR);
    EXPECT_EQ(ErrorHandler::ERR_IS_DIR, -EISDIR);
    EXPECT_EQ(ErrorHandler::ERR_TIMEOUT, -ETIMEDOUT);
    EXPECT_EQ(ErrorHandler::ERR_READ_ONLY, -EROFS);
    EXPECT_EQ(ErrorHandler::ERR_NO_SPACE, -ENOSPC);
    EXPECT_EQ(ErrorHandler::ERR_BUSY, -EBUSY);
}

// ErrorContext tests
class ErrorContextTest : public ::testing::Test {
};

TEST_F(ErrorContextTest, SetAndGetContext) {
    EXPECT_TRUE(ErrorContext::current().empty());

    {
        ErrorContext ctx("operation1");
        EXPECT_EQ(ErrorContext::current(), "operation1");
    }

    EXPECT_TRUE(ErrorContext::current().empty());
}

TEST_F(ErrorContextTest, NestedContext) {
    {
        ErrorContext ctx1("level1");
        EXPECT_EQ(ErrorContext::current(), "level1");

        {
            ErrorContext ctx2("level2");
            EXPECT_EQ(ErrorContext::current(), "level1 > level2");

            {
                ErrorContext ctx3("level3");
                EXPECT_EQ(ErrorContext::current(), "level1 > level2 > level3");
            }

            EXPECT_EQ(ErrorContext::current(), "level1 > level2");
        }

        EXPECT_EQ(ErrorContext::current(), "level1");
    }

    EXPECT_TRUE(ErrorContext::current().empty());
}

TEST_F(ErrorContextTest, ContextRestoredOnException) {
    try {
        ErrorContext ctx1("outer");
        {
            ErrorContext ctx2("inner");
            throw std::runtime_error("test");
        }
    } catch (...) {
        // Context should be restored even on exception
    }

    EXPECT_TRUE(ErrorContext::current().empty());
}

// MySQLException tests
class MySQLExceptionTest : public ::testing::Test {
};

TEST_F(MySQLExceptionTest, CreateWithErrorCodeAndMessage) {
    MySQLException ex(ER_ACCESS_DENIED_ERROR, "Access denied for user 'test'");

    EXPECT_EQ(ex.errorCode(), ER_ACCESS_DENIED_ERROR);
    EXPECT_EQ(ex.posixError(), EACCES);
    EXPECT_STREQ(ex.what(), "Access denied for user 'test'");
}

TEST_F(MySQLExceptionTest, PosixErrorMapping) {
    MySQLException ex1(ER_NO_SUCH_TABLE, "Table not found");
    EXPECT_EQ(ex1.posixError(), ENOENT);

    MySQLException ex2(ER_DUP_ENTRY, "Duplicate entry");
    EXPECT_EQ(ex2.posixError(), EEXIST);

    MySQLException ex3(ER_PARSE_ERROR, "Parse error");
    EXPECT_EQ(ex3.posixError(), EINVAL);
}

TEST_F(MySQLExceptionTest, CanBeCaught) {
    bool caught = false;

    try {
        throw MySQLException(ER_ACCESS_DENIED_ERROR, "test");
    } catch (const MySQLException& ex) {
        caught = true;
        EXPECT_EQ(ex.errorCode(), ER_ACCESS_DENIED_ERROR);
    }

    EXPECT_TRUE(caught);
}

TEST_F(MySQLExceptionTest, CanBeCaughtAsRuntimeError) {
    bool caught = false;

    try {
        throw MySQLException(ER_ACCESS_DENIED_ERROR, "test");
    } catch (const std::runtime_error& ex) {
        caught = true;
    }

    EXPECT_TRUE(caught);
}

// ExecuteWithRetry tests (basic logic test without actual MySQL)
TEST_F(ErrorHandlerTest, ExecuteWithRetrySuccessOnFirstTry) {
    int callCount = 0;
    auto result = ErrorHandler::executeWithRetry([&callCount]() {
        callCount++;
        return 0;  // Success
    }, 3);

    EXPECT_EQ(result, 0);
    EXPECT_EQ(callCount, 1);
}

TEST_F(ErrorHandlerTest, ExecuteWithRetryNonRetryableError) {
    int callCount = 0;
    auto result = ErrorHandler::executeWithRetry([&callCount]() {
        callCount++;
        return -static_cast<int>(ER_ACCESS_DENIED_ERROR);  // Non-retryable
    }, 3);

    EXPECT_NE(result, 0);
    EXPECT_EQ(callCount, 1);  // Should not retry
}
