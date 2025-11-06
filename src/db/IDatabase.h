#pragma once

#include <string>
#include <vector>
#include <unordered_map>

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

    /// @brief Begin a database transaction
    virtual bool beginTransaction() = 0;
    /// @brief Commit the current database transaction
    virtual bool commit() = 0;
    /// @brief Rollback the current database transaction
    virtual bool rollback() = 0;

    // 新增：获取创建表的 SQL 语句
    virtual std::string getCreateTableSql(const std::string& tableName, const std::string& columns) const = 0;
    // 新增：获取添加列的 SQL 语句
    virtual std::string getAddColumnSql(const std::string& tableName, const std::string& columnName, const std::string& columnDefinition) const = 0;
    // 新增：获取创建索引的 SQL 语句
    virtual std::string getCreateIndexSql(const std::string& indexName, const std::string& tableName, const std::string& columnName) const = 0;
    // 新增：获取插入或忽略冲突的 SQL 语句 (用于 ON CONFLICT / INSERT IGNORE)
    virtual std::string getInsertOrIgnoreSql(const std::string& tableName, const std::string& columns, const std::string& values, const std::string& conflictColumns) const = 0;
    // 新增：获取自增主键的数据库方言定义
    virtual std::string getAutoIncrementPrimaryKeyDefinition() const = 0;

    // 新增：获取 IN 子句的占位符字符串 (例如：?, ?, ?)
    virtual std::string getInClausePlaceholders(size_t count) const = 0;
    
    // 批量接口
    virtual std::unordered_map<std::string, std::vector<std::string>>
    fetchDirectPermissionsOfGroups(const std::vector<std::string>& groupIds) = 0;
};

} // namespace db
} // namespace BA
