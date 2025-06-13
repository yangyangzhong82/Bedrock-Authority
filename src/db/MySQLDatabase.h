#pragma once

#include "db/IDatabase.h"
#include <mysql.h>
#include <string>
#include <vector>

namespace BA {
namespace db {

class MySQLDatabase : public IDatabase {
public:
    MySQLDatabase(const std::string& host,
                  const std::string& user,
                  const std::string& password,
                  const std::string& database,
                  unsigned int port = 3306);
    ~MySQLDatabase() override;

    bool execute(const std::string& sql) override;
    std::vector<std::vector<std::string>> query(const std::string& sql) override;
    bool executePrepared(const std::string& sql, const std::vector<std::string>& params) override;
    std::vector<std::vector<std::string>> queryPrepared(const std::string& sql, const std::vector<std::string>& params) override;
    void close() override;

    virtual DatabaseType getType() const override;

    // 新增：获取创建表的 SQL 语句
    std::string getCreateTableSql(const std::string& tableName, const std::string& columns) const override;
    // 新增：获取添加列的 SQL 语句
    std::string getAddColumnSql(const std::string& tableName, const std::string& columnName, const std::string& columnDefinition) const override;
    // 新增：获取创建索引的 SQL 语句
    std::string getCreateIndexSql(const std::string& indexName, const std::string& tableName, const std::string& columnName) const override;
    // 新增：获取插入或忽略冲突的 SQL 语句 (用于 ON CONFLICT / INSERT IGNORE)
    std::string getInsertOrIgnoreSql(const std::string& tableName, const std::string& columns, const std::string& values, const std::string& conflictColumns) const override;
    // 新增：获取自增主键的数据库方言定义
    std::string getAutoIncrementPrimaryKeyDefinition() const override;

    // IDatabase 事务接口实现
    bool beginTransaction() override;
    bool commit() override;
    bool rollback() override;

private:
    MYSQL* conn_;
};

} // namespace db
} // namespace BA
