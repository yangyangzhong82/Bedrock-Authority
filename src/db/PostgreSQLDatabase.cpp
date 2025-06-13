
#include "db/PostgreSQLDatabase.h"
#include <stdexcept>
#include "ll/api/mod/NativeMod.h"
#include "ll/api/io/Logger.h"
#include <vector>
#include <string> // 确保包含 string 头文件
#include <algorithm> // 确保包含 algorithm 头文件
#include <functional> // 确保包含 functional 头文件

using namespace std; // 引入 std 命名空间
using namespace ll::mod; // 引入 ll::mod 命名空间

// Helper: replace '?' placeholders with '$n' for PostgreSQL
static string replacePlaceholders(const string& sql) {
    string result;
    result.reserve(sql.size());
    int index = 1;
    for (char c : sql) {
        if (c == '?') {
            result += '$' + to_string(index++);
        } else {
            result += c;
        }
    }
    return result;
}

namespace BA {
namespace db {

PostgreSQLDatabase::PostgreSQLDatabase(const string& host,
                                       const string& user,
                                       const string& password,
                                       const string& database,
                                       unsigned int port)
    : conn_(nullptr) {
    auto& logger = NativeMod::current()->getLogger();
    logger.info("正在初始化 PostgreSQL 连接到 %s:%u 数据库=%s 用户=%s", host.c_str(), port, database.c_str(), user.c_str());

    string conninfo = "host=" + host + " port=" + to_string(port) + " dbname=" + database + " user=" + user + " password=" + password;
    conn_ = PQconnectdb(conninfo.c_str());

    if (PQstatus(conn_) != CONNECTION_OK) {
        string err = PQerrorMessage(conn_);
        logger.error("连接到 PostgreSQL 失败: %s", err.c_str());
        PQfinish(conn_);
        throw runtime_error("连接到 PostgreSQL 失败: " + err);
    }
    logger.info("成功连接到 PostgreSQL");
}

PostgreSQLDatabase::~PostgreSQLDatabase() {
    close();
}

bool PostgreSQLDatabase::execute(const string& sql) {
    auto& logger = NativeMod::current()->getLogger();
    logger.debug("PostgreSQL 执行: {}", sql);

    PGresult* res = PQexec(conn_, sql.c_str());
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        string error_msg = PQerrorMessage(conn_);
        logger.error("PostgreSQL 执行错误: {}", error_msg);
        PQclear(res);
        return false;
    }
    PQclear(res);
    logger.debug("PostgreSQL 执行成功");
    return true;
}

vector<vector<string>> PostgreSQLDatabase::query(const string& sql) {
    auto& logger = NativeMod::current()->getLogger();
    logger.debug("PostgreSQL 查询: {}", sql);

    vector<vector<string>> result;
    PGresult* res = PQexec(conn_, sql.c_str());

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        string error_msg = PQerrorMessage(conn_);
        logger.error("PostgreSQL 查询错误: {}", error_msg);
        PQclear(res);
        return result;
    }

    int nFields = PQnfields(res);
    int nRows = PQntuples(res);
    for (int i = 0; i < nRows; i++) {
        vector<string> row;
        for (int j = 0; j < nFields; j++) {
            row.push_back(PQgetvalue(res, i, j));
        }
        result.push_back(std::move(row));
    }

    PQclear(res);
    logger.debug("PostgreSQL 查询返回 {} 行", result.size());
    return result;
}

void PostgreSQLDatabase::close() {
    auto& logger = NativeMod::current()->getLogger();
    if (conn_) {
        logger.info("正在关闭 PostgreSQL 连接");
        PQfinish(conn_);
        conn_ = nullptr;
        logger.info("PostgreSQL 连接已关闭");
    }
}

DatabaseType PostgreSQLDatabase::getType() const {
    return DatabaseType::PostgreSQL;
}

