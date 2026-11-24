#include "db/SQLiteDatabase.h"
#include "ll/api/io/Logger.h"
#include "ll/api/mod/NativeMod.h"
#include <stdexcept>
#include <string> // 确保包含 string
#include <vector> // 确保包含 vector


using namespace std;     // 引入 std 命名空间
using namespace ll::mod; // 引入 ll::mod 命名空间

namespace BA {
namespace db {

SQLiteDatabase::SQLiteDatabase(const std::string& dbPath) : db_(nullptr) {
    auto& logger = NativeMod::current()->getLogger();
    logger.info("正在打开 SQLite 数据库: {}", dbPath);
    if (sqlite3_open(dbPath.c_str(), &db_) != SQLITE_OK) {
        string err(sqlite3_errmsg(db_));
        logger.error("打开 SQLite 数据库失败: {}", err);
        sqlite3_close(db_);
        throw runtime_error("打开 SQLite 数据库失败: " + err);
    }
    if (!execute("PRAGMA busy_timeout = 5000;")
        || !execute("PRAGMA foreign_keys = ON;")
        || !execute("PRAGMA journal_mode = WAL;")) {
        close();
        throw runtime_error("SQLite 连接设置失败");
    }
    logger.info("SQLite 数据库已成功打开");
}

SQLiteDatabase::~SQLiteDatabase() { close(); }

bool SQLiteDatabase::execute(const std::string& sql) {
    std::lock_guard lock(connectionMutex_);
    auto& logger = NativeMod::current()->getLogger();
    logger.debug("正在执行 SQL: {}", sql);
    char* errMsg = nullptr;
    int   rc     = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        string err_msg(errMsg ? errMsg : "未知错误");
        if (errMsg) {
            sqlite3_free(errMsg);
        }

        // 检查可忽略的 SQLite 错误
        // 例如，在 ALTER TABLE ... ADD COLUMN 时如果列已存在，会返回 SQLITE_ERROR，错误信息包含 "duplicate column name"
        // 另一个常见的可忽略错误是尝试创建已存在的表 (SQLITE_CONSTRAINT, SQLITE_ERROR)
        if ((rc == SQLITE_ERROR && err_msg.find("duplicate column name") != string::npos
             && sql.find("ALTER TABLE") != string::npos)
            || (rc == SQLITE_CONSTRAINT && err_msg.find("UNIQUE constraint failed") != string::npos)
            || (rc == SQLITE_ERROR && err_msg.find("table") != string::npos
                && err_msg.find("already exists") != string::npos && sql.find("CREATE TABLE") != string::npos)
            || (rc == SQLITE_ERROR && err_msg.find("index") != string::npos
                && err_msg.find("already exists") != string::npos && sql.find("CREATE INDEX") != string::npos)) {
            logger.warn("SQLite 执行警告 (已忽略): {}. 语句: {}", err_msg, sql);
            return true; // 视为成功，因为是可忽略的错误
        } else {
            logger.error("SQLite 执行错误 (代码 {}): {}. 语句: {}", rc, err_msg, sql);
            return false;
        }
    }
    logger.debug("SQL 执行成功");
    return true;
}

std::vector<std::vector<std::string>> SQLiteDatabase::query(const std::string& sql) {
    std::lock_guard lock(connectionMutex_);
    auto& logger = NativeMod::current()->getLogger();
    logger.debug("正在查询 SQL: {}", sql);
    sqlite3_stmt*          stmt = nullptr;
    vector<vector<string>> result;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        logger.error("SQLite 准备错误: {}", sqlite3_errmsg(db_));
        return result;
    }
    int cols = sqlite3_column_count(stmt);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        vector<string> row;
        for (int i = 0; i < cols; ++i) {
            const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
            row.push_back(text ? text : "");
        }
        result.push_back(std::move(row));
    }
    sqlite3_finalize(stmt);
    logger.debug("查询返回 {} 行", result.size());
    return result;
}

void SQLiteDatabase::close() {
    std::lock_guard lock(connectionMutex_);
    auto& logger = NativeMod::current()->getLogger();
    if (db_) {
    logger.info("正在关闭 SQLite 数据库");
        sqlite3_close(db_);
        db_ = nullptr;
    logger.info("SQLite 数据库已关闭");
    }
}

bool SQLiteDatabase::executePrepared(const std::string& sql, const SqlParams& params) {
    std::lock_guard lock(connectionMutex_);
    auto& logger = NativeMod::current()->getLogger();
    logger.debug("正在执行预处理 SQL: {}", sql);
    sqlite3_stmt* stmt = nullptr;

    // 准备语句
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        logger.error("SQLite 准备错误: {}", sqlite3_errmsg(db_));
        return false;
    }

    // 绑定参数
    int index = 1; // SQLite 绑定索引从 1 开始
    for (const auto& param : params) {
        int rc = param ? sqlite3_bind_text(stmt, index, param->c_str(), -1, SQLITE_TRANSIENT)
                       : sqlite3_bind_null(stmt, index);
        if (rc != SQLITE_OK) {
            logger.error("SQLite 绑定错误: {}", sqlite3_errmsg(db_));
            sqlite3_finalize(stmt);
            return false;
        }
        ++index;
    }

    // 执行语句
    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        logger.error("SQLite 执行预处理语句错误: {}", sqlite3_errmsg(db_));
        sqlite3_finalize(stmt);
        return false;
    }

    lastWriteChanged_ = sqlite3_changes(db_) > 0;
    sqlite3_finalize(stmt);
    logger.debug("预处理 SQL 执行成功");
    return true;
}

