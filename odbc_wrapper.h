#ifndef MODERN_ODBC_WRAPPER_H
#define MODERN_ODBC_WRAPPER_H

#include <string>
#include <string_view>
#include <vector>
#include <expected>
#include <optional>
#include <stdexcept>
#include <utility>
#include <iostream>
#include <format>
#include <cstdint> // For uintptr_t

// Platform-specific ODBC and thread includes
#ifdef _WIN32
#include <Windows.h>
#else
#include <pthread.h>
#endif
#include <sql.h>
#include <sqlext.h>

namespace odbc {

// --- Error and Exception Classes ---

/**
 * @struct OdbcError
 * @brief Represents a detailed ODBC error, containing diagnostic information.
 */
struct OdbcError {
    std::string sql_state;
    long native_error = 0;
    std::string message;

    [[nodiscard]] std::string to_string() const;
};

/**
 * @class OdbcSetupError
 * @brief Custom exception for errors during ODBC resource allocation/setup.
 * Inherits directly from std::exception for more specific error handling.
 */
class OdbcSetupError : public std::exception {
private:
    std::string m_message;
public:
    explicit OdbcSetupError(const std::string& message) : m_message(message) {}
    [[nodiscard]] const char* what() const noexcept override {
        return m_message.c_str();
    }
};


// --- Helper Function ---

/**
 * @brief Retrieves detailed error information from an ODBC handle.
 * @param handle The ODBC handle that produced an error.
 * @param handle_type The type of the handle.
 * @return An std::optional<OdbcError> containing the error, or std::nullopt if no error is found.
 */
inline std::optional<OdbcError> get_diagnostic_record(SQLHANDLE handle, SQLSMALLINT handle_type) {
    OdbcError error;
    std::vector<SQLCHAR> sql_state_buffer(6);
    SQLINTEGER native_error = 0;
    std::vector<SQLCHAR> message_text_buffer(SQL_MAX_MESSAGE_LENGTH);
    SQLSMALLINT text_length = 0;

    if (SQLRETURN ret = SQLGetDiagRec(handle_type, handle, 1, 
                                  sql_state_buffer.data(), &native_error,
                                  message_text_buffer.data(), 
                                  static_cast<SQLSMALLINT>(message_text_buffer.size()), 
                                  &text_length); SQL_SUCCEEDED(ret)) {
        error.sql_state = reinterpret_cast<const char*>(sql_state_buffer.data());
        error.message.assign(reinterpret_cast<const char*>(message_text_buffer.data()), text_length);
        error.native_error = native_error;
        return error;
    }
    return std::nullopt;
}

inline std::string OdbcError::to_string() const {
    return std::format("ODBC Error: SQLSTATE={}, NativeError={}, Message='{}'",
                       sql_state, native_error, message);
}


// --- RAII Wrapper Classes ---

class Environment;
class Connection;
class Statement;

/**
 * @class Environment
 * @brief RAII wrapper for an ODBC Environment Handle (HENV).
 */
class Environment {
public:
    Environment();
    ~Environment();
    Environment(const Environment&) = delete;
    Environment& operator=(const Environment&) = delete;
    Environment(Environment&& other) noexcept;
    Environment& operator=(Environment&& other) noexcept;

    [[nodiscard]] SQLHENV get() const;

private:
    SQLHENV m_handle = nullptr;
};

/**
 * @class Connection
 * @brief RAII wrapper for an ODBC Connection Handle (HDBC).
 */
class Connection {
public:
    explicit Connection(const Environment& env);
    ~Connection() noexcept;
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    Connection(Connection&& other) noexcept;
    Connection& operator=(Connection&& other) noexcept;

    [[nodiscard]] SQLHDBC get() const;

    [[nodiscard]] std::expected<void, OdbcError> driver_connect(std::string_view connection_string);
    [[nodiscard]] std::expected<void, OdbcError> disconnect();

private:
    SQLHDBC m_handle = nullptr;
};

/**
 * @class Statement
 * @brief RAII wrapper for an ODBC Statement Handle (HSTMT).
 */
class Statement {
public:
    explicit Statement(const Connection& conn);
    ~Statement();
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&& other) noexcept;
    Statement& operator=(Statement&& other) noexcept;

    [[nodiscard]] SQLHSTMT get() const;

    [[nodiscard]] std::expected<void, OdbcError> execute_direct(std::string_view query);
    [[nodiscard]] std::expected<bool, OdbcError> fetch();
    [[nodiscard]] std::expected<SQLLEN, OdbcError> row_count();
    
    template <typename T>
    [[nodiscard]] std::expected<std::optional<T>, OdbcError> get_data(SQLUSMALLINT column_index);

private:
    SQLHSTMT m_handle = nullptr;
};

// --- Implementation ---

// --- Environment Implementation ---
inline Environment::Environment() {
    if (!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &m_handle))) {
        throw OdbcSetupError("ODBC: Failed to allocate environment handle.");
    }
    if (!SQL_SUCCEEDED(SQLSetEnvAttr(m_handle, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0))) {
        SQLFreeHandle(SQL_HANDLE_ENV, m_handle);
        m_handle = nullptr;
        throw OdbcSetupError("ODBC: Failed to set environment attribute to ODBC 3.0.");
    }
}

