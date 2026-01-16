#pragma once

#include <mysql/mysql.h>
#include <string>
#include <functional>
#include <system_error>
#include <thread>
#include <chrono>
#include <cerrno>

namespace sqlfuse {

// Database error codes to POSIX errno mapping
class ErrorHandler {
public:
    // Convert MySQL error to errno
    static int mysqlToErrno(unsigned int mysqlError);

    // Convert Oracle error to errno
    static int oracleToErrno(int oracleError);

    // Check if error is retryable
    static bool isRetryable(unsigned int mysqlError);

    // Check if error indicates connection issue
    static bool isConnectionError(unsigned int mysqlError);

    // Get human-readable error message
    static std::string getErrorMessage(unsigned int mysqlError);
    static std::string getErrorMessage(MYSQL* conn);

    // Execute with retry logic
    template<typename Func>
    static int executeWithRetry(Func&& operation, int maxRetries = 3) {
        int retries = 0;
        int result;

        while (retries < maxRetries) {
            result = operation();
            if (result == 0) {
                return 0;
            }

            // Check if we should retry
            unsigned int err = static_cast<unsigned int>(-result);
            if (!isRetryable(err)) {
                return result;
            }

            retries++;
            if (retries < maxRetries) {
                // Exponential backoff: 100ms, 200ms, 400ms...
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(100 * (1 << retries)));
            }
        }

        return result;
    }

    // Common error codes
    static constexpr int SUCCESS = 0;
    static constexpr int ERR_NOT_FOUND = -ENOENT;
    static constexpr int ERR_ACCESS_DENIED = -EACCES;
    static constexpr int ERR_IO = -EIO;
    static constexpr int ERR_INVALID = -EINVAL;
    static constexpr int ERR_EXISTS = -EEXIST;
    static constexpr int ERR_NOT_DIR = -ENOTDIR;
    static constexpr int ERR_IS_DIR = -EISDIR;
    static constexpr int ERR_TIMEOUT = -ETIMEDOUT;
    static constexpr int ERR_READ_ONLY = -EROFS;
    static constexpr int ERR_NO_SPACE = -ENOSPC;
    static constexpr int ERR_BUSY = -EBUSY;
};

// RAII wrapper for setting/clearing error context
class ErrorContext {
public:
    ErrorContext(const std::string& context);
    ~ErrorContext();

    static std::string current();

private:
    static thread_local std::string s_currentContext;
    std::string m_previous;
};

// Exception for MySQL errors
class MySQLException : public std::runtime_error {
public:
    MySQLException(unsigned int errorCode, const std::string& message);
    MySQLException(MYSQL* conn);

    unsigned int errorCode() const { return m_errorCode; }
    int posixError() const { return ErrorHandler::mysqlToErrno(m_errorCode); }

private:
    unsigned int m_errorCode;
};

}  // namespace sqlfuse