std::vector<std::vector<std::string>>
SQLiteDatabase::queryPrepared(const std::string& sql, const SqlParams& params) {
    std::lock_guard lock(connectionMutex_);
    lastQuerySucceeded_ = false;
    auto& logger = NativeMod::current()->getLogger();
    logger.debug("正在查询预处理 SQL: {}", sql);
    sqlite3_stmt*          stmt = nullptr;
    vector<vector<string>> result;

    // 准备语句
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        logger.error("SQLite 准备错误: {}", sqlite3_errmsg(db_));
        return result; // 准备错误时返回空结果
    }

    // 绑定参数
    int index = 1; // SQLite 绑定索引从 1 开始
    for (const auto& param : params) {
        int rc = param ? sqlite3_bind_text(stmt, index, param->c_str(), -1, SQLITE_TRANSIENT)
                       : sqlite3_bind_null(stmt, index);
        if (rc != SQLITE_OK) {
            logger.error("SQLite 绑定错误: {}", sqlite3_errmsg(db_));
            sqlite3_finalize(stmt);
            return result; // 绑定错误时返回空结果
        }
        ++index;
    }

    // 执行并获取行
    int cols = sqlite3_column_count(stmt);
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        vector<string> row;
        row.reserve(cols); // 为提高效率预留空间
        for (int i = 0; i < cols; ++i) {
            const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
            row.push_back(text ? text : "");
        }
        result.push_back(std::move(row));
    }

    // 检查执行步骤中的错误
    if (rc != SQLITE_DONE) {
        logger.error("SQLite 查询预处理步骤错误: {}", sqlite3_errmsg(db_));
        // 结果可能部分填充，决定是否应清除或返回部分结果
        // 目前，返回错误发生前收集到的任何内容
    }


    // 终结语句
    sqlite3_finalize(stmt);
    lastQuerySucceeded_ = rc == SQLITE_DONE;
    logger.debug("预处理查询返回 {} 行", result.size());
    return result;
}


DatabaseType SQLiteDatabase::getType() const { return DatabaseType::SQLite; }

// 新增：获取创建表的 SQL 语句
std::string SQLiteDatabase::getCreateTableSql(const std::string& tableName, const std::string& columns) const {
    return "CREATE TABLE IF NOT EXISTS " + tableName + " (" + columns + ");";
}

// 新增：获取添加列的 SQL 语句
std::string SQLiteDatabase::getAddColumnSql(
    const std::string& tableName,
    const std::string& columnName,
    const std::string& columnDefinition
) const {
    return "ALTER TABLE " + tableName + " ADD COLUMN " + columnName + " " + columnDefinition + ";";
}

// 新增：获取创建索引的 SQL 语句
std::string SQLiteDatabase::getCreateIndexSql(
    const std::string& indexName,
    const std::string& tableName,
    const std::string& columnName
) const {
    return "CREATE INDEX IF NOT EXISTS " + indexName + " ON " + tableName + " (" + columnName + ");";
}

// 新增：获取插入或忽略冲突的 SQL 语句 (用于 ON CONFLICT)
std::string SQLiteDatabase::getInsertOrIgnoreSql(
    const std::string& tableName,
    const std::string& columns,
    const std::string& values,
    const std::string& conflictColumns
) const {
    return "INSERT INTO " + tableName + " (" + columns + ") VALUES (" + values + ") ON CONFLICT (" + conflictColumns
         + ") DO NOTHING;";
}

// 新增：获取自增主键的数据库方言定义
std::string SQLiteDatabase::getAutoIncrementPrimaryKeyDefinition() const { return "INTEGER PRIMARY KEY AUTOINCREMENT"; }

std::string SQLiteDatabase::getInClausePlaceholders(size_t count) const {
    if (count == 0) {
        return "";
    }
    std::string placeholders;
    for (size_t i = 0; i < count; ++i) {
        placeholders += "?";
        if (i < count - 1) {
            placeholders += ", ";
        }
    }
    return placeholders;
}

bool SQLiteDatabase::beginTransaction() { return execute("BEGIN TRANSACTION;"); }

bool SQLiteDatabase::commit() { return execute("COMMIT;"); }

bool SQLiteDatabase::rollback() { return execute("ROLLBACK;"); }

// 批量接口实现
std::unordered_map<std::string, std::vector<std::string>>
SQLiteDatabase::fetchDirectPermissionsOfGroups(const std::vector<std::string>& groupIds) {
    std::unordered_map<std::string, std::vector<std::string>> result;
    if (groupIds.empty()) {
        return result;
    }

    std::string sql = "SELECT group_id, permission_rule FROM group_permissions WHERE group_id IN ("
                    + getInClausePlaceholders(groupIds.size()) + ");";

    auto rows = queryPrepared(sql, SqlParams(groupIds.begin(), groupIds.end()));
    for (const auto& row : rows) {
        if (row.size() == 2) {
            result[row[0]].push_back(row[1]);
        }
    }
    return result;
}

} // namespace db
} // namespace BA