inline Environment::~Environment() {
    if (m_handle != nullptr) {
        SQLFreeHandle(SQL_HANDLE_ENV, m_handle);
    }
}

inline Environment::Environment(Environment&& other) noexcept
    : m_handle(std::exchange(other.m_handle, nullptr)) {}

inline Environment& Environment::operator=(Environment&& other) noexcept {
    if (this != &other) {
        if (m_handle != nullptr) {
            SQLFreeHandle(SQL_HANDLE_ENV, m_handle);
        }
        m_handle = std::exchange(other.m_handle, nullptr);
    }
    return *this;
}

inline SQLHENV Environment::get() const { return m_handle; }

// --- Connection Implementation ---
inline Connection::Connection(const Environment& env) {
    if (!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_DBC, env.get(), &m_handle))) {
        throw OdbcSetupError("ODBC: Failed to allocate connection handle.");
    }
}

inline Connection::~Connection() noexcept {
    try {
        if (m_handle != nullptr) {
            #ifdef _WIN32
                const auto thread_id = static_cast<uintptr_t>(GetCurrentThreadId());
            #else
                const auto thread_id = reinterpret_cast<uintptr_t>(pthread_self());
            #endif
            std::cerr << std::format("[Thread 0x{:x}] Closing connection via destructor.\n", thread_id);
            
            SQLDisconnect(m_handle);
            SQLFreeHandle(SQL_HANDLE_DBC, m_handle);
        }
    } catch (...) {
        #ifdef _WIN32
            const auto thread_id = static_cast<uintptr_t>(GetCurrentThreadId());
        #else
            const auto thread_id = reinterpret_cast<uintptr_t>(pthread_self());
        #endif
        std::cerr << std::format("[Thread 0x{:x}] WARNING: Exception caught and suppressed in Connection destructor.\n", thread_id);
    }
}

inline Connection::Connection(Connection&& other) noexcept
    : m_handle(std::exchange(other.m_handle, nullptr)) {}

inline Connection& Connection::operator=(Connection&& other) noexcept {
    if (this != &other) {
        if (m_handle != nullptr) {
            SQLDisconnect(m_handle);
            SQLFreeHandle(SQL_HANDLE_DBC, m_handle);
        }
        m_handle = std::exchange(other.m_handle, nullptr);
    }
    return *this;
}

inline SQLHDBC Connection::get() const { return m_handle; }

inline std::expected<void, OdbcError> Connection::driver_connect(std::string_view connection_string) {
    std::vector<SQLCHAR> conn_str_buffer(connection_string.begin(), connection_string.end());
    conn_str_buffer.push_back('\0');

    if (SQLRETURN ret = SQLDriverConnect(m_handle, nullptr, conn_str_buffer.data(), SQL_NTS, nullptr, 0, nullptr, SQL_DRIVER_NOPROMPT); 
        !SQL_SUCCEEDED(ret)) {
        return std::unexpected(get_diagnostic_record(m_handle, SQL_HANDLE_DBC)
            .value_or(OdbcError{"HY000", 0, "Unknown connection error via DriverConnect"}));
    }

    #ifdef _WIN32
        const auto thread_id = static_cast<uintptr_t>(GetCurrentThreadId());
    #else
        const auto thread_id = reinterpret_cast<uintptr_t>(pthread_self());
    #endif
    std::cerr << std::format("[Thread 0x{:x}] Connection established successfully.\n", thread_id);
    
    return {};
}

inline std::expected<void, OdbcError> Connection::disconnect() {
    #ifdef _WIN32
        const auto thread_id = static_cast<uintptr_t>(GetCurrentThreadId());
    #else
        const auto thread_id = reinterpret_cast<uintptr_t>(pthread_self());
    #endif
    std::cerr << std::format("[Thread 0x{:x}] Explicitly disconnecting connection.\n", thread_id);
    
    if (SQLRETURN ret = SQLDisconnect(m_handle); !SQL_SUCCEEDED(ret)) {
        return std::unexpected(get_diagnostic_record(m_handle, SQL_HANDLE_DBC)
            .value_or(OdbcError{"HY000", 0, "Unknown disconnection error"}));
    }
    return {};
}

// --- Statement Implementation ---
inline Statement::Statement(const Connection& conn) {
    if (!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_STMT, conn.get(), &m_handle))) {
        throw OdbcSetupError("ODBC: Failed to allocate statement handle.");
    }
}

inline Statement::~Statement() {
    if (m_handle != nullptr) {
        SQLFreeHandle(SQL_HANDLE_STMT, m_handle);
    }
}

inline Statement::Statement(Statement&& other) noexcept
    : m_handle(std::exchange(other.m_handle, nullptr)) {}

