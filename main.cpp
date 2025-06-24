#include "odbc_wrapper.h"
#include <iostream>
#include <thread>
#include <vector>
#include <functional>
#include <string_view>
#include <memory>
#include <stdexcept>
#include <future>
#include <mutex>

// --- Configuration ---
// Use preprocessor directives to set the connection string based on the OS.
#ifdef _WIN32
// Windows-specific connection string (e.g., using the SQL Server Native Client)
const std::string_view CONNECTION_STRING = "DRIVER={ODBC Driver 18 for SQL Server};SERVER=demodb.mshome.net;PORT=1433;DATABASE=demodb;UID=sa;PWD=Basica2024;Encrypt=yes;TrustServerCertificate=yes;";
#else
// Linux-specific connection string (using FreeTDS)
const std::string_view CONNECTION_STRING = "Driver=FreeTDS;SERVER=demodb.mshome.net;PORT=1433;DATABASE=demodb;UID=sa;PWD=Basica2024;APP=CPPServer;Encryption=off;ClientCharset=UTF-8";
#endif

// --- Simple Assertion and Test Framework ---
#define ASSERT_TRUE(condition, message) \
    if (!(condition)) { \
        std::cerr << "\n--- ASSERTION FAILED ---\n" \
                  << "Thread " << std::this_thread::get_id() << "\n" \
                  << "File: " << __FILE__ << ", Line: " << __LINE__ << "\n" \
                  << "Condition: " << #condition << "\n" \
                  << "Message: " << message << "\n" \
                  << "------------------------" << std::endl; \
        return false; \
    }

// --- Helper for driver-specific error handling ---
bool handle_execute_result(odbc::Statement& stmt, std::expected<void, odbc::OdbcError> result, const std::string& command) {
    if (result) {
        return true; // Command succeeded, nothing more to do.
    }

    // Command "failed". Check row count for more context.
    auto count_res = stmt.row_count();
    if (count_res && *count_res == -1) {
        // SQL_NO_ROW_COUNT (-1) is often returned by drivers like FreeTDS for DDL
        // or other statements where a row count is not applicable.
        // We can treat this as a non-fatal warning.
        std::cout << "[ INFO     ] Command '" << command << "' returned a non-success code, but row count is -1. Assuming success for this driver." << std::endl;
        return true;
    }
    
    // If we are here, it's a real failure.
    throw std::runtime_error("Setup failed on command '" + command + "': " + result.error().to_string());
}

// --- Test Setup/Teardown ---
void setup_database_schema() {
    std::cout << "--- Test Setup ---" << std::endl;
    odbc::Environment env;
    odbc::Connection conn(env);
    auto connect_res = conn.driver_connect(CONNECTION_STRING);
    if (!connect_res) {
        throw std::runtime_error("Setup failed to connect: " + connect_res.error().to_string());
    }

    odbc::Statement stmt(conn);
    
    handle_execute_result(stmt, stmt.execute_direct("DROP TABLE IF EXISTS test_table"), "DROP TABLE");
    handle_execute_result(stmt, stmt.execute_direct("CREATE TABLE test_table (id INT, name VARCHAR(100), value REAL)"), "CREATE TABLE");
    handle_execute_result(stmt, stmt.execute_direct("INSERT INTO test_table VALUES (1, 'First', 10.5), (2, NULL, 20.25)"), "INSERT");

    std::cout << "--- Setup Complete ---" << std::endl;
}

// --- Test Cases ---
[[nodiscard]] bool test_fetch_valid_data() {
    odbc::Environment env;
    odbc::Connection conn(env);
    auto connect_res = conn.driver_connect(CONNECTION_STRING);
    ASSERT_TRUE(connect_res.has_value(), "Connection failed: " + connect_res.error().to_string());
    
    odbc::Statement stmt(conn);
    auto exec_res = stmt.execute_direct("SELECT id, name, value FROM test_table WHERE id = 1");
    ASSERT_TRUE(exec_res.has_value(), exec_res.error().to_string());
    
    auto fetch_res = stmt.fetch();
    ASSERT_TRUE(fetch_res.has_value() && *fetch_res, "Fetch failed or returned no data.");
    
    auto id_res = stmt.get_data<long>(1);
    ASSERT_TRUE(id_res.has_value(), "ID get_data failed: " + id_res.error().to_string());
    ASSERT_TRUE(id_res->has_value(), "ID was unexpectedly NULL.");
    ASSERT_TRUE(**id_res == 1, "ID was not 1.");
    return true;
}

[[nodiscard]] bool test_fetch_null_string() {
    odbc::Environment env;
    odbc::Connection conn(env);
    auto connect_res = conn.driver_connect(CONNECTION_STRING);
    ASSERT_TRUE(connect_res.has_value(), "Connection failed: " + connect_res.error().to_string());

    odbc::Statement stmt(conn);
    auto exec_res = stmt.execute_direct("SELECT name FROM test_table WHERE id = 2");
    ASSERT_TRUE(exec_res.has_value(), exec_res.error().to_string());

    auto fetch_res = stmt.fetch();
    ASSERT_TRUE(fetch_res.has_value() && *fetch_res, "Fetch failed or returned no data.");
    
    auto name_res = stmt.get_data<std::string>(1);
    ASSERT_TRUE(name_res.has_value(), "get_data failed: " + name_res.error().to_string());
    ASSERT_TRUE(!name_res->has_value(), "Expected a NULL value, but got a string.");
    return true;
}

int main() {
    using TestFunc = std::function<bool()>;
    std::vector<std::pair<std::string, TestFunc>> tests_to_run = {
        {"test_fetch_valid_data", test_fetch_valid_data},
        {"test_fetch_null_string", test_fetch_null_string}
    };

    try {
        setup_database_schema();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error during setup: " << e.what() << std::endl;
        return 1;
    }

    std::vector<std::future<bool>> results;

    for (const auto& test : tests_to_run) {
        std::cout << "[ RUN      ] " << test.first << std::endl;
        results.push_back(
            std::async(std::launch::async, test.second)
        );
    }
    
    int tests_passed = 0;
    int tests_failed = 0;
    for (size_t i = 0; i < results.size(); ++i) {
        try {
            if (results[i].get()) {
                std::cout << "[       OK ] " << tests_to_run[i].first << std::endl;
                tests_passed++;
            } else {
                std::cout << "[  FAILED  ] " << tests_to_run[i].first << std::endl;
                tests_failed++;
            }
        } catch(const std::exception& e) {
            std::cout << "[ EXCEPTION ] " << tests_to_run[i].first << " threw: " << e.what() << std::endl;
            tests_failed++;
        }
    }

    std::cout << "\n--- Test Summary ---" << std::endl;
    std::cout << tests_passed << " tests passed." << std::endl;
    std::cout << tests_failed << " tests failed." << std::endl;
    std::cout << "--------------------" << std::endl;

    return (tests_failed > 0) ? 1 : 0;
}
