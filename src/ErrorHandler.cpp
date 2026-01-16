#include "ErrorHandler.hpp"
#include <mysql/errmsg.h>
#include <mysql/mysqld_error.h>
#include <cerrno>
#include <thread>

namespace sqlfuse {

thread_local std::string ErrorContext::s_currentContext;

int ErrorHandler::mysqlToErrno(unsigned int mysql_error) {
    switch (mysql_error) {
        // Success
        case 0:
            return 0;

        // Connection errors
        case CR_CONNECTION_ERROR:
        case CR_CONN_HOST_ERROR:
        case CR_UNKNOWN_HOST:
        case CR_SERVER_GONE_ERROR:
        case CR_SERVER_LOST:
        case CR_SERVER_LOST_EXTENDED:
            return ENOENT;

        // Authentication errors
        case ER_ACCESS_DENIED_ERROR:
        case ER_DBACCESS_DENIED_ERROR:
        case ER_TABLEACCESS_DENIED_ERROR:
        case ER_COLUMNACCESS_DENIED_ERROR:
        case ER_SPECIFIC_ACCESS_DENIED_ERROR:
            return EACCES;

        // Not found errors
        case ER_BAD_DB_ERROR:
        case ER_NO_SUCH_TABLE:
        case ER_BAD_TABLE_ERROR:
        case ER_UNKNOWN_TABLE:
        case ER_NO_SUCH_THREAD:
        case ER_SP_DOES_NOT_EXIST:
        case ER_TRG_DOES_NOT_EXIST:
            return ENOENT;

        // Already exists errors
        case ER_DB_CREATE_EXISTS:
        case ER_TABLE_EXISTS_ERROR:
        case ER_DUP_ENTRY:
        case ER_DUP_KEY:
            return EEXIST;

        // Invalid input errors
        case ER_PARSE_ERROR:
        case ER_SYNTAX_ERROR:
        case ER_BAD_FIELD_ERROR:
        case ER_WRONG_VALUE_COUNT:
        case ER_WRONG_VALUE_COUNT_ON_ROW:
        case ER_TRUNCATED_WRONG_VALUE:
        case ER_WRONG_TYPE_FOR_VAR:
        case ER_WRONG_VALUE_FOR_VAR:
        case ER_DATA_TOO_LONG:
        case ER_TRUNCATED_WRONG_VALUE_FOR_FIELD:
            return EINVAL;

        // Constraint violations
        case ER_ROW_IS_REFERENCED:
        case ER_ROW_IS_REFERENCED_2:
        case ER_NO_REFERENCED_ROW:
        case ER_NO_REFERENCED_ROW_2:
            return EINVAL;

        // Lock/timeout errors
        case ER_LOCK_WAIT_TIMEOUT:
        case ER_LOCK_DEADLOCK:
            return ETIMEDOUT;

        // Read-only errors
        case ER_OPTION_PREVENTS_STATEMENT:
        case ER_READ_ONLY_MODE:
            return EROFS;

        // Disk/space errors
        case ER_RECORD_FILE_FULL:
            return ENOSPC;

        // Busy errors
        case ER_LOCK_TABLE_FULL:
        case ER_TOO_MANY_CONCURRENT_TRXS:
            return EBUSY;

        // Default to I/O error
        default:
            return EIO;
    }
}

bool ErrorHandler::isRetryable(unsigned int mysql_error) {
    switch (mysql_error) {
        case CR_SERVER_GONE_ERROR:
        case CR_SERVER_LOST:
        case CR_SERVER_LOST_EXTENDED:
        case ER_LOCK_WAIT_TIMEOUT:
        case ER_LOCK_DEADLOCK:
        case ER_TOO_MANY_CONCURRENT_TRXS:
            return true;
        default:
            return false;
    }
}

bool ErrorHandler::isConnectionError(unsigned int mysql_error) {
    switch (mysql_error) {
        case CR_CONNECTION_ERROR:
        case CR_CONN_HOST_ERROR:
        case CR_UNKNOWN_HOST:
        case CR_SERVER_GONE_ERROR:
        case CR_SERVER_LOST:
        case CR_SERVER_LOST_EXTENDED:
        case CR_COMMANDS_OUT_OF_SYNC:
        case CR_SOCKET_CREATE_ERROR:
        case CR_NAMEDPIPEOPEN_ERROR:
        case CR_NAMEDPIPEWAIT_ERROR:
        case CR_NAMEDPIPESETSTATE_ERROR:
        case CR_IPSOCK_ERROR:
            return true;
        default:
            return false;
    }
}

std::string ErrorHandler::getErrorMessage(unsigned int mysql_error) {
    switch (mysql_error) {
        case 0:
            return "Success";
        case CR_CONNECTION_ERROR:
            return "Connection error";
        case CR_CONN_HOST_ERROR:
            return "Cannot connect to host";
        case CR_UNKNOWN_HOST:
            return "Unknown host";
        case CR_SERVER_GONE_ERROR:
            return "MySQL server has gone away";
        case CR_SERVER_LOST:
            return "Lost connection to MySQL server";
        case ER_ACCESS_DENIED_ERROR:
            return "Access denied";
        case ER_BAD_DB_ERROR:
            return "Unknown database";
        case ER_NO_SUCH_TABLE:
            return "Table does not exist";
        case ER_DUP_ENTRY:
            return "Duplicate entry";
        case ER_PARSE_ERROR:
            return "SQL parse error";
        case ER_LOCK_WAIT_TIMEOUT:
            return "Lock wait timeout";
        case ER_LOCK_DEADLOCK:
            return "Deadlock detected";
        default:
            return "MySQL error " + std::to_string(mysql_error);
    }
}

std::string ErrorHandler::getErrorMessage(MYSQL* conn) {
    if (!conn) {
        return "No connection";
    }
    const char* err = mysql_error(conn);
    if (err && *err) {
        return std::string(err);
    }
    return getErrorMessage(mysql_errno(conn));
}

ErrorContext::ErrorContext(const std::string& context)
    : m_previous(s_currentContext) {
    if (s_currentContext.empty()) {
        s_currentContext = context;
    } else {
        s_currentContext = s_currentContext + " > " + context;
    }
}

ErrorContext::~ErrorContext() {
    s_currentContext = m_previous;
}

std::string ErrorContext::current() {
    return s_currentContext;
}

MySQLException::MySQLException(unsigned int error_code, const std::string& message)
    : std::runtime_error(message)
    , m_errorCode(error_code) {
}

MySQLException::MySQLException(MYSQL* conn)
    : std::runtime_error(ErrorHandler::getErrorMessage(conn))
    , m_errorCode(mysql_errno(conn)) {
}

}  // namespace sqlfuse