inline Statement& Statement::operator=(Statement&& other) noexcept {
    if (this != &other) {
        if (m_handle != nullptr) {
            SQLFreeHandle(SQL_HANDLE_STMT, m_handle);
        }
        m_handle = std::exchange(other.m_handle, nullptr);
    }
    return *this;
}

inline SQLHSTMT Statement::get() const { return m_handle; }

inline std::expected<void, OdbcError> Statement::execute_direct(std::string_view query) {
    std::vector<SQLCHAR> query_buffer(query.begin(), query.end());
    if (SQLRETURN ret = SQLExecDirect(m_handle, query_buffer.data(), static_cast<SQLINTEGER>(query_buffer.size())); 
        !SQL_SUCCEEDED(ret)) {
        return std::unexpected(get_diagnostic_record(m_handle, SQL_HANDLE_STMT)
            .value_or(OdbcError{"HY000", 0, "Unknown execution error"}));
    }
    return {};
}

inline std::expected<SQLLEN, OdbcError> Statement::row_count() {
    SQLLEN count = 0;
    if (SQLRETURN ret = SQLRowCount(m_handle, &count); !SQL_SUCCEEDED(ret)) {
         return std::unexpected(get_diagnostic_record(m_handle, SQL_HANDLE_STMT)
            .value_or(OdbcError{"HY000", 0, "Unknown error getting row count"}));
    }
    return count;
}


inline std::expected<bool, OdbcError> Statement::fetch() {
    if (SQLRETURN ret = SQLFetch(m_handle); SQL_SUCCEEDED(ret)) {
        return true;
    } else if (ret == SQL_NO_DATA) {
        return false;
    }
    
    // If we reach here, it must be an error.
    return std::unexpected(get_diagnostic_record(m_handle, SQL_HANDLE_STMT)
        .value_or(OdbcError{"HY000", 0, "Unknown fetch error"}));
}

namespace detail {
    // Helper function to encapsulate the complex logic for retrieving string data.
    // This reduces the cognitive complexity of the main get_data function.
    inline std::expected<std::optional<std::string>, OdbcError> get_string_data(SQLHSTMT hstmt, SQLUSMALLINT column_index) {
        std::vector<char> buffer(1024);
        SQLLEN indicator = 0;
        
        SQLRETURN ret = SQLGetData(hstmt, column_index, SQL_C_CHAR, buffer.data(), buffer.size(), &indicator);

        // Check if the buffer was too small and resize if necessary.
        if (ret == SQL_SUCCESS_WITH_INFO && indicator > static_cast<SQLLEN>(buffer.size() - 1)) {
            buffer.resize(static_cast<size_t>(indicator) + 1);
            ret = SQLGetData(hstmt, column_index, SQL_C_CHAR, buffer.data(), buffer.size(), &indicator);
        }
        
        // After potential resize, check for final failure.
        if (!SQL_SUCCEEDED(ret)) {
            if (indicator == SQL_NULL_DATA) {
                return std::optional<std::string>(std::nullopt);
            }
            return std::unexpected(get_diagnostic_record(hstmt, SQL_HANDLE_STMT).value_or(OdbcError{"HY000", 0, "Unknown GetData<string> error"}));
        }
        
        // Check for NULL data on success.
        if (indicator == SQL_NULL_DATA) {
            return std::optional<std::string>(std::nullopt);
        }
        
        std::string str_value;
        if (indicator > 0) {
            str_value.assign(buffer.data(), static_cast<size_t>(indicator));
        }
        return std::optional<std::string>(std::move(str_value));
    }
} // namespace detail


template <typename T>
inline std::expected<std::optional<T>, OdbcError> Statement::get_data(SQLUSMALLINT column_index) {
    // For strings, delegate to a helper function to reduce cognitive complexity here.
    if constexpr (std::is_same_v<T, std::string>) {
        return detail::get_string_data(m_handle, column_index);
    }
    
    // Logic for non-string types.
    T value{};
    SQLLEN indicator = 0;

    if constexpr (std::is_same_v<T, long>) {
        if (SQLRETURN ret = SQLGetData(m_handle, column_index, SQL_C_SLONG, &value, sizeof(value), &indicator); 
            !SQL_SUCCEEDED(ret)) {
            return std::unexpected(get_diagnostic_record(m_handle, SQL_HANDLE_STMT).value_or(OdbcError{"HY000", 0, "Unknown GetData<long> error"}));
        }
    } else if constexpr (std::is_same_v<T, double>) {
        if (SQLRETURN ret = SQLGetData(m_handle, column_index, SQL_C_DOUBLE, &value, sizeof(value), &indicator); 
            !SQL_SUCCEEDED(ret)) {
            return std::unexpected(get_diagnostic_record(m_handle, SQL_HANDLE_STMT).value_or(OdbcError{"HY000", 0, "Unknown GetData<double> error"}));
        }
    }
    
    if (indicator == SQL_NULL_DATA) {
        return std::optional<T>(std::nullopt);
    }

    return std::optional<T>(value);
}

} // namespace odbc

#endif // MODERN_ODBC_WRAPPER_H