bool PostgreSQLDatabase::executePrepared(const string& sql, const vector<string>& params) {
    auto& logger = NativeMod::current()->getLogger();
string processedSql = replacePlaceholders(sql);
logger.debug("PostgreSQL 执行预处理语句: {}", processedSql);

    vector<const char*> paramValues;
    vector<int> paramLengths;
    vector<int> paramFormats;

    for (const auto& param : params) {
        paramValues.push_back(param.c_str());
        paramLengths.push_back(static_cast<int>(param.length()));
        paramFormats.push_back(0); // 0 for text format
    }

PGresult* res = PQexecParams(
        conn_,
        processedSql.c_str(),
        static_cast<int>(params.size()),
        nullptr, // paramTypes[] - infer from usage
        paramValues.data(),
        paramLengths.data(),
        paramFormats.data(),
        0        // resultFormat - 0 for text
    );

    if (PQresultStatus(res) != PGRES_COMMAND_OK && PQresultStatus(res) != PGRES_TUPLES_OK) {
        string error_msg = PQerrorMessage(conn_);
        logger.error("PostgreSQL 执行预处理语句失败: {}. 语句: {}", error_msg, sql);
        PQclear(res);
        return false;
    }

    PQclear(res);
    logger.debug("PostgreSQL 执行预处理语句成功");
    return true;
}

vector<vector<string>> PostgreSQLDatabase::queryPrepared(const string& sql, const vector<string>& params) {
    auto& logger = NativeMod::current()->getLogger();
string processedSql = replacePlaceholders(sql);
logger.debug("PostgreSQL 查询预处理语句: {}", processedSql);

    vector<vector<string>> result;
    vector<const char*> paramValues;
    vector<int> paramLengths;
    vector<int> paramFormats;

    for (const auto& param : params) {
        paramValues.push_back(param.c_str());
        paramLengths.push_back(static_cast<int>(param.length()));
        paramFormats.push_back(0); // 0 for text format
    }

PGresult* res = PQexecParams(
        conn_,
        processedSql.c_str(),
        static_cast<int>(params.size()),
        nullptr, // paramTypes[] - infer from usage
        paramValues.data(),
        paramLengths.data(),
        paramFormats.data(),
        0        // resultFormat - 0 for text
    );

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        string error_msg = PQerrorMessage(conn_);
        logger.error("PostgreSQL 查询预处理语句失败: {}. 语句: {}", error_msg, sql);
        PQclear(res);
        return result;
    }

    int nFields = PQnfields(res);
    int nRows = PQntuples(res);
    for (int i = 0; i < nRows; i++) {
        vector<string> row;
        row.reserve(nFields);
        for (int j = 0; j < nFields; j++) {
            char* val = PQgetvalue(res, i, j);
            if (val) {
                row.push_back(val);
            } else {
                row.push_back(""); // Handle NULL values
            }
        }
        result.push_back(std::move(row));
    }

    PQclear(res);
    logger.debug("PostgreSQL 查询预处理语句返回 {} 行", result.size());
    return result;
}

// 新增：获取创建表的 SQL 语句
string PostgreSQLDatabase::getCreateTableSql(const string& tableName, const string& columns) const {
    return "CREATE TABLE IF NOT EXISTS " + tableName + " (" + columns + ");";
}

// 新增：获取添加列的 SQL 语句
string PostgreSQLDatabase::getAddColumnSql(const string& tableName, const string& columnName, const string& columnDefinition) const {
    return "ALTER TABLE " + tableName + " ADD COLUMN " + columnName + " " + columnDefinition + ";";
}

// 新增：获取创建索引的 SQL 语句
string PostgreSQLDatabase::getCreateIndexSql(const string& indexName, const string& tableName, const string& columnName) const {
    return "CREATE INDEX IF NOT EXISTS " + indexName + " ON " + tableName + " (" + columnName + ");";
}

// 新增：获取插入或忽略冲突的 SQL 语句 (用于 ON CONFLICT)
string PostgreSQLDatabase::getInsertOrIgnoreSql(const string& tableName, const string& columns, const string& values, const string& conflictColumns) const {
    // PostgreSQL uses ON CONFLICT (column) DO NOTHING
    return "INSERT INTO " + tableName + " (" + columns + ") VALUES (" + values + ") ON CONFLICT (" + conflictColumns + ") DO NOTHING;";
}

// 新增：获取自增主键的数据库方言定义
string PostgreSQLDatabase::getAutoIncrementPrimaryKeyDefinition() const {
    return "SERIAL PRIMARY KEY";
}

} // namespace db
} // namespace BA
