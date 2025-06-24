#ifndef MODERN_ODBC_CONNECTION_POOL_H
#define MODERN_ODBC_CONNECTION_POOL_H

/**
 * @file connection_pool.h
 * @brief Provides a thread-safe, thread-local connection pool for ODBC connections.
 *
 * This header-only library defines a simple and efficient connection pool
 * that is private to each thread, avoiding the need for mutexes and other
 * synchronization primitives for connection management.
 */

#include "odbc_wrapper.h"
#include <map>
#include <string>
#include <string_view>
#include <stdexcept>
#include <utility>
#include <iostream>
#include <thread>
#include <functional> // Required for std::less<>
#include <format>     // Required for std::format
#include <sstream>    // Required for std::stringstream

/**
 * @class ConnectionPoolError
 * @brief Custom exception for errors originating from the connection pool.
 *
 * This exception is thrown when a connection cannot be established or another
 * pool-specific error occurs.
 */
class ConnectionPoolError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};


/**
 * @class ThreadLocalConnectionPool
 * @brief Manages a pool of named ODBC connections private to a single thread.
 *
 * This class is intended to be used as a thread_local variable. It creates
 * and stores connections on demand, reusing them for subsequent requests
 * within the same thread. It is not meant to be instantiated directly by client code;
 * rather, it should be accessed via the getThreadLocalConnection() function.
 */
class ThreadLocalConnectionPool {
private:
    /**
     * @var env_
     * @brief The single ODBC environment handle for this thread's pool.
     * All connections in this pool are created from this environment.
     */
    odbc::Environment env_;
    
    /**
     * @var connections_
     * @brief The map storing named connections for this thread.
     * Using std::less<> enables heterogeneous lookup, allowing find() with string_view
     * for a minor performance optimization.
     */
    std::map<std::string, odbc::Connection, std::less<>> connections_;

public:
    /**
     * @brief Gets a connection from the pool by its alias.
     *
     * If a connection with the given alias does not exist for the current thread,
     * it will be created, connected, and stored for future use. Otherwise, the
     * existing, cached connection is returned.
     *
     * @param alias A unique string_view to identify the connection (e.g., "DB_PRIMARY").
     * @param connection_string The full ODBC connection string to use if a new connection is needed.
     * @return A reference to the active odbc::Connection object.
     * @throws ConnectionPoolError if a new connection is required but fails to be established.
     */
    odbc::Connection& getConnection(std::string_view alias, std::string_view connection_string) {
        // Use C++17 "if with initializer" to scope 'it' to the if/else blocks.
        // Thanks to heterogeneous lookup, we can use a string_view to find without allocating.
        if (auto it = connections_.find(alias); it == connections_.end()) {
            // Connection not found, create a new one.
            
            // Convert thread ID to string for formatting
            std::stringstream ss;
            ss << std::this_thread::get_id();
            
            // Use std::format and a single stream insertion for thread-safe logging.
            std::cerr << std::format("[Thread {}] Creating new connection for alias '{}'.\n", ss.str(), alias);
            
            odbc::Connection new_conn(env_);

            auto connect_res = new_conn.driver_connect(connection_string);
            if (!connect_res) {
                // Throw the specific exception type, also using std::format.
                throw ConnectionPoolError(std::format("Failed to establish connection for alias '{}': {}", alias, connect_res.error().to_string()));
            }

            // Emplace the new connection and return a reference to it.
            // We create a std::string from the alias here, as this only happens once on creation.
            auto [inserted_it, success] = connections_.emplace(std::string(alias), std::move(new_conn));
            return inserted_it->second;
        } else {
            // Connection found, return the existing one.
            return it->second;
        }
    }
};


/**
 * @brief Provides access to a thread-local connection pool.
 *
 * This function encapsulates the thread_local pool instance, making it the primary
 * access point for obtaining a database connection. The first time any thread calls this
 * function, a new pool is created for that thread. Subsequent calls from the same
 * thread will reuse the existing pool.
 *
 * @param alias A unique string_view to identify the connection (e.g., "DB_PRIMARY").
 * @param connection_string The full ODBC connection string.
 * @return A reference to the active odbc::Connection object for that thread.
 * @throws ConnectionPoolError if a new connection is required but fails to be established.
 */
inline odbc::Connection& getThreadLocalConnection(std::string_view alias, std::string_view connection_string) {
    // The pool is initialized once per thread and persists for the thread's lifetime.
    thread_local ThreadLocalConnectionPool pool;
    return pool.getConnection(alias, connection_string);
}

#endif // MODERN_ODBC_CONNECTION_POOL_H
