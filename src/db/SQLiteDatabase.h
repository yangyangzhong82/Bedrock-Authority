#pragma once

#include "db/IDatabase.h"
#include <sqlite3.h>
#include <string>
#include <vector>

namespace BA {
namespace db {

class SQLiteDatabase : public IDatabase {
public:
    explicit SQLiteDatabase(const std::string& dbPath);
    ~SQLiteDatabase() override;

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

private:
    sqlite3* db_;
};

} // namespace db
} // namespace BA
