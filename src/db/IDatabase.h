#pragma once

#include <string>
#include <vector>

namespace BA {
namespace db {

enum class DatabaseType {
    Unknown,
    SQLite,
    MySQL,
    PostgreSQL
};

class IDatabase {
public:
    virtual ~IDatabase() = default;
    virtual DatabaseType getType() const = 0;
    /// @brief Execute a SQL statement without expecting a result set (Use executePrepared for security)
    virtual bool execute(const std::string& sql) = 0;
    /// @brief Execute a SQL query and return rows of columns (Use queryPrepared for security)
    virtual std::vector<std::vector<std::string>> query(const std::string& sql) = 0;

    /// @brief Execute a prepared SQL statement without expecting a result set
    /// @param sql The SQL statement with placeholders (e.g., ?)
    /// @param params The parameters to bind to the placeholders
    /// @return True if execution was successful, false otherwise
    virtual bool executePrepared(const std::string& sql, const std::vector<std::string>& params) = 0;

    /// @brief Execute a prepared SQL query and return rows of columns
    /// @param sql The SQL query with placeholders (e.g., ?)
    /// @param params The parameters to bind to the placeholders
    /// @return A vector of rows, where each row is a vector of column strings
    virtual std::vector<std::vector<std::string>> queryPrepared(const std::string& sql, const std::vector<std::string>& params) = 0;

    /// @brief Close the database connection
    virtual void close() = 0;
};

} // namespace db
} // namespace my_mod
